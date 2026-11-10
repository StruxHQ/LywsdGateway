# Next up

**Active work only.** Rewritten constantly, kept tiny, and an item is *removed* when it
lands or is dropped — never ticked off in place. Everything else lives in
GitHub issues (work for later) or `docs/reasoning/` (why things are the way they are).
If a fact wants to survive, it does not belong in this file.

Last updated 2026-11-10.

## Now

**Prove the sensor gateway on hardware.** `SensorManager` (NimBLE passive scan,
pvvx / ATC1441 / BTHome v2 / stock MiBeacon) is built and its parser host-tested,
but has not yet run on the board: flash `esp32c3_oled`, check that `sensor list`
sees the sensors, that WiFi and the relay link survive BLE coexistence, and that
`climate` points arrive in InfluxDB.
