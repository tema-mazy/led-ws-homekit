#include "gradient_palettes.h"
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <FastLED.h>
#include <Ticker.h>
#include <WiFiManager.h>
#include <html.h>

#ifdef HOMEKIT
#include <arduino_homekit_server.h>
#endif

#ifndef NUM_LEDS
#define NUM_LEDS 50
#endif

#ifdef NODEMCU
#define TRIGGER_PIN D3  // #flash Button on GPIO0 / D3
#define LED_MCU_GPIO 16 // onboard MCU led D0
#define DENSITY 1
#define DATA_PIN D2

#endif

#ifdef WEMOSD1MINI
#define TRIGGER_PIN D1 // Button on D1
#define LED_MCU_GPIO 2 // onboard esp8266 led
#define DENSITY 1      // 255 / NUM_LEDS
#define DATA_PIN D4

#endif

#define EEPROM_OFFSET 2048
#define LOG_D(fmt, ...) printf_P(PSTR(fmt "\n"), ##__VA_ARGS__);

FASTLED_USING_NAMESPACE

#if defined(FASTLED_VERSION) && (FASTLED_VERSION < 3001000)
#warning "Requires FastLED 3.1 or later; check github for latest code."
#endif

#ifdef LEDSTRIP
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB
#define FRAMES_PER_SECOND 60

#else
#define LED_TYPE WS2811
#define COLOR_ORDER RGB
#define FRAMES_PER_SECOND 60
#endif
CRGB leds[NUM_LEDS + 1];

volatile unsigned int state = 1;
volatile unsigned int globalBrightness = 63;
volatile unsigned int globalDelay = 180;
volatile unsigned int brightnessBreath = 1;
volatile unsigned int cycleHue = 1;
volatile unsigned int hues = 5;
volatile unsigned int palc = 1;
volatile unsigned int pals = 20;
volatile unsigned int svv = 30;

extern const TProgmemRGBGradientPalettePtr
    gGradientPalettes[]; // These are for the fixed palettes in
                         // gradient_palettes.h
extern const uint8_t
    gGradientPaletteCount; // Total number of fixed palettes to display.
volatile uint8_t gCurrentPaletteNumber =
    0; // Current palette number from the 'playlist' of color palettes
volatile uint8_t cPattern = 0;
volatile uint8_t gHue = 0;
CRGB staticColor = CRGB::Red;
CRGBPalette16 gCurrentPalette;
volatile byte breathBrightness;

ESP8266WebServer server(80);

String ssid{"Mazy-IoT-Lights-"};

Ticker ttread;
Ticker blinker;
Ticker timerWifiReconnect;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
WiFiManager wifiManager;

#ifdef HOMEKIT
// access your HomeKit characteristics defined in my_accessory.c
extern "C" homekit_server_config_t accessory_config;
extern "C" homekit_characteristic_t cha_on;
extern "C" homekit_characteristic_t cha_name;
extern "C" homekit_characteristic_t cha_bright;
extern "C" homekit_characteristic_t cha_effect_on;
#endif

/* effects */

void rainbow() {
  // FastLED's built-in rainbow generator
  fill_rainbow(leds, NUM_LEDS, gHue, DENSITY);
}

void addGlitter(fract8 chanceOfGlitter) {
  if (random8() < chanceOfGlitter) {
    leds[random16(NUM_LEDS)] += CRGB::White;
  }
}

void glitter() {
  fadeToBlackBy(leds, NUM_LEDS, 25);
  addGlitter(80);
}

void confetti() {
  // random colored speckles that blink in and fade smoothly
  fadeToBlackBy(leds, NUM_LEDS, 16);
  int pos = random16(NUM_LEDS);
  leds[pos] += CHSV(gHue + random8(32), 200, globalBrightness);
  addGlitter(80);
}

uint32_t getPixColor(CRGB thisPixel) {
  return (((uint32_t)thisPixel.r << 16) | (thisPixel.g << 8) | thisPixel.b);
}

int prevPos = 0;
void sinelon() {
  // a colored dot sweeping back and forth, with fading trails
  fadeToBlackBy(leds, NUM_LEDS, 32);
  int pos = beatsin16(13, 0, NUM_LEDS - 1);
  if (pos >= prevPos) {
    for (int i = prevPos + 1; i <= pos; i++) {
      leds[i] += CHSV(gHue, 255, globalBrightness);
    }
  } else {
    for (int i = pos; i < prevPos; i++) {
      leds[i] += CHSV(gHue, 255, globalBrightness);
    }
  }
  prevPos = pos;
}

