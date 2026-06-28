# Cardputer IR Sender

A simple infrared transmitter for the **M5Stack Cardputer**, designed to be compiled and uploaded with the Arduino IDE.

The program lets you type IR commands with the Cardputer keyboard, or connect to the Cardputer's own WiFi access point and use a small web page, then transmit commands using its built-in IR LED on GPIO 44. The display automatically turns off after 20 seconds without keyboard input and turns back on when you press a key.

## Supported command formats

| Prefix | Protocol | Format |
|---|---|---|
| `N` | NEC | `N <address> <command> [repeats]` |
| `R` | RC6 | `R <address> <command> [repeats]` |
| `M` | Old-style 32-bit NEC/MSB | `M <32-bit-code> [repeats]` |

Examples:

```text
N 00FF 18
R 00 0C 1
M 20DF10EF
```

## Philips TV power command

For many Philips TVs using RC6, try:

```text
R 00 0C 1
```

This is a **power toggle** command. It may turn the TV off when the TV is already on. IR codes can vary by model and firmware.

## Web remote access point

On boot, the Cardputer starts a direct WiFi access point:

| Setting | Value |
|---|---|
| SSID | `Cardputer-IR` |
| Password | `12345678` |
| Web address | Captive portal redirect or `http://192.168.4.1/` |

To use it:

1. Power on the Cardputer and keep it pointed at the device you want to control.
2. Connect your phone or computer to the `Cardputer-IR` WiFi network.
3. Wait for your device to open or offer the captive portal page automatically. If it does not, open `http://192.168.4.1/` in a browser.
4. Press a quick button or enter any supported IR command and tap **Send command**.

The Cardputer keyboard still works while the web remote is running.

## Arduino IDE setup

1. Install the M5Stack board package through **Boards Manager**.
2. Install these libraries through **Library Manager**:
   - `M5Cardputer`
   - `IRremote` by Armin Joachimsmeyer
   - `WiFi`, `WebServer`, and `DNSServer` from the ESP32 board package
3. Select the **M5Cardputer** board.
4. Open `Cardputer_IR_Sender/Cardputer_IR_Sender.ino`.
5. Select the correct USB port and upload.

## Controls

- Type a command using the built-in keyboard.
- Press **Backspace** to remove a character.
- Press **Enter** to transmit.
- Point the top of the Cardputer toward the device's IR receiver.
- Leave the keyboard idle for 20 seconds to turn the screen off automatically; press any key to wake it.

## Notes

- Repeat values are limited to `0` through `9`.
- The built-in transmitter may have less range than a normal remote.
- This project transmits only; it does not learn or receive codes.
