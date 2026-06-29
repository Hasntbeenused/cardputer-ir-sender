/*
  M5Stack Cardputer - Interactive IR Sender

  Supported input formats:

  NEC:
    N <address> <command> [repeats]

  RC6:
    R <address> <command> [repeats]

  Old-style 32-bit NEC:
    M <32-bit-code> [repeats]

  Examples:
    N 00FF 18
    N 1111 34 2
    R 00 0C 1
    M 20DF10EF
    M 20DF10EF 2

  Philips TV power toggle:
    R 00 0C 1

  Press Backspace to delete.
  Press Enter to transmit.
*/

#define DISABLE_CODE_FOR_RECEIVER
#define SEND_PWM_BY_TIMER
#define IR_TX_PIN 44

#include <M5GFX.h>
#include <M5Cardputer.h>
#include <IRremote.hpp>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

String inputLine = "";
String statusLine = "Ready";
bool statusIsError = false;

const char *AP_SSID = "Cardputer-IR";
const char *AP_PASSWORD = "12345678";

WebServer server(80);
DNSServer dnsServer;

const byte DNS_PORT = 53;
const uint32_t SCREEN_TIMEOUT_MS = 20000;
const uint8_t SCREEN_BRIGHTNESS = 100;

unsigned long lastKeyboardInputMillis = 0;
bool screenIsOn = true;
bool keyboardRemoteMode = false;

const uint8_t MAX_CUSTOM_COMMANDS = 24;

struct CustomCommand {
  String command;
  String shortcut;
  String description;
};

CustomCommand customCommands[MAX_CUSTOM_COMMANDS];
uint8_t customCommandCount = 0;
String customCommandCsv = "";

bool sendCustomShortcut(char key);

int getBatteryLevelPercent() {
  int level = M5Cardputer.Power.getBatteryLevel();

  if (level < 0) {
    return 0;
  }

  if (level > 100) {
    return 100;
  }

  return level;
}

String getBatteryLevelText() {
  return String(getBatteryLevelPercent()) + "%";
}

String getBatteryBarClass(int batteryLevel) {
  if (batteryLevel <= 20) {
    return "low";
  }

  if (batteryLevel <= 50) {
    return "medium";
  }

  return "high";
}

void drawBatteryBar(int batteryLevel) {
  auto &display = M5Cardputer.Display;
  const int outerX = display.width() - 48;
  const int outerY = 3;
  const int outerWidth = 34;
  const int outerHeight = 12;
  const int fillWidth = map(batteryLevel, 0, 100, 0, outerWidth - 4);
  uint16_t fillColor = batteryLevel <= 20 ? RED : (batteryLevel <= 50 ? YELLOW : GREEN);

  display.drawRect(outerX, outerY, outerWidth, outerHeight, WHITE);
  display.fillRect(outerX + outerWidth, outerY + 3, 3, 6, WHITE);
  display.fillRect(outerX + 2, outerY + 2, outerWidth - 4, outerHeight - 4, BLACK);

  if (fillWidth > 0) {
    display.fillRect(outerX + 2, outerY + 2, fillWidth, outerHeight - 4, fillColor);
  }

  display.setTextColor(WHITE, BLACK);
  display.setCursor(outerX - 24, 6);
  display.printf("%3d", batteryLevel);
  display.print("%");
}

String shortenText(const String &text, size_t maximumLength) {
  if (text.length() <= maximumLength) {
    return text;
  }

  return "..." + text.substring(text.length() - maximumLength + 3);
}

void turnScreenOn() {
  if (!screenIsOn) {
    M5Cardputer.Display.setBrightness(SCREEN_BRIGHTNESS);
    screenIsOn = true;
  }
}

void updateScreenTimeout() {
  if (screenIsOn && millis() - lastKeyboardInputMillis >= SCREEN_TIMEOUT_MS) {
    M5Cardputer.Display.setBrightness(0);
    screenIsOn = false;
  }
}

