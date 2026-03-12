/*
 * TIME32 — LED Часы на ESP8266 (NodeMCU ESP-12E)
 * Версия: b14
 * 
 * Что нового в b12a:
 *   - Премиальный тёмный веб-интерфейс (iOS/Android/iPad адаптивный)
 *   - 10 эффектов: Радужный, Пульс, Волна, Хаос, Цвет, Чередование,
 *                  Огонь, Плазма, Градиент, Конфетти
 *   - HTML color picker для произвольного цвета
 *   - Ночной режим (авто-затемнение по расписанию)
 *   - Настройка часового пояса (UTC offset)
 *   - Опциональный пароль на веб-интерфейс
 *   - Настройка времени показа часов между чередованиями
 *   - API для отправки текста на матрицу (/api/text?msg=...)
 *   - Favicon
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
#define RESET_PIN 0
#define AP_SSID "Time32"
#define DNS_PORT 53
#define WEATHER_UPDATE_MS 900000UL
#define FW_VERSION "b14"

// ============ EEPROM Layout (768 байт) ============
#define EEPROM_SIZE 768
#define EE_MARKER       0
#define EE_EFFECT       4
#define EE_BRIGHT       5
#define EE_COL_R        6
#define EE_COL_G        7
#define EE_COL_B        8
#define EE_DISPMODE     9
#define EE_SPEED_L     10
#define EE_SPEED_H     11
#define EE_TXTLEN      12
#define EE_TXTDATA     13   // 256 байт
#define EE_SSID_LEN   270
#define EE_SSID_DATA  271
#define EE_PASS_LEN   303
#define EE_PASS_DATA  304
#define EE_APIKEY_LEN 370
#define EE_APIKEY_DATA 371
#define EE_CITY_LEN   412
#define EE_CITY_DATA  413
#define EE_RSTCNT     464
// b14 новые поля:
#define EE_TIMEZONE   465   // int8_t: UTC offset (-12..+14)
#define EE_NIGHTMODE  466   // 0=выкл, 1=вкл
#define EE_NIGHT_START 467  // час начала ночного режима (0-23)
#define EE_NIGHT_END  468   // час конца
#define EE_NIGHT_BRI  469   // яркость ночного режима
#define EE_CLOCK_SEC_L 470  // секунды показа часов (uint16)
#define EE_CLOCK_SEC_H 471
#define EE_ADMINPW_LEN 472
#define EE_ADMINPW_DATA 473 // 32 байта (473..504)

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

uint8_t displayMode = 0;

enum ShowPhase { SHOW_CLOCK, SHOW_WEATHER, SHOW_TEXT };
ShowPhase currentPhase = SHOW_CLOCK;
unsigned long phaseStartTime = 0;
uint16_t clockShowSec = 10;

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
int8_t utcOffset = 3; // Москва по умолчанию

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

// Ночной режим
bool nightModeEnabled = false;
uint8_t nightStart = 23;
uint8_t nightEnd = 7;
uint8_t nightBrightness = 5;
bool isNightNow = false;

// Пароль админки
String adminPassword = "";

bool wifiConnected = false;

// Массивы парсинга
ParsedChar parsedText[256];
int parsedTextCount = 0;
int totalTextWidth = 0;
ParsedChar parsedWeather[128];
int parsedWeatherCount = 0;
int totalWeatherWidth = 0;

// ============ Цвета ============
const CRGB presetColors[] = {
  CRGB(255,0,255), CRGB(128,0,128), CRGB(148,0,211),
  CRGB(138,43,226), CRGB(186,85,211), CRGB(75,0,130),
  CRGB(255,0,0), CRGB(220,20,60), CRGB(255,20,147),
  CRGB(255,105,180), CRGB(199,21,133),
  CRGB(255,165,0), CRGB(255,140,0), CRGB(255,69,0),
  CRGB(255,255,0), CRGB(255,215,0),
  CRGB(0,255,0), CRGB(0,200,0), CRGB(50,205,50),
  CRGB(0,255,127), CRGB(127,255,0),
  CRGB(0,0,255), CRGB(0,100,255), CRGB(30,144,255),
  CRGB(0,191,255), CRGB(0,255,255),
  CRGB(0,206,209), CRGB(64,224,208), CRGB(72,209,204),
  CRGB(255,255,255), CRGB(255,250,205), CRGB(255,228,196)
};
const int NUM_PRESETS = sizeof(presetColors)/sizeof(presetColors[0]);

// ============ Шрифт цифр ============
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
  // Кириллица
  {3,{0b010,0b101,0b111,0b101,0b101}},{3,{0b111,0b100,0b110,0b101,0b111}},
  {3,{0b110,0b101,0b110,0b101,0b110}},{3,{0b111,0b100,0b100,0b100,0b100}},
  {3,{0b011,0b011,0b011,0b101,0b111}},{3,{0b111,0b100,0b110,0b100,0b111}},
  {5,{0b10101,0b01110,0b00100,0b01110,0b10101}},
  {3,{0b110,0b001,0b010,0b001,0b110}},{3,{0b101,0b101,0b101,0b111,0b101}},
  {3,{0b010,0b101,0b101,0b111,0b101}},{3,{0b101,0b110,0b100,0b110,0b101}},
  {3,{0b011,0b011,0b011,0b101,0b101}},
  {5,{0b10001,0b11011,0b10101,0b10101,0b10001}},
  {3,{0b101,0b101,0b111,0b101,0b101}},{3,{0b111,0b101,0b101,0b101,0b111}},
  {3,{0b111,0b101,0b101,0b101,0b101}},{3,{0b111,0b101,0b111,0b100,0b100}},
  {3,{0b111,0b100,0b100,0b100,0b111}},{3,{0b111,0b010,0b010,0b010,0b010}},
  {3,{0b101,0b101,0b011,0b001,0b110}},
  {5,{0b01110,0b10101,0b10101,0b01110,0b00100}},
  {3,{0b101,0b101,0b010,0b101,0b101}},{3,{0b101,0b101,0b101,0b101,0b111}},
  {3,{0b101,0b101,0b111,0b001,0b001}},
  {5,{0b10101,0b10101,0b10101,0b10101,0b11111}},
  {5,{0b10101,0b10101,0b10101,0b10101,0b11111}},
  {3,{0b110,0b010,0b011,0b011,0b011}},
  {5,{0b10001,0b10001,0b11001,0b10101,0b11001}},
  {3,{0b100,0b100,0b110,0b101,0b110}},{3,{0b110,0b001,0b011,0b001,0b110}},
  {5,{0b10010,0b10101,0b11101,0b10101,0b10010}},
  {3,{0b011,0b101,0b011,0b001,0b001}},{3,{0b101,0b100,0b110,0b100,0b111}},
  // Спецсимволы
  {3,{0b000,0b000,0b000,0b000,0b000}},{3,{0b000,0b000,0b000,0b000,0b111}},
  {1,{0b1,0b1,0b1,0b0,0b1}},{1,{0b0,0b0,0b0,0b0,0b1}},
  {1,{0b0,0b0,0b0,0b1,0b1}},{3,{0b000,0b000,0b111,0b000,0b000}},
  {3,{0b000,0b010,0b111,0b010,0b000}},{2,{0b11,0b11,0b00,0b00,0b00}},
  {1,{0b0,0b1,0b0,0b1,0b0}},{3,{0b101,0b001,0b010,0b100,0b101}},
  {3,{0b001,0b001,0b010,0b100,0b100}},
};

GlyphDef readGlyph(int idx) {
  GlyphDef g; memcpy_P(&g, &glyphs[idx], sizeof(GlyphDef)); return g;
}


// ============================================================================
//   UTF-8 → глиф
// ============================================================================
int getGlyphIndex(const char* &ptr) {
  uint8_t c = (uint8_t)*ptr;
  if (c == 0) return -1;
  if (c < 0x80) {
    ptr++;
    if (c>='A'&&c<='Z') return c-'A';
    if (c>='a'&&c<='z') return c-'a';
    if (c>='0'&&c<='9') return -(c-'0'+100);
    switch(c){
      case ' ':return GLYPH_SPACE;case '_':return GLYPH_UNDERSCORE;
      case '!':return GLYPH_EXCL;case '.':return GLYPH_DOT;
      case ',':return GLYPH_COMMA;case '-':return GLYPH_MINUS;
      case '+':return GLYPH_PLUS;case ':':return GLYPH_COLON;
      case '%':return GLYPH_PERCENT;case '/':return GLYPH_SLASH;
      default:return GLYPH_SPACE;
    }
  }
  if ((c&0xE0)==0xC0&&ptr[1]) {
    uint16_t cp=((c&0x1F)<<6)|((uint8_t)ptr[1]&0x3F); ptr+=2;
    if (cp==0x0401||cp==0x0451) return 58;
    if (cp==0x00B0) return GLYPH_DEGREE;
    if (cp>=0x0410&&cp<=0x042F) return 26+(cp-0x0410);
    if (cp>=0x0430&&cp<=0x044F) return 26+(cp-0x0430);
    return GLYPH_SPACE;
  }
  if ((c&0xF0)==0xE0){ptr+=3;return GLYPH_SPACE;}
  if ((c&0xF8)==0xF0){ptr+=4;return GLYPH_SPACE;}
  ptr++; return GLYPH_SPACE;
}


// ============================================================================
//   LED / Эффекты
// ============================================================================
int xyToIndex(int x, int y) {
  if (x<0||x>=MATRIX_WIDTH||y<0||y>=MATRIX_HEIGHT) return -1;
  return (x<8)?(y*8+x):(64+y*8+x-8);
}

// 10 эффектов (0-9)
CRGB getEffectColor(int pos, int extra) {
  switch (effectMode) {
    case 0: return CHSV(hue+pos*25, 255, 255);                                    // Радужный
    case 1: return CHSV(hue, 190+sin8(hue*2+pos*20)/4, 255);                      // Пульс
    case 2: return CHSV(hue+sin8(hue*2+pos*30)/5, 255, 255);                      // Волна
    case 3: return CHSV(hue+extra*33+random8(20),200+random8(55),220+random8(35)); // Хаос
    case 4: return staticColor;                                                     // Цвет
    case 5: return CHSV((hue/2)+(pos*45), 240, 255);                               // Чередование
    case 6: { // Огонь (тёплые тона)
      uint8_t h = hue + pos * 8;
      return CHSV(h % 40, 255, 200 + sin8(h * 3 + pos * 50) / 5);
    }
    case 7: { // Плазма (медленные переливы)
      uint8_t v = sin8(hue * 2 + pos * 40) / 2 + sin8(hue * 3 + pos * 20) / 2;
      return CHSV(v + hue, 220, 255);
    }
    case 8: { // Градиент (2 цвета плавно)
      uint8_t blend = sin8(hue + pos * 30);
      CRGB c1 = CHSV(hue, 255, 255);
      CRGB c2 = CHSV(hue + 128, 255, 255);
      return blend_S(c1, c2, blend);
    }
    case 9: { // Конфетти (случайные яркие вспышки)
      if (random8() < 60) return CHSV(random8(), 200, 255);
      return CHSV(hue + pos * 20, 255, 180);
    }
    default: return CRGB::White;
  }
}

// Вспомогательная функция blend для CRGB
CRGB blend_S(CRGB a, CRGB b, uint8_t t) {
  return CRGB(
    a.r + (int)(b.r - a.r) * t / 255,
    a.g + (int)(b.g - a.g) * t / 255,
    a.b + (int)(b.b - a.b) * t / 255
  );
}

void setPixel(int idx, int r, int c, CRGB base) {
  if (idx<0||idx>=NUM_LEDS) return;
  if (effectMode==3||effectMode==9) { leds[idx]=base; return; }
  if (effectMode==1||effectMode==4) { leds[idx]=base; return; }
  uint8_t h=(r+c)*5; CRGB cl=base;
  cl.r=qadd8(cl.r,sin8(hue+h)/10);
  cl.g=qadd8(cl.g,sin8(hue+85+h)/10);
  cl.b=qadd8(cl.b,sin8(hue+170+h)/10);
  leds[idx]=cl;
}

int drawDigit(int d,int x,int y,CRGB base) {
  if(d<0||d>9) return 3;
  for(int r=0;r<5;r++) for(int c=0;c<3;c++)
    if(font5x3[d][r]&(1<<(2-c))) setPixel(xyToIndex(x+c,y+r),r,c,base);
  return 3;
}

int drawGlyph(int gi,int x,int y,CRGB base) {
  if(gi<0||gi>=GLYPH_COUNT) return 3;
  GlyphDef g=readGlyph(gi);
  for(int r=0;r<5;r++) for(int c=0;c<g.width;c++)
    if(g.rows[r]&(1<<(g.width-1-c))) setPixel(xyToIndex(x+c,y+r),r,c,base);
  return g.width;
}

void drawColon(int x,int y,CRGB base) {
  if(x<0||x>=MATRIX_WIDTH) return;
  int i1=xyToIndex(x,y+1),i2=xyToIndex(x,y+3);
  if(i1>=0) leds[i1]=base; if(i2>=0) leds[i2]=base;
}


// ============================================================================
//   Отображение
// ============================================================================
void displayTime(int h,int m,int s) {
  fill_solid(leds,NUM_LEDS,CRGB::Black);
  int sy=(MATRIX_HEIGHT-5)/2;
  drawDigit(h/10,0,sy,getEffectColor(0,h/10));
  drawDigit(h%10,4,sy,getEffectColor(1,h%10));
  if(s%2==0) drawColon(7,sy,getEffectColor(2,10));
  drawDigit(m/10,9,sy,getEffectColor(3,m/10));
  drawDigit(m%10,13,sy,getEffectColor(4,m%10));
}

void parseString(const char* str, ParsedChar* out, int &count, int &totalW, int maxC) {
  count=0; totalW=0;
  const char* p=str;
  while(*p&&count<maxC) {
    int gi=getGlyphIndex(p);
    out[count].glyphIndex=gi;
    out[count].pixelX=totalW;
    out[count].width=(gi<=-100)?3:(gi>=0&&gi<GLYPH_COUNT)?readGlyph(gi).width:3;
    totalW+=out[count].width+1;
    count++;
  }
}
void parseScrollText(){parseString(scrollText,parsedText,parsedTextCount,totalTextWidth,255);}
void parseWeatherText(){parseString(weatherText,parsedWeather,parsedWeatherCount,totalWeatherWidth,127);}

bool displayScrolling(ParsedChar* chars,int count,int totalW,int &pos) {
  fill_solid(leds,NUM_LEDS,CRGB::Black);
  if(count==0) return true;
  int yOff=(MATRIX_HEIGHT-5)/2;
  for(int i=0;i<count;i++){
    int sx=chars[i].pixelX-pos;
    if(sx+chars[i].width<0) continue;
    if(sx>=MATRIX_WIDTH) break;
    CRGB color=getEffectColor(i,i%10);
    int gi=chars[i].glyphIndex;
    if(gi<=-100) drawDigit(-(gi+100),sx,yOff,color);
    else drawGlyph(gi,sx,yOff,color);
  }
  return (pos>totalW+2);
}


// ============================================================================
//   EEPROM
// ============================================================================
void eepromWriteStr(int aL,int aD,const String &s,int mx){
  int l=s.length();if(l>mx) l=mx;
  EEPROM.write(aL,l);
  for(int i=0;i<l;i++) EEPROM.write(aD+i,s[i]);
}
String eepromReadStr(int aL,int aD,int mx){
  int l=EEPROM.read(aL);if(l>mx||l==0) return "";
  char b[65];int rl=(l>64)?64:l;
  for(int i=0;i<rl;i++) b[i]=EEPROM.read(aD+i);
  b[rl]='\0'; return String(b);
}
bool isEEInit(){return EEPROM.read(0)=='T'&&EEPROM.read(1)=='3'&&EEPROM.read(2)=='2';}
void writeMark(){EEPROM.write(0,'T');EEPROM.write(1,'3');EEPROM.write(2,'2');}

void loadSettings() {
  EEPROM.begin(EEPROM_SIZE);
  if(!isEEInit()){writeMark();saveSettings();saveWiFi();saveWeatherCfg();EEPROM.commit();return;}
  effectMode=EEPROM.read(EE_EFFECT);if(effectMode>9) effectMode=0;
  brightness=EEPROM.read(EE_BRIGHT);if(brightness<5) brightness=50;
  staticColor.r=EEPROM.read(EE_COL_R);staticColor.g=EEPROM.read(EE_COL_G);staticColor.b=EEPROM.read(EE_COL_B);
  displayMode=EEPROM.read(EE_DISPMODE);if(displayMode>2) displayMode=0;
  scrollSpeed=EEPROM.read(EE_SPEED_L)|(EEPROM.read(EE_SPEED_H)<<8);if(scrollSpeed<50||scrollSpeed>500) scrollSpeed=150;
  int tl=EEPROM.read(EE_TXTLEN);
  if(tl>0&&tl<256){for(int i=0;i<tl;i++) scrollText[i]=EEPROM.read(EE_TXTDATA+i);scrollText[tl]='\0';}
  else strcpy(scrollText,"HELLO");
  wifiSSID=eepromReadStr(EE_SSID_LEN,EE_SSID_DATA,32);
  wifiPassword=eepromReadStr(EE_PASS_LEN,EE_PASS_DATA,63);
  apiKey=eepromReadStr(EE_APIKEY_LEN,EE_APIKEY_DATA,40);
  cityName=eepromReadStr(EE_CITY_LEN,EE_CITY_DATA,50);
  if(cityName.length()==0) cityName="Moscow";
  // b14 поля
  int8_t tz=(int8_t)EEPROM.read(EE_TIMEZONE);
  if(tz>=-12&&tz<=14) utcOffset=tz; else utcOffset=3;
  nightModeEnabled=EEPROM.read(EE_NIGHTMODE)==1;
  nightStart=EEPROM.read(EE_NIGHT_START);if(nightStart>23) nightStart=23;
  nightEnd=EEPROM.read(EE_NIGHT_END);if(nightEnd>23) nightEnd=7;
  nightBrightness=EEPROM.read(EE_NIGHT_BRI);if(nightBrightness<1) nightBrightness=5;
  clockShowSec=EEPROM.read(EE_CLOCK_SEC_L)|(EEPROM.read(EE_CLOCK_SEC_H)<<8);
  if(clockShowSec<3||clockShowSec>120) clockShowSec=10;
  adminPassword=eepromReadStr(EE_ADMINPW_LEN,EE_ADMINPW_DATA,32);
}

void saveSettings() {
  writeMark();
  EEPROM.write(EE_EFFECT,effectMode);EEPROM.write(EE_BRIGHT,brightness);
  EEPROM.write(EE_COL_R,staticColor.r);EEPROM.write(EE_COL_G,staticColor.g);EEPROM.write(EE_COL_B,staticColor.b);
  EEPROM.write(EE_DISPMODE,displayMode);
  EEPROM.write(EE_SPEED_L,scrollSpeed&0xFF);EEPROM.write(EE_SPEED_H,(scrollSpeed>>8)&0xFF);
  int tl=strlen(scrollText);if(tl>255) tl=255;
  EEPROM.write(EE_TXTLEN,tl);for(int i=0;i<tl;i++) EEPROM.write(EE_TXTDATA+i,scrollText[i]);
  EEPROM.write(EE_TIMEZONE,(uint8_t)utcOffset);
  EEPROM.write(EE_NIGHTMODE,nightModeEnabled?1:0);
  EEPROM.write(EE_NIGHT_START,nightStart);EEPROM.write(EE_NIGHT_END,nightEnd);
  EEPROM.write(EE_NIGHT_BRI,nightBrightness);
  EEPROM.write(EE_CLOCK_SEC_L,clockShowSec&0xFF);EEPROM.write(EE_CLOCK_SEC_H,(clockShowSec>>8)&0xFF);
  eepromWriteStr(EE_ADMINPW_LEN,EE_ADMINPW_DATA,adminPassword,32);
  EEPROM.commit();
}
void saveWiFi(){eepromWriteStr(EE_SSID_LEN,EE_SSID_DATA,wifiSSID,32);eepromWriteStr(EE_PASS_LEN,EE_PASS_DATA,wifiPassword,63);EEPROM.commit();}
void saveWeatherCfg(){eepromWriteStr(EE_APIKEY_LEN,EE_APIKEY_DATA,apiKey,40);eepromWriteStr(EE_CITY_LEN,EE_CITY_DATA,cityName,50);EEPROM.commit();}
void clearWiFi(){EEPROM.write(EE_SSID_LEN,0);EEPROM.write(EE_PASS_LEN,0);EEPROM.commit();wifiSSID="";wifiPassword="";}


// ============================================================================
//   Погода
// ============================================================================
String jsonExtract(const String &json,const String &key){
  String s="\""+key+"\"";int i=json.indexOf(s);if(i<0) return "";
  i=json.indexOf(':',i+s.length());if(i<0) return "";i++;
  while(i<(int)json.length()&&json[i]==' ') i++;
  if(json[i]=='"'){int a=i+1,b=json.indexOf('"',a);return(b>a)?json.substring(a,b):"";}
  int a=i,b=a;while(b<(int)json.length()&&json[b]!=','&&json[b]!='}'&&json[b]!=']') b++;
  return json.substring(a,b);
}

void fetchWeather(){
  if(apiKey.length()==0||!wifiConnected) return;
  WiFiClient client;HTTPClient http;
  String url="http://api.openweathermap.org/data/2.5/weather?q="+cityName+"&appid="+apiKey+"&units=metric&lang=ru";
  http.begin(client,url);http.setTimeout(8000);int code=http.GET();
  if(code==200){
    String p=http.getString();
    weatherTemp=(int)jsonExtract(p,"temp").toFloat();
    int wi=p.indexOf("\"description\"");
    if(wi>0){int ci=p.indexOf(':',wi);if(ci>0){int qs=p.indexOf('"',ci+1);int qe=p.indexOf('"',qs+1);if(qs>0&&qe>qs) weatherDesc=p.substring(qs+1,qe);}}
    String city=jsonExtract(p,"name");if(city.length()==0) city=cityName;
    String hum=jsonExtract(p,"humidity");
    int wind=(int)jsonExtract(p,"speed").toFloat();
    String w=city+" ";
    if(weatherTemp>0) w+="+";
    w+=String(weatherTemp)+"°C "+weatherDesc;
    if(hum.length()>0) w+="  "+hum+"%";
    if(wind>0) w+="  ветер "+String(wind)+"м/с";
    w.toCharArray(weatherText,128);
    parseWeatherText();weatherOk=true;
  } else {
    if(code==401) strcpy(weatherText,"Ошибка API ключа");
    else if(code==404) strcpy(weatherText,"Город не найден");
    else strcpy(weatherText,"Нет данных");
    parseWeatherText();weatherOk=false;
  }
  http.end();lastWeatherUpdate=millis();
}


// ============================================================================
//   Ночной режим
// ============================================================================
void checkNightMode() {
  if(!nightModeEnabled||!wifiConnected) {isNightNow=false; return;}
  int h=timeClient.getHours();
  if(nightStart>nightEnd) {
    isNightNow=(h>=nightStart||h<nightEnd);
  } else {
    isNightNow=(h>=nightStart&&h<nightEnd);
  }
  FastLED.setBrightness(isNightNow?nightBrightness:brightness);
}


// ============================================================================
//   Аутентификация
// ============================================================================
bool checkAuth() {
  if(adminPassword.length()==0) return true;
  if(!server.authenticate("admin",adminPassword.c_str())){
    server.requestAuthentication(DIGEST_AUTH,"Time32","Login required");
    return false;
  }
  return true;
}


// ============================================================================
//   Captive Portal
// ============================================================================
void handleCaptivePortal() {
  String html=F(
    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no'>"
    "<title>Time32</title><style>"
    ":root{--bg:#080810;--card:#12122a;--border:#1e1e3a;--accent:#6c5ce7;--accent2:#a29bfe;"
    "--text:#e0e0f0;--dim:#666680;--input:#0a0a1a;--danger:#ff4757;--success:#2ed573}"
    "*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}"
    "body{font-family:'SF Pro Display',-apple-system,system-ui,sans-serif;background:var(--bg);"
    "color:var(--text);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px}"
    ".card{background:var(--card);border:1px solid var(--border);border-radius:20px;padding:32px;"
    "max-width:420px;width:100%;backdrop-filter:blur(20px)}"
    ".logo{text-align:center;margin-bottom:24px}"
    ".logo h1{font-size:2em;font-weight:700;background:linear-gradient(135deg,var(--accent),var(--accent2));"
    "-webkit-background-clip:text;-webkit-text-fill-color:transparent;letter-spacing:-0.02em}"
    ".logo p{color:var(--dim);font-size:.85em;margin-top:4px}"
    ".field{margin-bottom:16px}"
    ".field label{display:block;font-size:.8em;color:var(--dim);margin-bottom:6px;text-transform:uppercase;"
    "letter-spacing:.05em;font-weight:600}"
    "input[type=text],input[type=password]{width:100%;padding:14px 16px;border:1px solid var(--border);"
    "border-radius:12px;background:var(--input);color:var(--text);font-size:1em;outline:none;"
    "transition:border .3s,box-shadow .3s}"
    "input:focus{border-color:var(--accent);box-shadow:0 0 0 3px rgba(108,92,231,.15)}"
    ".toggle{display:flex;align-items:center;gap:8px;font-size:.85em;color:var(--dim);margin-top:8px;cursor:pointer}"
    ".toggle input{width:18px;height:18px;accent-color:var(--accent)}"
    "button{width:100%;padding:14px;border:none;border-radius:12px;font-size:1em;font-weight:600;"
    "cursor:pointer;transition:all .2s}"
    ".btn-primary{background:linear-gradient(135deg,var(--accent),var(--accent2));color:#fff;margin-top:8px}"
    ".btn-primary:active{transform:scale(.98);opacity:.9}"
    ".btn-scan{background:var(--input);color:var(--accent);border:1px solid var(--border);margin-bottom:16px}"
    ".nets{max-height:180px;overflow-y:auto;margin-bottom:16px}"
    ".net{padding:12px;border:1px solid var(--border);border-radius:10px;margin-bottom:6px;"
    "cursor:pointer;display:flex;justify-content:space-between;align-items:center;transition:all .2s}"
    ".net:hover,.net:active{background:rgba(108,92,231,.1);border-color:var(--accent)}"
    ".net .name{font-weight:500}.net .meta{color:var(--dim);font-size:.85em}"
    ".hint{text-align:center;color:var(--dim);margin-top:20px;font-size:.8em}"
    "</style></head><body><div class='card'>"
    "<div class='logo'><h1>Time32</h1><p>Настройка Wi-Fi</p></div>"
    "<button class='btn-scan' onclick='scan()' id='sb'>Поиск сетей</button>"
    "<div class='nets' id='ns'></div>"
    "<form action='/save' method='POST'>"
    "<div class='field'><label>Имя сети</label>"
    "<input type='text' name='ssid' id='ssid' required autocomplete='off'></div>"
    "<div class='field'><label>Пароль</label>"
    "<input type='password' name='pass' id='pass' autocomplete='off'>"
    "<label class='toggle'><input type='checkbox' "
    "onchange=\"document.getElementById('pass').type=this.checked?'text':'password'\">"
    "Показать пароль</label></div>"
    "<button type='submit' class='btn-primary'>Подключить</button></form>"
    "<p class='hint'>После подключения часы перезагрузятся</p></div>"
    "<script>"
    "function scan(){"
    "document.getElementById('sb').textContent='Поиск...';"
    "var x=new XMLHttpRequest();x.open('GET','/scan');x.onload=function(){"
    "document.getElementById('sb').textContent='Поиск сетей';"
    "try{var d=JSON.parse(x.responseText),h='';"
    "for(var i=0;i<d.length;i++){"
    "var b=d[i].r>-50?'●●●●':d[i].r>-70?'●●●○':d[i].r>-80?'●●○○':'●○○○';"
    "h+='<div class=\"net\" onclick=\"document.getElementById(\\'ssid\\').value=\\''+d[i].s.replace(/'/g,\"\\\\'\")+'\\';document.getElementById(\\'pass\\').focus()\">';"
    "h+='<span class=\"name\">'+d[i].s+(d[i].e?' 🔒':'')+'</span>';"
    "h+='<span class=\"meta\">'+b+' '+d[i].r+'</span></div>';}"
    "document.getElementById('ns').innerHTML=h||'<p style=\"padding:12px;color:var(--dim)\">Сети не найдены</p>';"
    "}catch(e){document.getElementById('sb').textContent='Ошибка'}};"
    "x.onerror=function(){document.getElementById('sb').textContent='Ошибка'};"
    "x.send()}"
    "</script></body></html>"
  );
  server.send(200,"text/html",html);
}

void handleScanNets(){
  int n=WiFi.scanNetworks();String j="[";
  for(int i=0;i<n;i++){if(i) j+=",";j+="{\"s\":\""+WiFi.SSID(i)+"\",\"r\":"+String(WiFi.RSSI(i))+",\"e\":"+String(WiFi.encryptionType(i)!=ENC_TYPE_NONE?"true":"false")+"}";}
  j+="]";WiFi.scanDelete();server.send(200,"application/json",j);
}

void handleSaveWiFi(){
  if(server.hasArg("ssid")){
    String s=server.arg("ssid");s.trim();
    String p=server.hasArg("pass")?server.arg("pass"):"";p.trim();
    if(s.length()>0){wifiSSID=s;wifiPassword=p;saveWiFi();
      server.send(200,"text/html",F("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><style>body{font-family:-apple-system,sans-serif;background:#080810;color:#e0e0f0;display:flex;align-items:center;justify-content:center;min-height:100vh}.c{background:#12122a;border:1px solid #1e1e3a;border-radius:20px;padding:40px;text-align:center}.sp{width:40px;height:40px;border:3px solid #1e1e3a;border-top:3px solid #6c5ce7;border-radius:50%;animation:s 1s linear infinite;margin:20px auto}@keyframes s{to{transform:rotate(360deg)}}</style></head><body><div class='c'><div class='sp'></div><h2 style='color:#a29bfe'>Подключение...</h2><p style='color:#666680;margin-top:12px'>Можно закрыть это окно</p></div></body></html>"));
      delay(1500);ESP.restart();
    }
  }
  server.sendHeader("Location","/",true);server.send(302,"text/plain","");
}

void handleNotFound(){
  if(sysMode==SYS_SETUP){server.sendHeader("Location","http://192.168.4.1/",true);server.send(302,"text/plain","");}
  else server.send(404,"text/plain","Not found");
}
void handleGenerate204(){
  if(sysMode==SYS_SETUP){server.sendHeader("Location","http://192.168.4.1/",true);server.send(302,"text/plain","");}
  else server.send(204,"","");
}
void handleHotspotDetect(){
  if(sysMode==SYS_SETUP){server.sendHeader("Location","http://192.168.4.1/",true);server.send(302,"text/plain","");}
  else server.send(200,"text/html","<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
}

// Favicon (простая 16x16 иконка — синий квадрат)
void handleFavicon(){
  server.send(204,"","");
}


// ============================================================================
//   Главная страница — премиальный UI
// ============================================================================
const char* ruCities[]={"Moscow","Saint Petersburg","Novosibirsk","Yekaterinburg","Kazan","Nizhny Novgorod","Chelyabinsk","Samara","Omsk","Rostov-on-Don","Ufa","Krasnoyarsk","Voronezh","Perm","Volgograd","Krasnodar","Tyumen","Saratov","Tolyatti","Izhevsk","Barnaul","Irkutsk","Khabarovsk","Vladivostok","Yakutsk","Tomsk","Sochi","Murmansk","Kaliningrad","Petropavlovsk-Kamchatsky"};
const char* ruCitiesRu[]={"Москва","Санкт-Петербург","Новосибирск","Екатеринбург","Казань","Нижний Новгород","Челябинск","Самара","Омск","Ростов-на-Дону","Уфа","Красноярск","Воронеж","Пермь","Волгоград","Краснодар","Тюмень","Саратов","Тольятти","Ижевск","Барнаул","Иркутск","Хабаровск","Владивосток","Якутск","Томск","Сочи","Мурманск","Калининград","Петропавловск-Камчатский"};
const int NUM_CITIES=30;

String getEffectName(uint8_t m){
  const char* n[]={"Радужный","Пульс","Волна","Хаос","Цвет","Чередование","Огонь","Плазма","Градиент","Конфетти"};
  return(m<10)?n[m]:"?";
}

void handleRoot(){
  if(sysMode==SYS_SETUP){handleCaptivePortal();return;}
  if(!checkAuth()) return;

  // CSS Variables + Premium dark UI
  String h=F("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no'>"
    "<title>Time32</title>"
    "<link rel='icon' href='data:,'>"
    "<style>"
    ":root{--bg:#080810;--card:#12122a;--border:#1e1e3a;--accent:#6c5ce7;--accent2:#a29bfe;"
    "--text:#e0e0f0;--dim:#666680;--input:#0a0a1a;--danger:#ff4757;--success:#2ed573}"
    "*{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}"
    "body{font-family:'SF Pro Display',-apple-system,system-ui,sans-serif;background:var(--bg);"
    "color:var(--text);padding:env(safe-area-inset-top) 16px 16px;min-height:100vh}"
    ".ct{max-width:520px;margin:0 auto}"
    ".header{text-align:center;padding:24px 0 20px}"
    ".header h1{font-size:1.8em;font-weight:700;background:linear-gradient(135deg,var(--accent),var(--accent2));"
    "-webkit-background-clip:text;-webkit-text-fill-color:transparent;letter-spacing:-.02em}"
    ".header .ver{color:var(--dim);font-size:.75em;margin-top:2px}"
    ".s{background:var(--card);border:1px solid var(--border);border-radius:16px;padding:20px;margin-bottom:12px;"
    "transition:border-color .3s}"
    ".s:hover{border-color:rgba(108,92,231,.3)}"
    ".s h2{font-size:.85em;text-transform:uppercase;letter-spacing:.06em;color:var(--dim);margin-bottom:14px;font-weight:600}"
    ".pills{display:flex;flex-wrap:wrap;gap:6px}"
    ".pill{padding:8px 14px;border-radius:20px;font-size:.85em;cursor:pointer;border:1px solid var(--border);"
    "background:var(--input);color:var(--text);font-weight:500;transition:all .2s;text-align:center}"
    ".pill:hover{border-color:var(--accent)}"
    ".pill.a{background:var(--accent);color:#fff;border-color:var(--accent);font-weight:600}"
    ".cg{display:grid;grid-template-columns:repeat(auto-fill,minmax(36px,1fr));gap:6px;margin:12px 0}"
    ".cb{aspect-ratio:1;border:2px solid transparent;border-radius:10px;cursor:pointer;transition:all .2s}"
    ".cb:hover{transform:scale(1.1)}"
    ".cb.a{border-color:var(--accent);box-shadow:0 0 12px rgba(108,92,231,.4)}"
    ".rng{display:flex;align-items:center;gap:12px}"
    ".rng input[type=range]{flex:1;-webkit-appearance:none;height:4px;border-radius:2px;background:var(--border);outline:none}"
    ".rng input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:22px;height:22px;border-radius:50%;"
    "background:var(--accent);cursor:pointer;box-shadow:0 0 8px rgba(108,92,231,.4)}"
    ".rng .val{min-width:40px;text-align:right;font-weight:600;color:var(--accent2);font-size:.9em}"
    ".rng .lbl{color:var(--dim);font-size:.8em;min-width:50px}"
    "textarea,input[type=text],input[type=password],input[type=number],select{"
    "width:100%;padding:12px 14px;border:1px solid var(--border);border-radius:10px;background:var(--input);"
    "color:var(--text);font-size:.95em;outline:none;font-family:inherit;transition:border .3s;margin-bottom:8px}"
    "textarea:focus,input:focus,select:focus{border-color:var(--accent)}"
    "select{-webkit-appearance:none;appearance:none;background-image:url(\"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' viewBox='0 0 12 12'%3E%3Cpath fill='%23666' d='M6 8L1 3h10z'/%3E%3C/svg%3E\");"
    "background-repeat:no-repeat;background-position:right 12px center;padding-right:32px}"
    ".btn{width:100%;padding:13px;border:none;border-radius:10px;font-size:.95em;font-weight:600;cursor:pointer;"
    "transition:all .15s;margin-top:8px}"
    ".btn-accent{background:linear-gradient(135deg,var(--accent),var(--accent2));color:#fff}"
    ".btn-accent:active{transform:scale(.98)}"
    ".btn-danger{background:rgba(255,71,87,.1);color:var(--danger);border:1px solid rgba(255,71,87,.2)}"
    ".btn-danger:active{background:rgba(255,71,87,.2)}"
    ".row{display:flex;gap:8px;align-items:center;margin-bottom:8px}"
    ".row input,.row select{margin-bottom:0}"
    ".color-pick{display:flex;gap:8px;align-items:center;margin-top:10px}"
    ".color-pick input[type=color]{width:44px;height:44px;border:2px solid var(--border);border-radius:10px;"
    "background:var(--input);cursor:pointer;padding:2px}"
    ".info-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}"
    ".info-box{background:var(--input);border:1px solid var(--border);border-radius:10px;padding:10px 12px}"
    ".info-box .label{font-size:.7em;text-transform:uppercase;letter-spacing:.05em;color:var(--dim);margin-bottom:2px}"
    ".info-box .value{font-size:.95em;font-weight:600;color:var(--accent2)}"
    ".toggle-row{display:flex;align-items:center;justify-content:space-between;padding:8px 0}"
    ".toggle-row .name{font-size:.9em}"
    ".switch{position:relative;width:44px;height:24px}"
    ".switch input{opacity:0;width:0;height:0}"
    ".switch .slider{position:absolute;top:0;left:0;right:0;bottom:0;background:var(--border);border-radius:12px;"
    "cursor:pointer;transition:.3s}"
    ".switch .slider:before{content:'';position:absolute;height:18px;width:18px;left:3px;bottom:3px;"
    "background:#fff;border-radius:50%;transition:.3s}"
    ".switch input:checked+.slider{background:var(--accent)}"
    ".switch input:checked+.slider:before{transform:translateX(20px)}"
    "@media(max-width:380px){.pills{gap:4px}.pill{padding:7px 10px;font-size:.8em}.cg{grid-template-columns:repeat(auto-fill,minmax(30px,1fr))}}"
    "</style></head><body><div class='ct'>"
    "<div class='header'><h1>Time32</h1><p class='ver'>" FW_VERSION "</p></div>"
  );

  // Режим
  h+="<div class='s'><h2>Режим</h2><div class='pills'>";
  h+="<div class='pill"+String(displayMode==0?" a":"")+"' onclick='location.href=\"/dispmode?m=0\"'>Часы</div>";
  h+="<div class='pill"+String(displayMode==1?" a":"")+"' onclick='location.href=\"/dispmode?m=1\"'>Часы+Погода</div>";
  h+="<div class='pill"+String(displayMode==2?" a":"")+"' onclick='location.href=\"/dispmode?m=2\"'>Часы+Погода+Фраза</div>";
  h+="</div></div>";

  // Фраза
  h+="<div class='s' style='display:"+String(displayMode==2?"block":"none")+"'><h2>Фраза</h2>";
  h+="<form action='/text' method='post'><textarea name='scrollText' rows='2' maxlength='255'>"+String(scrollText)+"</textarea>";
  h+="<button type='submit' class='btn btn-accent'>Применить</button></form></div>";

  // Погода
  h+="<div class='s' style='display:"+String(displayMode>=1?"block":"none")+"'><h2>Погода</h2>";
  h+="<form action='/weather' method='post'>";
  h+="<input type='text' name='apikey' value='"+apiKey+"' placeholder='OpenWeatherMap API Key'>";
  h+="<select name='city' onchange='document.getElementById(\"ccf\").style.display=this.value==\"custom\"?\"block\":\"none\"'>";
  bool cityIn=false;
  for(int i=0;i<NUM_CITIES;i++){String sel=(cityName==ruCities[i])?" selected":"";if(cityName==ruCities[i]) cityIn=true;h+="<option value='"+String(ruCities[i])+"'"+sel+">"+String(ruCitiesRu[i])+"</option>";}
  h+="<option value='custom'"+String(!cityIn?" selected":"")+">Другой...</option></select>";
  h+="<div id='ccf' style='display:"+String(!cityIn?"block":"none")+"'><input type='text' name='customCity' value='"+String(!cityIn?cityName:"")+"' placeholder='Город (англ.)'></div>";
  h+="<button type='submit' class='btn btn-accent'>Сохранить</button></form>";
  if(weatherOk){h+="<div style='margin-top:10px;padding:10px;background:var(--input);border:1px solid var(--border);border-radius:10px'><span style='color:var(--accent2);font-size:.9em'>"+String(weatherText)+"</span></div>";}
  h+="</div>";

  // Эффекты (10 штук)
  h+="<div class='s'><h2>Эффект</h2><div class='pills'>";
  const char* en[]={"Радужный","Пульс","Волна","Хаос","Цвет","Чередование","Огонь","Плазма","Градиент","Конфетти"};
  for(int i=0;i<10;i++) h+="<div class='pill"+String(effectMode==i?" a":"")+"' onclick='location.href=\"/set?mode="+String(i)+"\"'>"+en[i]+"</div>";
  h+="</div></div>";

  // Яркость
  h+="<div class='s'><h2>Яркость</h2><div class='rng'>";
  h+="<input type='range' min='5' max='255' value='"+String(brightness)+"' oninput='document.getElementById(\"bv\").textContent=this.value' onchange='fetch(\"/brightness?value=\"+this.value)'>";
  h+="<span class='val' id='bv'>"+String(brightness)+"</span></div></div>";

  // Скорость
  h+="<div class='s'><h2>Скорость прокрутки</h2><div class='rng'>";
  h+="<span class='lbl'>Быстро</span>";
  h+="<input type='range' min='50' max='400' value='"+String(scrollSpeed)+"' oninput='document.getElementById(\"sv\").textContent=this.value+\"мс\"' onchange='fetch(\"/speed?value=\"+this.value)'>";
  h+="<span class='val' id='sv'>"+String(scrollSpeed)+"мс</span></div></div>";

  // Цвета + color picker
  h+="<div class='s' style='display:"+String(effectMode==4?"block":"none")+"'><h2>Цвет</h2><div class='cg'>";
  for(int i=0;i<NUM_PRESETS;i++){
    String a=(effectMode==4&&staticColor.r==presetColors[i].r&&staticColor.g==presetColors[i].g&&staticColor.b==presetColors[i].b)?" a":"";
    h+="<div class='cb"+a+"' style='background:rgb("+String(presetColors[i].r)+","+String(presetColors[i].g)+","+String(presetColors[i].b)+")' onclick='location.href=\"/preset?index="+String(i)+"\"'></div>";}
  h+="</div>";
  // Color picker
  char hexCol[8]; sprintf(hexCol,"#%02x%02x%02x",staticColor.r,staticColor.g,staticColor.b);
  h+="<div class='color-pick'><input type='color' id='cp' value='"+String(hexCol)+"' onchange='applyColor(this.value)'>";
  h+="<span style='color:var(--dim);font-size:.85em'>Произвольный цвет</span></div>";
  h+="<script>function applyColor(c){var r=parseInt(c.substr(1,2),16),g=parseInt(c.substr(3,2),16),b=parseInt(c.substr(5,2),16);"
     "fetch('/customcolor?r='+r+'&g='+g+'&b='+b).then(()=>location.reload())}</script>";
  h+="</div>";

  // Настройки
  h+="<div class='s'><h2>Настройки</h2>";
  h+="<form action='/settings' method='post'>";
  // Часовой пояс
  h+="<div class='row'><span style='color:var(--dim);font-size:.85em;min-width:60px'>UTC:</span>";
  h+="<select name='tz' style='flex:1'>";
  for(int i=-12;i<=14;i++){
    String sel=(i==utcOffset)?" selected":"";
    String label="UTC"+(i>=0?String("+")+String(i):String(i));
    h+="<option value='"+String(i)+"'"+sel+">"+label+"</option>";
  }
  h+="</select></div>";
  // Время показа часов
  h+="<div class='row'><span style='color:var(--dim);font-size:.85em;min-width:90px'>Часы (сек):</span>";
  h+="<input type='number' name='clockSec' min='3' max='120' value='"+String(clockShowSec)+"' style='flex:1'></div>";
  // Ночной режим
  h+="<div class='toggle-row'><span class='name'>Ночной режим</span>";
  h+="<label class='switch'><input type='checkbox' name='nightOn' value='1'"+String(nightModeEnabled?" checked":"")+"><span class='slider'></span></label></div>";
  h+="<div class='row'><span style='color:var(--dim);font-size:.85em'>С</span>";
  h+="<input type='number' name='nightStart' min='0' max='23' value='"+String(nightStart)+"' style='flex:1'>";
  h+="<span style='color:var(--dim);font-size:.85em'>до</span>";
  h+="<input type='number' name='nightEnd' min='0' max='23' value='"+String(nightEnd)+"' style='flex:1'>";
  h+="<span style='color:var(--dim);font-size:.85em'>ярк:</span>";
  h+="<input type='number' name='nightBri' min='1' max='50' value='"+String(nightBrightness)+"' style='flex:1'></div>";
  // Пароль
  h+="<div class='row'><span style='color:var(--dim);font-size:.85em;min-width:60px'>Пароль:</span>";
  h+="<input type='text' name='adminPw' value='"+adminPassword+"' placeholder='Пусто = без пароля' style='flex:1;margin-bottom:0'></div>";
  h+="<button type='submit' class='btn btn-accent'>Сохранить настройки</button></form></div>";

  // Сброс
  h+="<div class='s' style='text-align:center'>";
  h+="<button class='btn btn-danger' onclick='if(confirm(\"Сбросить?\"))location.href=\"/reset\"'>Сброс настроек</button>";
  h+="<button class='btn btn-danger' style='margin-top:6px' onclick='if(confirm(\"Сбросить WiFi?\"))location.href=\"/resetwifi\"'>Сброс WiFi</button>";
  h+="<p style='color:var(--dim);font-size:.75em;margin-top:10px'>RST×2 за 3 сек / FLASH 5 сек = сброс WiFi</p></div>";

  // Инфо
  h+="<div class='s'><h2>Система</h2><div class='info-grid'>";
  h+="<div class='info-box'><div class='label'>Эффект</div><div class='value'>"+getEffectName(effectMode)+"</div></div>";
  h+="<div class='info-box'><div class='label'>Яркость</div><div class='value'>"+String(isNightNow?nightBrightness:brightness)+"</div></div>";
  h+="<div class='info-box'><div class='label'>WiFi</div><div class='value'>"+WiFi.SSID()+"</div></div>";
  h+="<div class='info-box'><div class='label'>IP</div><div class='value'>"+WiFi.localIP().toString()+"</div></div>";
  h+="<div class='info-box'><div class='label'>Память</div><div class='value'>"+String(ESP.getFreeHeap()/1024)+"KB</div></div>";
  h+="<div class='info-box'><div class='label'>Uptime</div><div class='value'>"+String(millis()/60000)+"мин</div></div>";
  h+="</div></div>";

  h+="</div></body></html>";
  server.send(200,"text/html",h);
}


// ============================================================================
//   Обработчики
// ============================================================================
void handleDispMode(){
  if(!checkAuth()) return;
  if(server.hasArg("m")){displayMode=constrain(server.arg("m").toInt(),0,2);
  currentPhase=SHOW_CLOCK;phaseStartTime=millis();textPosition=0;weatherTextPosition=0;
  if(displayMode>=1&&apiKey.length()>0&&wifiConnected&&!weatherOk) fetchWeather();
  saveSettings();}
  server.sendHeader("Location","/",true);server.send(302,"text/plain","");
}
void handleSet(){if(!checkAuth()) return;if(server.hasArg("mode")){int m=server.arg("mode").toInt();if(m>=0&&m<=9){effectMode=m;saveSettings();}}server.sendHeader("Location","/",true);server.send(302,"text/plain","");}
void handlePreset(){if(!checkAuth()) return;if(server.hasArg("index")){int i=server.arg("index").toInt();if(i>=0&&i<NUM_PRESETS){staticColor=presetColors[i];effectMode=4;saveSettings();}}server.sendHeader("Location","/",true);server.send(302,"text/plain","");}
void handleCustomColor(){
  if(!checkAuth()) return;
  if(server.hasArg("r")&&server.hasArg("g")&&server.hasArg("b")){
    staticColor=CRGB(constrain(server.arg("r").toInt(),0,255),constrain(server.arg("g").toInt(),0,255),constrain(server.arg("b").toInt(),0,255));
    effectMode=4;saveSettings();
  }
  server.send(200,"text/plain","OK");
}
void handleBrightness(){if(!checkAuth()) return;if(server.hasArg("value")){brightness=constrain(server.arg("value").toInt(),5,255);FastLED.setBrightness(brightness);saveSettings();}server.send(200,"text/plain","OK");}
void handleSpeed(){if(!checkAuth()) return;if(server.hasArg("value")){scrollSpeed=constrain(server.arg("value").toInt(),50,400);saveSettings();}server.send(200,"text/plain","OK");}

void handleText(){
  if(!checkAuth()) return;
  if(server.hasArg("scrollText")){String t=server.arg("scrollText");t.trim();
  if(t.length()>255) t=t.substring(0,255);if(t.length()==0) t="HELLO";
  t.toCharArray(scrollText,256);textPosition=0;parseScrollText();saveSettings();}
  server.sendHeader("Location","/",true);server.send(302,"text/plain","");
}

void handleWeatherCfg(){
  if(!checkAuth()) return;
  if(server.hasArg("apikey")){apiKey=server.arg("apikey");apiKey.trim();}
  if(server.hasArg("city")){String c=server.arg("city");
  if(c=="custom"&&server.hasArg("customCity")){c=server.arg("customCity");c.trim();}
  if(c.length()>0&&c!="custom") cityName=c;}
  saveWeatherCfg();if(apiKey.length()>0&&wifiConnected) fetchWeather();saveSettings();
  server.sendHeader("Location","/",true);server.send(302,"text/plain","");
}

void handleSettingsPost(){
  if(!checkAuth()) return;
  if(server.hasArg("tz")){
    utcOffset=constrain(server.arg("tz").toInt(),-12,14);
    timeClient.setTimeOffset(utcOffset*3600);
    timeClient.forceUpdate();
  }
  if(server.hasArg("clockSec")) clockShowSec=constrain(server.arg("clockSec").toInt(),3,120);
  nightModeEnabled=server.hasArg("nightOn");
  if(server.hasArg("nightStart")) nightStart=constrain(server.arg("nightStart").toInt(),0,23);
  if(server.hasArg("nightEnd")) nightEnd=constrain(server.arg("nightEnd").toInt(),0,23);
  if(server.hasArg("nightBri")) nightBrightness=constrain(server.arg("nightBri").toInt(),1,50);
  if(server.hasArg("adminPw")){adminPassword=server.arg("adminPw");adminPassword.trim();}
  saveSettings();
  server.sendHeader("Location","/",true);server.send(302,"text/plain","");
}

void handleReset(){
  if(!checkAuth()) return;
  effectMode=0;staticColor=CRGB(255,0,255);brightness=50;displayMode=0;
  strcpy(scrollText,"HELLO");textPosition=0;scrollSpeed=150;clockShowSec=10;
  nightModeEnabled=false;adminPassword="";
  currentPhase=SHOW_CLOCK;FastLED.setBrightness(brightness);saveSettings();
  server.sendHeader("Location","/",true);server.send(302,"text/plain","");
}
void handleResetWiFi(){
  if(!checkAuth()) return;
  clearWiFi();
  server.send(200,"text/html",F("<!DOCTYPE html><html><head><meta charset='UTF-8'><style>body{font-family:-apple-system,sans-serif;background:#080810;color:#e0e0f0;display:flex;align-items:center;justify-content:center;min-height:100vh}.c{background:#12122a;border:1px solid #1e1e3a;border-radius:20px;padding:40px;text-align:center}h2{color:#ff4757}</style></head><body><div class='c'><h2>WiFi сброшен</h2><p style='color:#666680;margin-top:12px'>Перезагрузка в AP режим...</p></div></body></html>"));
  delay(1500);ESP.restart();
}

// API для отправки текста на матрицу извне
void handleApiText(){
  if(server.hasArg("msg")){
    String msg=server.arg("msg");msg.trim();
    if(msg.length()>0&&msg.length()<=255){
      msg.toCharArray(scrollText,256);textPosition=0;
      displayMode=2;parseScrollText();saveSettings();
      server.send(200,"application/json","{\"ok\":true}");
      return;
    }
  }
  server.send(400,"application/json","{\"ok\":false}");
}

// JSON API статус
void handleApiStatus(){
  String j="{\"mode\":"+String(displayMode)+",\"effect\":"+String(effectMode)+
    ",\"brightness\":"+String(brightness)+",\"speed\":"+String(scrollSpeed)+
    ",\"weather\":\""+String(weatherText)+"\",\"text\":\""+String(scrollText)+
    ",\"uptime\":"+String(millis()/1000)+",\"heap\":"+String(ESP.getFreeHeap())+
    ",\"ip\":\""+WiFi.localIP().toString()+"\",\"night\":"+String(isNightNow?"true":"false")+"}";
  server.send(200,"application/json",j);
}


// ============================================================================
//   Маршруты
// ============================================================================
void setupRoutes(){
  server.on("/",handleRoot);
  server.on("/dispmode",HTTP_GET,handleDispMode);
  server.on("/set",HTTP_GET,handleSet);
  server.on("/preset",HTTP_GET,handlePreset);
  server.on("/custom",HTTP_POST,handleCustomColor);
  server.on("/customcolor",HTTP_GET,handleCustomColor);
  server.on("/brightness",HTTP_GET,handleBrightness);
  server.on("/speed",HTTP_GET,handleSpeed);
  server.on("/text",HTTP_POST,handleText);
  server.on("/weather",HTTP_POST,handleWeatherCfg);
  server.on("/settings",HTTP_POST,handleSettingsPost);
  server.on("/reset",HTTP_GET,handleReset);
  server.on("/resetwifi",HTTP_GET,handleResetWiFi);
  server.on("/scan",HTTP_GET,handleScanNets);
  server.on("/save",HTTP_POST,handleSaveWiFi);
  server.on("/favicon.ico",handleFavicon);
  // API
  server.on("/api/text",HTTP_GET,handleApiText);
  server.on("/api/status",HTTP_GET,handleApiStatus);
  // Captive portal
  server.on("/hotspot-detect.html",handleHotspotDetect);
  server.on("/library/test/success.html",handleHotspotDetect);
  server.on("/generate_204",handleGenerate204);
  server.on("/gen_204",handleGenerate204);
  server.on("/ncsi.txt",handleGenerate204);
  server.on("/connecttest.txt",handleGenerate204);
  server.on("/redirect",handleGenerate204);
  server.on("/fwlink",handleGenerate204);
  server.on("/canonical.html",handleGenerate204);
  server.on("/success.txt",handleGenerate204);
  server.onNotFound(handleNotFound);
  server.begin();Serial.println("HTTP OK");
}


// ============================================================================
//   WiFi / AP
// ============================================================================
void startAP(){
  sysMode=SYS_SETUP;
  WiFi.mode(WIFI_AP);WiFi.softAP(AP_SSID);delay(100);
  IPAddress ip(192,168,4,1);
  WiFi.softAPConfig(ip,ip,IPAddress(255,255,255,0));
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT,"*",ip);
  Serial.println("AP: "+String(AP_SSID));
  for(int i=0;i<3;i++){fill_solid(leds,NUM_LEDS,CRGB(108,92,231));FastLED.show();delay(200);fill_solid(leds,NUM_LEDS,CRGB::Black);FastLED.show();delay(200);}
}

bool connectWiFi(){
  if(wifiSSID.length()==0) return false;
  Serial.print("WiFi: "+wifiSSID);
  WiFi.mode(WIFI_STA);WiFi.begin(wifiSSID.c_str(),wifiPassword.c_str());
  int t=0;
  while(WiFi.status()!=WL_CONNECTED&&t<30){delay(500);Serial.print(".");
    fill_solid(leds,NUM_LEDS,CRGB::Black);
    int i=xyToIndex(t%MATRIX_WIDTH,3);if(i>=0) leds[i]=CRGB(108,92,231);
    FastLED.show();t++;}
  if(WiFi.status()==WL_CONNECTED){
    Serial.println("\nOK! IP: "+WiFi.localIP().toString());
    fill_solid(leds,NUM_LEDS,CRGB(0,80,0));FastLED.show();delay(400);
    fill_solid(leds,NUM_LEDS,CRGB::Black);FastLED.show();return true;}
  Serial.println("\nFAIL");return false;
}


// ============================================================================
//   SETUP
// ============================================================================
void setup(){
  Serial.begin(115200);delay(100);
  Serial.println("\n=== Time32 " FW_VERSION " ===");
  pinMode(RESET_PIN,INPUT_PULLUP);
  FastLED.addLeds<WS2812,LED_PIN,COLOR_ORDER>(leds,NUM_LEDS);
  FastLED.setBrightness(brightness);
  fill_solid(leds,NUM_LEDS,CRGB::Black);FastLED.show();
  loadSettings();
  FastLED.setBrightness(brightness);
  timeClient.setTimeOffset(utcOffset*3600);

  // Double-Reset Detection
  bool doReset=false;
  uint8_t rstCnt=EEPROM.read(EE_RSTCNT);
  if(rstCnt>=1){doReset=true;EEPROM.write(EE_RSTCNT,0);EEPROM.commit();Serial.println("DOUBLE-RESET");}
  else{EEPROM.write(EE_RSTCNT,1);EEPROM.commit();
    leds[xyToIndex(8,4)]=CRGB(80,80,0);FastLED.show();delay(3000);
    EEPROM.write(EE_RSTCNT,0);EEPROM.commit();fill_solid(leds,NUM_LEDS,CRGB::Black);FastLED.show();}
  if(digitalRead(RESET_PIN)==LOW){doReset=true;Serial.println("FLASH held");}
  if(doReset){clearWiFi();
    for(int i=0;i<3;i++){fill_solid(leds,NUM_LEDS,CRGB(255,0,0));FastLED.show();delay(200);fill_solid(leds,NUM_LEDS,CRGB::Black);FastLED.show();delay(200);}}
  if(wifiSSID.length()>0&&!doReset) wifiConnected=connectWiFi();
  if(!wifiConnected) startAP();
  else{timeClient.begin();timeClient.forceUpdate();sysMode=SYS_RUNNING;
    if(displayMode>=1&&apiKey.length()>0) fetchWeather();}
  parseScrollText();if(strlen(weatherText)>0) parseWeatherText();
  currentPhase=SHOW_CLOCK;phaseStartTime=millis();
  setupRoutes();randomSeed(analogRead(0));
  Serial.println("=== Ready ===");
}


// ============================================================================
//   LOOP
// ============================================================================
void loop(){
  if(sysMode==SYS_SETUP) dnsServer.processNextRequest();
  server.handleClient();
  if(wifiConnected) timeClient.update();

  // FLASH long-press
  static unsigned long fps=0;static bool fwp=false;
  if(digitalRead(RESET_PIN)==LOW){
    if(!fwp){fwp=true;fps=millis();}
    else if(millis()-fps>5000){clearWiFi();
      for(int i=0;i<5;i++){fill_solid(leds,NUM_LEDS,CRGB(255,0,0));FastLED.show();delay(150);fill_solid(leds,NUM_LEDS,CRGB::Black);FastLED.show();delay(150);}
      ESP.restart();}
  } else fwp=false;

  // Ночной режим
  static unsigned long lastNightCheck=0;
  if(millis()-lastNightCheck>30000){lastNightCheck=millis();checkNightMode();}

  // Погода
  if(wifiConnected&&apiKey.length()>0&&displayMode>=1&&(millis()-lastWeatherUpdate>WEATHER_UPDATE_MS)) fetchWeather();

  unsigned long now=millis();
  unsigned long clockShowMs=(unsigned long)clockShowSec*1000UL;

  if(sysMode==SYS_SETUP){
    static uint8_t bv=0;static int8_t bd=1;static unsigned long lb=0;
    if(now-lb>30){lb=now;fill_solid(leds,NUM_LEDS,CRGB::Black);
      bv+=bd*3;if(bv>120) bd=-1;if(bv<5) bd=1;
      drawGlyph(0,2,1,CRGB(bv/2,0,bv));drawGlyph(15,7,1,CRGB(bv/2,0,bv));}
  } else if(displayMode==0){
    if(wifiConnected) displayTime(timeClient.getHours(),timeClient.getMinutes(),timeClient.getSeconds());
    else displayTime(0,0,now/1000);
  } else {
    switch(currentPhase){
      case SHOW_CLOCK:
        if(wifiConnected) displayTime(timeClient.getHours(),timeClient.getMinutes(),timeClient.getSeconds());
        else displayTime(0,0,now/1000);
        if(now-phaseStartTime>clockShowMs){currentPhase=SHOW_WEATHER;phaseStartTime=now;weatherTextPosition=0;}
        break;
      case SHOW_WEATHER:
        if(parsedWeatherCount>0){
          if(now-lastScrollTime>(unsigned long)scrollSpeed){lastScrollTime=now;
            bool done=displayScrolling(parsedWeather,parsedWeatherCount,totalWeatherWidth,weatherTextPosition);
            weatherTextPosition++;
            if(done){if(displayMode==2){currentPhase=SHOW_TEXT;phaseStartTime=now;textPosition=0;}
              else{currentPhase=SHOW_CLOCK;phaseStartTime=now;}}}
        } else{fill_solid(leds,NUM_LEDS,CRGB::Black);
          drawGlyph(GLYPH_MINUS,4,1,CRGB(60,60,60));drawGlyph(GLYPH_MINUS,7,1,CRGB(60,60,60));drawGlyph(GLYPH_MINUS,10,1,CRGB(60,60,60));
          if(now-phaseStartTime>3000){if(displayMode==2){currentPhase=SHOW_TEXT;phaseStartTime=now;textPosition=0;}
            else{currentPhase=SHOW_CLOCK;phaseStartTime=now;}}}
        break;
      case SHOW_TEXT:
        if(parsedTextCount>0){
          if(now-lastScrollTime>(unsigned long)scrollSpeed){lastScrollTime=now;
            bool done=displayScrolling(parsedText,parsedTextCount,totalTextWidth,textPosition);
            textPosition++;if(done){currentPhase=SHOW_CLOCK;phaseStartTime=now;}}
        } else{currentPhase=SHOW_CLOCK;phaseStartTime=now;}
        break;
    }
  }
  hue++;FastLED.show();delay(20);
}
