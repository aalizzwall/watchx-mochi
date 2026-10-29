#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <avr/pgmspace.h>
#include <Adafruit_BLE.h>
#include <Adafruit_BluefruitLE_SPI.h>

// --- watchX 硬體腳位定義 ---
#define OLED_CS    A5
#define OLED_DC    A3
#define OLED_RESET A4

#define BLE_CS     A2
#define BLE_IRQ    0
#define BLE_RST    A1

#define SW1 8   // 按鍵 1 (待機時進 Menu，Menu 內為確認/切換)
#define SW2 11  // 按鍵 2 (Menu 內為「上」，待機時為戳一下)
#define SW3 10  // 按鍵 3 (Menu 內為「下」，待機時切換表情)

#define LED_LEFT   13  // 左腮紅 LED
#define LED_RIGHT  6   // 右腮紅 LED
#define BUZZER     9   // 蜂鳴器 PWM

#define BATTERY_POWER 4    // 電池分壓電路啟用腳位 (HIGH 啟用)
#define BATTERY_READ  A11  // 電池電壓類比讀取腳位
#define CHARGE_STATUS 5    // 充電狀態檢測腳位 (LOW 為充電中)

// 藍牙實例 (硬體 SPI)
Adafruit_BluefruitLE_SPI ble(BLE_CS, BLE_IRQ, BLE_RST);
bool bleHardwareReady = false;

// --- 自建 1KB 極簡 OLED 顯存 (大幅節省 12KB+ Flash) ---
uint8_t oled_buf[1024];

inline void drawPixel(int x, int y, uint8_t color) {
  if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
  if (color) oled_buf[x + (y >> 3) * 128] |= (1 << (y & 7));
  else       oled_buf[x + (y >> 3) * 128] &= ~(1 << (y & 7));
}

void clearDisplay() {
  memset(oled_buf, 0, 1024);
}

void drawFastHLine(int x, int y, int w, uint8_t color) {
  if (y < 0 || y >= 64 || x >= 128 || w <= 0) return;
  int x2 = min(128, x + w);
  for (int i = max(0, x); i < x2; i++) drawPixel(i, y, color);
}

void drawFastVLine(int x, int y, int h, uint8_t color) {
  if (x < 0 || x >= 128 || y >= 64 || h <= 0) return;
  int y2 = min(64, y + h);
  for (int i = max(0, y); i < y2; i++) drawPixel(x, i, color);
}

void fillRect(int x, int y, int w, int h, uint8_t color) {
  for (int i = 0; i < h; i++) drawFastHLine(x, y + i, w, color);
}

void fillRoundRect(int x, int y, int w, int h, int r, uint8_t color) {
  fillRect(x + r, y, w - 2 * r, h, color);
  for (int i = 0; i <= r; i++) {
    int dx = r - (int)sqrt((long)r * r - (long)(r - i) * (r - i));
    drawFastHLine(x + dx, y + i, w - 2 * dx, color);
    drawFastHLine(x + dx, y + h - 1 - i, w - 2 * dx, color);
  }
}

void fillCircle(int cx, int cy, int r, uint8_t color) {
  for (int dy = -r; dy <= r; dy++) {
    int dx = (int)sqrt((long)r * r - (long)dy * dy);
    drawFastHLine(cx - dx, cy + dy, 2 * dx + 1, color);
  }
}

