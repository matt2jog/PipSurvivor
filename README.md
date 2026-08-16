# PipSurvivor

PipSurvivor is an ESP32/Arduino firmware prototype for sensor-triggered alerts, keypad message entry, a local Wi-Fi interface, and radio messaging. The default build targets an MH-ET LIVE ESP32 development board and a RYLR998 UART LoRa module.

This repository is a prototype, not certified emergency or life-safety equipment. Its detection thresholds and radio behavior have not been validated for rescue, medical, impact, drowning, range, or reliability claims.

## Implemented behavior

The main application currently provides:

- A four-state text UI: inbox, menu, alert feed, and message composition, plus an error screen.
- Multi-tap text entry from a 4x4 keypad or the web keypad.
- A Wi-Fi SoftAP with a browser keypad and JSON display/mesh endpoints.
- Optional polling of an MPU6050 gyroscope, BMP280 barometer, and analog water-level sensor.
- Alert generation from angular-velocity change, absolute altitude change, and debounced submersion.
- A RYLR998 flood-relay protocol with TTL, deduplication, randomized relay delay, and cancellation after an overheard relay.
- An alternative ESP-NOW adapter with simple broadcast packets.
- Binary `top.stl` and `bottom.stl` enclosure models.

Not currently implemented:

- Accelerometer-based impact detection in the main application.
- Orientation or inversion detection.
- Fall velocity estimation in the main application. The current alert compares absolute altitude values from consecutive polls.
- A physical LCD driver. `MockDisplayAdapter` is always used and mirrors frames to Serial; `kEnableDisplay` does not select another adapter.
- Explicit acknowledgements, retries, or delivery guarantees in the RYLR998 mesh.
- Mesh forwarding or deduplication in the ESP-NOW adapter.
- A graphical mesh visualizer. `/mesh` returns JSON.

## Build configuration and defaults

Feature flags, pins, thresholds, and radio parameters are in `lib/core/src/settings.h`.

Current defaults:

| Setting | Default | Effect |
|---|---:|---|
| `kEnableRadio` | `true` | Initializes the radio adapter selected at build time. |
| `kEnableGyroscope` | `false` | Enables MPU6050 angular-velocity sampling. |
| `kEnableBarometer` | `false` | Enables BMP280 altitude sampling. |
| `kEnableWaterSensor` | `false` | Enables the analog water sensor and submersion filter. |
| `kEnableButtons` | `false` | Enables both physical and web keypad event processing. |
| `kEnableDisplay` | `false` | Reported at boot but does not change the current display adapter. |
| `kEnableWebServer` | `true` | Starts the Wi-Fi access point and HTTP server. |

`kEnableAlertDetection` becomes true when at least one of the three sensor flags is true. Disabled sensor paths are skipped.

The `node` PlatformIO environment defines `PIPSURVIVOR_RADIO_RYLR998`. Defining `PIPSURVIVOR_RADIO_ESPNOW` instead selects the ESP-NOW adapter; with neither macro, the application uses `MockRadioAdapter`.

## Runtime architecture

`MainDevice` owns the UI state, feeds, message composition, display rendering, and alert dispatch. Port interfaces isolate it from concrete adapters.

- A FreeRTOS task explicitly pinned to core 0 polls the selected radio and services Arduino's synchronous `WebServer` every 10 ms.
- Arduino's `loop()` runs sensor polling, alert processing, keypad input, UI transitions, and rendering. The code does not explicitly pin this loop.
- A radio mutex protects radio access between the two execution paths. A recursive feed mutex protects UI/feed state.

The code is organized as ports and adapters under `lib/core/src`, but it is not a strict dependency-isolated Clean Architecture implementation: the application entry point constructs and coordinates the concrete adapters.

## Alert detection

Enabled alert sensors are polled every 250 ms. The configured main-application policy is:

| Input | Current trigger | Notes |
|---|---|---|
| MPU6050 gyroscope | Magnitude greater than `18.0` | `GyroscopeJerkAdapter` computes the derivative of angular velocity, so the value is angular acceleration in rad/s² despite the `Jerk` type name. |
| BMP280 altitude | Absolute change greater than `1.2 m` between consecutive successful altitude samples | Direction and elapsed time are not used; this is not descent speed. |
| Water sensor | Submerged state when normalized level is at least `0.25` or the wet flag is true, sustained for at least 1200 ms and 3 samples | Exit threshold is `0.15`, with an 800 ms dry interval. Evidence mode is `Either`. |

Duplicate numeric alerts are suppressed for 4000 ms when their observed values remain within the configured epsilon (`0.4` for the gyroscope path, `0.1 m` for altitude). Submersion state alerts use the same cooldown.

These are configurable software thresholds, not validated safety limits.

## RYLR998 radio and mesh

The default `node` build configures the module over `Serial2` at 115200 baud using values from `settings.h`:

- Band: `915000000` Hz
- Network ID: `18`
- Parameters: spreading factor `9`, coding-rate setting `7`, bandwidth setting `1`, preamble `12`
- Module address: `1`
- ESP32 UART pins: RX `16`, TX `17`
- RYLR998 broadcast destination address: `0`

The application packet format is:

```text
<TYPE>|<SENDER_UID_HEX>|<DEST_UID_HEX>|<MSG_ID>|<TTL>|<PAYLOAD>
```

| Field | Actual representation |
|---|---|
| `TYPE` | `M` for an original message, `A` for an alert, or `R` for a relayed `M`. Relayed alerts remain `A`. |
| `SENDER_UID_HEX` | ESP32 MAC bytes 2–5 rendered as hexadecimal without fixed-width padding. |
| `DEST_UID_HEX` | Hexadecimal destination; application-originated packets use `ffffffff` for broadcast. |
| `MSG_ID` | Decimal sequence number, incremented locally from startup. |
| `TTL` | Decimal remaining relay count. The configured initial value is `7`; a relay decrements it before scheduling. |
| `PAYLOAD` | Remaining text after the fifth `|`. |

The RYLR998 adapter maintains:

- A 50-entry circular `(sender UID, message ID)` deduplication cache.
- A 20-entry recent-message history used by `/mesh`.
- Five pending relay slots.
- A random 100–500 ms delay before forwarding.
- Cancellation of a matching pending relay when the same sender/message ID is overheard.

Packets are transmitted with `AT+SEND=0,<length>,<packet>`. The adapter reports a send as successful after writing the command; it does not wait for a module response or end-to-end acknowledgement. Constants for acknowledgement timeout and retries exist in `settings.h`, but acknowledgement/retry behavior is not implemented.

The ESP-NOW adapter uses a separate `TYPE|HOPS|PAYLOAD` broadcast format. It does not implement the RYLR998 mesh logic.

## User interface

The display model contains three strings (`line1`, `line2`, and a hint in `line3`). The active display adapter is constructed as 16 columns by 2 rows, so Serial output represents two display rows; the browser separately renders the third hint row.

### State controls

| State | Controls |
|---|---|
| Inbox | `A` menu; `B`/`C` previous/next message; `*`/`#` pan text left/right. |
| Menu | `1` compose; `2` alerts; `3`, `A`, or `#` return to inbox. |
| Alert feed | `A` menu; `B`/`C` previous/next alert; `*`/`#` pan text left/right. |
| Compose | `0`–`9` multi-tap entry; `B` commit pending character; `C` send; `D` backspace; `A` cancel. `*` and `#` are ignored. |
| Error | `A` acknowledges the error and returns to the inbox. |

Multi-tap commits after 900 ms:

| Key | Characters |
|---:|---|
| `1` | `1 . , ! ?` |
| `2` | `2 A B C` |
| `3` | `3 D E F` |
| `4` | `4 G H I` |
| `5` | `5 J K L` |
| `6` | `6 M N O` |
| `7` | `7 P Q R S` |
| `8` | `8 T U V` |
| `9` | `9 W X Y Z` |
| `0` | `0` and space |

Composed messages are capped at 160 characters. The inbox retains 24 entries, and the alert queue retains 8 entries.