void drops() {
  // eight colored dots, weaving in and out of sync with each other
  fadeToBlackBy(leds, NUM_LEDS, 20);
  byte dothue = 0;
  for (int i = 0; i < 8; i++) {
    leds[beatsin16(i + 7, 0, NUM_LEDS - 1)] |=
        CHSV(dothue, 200, globalBrightness);
    dothue += 16;
  }
}

void bpm() {
  // colored stripes pulsing at a defined Beats-Per-Minute (BPM)
  uint8_t beat = beatsin8(32, 64, 255);
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] =
        ColorFromPalette(gCurrentPalette, gHue + (i * 2), beat - (i * 10));
  }
}

void milis() {
  long ms = millis() / 500;
  leds[ms % NUM_LEDS] = (ms % 2 == 0) ? CRGB::Yellow : CRGB::Blue;
  leds[NUM_LEDS - ms % NUM_LEDS] = (ms % 2 == 0) ? CRGB::Blue : CRGB::Yellow;
  leds[(millis() / 50) % NUM_LEDS] =
      CHSV(gHue + random8(-10, 10), 200, globalBrightness);
  fadeToBlackBy(leds, NUM_LEDS, 10); // 8 bit, 1 = slow, 255 = fast
}

void colors() { fill_solid(leds, NUM_LEDS, CHSV(gHue, 255, globalBrightness)); }

void staticColorEffect() { fill_solid(leds, NUM_LEDS, staticColor); }

void fullBlack() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
}

bool initflag = true;
void plua() {

  if (initflag) {
    for (int c = 0; c < 4; c++) {
      for (int i = 0; i < NUM_LEDS / 4; i++) {
        switch (c) {
        case 0:
          leds[c * NUM_LEDS / 4 + i] = CRGB::Blue;
          break;
        case 1:
          leds[c * NUM_LEDS / 4 + i] = CRGB::Yellow;
          break;
        case 2:
          leds[c * NUM_LEDS / 4 + i] = CRGB::White;
          break;
        case 3:
          leds[c * NUM_LEDS / 4 + i] = CRGB::Red;
          break;
        }
      }
    }
    initflag = false;
  } else {
    EVERY_N_MILLISECONDS(50) {

      CRGB first = leds[0];
      for (int i = 1; i < NUM_LEDS; i++) {
        leds[i - 1] = leds[i];
      }
      leds[NUM_LEDS - 1] = first;
    }
  }
}

void initLeds(CRGB c) {
  for (int i = 0; i < 50; i++) {
    leds[i] = c;
    FastLED.delay(5);
    FastLED.show();
  }
  fill_solid(leds, NUM_LEDS, CRGB::Black);
}

#define COOLING 55
// SPARKING: What chance (out of 255) is there that a new spark will be lit?
// Higher chance = more roaring fire.  Lower chance = more flickery fire.
// Default 120, suggested range 50-200.
#define SPARKING 120

void Fire2012WithPalette() {
  // Array of temperature readings at each simulation cell
  fadeToBlackBy(leds, NUM_LEDS, 10); // 8 bit, 1 = slow, 255 = fast
  const int numLeds = (NUM_LEDS > 200) ? 200 : NUM_LEDS;

  static byte heat[200]; // Fixed size to avoid VLA

  // Step 1.  Cool down every cell a little
  for (int i = 0; i < numLeds; i++) {
    heat[i] = qsub8(heat[i], random8(0, ((COOLING * 10) / numLeds) + 2));
  }

  // Step 2.  Heat from each cell drifts 'up' and diffuses a little
  for (int k = numLeds - 1; k >= 2; k--) {
    heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2]) / 3;
  }

  // Step 3.  Randomly ignite new 'sparks' of heat near the bottom
  if (random8() < SPARKING) {
    int y = random8(7);
    heat[y] = qadd8(heat[y], random8(160, 255));
  }

  // Step 4.  Map from heat cells to LED colors
  for (int j = 0; j < numLeds; j++) {
    // Scale the heat value from 0-255 down to 0-240
    // for best results with color palettes.
    byte colorindex = scale8(heat[j], 240);
    CRGB color =
        ColorFromPalette(gCurrentPalette, colorindex, globalBrightness);
    // CRGB color = ColorFromPalette( gCurrentPalette , colorindex);
    leds[j] = color;
  }
}