void drawScreen() {
  turnScreenOn();
  auto &display = M5Cardputer.Display;

  display.fillScreen(BLACK);
  display.setTextFont(1);
  display.setTextSize(1);
  display.setTextWrap(false);

  int batteryLevel = getBatteryLevelPercent();

  display.setTextColor(CYAN, BLACK);
  display.setCursor(4, 3);
  display.println("CARDPUTER IR SENDER");
  drawBatteryBar(batteryLevel);

  display.setTextColor(WHITE, BLACK);
  display.setCursor(4, 18);
  display.println("N addr cmd [rep]  NEC");
  display.setCursor(4, 30);
  display.println("R addr cmd [rep]  RC6");
  display.setCursor(4, 42);
  display.println("M code [rep]      NEC-MSB");

  display.setTextColor(YELLOW, BLACK);
  display.setCursor(4, 56);
  display.println("Philips power: R 00 0C 1");

  display.setTextColor(MAGENTA, BLACK);
  display.setCursor(4, 66);
  display.println("WiFi: Cardputer-IR / 12345678");

  display.setTextColor(keyboardRemoteMode ? ORANGE : DARKGREY, BLACK);
  display.setCursor(4, 92);
  display.println(keyboardRemoteMode ? "REMOTE: Ctrl exit, arrows/Enter, +/-" : "Ctrl = keyboard remote mode");

  display.setTextColor(statusIsError ? RED : GREEN, BLACK);
  display.setCursor(4, 80);
  display.print(shortenText(statusLine, 38));

  display.drawFastHLine(0, 102, display.width(), DARKGREY);

  display.setTextColor(WHITE, BLACK);
  display.setCursor(4, 110);
  display.print("> ");

  display.setTextColor(YELLOW, BLACK);
  display.print(shortenText(inputLine, 36));
}

String nextToken(String &text) {
  text.trim();

  if (text.length() == 0) {
    return "";
  }

  int spacePosition = text.indexOf(' ');

  if (spacePosition < 0) {
    String token = text;
    text = "";
    return token;
  }

  String token = text.substring(0, spacePosition);
  text = text.substring(spacePosition + 1);
  text.trim();
  return token;
}

bool isHexCharacter(char character) {
  return (
    (character >= '0' && character <= '9') ||
    (character >= 'A' && character <= 'F') ||
    (character >= 'a' && character <= 'f')
  );
}

bool parseHexValue(String token, uint32_t &result, uint8_t maximumDigits) {
  token.trim();

  if (token.startsWith("0X") || token.startsWith("0x")) {
    token.remove(0, 2);
  }

  if (token.length() == 0 || token.length() > maximumDigits) {
    return false;
  }

  for (size_t i = 0; i < token.length(); i++) {
    if (!isHexCharacter(token.charAt(i))) {
      return false;
    }
  }

  result = strtoul(token.c_str(), nullptr, 16);
  return true;
}

bool parseRepeatCount(String token, uint8_t &result) {
  token.trim();

  if (token.length() == 0) {
    result = 0;
    return true;
  }

  for (size_t i = 0; i < token.length(); i++) {
    if (!isDigit(token.charAt(i))) {
      return false;
    }
  }

  int value = token.toInt();

  if (value < 0 || value > 9) {
    return false;
  }

  result = static_cast<uint8_t>(value);
  return true;
}

void sendModernNEC(String arguments) {
  String addressToken = nextToken(arguments);
  String commandToken = nextToken(arguments);
  String repeatToken = nextToken(arguments);

  if (addressToken.length() == 0 || commandToken.length() == 0) {
    statusLine = "Use: N address command";
    statusIsError = true;
    return;
  }

  if (arguments.length() > 0) {
    statusLine = "Too many parameters";
    statusIsError = true;
    return;
  }

  uint32_t address;
  uint32_t command;
  uint8_t repeats;

  if (!parseHexValue(addressToken, address, 4)) {
    statusLine = "Invalid NEC address";
    statusIsError = true;
    return;
  }

  if (!parseHexValue(commandToken, command, 2)) {
    statusLine = "NEC command must be 00-FF";
    statusIsError = true;
    return;
  }

  if (!parseRepeatCount(repeatToken, repeats)) {
    statusLine = "Repeats must be 0-9";
    statusIsError = true;
    return;
  }

  IrSender.sendNEC(
    static_cast<uint16_t>(address),
    static_cast<uint16_t>(command),
    repeats
  );

  statusLine =
    "NEC sent A:" + addressToken +
    " C:" + commandToken +
    " R:" + String(repeats);

  statusIsError = false;
  Serial.println(statusLine);
}

