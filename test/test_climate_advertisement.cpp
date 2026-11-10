#include "check.h"
#include "ClimateAdvertisement.h"

// Advertising data is a sequence of [length][type][payload] fields. These build
// one service-data field (type 0x16) so each test states only its payload.

TEST(pvvx_custom_format_is_decoded)
{
    const uint8_t adv[] = {
        0x02, 0x01, 0x06,                                // flags, ignored
        0x12, 0x16, 0x1A, 0x18,                          // service data, UUID 0x181A
        0x33, 0x22, 0x11, 0x38, 0xC1, 0xA4,              // MAC, little-endian
        0x52, 0x08,                                      // 21.30 C
        0x9A, 0x10,                                      // 42.50 %
        0x6E, 0x0B,                                      // 2926 mV
        0x55,                                            // 85 %
        0x07,                                            // counter
        0x04,                                            // flags
    };
    ClimateReading r;
    CHECK(ParseClimateAdvertisement(adv, sizeof(adv), r));
    CHECK(r.format == ClimateFormat::Pvvx);
    CHECK(!r.encrypted);
    CHECK(r.hasTemperature && r.temperatureCenti == 2130);
    CHECK(r.hasHumidity && r.humidityCenti == 4250);
    CHECK(r.hasBatteryMillivolts && r.batteryMillivolts == 2926);
    CHECK(r.hasBatteryPercent && r.batteryPercent == 85);
    CHECK(r.hasCounter && r.counter == 7);
}

TEST(atc1441_format_is_decoded_and_scaled)
{
    const uint8_t adv[] = {
        0x10, 0x16, 0x1A, 0x18,
        0xA4, 0xC1, 0x38, 0x11, 0x22, 0x33,              // MAC, big-endian
        0xFF, 0xCE,                                      // -5.0 C
        0x37,                                            // 55 %
        0x64,                                            // 100 %
        0x0B, 0xB8,                                      // 3000 mV
        0x2A,                                            // counter
    };
    ClimateReading r;
    CHECK(ParseClimateAdvertisement(adv, sizeof(adv), r));
    CHECK(r.format == ClimateFormat::Atc1441);
    CHECK(r.temperatureCenti == -500);
    CHECK(r.humidityCenti == 5500);
    CHECK(r.batteryPercent == 100);
    CHECK(r.batteryMillivolts == 3000);
    CHECK(r.counter == 0x2A);
}

TEST(bthome_v2_objects_are_decoded)
{
    const uint8_t adv[] = {
        0x11, 0x16, 0xD2, 0xFC,
        0x40,                                            // v2, not encrypted
        0x00, 0x09,                                      // packet id 9
        0x01, 0x4B,                                      // battery 75 %
        0x02, 0xCA, 0x08,                                // 22.50 C
        0x03, 0xBF, 0x13,                                // 50.55 %
        0x0C, 0x40, 0x0B,                                // 2880 mV
    };
    ClimateReading r;
    CHECK(ParseClimateAdvertisement(adv, sizeof(adv), r));
    CHECK(r.format == ClimateFormat::BtHome);
    CHECK(!r.encrypted);
    CHECK(r.counter == 9);
    CHECK(r.batteryPercent == 75);
    CHECK(r.temperatureCenti == 2250);
    CHECK(r.humidityCenti == 5055);
    CHECK(r.batteryMillivolts == 2880);
}

TEST(bthome_stops_at_an_unknown_object_but_keeps_what_came_before)
{
    const uint8_t adv[] = {
        0x0A, 0x16, 0xD2, 0xFC,
        0x40,
        0x02, 0x10, 0x00,                                // 0.16 C
        0xEE, 0x01,                                      // unknown id
        0x01,
    };
    ClimateReading r;
    CHECK(ParseClimateAdvertisement(adv, sizeof(adv), r));
    CHECK(r.hasTemperature && r.temperatureCenti == 16);
    CHECK(!r.hasBatteryPercent);
}

TEST(bthome_two_byte_values_between_the_binary_ranges_are_skipped_correctly)
{
    // 0x12 (CO2) is two bytes even though its neighbours are one.
    const uint8_t adv[] = {
        0x0A, 0x16, 0xD2, 0xFC,
        0x40,
        0x12, 0xE2, 0x04,                                // CO2 1250 ppm
        0x02, 0x64, 0x00,                                // 1.00 C
    };
    ClimateReading r;
    CHECK(ParseClimateAdvertisement(adv, sizeof(adv), r));
    CHECK(r.hasTemperature && r.temperatureCenti == 100);
}

TEST(encrypted_bthome_is_recognised_without_values)
{
    const uint8_t adv[] = { 0x06, 0x16, 0xD2, 0xFC, 0x41, 0xAA, 0xBB };
    ClimateReading r;
    CHECK(ParseClimateAdvertisement(adv, sizeof(adv), r));
    CHECK(r.format == ClimateFormat::BtHome);
    CHECK(r.encrypted);
    CHECK(!r.hasTemperature);
}

