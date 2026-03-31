#include <WiFi.h>
#include <Wire.h>
#include <WiFiManager.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <time.h>

#include "font/TetrisClock16.h"
#include "font/TetrisDate8.h"

/* ===== OLED ===== */
static const uint8_t SCREEN_WIDTH = 64;
static const uint8_t SCREEN_HEIGHT = 32;
static const uint8_t OLED_ADDR = 0x3C;

static const uint8_t SDA_PIN = 21;
static const uint8_t SCL_PIN = 20;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

/* ===== LED ===== */
static const uint8_t LED_GREEN = 2;
static const uint8_t LED_YELLOW = 3;
static const uint8_t LED_RED = 8;

/* ===== RESET BUTTON ===== */
static const uint8_t BTN_RESET = 9;
static const unsigned long RESET_HOLD_MS = 5000;

/* ===== WIFI PORTAL ===== */
static const char* AP_SSID = "Voyager-Clock";
static const char* AP_PASS = "khrestomatiya";

/* ===== TIME ===== */
static const char* NTP_SERVER_1 = "pool.ntp.org";
static const char* NTP_SERVER_2 = "time.nist.gov";
static const char* NTP_SERVER_3 = "time.google.com";

/* ===== STATE ===== */
Preferences prefs;
WiFiManager wm;

String selectedCountry = "FI";
String selectedTzLabel = "Finland / Helsinki";

bool wifiConnected = false;
bool timeSynced = false;

unsigned long ledTimer = 0;
int ledStep = 0;
bool ledEnabled = false;

unsigned long displayTimer = 0;
static const unsigned long DISPLAY_REFRESH_MS = 100;

unsigned long timeSyncCheckTimer = 0;
static const unsigned long TIME_SYNC_CHECK_MS = 500;

unsigned long resetPressStart = 0;
bool resetHandled = false;

/* ===== MARQUEE ===== */
struct MarqueeState {
  String text;
  int16_t x;
  uint16_t textWidth;
  unsigned long lastStepMs;
  unsigned long holdStartMs;
  bool holdAtEdge;
};

MarqueeState marquee = {"", 0, 0, 0, 0, false};

static const uint8_t MARQUEE_Y = 24;
static const uint8_t MARQUEE_STEP_PX = 1;
static const unsigned long MARQUEE_SPEED_MS = 35;
static const unsigned long MARQUEE_EDGE_HOLD_MS = 450;

String statusLine1 = "STARTING...";
String statusLine2 = "";

enum ScreenMode {
  SCREEN_STATUS,
  SCREEN_CLOCK,
  SCREEN_SETUP
};

ScreenMode currentScreenMode = SCREEN_STATUS;

