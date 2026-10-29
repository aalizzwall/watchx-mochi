#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- watchX 硬體腳位定義 ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_DC    A3
#define OLED_RESET A4
#define OLED_CS    A5

#define SW1 8   // 按鍵 1 (待機時進 Menu，Menu 內為確認/切換)
#define SW2 11  // 按鍵 2 (Menu 內為「上」，待機時為戳一下)
#define SW3 10  // 按鍵 3 (Menu 內為「下」，待機時切換表情)

#define LED_LEFT   13  // 左腮紅 LED
#define LED_RIGHT  6   // 右腮紅 LED
#define BUZZER     9   // 蜂鳴器 PWM

#define BATTERY_POWER 4    // 電池分壓電路啟用腳位 (HIGH 啟用)
#define BATTERY_READ  A11  // 電池電壓類比讀取腳位
#define CHARGE_STATUS 5    // 充電狀態檢測腳位 (LOW 為充電中)

// OLED 顯示實例 (硬體 SPI)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &SPI, OLED_DC, OLED_RESET, OLED_CS);

// --- 系統狀態與開關 ---
enum AppState {
  STATE_FACE,      // 待機表情模式
  STATE_MENU,      // 選單模式
  STATE_SYS_INFO   // 系統狀態詳情頁
};

AppState appState = STATE_FACE;

int menuIndex = 0;
const int MENU_COUNT = 3;
unsigned long menuTimeout = 0;
const unsigned long MENU_IDLE_TIMEOUT = 6000; // 6 秒無操作自動返回待機畫面

bool soundEnabled = false; // 音量開關 (預設關閉)
bool bleEnabled   = false; // 藍牙開關 (預設關閉)

// --- Mochi 情緒列舉 ---
enum MochiMood {
  MOOD_DEFAULT,    // 呆萌正常 (自然眨眼 + 重力視線跟隨)
  MOOD_SCRIBBLING, // 振筆疾書 (LM Studio 模型運算忙碌中)
  MOOD_HAPPY,      // 開心微笑 (彎彎笑眼 + 歡呼)
  MOOD_ANGRY,      // 生氣皺眉 (斜角怒視 + 警告音)
  MOOD_SURPRISED,  // 驚訝大眼 (圓滾滾瞳孔 + 驚呼)
  MOOD_DIZZY,      // 暈頭轉向 (X X 雙眼 + 暈眩音)
  MOOD_SLEEPY,     // 想睡瞇眼 (平線半閉 + Zzz 飄動)
  MOOD_COUNT
};

MochiMood currentMood = MOOD_DEFAULT;

// 暫時性情緒
MochiMood tempMood = MOOD_DEFAULT;
unsigned long tempMoodUntil = 0;

// --- 電量偵測變數 ---
int batteryPercent = 85;
unsigned long lastBatteryCheck = 0;
const unsigned long BATTERY_INTERVAL = 3000;

// --- MPU6050 變數與門檻值 ---
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

// --- 動態眼睛參數 ---
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

// 自然眨眼
unsigned long lastBlinkTime = 0;
unsigned long blinkInterval = 3000;
bool isBlinking = false;
int blinkStage = 0;

int animFrame = 0;

// --- 電池電量讀取函式 ---
int readBatteryPercentage() {
  digitalWrite(BATTERY_POWER, HIGH);
  delayMicroseconds(300);
  int raw = analogRead(BATTERY_READ);
  digitalWrite(BATTERY_POWER, LOW);

  int pct = map(raw, 530, 630, 0, 100);
  return constrain(pct, 0, 100);
}

// --- 8-Bit 萌系音效輔助 ---
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

// --- MPU6050 初始化與讀取 ---
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

// --- 繪製外圈電量框 ---
void drawBatteryRing(int pct) {
  if (pct < 15 && (animFrame % 20 < 10)) return;

  int armLen = map(pct, 0, 100, 4, 192);

  int topR = min(armLen, 64);
  display.drawFastHLine(64, 0, topR, SSD1306_WHITE);
  if (armLen > 64) {
    int sideR = min(armLen - 64, 64);
    display.drawFastVLine(127, 0, sideR, SSD1306_WHITE);
  }
  if (armLen > 128) {
    int botR = min(armLen - 128, 64);
    display.drawFastHLine(127 - botR, 63, botR, SSD1306_WHITE);
  }

  display.drawFastHLine(64 - topR, 0, topR, SSD1306_WHITE);
  if (armLen > 64) {
    int sideL = min(armLen - 64, 64);
    display.drawFastVLine(0, 0, sideL, SSD1306_WHITE);
  }
  if (armLen > 128) {
    int botL = min(armLen - 128, 64);
    display.drawFastHLine(0, 63, botL, SSD1306_WHITE);
  }
}

