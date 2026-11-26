#include <Arduino.h>
#include <vector>
#include "Config.h"
#include "LGFX_Config.h"
#include "LVGL_Config.h"
#include "VietnameseFonts.h"
#include "NavigationManagerLVGL.h"
#include "BLEStatusOverlay.h"

// ===== CONFIG =====
namespace Config {
  // Hiển thị
  constexpr uint8_t DISPLAY_ROTATION = 0;  // Điều chỉnh rotation cho ST7789 1.3"
  constexpr uint8_t FRAME_DELAY_MS = 100;
  constexpr unsigned long NAV_IDLE_TIMEOUT_MS = 10000;
  
  // GPIO và cảm biến
  constexpr uint8_t BUTTON_PIN = 1;
  constexpr uint8_t DEBOUNCE_TIME_MS = 15;
  constexpr uint16_t HOLDING_TIME_MS = 400;
  
  // Thêm cấu hình cho việc tách biệt input processing
  constexpr bool PRIORITIZE_INPUT = true;
  constexpr unsigned long INPUT_CHECK_INTERVAL = 5;
  
  // LVGL update interval
  constexpr uint16_t LVGL_UPDATE_INTERVAL = 5; // ms
}

// ===== CÁC KIỂU DỮ LIỆU =====
typedef struct _VideoInfo {
  const uint8_t* const* frames;
  const uint16_t* frames_size;
  uint16_t num_frames;
  uint8_t audio_idx;
} VideoInfo;

// ===== KHAI BÁO VIDEOS =====
#include "full1.h"

// ===== BUTTON STATE MACHINE =====
enum class ButtonState {
  IDLE,
  PRESSED,
  HELD,
  RELEASED_SHORT,
  RELEASED_HOLD
};

// ===== PLAYER MODE =====
enum class PlayerMode {
  STOPPED,
  PLAYING,
  PAUSED,
  NAVIGATING,
  HOME          // tampilkan jam besar + kotak musik (tanpa video)
};

// ===== INPUT MANAGER =====
class InputManager {
private:
  ButtonState _buttonState = ButtonState::IDLE;
  unsigned long _buttonPressTime = 0;
  unsigned long _lastButtonReleaseTime = 0;
  int _lastReading = HIGH;
  unsigned long _lastDebounceTime = 0;
  unsigned long _lastUpdateTime = 0;
  
  // Trạng thái nút vật lý
  bool _physicalButtonState = false; // LOW (nhấn) = true, HIGH (nhả) = false
  
public:
  void init() {
    pinMode(Config::BUTTON_PIN, INPUT_PULLUP);
    _lastUpdateTime = millis();
  }
  
  // Cập nhật nhanh - chỉ đọc trạng thái nút
  void quickUpdate() {
    unsigned long now = millis();
    if (now - _lastUpdateTime >= Config::INPUT_CHECK_INTERVAL) {
      _lastUpdateTime = now;
      
      // Đọc trạng thái nút vật lý (đảo ngược vì INPUT_PULLUP)
      bool newButtonState = (digitalRead(Config::BUTTON_PIN) == LOW);
      
      // Nếu trạng thái thay đổi, cập nhật thời gian debounce
      if (newButtonState != _physicalButtonState) {
        _lastDebounceTime = now;
      }
      _physicalButtonState = newButtonState;
    }
  }
  
  // Cập nhật đầy đủ - xử lý state machine
  void update() {
    quickUpdate(); // Đảm bảo có dữ liệu mới nhất
    processButton();
  }
  
  ButtonState getButtonState() {
    ButtonState currentState = _buttonState;
    
    // Reset transient states
    if (_buttonState == ButtonState::RELEASED_SHORT || _buttonState == ButtonState::RELEASED_HOLD) {
      _buttonState = ButtonState::IDLE;
    }
    
    return currentState;
  }

private:
  // Xử lý trạng thái nút dựa trên _physicalButtonState
  void processButton() {
    unsigned long now = millis();
    
    // Chỉ xử lý sau khi vượt qua thời gian debounce
    if (now - _lastDebounceTime <= Config::DEBOUNCE_TIME_MS) {
      return;
    }
    
    // Xử lý trạng thái nút dựa trên _physicalButtonState
    if (_physicalButtonState) {  // Nút đang được nhấn
      if (_buttonState == ButtonState::IDLE) {
        _buttonState = ButtonState::PRESSED;
        _buttonPressTime = now;
        Serial.println("Button pressed");
      }
      else if (_buttonState == ButtonState::PRESSED && 
               now - _buttonPressTime >= Config::HOLDING_TIME_MS) {
        _buttonState = ButtonState::HELD;
        Serial.println("Button hold detected");
      }
    }
    else {  // Nút đang được nhả
      if (_buttonState == ButtonState::PRESSED) {
        if (now - _buttonPressTime < Config::HOLDING_TIME_MS) {
          _buttonState = ButtonState::RELEASED_SHORT;
          _lastButtonReleaseTime = now;
          Serial.println("Button released (short press)");
        } else {
          _buttonState = ButtonState::RELEASED_HOLD;
          _lastButtonReleaseTime = now;
          Serial.println("Button released (after hold)");
        }
      }
      else if (_buttonState == ButtonState::HELD) {
        _buttonState = ButtonState::RELEASED_HOLD;
        _lastButtonReleaseTime = now;
        Serial.println("Button released (after hold)");
      }
    }
  }

public:
  static InputManager& getInstance() {
    static InputManager instance;
    return instance;
  }
};

