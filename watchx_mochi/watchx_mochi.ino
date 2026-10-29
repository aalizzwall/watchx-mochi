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

#define SW1 8   // 按鍵 1 (控制聲音開關，預設關)
#define SW2 11  // 按鍵 2 (戳一下 / 摸頭互動 Poke)
#define SW3 10  // 按鍵 3 (手動切換表情模式)

#define LED_LEFT   13  // 左腮紅 LED
#define LED_RIGHT  6   // 右腮紅 LED
#define BUZZER     9   // 蜂鳴器 PWM

#define BATTERY_POWER 4    // 電池分壓電路啟用腳位 (HIGH 啟用)
#define BATTERY_READ  A11  // 電池電壓類比讀取腳位
#define CHARGE_STATUS 5    // 充電狀態檢測腳位 (LOW 為充電中)

// OLED 顯示實例 (硬體 SPI)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &SPI, OLED_DC, OLED_RESET, OLED_CS);

// --- 聲音控制 (預設為關閉) ---
bool soundEnabled = false;

// --- Mochi 情緒列舉 ---
enum MochiMood {
  MOOD_DEFAULT,    // 呆萌正常 (自然眨眼 + 重力視線跟隨)
  MOOD_HAPPY,      // 開心微笑 (彎彎笑眼 + 歡呼)
  MOOD_ANGRY,      // 生氣皺眉 (斜角怒視 + 警告音)
  MOOD_SURPRISED,  // 驚訝大眼 (圓滾滾瞳孔 + 驚呼)
  MOOD_DIZZY,      // 暈頭轉向 (X X 雙眼 + 暈眩音)
  MOOD_SLEEPY,     // 想睡瞇眼 (平線半閉 + Zzz 飄動)
  MOOD_COUNT
};

MochiMood currentMood = MOOD_DEFAULT;

// 暫時性情緒 (被搖晃暈眩、拍打嚇到時暫時覆蓋)
MochiMood tempMood = MOOD_DEFAULT;
unsigned long tempMoodUntil = 0;

// --- 電量偵測變數 ---
int batteryPercent = 85;
unsigned long lastBatteryCheck = 0;
const unsigned long BATTERY_INTERVAL = 3000; // 每 3 秒更新一次電量

// --- MPU6050 變數與門檻值 ---
uint8_t mpuAddr = 0x69;
bool mpuReady = false;

int16_t ax = 0, ay = 0, az = 0;
int16_t gx = 0, gy = 0, gz = 0;
int16_t lastAx = 0, lastAy = 0, lastAz = 0;

// 晃動累積量 (改為定點整數，節省 2KB+ 浮點運算庫空間)
long shakeEnergy = 0;
const long SHAKE_THRESHOLD = 360;

// 敲擊檢測門檻
const long TAP_THRESHOLD = 13000;

// 靜止睡眠計時器
unsigned long lastMotionTime = 0;
const unsigned long SLEEP_TIMEOUT = 15000; // 靜止 15 秒後進入想睡模式
bool isAsleep = false;

// --- 動態眼睛參數 (整數座標) ---
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

// 自然眨眼計時器
unsigned long lastBlinkTime = 0;
unsigned long blinkInterval = 3000;
bool isBlinking = false;
int blinkStage = 0;

unsigned long lastAnimTime = 0;
int animFrame = 0;

// --- 電池電量讀取函式 ---
int readBatteryPercentage() {
  pinMode(BATTERY_POWER, OUTPUT);
  digitalWrite(BATTERY_POWER, HIGH);
  delayMicroseconds(250);
  int raw = analogRead(BATTERY_READ);
  digitalWrite(BATTERY_POWER, LOW);

  // watchX 分壓讀值：530 代表約 3.4V (0%), 630 代表約 4.2V (100%)
  int pct = map(raw, 530, 630, 0, 100);
  pct = constrain(pct, 0, 100);

  Serial.print(F("[POWER] Raw: "));
  Serial.print(raw);
  Serial.print(F(" -> Battery: "));
  Serial.print(pct);
  Serial.println(F("%"));

  return pct;
}

// --- 8-Bit 萌系音效 ---
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

void sndBlink() {
  if (!soundEnabled) return;
  tone(BUZZER, 2400, 12);
  delay(16);
  noTone(BUZZER);
}