// --- 繪製各種表情 ---

// 1. 一般圓角方形眼睛
void drawNormalEyes(int lx, int rx, int y, int w, int h) {
  int r = min(8, h / 2);
  if (h <= 4) {
    display.fillRoundRect(lx - w / 2, y - 1, w, 3, 1, SSD1306_WHITE);
    display.fillRoundRect(rx - w / 2, y - 1, w, 3, 1, SSD1306_WHITE);
  } else {
    display.fillRoundRect(lx - w / 2, y - h / 2, w, h, r, SSD1306_WHITE);
    display.fillRoundRect(rx - w / 2, y - h / 2, w, h, r, SSD1306_WHITE);

    int pupilOffsetX = constrain(curGazeX * 4 / 10, -4, 4);
    int pupilOffsetY = constrain(curGazeY * 4 / 10, -3, 3);
    display.fillCircle(lx + pupilOffsetX + 4, y + pupilOffsetY - 5, 2, SSD1306_BLACK);
    display.fillCircle(rx + pupilOffsetX + 4, y + pupilOffsetY - 5, 2, SSD1306_BLACK);
  }
}

// 2. 振筆疾書 (LM Studio 運算中專注狂寫)
void drawScribblingEyes(int lx, int rx, int y, int frame) {
  // 認真微瞇眼
  display.fillRoundRect(lx - 13, y - 12, 26, 22, 5, SSD1306_WHITE);
  display.fillRoundRect(rx - 13, y - 12, 26, 22, 5, SSD1306_WHITE);
  // 嚴肅內壓眉
  display.fillTriangle(lx - 13, y - 13, lx + 13, y - 13, lx + 13, y - 4, SSD1306_BLACK);
  display.fillTriangle(rx - 13, y - 13, rx + 13, y - 13, rx - 13, y - 4, SSD1306_BLACK);

  // 瞳孔飛速左右掃描
  int scanX = ((frame * 3) % 16) - 8;
  display.fillCircle(lx + scanX, y + 2, 4, SSD1306_BLACK);
  display.fillCircle(rx + scanX, y + 2, 4, SSD1306_BLACK);

  // 額頭汗珠
  if (frame % 8 < 4) {
    display.fillCircle(112, 16, 2, SSD1306_WHITE);
    display.drawPixel(112, 13, SSD1306_WHITE);
  }

  // 底部：瘋狂書寫折線與筆尖動態
  int penX = 22 + ((frame * 5) % 84);
  int penY = 56 + ((frame % 2 == 0) ? -1 : 1);
  for (int i = 20; i < penX; i += 6) {
    int w = ((i / 6) % 2 == 0) ? -2 : 2;
    display.drawLine(i, 56 + w, i + 6, 56 - w, SSD1306_WHITE);
  }
  // 筆尖
  display.drawLine(penX, penY, penX + 4, penY - 6, SSD1306_WHITE);
  display.drawLine(penX + 1, penY, penX + 5, penY - 6, SSD1306_WHITE);
}

// 3. 開心微笑月牙眼 (^ ^)
void drawHappyEyes(int lx, int rx, int y, int bounce) {
  int cy = y + bounce;
  display.fillRoundRect(lx - 14, cy - 8, 28, 20, 8, SSD1306_WHITE);
  display.fillRoundRect(lx - 16, cy - 2, 32, 22, 6, SSD1306_BLACK);
  display.fillRoundRect(rx - 14, cy - 8, 28, 20, 8, SSD1306_WHITE);
  display.fillRoundRect(rx - 16, cy - 2, 32, 22, 6, SSD1306_BLACK);
}

// 4. 生氣斜眉眼 (> <)
void drawAngryEyes(int lx, int rx, int y, int w, int h) {
  int r = 6;
  display.fillRoundRect(lx - w / 2, y - h / 2, w, h, r, SSD1306_WHITE);
  display.fillRoundRect(rx - w / 2, y - h / 2, w, h, r, SSD1306_WHITE);
  display.fillTriangle(lx - w / 2, y - h / 2 - 1, lx + w / 2 + 1, y - h / 2 - 1, lx + w / 2 + 1, y + 2, SSD1306_BLACK);
  display.fillTriangle(rx - w / 2 - 1, y - h / 2 - 1, rx + w / 2, y - h / 2 - 1, rx - w / 2 - 1, y + 2, SSD1306_BLACK);
}

// 5. 驚訝大圓眼 (O O)
void drawSurprisedEyes(int lx, int rx, int y, int gx, int gy) {
  display.fillCircle(lx, y, 18, SSD1306_WHITE);
  display.fillCircle(rx, y, 18, SSD1306_WHITE);
  display.fillCircle(lx + gx, y + gy, 7, SSD1306_BLACK);
  display.fillCircle(rx + gx, y + gy, 7, SSD1306_BLACK);
  display.fillCircle(lx + gx + 3, y + gy - 3, 2, SSD1306_WHITE);
  display.fillCircle(rx + gx + 3, y + gy - 3, 2, SSD1306_WHITE);
}