void candles() {
  for (int x = 0; x < NUM_LEDS; x++) {
    uint8_t flicker = random8(1, 80);
    leds[x] = CRGB(255 - flicker * 2, 150 - flicker, flicker / 2);
  }
} // candles()

void blendwave() {
  CRGB clr1;
  CRGB clr2;
  uint8_t speed;
  uint8_t loc1;

  speed = beatsin8(6, 0, 255);

  clr1 = blend(CHSV(beatsin8(3, 0, 255), 255, globalBrightness),
               CHSV(beatsin8(4, 0, 255), 255, globalBrightness), speed);
  clr2 = blend(CHSV(beatsin8(4, 0, 255), 255, globalBrightness),
               CHSV(beatsin8(3, 0, 255), 255, globalBrightness), speed);

  loc1 = beatsin8(10, 0, NUM_LEDS - 1);

  fill_gradient_RGB(leds, 0, clr2, loc1, clr1);
  fill_gradient_RGB(leds, loc1, clr2, NUM_LEDS - 1, clr1);

} // blendwave()

/* This is adapted from a routine created by Mark Kriegsman
 *  Usage - noise8();
 */

uint16_t dist = 12345; // A random number for our noise generator.
uint8_t scale = 30;    // Wouldn't recommend changing this on the fly, or the
                       // animation will be really blocky.

void noise8_pal() {

  for (int i = 0; i < NUM_LEDS; i++) { // Just ONE loop to fill up the LED array
                                       // as all of the pixels change.
    uint8_t index = inoise8(i * scale, dist + i * scale) %
                    255; // Get a value from the noise function. I'm using both
                         // x and y axis.
    leds[i] = ColorFromPalette(
        gCurrentPalette, index, globalBrightness,
        LINEARBLEND); // With that value, look up the 8 bit colour palette value
                      // and assign it to the current LED.
  }
  dist += beatsin8(10, 1, 4);
  // Moving along the distance (that random number we started out with). Vary it
  // a bit with a sine wave. In some sketches, I've used millis() instead of an
  // incremented counter. Works a treat.
} // noise8_pal()

uint8_t thisindex = 0;
void matrix_pal() {
  thisindex++;
  // One line matrix
  if (random8(90) > 80) {

    leds[NUM_LEDS - 1] = ColorFromPalette(gCurrentPalette, thisindex,
                                          globalBrightness, LINEARBLEND);
  } else {

    leds[NUM_LEDS - 1] = CHSV(gHue, 200, 50);
  }

  for (int i = 0; i < NUM_LEDS - 1; i++)
    leds[i] = leds[i + 1];

} // matrix_pal()

/*  This is from Serendipitous Circles from the August 1977 and April 1978
 * issues of Byte Magazine. I didn't do a very good job of it, but am at least
 * getting some animation and the routine is very short.
 */

/*  Usage - serendipitous_pal();
 */

uint16_t Xorig = 0x013;
uint16_t Yorig = 0x021;
uint16_t X = Xorig;
uint16_t Y = Yorig;
uint16_t Xn;
uint16_t Yn;

void serendipitous_pal() {

  EVERY_N_SECONDS(15) {
    X = Xorig;
    Y = Yorig;
  }

  // Xn = X - (Y / 2); Yn = Y + (Xn / 2);
  //   Xn = X-Y/2;   Yn = Y+Xn/2;
  //   Xn = X-(Y/2); Yn = Y+(X/2.1);
  //   Xn = X-(Y/3); Yn = Y+(X/1.5);
  Xn = X - (2 * Y);
  Yn = Y + (X / 2.1);

  X = Xn;
  Y = Yn;
  thisindex = (sin8(X) + cos8(Y)) / 2;
  leds[X % (NUM_LEDS)] = ColorFromPalette(gCurrentPalette, thisindex,
                                          globalBrightness, LINEARBLEND);
  fadeToBlackBy(leds, NUM_LEDS, 16); // 8 bit, 1 = slow, 255 = fast

} // serendipitous_pal()

void snow() {
  fadeToBlackBy(leds, NUM_LEDS, 8 * DENSITY / 1.5);
  addGlitter(80);
}

byte meteorSize = 5;
byte meteorTrailDecay = 32;
int SpeedDelay = 30;
int mp = 0;

void meteorRain() {
  fadeToBlackBy(leds, NUM_LEDS, meteorTrailDecay);
  // draw meteor
  for (int j = 0; j < meteorSize; j++) {
    if ((mp - j < NUM_LEDS) && (mp - j >= 0)) {
      leds[mp - j] = CRGB::White;
    }
  }
  mp++;
  if (mp >= NUM_LEDS)
    mp = 0;
}

