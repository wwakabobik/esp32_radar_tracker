#pragma once

// Pin map: buttons confirmed on hardware via GPIO listen (2026-06-15).
// Btn1=GPIO18, Btn2=GPIO5 (active LOW, confirmed via button_listen).

#ifndef PIN_RADAR_RX
#define PIN_RADAR_RX 16
#endif
#ifndef PIN_RADAR_TX
#define PIN_RADAR_TX 17
#endif

#ifndef PIN_I2C_SDA
#define PIN_I2C_SDA 21
#endif
#ifndef PIN_I2C_SCL
#define PIN_I2C_SCL 22
#endif

#ifndef PIN_BTN_1
#define PIN_BTN_1 18
#endif
#ifndef PIN_BTN_2
#define PIN_BTN_2 5
#endif

#define OLED_I2C_ADDR 0x3C
