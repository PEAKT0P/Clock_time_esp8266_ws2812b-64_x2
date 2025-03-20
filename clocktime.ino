// ^-^
// Ниже настрой wifi
// Powered by denjik.ru
///////////////////
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <FastLED.h>
#include <EEPROM.h>

// Определение констант для светодиодной матрицы
#define NUM_LEDS 128
// my NodeMCU 1.0 (ESP-12E Module) D6 - PIN 12
#define PIN 12
#define COLOR_ORDER GRB

// Определение ширины и высоты общего дисплея
#define MATRIX_WIDTH 16
#define MATRIX_HEIGHT 8

// Дополнительные константы для EEPROM
#define EEPROM_SIZE 512 // Увеличен размер для хранения текста
#define EEPROM_EFFECT_MODE 0
#define EEPROM_COLOR_R 1
#define EEPROM_COLOR_G 2
#define EEPROM_COLOR_B 3
#define EEPROM_BRIGHTNESS 4
#define EEPROM_INITIALIZED 5
#define EEPROM_TEXT_MODE 6
#define EEPROM_TEXT_LENGTH 7
#define EEPROM_TEXT_START 8

CRGB leds[NUM_LEDS];
const char* ssid = "u_SSID";
const char* password = "u_pass";

ESP8266WebServer server(80);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3 * 3600, 60000);

// Настройки эффектов и цветов
uint8_t brightness = 50;
uint8_t hue = 0; // Начальный оттенок для радужной анимации
uint8_t effectMode = 0; // Режим эффекта (0-5)
CRGB staticColor = CRGB(255, 0, 255); // Фиолетовый по умолчанию для статического режима

// Настройки бегущей строки
bool textMode = false; // Режим отображения бегущей строки (false - часы, true - текст)
char scrollText[256] = "HELLO"; // Буфер для хранения текста бегущей строки
int textPosition = 0; // Текущая позиция текста
unsigned long lastScrollTime = 0; // Последнее время прокрутки
int scrollSpeed = 150; // Скорость прокрутки в мс

// Массив предустановленных цветов
const CRGB presetColors[] = {
  CRGB(255, 0, 255),   // Фиолетовый
  CRGB(128, 0, 128),   // Темно-фиолетовый
  CRGB(148, 0, 211),   // Темно-орхидея
  CRGB(138, 43, 226),  // Сине-фиолетовый
  CRGB(186, 85, 211),  // Средняя орхидея
  CRGB(255, 0, 0),     // Красный
  CRGB(0, 255, 0),     // Зеленый
  CRGB(0, 0, 255),     // Синий
  CRGB(255, 255, 0),   // Желтый
  CRGB(0, 255, 255),   // Бирюзовый
  CRGB(255, 165, 0),   // Оранжевый
  CRGB(255, 255, 255)  // Белый
};

const uint8_t font5x3[10][5] = {
  {0b111, 0b101, 0b101, 0b101, 0b111},  // 0
  {0b010, 0b110, 0b010, 0b010, 0b111},  // 1
  {0b111, 0b001, 0b111, 0b100, 0b111},  // 2
  {0b111, 0b001, 0b111, 0b001, 0b111},  // 3
  {0b101, 0b101, 0b111, 0b001, 0b001},  // 4
  {0b111, 0b100, 0b111, 0b001, 0b111},  // 5
  {0b111, 0b100, 0b111, 0b101, 0b111},  // 6
  {0b111, 0b001, 0b001, 0b010, 0b010},  // 7
  {0b111, 0b101, 0b111, 0b101, 0b111},  // 8
  {0b111, 0b101, 0b111, 0b001, 0b111}   // 9
};

