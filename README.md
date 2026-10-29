# watchX Mochi (Dasai Mochi Robot for watchX)

這是一個將 **watchX**（開源 ATmega32U4 穿戴式開發手錶）打造成類似 **Dasai Mochi** 桌面/車載萌系互動機器人的韌體專案。

擁有靈動的純粹像素圓角大眼、六軸感測器體感互動反應、對稱外圈電量框以及 8-bit 電子萌系音效。

---

## 🌟 核心功能特色

1. **Procedural 動態像素表情**：
   * **呆萌（Idle）**：自然隨機眨眼、東張西望。
   * **重力傾斜跟隨（Tilt-to-Gaze）**：手錶左右、前後傾斜時，眼睛順滑跟隨重力方向注視。
   * **開心（Happy）**：彎彎瞇眼笑（`^ ^`）帶微幅彈跳。
   * **生氣（Angry）**：斜角怒視眼（`> <`）帶發抖動畫。
   * **驚訝（Surprised）**：圓滾滾大眼（`O O`）帶亮點瞳孔。
   * **暈眩（Dizzy）**：叉叉眼（`X X`）左右晃動。
   * **打瞌睡（Sleepy）**：半閉眼縫（`- -`）帶升空漂浮的「Zzz」文字。

2. **MPU6050 體感互動**：
   * **劇烈左右搖晃**：觸發「暈眩（Dizzy）」模式，兩頰腮紅 LED 交替狂閃，伴隨下降音效。
   * **輕敲手錶 / 拍擊桌面**：觸發「嚇一跳（Surprised）」大眼與驚呼音。
   * **靜置 15 秒**：打呼嚕進入打瞌睡模式，拿起來或震動立刻醒來。

3. **外圈對稱電量邊框**：
   * 雙向對稱從頂部中心向兩側環繞，100% 滿電時底部閉合形成完整的 Cyberpunk 機甲外框。
   * 低於 15% 時呼吸閃爍警示。

4. **實體按鈕互動**：
   * **SW1 (Pin 8)**：聲音開關切換（開機預設為靜音，按一下開啟/關閉）。
   * **SW2 (Pin 11)**：戳一下（Poke）摸摸頭互動，害羞眨眼並在頭頂顯示目前精確電量數字（如 `85%`）。
   * **SW3 (Pin 10)**：手動循環切換各種表情。

---

## 🛠️ 硬體配置 (watchX Hardware Pinout)

* **MCU**：ATmega32U4 (Arduino Leonardo compatible)
* **OLED**：1.3 吋 128x64 SSD1306 (Hardware SPI)
  * `OLED_DC` = A3
  * `OLED_RESET` = A4
  * `OLED_CS` = A5
* **Buttons**：
  * `SW1` = Pin 8 (Sound Toggle)
  * `SW2` = Pin 11 (Poke / Battery Display)
  * `SW3` = Pin 10 (Mood Switch)
* **LEDs & Audio**：
  * `LED_LEFT` = Pin 13
  * `LED_RIGHT` = Pin 6
  * `BUZZER` = Pin 9
* **Sensors & Power**：
  * `MPU6050` = I2C 0x69 (or 0x68)
  * `BATTERY_POWER` = Pin 4 (Enable)
  * `BATTERY_READ` = Pin A11 (ADC)
  * `CHARGE_STATUS` = Pin 5

---

## 🚀 編譯與燒錄 (使用 arduino-cli)

```bash
# 編譯專案
arduino-cli compile --fqbn arduino:avr:leonardo watchx_mochi

# 燒錄進 watchX (連接埠依系統而異，Mac 通常為 /dev/cu.usbmodem*)
arduino-cli upload -p /dev/cu.usbmodem2101 --fqbn arduino:avr:leonardo watchx_mochi
```