// 6. 暈眩叉叉眼 (X X)
void drawDizzyEyes(int lx, int rx, int y) {
  for (int i = -1; i <= 1; i++) {
    display.drawLine(lx - 12, y - 12 + i, lx + 12, y + 12 + i, SSD1306_WHITE);
    display.drawLine(lx - 12, y + 12 + i, lx + 12, y - 12 + i, SSD1306_WHITE);
    display.drawLine(rx - 12, y - 12 + i, rx + 12, y + 12 + i, SSD1306_WHITE);
    display.drawLine(rx - 12, y + 12 + i, rx + 12, y - 12 + i, SSD1306_WHITE);
  }
}

// 7. 想睡瞇瞇眼 (- -)
void drawSleepyEyes(int lx, int rx, int y, int frame) {
  display.fillRoundRect(lx - 13, y + 4, 26, 6, 2, SSD1306_WHITE);
  display.fillRoundRect(rx - 13, y + 4, 26, 6, 2, SSD1306_WHITE);

  int zOffset = (frame % 30);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(105, 20 - (zOffset / 2));
  display.print(F("z"));
  if (zOffset > 10) {
    display.setCursor(112, 14 - (zOffset / 2));
    display.print(F("Z"));
  }
}

// --- 繪製選單 (MENU) 畫面 ---
void drawMenuScreen() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(42, 4);
  display.print(F("== MENU =="));
  display.drawFastHLine(6, 14, 116, SSD1306_WHITE);

  // 選項 0: 系統狀態
  if (menuIndex == 0) display.fillRect(6, 17, 116, 12, SSD1306_WHITE);
  display.setTextColor(menuIndex == 0 ? SSD1306_BLACK : SSD1306_WHITE);
  display.setCursor(10, 19);
  display.print(F("1. SYSTEM STATUS >"));

  // 選項 1: 藍牙開關
  if (menuIndex == 1) display.fillRect(6, 31, 116, 12, SSD1306_WHITE);
  display.setTextColor(menuIndex == 1 ? SSD1306_BLACK : SSD1306_WHITE);
  display.setCursor(10, 33);
  display.print(F("2. BLE     : "));
  display.print(bleEnabled ? F("[ON]") : F("[OFF]"));

  // 選項 2: 音量開關
  if (menuIndex == 2) display.fillRect(6, 45, 116, 12, SSD1306_WHITE);
  display.setTextColor(menuIndex == 2 ? SSD1306_BLACK : SSD1306_WHITE);
  display.setCursor(10, 47);
  display.print(F("3. SOUND   : "));
  display.print(soundEnabled ? F("[ON]") : F("[OFF]"));

  display.drawFastHLine(6, 59, 116, SSD1306_WHITE);
}

// --- 繪製系統狀態 (SYSTEM STATUS) 詳情頁 ---
void drawSysInfoScreen() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(24, 3);
  display.print(F("SYSTEM STATUS"));
  display.drawFastHLine(6, 13, 116, SSD1306_WHITE);

  display.setCursor(8, 17);
  display.print(F("BATTERY: "));
  display.print(batteryPercent);
  display.print(F("%"));

  display.setCursor(8, 28);
  display.print(F("POWER  : "));
  display.print(digitalRead(CHARGE_STATUS) == LOW ? F("CHARGING") : F("DISCHARGE"));

  display.setCursor(8, 39);
  display.print(F("MPU6050: "));
  display.print(mpuReady ? F("ONLINE OK") : F("FAILED"));

  display.setCursor(8, 50);
  display.print(F("BLE    : "));
  display.print(bleEnabled ? F("ACTIVE") : F("OFF"));
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

  if (!display.begin(SSD1306_SWITCHCAPVCC)) {
    while (1);
  }

  initMPU6050();

  batteryPercent = readBatteryPercentage();

  display.clearDisplay();
  drawBatteryRing(batteryPercent);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(30, 22);
  display.println(F("DASAI MOCHI"));
  display.setCursor(38, 36);
  display.print(F("BAT: "));
  display.print(batteryPercent);
  display.println(F("%"));
  display.display();

  sndHappy();
  delay(600);

  lastMotionTime = millis();
}