void sendOldStyleNEC(String arguments) {
  String codeToken = nextToken(arguments);
  String repeatToken = nextToken(arguments);

  if (codeToken.length() == 0) {
    statusLine = "Use: M 20DF10EF";
    statusIsError = true;
    return;
  }

  if (arguments.length() > 0) {
    statusLine = "Too many parameters";
    statusIsError = true;
    return;
  }

  uint32_t code;
  uint8_t repeats;

  if (!parseHexValue(codeToken, code, 8)) {
    statusLine = "Invalid 32-bit NEC code";
    statusIsError = true;
    return;
  }

  if (!parseRepeatCount(repeatToken, repeats)) {
    statusLine = "Repeats must be 0-9";
    statusIsError = true;
    return;
  }

  for (uint8_t i = 0; i <= repeats; i++) {
    IrSender.sendNECMSB(code, 32, false);

    if (i < repeats) {
      delay(110);
    }
  }

  statusLine =
    "NEC-MSB sent:" + codeToken +
    " R:" + String(repeats);

  statusIsError = false;
  Serial.println(statusLine);
}

void sendRC6Command(String arguments) {
  String addressToken = nextToken(arguments);
  String commandToken = nextToken(arguments);
  String repeatToken = nextToken(arguments);

  if (addressToken.length() == 0 || commandToken.length() == 0) {
    statusLine = "Use: R address command";
    statusIsError = true;
    return;
  }

  if (arguments.length() > 0) {
    statusLine = "Too many parameters";
    statusIsError = true;
    return;
  }

  uint32_t address;
  uint32_t command;
  uint8_t repeats;

  if (!parseHexValue(addressToken, address, 2)) {
    statusLine = "RC6 address must be 00-FF";
    statusIsError = true;
    return;
  }

  if (!parseHexValue(commandToken, command, 2)) {
    statusLine = "RC6 command must be 00-FF";
    statusIsError = true;
    return;
  }

  if (!parseRepeatCount(repeatToken, repeats)) {
    statusLine = "Repeats must be 0-9";
    statusIsError = true;
    return;
  }

  IrSender.sendRC6(
    static_cast<uint8_t>(address),
    static_cast<uint8_t>(command),
    repeats,
    true
  );

  statusLine =
    "RC6 sent A:" + addressToken +
    " C:" + commandToken +
    " R:" + String(repeats);

  statusIsError = false;
  Serial.println(statusLine);
}

void processCommand(String commandLine) {
  commandLine.trim();
  commandLine.toUpperCase();

  if (commandLine.length() == 0) {
    statusLine = "Nothing entered";
    statusIsError = true;
    return;
  }

  String mode = nextToken(commandLine);

  if (mode == "N") {
    sendModernNEC(commandLine);
  } else if (mode == "M") {
    sendOldStyleNEC(commandLine);
  } else if (mode == "R") {
    sendRC6Command(commandLine);
  } else {
    statusLine = "Start with N, R or M";
    statusIsError = true;
  }
}

void sendKeyboardRemoteCommand(const char *label, const char *command) {
  processCommand(String(command));

  if (!statusIsError) {
    statusLine = String("Remote ") + label;
  }
}

