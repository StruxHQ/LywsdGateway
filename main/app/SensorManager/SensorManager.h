#pragma once

#include "AppProvider.h"
#include "InitState.h"
#include "CommandEntry.h"
#include "TypedSettings.h"
#include "Mutex.h"
#include "Task.h"
#include "ClimateAdvertisement.h"
#include <cstdint>

struct ble_gap_event;

// ──────────────────────────────────────────────────────────────
// Listens for BLE advertisements from climate sensors (LYWSD03MMC and
// friends, see ClimateAdvertisement.h), keeps the latest reading of each, and
// forwards readings to the relay as `climate` telemetry points.
//
// Passive scanning only: nothing connects to a sensor, so a sensor's battery
// pays nothing for being heard. Every sensor in range is reported, tagged with
// its MAC address and, when the user has given it one, its name.
//
// Three tasks touch the table, which is why it sits behind one mutex:
//   • the NimBLE host task writes readings as advertisements arrive,
//   • this manager's own task reads it to send telemetry (a Point needs more
//     stack than the NimBLE or timer tasks should give),
//   • command handlers and the status display read it.
// ──────────────────────────────────────────────────────────────
class SensorManager
{
    static constexpr const char* TAG = "SensorManager";

public:
    static constexpr size_t MAX_SENSORS = 16;
    static constexpr size_t NAME_LENGTH = 24;

    struct Sensor
    {
        /// Most significant byte first — the order a MAC is written in.
        uint8_t mac[6] = {};
        char name[NAME_LENGTH] = {};
        ClimateReading reading;
        int8_t rssi = 0;
        int64_t lastSeenUs = 0;
    };

    explicit SensorManager(AppProvider& app);

    SensorManager(const SensorManager&) = delete;
    SensorManager& operator=(const SensorManager&) = delete;
    SensorManager(SensorManager&&) = delete;
    SensorManager& operator=(SensorManager&&) = delete;

    void Init();

    size_t Count() const;

    /// Copy one sensor out of the table. False past the end. Indices are
    /// stable while the table is not full; they are not identities — the MAC is.
    bool Get(size_t index, Sensor& out) const;

    /// "A4:C1:38:11:22:33" — `out` must hold 18 bytes.
    static void FormatMac(const uint8_t mac[6], char* out, size_t size);

private:
    struct Slot
    {
        bool used = false;
        Sensor sensor;
        /// A reading has arrived that has not been sent as telemetry yet.
        bool unreported = false;
        int64_t lastReportedUs = 0;
    };

    AppProvider& app_;
    InitState initState_;
    mutable Mutex mutex_;
    Slot slots_[MAX_SENSORS];
    Task reportTask_;
    uint8_t ownAddressType_ = 0;

    // NimBLE's sync callback takes no argument, so the instance has to be found
    // somewhere static. There is only ever one SensorManager.
    static SensorManager* instance_;

    static void OnBleSync();
    static void OnBleReset(int reason);
    static void BleHostTask(void* param);
    static int OnGapEvent(ble_gap_event* event, void* arg);

    void StartScan();
    void OnAdvertisement(const uint8_t littleEndianAddress[6], int8_t rssi,
                         const uint8_t* data, size_t length);

    /// The slot for `mac`, or a free one, or the one heard from longest ago.
    /// Caller holds mutex_.
    Slot& FindOrClaimSlot(const uint8_t mac[6], int64_t nowUs);

    void ReportLoop();
    void Report(const Sensor& sensor);

    static bool LoadName(const uint8_t mac[6], char* out, size_t size);
    static bool SaveName(const uint8_t mac[6], const char* name);
    static bool ParseMac(const char* text, uint8_t mac[6]);

    // How often each sensor's reading is sent. Sensors measure far more often
    // than a graph needs; the latest reading wins.
    inline static UInt32Setting reportInterval_{ "sensor.interval", "Sensor Report Interval (s)", 60 };

    CommandResult Cmd_List(CommandContext& ctx);
    CommandResult Cmd_Name(CommandContext& ctx);
    static CommandEntry listCommand_;
    static CommandEntry nameCommand_;
};
