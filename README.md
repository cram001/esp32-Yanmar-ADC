This project builds an inexpensive and highly customizable marine diesel engine analogue to digital converter. A simple inexpensive ESP32 microcontroller (https://en.wikipedia.org/wiki/ESP32), 
along with some voltage dividers, op-amp and opto isolator, converts analogue engine sensors to digital format and sends them to a SignalK server. Since many boats have a Cerbo GX
which can run SignalK (install Venus OS Large), the SignalK server can then send the values to the NMEA2000 (CerboGX can easily be connected to N2K / STNG).

Also included are additional temp sensors, using inexpensive and waterproof OneWire sensors which can be used to monitor exhaust elbow temp, engine compartment temp, alternator temp, etc...

Release notes:

- sensor -> esp32 -> signalk -> signalk-to-nmea200 plugin->canboat.js-> ethernet to N2K adapter -> N2K network
- for reference : https://github.com/canboat/canboat/blob/master/docs/canboat.json
- requires patching signalk-to-nmea2000 from https://github.com/cram001/signalk-to-nmea2000-ma  (due to significant timing and unit errors)
- signalk-to-nmea2000 is responsible for sending N2K data at the correct rate (1hz, 4hz, etc...)
- If the speed paddle wheel (STW) fails or is sluggish, the user must disable use of STW via sensesp configuration page, code will then use SOG instead to calculate fuel flow and engine load
- oil pressure monitor not tested / not fully implemented

  
Currently the code base is designed for:

- Yanmar 3JH3E engine (3800 RPM max)
- American type (D) coolant temp sender 450-30 ohm (most Yanmars, except on Catalina, use European spec senders, check your values!)
- Engine RPM via ring gear magnetic pickup.
- Calculates (estimates) fuel consumption, engine load based on user provided data. Corrects it based on wind, SOG, etc...

Note: both the Engine RPM and coolant temp gaugues must remain connected, with no impact on accuracy. If they fail, the digital values will not be usable.

Based on SensESP and the FIrebeetle 2 ESP-E (4MB) device.

Required hardware:

- Victron Cerbo GX, with OS Large option installed (via firmware update page) (or Raspbery Pi with SignalK or Windows with SignalK Server)
- NMEA 2000 - ethernet adapter if you want SignalK to send the data tor your chartplotter / instruments
- DF Robot Firebeetle ESP32-E (16MB) .... 4 MB is min, 16 MB is recommended for future improvements. As of Dec 2025, 92% of the storage space is used if OTA (Over the Air) updates are required on the 4MB model.
  by disabling OTA and changing the partition table, more space can be made available on the 4MB unit
  - DF Robot Gravity IO Shield is highly recommended to faciliate connections (DFR0762)
- Buck voltage converter, stable 5V output (DF Robot DFR1015)
- OneWIre temp sensor (waterproof) with resistor module (DF Robot KIT0021)
-   Note: you can use one resistor module per sensor, or connect multiple sensors in parallel to one resistor module. Code is currently written for 1 pin per OneWire sensor
- Voltage Divider: to connect your engine's coolant temp sender without affecting the gauge. 
-   CRITICAL: FireBeetle ESP32-E ADC uses 2.5V reference (NOT 3.3V). 
-   DFR0051 (30kΩ/7.5kΩ = 5:1 ratio) voltage analysis for 14.0V max system:
-     • Normal operation: 0.30V - 1.49V ✅ SAFE
-     • Open circuit fault: 2.8V (exceeds 2.45V recommended spec, but below 3.6V damage threshold)
-   RISK: Operating above 2.5V reduces ADC accuracy and lifespan, vulnerable to voltage spikes
-   RECOMMENDED: Add 2.4V Zener diode protection to ADC input (~$0.50 part) for hardware safety
-   Software protection: Code detects open circuit and emits NAN (see sender_resistance.h)
-   Suggest sampling the coolant temp gauge voltage (sender pin to ground pin) at various temperatures to confirm your boat's voltage for the coolant temp sender...
- To connect the mageteic pickup RPM sender to the ESP32, you will need an Op-Amp (LMV358), Opto Isolator and Capacitor 50V, 0.1uF
- Other misc parts: project box, interconnect wires, diode, capacitor to smooth data and protect inputs, etc..

Memory Optimization:

The code includes conditional compilation flags to reduce flash usage:
- `ENABLE_DEBUG_OUTPUTS=0` in platformio.ini disables all debug.* SignalK paths (saves ~5-10KB flash)
- `RPM_SIMULATOR=0` disables the RPM simulator (saves ~2KB flash)
- For 4MB units at 92% capacity: disable debug outputs and OTA to free up space
- To disable OTA: comment out `.enable_ota()` in setup() and use different partition table

Future upgrades:

- RPM off alternator
- oil pressure sender (code present, but not tested)
- low oil pressure, high coolant temp sender alarms (easy to send to SignalK, but unkonwn if canboat.js can handle them to convert to N2K ?)

Code is still in development (beta).... use at your own risk!

 