// ===== VIDEO PLAYER =====
class VideoPlayer {
private:
  std::vector<VideoInfo*> _videos;
  uint8_t _currentIndex = 0;
  uint16_t _currentFrame = 0;
  PlayerMode _currentMode = PlayerMode::STOPPED;
  unsigned long _lastFrameTime = 0;
  bool _navAlertShown = false;
  unsigned long _navIdleStartTime = 0;
  bool _navIdleTimerActive = false;

  // Siklus IDLE_VIDEO (1 putaran) <-> HOME (15 detik) saat musik aktif
  unsigned long _homeStartTime = 0;
  bool _homeTimerActive = false;
  bool _cycleJustCompleted = false;
  
public:
  VideoPlayer() {
    // Video idle tunggal (full1.h), diputar berulang
    _videos = {
      &full1
    };
    
    // Bắt đầu với video đầu tiên
    _currentIndex = 0;
    
    Serial.printf("Loaded %d video files\n", (int)_videos.size());
    for(int i = 0; i < _videos.size(); i++) {
      Serial.printf("Video %d: %d frames, audio track %d\n", 
                   i, _videos[i]->num_frames, _videos[i]->audio_idx);
    }
  }
  
  void init() {
    // Khởi tạo với màn hình đen, sau đó tự động bắt đầu phát video
    LGFX_Device* tft = LVGL_Display::getInstance().getTft();
    tft->fillScreen(TFT_BLACK);
    
    // Tự động bắt đầu phát video ngay khi khởi động
    _currentMode = PlayerMode::PLAYING;
    _currentIndex = 0;
    _currentFrame = 0;
    _lastFrameTime = millis();
    LVGL_Display::getInstance().setBacklight(true);
    
    Serial.println("Player initialized and automatically started playback");
  }
  
  void update() {
    // Đảm bảo luôn cập nhật nhanh đầu vào trước khi làm bất cứ việc gì khác
    InputManager::getInstance().quickUpdate();
    
    // Kiểm tra trạng thái điều hướng
    checkNavigationMode();
    
    // Nếu đang ở chế độ điều hướng toàn màn hình, ưu tiên hiển thị điều hướng
    if (_currentMode == PlayerMode::NAVIGATING) {
      return;
    }
    
    // Xử lý button input
    handleButtonInput();
    
    // Chỉ cập nhật frame nếu đang ở chế độ PLAYING
    if (_currentMode == PlayerMode::PLAYING) {
      unsigned long currentTime = millis();
      
      if (currentTime - _lastFrameTime >= Config::FRAME_DELAY_MS) {
        _lastFrameTime = currentTime;
        
        // Hiển thị frame hiện tại
        LGFX_Device* tft = LVGL_Display::getInstance().getTft();
        const uint8_t* jpg_data = (const uint8_t*)pgm_read_ptr(&getCurrentVideo()->frames[_currentFrame]);
        uint16_t jpg_size = pgm_read_word(&getCurrentVideo()->frames_size[_currentFrame]);
        uint8_t dbgVideo = _currentIndex;
        uint16_t dbgFrame = _currentFrame;
        bool ok = tft->drawJpg(jpg_data, jpg_size, 0, 0);
        if (!ok || dbgFrame < 2) {
          Serial.printf("V%u F%u size=%u ok=%d head=%02X%02X%02X%02X\n",
                        dbgVideo, dbgFrame, jpg_size, ok ? 1 : 0,
                        jpg_data[0], jpg_data[1], jpg_data[2], jpg_data[3]);
        }
        
        // Tăng chỉ số frame
        _currentFrame++;
        if (_currentFrame >= getCurrentVideo()->num_frames) {
          // Chuyển sang video tiếp theo, hết danh sách thì quay lại video đầu
          _currentFrame = 0;
          _currentIndex = (_currentIndex + 1) % _videos.size();
          _cycleJustCompleted = true;

          // Satu siklus penuh selesai. Bila ada musik aktif (dan tanpa
          // navigasi), pindah ke HOME tanpa memotong video.
          bool navActive = ChronosManager::getInstance().isNavigating();
          bool musicActive = hasActiveMusic();
          Serial.printf("[STATE] idle: hasMusic=%d navActive=%d cycleDone=%d\n",
                        musicActive ? 1 : 0, navActive ? 1 : 0, 1);
          if (_currentMode == PlayerMode::PLAYING && musicActive && !navActive) {
            enterHome("idle video cycle complete");
          }
        }
        
        // Cập nhật nhanh input sau khi vẽ frame
        InputManager::getInstance().quickUpdate();
      }
    }
  }
  
