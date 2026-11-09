// Konfiguracja LovyanGFX: ST7796 (SPI) + XPT2046 na wspólnej magistrali.
#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "config.h"

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796  _panel;
  lgfx::Bus_SPI       _bus;
  lgfx::Light_PWM     _light;
  lgfx::Touch_XPT2046 _touch;

public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host    = SPI2_HOST;
      cfg.spi_mode    = 0;
      cfg.freq_write  = 40000000;   // przy zakłóceniach obniż do 27 MHz
      cfg.freq_read   = 16000000;
      cfg.spi_3wire   = false;
      cfg.use_lock    = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk    = PIN_SPI_SCK;
      cfg.pin_mosi    = PIN_SPI_MOSI;
      cfg.pin_miso    = PIN_SPI_MISO;
      cfg.pin_dc      = PIN_TFT_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs          = PIN_TFT_CS;
      cfg.pin_rst         = PIN_TFT_RST;
      cfg.pin_busy        = -1;
      cfg.panel_width     = 320;
      cfg.panel_height    = 480;
      cfg.offset_x        = 0;
      cfg.offset_y        = 0;
      cfg.offset_rotation = 0;
      cfg.readable        = false;  // SDO wyświetlacza nie jest podłączone
      cfg.invert          = false;  // jeśli kolory są w negatywie -> true
      cfg.rgb_order       = false;  // jeśli zamienione R i B -> true
      cfg.dlen_16bit      = false;
      cfg.bus_shared      = true;   // magistrala współdzielona z dotykiem
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl      = PIN_TFT_BL;
      cfg.invert      = false;
      cfg.freq        = 12000;
      cfg.pwm_channel = 1;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    {
      auto cfg = _touch.config();
      cfg.x_min           = 300;
      cfg.x_max           = 3900;
      cfg.y_min           = 200;
      cfg.y_max           = 3900;
      cfg.pin_int         = -1;
      cfg.bus_shared      = true;
      cfg.offset_rotation = 0;
      cfg.spi_host        = SPI2_HOST;
      cfg.freq            = 1000000;
      cfg.pin_sclk        = PIN_SPI_SCK;
      cfg.pin_mosi        = PIN_SPI_MOSI;
      cfg.pin_miso        = PIN_SPI_MISO;
      cfg.pin_cs          = PIN_TOUCH_CS;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

extern LGFX lcd;
