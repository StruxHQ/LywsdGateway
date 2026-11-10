#pragma once

#include <cstddef>
#include <cstdint>

// ──────────────────────────────────────────────────────────────
// Decodes a BLE advertisement from a LYWSD03MMC-style thermometer into a
// reading. Pure: bytes in, struct out, nothing about radios or tasks — the
// scanner hands over the raw advertising data and this finds the service data
// it understands.
//
// Four formats, because what a sensor sends depends on the firmware on it:
//
//   pvvx      custom firmware, service UUID 0x181A, 15 bytes, little-endian,
//             0.01 °C / 0.01 %
//   ATC1441   custom firmware, service UUID 0x181A, 13 bytes, big-endian,
//             0.1 °C / 1 %
//   BTHome    v2, service UUID 0xFCD2 — what pvvx firmware sends when set to
//             BTHome, and what many other sensors speak
//   MiBeacon  Xiaomi stock firmware, service UUID 0xFE95. On a LYWSD03MMC the
//             readings are AES-CCM encrypted with a per-device bindkey, which
//             this does not have — so a stock sensor is RECOGNISED (format set,
//             `encrypted` true) but carries no values.
//
// Recognising a sensor without reading it is deliberate: it is how the user
// finds out which of their sensors still need the custom firmware.
// ──────────────────────────────────────────────────────────────

enum class ClimateFormat : uint8_t
{
    None,
    Pvvx,
    Atc1441,
    BtHome,
    MiBeacon,
};

inline const char* ClimateFormatName(ClimateFormat format)
{
    switch (format)
    {
    case ClimateFormat::None:     return "none";
    case ClimateFormat::Pvvx:     return "pvvx";
    case ClimateFormat::Atc1441:  return "atc1441";
    case ClimateFormat::BtHome:   return "bthome";
    case ClimateFormat::MiBeacon: return "mibeacon";
    }
    return "none";
}

struct ClimateReading
{
    ClimateFormat format = ClimateFormat::None;

    /// The values are in the advertisement but not readable without a key.
    bool encrypted = false;

    bool hasTemperature = false;
    bool hasHumidity = false;
    bool hasBatteryPercent = false;
    bool hasBatteryMillivolts = false;
    bool hasCounter = false;

    int32_t temperatureCenti = 0;   // 0.01 °C
    int32_t humidityCenti = 0;      // 0.01 %
    uint8_t batteryPercent = 0;
    uint16_t batteryMillivolts = 0;

    /// Changes when the sensor takes a new measurement; repeats of one
    /// measurement carry the same value.
    uint8_t counter = 0;
};

namespace climate_detail
{
    // The AD type carrying 16-bit-UUID service data.
    constexpr uint8_t AD_SERVICE_DATA_16 = 0x16;

    constexpr uint16_t UUID_ENVIRONMENTAL = 0x181A;
    constexpr uint16_t UUID_BTHOME = 0xFCD2;
    constexpr uint16_t UUID_XIAOMI = 0xFE95;

    constexpr uint16_t XIAOMI_PRODUCT_LYWSD03MMC = 0x055B;