boolean brightnessDirection;
void brightnessRoutine() {
  if (brightnessDirection) {
    breathBrightness += 1;
    if (breathBrightness > globalBrightness + 32 || breathBrightness > 254) {
      brightnessDirection = false;
    }
  } else {
    breathBrightness -= 1;
    if (breathBrightness < globalBrightness - 32 || breathBrightness < 1) {
      brightnessDirection = true;
    }
  }
  FastLED.setBrightness(breathBrightness);
}

// List of patterns to cycle through.  Each is defined as a separate function
// below.
typedef void (*SimplePatternList[])();
SimplePatternList gPatterns = {meteorRain,
                               confetti,
                               sinelon,
                               rainbow,
                               glitter,
                               drops,
                               bpm,
                               Fire2012WithPalette,
                               milis,
                               colors,
                               candles,
                               blendwave,
                               noise8_pal,
                               matrix_pal,
                               serendipitous_pal,
                               snow,
                               staticColorEffect};
int effectsEnabled[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                        1, 1, 1, 1, 1, 1, 1, 1, 1};
const char *effectsName[] = {
    "Метеор", "Конфеті", "Повзучка",    "Веселка", "Спалахи", "Крапельки",
    "Пульс",  "Вогонь",  "Секунди",     "Кольори", "Свічки",  "Хвилі",
    "Шум",    "Матриця", "Serendipity", "Сніг",    "Колір"};

// SimplePatternList gPatterns = { rainbow };
#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

bool checkOne() {
  uint sum = 0;
  for (unsigned int i = 0; i < ARRAY_SIZE(gPatterns); i++) {
    sum += effectsEnabled[i];
  }
  return (sum == 0);
}

void nextPattern() {
  initflag = true;

  int cnt = 0;
  int next = (cPattern + 1) % ARRAY_SIZE(gPatterns);
  // add one to the current pattern number, and wrap around at the end
  while (effectsEnabled[next] != 1) {
    next = (next + 1) % ARRAY_SIZE(gPatterns);

    cnt++;
    if (cnt > 30) {
      Serial.println(F("All patterns OFF!!!: "));
      effectsEnabled[0] = 1;
      cPattern = 0;
      return;
    }
  }
  cPattern = next;
  Serial.print(F("New pattern: "));
  Serial.println(cPattern);
}

/* EEPROM */
void eeWriteInt(int pos, int val) {
  byte *p = (byte *)&val;
  EEPROM.write(pos, *p);
  EEPROM.write(pos + 1, *(p + 1));
  EEPROM.write(pos + 2, *(p + 2));
  EEPROM.write(pos + 3, *(p + 3));
  EEPROM.commit();
}

int eeReadInt(int pos) {
  int val;
  byte *p = (byte *)&val;
  *p = EEPROM.read(pos);
  *(p + 1) = EEPROM.read(pos + 1);
  *(p + 2) = EEPROM.read(pos + 2);
  *(p + 3) = EEPROM.read(pos + 3);
  return val;
}

void storeSettings() {
  eeWriteInt(EEPROM_OFFSET + 0, state);
  eeWriteInt(EEPROM_OFFSET + 4, globalDelay);
  eeWriteInt(EEPROM_OFFSET + 8, globalBrightness);
  EEPROM.write(EEPROM_OFFSET + 12, brightnessBreath);
  EEPROM.write(EEPROM_OFFSET + 13, cycleHue);
  eeWriteInt(EEPROM_OFFSET + 14, hues);
  EEPROM.write(EEPROM_OFFSET + 19, palc);
  eeWriteInt(EEPROM_OFFSET + 20, pals);
  eeWriteInt(EEPROM_OFFSET + 24, svv);
  EEPROM.write(EEPROM_OFFSET + 60, staticColor.r);
  EEPROM.write(EEPROM_OFFSET + 61, staticColor.g);
  EEPROM.write(EEPROM_OFFSET + 62, staticColor.b);

  for (int i = 0; i < ARRAY_SIZE(gPatterns); i++) {
    byte e = (effectsEnabled[i] == 1) ? 1 : 0;
    EEPROM.write(EEPROM_OFFSET + 30 + i, e);
    Serial.print(i);
    Serial.print(":");
    Serial.println(e);
  }
  EEPROM.commit();
#ifdef HOMEKIT

  cha_bright.value.int_value = map(globalBrightness, 1, 200, 0, 100);
  homekit_characteristic_notify(&cha_bright, cha_bright.value);
  cha_on.value.bool_value = state == 1;
  homekit_characteristic_notify(&cha_on, cha_on.value);
#endif
}

