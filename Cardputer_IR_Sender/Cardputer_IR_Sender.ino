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

#include <M5Cardputer.h>
#include <IRremote.hpp>

String inputLine = "";
String statusLine = "Ready";
bool statusIsError = false;

String shortenText(const String &text, size_t maximumLength) {
  if (text.length() <= maximumLength) {
    return text;
  }

  return "..." + text.substring(text.length() - maximumLength + 3);
}

void drawScreen() {
  auto &display = M5Cardputer.Display;

  display.fillScreen(BLACK);
  display.setTextFont(1);
  display.setTextSize(1);
  display.setTextWrap(false);

  display.setTextColor(CYAN, BLACK);
  display.setCursor(4, 3);
  display.println("CARDPUTER IR SENDER");

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

  display.setTextColor(statusIsError ? RED : GREEN, BLACK);
  display.setCursor(4, 76);
  display.print(shortenText(statusLine, 38));

  display.drawFastHLine(0, 96, display.width(), DARKGREY);

  display.setTextColor(WHITE, BLACK);
  display.setCursor(4, 106);
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

void setup() {
  auto config = M5.config();

  M5Cardputer.begin(config, true);
  Serial.begin(115200);

  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setBrightness(100);

  IrSender.begin(DISABLE_LED_FEEDBACK);
  IrSender.setSendPin(IR_TX_PIN);

  drawScreen();

  Serial.println();
  Serial.println("Cardputer IR Sender ready");
  Serial.println("Philips power: R 00 0C 1");
}

void loop() {
  M5Cardputer.update();

  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    Keyboard_Class::KeysState keys = M5Cardputer.Keyboard.keysState();

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
