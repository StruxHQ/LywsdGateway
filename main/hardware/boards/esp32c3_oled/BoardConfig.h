#pragma once

#include <cstdint>

// ──────────────────────────────────────────────────────────────
// BoardContext configuration — ESP32-C3 with a 0.42" OLED.
//
// The small C3 board with a 72x40 SSD1306 OLED soldered on: ESP32-C3FH4
// (RISC-V, 4 MB flash, no PSRAM), USB-C wired to the chip's native USB
// Serial/JTAG, ceramic antenna. Pin assignments and constants for this
// board only. Other boards live in sibling folders under hardware/boards/
// and are selected with -DBOARD=<name>.
// ──────────────────────────────────────────────────────────────

namespace BoardConfig
{
    // LED
    // Blue LED between 3V3 and GPIO8, so it lights when the pin is driven
    // LOW. GPIO8 is a strapping pin too (it must read high at reset); it is
    // only driven after the bootloader has run, so using it as an output is
    // safe. Holding the LED on across a reset is not.
    static constexpr int LED_PIN = 8;
    static constexpr bool LED_ACTIVE_HIGH = false;

    // OLED — SSD1306, 72x40, on its own I2C bus. The panel's 72 columns sit
    // in the middle of the controller's 128-column RAM, starting at 28.
    static constexpr int OLED_SDA_PIN = 5;
    static constexpr int OLED_SCL_PIN = 6;
    static constexpr uint8_t OLED_ADDRESS = 0x3C;
    static constexpr uint8_t OLED_WIDTH = 72;
    static constexpr uint8_t OLED_HEIGHT = 40;
    static constexpr uint8_t OLED_COLUMN_OFFSET = 28;

    // Other fixed pins on this board, for reference when adding peripherals:
    //   GPIO9         BOOT button (strapping; low at reset = download mode)
    //   GPIO18/GPIO19 USB D-/D+ — taken by USB Serial/JTAG, do not reuse
    //   GPIO20/GPIO21 UART0 RX/TX (broken out, free if the console is on USB)
}