TEST(stock_xiaomi_firmware_is_recognised_as_encrypted)
{
    const uint8_t adv[] = {
        0x15, 0x16, 0x95, 0xFE,
        0x58, 0x58,                                      // frame control: encrypted, MAC, object
        0x5B, 0x05,                                      // product 0x055B, LYWSD03MMC
        0x10,                                            // frame counter
        0x33, 0x22, 0x11, 0x38, 0xC1, 0xA4,              // MAC
        0x01, 0x02, 0x03, 0x04, 0x05,                    // ciphertext
        0x06, 0x07,
    };
    ClimateReading r;
    CHECK(ParseClimateAdvertisement(adv, sizeof(adv), r));
    CHECK(r.format == ClimateFormat::MiBeacon);
    CHECK(r.encrypted);
    CHECK(!r.hasTemperature);
    CHECK(r.counter == 0x10);
}

TEST(unencrypted_mibeacon_temperature_and_humidity_are_decoded)
{
    const uint8_t adv[] = {
        0x15, 0x16, 0x95, 0xFE,
        0x50, 0x20,                                      // MAC + object, not encrypted
        0x5B, 0x05,
        0x01,
        0x33, 0x22, 0x11, 0x38, 0xC1, 0xA4,
        0x0D, 0x10, 0x04,                                // object 0x100D, 4 bytes
        0xD2, 0x00,                                      // 21.0 C
        0xBC, 0x01,                                      // 44.4 %
    };
    ClimateReading r;
    CHECK(ParseClimateAdvertisement(adv, sizeof(adv), r));
    CHECK(!r.encrypted);
    CHECK(r.temperatureCenti == 2100);
    CHECK(r.humidityCenti == 4440);
}

TEST(other_xiaomi_products_are_ignored)
{
    const uint8_t adv[] = {
        0x08, 0x16, 0x95, 0xFE, 0x58, 0x58, 0x47, 0x03, 0x01,
    };
    ClimateReading r;
    CHECK(!ParseClimateAdvertisement(adv, sizeof(adv), r));
}

TEST(a_truncated_field_is_rejected_rather_than_overread)
{
    const uint8_t adv[] = { 0x12, 0x16, 0x1A, 0x18, 0x01, 0x02 };
    ClimateReading r;
    CHECK(!ParseClimateAdvertisement(adv, sizeof(adv), r));
}

TEST(environmental_service_data_of_another_length_is_not_guessed_at)
{
    const uint8_t adv[] = { 0x05, 0x16, 0x1A, 0x18, 0x01, 0x02 };
    ClimateReading r;
    CHECK(!ParseClimateAdvertisement(adv, sizeof(adv), r));
}

TEST(merging_a_battery_only_packet_keeps_the_temperature)
{
    // pvvx in BTHome mode alternates these two packets.
    ClimateReading known;
    known.format = ClimateFormat::BtHome;
    known.hasTemperature = known.hasHumidity = true;
    known.temperatureCenti = 2155;
    known.humidityCenti = 3500;

    ClimateReading battery;
    battery.format = ClimateFormat::BtHome;
    battery.hasBatteryMillivolts = true;
    battery.batteryMillivolts = 2800;

    MergeClimateReading(known, battery);
    CHECK(known.hasTemperature && known.temperatureCenti == 2155);
    CHECK(known.hasHumidity && known.humidityCenti == 3500);
    CHECK(known.hasBatteryMillivolts && known.batteryMillivolts == 2800);
}

TEST(merging_a_newer_value_replaces_the_older_one)
{
    ClimateReading known;
    known.format = ClimateFormat::Pvvx;
    known.hasTemperature = true;
    known.temperatureCenti = 2000;

    ClimateReading next = known;
    next.temperatureCenti = 2100;

    MergeClimateReading(known, next);
    CHECK(known.temperatureCenti == 2100);
}

TEST(a_bare_beacon_does_not_clear_the_encrypted_state)
{
    ClimateReading known;
    known.format = ClimateFormat::MiBeacon;
    known.encrypted = true;

    ClimateReading bare;
    bare.format = ClimateFormat::MiBeacon;

    MergeClimateReading(known, bare);
    CHECK(known.encrypted);
}

TEST(a_new_format_starts_over)
{
    // Stock firmware replaced by pvvx: nothing from before still applies.
    ClimateReading known;
    known.format = ClimateFormat::MiBeacon;
    known.encrypted = true;
    known.hasBatteryPercent = true;
    known.batteryPercent = 90;

    ClimateReading flashed;
    flashed.format = ClimateFormat::BtHome;
    flashed.hasTemperature = true;
    flashed.temperatureCenti = 2300;

    MergeClimateReading(known, flashed);
    CHECK(known.format == ClimateFormat::BtHome);
    CHECK(!known.encrypted);
    CHECK(!known.hasBatteryPercent);
    CHECK(known.temperatureCenti == 2300);
}