void readSettings() {
  state = eeReadInt(EEPROM_OFFSET + 0);
  globalDelay = eeReadInt(EEPROM_OFFSET + 4);
  globalBrightness = eeReadInt(EEPROM_OFFSET + 8);
  brightnessBreath = EEPROM.read(EEPROM_OFFSET + 12);
  cycleHue = EEPROM.read(EEPROM_OFFSET + 13);
  hues = eeReadInt(EEPROM_OFFSET + 14);
  palc = EEPROM.read(EEPROM_OFFSET + 19);
  pals = eeReadInt(EEPROM_OFFSET + 20);
  svv = eeReadInt(EEPROM_OFFSET + 24);
  staticColor.r = EEPROM.read(EEPROM_OFFSET + 60);
  staticColor.g = EEPROM.read(EEPROM_OFFSET + 61);
  staticColor.b = EEPROM.read(EEPROM_OFFSET + 62);

  for (int i = 0; i < ARRAY_SIZE(gPatterns); i++) {
    byte e = EEPROM.read(EEPROM_OFFSET + 30 + i);
    Serial.print(i);
    Serial.print(":");
    Serial.println(e);
    effectsEnabled[i] = (e == 1) ? 1 : 0;
  }
  if (checkOne())
    effectsEnabled[0] = 1;
  FastLED.setBrightness(globalBrightness);

#ifdef HOMEKIT
  cha_bright.value.int_value = map(globalBrightness, 1, 200, 0, 100);
  homekit_characteristic_notify(&cha_bright, cha_bright.value);
  cha_on.value.bool_value = state == 1;
  homekit_characteristic_notify(&cha_on, cha_on.value);
#endif
}

#ifdef HOMEKIT
void cha_effect_on_setter(const homekit_value_t value) {
  LOG_D("HK NEXT");
  nextPattern();

  cha_effect_on.value.bool_value = false;
  homekit_characteristic_notify(&cha_effect_on, cha_effect_on.value);
}
#endif
/******************************/

const char _ETPL[] PROGMEM =
    "<li><label for='e%d'><input type='checkbox' value='1' id='e%d' name='e%d' "
    "%s><span>%s</span></label></li>";

/* webserver */
void handleRoot() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html; charset=utf-8", "");

  String head = FPSTR(_MIOT_HEAD);
  String body = FPSTR(_MIOT_BODY);
  String effname = (state == 1) ? effectsName[cPattern] : "OFF";
  body.replace("{mode}", effname);
  body.replace("{title}", String("X-Mas Lights: " + effname));
  body.replace("{millis}", String(millis()));
  body.replace("{state}",
               String((state == 0) ? "<b style='color:red;'>On</b>"
                                   : "<b style='color:blue;'>Off</b>"));
  body.replace("{statebage}", String((state == 1) ? "on" : "off"));
  body.replace("{newstate}", String((state == 1) ? "off" : "on"));

  server.sendContent(head);
  server.sendContent(body);

  server.sendContent_P(PSTR("<ul class=\"effects-list\">"));
  char c[255];
  for (int i = 0; i < (int)ARRAY_SIZE(gPatterns); i++) {
    snprintf_P(c, sizeof(c), _ETPL, i, i, i,
               (effectsEnabled[i] == 1) ? " checked " : "", effectsName[i]);
    server.sendContent(c);
  }
  server.sendContent_P(PSTR("</ul>"));

  String foot = FPSTR(_MIOT_FOOT);
  foot.replace("{brt}", String(globalBrightness));
  foot.replace("{brbr}", (brightnessBreath == 1) ? " checked " : "");
  foot.replace("{del}", String(globalDelay));
  foot.replace("{huec}", (cycleHue == 1) ? " checked " : "");
  foot.replace("{hues}", String(hues));
  foot.replace("{palc}", (palc == 1) ? " checked " : "");
  foot.replace("{pals}", String(pals));
  foot.replace("{fps}", String(FRAMES_PER_SECOND));
  foot.replace("{svv}", String(FRAMES_PER_SECOND - svv));
  char hexColor[8];
  snprintf(hexColor, sizeof(hexColor), "#%02X%02X%02X", staticColor.r,
           staticColor.g, staticColor.b);
  foot.replace("{stclr}", String(hexColor));
  foot.replace("{signal}", String(WiFi.RSSI()));

  server.sendContent(foot);
  server.sendContent(""); // Finalize chunked response
}