bool handleKeyboardRemoteKey(const Keyboard_Class::KeysState &keys) {
  if (keys.enter) {
    sendKeyboardRemoteCommand("OK", "R 00 5C 1");
    return true;
  }

  if (keys.del) {
    sendKeyboardRemoteCommand("Back", "R 00 0A 1");
    return true;
  }

  for (char character : keys.word) {
    char key = tolower(character);

    if (customCommandCount > 0) {
      return sendCustomShortcut(key);
    }

    if (key == 'p') {
      sendKeyboardRemoteCommand("Pause", "R 00 30 1");
      return true;
    }

    if (key == ' ') {
      sendKeyboardRemoteCommand("Play", "R 00 2C 1");
      return true;
    }

    if (key == '+' || key == '=') {
      sendKeyboardRemoteCommand("Volume up", "R 00 10 1");
      return true;
    }

    if (key == '-' || key == '_') {
      sendKeyboardRemoteCommand("Volume down", "R 00 11 1");
      return true;
    }

    if (key == 'm') {
      sendKeyboardRemoteCommand("Mute", "R 00 0D 1");
      return true;
    }

    if (key == 'u' || key == '/') {
      sendKeyboardRemoteCommand("Up", "R 00 58 1");
      return true;
    }

    if (key == 'd' || key == ',') {
      sendKeyboardRemoteCommand("Down", "R 00 59 1");
      return true;
    }

    if (key == 'l' || key == ';') {
      sendKeyboardRemoteCommand("Left", "R 00 5A 1");
      return true;
    }

    if (key == 'r' || key == '.') {
      sendKeyboardRemoteCommand("Right", "R 00 5B 1");
      return true;
    }

    if (key == 'h') {
      sendKeyboardRemoteCommand("Home", "R 00 54 1");
      return true;
    }

    if (key == 'b') {
      sendKeyboardRemoteCommand("Back", "R 00 0A 1");
      return true;
    }

    if (key == 's') {
      sendKeyboardRemoteCommand("Source", "R 00 38 1");
      return true;
    }

    if (key == 'i') {
      sendKeyboardRemoteCommand("Info", "R 00 0F 1");
      return true;
    }

    if (key == 'a') {
      sendKeyboardRemoteCommand("Ambilight", "R 00 8F 1");
      return true;
    }

    if (key == 'n') {
      sendKeyboardRemoteCommand("Netflix", "R 00 76 1");
      return true;
    }

    if (key == 'o' || key == 'q') {
      sendKeyboardRemoteCommand("Power", "R 00 0C 1");
      return true;
    }
  }

  return false;
}

String htmlEscape(const String &text) {
  String escaped = "";

  for (size_t i = 0; i < text.length(); i++) {
    char character = text.charAt(i);

    if (character == '&') {
      escaped += "&amp;";
    } else if (character == '<') {
      escaped += "&lt;";
    } else if (character == '>') {
      escaped += "&gt;";
    } else if (character == '"') {
      escaped += "&quot;";
    } else {
      escaped += character;
    }
  }

  return escaped;
}


String jsEscape(const String &text) {
  String escaped = "";

  for (size_t i = 0; i < text.length(); i++) {
    char character = text.charAt(i);

    if (character == '\\' || character == '\'') {
      escaped += '\\';
      escaped += character;
    } else if (character == '\n') {
      escaped += "\\n";
    } else {
      escaped += character;
    }
  }

  return escaped;
}

String urlEncode(const String &text) {
  const char *hex = "0123456789ABCDEF";
  String encoded = "";

  for (size_t i = 0; i < text.length(); i++) {
    char character = text.charAt(i);

    if (isAlphaNumeric(character) || character == '-' || character == '_' || character == '.' || character == '~') {
      encoded += character;
    } else if (character == ' ') {
      encoded += "%20";
    } else {
      encoded += '%';
      encoded += hex[(character >> 4) & 0x0F];
      encoded += hex[character & 0x0F];
    }
  }

  return encoded;
}

String getDelimitedField(String &line) {
  int separatorPosition = line.indexOf(';');

  if (separatorPosition < 0) {
    String field = line;
    line = "";
    field.trim();
    return field;
  }

  String field = line.substring(0, separatorPosition);
  line = line.substring(separatorPosition + 1);
  field.trim();
  return field;
}

String normalizeCommandWithRepeats(String command, String repeats) {
  command.trim();
  repeats.trim();

  if (repeats.length() == 0) {
    return command;
  }

  String probe = command;
  String mode = nextToken(probe);
  nextToken(probe);
  nextToken(probe);
  String existingRepeats = nextToken(probe);

  if (existingRepeats.length() > 0 || probe.length() > 0) {
    return command;
  }

  return command + " " + repeats;
}