// Определение матриц для букв кириллицы и латиницы 5x3
const uint8_t lettersFont[59][5] = {
  // Латинские буквы (A-Z) - индексы 0-25
  {0b010, 0b101, 0b111, 0b101, 0b101},  // A
  {0b110, 0b101, 0b110, 0b101, 0b110},  // B
  {0b111, 0b100, 0b100, 0b100, 0b111},  // C
  {0b110, 0b101, 0b101, 0b101, 0b110},  // D
  {0b111, 0b100, 0b111, 0b100, 0b111},  // E
  {0b111, 0b100, 0b111, 0b100, 0b100},  // F
  {0b111, 0b100, 0b101, 0b101, 0b111},  // G
  {0b101, 0b101, 0b111, 0b101, 0b101},  // H
  {0b111, 0b010, 0b010, 0b010, 0b111},  // I
  {0b001, 0b001, 0b001, 0b101, 0b111},  // J
  {0b101, 0b101, 0b110, 0b101, 0b101},  // K
  {0b100, 0b100, 0b100, 0b100, 0b111},  // L
  {0b101, 0b111, 0b111, 0b101, 0b101},  // M
  {0b101, 0b111, 0b111, 0b101, 0b101},  // N
  {0b111, 0b101, 0b101, 0b101, 0b111},  // O
  {0b111, 0b101, 0b111, 0b100, 0b100},  // P
  {0b111, 0b101, 0b101, 0b111, 0b001},  // Q
  {0b111, 0b101, 0b110, 0b101, 0b101},  // R
  {0b111, 0b100, 0b111, 0b001, 0b111},  // S
  {0b111, 0b010, 0b010, 0b010, 0b010},  // T
  {0b101, 0b101, 0b101, 0b101, 0b111},  // U
  {0b101, 0b101, 0b101, 0b101, 0b010},  // V
  {0b101, 0b101, 0b111, 0b111, 0b101},  // W
  {0b101, 0b101, 0b010, 0b101, 0b101},  // X
  {0b101, 0b101, 0b010, 0b010, 0b010},  // Y
  {0b111, 0b001, 0b010, 0b100, 0b111},  // Z
  
  // Кириллица (А-Я) - индексы 26-58
  {0b010, 0b101, 0b111, 0b101, 0b101},  // А
  {0b110, 0b101, 0b110, 0b101, 0b110},  // Б
  {0b111, 0b101, 0b111, 0b101, 0b111},  // В
  {0b111, 0b100, 0b100, 0b100, 0b100},  // Г
  {0b101, 0b101, 0b111, 0b101, 0b101},  // Д
  {0b111, 0b100, 0b111, 0b100, 0b111},  // Е
  {0b101, 0b010, 0b010, 0b010, 0b101},  // Ж
  {0b111, 0b001, 0b111, 0b100, 0b111},  // З
  {0b101, 0b101, 0b111, 0b101, 0b101},  // И
  {0b111, 0b001, 0b111, 0b001, 0b111},  // К
  {0b101, 0b101, 0b010, 0b101, 0b101},  // Л
  {0b101, 0b111, 0b101, 0b101, 0b101},  // М
  {0b101, 0b101, 0b101, 0b101, 0b111},  // Н
  {0b111, 0b101, 0b101, 0b101, 0b111},  // О
  {0b111, 0b101, 0b111, 0b100, 0b100},  // П
  {0b111, 0b101, 0b111, 0b100, 0b100},  // Р
  {0b111, 0b100, 0b100, 0b100, 0b100},  // С
  {0b111, 0b010, 0b010, 0b010, 0b010},  // Т
  {0b101, 0b101, 0b010, 0b010, 0b010},  // У
  {0b111, 0b101, 0b111, 0b101, 0b101},  // Ф
  {0b101, 0b101, 0b010, 0b101, 0b101},  // Х
  {0b101, 0b101, 0b111, 0b001, 0b111},  // Ц
  {0b101, 0b101, 0b111, 0b001, 0b001},  // Ч
  {0b101, 0b101, 0b111, 0b101, 0b101},  // Ш
  {0b101, 0b101, 0b111, 0b001, 0b111},  // Щ
  {0b100, 0b100, 0b111, 0b101, 0b111},  // Ъ
  {0b101, 0b101, 0b111, 0b101, 0b101},  // Ы
  {0b100, 0b100, 0b111, 0b101, 0b111},  // Ь
  {0b111, 0b001, 0b111, 0b001, 0b111},  // Э
  {0b101, 0b101, 0b111, 0b001, 0b111},  // Ю
  {0b101, 0b101, 0b111, 0b001, 0b001},  // Я
  
  // Специальные символы
  {0b000, 0b000, 0b000, 0b000, 0b111},  // _ (подчеркивание)
  {0b000, 0b000, 0b000, 0b000, 0b000}   // space (пробел)
};

void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  
  // Инициализация FastLED
  FastLED.addLeds<WS2812, PIN, COLOR_ORDER>(leds, NUM_LEDS);
  
  // Загрузка настроек из EEPROM
  loadSettingsFromEEPROM();
  
  // Установка яркости
  FastLED.setBrightness(brightness);
  
  // Установка начальной позиции для бегущей строки
  textPosition = -MATRIX_WIDTH;
  
  // Подключение к WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  
  // Ждем подключения к WiFi с таймаутом
  int wifiTimeout = 0;
  while (WiFi.status() != WL_CONNECTED && wifiTimeout < 20) {
    delay(500);
    Serial.print(".");
    wifiTimeout++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    
    // Инициализация NTP клиента
    timeClient.begin();
    timeClient.update();
  } else {
    Serial.println("");
    Serial.println("WiFi connection failed");
  }
  
  // Настройка сервера
  setupServer();
  
  // Проверка текста для бегущей строки
  if (strlen(scrollText) == 0) {
    strcpy(scrollText, "privet");
  }
  
  // Инициализация случайного зерна для эффектов
  randomSeed(analogRead(0));
  
  // Индикация запуска
  fill_solid(leds, NUM_LEDS, CRGB::Blue);
  FastLED.show();
  delay(500);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
}

