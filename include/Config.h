#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Shared configuration settings used by multiple files
namespace Config {
  // Navigation settings
  constexpr bool NAVIGATION_ENABLED = true;
  constexpr uint16_t NAV_UPDATE_INTERVAL = 2000; // ms - Tăng thời gian lên 2 giây để giảm tần suất cập nhật
  constexpr uint16_t NAV_ALERT_DISTANCE = 100;   // m
  
  // LVGL update interval
  constexpr uint16_t LVGL_UPDATE_INTERVAL = 20;  // ms

  // Idle-video <-> home cycle (only while music is playing and no navigation)
  constexpr unsigned long HOME_DISPLAY_MS = 15000; // 15 s HOME display per cycle
}

#endif // CONFIG_H