bool setCustomCommandList(String csvText) {
  CustomCommand parsedCommands[MAX_CUSTOM_COMMANDS];
  uint8_t parsedCount = 0;
  int startPosition = 0;

  csvText.replace("\r", "");

  while (startPosition <= csvText.length()) {
    int endPosition = csvText.indexOf('\n', startPosition);

    if (endPosition < 0) {
      endPosition = csvText.length();
    }

    String line = csvText.substring(startPosition, endPosition);
    line.trim();

    if (line.length() > 0) {
      if (line.indexOf(';') < 0) {
        statusLine = "List rows need semicolons";
        statusIsError = true;
        return false;
      }

      if (parsedCount >= MAX_CUSTOM_COMMANDS) {
        statusLine = "List max is " + String(MAX_CUSTOM_COMMANDS);
        statusIsError = true;
        return false;
      }

      String row = line;
      String command = getDelimitedField(row);
      String shortcut = getDelimitedField(row);
      String description = getDelimitedField(row);
      String repeats = getDelimitedField(row);

      if (command.length() == 0 || description.length() == 0) {
        statusLine = "List needs command and description";
        statusIsError = true;
        return false;
      }

      parsedCommands[parsedCount].command = normalizeCommandWithRepeats(command, repeats);
      parsedCommands[parsedCount].shortcut = shortcut;
      parsedCommands[parsedCount].description = description;
      parsedCount++;
    }

    if (endPosition == csvText.length()) {
      break;
    }

    startPosition = endPosition + 1;
  }

  for (uint8_t i = 0; i < parsedCount; i++) {
    customCommands[i] = parsedCommands[i];
  }

  customCommandCount = parsedCount;
  customCommandCsv = csvText;
  statusLine = parsedCount == 0 ? "Custom list cleared" : "Custom list loaded: " + String(parsedCount);
  statusIsError = false;
  return true;
}

bool sendCustomShortcut(char key) {
  for (uint8_t i = 0; i < customCommandCount; i++) {
    if (customCommands[i].shortcut.length() == 1 && tolower(customCommands[i].shortcut.charAt(0)) == key) {
      sendKeyboardRemoteCommand(customCommands[i].description.c_str(), customCommands[i].command.c_str());
      return true;
    }
  }

  return false;
}

void redirectToWebRemote() {
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "Redirecting to Cardputer IR Remote");
}

