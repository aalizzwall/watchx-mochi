#!/usr/bin/env python3
"""
LM Studio - Mochi Robot BLE 聯動代理伺服器
功能：
1. 建立本機 HTTP 代理 (預設埠號 1235) -> 轉發到 LM Studio (預設埠號 1234)
2. 當外部程式 (如 OpenWebUI、IDE、curl) 向代理發送模型推理請求時：
   - 立即發送 'B' (Busy) 給 watchX，Mochi 進入「振筆疾書」專注寫作表情
   - 推理完畢後，立即發送 'D' (Done) 給 watchX，Mochi 開心微笑 (^ ^) 並響起慶祝音效
   - 4 秒後自動發送 'I' (Idle) 回復正常待機表情
"""

import sys
import asyncio
import aiohttp
from aiohttp import web
from bleak import BleakScanner, BleakClient

# Nordic UART Service UUIDs
UART_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
UART_RX_CHAR_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e" # Write to watchX

LM_STUDIO_URL = "http://127.0.0.1:1234"
PROXY_PORT = 1235

ble_client = None

async def send_mochi_cmd(cmd: str):
    global ble_client
    if ble_client and ble_client.is_connected:
        try:
            await ble_client.write_gatt_char(UART_RX_CHAR_UUID, cmd.encode())
            print(f"[Mochi BLE] Sent command: '{cmd}'")
        except Exception as e:
            print(f"[Mochi BLE] Send error: {e}")
    else:
        print(f"[Mochi Simulation] Command: '{cmd}' (BLE not connected)")

async def handle_proxy(request: web.Request):
    path = request.match_info.get("tail", "")
    target_url = f"{LM_STUDIO_URL}/{path}"

    is_inference = "chat/completions" in path or "completions" in path

    if is_inference:
        print("\n[LM Studio] 檢測到模型推理請求，通知 Mochi 振筆疾書...")
        await send_mochi_cmd("B")

    body = await request.read()
    headers = {k: v for k, v in request.headers.items() if k.lower() not in ("host", "content-length")}

    try:
        async with aiohttp.ClientSession() as session:
            async with session.request(
                method=request.method,
                url=target_url,
                headers=headers,
                data=body,
                params=request.query
            ) as resp:
                response = web.StreamResponse(status=resp.status, headers=resp.headers)
                await response.prepare(request)

                async for chunk in resp.content.iter_any():
                    await response.write(chunk)

                if is_inference:
                    print("[LM Studio] 推理生成結束！Mochi 開心慶祝！")
                    await send_mochi_cmd("D")
                    # 4秒後回到呆萌待機
                    asyncio.create_task(reset_to_idle())

                return response

    except Exception as e:
        print(f"[Proxy Error] {e}")
        if is_inference:
            await send_mochi_cmd("I")
        return web.Response(status=502, text=f"LM Studio Proxy Error: {e}")

async def reset_to_idle():
    await asyncio.sleep(4.0)
    await send_mochi_cmd("I")

async def connect_ble():
    global ble_client
    print("\n🔍 正在搜尋 Mochi-Robot 藍牙裝置...")
    try:
        device = await BleakScanner.find_device_by_filter(
            lambda d, adv: d.name and any(p in d.name for p in ["Mochi", "watchX", "Adafruit"]),
            timeout=8.0
        )
        if not device:
            print("⚠️ 未找到 Mochi-Robot 藍牙裝置。請確認：")
            print("  1. watchX 電源已開啟")
            print("  2. 選單中 2. BLE 為 [ON]")
            print("  3. 亦可使用 mochi_web_controller.html 在 Chrome 中連線")
            return

        print(f"✅ 發現裝置: {device.name} ({device.address})，連線中...")
        ble_client = BleakClient(device)
        await ble_client.connect()
        print("🎉 藍牙連線成功！UART 通道已建立。")
    except Exception as e:
        print(f"藍牙連線提示: {e}")

async def main():
    app = web.Application()
    app.router.add_route("*", "/{tail:.*}", handle_proxy)
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, "127.0.0.1", PROXY_PORT)
    await site.start()
    print(f"\n🚀 LM Studio Mochi 代理已啟動於 http://127.0.0.1:{PROXY_PORT}")
    print(f"👉 請將客戶端 API URL 設定為 http://127.0.0.1:{PROXY_PORT}/v1")

    # 嘗試連接藍牙
    await connect_ble()

    try:
        while True:
            await asyncio.sleep(3600)
    except asyncio.CancelledError:
        pass
    finally:
        if ble_client and ble_client.is_connected:
            await ble_client.disconnect()
        await runner.cleanup()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n代理伺服器已安全停止。")