void updateVars() {
  String message = "";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }

  Serial.println(message);

  globalBrightness = server.arg("brt").toInt();
  FastLED.setBrightness(globalBrightness);
#ifdef HOMEKIT
  cha_bright.value.int_value = map(globalBrightness, 1, 200, 0, 100);
  homekit_characteristic_notify(&cha_bright, cha_bright.value);
#endif
  globalDelay = server.arg("del").toInt();

  brightnessBreath = server.arg("brbr").toInt();

  cycleHue = server.arg("huec").toInt();
  hues = server.arg("hues").toInt();

  palc = server.arg("palc").toInt();
  pals = server.arg("pals").toInt();

  svv = (FRAMES_PER_SECOND - server.arg("svv").toInt());

  if (server.hasArg("stclr")) {
    String hex = server.arg("stclr");
    if (hex.startsWith("#"))
      hex = hex.substring(1);
    long number = strtol(hex.c_str(), NULL, 16);
    staticColor.r = number >> 16;
    staticColor.g = (number >> 8) & 0xFF;
    staticColor.b = number & 0xFF;
  }

  for (int i = 0; i < ARRAY_SIZE(gPatterns); i++) {
    effectsEnabled[i] = (server.arg("e" + String(i)).toInt() == 1) ? 1 : 0;
  }

  if (checkOne())
    effectsEnabled[0] = 1;
}

void handleNext() {
  nextPattern();
  server.sendHeader("Location", String("/"), true);
  server.send(302, "text/plain", "/");
  server.client().stop(); // Stop is needed because we sent no content length
}

void changeState(int s) {
  state = s;
  if (state == 1) {
    Serial.println(F("STATE: 1"));
  } else {
    fullBlack();
    Serial.println(F("STATE: 0"));
  }
#ifdef HOMEKIT
  cha_on.value.bool_value = state == 1;

  cha_bright.value.int_value = map(globalBrightness, 1, 200, 0, 100);
  homekit_characteristic_notify(&cha_bright, cha_bright.value);
  homekit_characteristic_notify(&cha_on, cha_on.value);
#endif
  storeSettings();
}
#ifdef HOMEKIT
void set_on(const homekit_value_t v) {
  bool on = v.bool_value;
  cha_on.value.bool_value = on; // sync the value
  changeState(on ? 1 : 0);
}
#endif

void handleState() {
  state = (server.arg("s") == "on");
  changeState(state);
  server.sendHeader("Location", String("/"), true);
  server.send(302, "text/plain", "/");
  server.client().stop(); // Stop is needed because we sent no content length
}
#ifdef HOMEKIT
void set_bright(const homekit_value_t v) {
  Serial.println("set_bright");
  int bright = v.int_value;
  cha_bright.value.int_value = bright; // sync the value
  globalBrightness = map(bright, 0, 100, 1, 200);
  FastLED.setBrightness(globalBrightness);
  if (bright > 0) {
    changeState(1);
  } else {
    changeState(0);
  }

  storeSettings();
}
#endif
void handleSave() {
  // update vars
  updateVars();
  // save to memory
  storeSettings();
  cPattern = ARRAY_SIZE(gPatterns) - 1;
  nextPattern();
  FastLED.setBrightness(globalBrightness);

  server.sendHeader("Location", String("/"), true);
  server.send(302, "text/plain", "/");
  // Empty content inhibits Content-length header so we have to close the socket
  // ourselves.
  server.client().stop(); // Stop is needed because we sent no content length
}

void resetSettings() {
  Serial.println(F("Reset settings..."));
  server.sendHeader("Location", String("/"), true);
  server.send(302, "text/plain", "/");
  // Empty content inhibits Content-length header so we have to close the socket
  // ourselves.
  server.client().stop(); // Stop is needed because we sent no content length
#ifdef HOMEKIT
  homekit_storage_reset();
#endif
  wifiManager.resetSettings();
  EEPROM.write(EEPROM_OFFSET + 98, 0);
  EEPROM.commit();
  fullBlack();
  Serial.println(F("Reboot..."));

  delay(1000);
  ESP.restart();
}

volatile unsigned long buttlastSent = 0;
volatile bool resetRequested = false;
volatile bool setNext = false;
volatile bool setState = false;
volatile int pressCnt = 0;

void handleButtonActions() {
  if (resetRequested) {
    resetRequested = false;
    resetSettings();
  }
  if (setState) {
    setState = false;
    changeState(1 - state);
  }
  if (setNext) {
    setNext = false;
    nextPattern();
  }
}