void sendWebPage() {
  int batteryLevel = getBatteryLevelPercent();
  String escapedStatus = htmlEscape(statusLine);
  String statusClass = statusIsError ? "error" : "ok";
  String batteryClass = getBatteryBarClass(batteryLevel);
  String page =
    "<!doctype html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Cardputer IR Remote</title>"
    "<style>"
    "body{font-family:system-ui,Arial,sans-serif;margin:0;background:#111;color:#eee;}"
    "main{max-width:520px;margin:auto;padding:18px;}"
    ".topbar{display:flex;align-items:center;justify-content:space-between;gap:12px;margin:0 0 12px;}"
    "h1{font-size:1.5rem;margin:0;}"
    ".battery{display:flex;align-items:center;gap:8px;color:#ddd;font-weight:700;}"
    ".battery-shell{position:relative;width:52px;height:22px;border:2px solid #ddd;border-radius:5px;padding:2px;}"
    ".battery-shell:after{content:'';position:absolute;right:-7px;top:6px;width:5px;height:8px;background:#ddd;border-radius:0 3px 3px 0;}"
    ".battery-fill{height:100%;border-radius:2px;background:#63d471;}"
    ".battery-fill.medium{background:#f6c453}.battery-fill.low{background:#ff6b6b}"
    ".card{background:#1d1d1d;border-radius:14px;padding:14px;margin:12px 0;}"
    ".status{font-weight:700}.ok{color:#63d471}.error{color:#ff6b6b}"
    "form{display:grid;gap:10px}"
    "input,textarea,button{font:inherit;border-radius:10px;border:0;padding:12px;}"
    "input,textarea{background:#2b2b2b;color:#fff;}"
    "textarea{min-height:130px;resize:vertical;}"
    "button{background:#00a6ff;color:#fff;font-weight:700;}"
    ".buttons{display:grid;grid-template-columns:1fr 1fr;gap:10px;}"
    ".buttons a{background:#333;color:#fff;text-align:center;text-decoration:none;border-radius:10px;padding:12px;}"
    "details summary{cursor:pointer;font-weight:700;font-size:1.2rem;}"
    ".command-list{display:grid;gap:8px;margin-top:12px;}"
    ".command-list a{display:grid;grid-template-columns:1fr auto;gap:8px;align-items:center;background:#2b2b2b;color:#fff;text-decoration:none;border-radius:10px;padding:10px;}"
    ".command-list span{color:#aaa;font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-size:.9rem;}"
    ".custom-empty{color:#aaa;margin-top:10px;}"
    ".shortcut{display:inline-block;margin-left:6px;padding:2px 7px;border:1px solid #666;border-radius:6px;background:#111;color:#ddd;font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-size:.8rem;}"
    ".shortcut-help{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:6px;margin-top:10px;color:#ccc;}"
    "small{color:#aaa;}"
    "</style></head><body><main>"
    "<div class='topbar'><h1>Cardputer IR Remote</h1>"
    "<div class='battery' aria-label='Battery level'><div class='battery-shell'><div class='battery-fill ";

  page += batteryClass;
  page += "' style='width:";
  page += String(batteryLevel);
  page += "%'></div></div><span>";
  page += getBatteryLevelText();
  page += "</span></div></div>";
  page += "<div class='card status ";
  page += statusClass;
  page += "'>Status: ";
  page += escapedStatus;
  page +=
    "</div>"
    "<div class='card'><form action='/send' method='get'>"
    "<label for='cmd'>IR command</label>"
    "<input id='cmd' name='cmd' value='R 00 0C 1' autocomplete='off'>"
    "<button type='submit'>Send command</button>"
    "<small>Formats: N &lt;address&gt; &lt;command&gt; [repeats], "
    "R &lt;address&gt; &lt;command&gt; [repeats], M &lt;32-bit-code&gt; [repeats]</small>"
    "</form></div>"
    "<div class='card'><form action='/list' method='post'>"
    "<label for='cmdlist'>Custom semicolon command list</label>"
    "<textarea id='cmdlist' name='list' placeholder='R 00 0C;O;Power toggle;1&#10;R 00 10;+;Volume up;1'>";
  page += htmlEscape(customCommandCsv);
  page +=
    "</textarea>"
    "<button type='submit'>Replace keyboard/buttons</button>"
    "<small>One row per button: IR command; keyboard letter; description; repeats. When this list has rows, its shortcuts replace the default keyboard remote shortcuts.</small>"
    "</form></div>"
    "<div class='card'><h2>Quick buttons</h2><div class='buttons'>"
    "<a href='/send?cmd=R%2000%200C%201'>Philips Power <kbd class='shortcut'>O</kbd></a>"
    "<a href='/send?cmd=M%2020DF10EF'>NEC-MSB Power</a>"
    "</div><div class='shortcut-help'><small>Keyboard: Ctrl toggles Cardputer remote mode.</small><small>Web: shortcuts work when the command field is not focused.</small></div></div>"
    "<div class='card'><details open><summary>Custom command buttons</summary><div class='command-list'>";
  if (customCommandCount == 0) {
    page += "<p class='custom-empty'>Paste a semicolon list above to create custom buttons.</p>";
  } else {
    for (uint8_t i = 0; i < customCommandCount; i++) {
      page += "<a href='/send?cmd=" + urlEncode(customCommands[i].command) + "'><strong>" + htmlEscape(customCommands[i].description);
      if (customCommands[i].shortcut.length() > 0) {
        page += " <kbd class='shortcut'>" + htmlEscape(customCommands[i].shortcut) + "</kbd>";
      }
      page += "</strong><span>" + htmlEscape(customCommands[i].command) + "</span></a>";
    }
  }
  page +=
    "</div></details></div>"
    "<div class='card'><details open><summary>Main IR commands</summary><div class='command-list'>"
    "<a href='/send?cmd=R%2000%200C%201'><strong>Power toggle <kbd class='shortcut'>O</kbd></strong><span>RC6 0x00 0x0C</span></a>"
    "<a href='/send?cmd=R%2000%2010%201'><strong>Volume up <kbd class='shortcut'>+</kbd></strong><span>RC6 0x00 0x10</span></a>"
    "<a href='/send?cmd=R%2000%2011%201'><strong>Volume down <kbd class='shortcut'>-</kbd></strong><span>RC6 0x00 0x11</span></a>"
    "<a href='/send?cmd=R%2000%200D%201'><strong>Mute <kbd class='shortcut'>M</kbd></strong><span>RC6 0x00 0x0D</span></a>"
    "<a href='/send?cmd=R%2000%2020%201'><strong>Channel up</strong><span>RC6 0x00 0x20</span></a>"
    "<a href='/send?cmd=R%2000%2021%201'><strong>Channel down</strong><span>RC6 0x00 0x21</span></a>"
    "<a href='/send?cmd=R%2000%2058%201'><strong>Up <kbd class='shortcut'>↑</kbd>/<kbd class='shortcut'>U</kbd></strong><span>RC6 0x00 0x58</span></a>"
    "<a href='/send?cmd=R%2000%2059%201'><strong>Down <kbd class='shortcut'>↓</kbd>/<kbd class='shortcut'>D</kbd></strong><span>RC6 0x00 0x59</span></a>"
    "<a href='/send?cmd=R%2000%205A%201'><strong>Left <kbd class='shortcut'>←</kbd>/<kbd class='shortcut'>L</kbd></strong><span>RC6 0x00 0x5A</span></a>"
    "<a href='/send?cmd=R%2000%205B%201'><strong>Right <kbd class='shortcut'>→</kbd>/<kbd class='shortcut'>R</kbd></strong><span>RC6 0x00 0x5B</span></a>"
    "<a href='/send?cmd=R%2000%205C%201'><strong>OK <kbd class='shortcut'>Enter</kbd></strong><span>RC6 0x00 0x5C</span></a>"
    "<a href='/send?cmd=R%2000%2054%201'><strong>Home <kbd class='shortcut'>H</kbd></strong><span>RC6 0x00 0x54</span></a>"
    "<a href='/send?cmd=R%2000%200A%201'><strong>Back <kbd class='shortcut'>B</kbd></strong><span>RC6 0x00 0x0A</span></a>"
    "<a href='/send?cmd=R%2000%2038%201'><strong>Source menu <kbd class='shortcut'>S</kbd></strong><span>RC6 0x00 0x38</span></a>"
    "<a href='/send?cmd=R%2000%20BF%201'><strong>Settings</strong><span>RC6 0x00 0xBF</span></a>"
    "<a href='/send?cmd=R%2000%208F%201'><strong>Ambilight <kbd class='shortcut'>A</kbd></strong><span>RC6 0x00 0x8F</span></a>"
    "<a href='/send?cmd=R%2000%2076%201'><strong>Netflix <kbd class='shortcut'>N</kbd></strong><span>RC6 0x00 0x76</span></a>"
    "<a href='/send?cmd=R%2000%200F%201'><strong>Info <kbd class='shortcut'>I</kbd></strong><span>RC6 0x00 0x0F</span></a>"
    "<a href='/send?cmd=R%2000%202C%201'><strong>Play <kbd class='shortcut'>Space</kbd></strong><span>RC6 0x00 0x2C</span></a>"
    "<a href='/send?cmd=R%2000%2030%201'><strong>Pause <kbd class='shortcut'>P</kbd></strong><span>RC6 0x00 0x30</span></a>"
    "<a href='/send?cmd=R%2000%2028%201'><strong>Fast-forward</strong><span>RC6 0x00 0x28</span></a>"
    "<a href='/send?cmd=R%2000%202B%201'><strong>Rewind</strong><span>RC6 0x00 0x2B</span></a>"
    "</div></details></div>";

  page += "<script>history.scrollRestoration='manual';const saveScroll=()=>sessionStorage.setItem('scrollY',scrollY);addEventListener('beforeunload',saveScroll);addEventListener('load',()=>{const y=sessionStorage.getItem('scrollY');if(y!==null)scrollTo(0,+y);});document.addEventListener('submit',saveScroll);document.addEventListener('click',e=>{const a=e.target.closest(\"a[href^='/send?cmd=']\");if(a)saveScroll();});const shortcuts=";

  if (customCommandCount == 0) {
    page += "{'o':'R 00 0C 1','q':'R 00 0C 1','+':'R 00 10 1','=':'R 00 10 1','-':'R 00 11 1','m':'R 00 0D 1','ArrowUp':'R 00 58 1','u':'R 00 58 1','ArrowDown':'R 00 59 1','d':'R 00 59 1','ArrowLeft':'R 00 5A 1','l':'R 00 5A 1','ArrowRight':'R 00 5B 1','r':'R 00 5B 1','Enter':'R 00 5C 1','h':'R 00 54 1','b':'R 00 0A 1','Backspace':'R 00 0A 1','s':'R 00 38 1','i':'R 00 0F 1','a':'R 00 8F 1','n':'R 00 76 1',' ':'R 00 2C 1','p':'R 00 30 1'}";
  } else {
    page += "{";
    bool wroteShortcut = false;

    for (uint8_t i = 0; i < customCommandCount; i++) {
      if (customCommands[i].shortcut.length() == 0) {
        continue;
      }

      if (wroteShortcut) {
        page += ",";
      }

      String shortcutKey = customCommands[i].shortcut;
      shortcutKey.toLowerCase();
      page += "'" + jsEscape(shortcutKey) + "':'" + jsEscape(customCommands[i].command) + "'";
      wroteShortcut = true;
    }

    page += "}";
  }

  page +=
    ";document.addEventListener('keydown',e=>{if(e.target.matches('input,textarea,select,button'))return;const cmd=shortcuts[e.key]||shortcuts[e.key.toLowerCase()];if(!cmd)return;e.preventDefault();saveScroll();location.href='/send?cmd='+encodeURIComponent(cmd);});</script>"
    "</main></body></html>";

  server.send(200, "text/html", page);
}