  // Đặt chế độ phát hiện tại
  void setMode(PlayerMode mode) {
    if (_currentMode != mode) {
      Serial.printf("Changing player mode from %d to %d\n", (int)_currentMode, (int)mode);
      _currentMode = mode;
      
      if (mode == PlayerMode::STOPPED) {
        clearScreen(); // Xóa màn hình về màu đen
      } else if (mode == PlayerMode::PLAYING || mode == PlayerMode::HOME) {
        LVGL_Display::getInstance().setBacklight(true);
      } else if (mode == PlayerMode::NAVIGATING) {
        // Xóa màn hình về màu đen trước khi chuyển sang màn hình navigation
        LGFX_Device* tft = LVGL_Display::getInstance().getTft();
        tft->fillScreen(TFT_BLACK);
        
        // Đảm bảo backlight bật
        LVGL_Display::getInstance().setBacklight(true);
        
        // Đảm bảo LVGL được cập nhật
        LVGL_Display::getInstance().update();
      }
    }
  }
  
  // Lấy chế độ phát hiện tại
  PlayerMode getMode() {
    return _currentMode;
  }
  
private:
  // Sumber status musik (sama dengan UI): judul ada & tidak pause.
  bool hasActiveMusic() {
    return ChronosManager::getInstance().hasActiveMusic();
  }

  // Pastikan layar navigasi dalam mode FULLSCREEN.
  void ensureFullscreen(NavigationManagerLVGL& navManager) {
    if (navManager.getNavigationMode() == NavigationMode::NAV_DISABLED) {
      navManager.toggleNavigationMode(); // NAV_DISABLED -> BACKGROUND
    }
    if (navManager.getNavigationMode() == NavigationMode::BACKGROUND) {
      navManager.toggleNavigationMode(); // BACKGROUND -> FULLSCREEN
    }
  }

  // Masuk ke HOME (jam besar + kotak musik) dan tampilkan layar navigasi.
  void enterHome(const char* reason) {
    PlayerMode prev = _currentMode;
    _homeStartTime = millis();
    _homeTimerActive = true;
    _cycleJustCompleted = false;

    auto& navManager = NavigationManagerLVGL::getInstance();
    navManager.setHomeMode(true);
    navManager.setDisplaySuspended(false);
    ensureFullscreen(navManager);
    setMode(PlayerMode::HOME);
    navManager.drawFullscreenNavigation(ChronosManager::getInstance().getNavData());

    Serial.printf("[STATE] %d -> HOME (%s), tampil %lus\n", (int)prev, reason,
                  (unsigned long)(Config::HOME_DISPLAY_MS / 1000));
  }

  // Masuk mode video idle (loop 1 siklus).
  void enterVideoLoop(const char* reason) {
    PlayerMode prev = _currentMode;
    _homeTimerActive = false;
    _cycleJustCompleted = false;

    auto& navManager = NavigationManagerLVGL::getInstance();
    navManager.setHomeMode(false);
    NavigationManagerLVGL::getInstance().setDisplaySuspended(true);

    if (_currentMode != PlayerMode::PLAYING) {
      setMode(PlayerMode::PLAYING);
      Serial.printf("[STATE] %d -> PLAYING/IDLE_VIDEO (%s)\n", (int)prev, reason);
    }
  }

