#pragma once

// ──────────────────────────────────────────────────────────────
// What this product is, and how it is meant to be driven — in the product's own
// words, in the application layer, beside the managers the words are about.
//
// It lives here and not in the relay for one reason: whatever answers "how do I use
// this device" has to be the firmware that is actually running. A copy kept
// server-side is a copy that describes last month's build, and the one caller who
// cannot notice is the one most likely to act on it.
//
// Registered into the framework from AppContext::Init(), like every other thing the
// application tells Strux about itself (see SystemManager::SetDocumentation). Strux
// never reaches up for it.
//
// Two strings, on purpose:
//
//   DESCRIPTION   one line. It rides in the relay hello, so it is in a device list
//                 before anything asks the device a question. Sentence, not paragraph.
//
//   INSTRUCTIONS  free-form, as long as it needs to be, served only when asked for
//                 (`system describe`). Deliberately prose and NOT a schema: every
//                 command already declares its own name, arguments, types and
//                 required/optional state, and this is for the part that never fits
//                 in a declaration — which commands belong together, what order they
//                 go in, what the units are, what will not work and why.
//
// A fork replaces both when it replaces LedManager with a real feature.
// ──────────────────────────────────────────────────────────────
namespace DeviceDoc {

inline constexpr const char* DESCRIPTION =
    "BLE climate sensor gateway - listens to LYWSD03MMC-style thermometers and "
    "forwards temperature, humidity and battery to the relay as telemetry.";

inline constexpr const char* INSTRUCTIONS =
    "This is a gateway for BLE climate sensors (Xiaomi LYWSD03MMC and anything "
    "speaking BTHome v2), built on the Strux framework. It scans passively - it "
    "never connects to a sensor - and reports every sensor it hears.\n"
    "\n"
    "Sensors\n"
    "  `sensor list` returns every sensor heard: MAC, name, format, whether its "
    "readings are encrypted, temperature (C), humidity (%), battery (% and V), RSSI "
    "(dBm) and seconds since it was last heard. A sensor on stock Xiaomi firmware "
    "is listed as encrypted and carries no values: it needs the pvvx custom "
    "firmware, flashed once from a browser. `sensor name mac=... name=...` names a "
    "sensor; an empty name removes it. Readings go to the relay as the `climate` "
    "measurement, tagged `sensor` (MAC), `name` (when set) and `format`, at most "
    "once per sensor every `sensor.interval` seconds (default 60).\n"
    "\n"
    "The display\n"
    "  Shows WiFi and relay status, then the sensors a page at a time.\n"
    "\n"
    "Discovering what it can do\n"
    "  Every command declares itself. `help` returns every command this firmware "
    "has, with its description and each of its arguments - name, type, whether it "
    "is required, and what it means - in one reply. Nothing about a command is "
    "written down twice, so what it returns is always this exact build.\n"
    "\n"
    "Reading the display\n"
    "  `display get` returns the display's size in characters and the text of every "
    "row, top to bottom - exactly what the OLED shows. On a board without a screen "
    "it still answers, with what the screen would show.\n"
    "\n"
    "Settings\n"
    "  `settings list` enumerates every setting on the device with its type and "
    "current value; `settings set` changes one in RAM and `settings save` commits it "
    "to NVS. A change is not durable until it is saved. Keys are dotted "
    "(`relay.url`, `wifi.ssid`) and are at most 15 characters, which is a storage "
    "limit and not a style.\n"
    "\n"
    "Things worth knowing before driving this device\n"
    "  * `system reboot` answers first and then restarts about half a second later, "
    "so every connection drops and anything in flight is lost.\n"
    "  * The `partition` commands write flash. `partition write` and "
    "`partition clear` change what the device boots; they exist for firmware "
    "updates and are not a general storage API.\n"
    "  * `web read` serves the device's own web UI files. It is how the relay "
    "renders this device's page; it is not a filesystem.\n"
    "  * Two codecs carry a command, and the device picks by the request line: one "
    "starting with '{' is JSON and is answered in JSON; anything else is read as a "
    "console line (`display get`) and is answered in YAML. Nothing is "
    "negotiated and both reach the same commands.\n"
    "  * A reply is one record unless the command says otherwise. `web read` and "
    "`partition read` are the exceptions: they write a header record declaring a "
    "contentType and then raw bytes.\n"
    "  * Numbers in arguments are unsigned 32-bit. There are no floating-point "
    "arguments anywhere in the protocol.";

} // namespace DeviceDoc
