# UT37 腳位表（依 SCH_Schematic1_1_2026-08-30.pdf 逐腳核對）
ESP32-S3R8 (16MB flash / 8MB OPI PSRAM, GPIO33-37 被 PSRAM 佔用)

| 功能 | GPIO |
|---|---|
| 顯示 RST/BUSY/DC/CS/CLK/MOSI | 0/1/2/3/4/5 |
| I2C SCL/SDA（觸控 FT6336U、AHT30、RX8025T） | 12/13 |
| 觸控 INT | 21 |
| 觸控/SD 喚醒 (TOUCH_WAKE/SD_WAKE) | 42 |
| 電池 ADC (330k/100k 分壓, 比例 4.3) | 14 (ADC2) |
| 前光 PWM | 45 |
| 蜂鳴器 / PA_EN | 18 |
| 側鍵 SIDEKEY（RTC 可喚醒；SW2 接 3V3、R99 100k 下拉 → 高電位有效） | 17 |
| SIDEKEY1（僅網路標號，原理圖上無按鍵，韌體不使用） | 46 |
| SDMMC CLK/CMD | 38/39 |
| SDMMC D0/D1/D2/D3 | 47/48/41/40 |
| USB D-/D+ (原生 USB，勿他用) | 19/20 |
| I2S MCK/BCK/WS/DI/DO、ES7210_INT | 7/8/9/10/11、6（音訊，板上可能未焊） |
