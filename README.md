# Cardputer IR Sender

A simple infrared transmitter for the **M5Stack Cardputer**, designed to be compiled and uploaded with the Arduino IDE.

The program lets you type IR commands with the Cardputer keyboard, switch the Cardputer keyboard into a TV-style remote mode, or connect to the Cardputer's own WiFi access point and use a small web page, then transmit commands using its built-in IR LED on GPIO 44. The Cardputer screen and the web remote both show the current battery level. The display automatically turns off after 20 seconds without keyboard input and turns back on when you press a key.

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
4. Press a quick button, use one of the keyboard shortcuts shown beside the web buttons, or enter any supported IR command and tap **Send command**.

The battery bar on the web remote is updated each time the page is loaded or refreshed. The Cardputer keyboard still works while the web remote is running.

## Arduino IDE setup

1. Install or update the M5Stack board package through **Boards Manager**. Use M5Stack board manager version `3.2.2` or newer.
2. Install or update these libraries through **Library Manager**:
   - `M5Cardputer` version `1.1.0` or newer
   - `M5Unified` version `0.2.8` or newer
   - `M5GFX` version `0.2.10` or newer
   - `IRremote` by Armin Joachimsmeyer
   - `WiFi`, `WebServer`, and `DNSServer` from the ESP32 board package
3. Select the **M5Cardputer** board.
4. Open `Cardputer_IR_Sender/Cardputer_IR_Sender.ino`.
5. Select the correct USB port and upload.

## Troubleshooting compile errors

If the Arduino IDE reports `'M5Stack' does not name a type` on a line that says `M5Stack Cardputer - Interactive IR Sender`, the sketch was copied without the opening `/*` comment marker. Open the checked-in `Cardputer_IR_Sender/Cardputer_IR_Sender.ino` file directly, or make sure the title block starts with `/*` and ends with `*/`.

If compilation fails inside the M5Cardputer keyboard headers with errors such as `'Point2D_t' was not declared in this scope`, update the M5Stack board package and the `M5Cardputer`, `M5Unified`, and `M5GFX` libraries to the minimum versions listed above. This sketch includes `M5GFX.h` before `M5Cardputer.h` so the display and keyboard types are available to the Cardputer library.

## Controls

- Type a command using the built-in keyboard.
- Press **Backspace** to remove a character.
- Press **Enter** to transmit.
- Press **Ctrl** to toggle keyboard remote mode on the Cardputer. In remote mode, keys send common Philips RC6 remote commands immediately instead of editing the command line.
- Remote mode shortcuts: **P** = pause, **Space** = play, **+**/**-** = volume up/down, **arrow-labeled keys** (`/`, `,`, `;`, `.`) or **U/D/L/R** = up/down/left/right, **Enter** = OK, **Backspace** or **B** = back, **M** = mute, **H** = home, **S** = source, **I** = info, **N** = Netflix, and **O** or **Q** = power.
- The web remote shows matching keyboard shortcut badges next to supported buttons and sends those commands when the command field is not focused.
- Point the top of the Cardputer toward the device's IR receiver.
- Leave the keyboard idle for 20 seconds to turn the screen off automatically; press any key to wake it.

## Notes

- Repeat values are limited to `0` through `9`.
- The built-in transmitter may have less range than a normal remote.
- This project transmits only; it does not learn or receive codes.