    inline uint16_t U16LE(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
    inline uint16_t U16BE(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
    inline int16_t S16LE(const uint8_t* p) { return static_cast<int16_t>(U16LE(p)); }
    inline int16_t S16BE(const uint8_t* p) { return static_cast<int16_t>(U16BE(p)); }

    inline bool ParsePvvx(const uint8_t* p, ClimateReading& out)
    {
        // [mac 6][temp s16][hum u16][mV u16][batt % u8][counter u8][flags u8]
        out.format = ClimateFormat::Pvvx;
        out.hasTemperature = out.hasHumidity = true;
        out.hasBatteryMillivolts = out.hasBatteryPercent = out.hasCounter = true;
        out.temperatureCenti = S16LE(p + 6);
        out.humidityCenti = U16LE(p + 8);
        out.batteryMillivolts = U16LE(p + 10);
        out.batteryPercent = p[12];
        out.counter = p[13];
        return true;
    }

    inline bool ParseAtc1441(const uint8_t* p, ClimateReading& out)
    {
        // [mac 6][temp s16 BE, 0.1][hum u8][batt % u8][mV u16 BE][counter u8]
        out.format = ClimateFormat::Atc1441;
        out.hasTemperature = out.hasHumidity = true;
        out.hasBatteryMillivolts = out.hasBatteryPercent = out.hasCounter = true;
        out.temperatureCenti = S16BE(p + 6) * 10;
        out.humidityCenti = p[8] * 100;
        out.batteryPercent = p[9];
        out.batteryMillivolts = U16BE(p + 10);
        out.counter = p[12];
        return true;
    }

    /// Size of a BTHome v2 object's value, by object id. -1 for an id this
    /// does not know, which ends parsing: the format has no length field, so
    /// past an unknown object nothing that follows can be located.
    inline int BtHomeValueSize(uint8_t id)
    {
        // The binary sensors: 0x0F..0x11 and 0x15..0x2D. 0x12..0x14 sit between
        // them and are two-byte values (CO2, VOC, moisture).
        if ((id >= 0x0F && id <= 0x11) || (id >= 0x15 && id <= 0x2D))
            return 1;
        switch (id)
        {
        case 0x00: case 0x01: case 0x09: case 0x2E: case 0x2F: case 0x3A:
        case 0x46: case 0x57: case 0x58: case 0x59: case 0x60:
            return 1;
        case 0x02: case 0x03: case 0x06: case 0x07: case 0x08: case 0x0C:
        case 0x0D: case 0x0E: case 0x12: case 0x13: case 0x14: case 0x3C:
        case 0x3D: case 0x3F: case 0x40: case 0x41: case 0x43: case 0x44:
        case 0x45: case 0x47: case 0x48: case 0x49: case 0x4A: case 0x51:
        case 0x52: case 0x56: case 0x5A: case 0x5D: case 0x5E: case 0x5F:
        case 0xF0:
            return 2;
        case 0x04: case 0x05: case 0x0A: case 0x0B: case 0x42: case 0x4B:
        case 0xF2:
            return 3;
        case 0x3E: case 0x4C: case 0x4D: case 0x4E: case 0x4F: case 0x50:
        case 0x55: case 0x5B: case 0x5C: case 0xF1:
            return 4;
        default:
            return -1;
        }
    }

    inline bool ParseBtHome(const uint8_t* p, size_t length, ClimateReading& out)
    {
        if (length < 1)
            return false;

        const uint8_t info = p[0];
        const uint8_t version = info >> 5;
        if (version != 2)
            return false;

        out.format = ClimateFormat::BtHome;
        if (info & 0x01)
        {
            out.encrypted = true;
            return true;
        }

        size_t i = 1;
        while (i < length)
        {
            const uint8_t id = p[i++];
            const int size = BtHomeValueSize(id);
            if (size < 0 || i + static_cast<size_t>(size) > length)
                break;

            const uint8_t* v = p + i;
            switch (id)
            {
            case 0x00:
                out.hasCounter = true;
                out.counter = v[0];
                break;
            case 0x01:
                out.hasBatteryPercent = true;
                out.batteryPercent = v[0];
                break;
            case 0x02:
                out.hasTemperature = true;
                out.temperatureCenti = S16LE(v);
                break;
            case 0x45:
                out.hasTemperature = true;
                out.temperatureCenti = S16LE(v) * 10;
                break;
            case 0x03:
                out.hasHumidity = true;
                out.humidityCenti = U16LE(v);
                break;
            case 0x2E:
                out.hasHumidity = true;
                out.humidityCenti = v[0] * 100;
                break;
            case 0x0C:
                out.hasBatteryMillivolts = true;
                out.batteryMillivolts = U16LE(v);
                break;
            default:
                break;
            }
            i += static_cast<size_t>(size);
        }
        return true;
    }

    inline bool ParseMiBeacon(const uint8_t* p, size_t length, ClimateReading& out)
    {
        // [frame control u16][product id u16][frame counter u8][mac 6?]
        // [capability 1?][objects or ciphertext]
        if (length < 5)
            return false;

        const uint16_t frameControl = U16LE(p);
        const uint16_t product = U16LE(p + 2);
        if (product != XIAOMI_PRODUCT_LYWSD03MMC)
            return false;

        const bool isEncrypted = frameControl & 0x0008;
        const bool hasMac = frameControl & 0x0010;
        const bool hasCapability = frameControl & 0x0020;
        const bool hasObject = frameControl & 0x0040;

        out.format = ClimateFormat::MiBeacon;
        out.hasCounter = true;
        out.counter = p[4];

        if (isEncrypted)
        {
            out.encrypted = true;
            return true;
        }

        size_t i = 5;
        if (hasMac)
            i += 6;
        if (hasCapability && i < length)
        {
            const uint8_t capability = p[i++];
            if (capability & 0x20)
                i += 2;   // I/O capability follows
        }
        if (!hasObject)
            return true;

        // [type u16][length u8][value] ...
        while (i + 3 <= length)
        {
            const uint16_t type = U16LE(p + i);
            const uint8_t size = p[i + 2];
            const uint8_t* v = p + i + 3;
            if (i + 3 + size > length)
                break;

            if (type == 0x1004 && size == 2)
            {
                out.hasTemperature = true;
                out.temperatureCenti = S16LE(v) * 10;
            }
            else if (type == 0x1006 && size == 2)
            {
                out.hasHumidity = true;
                out.humidityCenti = U16LE(v) * 10;
            }
            else if (type == 0x100A && size == 1)
            {
                out.hasBatteryPercent = true;
                out.batteryPercent = v[0];
            }
            else if (type == 0x100D && size == 4)
            {
                out.hasTemperature = out.hasHumidity = true;
                out.temperatureCenti = S16LE(v) * 10;
                out.humidityCenti = U16LE(v + 2) * 10;
            }
            i += 3 + size;
        }
        return true;
    }

    inline bool ParseServiceData(uint16_t uuid, const uint8_t* p, size_t length, ClimateReading& out)
    {
        switch (uuid)
        {
        case UUID_ENVIRONMENTAL:
            if (length == 15)
                return ParsePvvx(p, out);
            if (length == 13)
                return ParseAtc1441(p, out);
            return false;
        case UUID_BTHOME:
            return ParseBtHome(p, length, out);
        case UUID_XIAOMI:
            return ParseMiBeacon(p, length, out);
        default:
            return false;
        }
    }
}

/// Fold a newly received reading into what is known about a sensor.
///
/// A sensor does not repeat every value in every advertisement: pvvx's BTHome
/// mode alternates a temperature/humidity packet with a battery one, and stock
/// Xiaomi firmware interleaves bare beacons with its encrypted frames. So a
/// field is replaced only by a newer value of that same field, and an
/// advertisement that carries nothing leaves the reading alone.
///
/// A change of format starts over: values decoded under one firmware say
/// nothing about the sensor once it has been flashed with another.
inline void MergeClimateReading(ClimateReading& into, const ClimateReading& from)
{
    if (into.format != from.format)
    {
        into = from;
        return;
    }

    const bool carriesValues = from.hasTemperature || from.hasHumidity ||
                               from.hasBatteryPercent || from.hasBatteryMillivolts;
    if (from.encrypted)
        into.encrypted = true;
    else if (carriesValues)
        into.encrypted = false;

    if (from.hasTemperature)
    {
        into.hasTemperature = true;
        into.temperatureCenti = from.temperatureCenti;
    }
    if (from.hasHumidity)
    {
        into.hasHumidity = true;
        into.humidityCenti = from.humidityCenti;
    }
    if (from.hasBatteryPercent)
    {
        into.hasBatteryPercent = true;
        into.batteryPercent = from.batteryPercent;
    }
    if (from.hasBatteryMillivolts)
    {
        into.hasBatteryMillivolts = true;
        into.batteryMillivolts = from.batteryMillivolts;
    }
    if (from.hasCounter)
    {
        into.hasCounter = true;
        into.counter = from.counter;
    }
}

/// Walk the advertising data's AD structures and decode the first service
/// data this recognises. False when there is none — not a climate sensor, or
/// one this does not speak.
inline bool ParseClimateAdvertisement(const uint8_t* data, size_t length, ClimateReading& out)
{
    using namespace climate_detail;

    size_t i = 0;
    while (i < length)
    {
        const uint8_t fieldLength = data[i];   // covers the type byte and the payload
        if (fieldLength == 0 || i + 1 + fieldLength > length)
            return false;

        const uint8_t type = data[i + 1];
        const uint8_t* payload = data + i + 2;
        const size_t payloadLength = fieldLength - 1;

        if (type == AD_SERVICE_DATA_16 && payloadLength >= 2)
        {
            ClimateReading reading;
            if (ParseServiceData(U16LE(payload), payload + 2, payloadLength - 2, reading))
            {
                out = reading;
                return true;
            }
        }
        i += 1 + fieldLength;
    }
    return false;
}