void IRAM_ATTR button_ISR() {
  unsigned long edelay = millis() - buttlastSent;
  if (edelay > 2000) {
    pressCnt = 0;
  }
  if (edelay >= 30) { // remove jitter
    buttlastSent = millis();
    pressCnt++;
  }
  if (pressCnt == 1) {
    setNext = true;
  }
  if (pressCnt == 3) {
    setState = true;
  }

  if (pressCnt > 10) {
    pressCnt = 0;
    resetRequested = true;
  }
}

void blink() { digitalWrite(LED_MCU_GPIO, !digitalRead(LED_MCU_GPIO)); }

void ledOff() {
  blinker.detach();
  digitalWrite(LED_MCU_GPIO, HIGH);
}

void connectToWifi() {
  Serial.println(F("# Connecting to Wi-Fi..."));
  blinker.attach(1.0, blink);
  WiFi.begin();
}

void onWifiConnect(const WiFiEventStationModeGotIP &event) {
  Serial.print(F("# Connected! IP address: "));
  Serial.println(WiFi.localIP());
  ledOff();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected &event) {
  Serial.println(F("# Disconnected from Wi-Fi."));
  timerWifiReconnect.once(5, connectToWifi);
}

// gets called when WiFiManager enters configuration mode
void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.print(F("@@ Entered config mode IP:"));
  Serial.println(WiFi.softAPIP());
  blinker.attach(0.2, blink);
}

void _iototasetup() {
  ArduinoOTA.setPassword(OTAKEY);
  ArduinoOTA.onStart([]() {
    fullBlack();
    blinker.attach(0.3, blink);
  });
  ArduinoOTA.onEnd([]() {
    digitalWrite(D0, LOW);
    for (int i = 0; i < 20; i++) {
      digitalWrite(D0, HIGH);
      delay(i * 2);
      digitalWrite(D0, LOW);
      delay(i * 2);
    }
    digitalWrite(D0, HIGH);
    ESP.restart();
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    digitalWrite(D0, total % 1);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
      Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)
      Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
      Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
      Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR)
      Serial.println("End Failed");
  });
  ArduinoOTA.begin();
}

boolean isIp(String str) {
  for (size_t i = 0; i < str.length(); i++) {
    int c = str.charAt(i);
    if (c != '.' && (c < '0' || c > '9')) {
      return false;
    }
  }
  return true;
}
String toStringIp(IPAddress ip) {
  String res = "";
  for (int i = 0; i < 3; i++) {
    res += String((ip >> (8 * i)) & 0xFF) + ".";
  }
  res += String(((ip >> 8 * 3)) & 0xFF);
  return res;
}

void setup() {
  Serial.begin(9600);
  Serial.println(F("# Mazy IoT WunderWaffel boot"));
  ssid = ssid + String(ESP.getChipId(), 16);

  pinMode(LED_MCU_GPIO, OUTPUT);
  pinMode(TRIGGER_PIN, INPUT);

  Serial.println(F("# EEPROM "));
  // EEPROM
  EEPROM.begin(4096);
  delay(250);
  if (EEPROM.read(EEPROM_OFFSET + 98) != 27) { // first run
    Serial.println(F("# first run detected "));
    EEPROM.write(EEPROM_OFFSET + 98, 27);
    storeSettings();
#ifdef HOMEKIT
    homekit_storage_reset();
#endif
    wifiManager.resetSettings();
  } else {
    readSettings();
    nextPattern();
  }

  Serial.println(F("# Init LED "));
  pinMode(DATA_PIN, OUTPUT);
  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS)
      .setCorrection(TypicalLEDStrip);

  // set master brightness control
  FastLED.setBrightness(globalBrightness);

  Serial.println(F(" done."));
  initLeds(CRGB::Red);

  wifiManager.setDebugOutput(false);
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setTimeout(60);
  wifiManager.setConfigPortalTimeout(60);

  Serial.println(F("# WiFi manager start..."));

  blinker.attach(0.6, blink);

  if (wifiManager.autoConnect(ssid.c_str())) {
    wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
    wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);

  } else {
    Serial.println(F("# WiFi AP start..."));
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(IPAddress(10, 0, 0, 10), IPAddress(10, 0, 0, 10),
                      IPAddress(255, 255, 255, 0)); // subnet FF
    WiFi.softAP(ssid.c_str());
    Serial.print("AP IP address: ");
    Serial.println(WiFi.softAPIP());
    ledOff();
  }
  if (WiFi.isConnected()) {
    Serial.print(F("# Connected! IP address: "));
    Serial.println(WiFi.localIP());
    ledOff();
  }

  initLeds(CRGB::Blue);

  Serial.println(F("# OTA setup "));
  _iototasetup();

  Serial.println(F("# Button attach "));
  attachInterrupt(digitalPinToInterrupt(TRIGGER_PIN), button_ISR, RISING);

  Serial.print(F("# HTTP server .."));
  // web server
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.on("/next", handleNext);
  server.on("/state", handleState);
  server.on("/reset", resetSettings);
  server.onNotFound([]() {
    if (!isIp(server.hostHeader())) {
      Serial.println(F("Request redirected to captive portal"));
      server.sendHeader(
          F("Location"),
          String("http://") + toStringIp(server.client().localIP()), true);
      server.send(302, "text/plain",
                  ""); // Empty content inhibits Content-length header so we
                       // have to close the socket ourselves.
      server.client()
          .stop(); // Stop is needed because we sent no content length
      return;
    }

    server.send(404, "text/plain", "nothing here");
  });
  server.begin();
  random16_add_entropy(random8());
  Serial.println(F(" started"));

  gCurrentPaletteNumber = random8(0, gGradientPaletteCount);
  gCurrentPalette = gGradientPalettes[gCurrentPaletteNumber];
