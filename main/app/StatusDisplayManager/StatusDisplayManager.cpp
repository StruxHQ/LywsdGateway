#include "StatusDisplayManager.h"
#include "BoardContext.h"
#include "StruxProvider.h"
#include "CommandManager.h"
#include "NetworkManager.h"
#include "RelayManager.h"
#include "SensorManager.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>

StatusDisplayManager::StatusDisplayManager(AppProvider& app)
    : app_(app)
{
}

void StatusDisplayManager::Init()
{
    auto initAttempt = initState_.TryBeginInit();
    if (!initAttempt)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    app_.getStrux().getCommandManager().Register(this, { &getCommand_ });

    timer_.SetHandler([this] { OnTick(); });
    timer_.Init("display", pdMS_TO_TICKS(POLL_MS));
    OnTick();
    timer_.Start();

    initAttempt.SetReady();
    ESP_LOGI(TAG, "Initialized");
}

uint8_t StatusDisplayManager::RowCount()
{
    const uint8_t rows = app_.getBoard().GetDisplay().Rows();
    return rows < MAX_ROWS ? rows : MAX_ROWS;
}

void StatusDisplayManager::Compose(Line* lines, uint8_t rows)
{
    for (uint8_t row = 0; row < rows; row++)
        lines[row][0] = '\0';

    NetworkManager& network = app_.getStrux().getNetworkManager();
    const char* wifi = network.IsAccessPoint() ? "AP" : network.HasUpstream() ? "ok" : "--";
    const char* relay = app_.getStrux().getRelayManager().IsConnected() ? "ok" : "--";

    if (rows > 0)
        snprintf(lines[0], sizeof(Line), "WiFi   %s", wifi);
    if (rows > 1)
        snprintf(lines[1], sizeof(Line), "Strux  %s", relay);

    ComposeSensors(lines + STATUS_ROWS, rows > STATUS_ROWS ? rows - STATUS_ROWS : 0);
}

void StatusDisplayManager::ComposeSensors(Line* lines, uint8_t rows)
{
    SensorManager& sensors = app_.getSensorManager();
    const size_t count = sensors.Count();
    if (rows == 0 || count == 0)
        return;

    // More sensors than rows: show them a page at a time. The page follows the
    // clock rather than a counter, so the display and `display get` agree.
    const size_t pages = (count + rows - 1) / rows;
    const size_t page = static_cast<size_t>(esp_timer_get_time() / (PAGE_SECONDS * 1000000LL)) % pages;

    SensorManager::Sensor sensor;
    for (uint8_t row = 0; row < rows; row++)
    {
        if (!sensors.Get(page * rows + row, sensor))
            break;
        FormatSensor(sensor, lines[row]);
    }
}

// Every value here is bounded by the sensor formats, but not in a way GCC can
// see, and cutting a row at the panel's width is what a Line is for anyway.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
void StatusDisplayManager::FormatSensor(const SensorManager::Sensor& sensor, Line& line)
{
    // 12 characters: a 4-character label, the temperature in 5, humidity in 3.
    //   "Woon 21.3 45"   "3344-12.5 80"   "3344 encr."
    char label[5];
    if (sensor.name[0] != '\0')
        snprintf(label, sizeof(label), "%.4s", sensor.name);
    else
        snprintf(label, sizeof(label), "%02X%02X", sensor.mac[4], sensor.mac[5]);

    const ClimateReading& r = sensor.reading;
    if (r.encrypted)
    {
        snprintf(line, sizeof(Line), "%-4s encr.", label);
        return;
    }
    if (!r.hasTemperature)
    {
        snprintf(line, sizeof(Line), "%-4s  --", label);
        return;
    }

    // Integer arithmetic rather than %f: this runs on the timer service task,
    // and float formatting costs stack it has little of.
    const int32_t tenths = (r.temperatureCenti + (r.temperatureCenti >= 0 ? 5 : -5)) / 10;
    const int32_t whole = (tenths < 0 ? -tenths : tenths) / 10;
    const int32_t fraction = (tenths < 0 ? -tenths : tenths) % 10;
    char temperature[16];
    snprintf(temperature, sizeof(temperature), "%s%ld.%ld",
             tenths < 0 ? "-" : "", static_cast<long>(whole), static_cast<long>(fraction));

    if (r.hasHumidity)
        snprintf(line, sizeof(Line), "%-4s%5s %2ld", label, temperature,
                 static_cast<long>((r.humidityCenti + 50) / 100));
    else
        snprintf(line, sizeof(Line), "%-4s%5s", label, temperature);
}
#pragma GCC diagnostic pop

void StatusDisplayManager::OnTick()
{
    const uint8_t rows = RowCount();
    Line wanted[MAX_ROWS];
    Compose(wanted, rows);

    Display& display = app_.getBoard().GetDisplay();
    for (uint8_t row = 0; row < rows; row++)
    {
        if (strcmp(wanted[row], shown_[row]) == 0)
            continue;
        display.WriteLine(row, wanted[row]);
        snprintf(shown_[row], sizeof(Line), "%s", wanted[row]);
    }
}

// ──────────────────────────────────────────────────────────────
// Commands
// ──────────────────────────────────────────────────────────────

CommandEntry StatusDisplayManager::getCommand_{
    "display get", &InvokeCommand<&StatusDisplayManager::Cmd_Get>,
    "Report what the status display shows: its size in characters and the text "
    "of every row, top to bottom."
};

CommandResult StatusDisplayManager::Cmd_Get(CommandContext& ctx)
{
    const uint8_t rows = RowCount();
    Line lines[MAX_ROWS];
    Compose(lines, rows);

    auto resp = ctx.reply.object();
    resp.field("columns", static_cast<uint32_t>(app_.getBoard().GetDisplay().Columns()));
    resp.field("rows", static_cast<uint32_t>(rows));
    auto list = resp.array("lines");
    for (uint8_t row = 0; row < rows; row++)
        list.value(lines[row]);
    return CommandResult::Ok;
}