void handleSendRequest() {
  if (!server.hasArg("cmd")) {
    statusLine = "Web: missing cmd";
    statusIsError = true;
    drawScreen();
    sendWebPage();
    return;
  }

  String command = server.arg("cmd");

  if (command.indexOf(';') >= 0) {
    setCustomCommandList(command);
  } else {
    processCommand(command);
  }
  drawScreen();
  sendWebPage();
}


void handleListRequest() {
  if (!server.hasArg("list")) {
    statusLine = "Web: missing list";
    statusIsError = true;
  } else {
    setCustomCommandList(server.arg("list"));
  }

  drawScreen();
  sendWebPage();
}

void setupWebRemote() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  server.on("/", HTTP_GET, sendWebPage);
  server.on("/generate_204", HTTP_GET, redirectToWebRemote);
  server.on("/gen_204", HTTP_GET, redirectToWebRemote);
  server.on("/hotspot-detect.html", HTTP_GET, redirectToWebRemote);
  server.on("/library/test/success.html", HTTP_GET, redirectToWebRemote);
  server.on("/ncsi.txt", HTTP_GET, redirectToWebRemote);
  server.on("/connecttest.txt", HTTP_GET, redirectToWebRemote);
  server.on("/redirect", HTTP_GET, redirectToWebRemote);
  server.on("/send", HTTP_GET, handleSendRequest);
  server.on("/list", HTTP_POST, handleListRequest);
  server.onNotFound(redirectToWebRemote);
  server.begin();

  Serial.print("Web remote AP: ");
  Serial.println(AP_SSID);
  Serial.print("Open http://");
  Serial.println(WiFi.softAPIP());
}

