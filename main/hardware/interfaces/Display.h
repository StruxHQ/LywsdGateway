#pragma once

#include <cstdint>

// ──────────────────────────────────────────────────────────────
// Role interface: a small text display, addressed as rows of fixed-width
// characters. Boards bind a real driver (Ssd1306TextDisplay) or a mock
// (MockDisplay) when no screen is fitted.
//
// Text rows rather than pixels because that is all the application asks of
// it today; a board that needs graphics exposes its driver as a concrete
// accessor on BoardContext instead of growing this role.
// ──────────────────────────────────────────────────────────────

class Display
{
public:
    virtual ~Display() = default;

    virtual uint8_t Columns() const = 0;
    virtual uint8_t Rows() const = 0;

    /// Replace one row. Text beyond Columns() is cut off, a shorter text is
    /// blank-padded, and a row past Rows() is ignored.
    virtual void WriteLine(uint8_t row, const char* text) = 0;
};
