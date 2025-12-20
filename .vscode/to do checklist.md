Boat Test Checklist — ESP32 / SignalK / NMEA2000
1️⃣ Power-up & connectivity

☐ Power up ESP32

☐ Confirm ESP32 joins network (Wi-Fi / Ethernet as applicable)

☐ Open Signal K web UI

☐ Verify ESP32 data source is connected (no reconnect loop)

2️⃣ Signal K data sanity (before engine start)

☐ Patch battery.js and engineparameters.js
    ssh root@<ip>
    password: CerboGx password
☐ restart SignalK via ui

RPM / frequency path

☐ Check debug paths:

debug.engine.revolutions_hz

debug.engine.revolutions_rad_s_raw

debug.engine.revolutions_rad_s_smooth

☐ With engine off:

RPM = null / NAN

No PGN 127488 emitted (verify logs)

Coolant temperature (engine off)

☐ Verify SK shows coolant temp value (may be non-zero due to sender resistance)

☐ Confirm ADC voltage path is stable (not floating):

Expect some voltage if divider is biased

☐ If coolant temp reads:

Constant / fixed → OK (expected with no sender excitation)

Wild / noisy → ADC may be floating (note for later)

Engine hours

☐ Check propulsion.engine.runTime in Signal K

☐ Confirm:

Value exists

Increases only when RPM > threshold

3️⃣ Display checks — engine off
MFD (chartplotter)

☐ Engine hours visible

☐ Coolant temperature visible

☐ Battery voltage(s) visible

Raymarine i70s

☐ Engine hours visible

☐ Coolant temperature visible

☐ Battery voltage(s) visible

If missing → check:

PGN 127508 rate (~1 Hz)

Battery Instance mapping (0 / 1)

No null voltage values

4️⃣ Engine start sequence

☐ Start engine

☐ Observe RPM rise in Signal K debug

☐ Verify:

debug.engine.revolutions_hz increases

debug.engine.revolutions_rad_s_raw increases

debug.engine.revolutions_rad_s_smooth is stable

Expected values at idle

Target idle RPM: 900–1000 RPM

Conversion check:

rad/s = RPM × (2π / 60)

RPM	Expected rad/s
900	~94.2 rad/s
950	~99.5 rad/s
1000	~104.7 rad/s

☐ Smoothed rad/s falls in this range

5️⃣ Display checks — engine running
MFD

☐ RPM visible and stable

☐ No lag / stepping

☐ Coolant temp updates

☐ Battery voltage updates

Raymarine i70s

☐ RPM visible (this confirms:

PGN 127488 @ 4 Hz

Correct Engine Instance

Valid rad/s)

☐ Coolant temperature visible

☐ Battery voltage visible and steady

6️⃣ Failure triage notes (if something is missing)

RPM missing on i70s but present on MFD:

☐ Check PGN 127488 frequency (should be ~4 Hz)

☐ Check Engine Instance = 0 or 1

Battery volts missing on i70s:

☐ Check PGN 127508 @ ~1 Hz

☐ Check 'Battery Instance' field

☐ Ensure voltage is never null

Coolant temp weird with engine off:

☐ Confirm sender bias / divider excitation

☐ ADC floating possibility noted