void loop() {
  unsigned long now = millis();

  // 定期更新電量讀數
  if (now - lastBatteryCheck > BATTERY_INTERVAL) {
    lastBatteryCheck = now;
    int curPct = readBatteryPercentage();
    batteryPercent = (batteryPercent * 3 + curPct) / 4;
  }

  // --- 1. 讀取六軸感測器 ---
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

  // --- 2. 實體動作觸發事件 (僅在待機表情模式有效) ---
  if (appState == STATE_FACE) {
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

  // --- 3. 重力傾斜視線跟隨 ---
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

  // --- 4. 按鈕狀態與選單機制 ---
  static bool lastSw1 = HIGH;
  static bool lastSw2 = HIGH;
  static bool lastSw3 = HIGH;

  bool sw1 = digitalRead(SW1);
  bool sw2 = digitalRead(SW2);
  bool sw3 = digitalRead(SW3);

  // 選單逾時自動返回待機畫面
  if ((appState == STATE_MENU || appState == STATE_SYS_INFO) && (now > menuTimeout)) {
    appState = STATE_FACE;
  }

  // --- 按鍵 1 (SW1): 進入選單 / 確認 / True-False 切換 ---
  if (lastSw1 == HIGH && sw1 == LOW) {
    lastMotionTime = now;
    if (appState == STATE_FACE) {
      // 待機畫面下：按 SW1 進入選單
      appState = STATE_MENU;
      menuIndex = 0;
      menuTimeout = now + MENU_IDLE_TIMEOUT;
      sndTick();
    } else if (appState == STATE_MENU) {
      // 選單內：按 SW1 進行 True/False 切換或進入狀態頁
      menuTimeout = now + MENU_IDLE_TIMEOUT;
      if (menuIndex == 0) {
        // 進入系統狀態頁
        appState = STATE_SYS_INFO;
        sndTick();
      } else if (menuIndex == 1) {
        // 藍牙開關切換
        bleEnabled = !bleEnabled;
        if (bleEnabled) sndHappy(); else sndTick();
      } else if (menuIndex == 2) {
        // 音量開關切換
        soundEnabled = !soundEnabled;
        if (soundEnabled) sndHappy(); else sndTick();
      }
    } else if (appState == STATE_SYS_INFO) {
      // 系統狀態頁下按 SW1 返回選單
      appState = STATE_MENU;
      menuTimeout = now + MENU_IDLE_TIMEOUT;
      sndTick();
    }
    delay(40);
  }

  // --- 按鍵 2 (SW2): 選單中為「上」，待機中為「戳一下」 ---
  if (lastSw2 == HIGH && sw2 == LOW) {
    lastMotionTime = now;
    if (appState == STATE_MENU) {
      // 選單內：向上切換選項
      menuIndex = (menuIndex + MENU_COUNT - 1) % MENU_COUNT;
      menuTimeout = now + MENU_IDLE_TIMEOUT;
      sndTick();
    } else if (appState == STATE_SYS_INFO) {
      appState = STATE_MENU;
      menuTimeout = now + MENU_IDLE_TIMEOUT;
      sndTick();
    } else {
      // 待機時戳一下 (摸頭)
      isAsleep = false;
      tempMoodUntil = 0;
      sndPoke();
      digitalWrite(LED_LEFT, HIGH);
      digitalWrite(LED_RIGHT, HIGH);
      display.clearDisplay();
      drawBatteryRing(batteryPercent);
      drawHappyEyes(EYE_LEFT_X, EYE_RIGHT_X, EYE_DEFAULT_Y, -4);
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(54, 6);
      display.print(batteryPercent);
      display.print(F("%"));
      display.display();
      delay(400);
      digitalWrite(LED_LEFT, (currentMood == MOOD_HAPPY) ? HIGH : LOW);
      digitalWrite(LED_RIGHT, (currentMood == MOOD_HAPPY) ? HIGH : LOW);
    }
    delay(40);
  }

  // --- 按鍵 3 (SW3): 選單中為「下」，待機中為「手動切換表情」 ---
  if (lastSw3 == HIGH && sw3 == LOW) {
    lastMotionTime = now;
    if (appState == STATE_MENU) {
      // 選單內：向下切換選項
      menuIndex = (menuIndex + 1) % MENU_COUNT;
      menuTimeout = now + MENU_IDLE_TIMEOUT;
      sndTick();
    } else if (appState == STATE_SYS_INFO) {
      appState = STATE_MENU;
      menuTimeout = now + MENU_IDLE_TIMEOUT;
      sndTick();
    } else {
      // 待機時手動循環表情
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

  // --- 5. 決定活躍表情 (待機狀態下) ---
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

  // --- 6. 自然眨眼邏輯 ---
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

  // --- 7. 畫面渲染 ---
  display.clearDisplay();

  if (appState == STATE_MENU) {
    // 渲染選單介面
    drawMenuScreen();
  } else if (appState == STATE_SYS_INFO) {
    // 渲染系統狀態詳情
    drawSysInfoScreen();
  } else {
    // 渲染待機表情畫面
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

  display.display();

  animFrame++;
  delay(35);
}