void handleReset() {
  // Очистка всех настроек EEPROM
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0);
  }
  
  // Сброс метки инициализации
  EEPROM.write(EEPROM_INITIALIZED, 0);
  EEPROM.commit();
  
  // Установка настроек по умолчанию
  effectMode = 0;
  staticColor = CRGB(255, 0, 255);
  brightness = 50;
  textMode = false;
  strcpy(scrollText, "HELLO");
  textPosition = -MATRIX_WIDTH;
  
  FastLED.setBrightness(brightness);
  
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void loadSettingsFromEEPROM() {
  // Проверяем инициализацию EEPROM
  if (EEPROM.read(EEPROM_INITIALIZED) != 42) {
    // Первый запуск - инициализируем значения по умолчанию
    EEPROM.write(EEPROM_EFFECT_MODE, effectMode);
    EEPROM.write(EEPROM_COLOR_R, staticColor.r);
    EEPROM.write(EEPROM_COLOR_G, staticColor.g);
    EEPROM.write(EEPROM_COLOR_B, staticColor.b);
    EEPROM.write(EEPROM_BRIGHTNESS, brightness);
    EEPROM.write(EEPROM_TEXT_MODE, textMode);
    EEPROM.write(EEPROM_TEXT_LENGTH, strlen(scrollText));
    
    // Сохраняем текст по умолчанию
    for(int i = 0; i < strlen(scrollText); i++) {
      EEPROM.write(EEPROM_TEXT_START + i, scrollText[i]);
    }
    
    EEPROM.write(EEPROM_INITIALIZED, 42); // Метка инициализации
    EEPROM.commit();
  } else {
    // Загружаем сохраненные настройки
    effectMode = EEPROM.read(EEPROM_EFFECT_MODE);
    staticColor.r = EEPROM.read(EEPROM_COLOR_R);
    staticColor.g = EEPROM.read(EEPROM_COLOR_G);
    staticColor.b = EEPROM.read(EEPROM_COLOR_B);
    brightness = EEPROM.read(EEPROM_BRIGHTNESS);
    textMode = EEPROM.read(EEPROM_TEXT_MODE);
    
    // Загружаем текст для бегущей строки
    uint8_t textLength = EEPROM.read(EEPROM_TEXT_LENGTH);
    if(textLength < 256 && textLength > 0) { // Проверка на разумный размер
      for(int i = 0; i < textLength; i++) {
        scrollText[i] = EEPROM.read(EEPROM_TEXT_START + i);
      }
      scrollText[textLength] = '\0'; // Завершающий нуль-символ
    } else {
      // Если длина текста некорректная, устанавливаем значение по умолчанию
      strcpy(scrollText, "HELLO");
    }
  }
  
  // Инициализация позиции текста
  textPosition = -MATRIX_WIDTH;
}

void saveSettingsToEEPROM() {
  EEPROM.write(EEPROM_EFFECT_MODE, effectMode);
  EEPROM.write(EEPROM_COLOR_R, staticColor.r);
  EEPROM.write(EEPROM_COLOR_G, staticColor.g);
  EEPROM.write(EEPROM_COLOR_B, staticColor.b);
  EEPROM.write(EEPROM_BRIGHTNESS, brightness);
  EEPROM.write(EEPROM_TEXT_MODE, textMode);
  EEPROM.write(EEPROM_TEXT_LENGTH, strlen(scrollText));
  
  // Сохраняем текст бегущей строки
  for(int i = 0; i < strlen(scrollText); i++) {
    EEPROM.write(EEPROM_TEXT_START + i, scrollText[i]);
  }
  
  EEPROM.commit();
}

void setupServer() {
  // Маршруты для веб-интерфейса
  server.on("/", handleRoot);
  server.on("/set", HTTP_GET, handleSet);
  server.on("/preset", HTTP_GET, handlePreset);
  server.on("/custom", HTTP_POST, handleCustomColor);
  server.on("/brightness", HTTP_GET, handleBrightness);
  server.on("/reset", HTTP_GET, handleReset);
  // Новые маршруты для управления бегущей строкой
  server.on("/text", HTTP_POST, handleText);
  server.on("/textmode", HTTP_GET, handleTextMode);
  
  // Запуск сервера
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient(); // Обработка запросов к веб-серверу
  timeClient.update();

  // Проверяем режим отображения
  if (textMode) {
    // Режим бегущей строки
    unsigned long currentTime = millis();
    if (currentTime - lastScrollTime > scrollSpeed) {
      lastScrollTime = currentTime;
      displayScrollingText();
      textPosition++;
      
      // Если текст полностью скрылся за левым краем, начинаем сначала
      int textLength = strlen(scrollText);
      if (textLength > 0) {
        // Проверяем, полностью ли строка ушла за экран
        if (textPosition / 4 > textLength) {
          textPosition = -MATRIX_WIDTH;
        }
      }
    }
  } else {
    // Режим часов
    int hours = timeClient.getHours();
    int minutes = timeClient.getMinutes();
    int seconds = timeClient.getSeconds();
    
    displayTime(hours, minutes, seconds);
  }
  
  // Инкремент глобального hue для создания эффекта переливания
  hue++;
  
  // Автоматическая смена эффекта только если выбран режим хаоса (режим 3)
  if (effectMode == 3 && timeClient.getSeconds() % 30 == 0 && timeClient.getSeconds() != 0) {
    // Ничего не делаем, режим хаоса остается включенным
  }
  
  FastLED.show();
  delay(30); // Уменьшаем задержку для более плавной анимации
}

