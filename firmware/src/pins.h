#pragma once

// Pin map from firmware dump disassembly (ce436a87 analysis).
// Buttons 32/33 confirmed; UART 16/17 and I2C 21/22 likely (Arduino defaults).

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
#define PIN_BTN_1 32
#endif
#ifndef PIN_BTN_2
#define PIN_BTN_2 33
#endif

#define OLED_I2C_ADDR 0x3C