## Web interface

With `kEnableWebServer = true`, startup creates:

- SSID: `PipSurvivor-Node-XXYY`, where `XXYY` is the final two MAC bytes in uppercase hexadecimal.
- Password: `survivor123!`
- Default ESP32 SoftAP address: normally `http://192.168.4.1/`; the assigned address is printed to Serial and should be treated as authoritative.

Endpoints:

| Request | Response |
|---|---|
| `GET /` | HTML virtual display and 4x4 keypad. Browser JavaScript polls `/lcd`. |
| `GET /lcd` | JSON object with `l1`, `l2`, and `l3` strings. This is a polled snapshot, not a stream. |
| `GET /key?k=<value>` | Queues `K0`–`K9`, `A`–`D`, `Star`, or `Hash`; always responds `OK`. Events are consumed only when `kEnableButtons` is true. |
| `GET /mesh` | JSON containing `my_uid`, up to 20 recent messages, and `total_rx`, `cache_size`, and `relay_jobs` statistics. Returns `503 Busy` if the radio mutex cannot be acquired within 50 ms. |
| `GET /feed` | `404 Removed`. |

## Wiring used by the main application

| Function | ESP32 pin(s) |
|---|---|
| I2C SDA / SCL | `21` / `22` |
| RYLR998 RX / TX | `16` / `17` |
| Water sensor analog input | `34` |
| Keypad rows | `32`, `33`, `25`, `26` |
| Keypad columns | `27`, `14`, `12`, `13` |

The sensor and radio modules must be wired and powered according to their own electrical specifications. The repository does not contain a complete power, battery, or protection design.

## Build

Prerequisites:

- PlatformIO Core or PlatformIO for VS Code.
- PlatformIO's Espressif32 platform and dependencies are resolved from `platformio.ini`.
- The configured board is `mhetesp32devkit`.

Build the default firmware:

```bash
pio run -e node
```

Upload and monitor when a compatible board is connected:

```bash
pio run -e node -t upload
pio device monitor -b 115200
```

Two PlatformIO hardware-example environments currently have buildable `.cpp` entry points:

```bash
pio run -e test_barometer
pio run -e test_sensor_combo
```

`test_uart` also has a `.cpp` entry point but requires a connected RYLR998 for a meaningful run.

The `test_alert_detection`, `test_tzt_water_level`, `test_tzt_submersion`, and `test_mpu6050_accel` PlatformIO environments point at `.ino` files outside PlatformIO's normal source directory and currently report `Nothing to build`. Those sketches must first be moved or converted into a valid source/library layout; see `examples/README.md`. `mpu6050_gyro_jerk_test` has no PlatformIO environment.

All example programs are hardware smoke tests. This repository contains no host-side automated test suite.

## Repository layout

```text
PipSurvivor/
├── platformio.ini
├── src/main.cpp
├── lib/core/src/
│   ├── settings.h
│   ├── ports/
│   └── adapters/
├── examples/
│   ├── README.md
│   ├── alert_detection_test/
│   ├── bmp280_altitude_test/
│   ├── mpu6050_accel_test/
│   ├── mpu6050_gyro_jerk_test/
│   ├── sensor_combo_test/
│   ├── tzt_submersion_test/
│   ├── tzt_water_level_test/
│   └── uart_radio_detect/
├── flows/user/
├── EMERGENCY_DETECT.md
├── MESH_HOP_VIS.md
├── top.stl
└── bottom.stl
```

`EMERGENCY_DETECT.md`, `MESH_HOP_VIS.md`, and `flows/user/lcd_flow.md` are design/reference documents and may describe intended behavior beyond the current implementation. The source and `settings.h` define current behavior.

## Enclosure files

`top.stl` and `bottom.stl` are binary STL meshes included at the repository root. Review their dimensions, clearances, printability, and fit in a slicer/CAD tool before fabrication; the repository does not establish hardware compatibility, material choice, ruggedness, water resistance, or wearability.

## License status

This repository does not currently include a license file. Do not assume it is MIT-licensed from prior README text.