  // Kiểm tra trạng thái điều hướng + musik, kelola transisi state.
  void checkNavigationMode() {
    static unsigned long lastCheckTime = 0;

    // Chỉ kiểm tra mỗi 500ms
    unsigned long currentTime = millis();
    if (currentTime - lastCheckTime < 500) {
      return;
    }
    lastCheckTime = currentTime;

    // Kiểm tra một cách an toàn - sử dụng biến tĩnh từ loop()
    extern bool navigationInitialized;
    if (!navigationInitialized) {
      return; // Nếu chưa khởi tạo, không làm gì cả
    }

    auto& navManager = NavigationManagerLVGL::getInstance();
    auto& chronosManager = ChronosManager::getInstance();

    bool isConnectedToBLE = chronosManager.isConnected();
    bool isNavigationActive = chronosManager.isNavigating();
    bool musicActive = chronosManager.hasActiveMusic();

    // In debug mỗi 10 giây để xác nhận trạng thái
    static unsigned long lastDebugTime = 0;
    if (currentTime - lastDebugTime > 10000) {
      lastDebugTime = currentTime;
      Serial.printf("BLE: %s, Nav: %s, Music: %s, NavMode: %d, PlayerMode: %d\n",
                    isConnectedToBLE ? "Connected" : "Disconnected",
                    isNavigationActive ? "Active" : "Inactive",
                    musicActive ? "Playing" : "None",
                    (int)navManager.getNavigationMode(),
                    (int)_currentMode);
    }

    // ---- BLE terputus: video idle loop biasa ----
    if (!isConnectedToBLE) {
      _navIdleTimerActive = false;
      _homeTimerActive = false;
      _cycleJustCompleted = false;

      if (_currentMode != PlayerMode::PLAYING) {
        setMode(PlayerMode::PLAYING);
        Serial.println("State: BLE terputus -> PLAYING/IDLE_VIDEO");
      }

      if (navManager.getNavigationMode() != NavigationMode::NAV_DISABLED) {
        navManager.toggleNavigationMode();
      }
      navManager.setDisplaySuspended(true);
      return;
    }

    // ---- Navigasi aktif: prioritas tertinggi, batalkan semua timer ----
    if (isNavigationActive) {
      _navIdleTimerActive = false;
      _homeTimerActive = false;
      _cycleJustCompleted = false;

      if (_currentMode != PlayerMode::NAVIGATING) {
        setMode(PlayerMode::NAVIGATING);
        Serial.println("[STATE] -> NAVIGATING (navigasi aktif)");
      }

      navManager.setHomeMode(false);
      navManager.setDisplaySuspended(false);
      ensureFullscreen(navManager);
      navManager.drawFullscreenNavigation(chronosManager.getNavData());
      return;
    }

    // ---- Navigasi tidak aktif + musik aktif: siklus video <-> HOME ----
    if (musicActive) {
      _navIdleTimerActive = false;

      if (_currentMode == PlayerMode::HOME) {
        // Ganti lagu tidak me-reset timer; cukup refresh teks & layar.
        navManager.setDisplaySuspended(false);
        ensureFullscreen(navManager);
        navManager.drawFullscreenNavigation(chronosManager.getNavData());

        if (_homeTimerActive && currentTime - _homeStartTime >= Config::HOME_DISPLAY_MS) {
          enterVideoLoop("HOME 15s selesai");
        }
        return;
      }

      if (_currentMode != PlayerMode::PLAYING) {
        // Sedang di layar tunggu / state lain: mulai siklus video dari awal.
        enterVideoLoop("musik aktif, mulai siklus video");
      } else {
        navManager.setDisplaySuspended(true);
      }
      return;
    }

    // ---- Navigasi tidak aktif + tidak ada musik: perilaku lama ----
    _homeTimerActive = false;
    _cycleJustCompleted = false;

    if (_currentMode == PlayerMode::HOME) {
      // Musik berhenti saat di HOME: segera kembali ke video.
      enterVideoLoop("musik berhenti saat di HOME");
      return;
    }

    if (!_navIdleTimerActive) {
      _navIdleTimerActive = true;
      _navIdleStartTime = currentTime;

      if (_currentMode != PlayerMode::NAVIGATING) {
        setMode(PlayerMode::NAVIGATING);
      }

      // Layar tunggu: tampilkan HOME (jam besar) tanpa musik bila belum ada.
      navManager.setHomeMode(true);
      navManager.setDisplaySuspended(false);
      ensureFullscreen(navManager);
      navManager.drawFullscreenNavigation(chronosManager.getNavData());

      Serial.println("State: BLE terhubung, menunggu navigasi maks 10s (HOME)");
    } else if (currentTime - _navIdleStartTime >= Config::NAV_IDLE_TIMEOUT_MS) {
      if (_currentMode != PlayerMode::PLAYING) {
        setMode(PlayerMode::PLAYING);
        Serial.println("State: tidak ada navigasi dalam 10s -> PLAYING/IDLE_VIDEO");
      }
      navManager.setDisplaySuspended(true);
    } else {
      // Masih menunggu: pastikan layar tunggu (HOME) tetap tampil.
      if (_currentMode == PlayerMode::NAVIGATING) {
        navManager.setHomeMode(true);
        navManager.setDisplaySuspended(false);
        ensureFullscreen(navManager);
        navManager.drawFullscreenNavigation(chronosManager.getNavData());
      }
    }
  }
  
