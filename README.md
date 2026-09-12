# LoRa Morse Link

Two identical ESP32 stations that send Morse-code dots and dashes to each other over 433 MHz LoRa radio.

Each station has:

- ESP32 Dev Module
- SX1278 / Ra-02 433 MHz LoRa module
- 1.3-inch 128×64 SH1106 I2C OLED
- Send button
- Clear button
- Active buzzer
- Red transmit LED
- Blue receive LED

Flash the same firmware onto both ESP32 boards.

---

## Features

- Send Morse code with one push button.
- Receive and decode Morse code from the other station.
- Red LED flashes while transmitting.
- Blue LED flashes and buzzer sounds when receiving.
- SH1106 OLED shows outgoing marks, incoming marks, decoded text, and LoRa status.
- Short animated activity bar for sent/received marks.
- Clear button clears the local display text.

---

## Parts needed

Build **two matching stations**:

| Part | Total quantity |
|---|---:|
| ESP32 Dev Module | 2 |
| SX1278 / Ra-02 LoRa module, 433 MHz | 2 |
| SH1106 1.3-inch I2C OLED, 128×64 | 2 |
| 3.3 V active buzzer | 2 |
| Red LED | 2 |
| Blue LED | 2 |
| 220 Ω resistor | 4 |
| Momentary push button | 4 |
| 433 MHz LoRa antenna | 2 |
| Breadboards and jumper wires | as needed |

> **Important:** The SX1278 and OLED use **3.3 V**. Do not connect either module to 5 V.

---

## Wiring for each station

Both stations use the same wiring.

### SH1106 OLED

| OLED pin | ESP32 connection | Job |
|---|---|---|
| VCC | 3V3 | power |
| GND | GND | ground |
| SDA | GPIO21 | screen data |
| SCL | GPIO22 | screen clock |

### SX1278 / Ra-02 LoRa module

| LoRa pin | ESP32 connection | Job |
|---|---|---|
| VCC | 3V3 | power |
| GND | GND | ground |
| SCK | GPIO18 | radio clock |
| MISO | GPIO19 | data from radio |
| MOSI | GPIO23 | data to radio |
| NSS / CS | GPIO16 | select the radio |
| RESET | GPIO26 | reset the radio |
| DIO0 | GPIO25 | receive/transmit signal |

Attach a suitable **433 MHz antenna** before powering or transmitting with the LoRa module.

### Buttons

The firmware uses the ESP32’s internal pull-up resistors, so no extra resistor is needed for either button.

| Button | One side | Other side | Job |
|---|---|---|---|
| Send / Morse key | GPIO4 | GND | dot and dash input |
| Clear button | GPIO14 | GND | clears local display text |

### LEDs

Each LED needs its own 220 Ω resistor.

| LED | Wiring |
|---|---|
| Red transmit LED | GPIO32 → 220 Ω resistor → LED long leg; LED short leg → GND |
| Blue receive LED | GPIO13 → 220 Ω resistor → LED long leg; LED short leg → GND |

The LED long leg is positive. The short leg, usually beside the flat edge of the LED body, goes to GND.

### Active buzzer

| Buzzer pin | ESP32 connection | Job |
|---|---|---|
| Positive / SIGNAL | GPIO33 | sound signal |
| Negative / GND | GND | ground |

---

## PlatformIO configuration

Create a file named `platformio.ini` beside the `src` folder:

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino

lib_deps =
  sandeepmistry/LoRa @ 0.8.0
  olikraus/U8g2 @ 2.36.5

monitor_speed = 115200
```


---

## Using the Morse stations

1. Build and power both stations.
2. Flash the same `src/main.cpp` firmware to both ESP32 boards.
3. The OLED should show `MORSE LINK` and `RADIO READY`.
4. Use the **Send** button:
   - quick tap = dot (`.`)
   - hold longer than about 250 ms = dash (`-`)
5. Wait about one second after the final mark of a letter. The station sends the completed Morse character.
6. The other station flashes its blue LED, beeps, and adds the decoded letter to its OLED message line.
7. Press **Clear** to erase the local sent/received display text.

### Example: send SOS

```text
S = ...     tap three times, then wait
O = ---     hold three times, then wait
S = ...     tap three times, then wait
```

---

## Display layout

The 1.3-inch SH1106 display has a 128×64 pixel screen:

- **Top:** `MORSE LINK` and radio status
- **Left card:** marks currently being sent
- **Right card:** marks currently being received
- **Middle:** moving radio activity bar during a transmission
- **Bottom:** decoded received letters

The screen updates only when a visible state changes, except for the brief activity animation after a transmission or reception.

---

## Troubleshooting

### OLED stays blank

Check these physical connections:

- OLED **VCC → 3V3**, not 5 V.
- OLED **GND → GND**.
- OLED **SDA → GPIO21**.
- OLED **SCL → GPIO22**.

### The stations do not receive messages

Check:

- Both modules are **SX1278 433 MHz** versions.
- Both modules have a 433 MHz antenna attached.
- Both LoRa VCC pins are connected to **3V3**.
- Both stations use the same firmware and the same `433E6` frequency setting.
- NSS/CS goes to GPIO16, RESET to GPIO26, and DIO0 to GPIO25.

### The compiler reports a `drawArc` error

U8g2 `drawArc()` uses one radius value. The valid call has five arguments:

```cpp
display.drawArc(x + 5, y + 6, 5 + wave, 150, 234);
```

Do not use a version with two radius values.

### Button presses do nothing

- Make sure the button crosses the center gap of the breadboard.
- One button connection must go to the GPIO pin and the other to GND.
- If using a four-leg tactile button, rotate it 90 degrees if it appears permanently pressed or never responds.

---

## Safety notes

- Do not use 5 V with the SX1278 LoRa module or SH1106 OLED.
- Do not connect LEDs directly to ESP32 GPIO pins; keep the 220 Ω series resistors installed.
- Do not transmit with the SX1278 module disconnected from its antenna.
- Keep radio wiring short to reduce communication problems.

---

## Power

Each station is powered through the ESP32 USB port. Expected current draw is approximately **250 mA per station**, depending on LoRa transmit activity and the OLED brightness.
