#include <WiFi.h>
#include <Wire.h>
#include <WiFiManager.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <time.h>

/* ===== FONT ===== */
#include "font/Orbitron_Medium_16.h"

/* ===== OLED ===== */
#define SCREEN_WIDTH 64
#define SCREEN_HEIGHT 32
#define OLED_ADDR 0x3C

#define SDA_PIN 21
#define SCL_PIN 20

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

/* ===== LED ===== */
#define LED_GREEN  2
#define LED_YELLOW 3
#define LED_RED    8

/* ===== RESET BUTTON ===== */
#define BTN_RESET  9
#define RESET_HOLD_MS 5000

/* ===== TIME ===== */
static const char* NTP_SERVER_1 = "pool.ntp.org";
static const char* NTP_SERVER_2 = "time.nist.gov";
static const char* NTP_SERVER_3 = "time.google.com";

/* ===== STATE ===== */
Preferences prefs;
WiFiManager wm;

unsigned long ledTimer = 0;
int ledStep = 0;
bool ledEnabled = false;

unsigned long displayTimer = 0;
const unsigned long DISPLAY_REFRESH_MS = 250;

unsigned long resetPressStart = 0;
bool resetHandled = false;

/* ===== PORTAL UI ===== */
const char* portal_html = R"(
<style>
body{background:#000;color:#0f0;font-family:Arial,Helvetica,sans-serif}
h1{color:#00ffd5;text-align:center;margin-bottom:8px}
p,div,label{font-size:14px}
input,select,button{
  font-size:16px;
  padding:8px;
  width:100%;
  box-sizing:border-box;
  margin-top:6px;
  margin-bottom:10px;
}
button{
  background:#00ffd5;
  color:#000;
  border:none;
  font-weight:bold;
}
.help{
  color:#9ff;
  font-size:13px;
  margin-top:10px;
  line-height:1.4;
}
.card{
  border:1px solid #00ffd5;
  padding:12px;
  border-radius:8px;
  margin-top:12px;
}
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
  {"FI", "Finland / Helsinki",     "EET-2EEST,M3.5.0/3,M10.5.0/4"},
  {"SE", "Sweden / Stockholm",     "CET-1CEST,M3.5.0/2,M10.5.0/3"},
  {"DE", "Germany / Berlin",       "CET-1CEST,M3.5.0/2,M10.5.0/3"},
  {"UA", "Ukraine / Kyiv",         "EET-2EEST,M3.5.0/3,M10.5.0/4"},
  {"PL", "Poland / Warsaw",        "CET-1CEST,M3.5.0/2,M10.5.0/3"},
  {"NL", "Netherlands / Amsterdam","CET-1CEST,M3.5.0/2,M10.5.0/3"}
};

const size_t TIMEZONE_COUNT = sizeof(TIMEZONES) / sizeof(TIMEZONES[0]);

/* ===================================================== */

void normalizeCountryCode(char* code) {
  if (!code) return;

  for (int i = 0; code[i] != '\0'; i++) {
    if (code[i] >= 'a' && code[i] <= 'z') {
      code[i] = code[i] - 'a' + 'A';
    }
  }
}

const TimeZoneOption* findTimeZone(const char* code) {
  if (!code) return &TIMEZONES[0];

  for (size_t i = 0; i < TIMEZONE_COUNT; i++) {
    if (strcasecmp(code, TIMEZONES[i].code) == 0) {
      return &TIMEZONES[i];
    }
  }

  return &TIMEZONES[0];
}

void applyTimeZone(const char* countryCode) {
  const TimeZoneOption* tz = findTimeZone(countryCode);
  configTzTime(tz->posixTz, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
}

bool isTimeValid() {
  struct tm timeinfo{};
  if (!getLocalTime(&timeinfo, 100)) {
    return false;
  }
  return timeinfo.tm_year > (2024 - 1900);
}

void drawCenteredText(const String& text, int y, const GFXfont* font) {
  int16_t x1, y1;
  uint16_t w, h;

  display.setFont(font);
  display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  int x = (display.width() - (int)w) / 2;
  display.setCursor(x, y);
  display.print(text);
}

void showBootScreen(const char* line1, const char* line2 = nullptr, const char* line3 = nullptr) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setFont();

  display.setCursor(0, 0);
  display.println("VOYAGER CLOCK");
  display.println();

  if (line1) display.println(line1);
  if (line2) display.println(line2);
  if (line3) display.println(line3);

  display.display();
}

void drawClockVertical() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  if (!isTimeValid()) {
    display.setFont();
    display.setCursor(0, 0);
    display.println("VOYAGER CLOCK");
    display.println();
    display.println("SYNCING TIME...");
    display.display();
    return;
  }

  struct tm timeinfo{};
  if (!getLocalTime(&timeinfo, 100)) {
    display.setFont();
    display.setCursor(0, 0);
    display.println("TIME ERROR");
    display.display();
    return;
  }

  char hh[3];
  char mm[3];
  char dateBuf[6];

  strftime(hh, sizeof(hh), "%H", &timeinfo);
  strftime(mm, sizeof(mm), "%M", &timeinfo);
  strftime(dateBuf, sizeof(dateBuf), "%d.%m", &timeinfo);

  const int centerY = display.height() / 2;

  display.setFont(&Orbitron_Medium_16);

  int16_t x1, y1;
  uint16_t w1, h1;

  display.getTextBounds(hh, 0, 0, &x1, &y1, &w1, &h1);
  int timeX = (display.width() - (int)w1) / 2;

  display.setCursor(timeX, centerY - 4);
  display.print(hh);

  display.drawLine(10, centerY + 2, display.width() - 10, centerY + 2, SSD1306_WHITE);

  display.setCursor(timeX, centerY + 24);
  display.print(mm);

  display.setFont();
  drawCenteredText(String(dateBuf), display.height() - 1, nullptr);

  display.display();
}