  // Xử lý button input
  void handleButtonInput() {
    auto& inputManager = InputManager::getInstance();
    ButtonState buttonState = inputManager.getButtonState();
    
    // Không còn logic xử lý nút nhấn để chuyển đổi chế độ điều hướng
    // Điều hướng hoạt động hoàn toàn dựa vào trạng thái kết nối Bluetooth
  }
  
  // Lấy video hiện tại
  VideoInfo* getCurrentVideo() const {
    return _videos[_currentIndex];
  }
  
  // Xóa màn hình về màu đen và tắt đèn nền
  void clearScreen() {
    LGFX_Device* tft = LVGL_Display::getInstance().getTft();
    tft->fillScreen(TFT_BLACK);
    LVGL_Display::getInstance().setBacklight(false);
  }

public:
  static VideoPlayer& getInstance() {
    static VideoPlayer instance;
    return instance;
  }
};

// Biến toàn cục để theo dõi trạng thái khởi tạo Navigation
bool navigationInitialized = false;

// ===== PROGRAM ENTRY POINTS =====
void setup() {
  Serial.begin(115200);
  Serial.println("Initializing...");
  
  // Khởi tạo LVGL Display
  LVGL_Display::getInstance().init();
  
  // Khởi tạo font tiếng Việt
  VietnameseFonts::init();
  
  Serial.println("Display and fonts initialized");
  
  // Khởi tạo Input Manager
  InputManager::getInstance().init();
  
  
  // Khởi tạo video player
  VideoPlayer::getInstance().init();
  
  Serial.println("Initialization complete - system ready");
}

void loop() {
  // Khởi tạo Navigation Manager sau 2 giây và chỉ một lần
  static unsigned long startTime = millis();
  
  if (!navigationInitialized && Config::NAVIGATION_ENABLED && (millis() - startTime >= 2000)) {
    NavigationManagerLVGL::getInstance().init(&LVGL_Display::getInstance());
    BLEStatusOverlay::getInstance().init(LVGL_Display::getInstance().getTft());
    Serial.println("Navigation Manager initialized after 2 seconds");
    
    // Cập nhật ngay lập tức để tạo UI
    LVGL_Display::getInstance().update();
    NavigationManagerLVGL::getInstance().update();
    BLEStatusOverlay::getInstance().update();
    
    // Đảm bảo màn hình được hiển thị
    LGFX_Device* tft = LVGL_Display::getInstance().getTft();
    tft->display();
    
    navigationInitialized = true;
  }
  
  // Cập nhật LVGL
  static unsigned long lastLvglUpdate = 0;
  unsigned long currentTime = millis();
  if (currentTime - lastLvglUpdate >= Config::LVGL_UPDATE_INTERVAL) {
    lastLvglUpdate = currentTime;
    LVGL_Display::getInstance().update();
  }
  
  // Cập nhật input
  InputManager::getInstance().quickUpdate();
  InputManager::getInstance().update();
  
  if(navigationInitialized)
  {
    // Cập nhật ChronosManager để xử lý kết nối BLE
    ChronosManager::getInstance().update();
    
    PlayerMode currentMode = VideoPlayer::getInstance().getMode();
    
    // Cập nhật NavigationManagerLVGL để xử lý trạng thái kết nối
    NavigationManagerLVGL::getInstance().update();
    
    // Cập nhật thông báo trạng thái BLE
    BLEStatusOverlay::getInstance().update();
    
    // Nếu đang ở chế độ điều hướng, dành nhiều thời gian hơn cho LVGL
    if (currentMode == PlayerMode::NAVIGATING) {
      // Đảm bảo LVGL được cập nhật nhiều lần
      for (int i = 0; i < 3; i++) {
        LVGL_Display::getInstance().update();
        delay(5); // Dành thêm thời gian cho LVGL xử lý
      }
    }
  }
  
  // Cập nhật video player
  VideoPlayer::getInstance().update();
}