#ifdef HOMEKIT

  Serial.println(F(" Homekit setup"));
  uint8_t mac[WL_MAC_ADDR_LENGTH];
  WiFi.macAddress(mac);
  int name_len = snprintf(NULL, 0, "%s %02X%02X%02X",
                          cha_name.value.string_value, mac[3], mac[4], mac[5]);
  char *name_value = (char *)malloc(name_len + 1);
  snprintf(name_value, name_len + 1, "%s %02X%02X%02X",
           cha_name.value.string_value, mac[3], mac[4], mac[5]);
  cha_name.value = HOMEKIT_STRING_CPP(name_value);

  cha_on.setter = set_on;
  cha_bright.setter = set_bright;
  cha_effect_on.setter = cha_effect_on_setter;
  arduino_homekit_setup(&accessory_config);

  cha_bright.value.int_value = map(globalBrightness, 1, 200, 0, 100);
  homekit_characteristic_notify(&cha_bright, cha_bright.value);
  cha_on.value.bool_value = state == 1;
  homekit_characteristic_notify(&cha_on, cha_on.value);
#endif
  initLeds(CRGB::Green);

  Serial.println(F(" done "));
}

void loop() {
  ArduinoOTA.handle();
#ifdef HOMEKIT
  arduino_homekit_loop();
#endif
  server.handleClient();

  handleButtonActions();

  if (state == 1) {
    EVERY_N_MILLISECONDS(1000 / FRAMES_PER_SECOND) {

      gPatterns[cPattern]();

      if (cycleHue == 1 &&
          !(cPattern == 0 || cPattern == 7 || cPattern == 11)) { // fire & waves
        EVERY_N_MILLISECONDS_I(timingObj, 20) {
          timingObj.setPeriod(hues);
          gHue++;
        }
      }
      if (palc == 1) {
        EVERY_N_SECONDS_I(timingObj, 5) {
          timingObj.setPeriod(pals);
          gCurrentPaletteNumber++;
          if (gCurrentPaletteNumber >= gGradientPaletteCount)
            gCurrentPaletteNumber = 0;
          gCurrentPalette = gGradientPalettes[gCurrentPaletteNumber];
        }
      } else {
        if (cPattern == 7)
          gCurrentPalette = fire_gp; // fire_gp
        if (cPattern == 12)
          gCurrentPalette = es_emerald_dragon_08_gp; // MATRIX
      }

      if (brightnessBreath == 1 && !(cPattern == 0 || cPattern == 3)) {
        EVERY_N_MILLISECONDS(15) { brightnessRoutine(); }
      }

      EVERY_N_SECONDS_I(timingObj, 5) {
        timingObj.setPeriod(globalDelay);
        nextPattern();
      } // change patterns periodically
      FastLED.show();
    }
  } else {
    fullBlack();
  }
#ifdef HOMEKIT
  EVERY_N_MILLISECONDS(15 * 1000) {
    homekit_characteristic_notify(&cha_bright, cha_bright.value);
    homekit_characteristic_notify(&cha_on, cha_on.value);
  }
#endif
}
