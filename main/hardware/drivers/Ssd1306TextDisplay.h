#pragma once

#include "interfaces/Display.h"
#include "Font5x7.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

// ──────────────────────────────────────────────────────────────
// SSD1306 OLED over I2C, driven as a text display: one 8-pixel page per
// row, 6-pixel character cells (5x7 glyph plus a blank column). Writing a
// row writes that page straight to the panel, so there is no framebuffer.
//
// The controller always has 128x64 of RAM; a smaller panel is a window into
// it. `columnOffset` is where that window starts — 28 on the 0.42" 72x40
// panels — and the multiplex ratio below is what limits the scan to
// `height` rows.
//
// The bus is the board's: it is created in BoardContext::Init() and handed
// to Init() here, because an I2C bus handle does not exist before then.
// ──────────────────────────────────────────────────────────────

class Ssd1306TextDisplay : public Display
{
    static constexpr const char* TAG = "Ssd1306";
    static constexpr uint8_t CELL_WIDTH = Font5x7::WIDTH + 1;
    static constexpr uint8_t MAX_WIDTH = 128;
    static constexpr uint32_t I2C_SPEED_HZ = 400000;
    static constexpr int I2C_TIMEOUT_MS = 50;

    // First byte of every I2C write: what the rest of it is.
    static constexpr uint8_t CONTROL_COMMAND = 0x00;
    static constexpr uint8_t CONTROL_DATA = 0x40;

public:
    Ssd1306TextDisplay(uint8_t address, uint8_t width, uint8_t height, uint8_t columnOffset)
        : address_(address), width_(width < MAX_WIDTH ? width : MAX_WIDTH), height_(height), columnOffset_(columnOffset)
    {
    }

    void Init(i2c_master_bus_handle_t bus)
    {
        i2c_device_config_t dev = {};
        dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev.device_address = address_;
        dev.scl_speed_hz = I2C_SPEED_HZ;
        if (i2c_master_bus_add_device(bus, &dev, &device_) != ESP_OK)
        {
            ESP_LOGE(TAG, "Could not add device 0x%02X to the bus", address_);
            return;
        }

        const uint8_t init[] = {
            CONTROL_COMMAND,
            0xAE,                               // display off
            0xD5, 0x80,                         // clock divide / oscillator
            0xA8, static_cast<uint8_t>(height_ - 1), // multiplex ratio: rows scanned
            0xD3, 0x00,                         // display offset
            0x40,                               // start line 0
            0x8D, 0x14,                         // charge pump on
            0x20, 0x02,                         // page addressing mode
            0xA1,                               // segment remap: column 0 on the left
            0xC8,                               // COM scan reversed: row 0 at the top
            0xDA, 0x12,                         // COM pins: alternative configuration
            0x81, 0x7F,                         // contrast
            0xD9, 0x22,                         // pre-charge period
            0xDB, 0x20,                         // VCOMH deselect level
            0xAD, 0x30,                         // internal IREF (the 0.42" panels need it)
            0xA4,                               // show RAM contents
            0xA6,                               // not inverted
        };
        if (!Send(init, sizeof(init)))
        {
            ESP_LOGE(TAG, "No display answered at 0x%02X", address_);
            return;
        }

        ready_ = true;
        for (uint8_t row = 0; row < Rows(); row++)
            WriteLine(row, "");

        const uint8_t on[] = { CONTROL_COMMAND, 0xAF };
        Send(on, sizeof(on));
        ESP_LOGI(TAG, "Initialized %ux%u at 0x%02X", width_, height_, address_);
    }

    uint8_t Columns() const override { return width_ / CELL_WIDTH; }
    uint8_t Rows() const override { return height_ / 8; }

    void WriteLine(uint8_t row, const char* text) override
    {
        if (!ready_ || row >= Rows())
            return;

        const uint8_t column = columnOffset_;
        const uint8_t position[] = {
            CONTROL_COMMAND,
            static_cast<uint8_t>(0xB0 | row),            // page
            static_cast<uint8_t>(0x00 | (column & 0x0F)), // column, low nibble
            static_cast<uint8_t>(0x10 | (column >> 4)),   // column, high nibble
        };
        if (!Send(position, sizeof(position)))
            return;

        uint8_t pixels[1 + MAX_WIDTH] = { CONTROL_DATA };
        uint8_t* out = pixels + 1;
        for (uint8_t cell = 0; cell < Columns(); cell++)
        {
            // A NUL ends the text; every cell after it stays blank.
            if (*text != '\0')
            {
                const uint8_t* glyph = Font5x7::Glyph(*text++);
                for (uint8_t x = 0; x < Font5x7::WIDTH; x++)
                    out[x] = glyph[x];
            }
            out += CELL_WIDTH;
        }
        Send(pixels, 1 + width_);
    }

private:
    uint8_t address_;
    uint8_t width_;
    uint8_t height_;
    uint8_t columnOffset_;
    i2c_master_dev_handle_t device_ = nullptr;
    bool ready_ = false;

    bool Send(const uint8_t* bytes, size_t length)
    {
        return i2c_master_transmit(device_, bytes, length, I2C_TIMEOUT_MS) == ESP_OK;
    }
};
