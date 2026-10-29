# watchX Mochi (Dasai Mochi Robot for watchX)

這是一個將 **watchX**（開源 ATmega32U4 穿戴式開發手錶）打造成類似 **Dasai Mochi** 桌面/車載萌系互動機器人的開源專案。

具備純粹極簡像素圓角大眼、六軸感測器體感互動、實體互動選單、對稱外圈電量框、8-bit 電子萌系音效，以及透過 **Nordic nRF51822 藍牙 BLE** 與地端大模型（**LM Studio**）即時聯動！

---

## 🌟 核心功能特色

1. **Procedural 動態像素表情**：
   * **呆萌（Idle）**：純淨飽滿圓潤大眼，自然隨機眨眼、東張西望。
   * **振筆疾書（Scribbling）**：專注斜眉、瞳孔飛速掃視、冒汗、底部高速書寫折線（LM Studio 模型推理時專屬動態）。
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

3. **實體按鍵互動選單系統**：
   * **SW1 (Pin 8)**：
     * 待機時：短按進入選單（`== MENU ==`）。
     * 選單內：確認 / 切換（True/False 切換）。
   * **SW2 (Pin 11)**：
     * 待機時：戳一下（Poke）摸摸頭互動，害羞笑臉並在頂部顯示即時電量百分比（如 `85%`）。
     * 選單內：選項「向上」移動。
   * **SW3 (Pin 10)**：
     * 待機時：手動循環切換各種表情。
     * 選單內：選項「向下」移動。
   * **自動閒置返回**：選單內 6 秒無操作自動返回待機表情。
   * **選單功能項**：
     1. `SYSTEM STATUS >`：即時檢視電池%、充電狀態、MPU6050 狀態與藍牙廣播狀態。
     2. `BLE : [ON] / [OFF]`：實體啟閉 Nordic nRF51822 藍牙晶片廣播。
     3. `SOUND : [ON] / [OFF]`：蜂鳴器音效開關（預設為靜音關閉）。

4. **藍牙 BLE 聯動（支援 LM Studio 地端大模型）**：
   * 藍牙晶片開機即以 **`Mochi-Robot`** 廣播。
   * 支援 Nordic UART 服務協議，可接收字元指令切換表情：
     * `'B'` 或 `'b'`：**Busy** -> 振筆疾書（LM Studio 正在生成答案）
     * `'D'` 或 `'d'`：**Done** -> 開心慶祝微笑（`^ ^`）並伴隨叮咚音效
     * `'I'` 或 `'i'`：**Idle** -> 回復普通呆萌待機
     * `'S'` 或 `'s'`：**Sleep** -> 進入睡眠（Zzz）

---

## 💻 與 LM Studio 聯動方式

### 方法 A：使用網頁控制器（推薦，零安裝免環境）
1. 用 **Chrome** 或 **Edge** 瀏覽器開啟專案內的 [`mochi_web_controller.html`](file:///Users/da042300068/workspace/watchx/watchx_mochi/mochi_web_controller.html)。
2. 點擊「**掃描並連線 Mochi-Robot**」，在系統彈出視窗中選取你的 Mochi。
3. 即可在網頁上手動點擊表情按鈕，或輸入問題直接送出給 LM Studio（`http://localhost:1234`），Mochi 會即時進入振筆疾書並於回答完成時開心微笑！

### 方法 B：使用 Python 代理伺服器
1. 確保已安裝依賴：`pip3 install bleak aiohttp`
2. 啟動 LM Studio 的 Local Server（預設埠號 1234）。
3. 執行代理伺服器：
   ```bash
   python3 watchx_mochi/lmstudio_proxy.py
   ```
4. 將您的前端軟體（例如 OpenWebUI、Chatbox 或 IDE 外掛）之 OpenAI API Endpoint 指向 `http://127.0.0.1:1235/v1`。
5. 只要模型開始推理生成，Mochi 便會自動進入「振筆疾書」！

---

## 🛠️ 硬體腳位配置 (watchX Hardware Pinout)

* **MCU**：ATmega32U4 (Arduino Leonardo compatible)
* **OLED**：1.3 吋 128x64 SSD1306 (Hardware SPI)
  * `OLED_DC` = A3
  * `OLED_RESET` = A4
  * `OLED_CS` = A5
* **Bluetooth BLE**：Nordic nRF51822 (Hardware SPI)
  * `BLE_CS` = A2
  * `BLE_IRQ` = Pin 0
  * `BLE_RST` = A1
* **Buttons**：
  * `SW1` = Pin 8 (Menu / Select / Toggle)
  * `SW2` = Pin 11 (Menu Up / Poke & Battery)
  * `SW3` = Pin 10 (Menu Down / Manual Mood)
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

## 🚀 編譯與燒錄

```bash
# 編譯專案 (僅佔用 84% Flash，釋放 4KB+ 記憶體空間)
arduino-cli compile --fqbn arduino:avr:leonardo watchx_mochi

# 燒錄進 watchX
arduino-cli upload -p /dev/cu.usbmodem2101 --fqbn arduino:avr:leonardo watchx_mochi
```
