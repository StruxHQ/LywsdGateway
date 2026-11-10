#include "SensorManager.h"
#include "StruxProvider.h"
#include "SettingsManager.h"
#include "CommandManager.h"
#include "TelemetryManager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include <cctype>
#include <cstdio>
#include <cstring>

SensorManager* SensorManager::instance_ = nullptr;

namespace {

// Names live in their own NVS namespace, one key per sensor: the MAC as 12
// hex digits, which fits NVS's 15-character key limit.
constexpr const char* NAME_NAMESPACE = "sensornames";

// Passive scan, 30 ms of every 60 ms. The rest of the air time is WiFi's: on a
// single-radio chip the coexistence arbiter shares the antenna, and a full
// window would starve the relay link.
constexpr uint16_t SCAN_INTERVAL = 0x60;   // 0.625 ms units: 60 ms
constexpr uint16_t SCAN_WINDOW = 0x30;     // 30 ms

constexpr int REPORT_TASK_STACK = 4096;
constexpr TickType_t REPORT_PERIOD = pdMS_TO_TICKS(1000);

void MacKey(const uint8_t mac[6], char* out, size_t size)
{
    snprintf(out, size, "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

} // namespace

SensorManager::SensorManager(AppProvider& app)
    : app_(app)
{
}

void SensorManager::Init()
{
    auto initAttempt = initState_.TryBeginInit();
    if (!initAttempt)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    StruxProvider& strux = app_.getStrux();
    strux.getSettingsManager().Register({ &reportInterval_ });
    strux.getCommandManager().Register(this, { &listCommand_, &nameCommand_ });

    instance_ = this;
    if (nimble_port_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "NimBLE did not start; no sensors will be heard");
        return;
    }
    ble_hs_cfg.sync_cb = &SensorManager::OnBleSync;
    ble_hs_cfg.reset_cb = &SensorManager::OnBleReset;
    nimble_port_freertos_init(&SensorManager::BleHostTask);

    reportTask_.Init("sensors", tskIDLE_PRIORITY + 2, REPORT_TASK_STACK);
    reportTask_.SetHandler([this] { ReportLoop(); });
    reportTask_.Run();

    initAttempt.SetReady();
    ESP_LOGI(TAG, "Initialized");
}

// ──────────────────────────────────────────────────────────────
// BLE
// ──────────────────────────────────────────────────────────────

void SensorManager::BleHostTask(void*)
{
    nimble_port_run();   // returns only when nimble_port_stop() is called
    nimble_port_freertos_deinit();
}

void SensorManager::OnBleReset(int reason)
{
    ESP_LOGW(TAG, "BLE host reset, reason %d", reason);
}

void SensorManager::OnBleSync()
{
    if (ble_hs_util_ensure_addr(0) != 0 ||
        ble_hs_id_infer_auto(0, &instance_->ownAddressType_) != 0)
    {
        ESP_LOGE(TAG, "No usable BLE address; not scanning");
        return;
    }
    instance_->StartScan();
}

void SensorManager::StartScan()
{
    ble_gap_disc_params params = {};
    params.itvl = SCAN_INTERVAL;
    params.window = SCAN_WINDOW;
    params.passive = 1;
    // Duplicates are the point: a sensor repeats its advertisement with new
    // values in it, and the controller's filter would swallow those.
    params.filter_duplicates = 0;

    const int rc = ble_gap_disc(ownAddressType_, BLE_HS_FOREVER, &params,
                                &SensorManager::OnGapEvent, this);
    if (rc != 0)
        ESP_LOGE(TAG, "Could not start scanning: %d", rc);
    else
        ESP_LOGI(TAG, "Scanning for sensors");
}

int SensorManager::OnGapEvent(ble_gap_event* event, void* arg)
{
    auto* self = static_cast<SensorManager*>(arg);
    switch (event->type)
    {
    case BLE_GAP_EVENT_DISC:
        self->OnAdvertisement(event->disc.addr.val, event->disc.rssi,
                              event->disc.data, event->disc.length_data);
        break;
#if MYNEWT_VAL(BLE_EXT_ADV)
    case BLE_GAP_EVENT_EXT_DISC:
        self->OnAdvertisement(event->ext_disc.addr.val, event->ext_disc.rssi,
                              event->ext_disc.data, event->ext_disc.length_data);
        break;
#endif
    case BLE_GAP_EVENT_DISC_COMPLETE:
        // A scan "forever" still ends if the host is reset under it.
        self->StartScan();
        break;
    default:
        break;
    }
    return 0;
}

void SensorManager::OnAdvertisement(const uint8_t littleEndianAddress[6], int8_t rssi,
                                    const uint8_t* data, size_t length)
{
    ClimateReading reading;
    if (!ParseClimateAdvertisement(data, length, reading))
        return;

    uint8_t mac[6];
    for (int i = 0; i < 6; i++)
        mac[i] = littleEndianAddress[5 - i];

    const int64_t now = esp_timer_get_time();
    LOCK(mutex_);
    Slot& slot = FindOrClaimSlot(mac, now);

    // A freshly claimed slot has never held a reading.
    if (slot.sensor.reading.format == ClimateFormat::None)
    {
        char text[18];
        FormatMac(mac, text, sizeof(text));
        ESP_LOGI(TAG, "New sensor %s: %s%s", text, ClimateFormatName(reading.format),
                 reading.encrypted ? ", encrypted" : "");
    }

    // A sensor repeats each measurement several times; only a new counter is
    // a new reading. Encrypted readings carry nothing to report.
    const bool isNew = !reading.hasCounter || !slot.sensor.reading.hasCounter ||
                       reading.counter != slot.sensor.reading.counter;
    if (isNew && reading.hasTemperature)
        slot.unreported = true;

    MergeClimateReading(slot.sensor.reading, reading);
    slot.sensor.rssi = rssi;
    slot.sensor.lastSeenUs = now;
}

SensorManager::Slot& SensorManager::FindOrClaimSlot(const uint8_t mac[6], int64_t nowUs)
{
    Slot* oldest = &slots_[0];
    for (Slot& slot : slots_)
    {
        if (slot.used && memcmp(slot.sensor.mac, mac, 6) == 0)
            return slot;
    }
    for (Slot& slot : slots_)
    {
        if (!slot.used)
        {
            oldest = &slot;
            break;
        }
        if (slot.sensor.lastSeenUs < oldest->sensor.lastSeenUs)
            oldest = &slot;
    }

    *oldest = Slot{};
    oldest->used = true;
    memcpy(oldest->sensor.mac, mac, 6);
    oldest->sensor.lastSeenUs = nowUs;
    LoadName(mac, oldest->sensor.name, sizeof(oldest->sensor.name));
    return *oldest;
}

// ──────────────────────────────────────────────────────────────
// Reading the table
// ──────────────────────────────────────────────────────────────

size_t SensorManager::Count() const
{
    LOCK(mutex_);
    size_t count = 0;
    for (const Slot& slot : slots_)
        if (slot.used)
            count++;
    return count;
}

bool SensorManager::Get(size_t index, Sensor& out) const
{
    LOCK(mutex_);
    for (const Slot& slot : slots_)
    {
        if (!slot.used)
            continue;
        if (index == 0)
        {
            out = slot.sensor;
            return true;
        }
        index--;
    }
    return false;
}

void SensorManager::FormatMac(const uint8_t mac[6], char* out, size_t size)
{
    snprintf(out, size, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// ──────────────────────────────────────────────────────────────
// Telemetry
// ──────────────────────────────────────────────────────────────

void SensorManager::ReportLoop()
{
    while (true)
    {
        vTaskDelay(REPORT_PERIOD);

        const int64_t intervalUs = static_cast<int64_t>(reportInterval_.Get()) * 1000000;
        const int64_t now = esp_timer_get_time();

        for (Slot& slot : slots_)
        {
            Sensor due;
            {
                LOCK(mutex_);
                const bool waited = slot.lastReportedUs == 0 ||
                                    now - slot.lastReportedUs >= intervalUs;
                if (!slot.used || !slot.unreported || !waited)
                    continue;
                slot.unreported = false;
                slot.lastReportedUs = now;
                due = slot.sensor;
            }
            // Outside the lock: a Point is built on this task's stack and the
            // advertisements keep arriving meanwhile.
            Report(due);
        }
    }
}

void SensorManager::Report(const Sensor& sensor)
{
    char mac[18];
    FormatMac(sensor.mac, mac, sizeof(mac));
    const ClimateReading& r = sensor.reading;

    auto point = app_.getStrux().getTelemetryManager().Measure("climate");
    point.Tag("sensor", mac);
    // Influx refuses an empty tag value, so an unnamed sensor has no name tag.
    if (sensor.name[0] != '\0')
        point.Tag("name", sensor.name);
    point.Tag("format", ClimateFormatName(r.format));

    point.Field("temperature", r.temperatureCenti / 100.0);
    if (r.hasHumidity)
        point.Field("humidity", r.humidityCenti / 100.0);
    if (r.hasBatteryPercent)
        point.Field("battery", static_cast<int32_t>(r.batteryPercent));
    if (r.hasBatteryMillivolts)
        point.Field("voltage", r.batteryMillivolts / 1000.0);
    point.Field("rssi", static_cast<int32_t>(sensor.rssi));
    point.Commit();
}

// ──────────────────────────────────────────────────────────────
// Names
// ──────────────────────────────────────────────────────────────

bool SensorManager::LoadName(const uint8_t mac[6], char* out, size_t size)
{
    out[0] = '\0';
    nvs_handle_t handle;
    if (nvs_open(NAME_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
        return false;   // nothing has ever been named

    char key[13];
    MacKey(mac, key, sizeof(key));
    size_t length = size;
    const bool found = nvs_get_str(handle, key, out, &length) == ESP_OK;
    nvs_close(handle);
    if (!found)
        out[0] = '\0';
    return found;
}

bool SensorManager::SaveName(const uint8_t mac[6], const char* name)
{
    nvs_handle_t handle;
    if (nvs_open(NAME_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
        return false;

    char key[13];
    MacKey(mac, key, sizeof(key));
    esp_err_t err;
    if (name[0] == '\0')
    {
        err = nvs_erase_key(handle, key);
        if (err == ESP_ERR_NVS_NOT_FOUND)
            err = ESP_OK;
    }
    else
    {
        err = nvs_set_str(handle, key, name);
    }
    if (err == ESP_OK)
        err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

bool SensorManager::ParseMac(const char* text, uint8_t mac[6])
{
    // Accepts "A4:C1:38:11:22:33", "a4-c1-38-11-22-33" and "A4C138112233".
    int digits = 0;
    for (const char* p = text; *p != '\0'; p++)
    {
        if (*p == ':' || *p == '-')
            continue;
        if (!isxdigit(static_cast<unsigned char>(*p)) || digits >= 12)
            return false;
        const int nibble = isdigit(static_cast<unsigned char>(*p))
                               ? *p - '0'
                               : tolower(static_cast<unsigned char>(*p)) - 'a' + 10;
        if (digits % 2 == 0)
            mac[digits / 2] = static_cast<uint8_t>(nibble << 4);
        else
            mac[digits / 2] |= static_cast<uint8_t>(nibble);
        digits++;
    }
    return digits == 12;
}

// ──────────────────────────────────────────────────────────────
// Commands
// ──────────────────────────────────────────────────────────────

namespace {

constexpr CommandArg<const char*> macArg{
    "mac", "The sensor's MAC address, as 'sensor list' reports it.", 18 };
constexpr CommandArg<const char*> nameArg{
    "name", "What to call it - becomes the 'name' tag on its telemetry. Empty "
            "removes the name.", SensorManager::NAME_LENGTH - 1 };

} // namespace

CommandEntry SensorManager::listCommand_{
    "sensor list", &InvokeCommand<&SensorManager::Cmd_List>,
    "Every climate sensor heard over BLE: MAC, name, firmware format, whether its "
    "readings are encrypted (stock Xiaomi firmware), the latest values, signal "
    "strength and how long ago it was heard."
};

CommandEntry SensorManager::nameCommand_{
    "sensor name", &InvokeCommand<&SensorManager::Cmd_Name>,
    "Give a sensor a name, stored on the device. It shows on the display and as "
    "the 'name' tag on the sensor's telemetry from the next report on.",
    { &macArg, &nameArg }
};

CommandResult SensorManager::Cmd_List(CommandContext& ctx)
{
    const int64_t now = esp_timer_get_time();

    auto resp = ctx.reply.object();
    auto list = resp.array("sensors");
    Sensor sensor;
    for (size_t i = 0; Get(i, sensor); i++)
    {
        char mac[18];
        FormatMac(sensor.mac, mac, sizeof(mac));
        const ClimateReading& r = sensor.reading;

        auto item = list.object();
        item.field("mac", mac);
        item.field("name", sensor.name);
        item.field("format", ClimateFormatName(r.format));
        item.field("encrypted", r.encrypted);
        if (r.hasTemperature)
            item.field("temperature", static_cast<float>(r.temperatureCenti / 100.0));
        if (r.hasHumidity)
            item.field("humidity", static_cast<float>(r.humidityCenti / 100.0));
        if (r.hasBatteryPercent)
            item.field("battery", static_cast<uint32_t>(r.batteryPercent));
        if (r.hasBatteryMillivolts)
            item.field("voltage", static_cast<float>(r.batteryMillivolts / 1000.0));
        item.field("rssi", static_cast<int32_t>(sensor.rssi));
        item.field("lastSeen", static_cast<uint32_t>((now - sensor.lastSeenUs) / 1000000));
    }
    return CommandResult::Ok;
}

CommandResult SensorManager::Cmd_Name(CommandContext& ctx)
{
    uint8_t mac[6];
    if (!ParseMac(ctx.arg(macArg), mac))
    {
        auto resp = ctx.reply.object();
        resp.field("ok", false);
        resp.field("error", "Not a MAC address.");
        return CommandResult::Ok;
    }

    const char* name = ctx.arg(nameArg);
    if (!SaveName(mac, name))
    {
        auto resp = ctx.reply.object();
        resp.field("ok", false);
        resp.field("error", "Could not store the name.");
        return CommandResult::Ok;
    }

    {
        LOCK(mutex_);
        for (Slot& slot : slots_)
        {
            if (slot.used && memcmp(slot.sensor.mac, mac, 6) == 0)
                snprintf(slot.sensor.name, sizeof(slot.sensor.name), "%s", name);
        }
    }

    auto resp = ctx.reply.object();
    resp.field("ok", true);
    return CommandResult::Ok;
}