String getEffectModeName(uint8_t mode) {
  switch(mode) {
    case 0: return "Радужный";
    case 1: return "Пульсирующий";
    case 2: return "Волновой";
    case 3: return "Хаотичный";
    case 4: return "Статический";
    case 5: return "Чередование";
    default: return "Неизвестный";
  }
}

void handleRoot() {
  String html = "<!DOCTYPE html><html>";
  html += "<head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>LED Часы - Управление</title>";
  html += "<link rel='preconnect' href='https://fonts.googleapis.com'>";
  html += "<link rel='preconnect' href='https://fonts.gstatic.com' crossorigin>";
  html += "<link href='https://fonts.googleapis.com/css2?family=Roboto:wght@300;400;500;700&display=swap' rel='stylesheet'>";
  html += "<style>";
  html += "body{font-family:'Roboto',sans-serif;margin:0;padding:0;background:linear-gradient(135deg,#f5f7fa 0%,#c3cfe2 100%);min-height:100vh;color:#333;}";
  html += ".container{max-width:800px;margin:20px auto;background-color:rgba(255,255,255,0.95);padding:25px;border-radius:15px;box-shadow:0 5px 20px rgba(0,0,0,0.1);}";
  html += "h1{color:#1a365d;text-align:center;font-weight:500;margin-top:0;padding-bottom:15px;border-bottom:1px solid #eaeaea;}";
  html += "h2{color:#2c5282;font-size:1.4rem;margin-top:0;font-weight:500;}";
  html += "h3{color:#2b6cb0;font-size:1.1rem;margin-bottom:10px;}";
  html += ".section{margin:25px 0;padding:20px;background-color:#f8fafc;border-radius:10px;box-shadow:0 2px 5px rgba(0,0,0,0.05);transition:all 0.3s ease;}";
  html += ".section:hover{box-shadow:0 5px 15px rgba(0,0,0,0.1);}";
  html += ".text-mode-section{background-color:#e6f7ff;border-left:4px solid #3182ce;}";
  html += "button{border:none;padding:10px 16px;text-align:center;text-decoration:none;display:inline-block;font-size:16px;margin:4px 2px;cursor:pointer;border-radius:5px;font-weight:500;transition:all 0.2s ease;color:white;background-color:#4299e1;}";
  html += "button:hover{background-color:#3182ce;transform:translateY(-2px);box-shadow:0 4px 6px rgba(0,0,0,0.1);}";
  html += "button:active{transform:translateY(0);}";
  html += ".mode-btn{width:180px;margin:10px;padding:15px;font-size:18px;background-color:#4299e1;}";
  html += ".mode-btn:hover{background-color:#3182ce;}";
  html += ".mode-btn.current{background-color:#2b6cb0;box-shadow:inset 0 2px 4px rgba(0,0,0,0.1);}";
  html += ".effect-btn{width:150px;margin:6px;background-color:#4299e1;}";
  html += ".effect-btn.current{background-color:#2b6cb0;box-shadow:inset 0 2px 4px rgba(0,0,0,0.1);}";
  html += ".color-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(70px,1fr));gap:10px;margin:15px 0;}";
  html += ".color-btn{width:100%;height:45px;margin:0;border:none;border-radius:5px;cursor:pointer;transition:all 0.2s ease;}";
  html += ".color-btn:hover{transform:scale(1.05);box-shadow:0 2px 8px rgba(0,0,0,0.2);}";
  html += ".color-btn.current{border:3px solid #2c5282;box-shadow:0 0 0 2px white;}";
  html += ".brightness-container{display:flex;align-items:center;justify-content:center;margin:15px 0;flex-wrap:wrap;}";
  html += ".brightness-container label{margin-right:10px;font-weight:500;}";
  html += ".brightness-slider{width:100%;max-width:300px;margin:10px 0;}";
  html += "#brightness-value{min-width:40px;text-align:center;font-weight:bold;color:#2b6cb0;margin-left:15px;}";
  html += "input[type='range']{-webkit-appearance:none;height:10px;border-radius:5px;background:#d1d5db;outline:none;opacity:0.7;transition:opacity .2s;}";
  html += "input[type='range']:hover{opacity:1;}";
  html += "input[type='range']::-webkit-slider-thumb{-webkit-appearance:none;appearance:none;width:20px;height:20px;border-radius:50%;background:#3182ce;cursor:pointer;}";
  html += "input[type='range']::-moz-range-thumb{width:20px;height:20px;border-radius:50%;background:#3182ce;cursor:pointer;}";
  html += "textarea{width:100%;padding:12px;margin:10px 0;border-radius:5px;border:1px solid #cbd5e0;font-family:'Roboto',sans-serif;resize:vertical;box-shadow:inset 0 1px 2px rgba(0,0,0,0.1);transition:border-color 0.3s ease;}";
  html += "textarea:focus{border-color:#3182ce;outline:none;box-shadow:0 0 0 3px rgba(66,153,225,0.3);}";
  html += "input[type='number']{width:60px;padding:8px;border-radius:5px;border:1px solid #cbd5e0;font-family:'Roboto',sans-serif;margin:0 8px;box-shadow:inset 0 1px 2px rgba(0,0,0,0.1);}";
  html += "input[type='number']:focus{border-color:#3182ce;outline:none;box-shadow:0 0 0 3px rgba(66,153,225,0.3);}";
  html += ".custom-color{margin:20px 0;display:flex;flex-wrap:wrap;align-items:center;justify-content:center;}";
  html += ".custom-color form{display:flex;flex-wrap:wrap;align-items:center;justify-content:center;width:100%;}";
  html += ".custom-color label{margin:0 5px;font-weight:500;}";
  html += ".custom-color button{margin-left:15px;background-color:#4299e1;}";
  html += ".current-settings{display:flex;flex-wrap:wrap;justify-content:space-between;margin-top:15px;}";
  html += ".setting-item{background-color:#f0f9ff;padding:10px 15px;border-radius:8px;margin-bottom:10px;flex:1 0 200px;margin-right:10px;box-shadow:0 1px 3px rgba(0,0,0,0.05);}";
  html += ".setting-item strong{color:#2b6cb0;}";
  html += ".setting-item span.color-box{display:inline-block;width:20px;height:20px;margin-right:5px;border:1px solid #cbd5e0;border-radius:3px;vertical-align:middle;}";
  html += ".reset-btn{background-color:#e53e3e;margin-top:10px;width:100%;max-width:300px;}";
  html += ".reset-btn:hover{background-color:#c53030;}";
  html += ".btn-group{display:flex;flex-wrap:wrap;justify-content:center;}";
  html += "form{margin:0;}";
  html += "@media(max-width:600px){.container{margin:10px;padding:15px;} .mode-btn,.effect-btn{width:100%;margin:5px 0;} .custom-color{flex-direction:column;} .custom-color form{flex-direction:column;} .custom-color input{margin:5px 0;} .setting-item{flex:1 0 100%;margin-right:0;}}";
  html += "</style>";
  html += "</head>";
  html += "<body>";
  html += "<div class='container'>";
  html += "<h1>LED Часы - Управление</h1>";
  
  // Переключение между режимами часов и бегущей строки
  html += "<div class='section text-mode-section'>";
  html += "<h2>Режим отображения</h2>";
  html += "<div class='btn-group'>";
  html += "<button class='mode-btn " + String(textMode ? "" : "current") + "' onclick='location.href=\"/textmode?mode=0\"'>Режим часов</button>";
  html += "<button class='mode-btn " + String(textMode ? "current" : "") + "' onclick='location.href=\"/textmode?mode=1\"'>Бегущая строка</button>";
  html += "</div>";
  html += "</div>";
  
  // Форма для ввода текста бегущей строки
  html += "<div class='section' id='text-section' style='display:" + String(textMode ? "block" : "none") + ";'>";
  html += "<h2>Настройка бегущей строки</h2>";
  html += "<form action='/text' method='post'>";
  html += "<label for='scrollText'>Введите текст для отображения:</label><br>";
  html += "<textarea id='scrollText' name='scrollText' rows='3' maxlength='255' placeholder='Введите текст для отображения на LED дисплее'>" + String(scrollText) + "</textarea><br>";
  html += "<button type='submit'>Применить</button>";
  html += "</form>";
  html += "</div>";
  
  // Текущие настройки
  html += "<div class='section'>";
  html += "<h2>Текущие настройки</h2>";
  html += "<div class='current-settings'>";
  html += "<div class='setting-item'><p>Режим эффекта: <strong>" + getEffectModeName(effectMode) + "</strong></p></div>";
  if (effectMode == 4) {
    html += "<div class='setting-item'><p>Текущий цвет: <span class='color-box' style='background-color:rgb(" + 
            String(staticColor.r) + "," + String(staticColor.g) + "," + String(staticColor.b) + 
            ");'></span> RGB(" + 
            String(staticColor.r) + "," + String(staticColor.g) + "," + String(staticColor.b) + ")</p></div>";
  }
  html += "<div class='setting-item'><p>Яркость: <strong>" + String(brightness) + "</strong></p></div>";
  if (textMode) {
    html += "<div class='setting-item' style='flex:1 0 100%;'><p>Текущий текст: <strong>" + String(scrollText) + "</strong></p></div>";
  }
  html += "</div>";
  html += "</div>";
  
  // Выбор режима эффекта
  html += "<div class='section'>";
  html += "<h2>Режим эффекта</h2>";
  html += "<div class='btn-group'>";
  html += "<button class='effect-btn " + String(effectMode == 0 ? "current" : "") + "' onclick='location.href=\"/set?mode=0\"'>Радужный</button>";
  html += "<button class='effect-btn " + String(effectMode == 1 ? "current" : "") + "' onclick='location.href=\"/set?mode=1\"'>Пульсирующий</button>";
  html += "<button class='effect-btn " + String(effectMode == 2 ? "current" : "") + "' onclick='location.href=\"/set?mode=2\"'>Волновой</button>";
  html += "<button class='effect-btn " + String(effectMode == 3 ? "current" : "") + "' onclick='location.href=\"/set?mode=3\"'>Хаотичный</button>";
  html += "<button class='effect-btn " + String(effectMode == 4 ? "current" : "") + "' onclick='location.href=\"/set?mode=4\"'>Статический</button>";
  html += "<button class='effect-btn " + String(effectMode == 5 ? "current" : "") + "' onclick='location.href=\"/set?mode=5\"'>Чередование</button>";
  html += "</div>";
  html += "</div>";
  
  // Настройка яркости
  html += "<div class='section'>";
  html += "<h2>Яркость</h2>";
  html += "<div class='brightness-container'>";
  html += "<label for='brightness'>Уровень яркости:</label>";
  html += "<div class='brightness-slider'>";
  html += "<input type='range' id='brightness' name='brightness' min='10' max='255' value='" + String(brightness) + "' oninput='updateBrightnessValue(this.value)' onchange='updateBrightness(this.value)'>";
  html += "</div>";
  html += "<span id='brightness-value'>" + String(brightness) + "</span>";
  html += "</div>";
  html += "</div>";
  
  // Предустановленные цвета (только для статического режима)
  html += "<div class='section' id='color-section' style='display:" + String(effectMode == 4 ? "block" : "none") + ";'>";
  html += "<h2>Выбор цвета</h2>";
  html += "<h3>Предустановленные цвета</h3>";
  html += "<div class='color-grid'>";
  
  // Добавление предустановленных цветов
  for (int i = 0; i < sizeof(presetColors)/sizeof(presetColors[0]); i++) {
    String isCurrentColor = "";
    if (effectMode == 4 && staticColor.r == presetColors[i].r && staticColor.g == presetColors[i].g && staticColor.b == presetColors[i].b) {
      isCurrentColor = " current";
    }
    
    html += "<button class='color-btn" + isCurrentColor + "' style='background-color:rgb(" + 
            String(presetColors[i].r) + "," + String(presetColors[i].g) + "," + String(presetColors[i].b) + 
            ");' onclick='location.href=\"/preset?index=" + String(i) + "\"'></button>";
  }

  html += "</div>";

  // Пользовательский выбор цвета
  html += "<h3>Пользовательский цвет</h3>";
  html += "<div class='custom-color'>";
  html += "<form action='/custom' method='post'>";
  html += "<label for='r'>R:</label>";
  html += "<input type='number' id='r' name='r' min='0' max='255' value='" + String(staticColor.r) + "'>";
  html += "<label for='g'>G:</label>";
  html += "<input type='number' id='g' name='g' min='0' max='255' value='" + String(staticColor.g) + "'>";
  html += "<label for='b'>B:</label>";
  html += "<input type='number' id='b' name='b' min='0' max='255' value='" + String(staticColor.b) + "'>";
  html += "<button type='submit'>Применить</button>";
  html += "</form>";
  html += "</div>";
  html += "</div>";

  // Кнопка сброса EEPROM
  html += "<div class='section' style='text-align:center;'>";
  html += "<h2>Сброс настроек</h2>";
  html += "<button class='reset-btn' onclick='confirmReset()'>Сбросить все настройки</button>";
  html += "</div>";

  // JavaScript для обновления яркости и показа/скрытия разделов
  html += "<script>";
  html += "function updateBrightnessValue(val) {";
  html += "  document.getElementById('brightness-value').innerText = val;";
  html += "}";
  html += "function updateBrightness(val) {";
  html += "  fetch('/brightness?value=' + val);";
  html += "}";
  html += "function confirmReset() {";
  html += "  if(confirm('Вы уверены, что хотите сбросить все настройки?')) {";
  html += "    location.href='/reset';";
  html += "  }";
  html += "}";
  html += "document.addEventListener('DOMContentLoaded', function() {";
  html += "  const effectMode = " + String(effectMode) + ";";
  html += "  const textMode = " + String(textMode ? "true" : "false") + ";";
  html += "  const colorSection = document.getElementById('color-section');";
  html += "  const textSection = document.getElementById('text-section');";
  html += "  if (effectMode == 4) {";
  html += "    colorSection.style.display = 'block';";
  html += "  } else {";
  html += "    colorSection.style.display = 'none';";
  html += "  }";
  html += "  if (textMode) {";
  html += "    textSection.style.display = 'block';";
  html += "  } else {";
  html += "    textSection.style.display = 'none';";
  html += "  }";
  html += "});";
  html += "</script>";

  html += "</div>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

// Обработчик установки режима эффекта
void handleSet() {
  if (server.hasArg("mode")) {
    int mode = server.arg("mode").toInt();
    if (mode >= 0 && mode <= 5) {
      effectMode = mode;
      saveSettingsToEEPROM();
    }
  }
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

// Обработчик выбора предустановленного цвета
void handlePreset() {
  if (server.hasArg("index")) {
    int index = server.arg("index").toInt();
    if (index >= 0 && index < sizeof(presetColors)/sizeof(presetColors[0])) {
      staticColor = presetColors[index];
effectMode = 4; // Переключаем на статический режим
      saveSettingsToEEPROM();
    }
  }
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

// Обработчик пользовательского цвета
void handleCustomColor() {
  if (server.hasArg("r") && server.hasArg("g") && server.hasArg("b")) {
    int r = server.arg("r").toInt();
    int g = server.arg("g").toInt();
    int b = server.arg("b").toInt();
    
    // Проверка диапазона значений
    r = constrain(r, 0, 255);
    g = constrain(g, 0, 255);
    b = constrain(b, 0, 255);
    
    staticColor = CRGB(r, g, b);
    effectMode = 4; // Переключаемся на статический режим
    saveSettingsToEEPROM();
  }
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

// Обработчик изменения яркости
void handleBrightness() {
  if (server.hasArg("value")) {
    int value = server.arg("value").toInt();
    brightness = constrain(value, 10, 255);
    FastLED.setBrightness(brightness);
    saveSettingsToEEPROM();
  }
  server.send(200, "text/plain", "OK");
}

// Новый обработчик для установки текста бегущей строки
void handleText() {
  if (server.hasArg("scrollText")) {
    String newText = server.arg("scrollText");
    newText.trim();
    
    // Ограничиваем длину текста
    if (newText.length() > 255) {
      newText = newText.substring(0, 255);
    }
    
    // Проверяем, не пустой ли текст
    if (newText.length() == 0) {
      newText = "HELLO"; // Значение по умолчанию
    }
    
    // Копируем в глобальную переменную
    newText.toCharArray(scrollText, 256);
    
    // Сбрасываем позицию
    textPosition = -MATRIX_WIDTH;
    
    // Включаем режим бегущей строки
    textMode = true;
    
    saveSettingsToEEPROM();
  }
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

// Новый обработчик для переключения между режимами часов и бегущей строки
void handleTextMode() {
  if (server.hasArg("mode")) {
    int mode = server.arg("mode").toInt();
    textMode = (mode == 1);
    
    // Сбрасываем позицию текста при переключении в режим бегущей строки
    if (textMode) {
      textPosition = -MATRIX_WIDTH;
    }
    
    saveSettingsToEEPROM();
  }
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

// Получение цвета в зависимости от текущего режима эффекта
CRGB getEffectColor(int position, int digitIndex) {
  switch(effectMode) {
    case 0: // Радужный эффект - каждый символ свой цвет, но все плавно меняются
      return CHSV(hue + position * 25, 255, 255);
      
    case 1: // Пульсирующий эффект - все символы одного цвета, меняющегося по насыщенности
      return CHSV(hue, 190 + sin8(hue * 2 + position * 20) / 4, 255);
      
    case 2: // Волновой эффект - перемещающаяся волна через символы
      return CHSV(hue + sin8(hue * 2 + position * 30) / 5, 255, 255);
      
    case 3: // Разноцветный хаос - каждый символ и каждая точка случайно меняет цвет
      return CHSV(hue + digitIndex * 33 + random8(20), 200 + random8(55), 220 + random8(35));
      
    case 4: // Статический цвет
      return staticColor;
      
    case 5: // Чередование - чередование цветов радуги по буквам
      return CHSV((hue / 2) + (position * 45), 240, 255);
      
    default:
      return CRGB::White;
  }
}

void displayTime(int hours, int minutes, int seconds) {
  // Очищаем весь дисплей
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  
  char timeStr[6];
  sprintf(timeStr, "%02d:%02d", hours, minutes);
  
  // Новая позиция начала отображения - сдвинута на 1 пиксель влево
  int startX = (MATRIX_WIDTH - 17) / 2;  // Убрали +1 для сдвига влево
  
  // Центрирование по вертикали
  int startY = (MATRIX_HEIGHT - 5) / 2;  // 5 - высота шрифта
  
  // Отрисовка всех элементов времени в центре дисплея
  // Первая цифра часов
  drawChar(timeStr[0], startX, startY, getEffectColor(0, timeStr[0] - '0'));
  
  // Вторая цифра часов
  drawChar(timeStr[1], startX + 4, startY, getEffectColor(1, timeStr[1] - '0'));
  
  // Двоеточие - теперь мигает в зависимости от секунд
  if (seconds % 2 == 0) {
    drawColon(startX + 7, startY, getEffectColor(2, 10));
  }
  
  // Первая цифра минут (уменьшен отступ после двоеточия)
  drawChar(timeStr[3], startX + 8, startY, getEffectColor(3, timeStr[3] - '0'));
  
  // Вторая цифра минут
  drawChar(timeStr[4], startX + 12, startY, getEffectColor(4, timeStr[4] - '0'));
}

// Функция для отображения бегущей строки
void displayScrollingText() {
  // Очищаем весь дисплей
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  
  int textLen = strlen(scrollText);
  if (textLen == 0) return;
  
  // Проходим по всем символам строки, которые могут быть видны на экране
  for (int i = 0; i < MATRIX_WIDTH / 4 + 10; i++) {
    int charIndex = textPosition / 4 + i;
    
    // Отображаем только текст, который находится в пределах массива
    if (charIndex >= 0 && charIndex < textLen) {
      char currentChar = scrollText[charIndex];
      int x = i * 4 - (textPosition % 4);
      
      // Проверяем, что символ в пределах видимой области
      if (x >= -3 && x < MATRIX_WIDTH) {
        drawLetter(currentChar, x, 1, getEffectColor(i, i % 10));
      }
    }
  }
}

// Функция для отображения буквы (как цифры, но с поддержкой букв)
void drawLetter(char ch, int x, int y, CRGB baseColor) {
  int fontIndex = -1;
  
  // Определяем индекс в массиве шрифтов
  if (ch >= '0' && ch <= '9') {
    // Цифры
    drawChar(ch, x, y, baseColor);
    return;
  } else if (ch >= 'A' && ch <= 'Z') {
    // Латинские заглавные буквы
    fontIndex = ch - 'A';
  } else if (ch >= 'a' && ch <= 'z') {
    // Латинские строчные буквы (используем тот же шрифт, что и для заглавных)
    fontIndex = ch - 'a';
  } else if (ch >= 'А' && ch <= 'Я') {
    // Кириллица заглавные буквы
    fontIndex = ch - 'А' + 26;
  } else if (ch >= 'а' && ch <= 'я') {
    // Кириллица строчные буквы (используем тот же шрифт, что и для заглавных)
    fontIndex = ch - 'а' + 26;
  } else if (ch == ' ') {
    // Пробел
    fontIndex = 58; // Индекс последней буквы в массиве lettersFont
  } else if (ch == '_') {
    // Подчеркивание
    fontIndex = 57;
  } else {
    // Другие символы - отображаем пробел
    fontIndex = 58;
  }
  
  // Проверка на валидный индекс
  if (fontIndex >= 0 && fontIndex < 59) {
    // Рисуем символ по матрице шрифта
    for (int row = 0; row < 5; row++) {
      for (int col = 0; col < 3; col++) {
        if (lettersFont[fontIndex][row] & (1 << (2 - col))) {
          int index = xyToIndex(x + col, y + row);
          if (index >= 0 && index < NUM_LEDS) {
            // В режиме хаоса (режим 3) каждая точка буквы имеет свой оттенок
            if (effectMode == 3) {
              leds[index] = CHSV(hue + row*10 + col*25 + random8(30), 230 + random8(25), 255);
            } else {
              // Небольшие вариации цвета для точек внутри одной буквы для большего эффекта переливания
              uint8_t hueOffset = (row + col) * 5;
              CRGB color = baseColor;
              if (effectMode != 1 && effectMode != 4) { // Кроме режима пульсации и статического
                color.r = qadd8(color.r, sin8(hue + hueOffset) / 10);
                color.g = qadd8(color.g, sin8(hue + 85 + hueOffset) / 10);
                color.b = qadd8(color.b, sin8(hue + 170 + hueOffset) / 10);
              }
              leds[index] = color;
            }
          }
        }
      }
    }
  }
}

void drawChar(char ch, int x, int y, CRGB baseColor) {
  if (ch == ':') {  // Специальный случай для двоеточия
    drawColon(x, y, baseColor);
    return;
  }
  
  int num = ch - '0';
  if (num < 0 || num > 9) return;
  
  for (int row = 0; row < 5; row++) {
    for (int col = 0; col < 3; col++) {
      if (font5x3[num][row] & (1 << (2 - col))) {
        int index = xyToIndex(x + col, y + row);
        if (index >= 0 && index < NUM_LEDS) {
          // В режиме хаоса (режим 3) каждая точка цифры имеет свой оттенок
          if (effectMode == 3) {
            leds[index] = CHSV(hue + row*10 + col*25 + random8(30), 230 + random8(25), 255);
          } else {
            // Небольшие вариации цвета для точек внутри одной цифры для большего эффекта переливания
            uint8_t hueOffset = (row + col) * 5;
            CRGB color = baseColor;
            if (effectMode != 1 && effectMode != 4) { // Кроме режима пульсации и статического
              color.r = qadd8(color.r, sin8(hue + hueOffset) / 10);
              color.g = qadd8(color.g, sin8(hue + 85 + hueOffset) / 10);
              color.b = qadd8(color.b, sin8(hue + 170 + hueOffset) / 10);
            }
            leds[index] = color;
          }
        }
      }
    }
  }
}

// Выделил функцию для двоеточия, чтобы не дублировать код
void drawColon(int x, int y, CRGB baseColor) {
  // Проверяем, что координаты в пределах дисплея
  if (x >= 0 && x < MATRIX_WIDTH) {
    // Для двоеточия меняем цвет для каждой точки
    leds[xyToIndex(x, y + 1)] = baseColor;
    leds[xyToIndex(x, y + 3)] = baseColor;
  }
}

int xyToIndex(int x, int y) {
  // Преобразование координат (x,y) в индекс LED для двух матриц 8x8
  if (x < 0 || x >= MATRIX_WIDTH || y < 0 || y >= MATRIX_HEIGHT) 
    return -1;  // Проверка границ
  
  // Определяем, на какой матрице находится пиксель
  if (x < 8) {
    // Первая матрица (левая)
    return (y * 8) + x;
  } else {
    // Вторая матрица (правая)
    return 64 + (y * 8) + (x - 8);
  }
}
