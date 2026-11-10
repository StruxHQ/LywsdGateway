#pragma once

#include "interfaces/Display.h"

// ──────────────────────────────────────────────────────────────
// Display role with no hardware behind it — accepts and discards text.
// For boards without a screen fitted. The size matches the smallest real
// display so the application lays out the same on every board.
// ──────────────────────────────────────────────────────────────

class MockDisplay : public Display
{
public:
    uint8_t Columns() const override { return 12; }
    uint8_t Rows() const override { return 5; }
    void WriteLine(uint8_t, const char*) override {}
};
