/*
 * TIME32 — LED Часы на ESP8266 (NodeMCU ESP-12E)
 * Версия: b12
 * 
 * Режимы отображения (чередование):
 *   0: Только часы
 *   1: Часы + Погода (10с часы → полный проход погоды)
 *   2: Часы + Погода + Фраза (10с часы → погода → фраза)
 * 
 * Кнопки:
 *   FLASH (GPIO0): зажать при включении → сброс WiFi → Captive Portal
 *   RST:           аппаратная перезагрузка ESP
 * 
 * Captive Portal: точка доступа "Time32", автоматическое приглашение
 *                 на iOS и Android для настройки WiFi
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <DNSServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <FastLED.h>
#include <EEPROM.h>

// ============ Конфигурация ============
#define NUM_LEDS 128
#define LED_PIN 12
#define COLOR_ORDER GRB
#define MATRIX_WIDTH 16
#define MATRIX_HEIGHT 8
#define RESET_PIN 0        // GPIO0 = кнопка FLASH
#define AP_SSID "Time32"
#define DNS_PORT 53
#define WEATHER_UPDATE_MS 900000UL  // 15 мин

// ============ EEPROM Layout ============
#define EEPROM_SIZE 768
#define EE_MARKER       0   // "T32"
#define EE_EFFECT       4
#define EE_BRIGHT       5
#define EE_COL_R        6
#define EE_COL_G        7
#define EE_COL_B        8
#define EE_DISPMODE     9   // 0/1/2
#define EE_SPEED_L     10
#define EE_SPEED_H     11
#define EE_TXTLEN      12
#define EE_TXTDATA     13   // 256 байт
#define EE_SSID_LEN   270
#define EE_SSID_DATA  271   // 32 байта
#define EE_PASS_LEN   303
#define EE_PASS_DATA  304   // 64 байта
#define EE_APIKEY_LEN 370
#define EE_APIKEY_DATA 371  // 40 байт
#define EE_CITY_LEN   412
#define EE_CITY_DATA  413   // 50 байт
#define EE_RSTCNT     464   // счётчик быстрых перезагрузок для double-reset

// ============ Структуры ============
struct ParsedChar {
  int glyphIndex;
  int pixelX;
  int width;
};

struct GlyphDef {
  uint8_t width;
  uint8_t rows[5];
};

// ============ Состояние ============
enum SystemMode { SYS_SETUP, SYS_RUNNING };
SystemMode sysMode = SYS_RUNNING;

// displayMode: 0=только часы, 1=часы+погода, 2=часы+погода+фраза
uint8_t displayMode = 0;

// Чередование: что сейчас показываем
enum ShowPhase { SHOW_CLOCK, SHOW_WEATHER, SHOW_TEXT };
ShowPhase currentPhase = SHOW_CLOCK;
unsigned long phaseStartTime = 0;
#define CLOCK_SHOW_MS 10000   // 10 секунд часов

// ============ Объекты ============
CRGB leds[NUM_LEDS];
ESP8266WebServer server(80);
DNSServer dnsServer;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3 * 3600, 60000);

// ============ Настройки ============
uint8_t brightness = 50;
uint8_t hue = 0;
uint8_t effectMode = 0;
CRGB staticColor = CRGB(255, 0, 255);

String wifiSSID = "";
String wifiPassword = "";

// Бегущая строка
char scrollText[256] = "HELLO";
int textPosition = 0;
unsigned long lastScrollTime = 0;
int scrollSpeed = 150;

// Погода
String apiKey = "";
String cityName = "Moscow";
char weatherText[128] = "";
int weatherTemp = 0;
String weatherDesc = "";
unsigned long lastWeatherUpdate = 0;
bool weatherOk = false;
int weatherTextPosition = 0;

bool wifiConnected = false;

// Массивы парсинга
ParsedChar parsedText[256];
int parsedTextCount = 0;
int totalTextWidth = 0;

ParsedChar parsedWeather[128];
int parsedWeatherCount = 0;
int totalWeatherWidth = 0;

// Флаг: текущая бегущая строка завершила проход
bool scrollDone = false;

// ============ Цвета — расширенная палитра ============
const CRGB presetColors[] = {
  // Фиолетовые
  CRGB(255,0,255), CRGB(128,0,128), CRGB(148,0,211),
  CRGB(138,43,226), CRGB(186,85,211), CRGB(75,0,130),
  // Красные / розовые
  CRGB(255,0,0), CRGB(220,20,60), CRGB(255,20,147),
  CRGB(255,105,180), CRGB(199,21,133),
  // Оранжевые / жёлтые
  CRGB(255,165,0), CRGB(255,140,0), CRGB(255,69,0),
  CRGB(255,255,0), CRGB(255,215,0),
  // Зелёные
  CRGB(0,255,0), CRGB(0,200,0), CRGB(50,205,50),
  CRGB(0,255,127), CRGB(127,255,0),
  // Синие / голубые
  CRGB(0,0,255), CRGB(0,100,255), CRGB(30,144,255),
  CRGB(0,191,255), CRGB(0,255,255),
  // Бирюзовые / морские
  CRGB(0,206,209), CRGB(64,224,208), CRGB(72,209,204),
  // Белый / тёплые
  CRGB(255,255,255), CRGB(255,250,205), CRGB(255,228,196)
};
const int NUM_PRESETS = sizeof(presetColors)/sizeof(presetColors[0]);

// ============ Шрифт цифр 5x3 ============
const uint8_t font5x3[10][5] = {
  {0b111,0b101,0b101,0b101,0b111},{0b010,0b110,0b010,0b010,0b111},
  {0b111,0b001,0b111,0b100,0b111},{0b111,0b001,0b111,0b001,0b111},
  {0b101,0b101,0b111,0b001,0b001},{0b111,0b100,0b111,0b001,0b111},
  {0b111,0b100,0b111,0b101,0b111},{0b111,0b001,0b001,0b010,0b010},
  {0b111,0b101,0b111,0b101,0b111},{0b111,0b101,0b111,0b001,0b111}
};

// ============ Шрифт букв ============
#define GLYPH_SPACE 59
#define GLYPH_UNDERSCORE 60
#define GLYPH_EXCL 61
#define GLYPH_DOT 62
#define GLYPH_COMMA 63
#define GLYPH_MINUS 64
#define GLYPH_PLUS 65
#define GLYPH_DEGREE 66
#define GLYPH_COLON 67
#define GLYPH_PERCENT 68
#define GLYPH_SLASH 69
#define GLYPH_COUNT 70

const GlyphDef glyphs[GLYPH_COUNT] PROGMEM = {
  // Латиница A-Z (0-25)
  {3,{0b010,0b101,0b111,0b101,0b101}},{3,{0b110,0b101,0b110,0b101,0b110}},
  {3,{0b111,0b100,0b100,0b100,0b111}},{3,{0b110,0b101,0b101,0b101,0b110}},
  {3,{0b111,0b100,0b110,0b100,0b111}},{3,{0b111,0b100,0b110,0b100,0b100}},
  {3,{0b111,0b100,0b101,0b101,0b111}},{3,{0b101,0b101,0b111,0b101,0b101}},
  {3,{0b111,0b010,0b010,0b010,0b111}},{3,{0b001,0b001,0b001,0b101,0b111}},
  {3,{0b101,0b110,0b100,0b110,0b101}},{3,{0b100,0b100,0b100,0b100,0b111}},
  {3,{0b101,0b111,0b111,0b101,0b101}},{3,{0b101,0b111,0b111,0b101,0b101}},
  {3,{0b111,0b101,0b101,0b101,0b111}},{3,{0b111,0b101,0b111,0b100,0b100}},
  {3,{0b111,0b101,0b101,0b111,0b001}},{3,{0b111,0b101,0b110,0b101,0b101}},
  {3,{0b111,0b100,0b111,0b001,0b111}},{3,{0b111,0b010,0b010,0b010,0b010}},
  {3,{0b101,0b101,0b101,0b101,0b111}},{3,{0b101,0b101,0b101,0b101,0b010}},
  {3,{0b101,0b101,0b111,0b111,0b101}},{3,{0b101,0b101,0b010,0b101,0b101}},
  {3,{0b101,0b101,0b010,0b010,0b010}},{3,{0b111,0b001,0b010,0b100,0b111}},
  // Кириллица А-Я+Ё (26-58)
  {3,{0b010,0b101,0b111,0b101,0b101}},  // А
  {3,{0b111,0b100,0b110,0b101,0b111}},  // Б
  {3,{0b110,0b101,0b110,0b101,0b110}},  // В
  {3,{0b111,0b100,0b100,0b100,0b100}},  // Г
  {3,{0b011,0b011,0b011,0b101,0b111}},  // Д
  {3,{0b111,0b100,0b110,0b100,0b111}},  // Е
  {5,{0b10101,0b01110,0b00100,0b01110,0b10101}},  // Ж
  {3,{0b110,0b001,0b010,0b001,0b110}},  // З
  {3,{0b101,0b101,0b101,0b111,0b101}},  // И
  {3,{0b010,0b101,0b101,0b111,0b101}},  // Й
  {3,{0b101,0b110,0b100,0b110,0b101}},  // К
  {3,{0b011,0b011,0b011,0b101,0b101}},  // Л
  {5,{0b10001,0b11011,0b10101,0b10101,0b10001}},  // М
  {3,{0b101,0b101,0b111,0b101,0b101}},  // Н
  {3,{0b111,0b101,0b101,0b101,0b111}},  // О
  {3,{0b111,0b101,0b101,0b101,0b101}},  // П
  {3,{0b111,0b101,0b111,0b100,0b100}},  // Р
  {3,{0b111,0b100,0b100,0b100,0b111}},  // С
  {3,{0b111,0b010,0b010,0b010,0b010}},  // Т
  {3,{0b101,0b101,0b011,0b001,0b110}},  // У
  {5,{0b01110,0b10101,0b10101,0b01110,0b00100}},  // Ф
  {3,{0b101,0b101,0b010,0b101,0b101}},  // Х
  {3,{0b101,0b101,0b101,0b101,0b111}},  // Ц
  {3,{0b101,0b101,0b111,0b001,0b001}},  // Ч
  {5,{0b10101,0b10101,0b10101,0b10101,0b11111}},  // Ш
  {5,{0b10101,0b10101,0b10101,0b10101,0b11111}},  // Щ
  {3,{0b110,0b010,0b011,0b011,0b011}},  // Ъ
  {5,{0b10001,0b10001,0b11001,0b10101,0b11001}},  // Ы
  {3,{0b100,0b100,0b110,0b101,0b110}},  // Ь
  {3,{0b110,0b001,0b011,0b001,0b110}},  // Э
  {5,{0b10010,0b10101,0b11101,0b10101,0b10010}},  // Ю
  {3,{0b011,0b101,0b011,0b001,0b001}},  // Я
  {3,{0b101,0b100,0b110,0b100,0b111}},  // Ё
  // Спецсимволы
  {3,{0b000,0b000,0b000,0b000,0b000}},  // 59 пробел
  {3,{0b000,0b000,0b000,0b000,0b111}},  // 60 _
  {1,{0b1,0b1,0b1,0b0,0b1}},            // 61 !
  {1,{0b0,0b0,0b0,0b0,0b1}},            // 62 .
  {1,{0b0,0b0,0b0,0b1,0b1}},            // 63 ,
  {3,{0b000,0b000,0b111,0b000,0b000}},  // 64 -
  {3,{0b000,0b010,0b111,0b010,0b000}},  // 65 +
  {2,{0b11,0b11,0b00,0b00,0b00}},       // 66 °
  {1,{0b0,0b1,0b0,0b1,0b0}},            // 67 :
  {3,{0b101,0b001,0b010,0b100,0b101}},  // 68 %
  {3,{0b001,0b001,0b010,0b100,0b100}},  // 69 /
};

GlyphDef readGlyph(int idx) {
  GlyphDef g;
  memcpy_P(&g, &glyphs[idx], sizeof(GlyphDef));
  return g;
}


// ============================================================================
//   UTF-8 → индекс глифа
// ============================================================================
int getGlyphIndex(const char* &ptr) {
  uint8_t c = (uint8_t)*ptr;
  if (c == 0) return -1;
  if (c < 0x80) {
    ptr++;
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c >= '0' && c <= '9') return -(c - '0' + 100);
    switch (c) {
      case ' ': return GLYPH_SPACE; case '_': return GLYPH_UNDERSCORE;
      case '!': return GLYPH_EXCL;  case '.': return GLYPH_DOT;
      case ',': return GLYPH_COMMA; case '-': return GLYPH_MINUS;
      case '+': return GLYPH_PLUS;  case ':': return GLYPH_COLON;
      case '%': return GLYPH_PERCENT; case '/': return GLYPH_SLASH;
      default:  return GLYPH_SPACE;
    }
  }
  if ((c & 0xE0) == 0xC0 && ptr[1]) {
    uint16_t cp = ((c & 0x1F) << 6) | ((uint8_t)ptr[1] & 0x3F);
    ptr += 2;
    if (cp == 0x0401 || cp == 0x0451) return 58;
    if (cp == 0x00B0) return GLYPH_DEGREE;
    if (cp >= 0x0410 && cp <= 0x042F) return 26 + (cp - 0x0410);
    if (cp >= 0x0430 && cp <= 0x044F) return 26 + (cp - 0x0430);
    return GLYPH_SPACE;
  }
  if ((c & 0xF0) == 0xE0) { ptr += 3; return GLYPH_SPACE; }
  if ((c & 0xF8) == 0xF0) { ptr += 4; return GLYPH_SPACE; }
  ptr++;
  return GLYPH_SPACE;
}


// ============================================================================
//   LED
// ============================================================================
int xyToIndex(int x, int y) {
  if (x < 0 || x >= MATRIX_WIDTH || y < 0 || y >= MATRIX_HEIGHT) return -1;
  return (x < 8) ? (y * 8 + x) : (64 + y * 8 + x - 8);
}

CRGB getEffectColor(int pos, int extra) {
  switch (effectMode) {
    case 0: return CHSV(hue + pos * 25, 255, 255);
    case 1: return CHSV(hue, 190 + sin8(hue * 2 + pos * 20) / 4, 255);
    case 2: return CHSV(hue + sin8(hue * 2 + pos * 30) / 5, 255, 255);
    case 3: return CHSV(hue + extra * 33 + random8(20), 200 + random8(55), 220 + random8(35));
    case 4: return staticColor;
    case 5: return CHSV((hue / 2) + (pos * 45), 240, 255);
    default: return CRGB::White;
  }
}

void setPixel(int idx, int r, int c, CRGB base) {
  if (idx < 0 || idx >= NUM_LEDS) return;
  if (effectMode == 3) { leds[idx] = CHSV(hue+r*10+c*25+random8(30),230+random8(25),255); return; }
  if (effectMode == 1 || effectMode == 4) { leds[idx] = base; return; }
  uint8_t h = (r+c)*5; CRGB cl = base;
  cl.r = qadd8(cl.r, sin8(hue+h)/10);
  cl.g = qadd8(cl.g, sin8(hue+85+h)/10);
  cl.b = qadd8(cl.b, sin8(hue+170+h)/10);
  leds[idx] = cl;
}

int drawDigit(int d, int x, int y, CRGB base) {
  if (d < 0 || d > 9) return 3;
  for (int r=0;r<5;r++) for (int c=0;c<3;c++)
    if (font5x3[d][r] & (1<<(2-c))) setPixel(xyToIndex(x+c,y+r),r,c,base);
  return 3;
}

int drawGlyph(int gi, int x, int y, CRGB base) {
  if (gi < 0 || gi >= GLYPH_COUNT) return 3;
  GlyphDef g = readGlyph(gi);
  for (int r=0;r<5;r++) for (int c=0;c<g.width;c++)
    if (g.rows[r] & (1<<(g.width-1-c))) setPixel(xyToIndex(x+c,y+r),r,c,base);
  return g.width;
}

void drawColon(int x, int y, CRGB base) {
  if (x<0||x>=MATRIX_WIDTH) return;
  int i1=xyToIndex(x,y+1), i2=xyToIndex(x,y+3);
  if (i1>=0) leds[i1]=base; if (i2>=0) leds[i2]=base;
}


// ============================================================================
//   Отображение
// ============================================================================
void displayTime(int h, int m, int s) {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  int sy = (MATRIX_HEIGHT-5)/2;
  drawDigit(h/10,0,sy,getEffectColor(0,h/10));
  drawDigit(h%10,4,sy,getEffectColor(1,h%10));
  if (s%2==0) drawColon(7,sy,getEffectColor(2,10));
  drawDigit(m/10,9,sy,getEffectColor(3,m/10));
  drawDigit(m%10,13,sy,getEffectColor(4,m%10));
}

void parseString(const char* str, ParsedChar* out, int &count, int &totalW, int maxC) {
  count = 0; totalW = 0;
  const char* p = str;
  while (*p && count < maxC) {
    int gi = getGlyphIndex(p);
    out[count].glyphIndex = gi;
    out[count].pixelX = totalW;
    out[count].width = (gi <= -100) ? 3 : (gi >= 0 && gi < GLYPH_COUNT) ? readGlyph(gi).width : 3;
    totalW += out[count].width + 1;
    count++;
  }
}

void parseScrollText() { parseString(scrollText, parsedText, parsedTextCount, totalTextWidth, 255); }
void parseWeatherText() { parseString(weatherText, parsedWeather, parsedWeatherCount, totalWeatherWidth, 127); }

// Возвращает true когда строка завершила полный проход
bool displayScrolling(ParsedChar* chars, int count, int totalW, int &pos) {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  if (count == 0) return true;
  int yOff = (MATRIX_HEIGHT-5)/2;
  for (int i=0; i<count; i++) {
    int sx = chars[i].pixelX - pos;
    if (sx + chars[i].width < 0) continue;
    if (sx >= MATRIX_WIDTH) break;
    CRGB color = getEffectColor(i, i%10);
    int gi = chars[i].glyphIndex;
    if (gi <= -100) drawDigit(-(gi+100), sx, yOff, color);
    else drawGlyph(gi, sx, yOff, color);
  }
  return (pos > totalW + 2);  // +2 небольшой отступ после последнего символа
}


// ============================================================================
//   EEPROM
// ============================================================================
void eepromWriteStr(int aL, int aD, const String &s, int mx) {
  int l=s.length(); if(l>mx) l=mx;
  EEPROM.write(aL, l);
  for(int i=0;i<l;i++) EEPROM.write(aD+i, s[i]);
}

String eepromReadStr(int aL, int aD, int mx) {
  int l=EEPROM.read(aL); if(l>mx||l==0) return "";
  char b[65]; int rl=(l>64)?64:l;
  for(int i=0;i<rl;i++) b[i]=EEPROM.read(aD+i);
  b[rl]='\0'; return String(b);
}

bool isEEInit() { return EEPROM.read(0)=='T'&&EEPROM.read(1)=='3'&&EEPROM.read(2)=='2'; }
void writeMark() { EEPROM.write(0,'T'); EEPROM.write(1,'3'); EEPROM.write(2,'2'); }

void loadSettings() {
  EEPROM.begin(EEPROM_SIZE);
  if (!isEEInit()) { writeMark(); saveSettings(); saveWiFi(); saveWeatherCfg(); EEPROM.commit(); return; }
  effectMode=EEPROM.read(EE_EFFECT); if(effectMode>5) effectMode=0;
  brightness=EEPROM.read(EE_BRIGHT); if(brightness<5) brightness=50;
  staticColor.r=EEPROM.read(EE_COL_R); staticColor.g=EEPROM.read(EE_COL_G); staticColor.b=EEPROM.read(EE_COL_B);
  displayMode=EEPROM.read(EE_DISPMODE); if(displayMode>2) displayMode=0;
  scrollSpeed=EEPROM.read(EE_SPEED_L)|(EEPROM.read(EE_SPEED_H)<<8); if(scrollSpeed<50||scrollSpeed>500) scrollSpeed=150;
  int tl=EEPROM.read(EE_TXTLEN);
  if(tl>0&&tl<256){for(int i=0;i<tl;i++) scrollText[i]=EEPROM.read(EE_TXTDATA+i); scrollText[tl]='\0';}
  else strcpy(scrollText,"HELLO");
  wifiSSID=eepromReadStr(EE_SSID_LEN,EE_SSID_DATA,32);
  wifiPassword=eepromReadStr(EE_PASS_LEN,EE_PASS_DATA,63);
  apiKey=eepromReadStr(EE_APIKEY_LEN,EE_APIKEY_DATA,40);
  cityName=eepromReadStr(EE_CITY_LEN,EE_CITY_DATA,50);
  if(cityName.length()==0) cityName="Moscow";
}

void saveSettings() {
  writeMark();
  EEPROM.write(EE_EFFECT,effectMode); EEPROM.write(EE_BRIGHT,brightness);
  EEPROM.write(EE_COL_R,staticColor.r); EEPROM.write(EE_COL_G,staticColor.g); EEPROM.write(EE_COL_B,staticColor.b);
  EEPROM.write(EE_DISPMODE,displayMode);
  EEPROM.write(EE_SPEED_L,scrollSpeed&0xFF); EEPROM.write(EE_SPEED_H,(scrollSpeed>>8)&0xFF);
  int tl=strlen(scrollText); if(tl>255) tl=255;
  EEPROM.write(EE_TXTLEN,tl);
  for(int i=0;i<tl;i++) EEPROM.write(EE_TXTDATA+i,scrollText[i]);
  EEPROM.commit();
}

void saveWiFi() { eepromWriteStr(EE_SSID_LEN,EE_SSID_DATA,wifiSSID,32); eepromWriteStr(EE_PASS_LEN,EE_PASS_DATA,wifiPassword,63); EEPROM.commit(); }
void saveWeatherCfg() { eepromWriteStr(EE_APIKEY_LEN,EE_APIKEY_DATA,apiKey,40); eepromWriteStr(EE_CITY_LEN,EE_CITY_DATA,cityName,50); EEPROM.commit(); }
void clearWiFi() { EEPROM.write(EE_SSID_LEN,0); EEPROM.write(EE_PASS_LEN,0); EEPROM.commit(); wifiSSID=""; wifiPassword=""; }


// ============================================================================
//   Погода
// ============================================================================
String jsonExtract(const String &json, const String &key) {
  String s="\""+key+"\""; int i=json.indexOf(s); if(i<0) return "";
  i=json.indexOf(':',i+s.length()); if(i<0) return ""; i++;
  while(i<(int)json.length()&&json[i]==' ') i++;
  if(json[i]=='"'){int a=i+1,b=json.indexOf('"',a); return (b>a)?json.substring(a,b):"";}
  int a=i,b=a; while(b<(int)json.length()&&json[b]!=','&&json[b]!='}'&&json[b]!=']') b++;
  return json.substring(a,b);
}

void fetchWeather() {
  if(apiKey.length()==0||!wifiConnected) return;
  WiFiClient client; HTTPClient http;
  String url="http://api.openweathermap.org/data/2.5/weather?q="+cityName+"&appid="+apiKey+"&units=metric&lang=ru";
  Serial.println("Weather: "+url);
  http.begin(client,url); http.setTimeout(8000);
  int code=http.GET();
  if(code==200){
    String p=http.getString();
    weatherTemp=(int)jsonExtract(p,"temp").toFloat();
    // description
    int wi=p.indexOf("\"description\"");
    if(wi>0){int ci=p.indexOf(':',wi);if(ci>0){int qs=p.indexOf('"',ci+1);int qe=p.indexOf('"',qs+1);if(qs>0&&qe>qs) weatherDesc=p.substring(qs+1,qe);}}
    String city=jsonExtract(p,"name"); if(city.length()==0) city=cityName;
    String hum=jsonExtract(p,"humidity");
    int wind=(int)jsonExtract(p,"speed").toFloat();
    
    String w=city+" ";
    if(weatherTemp>0) w+="+";
    w+=String(weatherTemp)+"°C "+weatherDesc;
    if(hum.length()>0) w+="  "+hum+"%";
    if(wind>0) w+="  ветер "+String(wind)+"м/с";
    w.toCharArray(weatherText,128);
    parseWeatherText(); weatherOk=true;
    Serial.println("OK: "+w);
  } else {
    Serial.println("FAIL: "+String(code));
    if(code==401) strcpy(weatherText,"Ошибка API ключа");
    else if(code==404) strcpy(weatherText,"Город не найден");
    else strcpy(weatherText,"Нет данных");
    parseWeatherText(); weatherOk=false;
  }
  http.end(); lastWeatherUpdate=millis();
}


// ============================================================================
//   Captive Portal
// ============================================================================
void handleCaptivePortal() {
  String html = F(
    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Time32</title><style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:-apple-system,system-ui,sans-serif;background:#0a0a0a;color:#e0e0e0;"
    "min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px}"
    ".card{background:#1a1a2e;border:1px solid #333;border-radius:16px;padding:30px;max-width:400px;width:100%}"
    "h1{text-align:center;font-size:1.8em;margin-bottom:5px;color:#4facfe}"
    ".sub{text-align:center;color:#888;margin-bottom:25px;font-size:.9em}"
    ".field{margin-bottom:16px}"
    "label{display:block;margin-bottom:6px;color:#aaa;font-size:.85em}"
    "input[type=text],input[type=password]{width:100%;padding:12px;border:1px solid #333;"
    "border-radius:8px;background:#111;color:#fff;font-size:1em;outline:none}"
    "input:focus{border-color:#4facfe}"
    ".sp{display:flex;align-items:center;gap:6px;margin-top:6px;font-size:.85em;color:#888}"
    ".sp input{width:16px;height:16px}"
    "button{width:100%;padding:14px;border:none;border-radius:8px;background:#4facfe;"
    "color:#000;font-size:1.1em;font-weight:600;cursor:pointer;margin-top:10px}"
    ".sc button{background:#222;color:#4facfe;border:1px solid #333;font-size:.9em;padding:10px}"
    ".nets{max-height:200px;overflow-y:auto;margin-top:10px}"
    ".net{padding:10px;border:1px solid #222;border-radius:6px;margin-bottom:4px;"
    "cursor:pointer;display:flex;justify-content:space-between}"
    ".net:hover{background:#252540}"
    ".info{text-align:center;color:#666;margin-top:20px;font-size:.8em}"
    "</style></head><body><div class='card'>"
    "<h1>Time32</h1><p class='sub'>Настройка WiFi</p>"
    "<div class='sc'><button onclick='scan()' id='sb'>Поиск сетей</button>"
    "<div class='nets' id='ns'></div></div>"
    "<form action='/save' method='POST'>"
    "<div class='field'><label>SSID</label>"
    "<input type='text' name='ssid' id='ssid' required></div>"
    "<div class='field'><label>Пароль</label>"
    "<input type='password' name='pass' id='pass'>"
    "<label class='sp'><input type='checkbox' "
    "onchange=\"document.getElementById('pass').type=this.checked?'text':'password'\">"
    " Показать</label></div>"
    "<button type='submit'>Подключить</button></form>"
    "<p class='info'>Часы перезагрузятся</p></div>"
    "<script>"
    "function scan(){"
    "document.getElementById('sb').textContent='Поиск...';"
    "var x=new XMLHttpRequest();x.open('GET','/scan');x.onload=function(){"
    "document.getElementById('sb').textContent='Поиск сетей';"
    "var d=JSON.parse(x.responseText),h='';"
    "for(var i=0;i<d.length;i++){"
    "h+='<div class=\"net\" onclick=\"document.getElementById(\\'ssid\\').value=\\''+d[i].s+'\\'\"><span>';"
    "h+=d[i].s+(d[i].e?' 🔒':'')+'</span><span style=\"color:#888\">'+d[i].r+'dBm</span></div>';}"
    "document.getElementById('ns').innerHTML=h||'<div style=\"padding:10px;color:#666\">Не найдено</div>';};"
    "x.onerror=function(){document.getElementById('sb').textContent='Ошибка';};"
    "x.send();}"
    "</script></body></html>"
  );
  server.send(200, "text/html", html);
}

void handleScanNets() {
  int n=WiFi.scanNetworks(); String j="[";
  for(int i=0;i<n;i++){if(i) j+=","; j+="{\"s\":\""+WiFi.SSID(i)+"\",\"r\":"+String(WiFi.RSSI(i))+",\"e\":"+String(WiFi.encryptionType(i)!=ENC_TYPE_NONE?"true":"false")+"}";}
  j+="]"; WiFi.scanDelete(); server.send(200,"application/json",j);
}

void handleSaveWiFi() {
  if(server.hasArg("ssid")){
    String s=server.arg("ssid"); s.trim();
    String p=server.hasArg("pass")?server.arg("pass"):""; p.trim();
    if(s.length()>0){
      wifiSSID=s; wifiPassword=p; saveWiFi();
      server.send(200,"text/html",F("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><style>body{font-family:-apple-system,sans-serif;background:#0a0a0a;color:#e0e0e0;display:flex;align-items:center;justify-content:center;min-height:100vh}.c{background:#1a1a2e;border:1px solid #333;border-radius:16px;padding:40px;text-align:center}.sp{width:40px;height:40px;border:3px solid #333;border-top:3px solid #4facfe;border-radius:50%;animation:s 1s linear infinite;margin:20px auto}@keyframes s{to{transform:rotate(360deg)}}</style></head><body><div class='c'><div class='sp'></div><h2 style='color:#4facfe'>Подключение...</h2><p style='color:#aaa;margin-top:10px'>Можно закрыть это окно</p></div></body></html>"));
      delay(1500); ESP.restart();
    }
  }
  server.sendHeader("Location","/",true); server.send(302,"text/plain","");
}

// Captive Portal detection — ключевой момент для iOS/Android
void handleNotFound() {
  if(sysMode==SYS_SETUP){
    // Перенаправляем ВСЕ запросы на портал
    server.sendHeader("Location","http://192.168.4.1/",true);
    server.send(302,"text/plain","redirect");
  } else server.send(404,"text/plain","Not found");
}

void handleGenerate204() {
  // Android: если вернуть НЕ 204, покажет портал
  if(sysMode==SYS_SETUP){
    server.sendHeader("Location","http://192.168.4.1/",true);
    server.send(302,"text/plain","redirect");
  } else server.send(204,"","");
}

void handleHotspotDetect() {
  // iOS: если title НЕ "Success" → показывает captive portal окно
  if(sysMode==SYS_SETUP){
    // НЕ отвечаем Success — iOS откроет портал
    server.sendHeader("Location","http://192.168.4.1/",true);
    server.send(302,"text/plain","redirect");
  } else {
    // Нормальный режим — отвечаем Success, iOS не показывает окно
    server.send(200,"text/html","<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
  }
}


// ============================================================================
//   Главная страница (рабочий режим)
// ============================================================================
String getEffectName(uint8_t m) {
  const char* n[]={"Радужный","Пульсирующий","Волновой","Хаотичный","Статический","Чередование"};
  return (m<6)?n[m]:"?";
}

const char* ruCities[]={"Moscow","Saint Petersburg","Novosibirsk","Yekaterinburg","Kazan","Nizhny Novgorod","Chelyabinsk","Samara","Omsk","Rostov-on-Don","Ufa","Krasnoyarsk","Voronezh","Perm","Volgograd","Krasnodar","Tyumen","Saratov","Tolyatti","Izhevsk","Barnaul","Irkutsk","Khabarovsk","Vladivostok","Yakutsk","Tomsk","Sochi","Murmansk","Kaliningrad","Petropavlovsk-Kamchatsky"};
const char* ruCitiesRu[]={"Москва","Санкт-Петербург","Новосибирск","Екатеринбург","Казань","Нижний Новгород","Челябинск","Самара","Омск","Ростов-на-Дону","Уфа","Красноярск","Воронеж","Пермь","Волгоград","Краснодар","Тюмень","Саратов","Тольятти","Ижевск","Барнаул","Иркутск","Хабаровск","Владивосток","Якутск","Томск","Сочи","Мурманск","Калининград","Петропавловск-Камчатский"};
const int NUM_CITIES=30;

void handleRoot() {
  if(sysMode==SYS_SETUP){handleCaptivePortal(); return;}
  
  String h=F("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Time32</title><style>*{box-sizing:border-box;margin:0;padding:0}body{font-family:-apple-system,system-ui,sans-serif;background:#0a0a0a;color:#e0e0e0;padding:15px}.ct{max-width:500px;margin:0 auto}h1{text-align:center;font-size:1.6em;margin-bottom:20px;color:#4facfe}.s{background:#1a1a2e;border:1px solid #222;border-radius:12px;padding:18px;margin-bottom:14px}.s h2{font-size:1.1em;color:#4facfe;margin-bottom:12px}.br{display:flex;flex-wrap:wrap;gap:6px}button,.b{border:none;padding:10px 14px;border-radius:8px;font-size:.9em;cursor:pointer;color:#fff;background:#2a2a4a;font-weight:500;text-align:center}button:hover,.b:hover{background:#3a3a6a}.b.a{background:#4facfe;color:#000;font-weight:600}.cg{display:grid;grid-template-columns:repeat(8,1fr);gap:6px;margin:10px 0}.cb{width:100%;aspect-ratio:1;border:2px solid transparent;border-radius:8px;cursor:pointer}.cb.a{border-color:#4facfe;box-shadow:0 0 10px rgba(79,172,254,.4)}.sw{display:flex;align-items:center;gap:10px}.sw input[type=range]{flex:1;-webkit-appearance:none;height:6px;border-radius:3px;background:#333}.sw input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:20px;height:20px;border-radius:50%;background:#4facfe}.sv{min-width:35px;text-align:right;font-weight:600;color:#4facfe}textarea,input[type=text],select{width:100%;padding:10px;border:1px solid #333;border-radius:8px;background:#111;color:#fff;font-size:1em;outline:none;font-family:inherit;margin-bottom:8px}textarea:focus,input:focus,select:focus{border-color:#4facfe}.sb{width:100%;background:#4facfe;color:#000;padding:12px;font-size:1em;font-weight:600;margin-top:8px}.cc{display:flex;gap:6px;align-items:center;margin-top:10px}.cc input{width:60px;padding:8px;border:1px solid #333;border-radius:6px;background:#111;color:#fff;text-align:center}.db{background:#3a1a1a;color:#ff4444}.db:hover{background:#5a2a2a}.info{font-size:.85em;color:#666;margin-top:4px}.spw{display:flex;align-items:center;gap:10px;margin-top:10px}</style></head><body><div class='ct'><h1>Time32</h1>");

  // Режим отображения
  h+="<div class='s'><h2>Режим отображения</h2><div class='br'>";
  h+="<button class='b"+String(displayMode==0?" a":"")+"' onclick='location.href=\"/dispmode?m=0\"'>Только часы</button>";
  h+="<button class='b"+String(displayMode==1?" a":"")+"' onclick='location.href=\"/dispmode?m=1\"'>Часы + Погода</button>";
  h+="<button class='b"+String(displayMode==2?" a":"")+"' onclick='location.href=\"/dispmode?m=2\"'>Часы + Погода + Фраза</button>";
  h+="</div></div>";

  // Фраза (видна в режиме 2)
  h+="<div class='s' style='display:"+String(displayMode==2?"block":"none")+"'>";
  h+="<h2>Фраза</h2><form action='/text' method='post'>";
  h+="<textarea name='scrollText' rows='2' maxlength='255'>"+String(scrollText)+"</textarea>";
  h+="<button type='submit' class='sb'>Применить</button></form></div>";

  // Погода (видна в режимах 1 и 2)
  h+="<div class='s' style='display:"+String(displayMode>=1?"block":"none")+"'>";
  h+="<h2>Погода</h2><form action='/weather' method='post'>";
  h+="<label style='color:#aaa;font-size:.85em'>API Key (openweathermap.org)</label>";
  h+="<input type='text' name='apikey' value='"+apiKey+"' placeholder='API ключ'>";
  h+="<label style='color:#aaa;font-size:.85em'>Город</label>";
  h+="<select name='city' onchange='document.getElementById(\"ccf\").style.display=this.value==\"custom\"?\"block\":\"none\"'>";
  bool cityIn=false;
  for(int i=0;i<NUM_CITIES;i++){String sel=(cityName==ruCities[i])?" selected":""; if(cityName==ruCities[i]) cityIn=true; h+="<option value='"+String(ruCities[i])+"'"+sel+">"+String(ruCitiesRu[i])+"</option>";}
  h+="<option value='custom'"+String(!cityIn?" selected":"")+">Другой город...</option></select>";
  h+="<div id='ccf' style='display:"+String(!cityIn?"block":"none")+"'>";
  h+="<input type='text' name='customCity' value='"+String(!cityIn?cityName:"")+"' placeholder='Город (англ.)'></div>";
  h+="<button type='submit' class='sb'>Сохранить</button></form>";
  if(weatherOk){h+="<div style='margin-top:12px;padding:10px;background:#111;border-radius:8px'><p style='color:#4facfe;font-size:.9em'>"+String(weatherText)+"</p></div>";}
  if(apiKey.length()==0){h+="<p class='info' style='margin-top:8px'>Получите ключ: <a href='https://openweathermap.org/api' style='color:#4facfe'>openweathermap.org</a></p>";}
  h+="</div>";

  // Эффекты
  h+="<div class='s'><h2>Эффект</h2><div class='br'>";
  const char* en[]={"Радужный","Пульс","Волна","Хаос","Цвет","Чередование"};
  for(int i=0;i<6;i++) h+="<button class='b"+String(effectMode==i?" a":"")+"' onclick='location.href=\"/set?mode="+String(i)+"\"'>"+en[i]+"</button>";
  h+="</div></div>";

  // Яркость
  h+="<div class='s'><h2>Яркость</h2><div class='sw'>";
  h+="<input type='range' min='5' max='255' value='"+String(brightness)+"' oninput='document.getElementById(\"bv\").textContent=this.value' onchange='fetch(\"/brightness?value=\"+this.value)'>";
  h+="<span class='sv' id='bv'>"+String(brightness)+"</span></div></div>";

  // Скорость прокрутки (влияет на погоду и фразу)
  h+="<div class='s'><h2>Скорость прокрутки</h2><div class='sw'>";
  h+="<span>Быстро</span>";
  h+="<input type='range' min='50' max='400' value='"+String(scrollSpeed)+"' oninput='document.getElementById(\"spv\").textContent=this.value+\"мс\"' onchange='fetch(\"/speed?value=\"+this.value)'>";
  h+="<span>Медленно</span>";
  h+="</div><p style='text-align:center;margin-top:6px'><span class='sv' id='spv'>"+String(scrollSpeed)+"мс</span></p></div>";

  // Цвета
  h+="<div class='s' style='display:"+String(effectMode==4?"block":"none")+"'><h2>Цвет</h2><div class='cg'>";
  for(int i=0;i<NUM_PRESETS;i++){
    String a=(effectMode==4&&staticColor.r==presetColors[i].r&&staticColor.g==presetColors[i].g&&staticColor.b==presetColors[i].b)?" a":"";
    h+="<div class='cb"+a+"' style='background:rgb("+String(presetColors[i].r)+","+String(presetColors[i].g)+","+String(presetColors[i].b)+")' onclick='location.href=\"/preset?index="+String(i)+"\"'></div>";}
  h+="</div><form action='/custom' method='post' class='cc'><span>RGB:</span>";
  h+="<input type='number' name='r' min='0' max='255' value='"+String(staticColor.r)+"'>";
  h+="<input type='number' name='g' min='0' max='255' value='"+String(staticColor.g)+"'>";
  h+="<input type='number' name='b' min='0' max='255' value='"+String(staticColor.b)+"'>";
  h+="<button type='submit' class='b a'>OK</button></form></div>";

  // Сброс
  h+="<div class='s' style='text-align:center'>";
  h+="<button class='db' onclick='if(confirm(\"Сбросить настройки?\"))location.href=\"/reset\"' style='width:100%'>Сброс настроек</button>";
  h+="<button class='db' onclick='if(confirm(\"Сбросить WiFi? Часы уйдут в режим AP!\"))location.href=\"/resetwifi\"' style='width:100%;margin-top:8px'>Сброс WiFi</button>";
  h+="<p class='info' style='margin-top:8px'>Или: RST дважды за 3 сек / FLASH 5 сек</p>";
  h+="<p class='info'>Кнопка RST — перезагрузка</p></div>";

  // Инфо
  h+="<div class='s'><h2>Инфо</h2>";
  h+="<p>Эффект: <strong>"+getEffectName(effectMode)+"</strong> | Яркость: <strong>"+String(brightness)+"</strong></p>";
  h+="<p>WiFi: <strong>"+WiFi.SSID()+"</strong> | IP: <strong>"+WiFi.localIP().toString()+"</strong></p>";
  h+="</div></div></body></html>";
  
  server.send(200,"text/html",h);
}


// ============================================================================
//   HTTP обработчики
// ============================================================================
void handleDispMode() {
  if(server.hasArg("m")){
    displayMode=constrain(server.arg("m").toInt(),0,2);
    currentPhase=SHOW_CLOCK; phaseStartTime=millis();
    textPosition=0; weatherTextPosition=0;
    if(displayMode>=1 && apiKey.length()>0 && wifiConnected && !weatherOk) fetchWeather();
    saveSettings();
  }
  server.sendHeader("Location","/",true); server.send(302,"text/plain","");
}

void handleSet(){if(server.hasArg("mode")){int m=server.arg("mode").toInt();if(m>=0&&m<=5){effectMode=m;saveSettings();}}server.sendHeader("Location","/",true);server.send(302,"text/plain","");}
void handlePreset(){if(server.hasArg("index")){int i=server.arg("index").toInt();if(i>=0&&i<NUM_PRESETS){staticColor=presetColors[i];effectMode=4;saveSettings();}}server.sendHeader("Location","/",true);server.send(302,"text/plain","");}
void handleCustomColor(){if(server.hasArg("r")&&server.hasArg("g")&&server.hasArg("b")){staticColor=CRGB(constrain(server.arg("r").toInt(),0,255),constrain(server.arg("g").toInt(),0,255),constrain(server.arg("b").toInt(),0,255));effectMode=4;saveSettings();}server.sendHeader("Location","/",true);server.send(302,"text/plain","");}
void handleBrightness(){if(server.hasArg("value")){brightness=constrain(server.arg("value").toInt(),5,255);FastLED.setBrightness(brightness);saveSettings();}server.send(200,"text/plain","OK");}

void handleSpeed(){if(server.hasArg("value")){scrollSpeed=constrain(server.arg("value").toInt(),50,400);saveSettings();}server.send(200,"text/plain","OK");}

void handleText() {
  if(server.hasArg("scrollText")){
    String t=server.arg("scrollText"); t.trim();
    if(t.length()>255) t=t.substring(0,255);
    if(t.length()==0) t="HELLO";
    t.toCharArray(scrollText,256); textPosition=0;
    parseScrollText(); saveSettings();
  }
  server.sendHeader("Location","/",true); server.send(302,"text/plain","");
}

void handleWeatherCfg() {
  if(server.hasArg("apikey")){apiKey=server.arg("apikey"); apiKey.trim();}
  if(server.hasArg("city")){
    String c=server.arg("city");
    if(c=="custom"&&server.hasArg("customCity")){c=server.arg("customCity"); c.trim();}
    if(c.length()>0&&c!="custom") cityName=c;
  }
  saveWeatherCfg();
  if(apiKey.length()>0&&wifiConnected) fetchWeather();
  saveSettings();
  server.sendHeader("Location","/",true); server.send(302,"text/plain","");
}

void handleReset(){
  effectMode=0;staticColor=CRGB(255,0,255);brightness=50;displayMode=0;
  strcpy(scrollText,"HELLO");textPosition=0;scrollSpeed=150;
  currentPhase=SHOW_CLOCK;FastLED.setBrightness(brightness);saveSettings();
  server.sendHeader("Location","/",true);server.send(302,"text/plain","");
}

void handleResetWiFi(){
  clearWiFi();
  server.send(200,"text/html",F("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><style>body{font-family:-apple-system,sans-serif;background:#0a0a0a;color:#e0e0e0;display:flex;align-items:center;justify-content:center;min-height:100vh}.c{background:#1a1a2e;border:1px solid #333;border-radius:16px;padding:40px;text-align:center}h2{color:#ff4444}</style></head><body><div class='c'><h2>WiFi сброшен!</h2><p style='color:#aaa;margin-top:10px'>Часы перезагружаются в режим AP...</p></div></body></html>"));
  delay(1500); ESP.restart();
}


// ============================================================================
//   Маршруты
// ============================================================================
void setupRoutes() {
  server.on("/",handleRoot);
  server.on("/dispmode",HTTP_GET,handleDispMode);
  server.on("/set",HTTP_GET,handleSet);
  server.on("/preset",HTTP_GET,handlePreset);
  server.on("/custom",HTTP_POST,handleCustomColor);
  server.on("/brightness",HTTP_GET,handleBrightness);
  server.on("/speed",HTTP_GET,handleSpeed);
  server.on("/text",HTTP_POST,handleText);
  server.on("/weather",HTTP_POST,handleWeatherCfg);
  server.on("/reset",HTTP_GET,handleReset);
  server.on("/resetwifi",HTTP_GET,handleResetWiFi);
  server.on("/scan",HTTP_GET,handleScanNets);
  server.on("/save",HTTP_POST,handleSaveWiFi);
  // Captive portal — iOS
  server.on("/hotspot-detect.html",handleHotspotDetect);
  server.on("/library/test/success.html",handleHotspotDetect);
  // Captive portal — Android
  server.on("/generate_204",handleGenerate204);
  server.on("/gen_204",handleGenerate204);
  // Captive portal — Windows / Firefox
  server.on("/ncsi.txt",handleGenerate204);
  server.on("/connecttest.txt",handleGenerate204);
  server.on("/redirect",handleGenerate204);
  server.on("/fwlink",handleGenerate204);
  server.on("/canonical.html",handleGenerate204);
  server.on("/success.txt",handleGenerate204);
  server.onNotFound(handleNotFound);
  server.begin(); Serial.println("HTTP OK");
}


// ============================================================================
//   WiFi / AP
// ============================================================================
void startAP() {
  sysMode=SYS_SETUP;
  WiFi.mode(WIFI_AP); WiFi.softAP(AP_SSID); delay(100);
  IPAddress ip(192,168,4,1);
  WiFi.softAPConfig(ip,ip,IPAddress(255,255,255,0));
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT,"*",ip);
  Serial.println("AP: "+String(AP_SSID)+" IP: "+ip.toString());
  for(int i=0;i<3;i++){fill_solid(leds,NUM_LEDS,CRGB(0,80,255));FastLED.show();delay(200);fill_solid(leds,NUM_LEDS,CRGB::Black);FastLED.show();delay(200);}
}

bool connectWiFi() {
  if(wifiSSID.length()==0) return false;
  Serial.print("WiFi: "+wifiSSID);
  WiFi.mode(WIFI_STA); WiFi.begin(wifiSSID.c_str(),wifiPassword.c_str());
  int t=0;
  while(WiFi.status()!=WL_CONNECTED&&t<30){
    delay(500);Serial.print(".");
    fill_solid(leds,NUM_LEDS,CRGB::Black);
    int i=xyToIndex(t%MATRIX_WIDTH,3); if(i>=0) leds[i]=CRGB(0,80,255);
    FastLED.show(); t++;
  }
  if(WiFi.status()==WL_CONNECTED){
    Serial.println("\nOK! IP: "+WiFi.localIP().toString());
    fill_solid(leds,NUM_LEDS,CRGB(0,80,0));FastLED.show();delay(400);
    fill_solid(leds,NUM_LEDS,CRGB::Black);FastLED.show();
    return true;
  }
  Serial.println("\nFAIL"); return false;
}


// ============================================================================
//   SETUP
// ============================================================================
void setup() {
  Serial.begin(115200); delay(100);
  Serial.println("\n=== Time32 b8 ===");
  pinMode(RESET_PIN,INPUT_PULLUP);
  FastLED.addLeds<WS2812,LED_PIN,COLOR_ORDER>(leds,NUM_LEDS);
  FastLED.setBrightness(brightness);
  fill_solid(leds,NUM_LEDS,CRGB::Black); FastLED.show();
  
  loadSettings();
  FastLED.setBrightness(brightness);
  
  // === Double-Reset Detection ===
  // Если RST нажата дважды за 3 секунды — сброс WiFi
  // Читаем счётчик из EEPROM, инкрементируем, ждём 3 сек
  bool doReset = false;
  uint8_t rstCnt = EEPROM.read(EE_RSTCNT);
  
  if (rstCnt >= 1) {
    // Вторая перезагрузка подряд — сброс!
    doReset = true;
    EEPROM.write(EE_RSTCNT, 0);
    EEPROM.commit();
    Serial.println("DOUBLE-RESET: clearing WiFi!");
  } else {
    // Первая перезагрузка — ставим флаг и ждём 3 сек
    EEPROM.write(EE_RSTCNT, 1);
    EEPROM.commit();
    
    // Показываем жёлтую точку пока ждём
    leds[xyToIndex(8, 4)] = CRGB(80, 80, 0);
    FastLED.show();
    
    // Ждём 3 секунды — если за это время будет RST, при следующем boot
    // rstCnt будет 1 и сработает сброс
    delay(3000);
    
    // Не было второго RST — сбрасываем счётчик
    EEPROM.write(EE_RSTCNT, 0);
    EEPROM.commit();
    fill_solid(leds,NUM_LEDS,CRGB::Black); FastLED.show();
  }
  
  // Проверяем также GPIO0 (FLASH) — если зажат прямо сейчас
  if (digitalRead(RESET_PIN) == LOW) {
    doReset = true;
    Serial.println("FLASH held: clearing WiFi!");
  }
  
  if (doReset) {
    clearWiFi();
    // Красная вспышка — WiFi сброшен
    for (int i = 0; i < 3; i++) {
      fill_solid(leds,NUM_LEDS,CRGB(255,0,0)); FastLED.show(); delay(200);
      fill_solid(leds,NUM_LEDS,CRGB::Black); FastLED.show(); delay(200);
    }
  }
  
  if(wifiSSID.length()>0 && !doReset) wifiConnected=connectWiFi();
  
  if(!wifiConnected){
    startAP();
  } else {
    timeClient.begin(); timeClient.forceUpdate();
    sysMode=SYS_RUNNING;
    if(displayMode>=1&&apiKey.length()>0) fetchWeather();
  }
  
  parseScrollText();
  if(strlen(weatherText)>0) parseWeatherText();
  currentPhase=SHOW_CLOCK; phaseStartTime=millis();
  setupRoutes(); randomSeed(analogRead(0));
  Serial.println("=== Ready ===");
}


// ============================================================================
//   LOOP
// ============================================================================
void loop() {
  if(sysMode==SYS_SETUP) dnsServer.processNextRequest();
  server.handleClient();
  if(wifiConnected) timeClient.update();
  
  // === FLASH (GPIO0) long-press: 5 сек → сброс WiFi ===
  static unsigned long flashPressStart = 0;
  static bool flashWasPressed = false;
  if (digitalRead(RESET_PIN) == LOW) {
    if (!flashWasPressed) { flashWasPressed = true; flashPressStart = millis(); }
    else if (millis() - flashPressStart > 5000) {
      // 5 секунд — сброс!
      Serial.println("FLASH 5s hold: reset WiFi!");
      clearWiFi();
      for(int i=0;i<5;i++){fill_solid(leds,NUM_LEDS,CRGB(255,0,0));FastLED.show();delay(150);fill_solid(leds,NUM_LEDS,CRGB::Black);FastLED.show();delay(150);}
      ESP.restart();
    }
  } else {
    flashWasPressed = false;
  }
  
  // Автообновление погоды
  if(wifiConnected && apiKey.length()>0 && displayMode>=1 && (millis()-lastWeatherUpdate>WEATHER_UPDATE_MS))
    fetchWeather();
  
  unsigned long now = millis();
  
  if(sysMode==SYS_SETUP) {
    // AP режим — дыхание
    static uint8_t bv=0; static int8_t bd=1; static unsigned long lb=0;
    if(now-lb>30){lb=now; fill_solid(leds,NUM_LEDS,CRGB::Black);
      bv+=bd*3; if(bv>120) bd=-1; if(bv<5) bd=1;
      drawGlyph(0,2,1,CRGB(0,bv,bv*2)); drawGlyph(15,7,1,CRGB(0,bv,bv*2));
    }
  } else if(displayMode==0) {
    // Только часы
    if(wifiConnected) displayTime(timeClient.getHours(),timeClient.getMinutes(),timeClient.getSeconds());
    else displayTime(0,0,now/1000);
    
  } else {
    // Режимы 1 и 2: чередование фаз
    
    switch(currentPhase) {
      case SHOW_CLOCK:
        if(wifiConnected) displayTime(timeClient.getHours(),timeClient.getMinutes(),timeClient.getSeconds());
        else displayTime(0,0,now/1000);
        
        if(now - phaseStartTime > CLOCK_SHOW_MS) {
          // Переход к погоде
          currentPhase = SHOW_WEATHER;
          phaseStartTime = now;
          weatherTextPosition = 0;
        }
        break;
        
      case SHOW_WEATHER:
        if(parsedWeatherCount > 0) {
          if(now - lastScrollTime > (unsigned long)scrollSpeed) {
            lastScrollTime = now;
            bool done = displayScrolling(parsedWeather, parsedWeatherCount, totalWeatherWidth, weatherTextPosition);
            weatherTextPosition++;
            if(done) {
              // Погода прокрутилась полностью
              if(displayMode == 2) {
                // Переход к фразе
                currentPhase = SHOW_TEXT;
                phaseStartTime = now;
                textPosition = 0;
              } else {
                // Режим 1 — обратно к часам
                currentPhase = SHOW_CLOCK;
                phaseStartTime = now;
              }
            }
          }
        } else {
          // Нет данных погоды — показываем "---" на 3 секунды
          fill_solid(leds,NUM_LEDS,CRGB::Black);
          drawGlyph(GLYPH_MINUS,4,1,CRGB(100,100,100));
          drawGlyph(GLYPH_MINUS,7,1,CRGB(100,100,100));
          drawGlyph(GLYPH_MINUS,10,1,CRGB(100,100,100));
          if(now - phaseStartTime > 3000) {
            if(displayMode==2){currentPhase=SHOW_TEXT;phaseStartTime=now;textPosition=0;}
            else {currentPhase=SHOW_CLOCK;phaseStartTime=now;}
          }
        }
        break;
        
      case SHOW_TEXT:
        if(parsedTextCount > 0) {
          if(now - lastScrollTime > (unsigned long)scrollSpeed) {
            lastScrollTime = now;
            bool done = displayScrolling(parsedText, parsedTextCount, totalTextWidth, textPosition);
            textPosition++;
            if(done) {
              // Фраза прокрутилась — обратно к часам
              currentPhase = SHOW_CLOCK;
              phaseStartTime = now;
            }
          }
        } else {
          // Нет текста — сразу к часам
          currentPhase = SHOW_CLOCK;
          phaseStartTime = now;
        }
        break;
    }
  }
  
  hue++;
  FastLED.show();
  delay(20);
}
