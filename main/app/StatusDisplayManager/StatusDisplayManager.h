#pragma once

#include "AppProvider.h"
#include "InitState.h"
#include "CommandEntry.h"
#include "Timer.h"
#include "SensorManager.h"

// ──────────────────────────────────────────────────────────────
// Owns the board's display: one row for WiFi, one for the relay (Strux), and
// the rows below them for the climate sensors SensorManager hears — a page of
// them at a time when there are more sensors than rows.
//
// A board without a screen binds MockDisplay, so this manager runs the same
// everywhere; `display get` reports what the screen would show, which is also
// what the web UI's home page mirrors.
// ──────────────────────────────────────────────────────────────
class StatusDisplayManager
{
    static constexpr const char* TAG = "StatusDisplay";

    // How often the connection state is sampled. A poll, because reading two
    // bools costs nothing and needs no callback registered in the framework.
    static constexpr uint32_t POLL_MS = 500;

    // Enough for any SSD1306 at 6-pixel cells (128 / 6 = 21) and 8 pages.
    static constexpr uint8_t MAX_ROWS = 8;
    static constexpr uint8_t MAX_COLUMNS = 21;

    using Line = char[MAX_COLUMNS + 1];

    // The WiFi and Strux rows; sensors start below them.
    static constexpr uint8_t STATUS_ROWS = 2;

    // How long each page of sensors stays up when they do not all fit.
    static constexpr int64_t PAGE_SECONDS = 4;

public:
    explicit StatusDisplayManager(AppProvider& app);

    StatusDisplayManager(const StatusDisplayManager&) = delete;
    StatusDisplayManager& operator=(const StatusDisplayManager&) = delete;
    StatusDisplayManager(StatusDisplayManager&&) = delete;
    StatusDisplayManager& operator=(StatusDisplayManager&&) = delete;

    void Init();

private:
    AppProvider& app_;
    InitState initState_;
    Timer timer_;

    /// What is on the panel now, so a row is only rewritten when it changed.
    /// Touched only by the timer task.
    Line shown_[MAX_ROWS] = {};

    uint8_t RowCount();

    /// Write the screen's contents into `lines`, one per row. Reads state
    /// only, so the timer and the command may both call it.
    void Compose(Line* lines, uint8_t rows);
    void ComposeSensors(Line* lines, uint8_t rows);
    static void FormatSensor(const SensorManager::Sensor& sensor, Line& line);

    /// Runs on the FreeRTOS timer service task: composes, and writes to the
    /// panel only the rows that changed — normally none.
    void OnTick();

    CommandResult Cmd_Get(CommandContext& ctx);
    static CommandEntry getCommand_;
};