/* ===== PORTAL UI ===== */
const char* portal_html = R"(
<style>
body{background:#000;color:#0f0;font-family:Arial,Helvetica,sans-serif}
h1{color:#00ffd5;text-align:center;margin-bottom:8px}
p,div,label{font-size:14px}
input,select,button{font-size:16px;padding:8px;width:100%;box-sizing:border-box;margin-top:6px;margin-bottom:10px;}
button{background:#00ffd5;color:#000;border:none;font-weight:bold;}
.help{color:#9ff;font-size:13px;margin-top:10px;line-height:1.4;}
.card{border:1px solid #00ffd5;padding:12px;border-radius:8px;margin-top:12px;}
</style>
<h1>VOYAGER CLOCK</h1>
<div class='card'>
  <p>WiFi setup & timezone</p>
  <div class='help'>
    Country codes:<br>
    FI = Finland<br>
    SE = Sweden<br>
    DE = Germany<br>
    UA = Ukraine<br>
    PL = Poland<br>
    NL = Netherlands
  </div>
</div>
)";

/* ===== TIMEZONE TABLE ===== */
struct TimeZoneOption {
  const char* code;
  const char* label;
  const char* posixTz;
};

static const TimeZoneOption TIMEZONES[] = {
  {"FI", "Finland / Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
  {"SE", "Sweden / Stockholm", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
  {"DE", "Germany / Berlin", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
  {"UA", "Ukraine / Kyiv", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
  {"PL", "Poland / Warsaw", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
  {"NL", "Netherlands / Amsterdam", "CET-1CEST,M3.5.0/2,M10.5.0/3"}
};

const size_t TIMEZONE_COUNT = sizeof(TIMEZONES) / sizeof(TIMEZONES[0]);

void normalizeCountryCode(char* code) {
  if (!code) return;
  for (size_t i = 0; code[i] != '\0'; i++) {
    if (code[i] >= 'a' && code[i] <= 'z') {
      code[i] = code[i] - 'a' + 'A';
    }
  }
}

const TimeZoneOption* findTimeZone(const char* code) {
  if (!code || code[0] == '\0') return &TIMEZONES[0];

  for (size_t i = 0; i < TIMEZONE_COUNT; i++) {
    if (strcasecmp(code, TIMEZONES[i].code) == 0) {
      return &TIMEZONES[i];
    }
  }

  return &TIMEZONES[0];
}

void applyTimeZone(const char* countryCode) {
  const TimeZoneOption* tz = findTimeZone(countryCode);
  selectedCountry = tz->code;
  selectedTzLabel = tz->label;
  configTzTime(tz->posixTz, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
}

void saveSettings(const char* countryCode) {
  const TimeZoneOption* tz = findTimeZone(countryCode);
  prefs.putString("country", tz->code);
}

void loadSettings() {
  String storedCountry = prefs.getString("country", "FI");
  storedCountry.toUpperCase();
  const TimeZoneOption* tz = findTimeZone(storedCountry.c_str());
  selectedCountry = tz->code;
  selectedTzLabel = tz->label;
}

bool isTimeValid() {
  struct tm timeinfo{};
  if (!getLocalTime(&timeinfo, 100)) {
    return false;
  }
  return timeinfo.tm_year > (2024 - 1900);
}

void setMarqueeText(const String& message) {
  marquee.text = message;
  marquee.textWidth = message.length() * 6;
  marquee.x = display.width();
  marquee.lastStepMs = millis();
  marquee.holdStartMs = 0;
  marquee.holdAtEdge = false;
}

void drawMarquee() {
  if (marquee.text.length() == 0) return;

  display.setFont();
  display.setTextSize(1);
  display.setCursor(marquee.x, MARQUEE_Y);
  display.print(marquee.text);

  if (marquee.textWidth <= display.width()) {
    marquee.x = (display.width() - marquee.textWidth) / 2;
    return;
  }

  unsigned long now = millis();
  if (marquee.holdAtEdge) {
    if (now - marquee.holdStartMs >= MARQUEE_EDGE_HOLD_MS) {
      marquee.holdAtEdge = false;
      marquee.lastStepMs = now;
    }
    return;
  }

  if (now - marquee.lastStepMs < MARQUEE_SPEED_MS) return;

  marquee.lastStepMs = now;
  marquee.x -= MARQUEE_STEP_PX;

  if (marquee.x <= -(int16_t)marquee.textWidth) {
    marquee.x = display.width();
    marquee.holdAtEdge = true;
    marquee.holdStartMs = now;
  }
}

void drawStatusScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setFont();
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println(statusLine1);

  if (statusLine2.length() > 0) {
    if (statusLine2.length() <= 10) {
      display.setCursor(0, 12);
      display.print(statusLine2);
    } else {
      drawMarquee();
    }
  }

  display.display();
}

void drawSetupScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setFont();
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("VOYAGER CLOCK");
  display.println("SETUP MODE");

  drawMarquee();

  display.display();
}

void drawClockVertical() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  struct tm timeinfo{};
  if (!getLocalTime(&timeinfo, 100)) {
    statusLine1 = "TIME ERROR";
    statusLine2 = "SYNCING TIME...";
    setMarqueeText(statusLine2);
    drawStatusScreen();
    return;
  }

  char hh[3];
  char mm[3];
  char dateBuf[6];

  strftime(hh, sizeof(hh), "%H", &timeinfo);
  strftime(mm, sizeof(mm), "%M", &timeinfo);
  strftime(dateBuf, sizeof(dateBuf), "%d.%m", &timeinfo);

  const uint16_t hhWidth = TetrisClock16::textWidth(hh);
  const uint16_t mmWidth = TetrisClock16::textWidth(mm);

  const int16_t hhX = (display.width() - hhWidth) / 2;
  const int16_t mmX = (display.width() - mmWidth) / 2;

  TetrisClock16::drawText(display, hhX, 0, hh);
  display.drawFastHLine(8, 16, display.width() - 16, SSD1306_WHITE);
  TetrisClock16::drawText(display, mmX, 17, mm);

  const uint16_t dateWidth = TetrisDate8::textWidth(dateBuf);
  const int16_t dateX = (display.width() - dateWidth) / 2;
  TetrisDate8::drawText(display, dateX, 24, dateBuf);

  display.display();
}

void renderScreen() {
  switch (currentScreenMode) {
    case SCREEN_SETUP:
      drawSetupScreen();
      break;
    case SCREEN_STATUS:
      drawStatusScreen();
      break;
    case SCREEN_CLOCK:
    default:
      drawClockVertical();
      break;
  }
}

void ledPattern() {
  if (!ledEnabled) {
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_YELLOW, LOW);
    digitalWrite(LED_RED, LOW);
    return;
  }

  const unsigned long now = millis();

  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_RED, LOW);

  switch (ledStep) {
    case 0:
      digitalWrite(LED_YELLOW, HIGH);
      digitalWrite(LED_RED, HIGH);
      if (now - ledTimer >= 20) {
        ledTimer = now;
        ledStep = 1;
      }
      break;

    case 1:
      if (now - ledTimer >= 60) {
        ledTimer = now;
        ledStep = 2;
      }
      break;

    case 2:
      digitalWrite(LED_YELLOW, HIGH);
      digitalWrite(LED_RED, HIGH);
      if (now - ledTimer >= 20) {
        ledTimer = now;
        ledStep = 3;
      }
      break;

    case 3:
      if (now - ledTimer >= 100) {
        ledTimer = now;
        ledStep = 4;
      }
      break;

    case 4:
      digitalWrite(LED_GREEN, HIGH);
      if (now - ledTimer >= 20) {
        ledTimer = now;
        ledStep = 5;
      }
      break;

    default:
      if (now - ledTimer >= 1000) {
        ledTimer = now;
        ledStep = 0;
      }
      break;
  }
}

void clearSettingsAndRestart() {
  currentScreenMode = SCREEN_STATUS;
  statusLine1 = "RESETTING...";
  statusLine2 = "CLEARING WIFI + NVS";
  setMarqueeText(statusLine2);
  renderScreen();

  wm.resetSettings();
  prefs.clear();

  delay(800);
  ESP.restart();
}

void handleResetButton() {
  const int state = digitalRead(BTN_RESET);

  if (state == LOW) {
    if (resetPressStart == 0) {
      resetPressStart = millis();
      resetHandled = false;
    }

    const unsigned long heldMs = millis() - resetPressStart;
    if (!resetHandled && heldMs >= RESET_HOLD_MS) {
      resetHandled = true;
      clearSettingsAndRestart();
    }
  } else {
    resetPressStart = 0;
    resetHandled = false;
  }
}

void onConfigPortalStart(WiFiManager* manager) {
  (void)manager;
  currentScreenMode = SCREEN_SETUP;
  setMarqueeText(String("SSID: ") + AP_SSID + "  PASS: " + AP_PASS);
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(BTN_RESET, INPUT_PULLUP);

  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_RED, LOW);

  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    while (true) {
      delay(1);
    }
  }

  display.setRotation(1);
  display.setTextColor(SSD1306_WHITE);

  prefs.begin("voyager", false);
  loadSettings();

  currentScreenMode = SCREEN_STATUS;
  statusLine1 = "STARTING...";
  statusLine2 = "INIT WIFI";
  setMarqueeText(statusLine2);
  renderScreen();

  char savedCountry[4] = "FI";
  selectedCountry.toCharArray(savedCountry, sizeof(savedCountry));

  WiFiManagerParameter paramCountry(
    "country",
    "Country code (FI/SE/DE/UA/PL/NL)",
    savedCountry,
    3
  );

  wm.setCustomHeadElement(portal_html);
  wm.setAPCallback(onConfigPortalStart);
  wm.setConfigPortalTimeout(180);
  wm.addParameter(&paramCountry);

  bool ok = wm.autoConnect(AP_SSID, AP_PASS);
  if (!ok) {
    ESP.restart();
  }

  wifiConnected = true;
  ledEnabled = true;
  ledTimer = millis();
  ledStep = 0;

  char countryCode[4];
  strncpy(countryCode, paramCountry.getValue(), sizeof(countryCode) - 1);
  countryCode[sizeof(countryCode) - 1] = '\0';
  normalizeCountryCode(countryCode);

  const TimeZoneOption* tz = findTimeZone(countryCode);
  saveSettings(tz->code);
  applyTimeZone(tz->code);

  statusLine1 = "CONNECTED";
  statusLine2 = String(WiFi.localIP().toString()) + " " + tz->label;
  currentScreenMode = SCREEN_STATUS;
  setMarqueeText(statusLine2);
  renderScreen();
  delay(800);

  statusLine1 = "SYNCING TIME...";
  statusLine2 = tz->label;
  setMarqueeText(statusLine2);
  currentScreenMode = SCREEN_STATUS;
}

void loop() {
  handleResetButton();

  if (!timeSynced && wifiConnected && millis() - timeSyncCheckTimer >= TIME_SYNC_CHECK_MS) {
    timeSyncCheckTimer = millis();
    timeSynced = isTimeValid();

    if (timeSynced) {
      currentScreenMode = SCREEN_CLOCK;
    } else {
      currentScreenMode = SCREEN_STATUS;
      statusLine1 = "SYNCING TIME...";
      statusLine2 = selectedTzLabel;
      setMarqueeText(statusLine2);
    }
  }

  if (millis() - displayTimer >= DISPLAY_REFRESH_MS) {
    displayTimer = millis();
    renderScreen();
  }

  ledPattern();
}