void drawLine(int x0, int y0, int x1, int y1, uint8_t color) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy, e2;
  while (true) {
    drawPixel(x0, y0, color);
    if (x0 == x1 && y0 == y1) break;
    e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

// 5x7 極簡字庫 (ASCII 32 ' ' 到 93 ']')
static const uint8_t font5x7[] PROGMEM = {
  0x00,0x00,0x00,0x00,0x00, // 32 ' '
  0x00,0x00,0x5F,0x00,0x00, // 33 '!'
  0x00,0x07,0x00,0x07,0x00, // 34 '"'
  0x14,0x7F,0x14,0x7F,0x14, // 35 '#'
  0x24,0x2A,0x7F,0x2A,0x12, // 36 '$'
  0x23,0x13,0x08,0x64,0x62, // 37 '%'
  0x36,0x49,0x55,0x22,0x50, // 38 '&'
  0x00,0x05,0x03,0x00,0x00, // 39 '''
  0x00,0x1C,0x22,0x41,0x00, // 40 '('
  0x00,0x41,0x22,0x1C,0x00, // 41 ')'
  0x14,0x08,0x3E,0x08,0x14, // 42 '*'
  0x08,0x08,0x3E,0x08,0x08, // 43 '+'
  0x00,0x50,0x30,0x00,0x00, // 44 ','
  0x08,0x08,0x08,0x08,0x08, // 45 '-'
  0x00,0x60,0x60,0x00,0x00, // 46 '.'
  0x20,0x10,0x08,0x04,0x02, // 47 '/'
  0x3E,0x51,0x49,0x45,0x3E, // 48 '0'
  0x00,0x42,0x7F,0x40,0x00, // 49 '1'
  0x42,0x61,0x51,0x49,0x46, // 50 '2'
  0x21,0x41,0x45,0x4B,0x31, // 51 '3'
  0x18,0x14,0x12,0x7F,0x10, // 52 '4'
  0x27,0x45,0x45,0x45,0x39, // 53 '5'
  0x3C,0x4A,0x49,0x49,0x30, // 54 '6'
  0x01,0x71,0x09,0x05,0x03, // 55 '7'
  0x36,0x49,0x49,0x49,0x36, // 56 '8'
  0x06,0x49,0x49,0x29,0x1E, // 57 '9'
  0x00,0x36,0x36,0x00,0x00, // 58 ':'
  0x00,0x56,0x36,0x00,0x00, // 59 ';'
  0x08,0x14,0x22,0x41,0x00, // 60 '<'
  0x14,0x14,0x14,0x14,0x14, // 61 '='
  0x00,0x41,0x22,0x14,0x08, // 62 '>'
  0x02,0x01,0x51,0x09,0x06, // 63 '?'
  0x32,0x49,0x79,0x41,0x3E, // 64 '@'
  0x7E,0x11,0x11,0x11,0x7E, // 65 'A'
  0x7F,0x49,0x49,0x49,0x36, // 66 'B'
  0x3E,0x41,0x41,0x41,0x22, // 67 'C'
  0x7F,0x41,0x41,0x22,0x1C, // 68 'D'
  0x7F,0x49,0x49,0x49,0x41, // 69 'E'
  0x7F,0x09,0x09,0x09,0x01, // 70 'F'
  0x3E,0x41,0x49,0x49,0x7A, // 71 'G'
  0x7F,0x08,0x08,0x08,0x7F, // 72 'H'
  0x00,0x41,0x7F,0x41,0x00, // 73 'I'
  0x20,0x40,0x41,0x3F,0x01, // 74 'J'
  0x7F,0x08,0x14,0x22,0x41, // 75 'K'
  0x7F,0x40,0x40,0x40,0x40, // 76 'L'
  0x7F,0x02,0x0C,0x02,0x7F, // 77 'M'
  0x7F,0x04,0x08,0x10,0x7F, // 78 'N'
  0x3E,0x41,0x41,0x41,0x3E, // 79 'O'
  0x7F,0x09,0x09,0x09,0x06, // 80 'P'
  0x3E,0x41,0x51,0x21,0x5E, // 81 'Q'
  0x7F,0x09,0x19,0x29,0x46, // 82 'R'
  0x46,0x49,0x49,0x49,0x31, // 83 'S'
  0x01,0x01,0x7F,0x01,0x01, // 84 'T'
  0x3F,0x40,0x40,0x40,0x3F, // 85 'U'
  0x1F,0x20,0x40,0x20,0x1F, // 86 'V'
  0x7F,0x20,0x18,0x20,0x7F, // 87 'W'
  0x63,0x14,0x08,0x14,0x63, // 88 'X'
  0x07,0x08,0x70,0x08,0x07, // 89 'Y'
  0x61,0x51,0x49,0x45,0x43, // 90 'Z'
  0x00,0x7F,0x41,0x41,0x00, // 91 '['
  0x02,0x04,0x08,0x10,0x20, // 92 '\'
  0x00,0x41,0x41,0x7F,0x00  // 93 ']'
};

void drawChar(int x, int y, char c, uint8_t color) {
  if (c >= 'a' && c <= 'z') c -= 32;
  if (c < 32 || c > 93) return;
  int offset = (c - 32) * 5;
  for (int col = 0; col < 5; col++) {
    uint8_t line = pgm_read_byte(&font5x7[offset + col]);
    for (int row = 0; row < 7; row++) {
      if (line & (1 << row)) {
        drawPixel(x + col, y + row, color);
      }
    }
  }
}

void drawText(int x, int y, const char *str, uint8_t color) {
  int curX = x;
  while (*str) {
    drawChar(curX, y, *str++, color);
    curX += 6;
  }
}

void drawText_P(int x, int y, const char *pstr, uint8_t color) {
  int curX = x;
  char c;
  while ((c = pgm_read_byte(pstr++))) {
    drawChar(curX, y, c, color);
    curX += 6;
  }
}

void drawNum(int x, int y, int num, uint8_t color) {
  char buf[8];
  itoa(num, buf, 10);
  drawText(x, y, buf, color);
}

// --- OLED 硬體初始化與畫面推送 ---
void oled_init() {
  pinMode(OLED_CS, OUTPUT);
  pinMode(OLED_DC, OUTPUT);
  pinMode(OLED_RESET, OUTPUT);
  digitalWrite(OLED_RESET, LOW); delay(10);
  digitalWrite(OLED_RESET, HIGH); delay(10);
  SPI.begin();

  digitalWrite(OLED_CS, LOW);
  digitalWrite(OLED_DC, LOW);
  static const uint8_t PROGMEM initData[] = {
    0xAE, 0x20, 0x00, 0xC8, 0x40, 0x81, 0xFF, 0xA1,
    0xA6, 0xA8, 63,   0xD5, 0x80, 0xD9, 0xF1, 0xDA, 0x12,
    0xDB, 0x40, 0x8D, 0x14, 0xAF
  };
  for (uint8_t i = 0; i < sizeof(initData); i++) {
    SPI.transfer(pgm_read_byte(&initData[i]));
  }
  digitalWrite(OLED_CS, HIGH);
}

void oled_display() {
  digitalWrite(OLED_CS, LOW);
  digitalWrite(OLED_DC, LOW);
  SPI.transfer(0x21); SPI.transfer(0); SPI.transfer(127);
  SPI.transfer(0x22); SPI.transfer(0); SPI.transfer(7);
  digitalWrite(OLED_DC, HIGH);
  for (uint16_t i = 0; i < 1024; i++) {
    SPI.transfer(oled_buf[i]);
  }
  digitalWrite(OLED_CS, HIGH);
}

// --- 系統狀態與開關 ---
enum AppState {
  STATE_FACE,
  STATE_MENU,
  STATE_SYS_INFO
};

AppState appState = STATE_FACE;

int menuIndex = 0;
const int MENU_COUNT = 3;
unsigned long menuTimeout = 0;
const unsigned long MENU_IDLE_TIMEOUT = 6000;

bool soundEnabled = false; // 音量開關 (預設關閉)
bool bleEnabled   = false; // 藍牙開關 (預設關閉)

// --- Mochi 情緒列舉 ---
enum MochiMood {
  MOOD_DEFAULT,
  MOOD_SCRIBBLING, // 振筆疾書 (LM Studio 運算中)
  MOOD_HAPPY,      // 開心微笑
  MOOD_ANGRY,      // 生氣皺眉
  MOOD_SURPRISED,  // 驚訝大眼
  MOOD_DIZZY,      // 暈頭轉向
  MOOD_SLEEPY,     // 想睡瞇眼
  MOOD_COUNT
};

MochiMood currentMood = MOOD_DEFAULT;
MochiMood tempMood = MOOD_DEFAULT;
unsigned long tempMoodUntil = 0;

int batteryPercent = 85;
unsigned long lastBatteryCheck = 0;
const unsigned long BATTERY_INTERVAL = 3000;

// MPU6050
uint8_t mpuAddr = 0x69;
bool mpuReady = false;
int16_t ax = 0, ay = 0, az = 0;
int16_t gx = 0, gy = 0, gz = 0;
int16_t lastAx = 0, lastAy = 0, lastAz = 0;

long shakeEnergy = 0;
const long SHAKE_THRESHOLD = 360;
const long TAP_THRESHOLD = 13000;

unsigned long lastMotionTime = 0;
const unsigned long SLEEP_TIMEOUT = 15000;
bool isAsleep = false;

// 眼睛參數
const int EYE_LEFT_X = 38;
const int EYE_RIGHT_X = 90;
const int EYE_DEFAULT_Y = 32;
const int EYE_DEFAULT_W = 26;
const int EYE_DEFAULT_H = 34;

int eyeH = EYE_DEFAULT_H;
int eyeW = EYE_DEFAULT_W;
int curGazeX = 0;
int curGazeY = 0;
int targetGazeX = 0;
int targetGazeY = 0;

unsigned long lastBlinkTime = 0;
unsigned long blinkInterval = 3000;
bool isBlinking = false;
int blinkStage = 0;

int animFrame = 0;

// --- 藍牙開關實體控制函式 ---
void setBlePower(bool enable) {
  if (enable) {
    if (!bleHardwareReady) {
      if (ble.begin(false)) {
        bleHardwareReady = true;
        ble.sendCommandCheckOK(F("AT+GAPDEVNAME=Mochi-Robot"));
      }
    }
  } else {
    // 關閉廣播節能
    if (bleHardwareReady) {
      ble.sendCommandCheckOK(F("AT+GAPSTOPADV"));
    }
  }
}

int readBatteryPercentage() {
  digitalWrite(BATTERY_POWER, HIGH);
  delayMicroseconds(300);
  int raw = analogRead(BATTERY_READ);
  digitalWrite(BATTERY_POWER, LOW);
  int pct = map(raw, 530, 630, 0, 100);
  return constrain(pct, 0, 100);
}

void sndTick() {
  if (!soundEnabled) return;
  tone(BUZZER, 2200, 10);
}

void sndHappy() {
  if (!soundEnabled) return;
  tone(BUZZER, 1318, 50); delay(60);
  tone(BUZZER, 1568, 50); delay(60);
  tone(BUZZER, 2093, 90); delay(100);
  noTone(BUZZER);
}

void sndPoke() {
  if (!soundEnabled) return;
  tone(BUZZER, 1760, 35); delay(45);
  tone(BUZZER, 2200, 60); delay(70);
  noTone(BUZZER);
}

void sndSurprised() {
  if (!soundEnabled) return;
  tone(BUZZER, 1046, 40); delay(50);
  tone(BUZZER, 2093, 100); delay(110);
  noTone(BUZZER);
}

void sndAngry() {
  if (!soundEnabled) return;
  tone(BUZZER, 350, 70); delay(80);
  tone(BUZZER, 260, 130); delay(140);
  noTone(BUZZER);
}

void sndDizzy() {
  if (!soundEnabled) return;
  for (int f = 1600; f > 400; f -= 140) {
    tone(BUZZER, f, 22);
    delay(25);
  }
  noTone(BUZZER);
}

void sndSleep() {
  if (!soundEnabled) return;
  tone(BUZZER, 659, 80); delay(90);
  tone(BUZZER, 523, 120); delay(130);
  noTone(BUZZER);
}

void sndWake() {
  if (!soundEnabled) return;
  tone(BUZZER, 880, 40); delay(50);
  tone(BUZZER, 1318, 60); delay(70);
  tone(BUZZER, 1760, 90); delay(100);
  noTone(BUZZER);
}

void initMPU6050() {
  Wire.begin();
  Wire.setWireTimeout(3000, true);

  Wire.beginTransmission(0x69);
  if (Wire.endTransmission() != 0) {
    mpuAddr = 0x68;
  } else {
    mpuAddr = 0x69;
  }

  Wire.beginTransmission(mpuAddr);
  Wire.write(0x6B);
  Wire.write(0x00);
  if (Wire.endTransmission() == 0) {
    mpuReady = true;
  }
  delay(20);
}

void readMPU6050() {
  if (!mpuReady) return;

  Wire.beginTransmission(mpuAddr);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(mpuAddr, (uint8_t)14, (uint8_t)true);

  if (Wire.available() >= 14) {
    ax = Wire.read() << 8 | Wire.read();
    ay = Wire.read() << 8 | Wire.read();
    az = Wire.read() << 8 | Wire.read();
    int16_t rawTemp = Wire.read() << 8 | Wire.read();
    gx = Wire.read() << 8 | Wire.read();
    gy = Wire.read() << 8 | Wire.read();
    gz = Wire.read() << 8 | Wire.read();
  }
}

void drawBatteryRing(int pct) {
  if (pct < 15 && (animFrame % 20 < 10)) return;

  int armLen = map(pct, 0, 100, 4, 192);

  int topR = min(armLen, 64);
  drawFastHLine(64, 0, topR, 1);
  if (armLen > 64) {
    int sideR = min(armLen - 64, 64);
    drawFastVLine(127, 0, sideR, 1);
  }
  if (armLen > 128) {
    int botR = min(armLen - 128, 64);
    drawFastHLine(127 - botR, 63, botR, 1);
  }

  drawFastHLine(64 - topR, 0, topR, 1);
  if (armLen > 64) {
    int sideL = min(armLen - 64, 64);
    drawFastVLine(0, 0, sideL, 1);
  }
  if (armLen > 128) {
    int botL = min(armLen - 128, 64);
    drawFastHLine(0, 63, botL, 1);
  }
}

// 1. 一般圓角方形眼睛
void drawNormalEyes(int lx, int rx, int y, int w, int h) {
  int r = min(8, h / 2);
  if (h <= 4) {
    fillRoundRect(lx - w / 2, y - 1, w, 3, 1, 1);
    fillRoundRect(rx - w / 2, y - 1, w, 3, 1, 1);
  } else {
    fillRoundRect(lx - w / 2, y - h / 2, w, h, r, 1);
    fillRoundRect(rx - w / 2, y - h / 2, w, h, r, 1);

    int pupilOffsetX = constrain(curGazeX * 4 / 10, -4, 4);
    int pupilOffsetY = constrain(curGazeY * 4 / 10, -3, 3);
    fillCircle(lx + pupilOffsetX + 4, y + pupilOffsetY - 5, 2, 0);
    fillCircle(rx + pupilOffsetX + 4, y + pupilOffsetY - 5, 2, 0);
  }
}

// 2. 振筆疾書 (LM Studio 運算專注狂寫)
void drawScribblingEyes(int lx, int rx, int y, int frame) {
  fillRoundRect(lx - 13, y - 12, 26, 22, 5, 1);
  fillRoundRect(rx - 13, y - 12, 26, 22, 5, 1);

  // 斜眉
  for (int r = 0; r < 8; r++) {
    drawFastHLine(lx - 13, y - 12 + r, 26 - r * 2, 0);
    drawFastHLine(rx - 13 + r * 2, y - 12 + r, 26 - r * 2, 0);
  }

  // 瞳孔飛速掃視
  int scanX = ((frame * 3) % 16) - 8;
  fillCircle(lx + scanX, y + 2, 4, 0);
  fillCircle(rx + scanX, y + 2, 4, 0);

  // 汗滴
  if (frame % 8 < 4) {
    fillCircle(112, 16, 2, 1);
    drawPixel(112, 13, 1);
  }

  // 底部書寫折線與筆尖
  int penX = 22 + ((frame * 5) % 84);
  int penY = 56 + ((frame % 2 == 0) ? -1 : 1);
  for (int i = 20; i < penX; i += 6) {
    int w = ((i / 6) % 2 == 0) ? -2 : 2;
    drawLine(i, 56 + w, i + 6, 56 - w, 1);
  }
  drawLine(penX, penY, penX + 4, penY - 6, 1);
  drawLine(penX + 1, penY, penX + 5, penY - 6, 1);
}

// 3. 開心微笑月牙眼 (^ ^)
void drawHappyEyes(int lx, int rx, int y, int bounce) {
  int cy = y + bounce;
  fillRoundRect(lx - 14, cy - 8, 28, 20, 8, 1);
  fillRoundRect(lx - 16, cy - 2, 32, 22, 6, 0);
  fillRoundRect(rx - 14, cy - 8, 28, 20, 8, 1);
  fillRoundRect(rx - 16, cy - 2, 32, 22, 6, 0);
}

// 4. 生氣斜眉眼 (> <)
void drawAngryEyes(int lx, int rx, int y, int w, int h) {
  int r = 6;
  fillRoundRect(lx - w / 2, y - h / 2, w, h, r, 1);
  fillRoundRect(rx - w / 2, y - h / 2, w, h, r, 1);
  for (int row = 0; row < 10; row++) {
    drawFastHLine(lx - w / 2, y - h / 2 + row, w - row * 2, 0);
    drawFastHLine(rx - w / 2 + row * 2, y - h / 2 + row, w - row * 2, 0);
  }
}

// 5. 驚訝大圓眼 (O O)
void drawSurprisedEyes(int lx, int rx, int y, int gx, int gy) {
  fillCircle(lx, y, 18, 1);
  fillCircle(rx, y, 18, 1);
  fillCircle(lx + gx, y + gy, 7, 0);
  fillCircle(rx + gx, y + gy, 7, 0);
  fillCircle(lx + gx + 3, y + gy - 3, 2, 1);
  fillCircle(rx + gx + 3, y + gy - 3, 2, 1);
}

// 6. 暈眩叉叉眼 (X X)
void drawDizzyEyes(int lx, int rx, int y) {
  for (int i = -1; i <= 1; i++) {
    drawLine(lx - 12, y - 12 + i, lx + 12, y + 12 + i, 1);
    drawLine(lx - 12, y + 12 + i, lx + 12, y - 12 + i, 1);
    drawLine(rx - 12, y - 12 + i, rx + 12, y + 12 + i, 1);
    drawLine(rx - 12, y + 12 + i, rx + 12, y - 12 + i, 1);
  }
}

// 7. 想睡瞇瞇眼 (- -)
void drawSleepyEyes(int lx, int rx, int y, int frame) {
  fillRoundRect(lx - 13, y + 4, 26, 6, 2, 1);
  fillRoundRect(rx - 13, y + 4, 26, 6, 2, 1);

  int zOffset = (frame % 30);
  drawText_P(105, 20 - (zOffset / 2), PSTR("z"), 1);
  if (zOffset > 10) {
    drawText_P(112, 14 - (zOffset / 2), PSTR("Z"), 1);
  }
}

// --- 繪製選單 (MENU) 畫面 ---
void drawMenuScreen() {
  drawText_P(36, 4, PSTR("== MENU =="), 1);
  drawFastHLine(6, 14, 116, 1);

  if (menuIndex == 0) fillRect(6, 17, 116, 12, 1);
  drawText_P(10, 19, PSTR("1. SYSTEM STATUS >"), menuIndex == 0 ? 0 : 1);

  if (menuIndex == 1) fillRect(6, 31, 116, 12, 1);
  drawText_P(10, 33, PSTR("2. BLE     : "), menuIndex == 1 ? 0 : 1);
  drawText_P(82, 33, bleEnabled ? PSTR("[ON]") : PSTR("[OFF]"), menuIndex == 1 ? 0 : 1);

  if (menuIndex == 2) fillRect(6, 45, 116, 12, 1);
  drawText_P(10, 47, PSTR("3. SOUND   : "), menuIndex == 2 ? 0 : 1);
  drawText_P(82, 47, soundEnabled ? PSTR("[ON]") : PSTR("[OFF]"), menuIndex == 2 ? 0 : 1);

  drawFastHLine(6, 59, 116, 1);
}

// --- 繪製系統狀態 (SYSTEM STATUS) 詳情頁 ---
void drawSysInfoScreen() {
  drawText_P(22, 3, PSTR("SYSTEM STATUS"), 1);
  drawFastHLine(6, 13, 116, 1);

  drawText_P(8, 17, PSTR("BATTERY: "), 1);
  drawNum(62, 17, batteryPercent, 1);
  drawText_P(86, 17, PSTR("%"), 1);

  drawText_P(8, 28, PSTR("POWER  : "), 1);
  drawText_P(62, 28, digitalRead(CHARGE_STATUS) == LOW ? PSTR("CHARGING") : PSTR("BATTERY"), 1);

  drawText_P(8, 39, PSTR("MPU6050: "), 1);
  drawText_P(62, 39, mpuReady ? PSTR("ONLINE OK") : PSTR("FAILED"), 1);

  drawText_P(8, 50, PSTR("BLE    : "), 1);
  drawText_P(62, 50, bleEnabled ? PSTR("BROADCASTING") : PSTR("OFF"), 1);
}

void setup() {
  pinMode(SW1, INPUT_PULLUP);
  pinMode(SW2, INPUT_PULLUP);
  pinMode(SW3, INPUT_PULLUP);

  pinMode(LED_LEFT, OUTPUT);
  pinMode(LED_RIGHT, OUTPUT);
  digitalWrite(LED_LEFT, LOW);
  digitalWrite(LED_RIGHT, LOW);

  pinMode(BUZZER, OUTPUT);
  pinMode(CHARGE_STATUS, INPUT_PULLUP);
  pinMode(BATTERY_POWER, OUTPUT);
  digitalWrite(BATTERY_POWER, LOW);

  oled_init();
  initMPU6050();

  batteryPercent = readBatteryPercentage();

  clearDisplay();
  drawBatteryRing(batteryPercent);
  drawText_P(30, 22, PSTR("DASAI MOCHI"), 1);
  drawText_P(36, 36, PSTR("BAT: "), 1);
  drawNum(66, 36, batteryPercent, 1);
  drawText_P(90, 36, PSTR("%"), 1);
  oled_display();

  sndHappy();
  delay(600);

  lastMotionTime = millis();
}

void loop() {
  unsigned long now = millis();

  // 定期更新電量
  if (now - lastBatteryCheck > BATTERY_INTERVAL) {
    lastBatteryCheck = now;
    int curPct = readBatteryPercentage();
    batteryPercent = (batteryPercent * 3 + curPct) / 4;
  }

  // --- 藍牙數據接收監聽 (真實硬體監聽) ---
  if (bleEnabled && bleHardwareReady) {
    while (ble.available()) {
      char c = ble.read();
      if (c == 'B' || c == 'b') {
        // 'B' = Busy (LM Studio 運算中 -> 振筆疾書)
        currentMood = MOOD_SCRIBBLING;
        tempMoodUntil = 0;
        isAsleep = false;
        lastMotionTime = now;
        appState = STATE_FACE;
      } else if (c == 'D' || c == 'd') {
        // 'D' = Done (LM Studio 完成 -> 得意笑臉 + 叮一聲)
        currentMood = MOOD_HAPPY;
        tempMoodUntil = now + 4000;
        isAsleep = false;
        lastMotionTime = now;
        appState = STATE_FACE;
        sndHappy();
      } else if (c == 'I' || c == 'i') {
        // 'I' = Idle (回復呆萌待機)
        currentMood = MOOD_DEFAULT;
        tempMoodUntil = 0;
        appState = STATE_FACE;
      } else if (c == 'S' || c == 's') {
        // 'S' = Sleep (打瞌睡)
        isAsleep = true;
      }
    }
  }

  // 六軸感測器讀取
  readMPU6050();

  long dAx = (long)ax - lastAx;
  long dAy = (long)ay - lastAy;
  long dAz = (long)az - lastAz;
  long accelJerk = abs(dAx) + abs(dAy) + abs(dAz);

  long gyroRate = (abs((long)gx) + abs((long)gy) + abs((long)gz)) / 1000;
  if (gyroRate > 9) {
    shakeEnergy += gyroRate * 2;
  } else {
    shakeEnergy = (shakeEnergy * 86) / 100;
  }

  lastAx = ax;
  lastAy = ay;
  lastAz = az;

  // 實體動作觸發事件
  if (appState == STATE_FACE && currentMood != MOOD_SCRIBBLING) {
    if (shakeEnergy > SHAKE_THRESHOLD) {
      shakeEnergy = 0;
      tempMood = MOOD_DIZZY;
      tempMoodUntil = now + 3200;
      sndDizzy();
      lastMotionTime = now;
      isAsleep = false;
    }
    else if (accelJerk > TAP_THRESHOLD && (tempMood != MOOD_DIZZY)) {
      tempMood = MOOD_SURPRISED;
      tempMoodUntil = now + 1600;
      sndSurprised();
      lastMotionTime = now;
      if (isAsleep) { isAsleep = false; sndWake(); }
    }
    else if (accelJerk > 1000 || gyroRate > 3) {
      if (isAsleep) {
        isAsleep = false;
        sndWake();
      }
      lastMotionTime = now;
    }

    if (!isAsleep && (now - lastMotionTime > SLEEP_TIMEOUT) && (tempMoodUntil < now)) {
      isAsleep = true;
      sndSleep();
    }
  }

  // 重力傾斜視線跟隨
  if (appState == STATE_FACE && !isAsleep && tempMoodUntil < now && currentMood == MOOD_DEFAULT) {
    int tiltH = -((long)ay / 550);
    if (abs(ax) > abs(ay) * 4 / 3) {
      tiltH = -((long)ax / 550);
    }
    targetGazeX = constrain(tiltH, -13, 13);

    int tiltV = (long)ax / 800;
    if (abs(ay) > abs(ax) * 4 / 3) {
      tiltV = (long)ay / 800;
    }
    targetGazeY = constrain(tiltV, -5, 5);
  } else {
    targetGazeX = 0;
    targetGazeY = 0;
  }

  curGazeX = (curGazeX * 65 + targetGazeX * 35) / 100;
  curGazeY = (curGazeY * 65 + targetGazeY * 35) / 100;

  // 按鈕狀態處理
  static bool lastSw1 = HIGH;
  static bool lastSw2 = HIGH;
  static bool lastSw3 = HIGH;

  bool sw1 = digitalRead(SW1);
  bool sw2 = digitalRead(SW2);
  bool sw3 = digitalRead(SW3);

  // 選單逾時返回待機
  if ((appState == STATE_MENU || appState == STATE_SYS_INFO) && (now > menuTimeout)) {
    appState = STATE_FACE;
  }

  // 按鍵 1 (SW1): 進入選單 / 確認 / 切換
  if (lastSw1 == HIGH && sw1 == LOW) {
    lastMotionTime = now;
    if (appState == STATE_FACE) {
      appState = STATE_MENU;
      menuIndex = 0;
      menuTimeout = now + MENU_IDLE_TIMEOUT;
      sndTick();
    } else if (appState == STATE_MENU) {
      menuTimeout = now + MENU_IDLE_TIMEOUT;
      if (menuIndex == 0) {
        appState = STATE_SYS_INFO;
        sndTick();
      } else if (menuIndex == 1) {
        // 真實控制藍牙硬體晶片！
        bleEnabled = !bleEnabled;
        setBlePower(bleEnabled);
        if (bleEnabled) sndHappy(); else sndTick();
      } else if (menuIndex == 2) {
        soundEnabled = !soundEnabled;
        if (soundEnabled) sndHappy(); else sndTick();
      }
    } else if (appState == STATE_SYS_INFO) {
      appState = STATE_MENU;
      menuTimeout = now + MENU_IDLE_TIMEOUT;
      sndTick();
    }
    delay(40);
  }

  // 按鍵 2 (SW2): 選單中為「上」，待機中為「戳一下」
  if (lastSw2 == HIGH && sw2 == LOW) {
    lastMotionTime = now;
    if (appState == STATE_MENU) {
      menuIndex = (menuIndex + MENU_COUNT - 1) % MENU_COUNT;
      menuTimeout = now + MENU_IDLE_TIMEOUT;
      sndTick();
    } else if (appState == STATE_SYS_INFO) {
      appState = STATE_MENU;
      menuTimeout = now + MENU_IDLE_TIMEOUT;
      sndTick();
    } else {
      isAsleep = false;
      tempMoodUntil = 0;
      sndPoke();
      digitalWrite(LED_LEFT, HIGH);
      digitalWrite(LED_RIGHT, HIGH);
      clearDisplay();
      drawBatteryRing(batteryPercent);
      drawHappyEyes(EYE_LEFT_X, EYE_RIGHT_X, EYE_DEFAULT_Y, -4);
      drawNum(52, 6, batteryPercent, 1);
      drawText_P(70, 6, PSTR("%"), 1);
      oled_display();
      delay(400);
      digitalWrite(LED_LEFT, (currentMood == MOOD_HAPPY) ? HIGH : LOW);
      digitalWrite(LED_RIGHT, (currentMood == MOOD_HAPPY) ? HIGH : LOW);
    }
    delay(40);
  }

  // 按鍵 3 (SW3): 選單中為「下」，待機中為手動切換表情
  if (lastSw3 == HIGH && sw3 == LOW) {
    lastMotionTime = now;
    if (appState == STATE_MENU) {
      menuIndex = (menuIndex + 1) % MENU_COUNT;
      menuTimeout = now + MENU_IDLE_TIMEOUT;
      sndTick();
    } else if (appState == STATE_SYS_INFO) {
      appState = STATE_MENU;
      menuTimeout = now + MENU_IDLE_TIMEOUT;
      sndTick();
    } else {
      tempMoodUntil = 0;
      isAsleep = false;
      currentMood = (MochiMood)((currentMood + 1) % MOOD_COUNT);
      switch (currentMood) {
        case MOOD_HAPPY:      sndHappy(); digitalWrite(LED_LEFT, HIGH); digitalWrite(LED_RIGHT, HIGH); break;
        case MOOD_SCRIBBLING: sndPoke();  digitalWrite(LED_LEFT, HIGH); digitalWrite(LED_RIGHT, LOW); break;
        case MOOD_ANGRY:      sndAngry(); digitalWrite(LED_LEFT, HIGH); digitalWrite(LED_RIGHT, LOW); break;
        case MOOD_SURPRISED:  sndSurprised(); digitalWrite(LED_LEFT, HIGH); digitalWrite(LED_RIGHT, HIGH); break;
        case MOOD_DIZZY:      sndDizzy(); break;
        default:
          sndPoke();
          digitalWrite(LED_LEFT, LOW);
          digitalWrite(LED_RIGHT, LOW);
          break;
      }
    }
    delay(40);
  }

  lastSw1 = sw1;
  lastSw2 = sw2;
  lastSw3 = sw3;

  static MochiMood lastActiveMood = MOOD_DEFAULT;
  MochiMood activeMood = currentMood;
  if (tempMoodUntil > now) {
    activeMood = tempMood;
  } else if (isAsleep) {
    activeMood = MOOD_SLEEPY;
  }

  if (lastActiveMood == MOOD_DIZZY && activeMood != MOOD_DIZZY) {
    digitalWrite(LED_LEFT, LOW);
    digitalWrite(LED_RIGHT, LOW);
  }
  lastActiveMood = activeMood;

  // 自然眨眼
  if (appState == STATE_FACE && activeMood == MOOD_DEFAULT && !isAsleep) {
    if (!isBlinking && (now - lastBlinkTime > blinkInterval)) {
      isBlinking = true;
      blinkStage = 0;
      lastBlinkTime = now;
      blinkInterval = random(2200, 4800);
    }

    if (isBlinking) {
      blinkStage++;
      if (blinkStage == 1) eyeH = 18;
      else if (blinkStage == 2) eyeH = 3;
      else if (blinkStage == 3) eyeH = 18;
      else {
        eyeH = EYE_DEFAULT_H;
        isBlinking = false;
      }
    }
  } else {
    eyeH = EYE_DEFAULT_H;
    isBlinking = false;
  }

  // 畫面渲染
  clearDisplay();

  if (appState == STATE_MENU) {
    drawMenuScreen();
  } else if (appState == STATE_SYS_INFO) {
    drawSysInfoScreen();
  } else {
    drawBatteryRing(batteryPercent);

    int lx = EYE_LEFT_X + curGazeX;
    int rx = EYE_RIGHT_X + curGazeX;
    int y  = EYE_DEFAULT_Y + curGazeY;

    switch (activeMood) {
      case MOOD_DEFAULT:
        digitalWrite(LED_LEFT, LOW);
        digitalWrite(LED_RIGHT, LOW);
        drawNormalEyes(lx, rx, y, eyeW, eyeH);
        break;

      case MOOD_SCRIBBLING:
        drawScribblingEyes(EYE_LEFT_X, EYE_RIGHT_X, EYE_DEFAULT_Y, animFrame);
        break;

      case MOOD_HAPPY: {
        int bounce = (animFrame % 10 < 5) ? -2 : 0;
        drawHappyEyes(EYE_LEFT_X, EYE_RIGHT_X, EYE_DEFAULT_Y, bounce);
        break;
      }

      case MOOD_ANGRY: {
        int shake = (animFrame % 4 < 2) ? -1 : 1;
        drawAngryEyes(EYE_LEFT_X + shake, EYE_RIGHT_X + shake, EYE_DEFAULT_Y, eyeW, eyeH);
        break;
      }

      case MOOD_SURPRISED:
        digitalWrite(LED_LEFT, LOW);
        digitalWrite(LED_RIGHT, LOW);
        drawSurprisedEyes(EYE_LEFT_X, EYE_RIGHT_X, EYE_DEFAULT_Y, curGazeX, curGazeY);
        break;

      case MOOD_DIZZY: {
        int dX = (animFrame % 6 < 3) ? -3 : 3;
        drawDizzyEyes(EYE_LEFT_X + dX, EYE_RIGHT_X + dX, EYE_DEFAULT_Y);
        digitalWrite(LED_LEFT, (animFrame % 4 < 2) ? HIGH : LOW);
        digitalWrite(LED_RIGHT, (animFrame % 4 >= 2) ? HIGH : LOW);
        break;
      }

      case MOOD_SLEEPY:
        digitalWrite(LED_LEFT, LOW);
        digitalWrite(LED_RIGHT, LOW);
        drawSleepyEyes(EYE_LEFT_X, EYE_RIGHT_X, EYE_DEFAULT_Y, animFrame);
        break;

      default:
        drawNormalEyes(lx, rx, y, eyeW, eyeH);
        break;
    }
  }

  oled_display();

  animFrame++;
  delay(35);
}