void sndWake() {
  if (!soundEnabled) return;
  tone(BUZZER, 880, 40); delay(50);
  tone(BUZZER, 1318, 60); delay(70);
  tone(BUZZER, 1760, 90); delay(100);
  noTone(BUZZER);
}

void sndSleep() {
  if (!soundEnabled) return;
  tone(BUZZER, 659, 80); delay(90);
  tone(BUZZER, 523, 120); delay(130);
  noTone(BUZZER);
}

// --- MPU6050 初始化與讀取 ---
void initMPU6050() {
  Wire.begin();
  // 檢測 I2C 地址 (0x69 優先，失敗則嘗試 0x68)
  Wire.beginTransmission(0x69);
  if (Wire.endTransmission() != 0) {
    mpuAddr = 0x68;
  } else {
    mpuAddr = 0x69;
  }

  // 喚醒 MPU6050 (寫入 PWR_MGMT_1 = 0)
  Wire.beginTransmission(mpuAddr);
  Wire.write(0x6B);
  Wire.write(0x00);
  if (Wire.endTransmission() == 0) {
    mpuReady = true;
  }
  delay(20); // 等待內部振盪穩定
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

// --- 繪製外圈電量框 (雙向對稱動態環繞) ---
void drawBatteryRing(int pct) {
  // 當電量極低時 (< 15%)，邊框每秒閃爍一次提醒
  if (pct < 15 && (animFrame % 20 < 10)) {
    return;
  }

  // 半周長總長度為 192 像素 (頂部中心 -> 側邊 -> 底部中心)
  // 頂邊半長 64px, 側邊 64px, 底邊半長 64px = 192px
  int armLen = map(pct, 0, 100, 4, 192);

  // --- 右半邊電量軌道 ---
  // 階段 1: 頂邊 (從 X=64 往右畫)
  int topR = min(armLen, 64);
  display.drawFastHLine(64, 0, topR, SSD1306_WHITE);

  // 階段 2: 右邊邊界 (從 Y=0 往下畫)
  if (armLen > 64) {
    int sideR = min(armLen - 64, 64);
    display.drawFastVLine(127, 0, sideR, SSD1306_WHITE);
  }

  // 階段 3: 底邊 (從 X=127 往左畫向中心)
  if (armLen > 128) {
    int botR = min(armLen - 128, 64);
    display.drawFastHLine(127 - botR, 63, botR, SSD1306_WHITE);
  }

  // --- 左半邊電量軌道 (對稱鏡像) ---
  // 階段 1: 頂邊 (從 X=64 往左畫)
  display.drawFastHLine(64 - topR, 0, topR, SSD1306_WHITE);

  // 階段 2: 左邊邊界 (從 Y=0 往下畫)
  if (armLen > 64) {
    int sideL = min(armLen - 64, 64);
    display.drawFastVLine(0, 0, sideL, SSD1306_WHITE);
  }

  // 階段 3: 底邊 (從 X=0 往右畫向中心)
  if (armLen > 128) {
    int botL = min(armLen - 128, 64);
    display.drawFastHLine(0, 63, botL, SSD1306_WHITE);
  }
}

// --- 繪製各種情緒的眼睛 ---

// 1. 一般圓角方形眼睛 (含高對比眼神光)
void drawNormalEyes(int lx, int rx, int y, int w, int h) {
  int r = min(8, h / 2);
  if (h <= 4) {
    display.fillRoundRect(lx - w / 2, y - 1, w, 3, 1, SSD1306_WHITE);
    display.fillRoundRect(rx - w / 2, y - 1, w, 3, 1, SSD1306_WHITE);
  } else {
    display.fillRoundRect(lx - w / 2, y - h / 2, w, h, r, SSD1306_WHITE);
    display.fillRoundRect(rx - w / 2, y - h / 2, w, h, r, SSD1306_WHITE);

    // 加上內側反光瞳孔，讓視線方向一目了然
    int pupilOffsetX = constrain((int)(curGazeX * 0.4), -4, 4);
    int pupilOffsetY = constrain((int)(curGazeY * 0.4), -3, 3);
    display.fillCircle(lx + pupilOffsetX + 4, y + pupilOffsetY - 5, 2, SSD1306_BLACK);
    display.fillCircle(rx + pupilOffsetX + 4, y + pupilOffsetY - 5, 2, SSD1306_BLACK);
  }
}

// 2. 開心微笑月牙眼 (^ ^)
void drawHappyEyes(int lx, int rx, int y, int bounce) {
  int cy = y + bounce;
  display.fillRoundRect(lx - 14, cy - 8, 28, 20, 8, SSD1306_WHITE);
  display.fillRoundRect(lx - 16, cy - 2, 32, 22, 6, SSD1306_BLACK);
  display.fillRoundRect(rx - 14, cy - 8, 28, 20, 8, SSD1306_WHITE);
  display.fillRoundRect(rx - 16, cy - 2, 32, 22, 6, SSD1306_BLACK);
}

// 3. 生氣斜眉眼 (> <)
void drawAngryEyes(int lx, int rx, int y, int w, int h) {
  int r = 6;
  display.fillRoundRect(lx - w / 2, y - h / 2, w, h, r, SSD1306_WHITE);
  display.fillRoundRect(rx - w / 2, y - h / 2, w, h, r, SSD1306_WHITE);
  display.fillTriangle(lx - w / 2, y - h / 2 - 1, lx + w / 2 + 1, y - h / 2 - 1, lx + w / 2 + 1, y + 2, SSD1306_BLACK);
  display.fillTriangle(rx - w / 2 - 1, y - h / 2 - 1, rx + w / 2, y - h / 2 - 1, rx - w / 2 - 1, y + 2, SSD1306_BLACK);
}

// 4. 驚訝大圓眼 (O O)
void drawSurprisedEyes(int lx, int rx, int y, int gx, int gy) {
  display.fillCircle(lx, y, 18, SSD1306_WHITE);
  display.fillCircle(rx, y, 18, SSD1306_WHITE);
  display.fillCircle(lx + gx, y + gy, 7, SSD1306_BLACK);
  display.fillCircle(rx + gx, y + gy, 7, SSD1306_BLACK);
  display.fillCircle(lx + gx + 3, y + gy - 3, 2, SSD1306_WHITE);
  display.fillCircle(rx + gx + 3, y + gy - 3, 2, SSD1306_WHITE);
}

// 5. 暈眩叉叉眼 (X X)
void drawDizzyEyes(int lx, int rx, int y) {
  for (int i = -1; i <= 1; i++) {
    display.drawLine(lx - 12, y - 12 + i, lx + 12, y + 12 + i, SSD1306_WHITE);
    display.drawLine(lx - 12, y + 12 + i, lx + 12, y - 12 + i, SSD1306_WHITE);
    display.drawLine(rx - 12, y - 12 + i, rx + 12, y + 12 + i, SSD1306_WHITE);
    display.drawLine(rx - 12, y + 12 + i, rx + 12, y - 12 + i, SSD1306_WHITE);
  }
}

// 6. 想睡瞇瞇眼 (- -)
void drawSleepyEyes(int lx, int rx, int y, int frame) {
  display.fillRoundRect(lx - 13, y + 4, 26, 6, 2, SSD1306_WHITE);
  display.fillRoundRect(rx - 13, y + 4, 26, 6, 2, SSD1306_WHITE);

  int zOffset = (frame % 30);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(105, 20 - (zOffset / 2));
  display.print("z");
  if (zOffset > 10) {
    display.setCursor(112, 14 - (zOffset / 2));
    display.print("Z");
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(SW1, INPUT_PULLUP);
  pinMode(SW2, INPUT_PULLUP);
  pinMode(SW3, INPUT_PULLUP);

  pinMode(LED_LEFT, OUTPUT);
  pinMode(LED_RIGHT, OUTPUT);
  digitalWrite(LED_LEFT, LOW);
  digitalWrite(LED_RIGHT, LOW);

  pinMode(BUZZER, OUTPUT);
  pinMode(CHARGE_STATUS, INPUT_PULLUP);

  // 初始化 OLED (SPI 介面)
  if (!display.begin(SSD1306_SWITCHCAPVCC)) {
    while (1);
  }

  // 初始化 MPU6050
  initMPU6050();

  // 初始讀取電量
  batteryPercent = readBatteryPercentage();

  // 開機畫面
  display.clearDisplay();
  drawBatteryRing(batteryPercent);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(30, 22);
  display.println("DASAI MOCHI");
  display.setCursor(38, 36);
  display.print("BAT: ");
  display.print(batteryPercent);
  display.println("%");
  display.display();

  // 若聲音有開才叫 (目前預設關閉)
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
    // 濾波平滑更新
    batteryPercent = (batteryPercent * 3 + curPct) / 4;
  }

  // --- 1. 讀取六軸感測器 ---
  readMPU6050();

  // 加速度劇烈突變 (敲擊 / 摔落)
  long dAx = (long)ax - lastAx;
  long dAy = (long)ay - lastAy;
  long dAz = (long)az - lastAz;
  long accelJerk = abs(dAx) + abs(dAy) + abs(dAz);

  // 角速度綜合能量 (劇烈搖晃檢測，改為整數定點)
  long gyroRate = (abs((long)gx) + abs((long)gy) + abs((long)gz)) / 1000;
  if (gyroRate > 9) {
    shakeEnergy += gyroRate * 2;
  } else {
    shakeEnergy = (shakeEnergy * 86) / 100;
  }

  lastAx = ax;
  lastAy = ay;
  lastAz = az;

  // --- 2. 實體動作觸發事件 ---

  // 事件 A: 劇烈搖晃 -> Dizzy 暈眩
  if (shakeEnergy > SHAKE_THRESHOLD) {
    shakeEnergy = 0;
    tempMood = MOOD_DIZZY;
    tempMoodUntil = now + 3200;
    sndDizzy();
    lastMotionTime = now;
    isAsleep = false;
  }
  // 事件 B: 敲擊 / 拍打 -> Surprised 嚇一跳
  else if (accelJerk > TAP_THRESHOLD && (tempMood != MOOD_DIZZY)) {
    tempMood = MOOD_SURPRISED;
    tempMoodUntil = now + 1600;
    sndSurprised();
    lastMotionTime = now;
    if (isAsleep) { isAsleep = false; sndWake(); }
  }
  // 事件 C: 正常移動活動
  else if (accelJerk > 1000 || gyroRate > 3) {
    if (isAsleep) {
      isAsleep = false;
      sndWake();
    }
    lastMotionTime = now;
  }

  // 事件 D: 長時間靜止檢測 (逾時睡眠)
  if (!isAsleep && (now - lastMotionTime > SLEEP_TIMEOUT) && (tempMoodUntil < now)) {
    isAsleep = true;
    sndSleep();
  }

  // --- 3. 重力傾斜視線跟隨 (全整數運算) ---
  if (!isAsleep && tempMoodUntil < now && currentMood == MOOD_DEFAULT) {
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

  // 視線過渡 (整數阻尼濾波)
  curGazeX = (curGazeX * 65 + targetGazeX * 35) / 100;
  curGazeY = (curGazeY * 65 + targetGazeY * 35) / 100;

  // --- 4. 按鈕狀態處理 ---
  static bool lastSw1 = HIGH;
  static bool lastSw2 = HIGH;
  static bool lastSw3 = HIGH;

  bool sw1 = digitalRead(SW1);
  bool sw2 = digitalRead(SW2);
  bool sw3 = digitalRead(SW3);

  // SW1: 控制聲音開關 (預設關，按一下切換)
  if (lastSw1 == HIGH && sw1 == LOW) {
    soundEnabled = !soundEnabled;
    if (soundEnabled) {
      // 開啟聲音回饋：清脆雙嗶聲 + 雙腮紅亮起
      tone(BUZZER, 1760, 50); delay(60);
      tone(BUZZER, 2093, 80); delay(90);
      noTone(BUZZER);
      digitalWrite(LED_LEFT, HIGH);
      digitalWrite(LED_RIGHT, HIGH);
      delay(70);
      digitalWrite(LED_LEFT, LOW);
      digitalWrite(LED_RIGHT, LOW);
    } else {
      // 靜音回饋：雙腮紅短閃兩次 (安靜無聲)
      digitalWrite(LED_LEFT, HIGH); digitalWrite(LED_RIGHT, HIGH);
      delay(80);
      digitalWrite(LED_LEFT, LOW); digitalWrite(LED_RIGHT, LOW);
      delay(80);
      digitalWrite(LED_LEFT, HIGH); digitalWrite(LED_RIGHT, HIGH);
      delay(80);
      digitalWrite(LED_LEFT, LOW); digitalWrite(LED_RIGHT, LOW);
    }
    delay(40);
  }

  // SW2: 戳一下 (Poke / 摸頭) -> 害羞笑 + 顯示具體電量
  if (lastSw2 == HIGH && sw2 == LOW) {
    isAsleep = false;
    lastMotionTime = now;
    tempMoodUntil = 0;
    sndPoke();
    digitalWrite(LED_LEFT, HIGH);
    digitalWrite(LED_RIGHT, HIGH);
    display.clearDisplay();
    drawBatteryRing(batteryPercent);
    drawHappyEyes(EYE_LEFT_X, EYE_RIGHT_X, EYE_DEFAULT_Y, -4);

    // 在頭頂正中央顯示具體電量數字 (例如 "85%")
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(54, 6);
    display.print(batteryPercent);
    display.print("%");

    display.display();
    delay(400); // 停留一下讓使用者看清電量
    digitalWrite(LED_LEFT, (currentMood == MOOD_HAPPY) ? HIGH : LOW);
    digitalWrite(LED_RIGHT, (currentMood == MOOD_HAPPY) ? HIGH : LOW);
  }

  // SW3: 手動切換表情模式 (正常 -> 開心 -> 生氣 -> 驚訝 -> 暈眩 -> 想睡)
  if (lastSw3 == HIGH && sw3 == LOW) {
    tempMoodUntil = 0;
    isAsleep = false;
    lastMotionTime = now;
    currentMood = (MochiMood)((currentMood + 1) % MOOD_COUNT);

    switch (currentMood) {
      case MOOD_HAPPY:     sndHappy(); digitalWrite(LED_LEFT, HIGH); digitalWrite(LED_RIGHT, HIGH); break;
      case MOOD_ANGRY:     sndAngry(); digitalWrite(LED_LEFT, HIGH); digitalWrite(LED_RIGHT, LOW); break;
      case MOOD_SURPRISED: sndSurprised(); digitalWrite(LED_LEFT, HIGH); digitalWrite(LED_RIGHT, HIGH); break;
      case MOOD_DIZZY:     sndDizzy(); break;
      default:
        sndPoke();
        digitalWrite(LED_LEFT, LOW);
        digitalWrite(LED_RIGHT, LOW);
        break;
    }
    delay(40);
  }

  lastSw1 = sw1;
  lastSw2 = sw2;
  lastSw3 = sw3;

  // --- 5. 決定當前活躍表情 ---
  static MochiMood lastActiveMood = MOOD_DEFAULT;
  MochiMood activeMood = currentMood;
  if (tempMoodUntil > now) {
    activeMood = tempMood;
  } else if (isAsleep) {
    activeMood = MOOD_SLEEPY;
  }

  // 若剛結束暈眩 (DIZZY)，強制熄滅雙頰 LED
  if (lastActiveMood == MOOD_DIZZY && activeMood != MOOD_DIZZY) {
    digitalWrite(LED_LEFT, LOW);
    digitalWrite(LED_RIGHT, LOW);
  }
  lastActiveMood = activeMood;

  // --- 6. 自然眨眼邏輯 ---
  if (activeMood == MOOD_DEFAULT && !isAsleep) {
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

  // 1. 繪製外圈電量框
  drawBatteryRing(batteryPercent);

  // 2. 繪製動態眼睛
  int lx = EYE_LEFT_X + (int)curGazeX;
  int rx = EYE_RIGHT_X + (int)curGazeX;
  int y  = EYE_DEFAULT_Y + (int)curGazeY;

  switch (activeMood) {
    case MOOD_DEFAULT:
      digitalWrite(LED_LEFT, LOW);
      digitalWrite(LED_RIGHT, LOW);
      drawNormalEyes(lx, rx, y, eyeW, eyeH);
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
      drawSurprisedEyes(EYE_LEFT_X, EYE_RIGHT_X, EYE_DEFAULT_Y, (int)curGazeX, (int)curGazeY);
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

  display.display();

  animFrame++;
  delay(35); // 約 30 FPS
}