void setup() {
  auto config = M5.config();

  M5Cardputer.begin(config, true);
  Serial.begin(115200);

  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setBrightness(SCREEN_BRIGHTNESS);
  lastKeyboardInputMillis = millis();

  IrSender.begin(DISABLE_LED_FEEDBACK);
  IrSender.setSendPin(IR_TX_PIN);
  setupWebRemote();

  drawScreen();

  Serial.println();
  Serial.println("Cardputer IR Sender ready");
  Serial.println("Philips power: R 00 0C 1");
  Serial.println("Connect to WiFi Cardputer-IR with password 12345678");
}

void loop() {
  M5Cardputer.update();
  dnsServer.processNextRequest();
  server.handleClient();
  updateScreenTimeout();

  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    lastKeyboardInputMillis = millis();
    turnScreenOn();
    Keyboard_Class::KeysState keys = M5Cardputer.Keyboard.keysState();

    if (keys.ctrl) {
      keyboardRemoteMode = !keyboardRemoteMode;
      statusLine = keyboardRemoteMode ? "Keyboard remote mode" : "Command entry mode";
      statusIsError = false;
      drawScreen();
      return;
    }

    if (keyboardRemoteMode) {
      handleKeyboardRemoteKey(keys);
      drawScreen();
      return;
    }

    for (char character : keys.word) {
      if (inputLine.length() < 48) {
        inputLine += character;
      }
    }

    if (keys.del && inputLine.length() > 0) {
      inputLine.remove(inputLine.length() - 1);
    }

    if (keys.enter) {
      String command = inputLine;
      inputLine = "";
      processCommand(command);
    }

    drawScreen();
  }

  delay(5);
}
