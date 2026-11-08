#pragma once

// SoftAP name shown to phones during onboarding
#ifndef PP_SOFTAP_SSID
#define PP_SOFTAP_SSID "T-Print"
#endif

// LilyGO T-Embed CC1101 pin map
#define BOARD_USER_KEY 6
#define BOARD_PWR_EN 15

#define WS2812_NUM_LEDS 8
#define WS2812_DATA_PIN 14

#define BOARD_VOICE_BCLK 46
#define BOARD_VOICE_LRCLK 40
#define BOARD_VOICE_DIN 7

// Native panel memory is 170×320; runtime rotation 1/3 yields landscape 320×170.
#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 170

#define DISPLAY_BL 21
#define DISPLAY_CS 41
#define DISPLAY_MISO 10
#define DISPLAY_MOSI 9
#define DISPLAY_SCLK 11
#define DISPLAY_DC 16
#define DISPLAY_RST -1

#define ENCODER_INA 4
#define ENCODER_INB 5
#define ENCODER_KEY 0

#define BOARD_I2C_SDA 8
#define BOARD_I2C_SCL 18

#define BOARD_SPI_SCK 11
#define BOARD_SPI_MOSI 9
#define BOARD_SPI_MISO 10

#define BOARD_SD_CS 13
#define BOARD_CC1101_CS 12

#define BQ25896_ADDR 0x6B
#define BQ27220_ADDR 0x55