void ledPattern() {
  if (!ledEnabled) {
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_YELLOW, LOW);
    digitalWrite(LED_RED, LOW);
    return;
  }

  unsigned long now = millis();

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

    case 5:
      if (now - ledTimer >= 1000) {
        ledTimer = now;
        ledStep = 0;
      }
      break;
  }
}

void clearSettingsAndRestart() {
  showBootScreen("RESETTING...", "CLEARING WIFI", "PLEASE WAIT");

  wm.resetSettings();
  prefs.clear();

  delay(800);
  ESP.restart();
}

void handleResetButton() {
  int state = digitalRead(BTN_RESET);

  if (state == LOW) {
    if (resetPressStart == 0) {
      resetPressStart = millis();
      resetHandled = false;
    }

    unsigned long held = millis() - resetPressStart;

    if (!resetHandled && held >= RESET_HOLD_MS) {
      resetHandled = true;
      clearSettingsAndRestart();
    }
  } else {
    resetPressStart = 0;
    resetHandled = false;
  }
}

void waitForTimeSync() {
  unsigned long start = millis();

  while (!isTimeValid() && millis() - start < 15000) {
    drawClockVertical();
    handleResetButton();
    delay(100);
  }
}

/* ===================================================== */

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

  showBootScreen("Starting...");

  prefs.begin("voyager", false);

  char savedCountry[4] = "FI";
  String storedCountry = prefs.getString("country", "FI");
  storedCountry.toUpperCase();
  storedCountry.toCharArray(savedCountry, sizeof(savedCountry));

  WiFiManagerParameter param_country(
    "country",
    "Country code (FI/SE/DE/UA/PL/NL)",
    savedCountry,
    3
  );

  showBootScreen("WIFI SETUP", "", "AP: Voyager-Clock");

  wm.setCustomHeadElement(portal_html);
  wm.addParameter(&param_country);
  wm.setConfigPortalTimeout(180);

  bool ok = wm.autoConnect("Voyager-Clock");

  if (!ok) {
    ESP.restart();
  }

  char countryCode[4];
  strncpy(countryCode, param_country.getValue(), sizeof(countryCode) - 1);
  countryCode[sizeof(countryCode) - 1] = '\0';
  normalizeCountryCode(countryCode);

  const TimeZoneOption* tz = findTimeZone(countryCode);
  prefs.putString("country", tz->code);

  showBootScreen("WIFI OK", WiFi.localIP().toString().c_str(), tz->label);
  delay(1500);

  applyTimeZone(tz->code);

  showBootScreen("SYNCING TIME...", tz->label);
  waitForTimeSync();

  ledEnabled = true;
  ledTimer = millis();
  ledStep = 0;
}

void loop() {
  handleResetButton();

  if (millis() - displayTimer >= DISPLAY_REFRESH_MS) {
    displayTimer = millis();
    drawClockVertical();
  }

  ledPattern();
}