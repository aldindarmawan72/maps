#ifndef NAVIGATION_SCREEN_LVGL_H
#define NAVIGATION_SCREEN_LVGL_H

#include <Arduino.h>
#include <lvgl.h>
#include "LVGL_Config.h"
#include "NavColors.h"
#include "VietnameseFonts.h"
#include "ChronosTypes.h"
#include "ChronosManager.h"
#include "ESP32Time.h"
#include "bg.h"

// ICON_DATA_SIZE berasal dari ChronosESP32Patched.h (48x48 pixels, 1 bit per
// pixel = 288 bytes) dan sudah tersedia lewat include ChronosManager.h.

// ============================================================================
// Navigation HUD (240x240)
// ----------------------------------------------------------------------------
//  ┌──────────────────────────────────────────┐  0
//  │ 12:40                          Tiba 1:58 │  header (38 px)
//  ├──────────────────────────────────────────┤
//  │ ┌────┐  Jl. Pahlawan II                  │  44
//  │ │icon│                                   │
//  │ └────┘                                   │
//  │  Belok kanan ke Jl. Pahlawan II          │
//  │                                          │
//  └──────────────────────────────────────────┘  196
//  ┌──────────────────────────────────────────┐  200
//  │     250 m       25 mnt      45 km/j      │  metrics (36 px)
//  └──────────────────────────────────────────┘  236
// ============================================================================

class NavigationScreenLVGL {
private:
    // ---- screen ----
    lv_obj_t* _screen = nullptr;
    lv_obj_t* _bgImage = nullptr;

    // ---- header ----
    lv_obj_t* _headerBar = nullptr;
    lv_obj_t* _timeLabel = nullptr;
    lv_obj_t* _etaLabel = nullptr;
    lv_obj_t* _headerDivider = nullptr;

    // ---- manoeuvre card ----
    lv_obj_t* _heroCard = nullptr;
    lv_obj_t* _iconTile = nullptr;
    lv_obj_t* _navIcon = nullptr;
    lv_obj_t* _titleLabel = nullptr;
    lv_obj_t* _directionLabel = nullptr;

    // ---- bottom metrics ----
    lv_obj_t* _metricsBar = nullptr;
    lv_obj_t* _distanceLabel = nullptr;
    lv_obj_t* _durationLabel = nullptr;
    lv_obj_t* _speedLabel = nullptr;

    // ---- idle state ----
    lv_obj_t* _idleCard = nullptr;
    lv_obj_t* _idleDot = nullptr;
    lv_obj_t* _idleTitle = nullptr;
    lv_obj_t* _defaultMessageLabel = nullptr;

    // ---- icon ----
    lv_img_dsc_t _navIconDesc = {};
    uint8_t* _iconData = nullptr;
    uint8_t _iconBuffer[ICON_DATA_SIZE];
    bool _hasValidIcon = false;
    uint32_t _iconCRC = 0;

    // ---- data ----
    AppNavigation _navData;
    bool _lastActiveState = false;

    // ------------------------------------------------------------------
    // Helpers
    // ------------------------------------------------------------------
    static void setVisible(lv_obj_t* obj, bool visible) {
        if (obj == nullptr) {
            return;
        }
        if (visible) {
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }

    static void setLabel(lv_obj_t* label, const char* text) {
        if (label == nullptr || text == nullptr) {
            return;
        }
        const char* current = lv_label_get_text(label);
        if (current == nullptr || strcmp(current, text) != 0) {
            lv_label_set_text(label, text);
        }
    }

    static void makeSurface(lv_obj_t* obj, uint32_t bgColor, lv_opa_t bgOpa,
                            lv_coord_t radius, uint32_t borderColor) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(bgColor), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(obj, bgOpa, LV_PART_MAIN);
        lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
        if (borderColor != 0) {
            lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
            lv_obj_set_style_border_color(obj, lv_color_hex(borderColor), LV_PART_MAIN);
            lv_obj_set_style_border_opa(obj, 255, LV_PART_MAIN);
        } else {
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
        }
        lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    }

    // Vẽ bitmap 1-bit của Chronos sebagai icon alpha (nền trong suốt).
    // Bit 1 = nét vẽ, bit 0 = trong suốt. Icon được tô trắng bằng recolor.
    void drawNavIconDirectly() {
        if (!_hasValidIcon || _iconData == nullptr || _iconTile == nullptr) {
            return;
        }

        if (_navIcon != nullptr) {
            lv_obj_del(_navIcon);
            _navIcon = nullptr;
        }

        _navIconDesc.header.cf = LV_IMG_CF_ALPHA_1BIT;
        _navIconDesc.header.always_zero = 0;
        _navIconDesc.header.reserved = 0;
        _navIconDesc.header.w = 48;
        _navIconDesc.header.h = 48;
        _navIconDesc.data_size = ICON_DATA_SIZE;
        _navIconDesc.data = _iconData;

        _navIcon = lv_img_create(_iconTile);
        lv_img_set_src(_navIcon, &_navIconDesc);
        lv_obj_set_style_img_recolor(_navIcon, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_img_recolor_opa(_navIcon, 255, LV_PART_MAIN);
        lv_obj_clear_flag(_navIcon, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_center(_navIcon);
    }

    // Atur font instruksi secara adaptif agar tetap terbaca di layar 240 px.
    void applyDirectionStyle(const String& direction) {
        if (_directionLabel == nullptr) {
            return;
        }

        const size_t len = direction.length();
        lv_font_t* font = VietnameseFonts::getSemiboldSmallFont();
        lv_label_long_mode_t mode = LV_LABEL_LONG_WRAP;

        if (len <= 18) {
            // Instruksi pendek ("Belok kiri") tampil besar dan tebal.
            font = VietnameseFonts::getBoldFont();
        } else if (len > 42) {
            // Instruksi sangat panjang: font lebih ramping + elipsis.
            font = VietnameseFonts::getNormalFont();
            mode = LV_LABEL_LONG_DOT;
        }

        lv_obj_set_style_text_font(_directionLabel, font, LV_PART_MAIN);
        lv_label_set_long_mode(_directionLabel, mode);
    }

    // Sesuaikan area teks dengan ada/tidaknya baris icon + nama jalan.
    void layoutHero() {
        if (_heroCard == nullptr || _directionLabel == nullptr) {
            return;
        }

        const bool hasTitle = (_navData.active && _navData.isNavigation &&
                               _navData.title.length() > 0);
        const bool hasTopRow = _hasValidIcon || hasTitle;

        if (_titleLabel != nullptr) {
            lv_obj_set_pos(_titleLabel, _hasValidIcon ? 68 : 12, 10);
            lv_obj_set_width(_titleLabel, _hasValidIcon ? 144 : 200);
        }

        lv_obj_set_pos(_directionLabel, 12, hasTopRow ? 58 : 14);
        lv_obj_set_height(_directionLabel, hasTopRow ? 93 : 124);
    }

    void resetPointers() {
        _bgImage = nullptr;
        _headerBar = nullptr;
        _timeLabel = nullptr;
        _etaLabel = nullptr;
        _headerDivider = nullptr;
        _heroCard = nullptr;
        _iconTile = nullptr;
        _navIcon = nullptr;
        _titleLabel = nullptr;
        _directionLabel = nullptr;
        _metricsBar = nullptr;
        _distanceLabel = nullptr;
        _durationLabel = nullptr;
        _speedLabel = nullptr;
        _idleCard = nullptr;
        _idleDot = nullptr;
        _idleTitle = nullptr;
        _defaultMessageLabel = nullptr;
    }

    // ------------------------------------------------------------------
    // Builders
    // ------------------------------------------------------------------
    void createHeader() {
        _headerBar = lv_obj_create(_screen);
        lv_obj_set_size(_headerBar, 240, 38);
        lv_obj_set_pos(_headerBar, 0, 0);
        lv_obj_set_style_bg_color(_headerBar, lv_color_hex(NavColors::BackgroundHex), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_headerBar, 242, LV_PART_MAIN);
        lv_obj_set_style_radius(_headerBar, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(_headerBar, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(_headerBar, 0, LV_PART_MAIN);
        lv_obj_clear_flag(_headerBar, LV_OBJ_FLAG_SCROLLABLE);

        _timeLabel = lv_label_create(_headerBar);
        lv_obj_set_style_text_font(_timeLabel, VietnameseFonts::getSemiboldSmallFont(), LV_PART_MAIN);
        lv_obj_set_style_text_color(_timeLabel, lv_color_hex(NavColors::TextPrimaryHex), LV_PART_MAIN);
        lv_obj_set_pos(_timeLabel, 10, 3);
        lv_label_set_text(_timeLabel, "--:--");

        _etaLabel = lv_label_create(_headerBar);
        lv_obj_set_style_text_font(_etaLabel, VietnameseFonts::getNormalFont(), LV_PART_MAIN);
        lv_obj_set_style_text_color(_etaLabel, lv_color_hex(NavColors::TextMutedHex), LV_PART_MAIN);
        lv_obj_set_width(_etaLabel, 150);
        lv_obj_set_style_text_align(_etaLabel, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_label_set_long_mode(_etaLabel, LV_LABEL_LONG_DOT);
        lv_obj_align(_etaLabel, LV_ALIGN_TOP_RIGHT, -10, 4);
        lv_label_set_text(_etaLabel, "");

        _headerDivider = lv_obj_create(_headerBar);
        lv_obj_set_size(_headerDivider, 240, 1);
        lv_obj_set_pos(_headerDivider, 0, 37);
        lv_obj_set_style_bg_color(_headerDivider, lv_color_hex(NavColors::BorderHex), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_headerDivider, 255, LV_PART_MAIN);
        lv_obj_set_style_radius(_headerDivider, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(_headerDivider, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(_headerDivider, 0, LV_PART_MAIN);
    }

    void createHeroCard() {
        _heroCard = lv_obj_create(_screen);
        lv_obj_set_size(_heroCard, 224, 152);
        lv_obj_set_pos(_heroCard, 8, 44);
        makeSurface(_heroCard, NavColors::SurfaceHex, 242, 22, NavColors::BorderHex);
        lv_obj_set_style_clip_corner(_heroCard, true, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(_heroCard, 14, LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(_heroCard, 60, LV_PART_MAIN);
        lv_obj_set_style_shadow_color(_heroCard, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_shadow_ofs_y(_heroCard, 5, LV_PART_MAIN);

        // Tile biru untuk icon manuver
        _iconTile = lv_obj_create(_heroCard);
        lv_obj_set_size(_iconTile, 48, 48);
        lv_obj_set_pos(_iconTile, 12, 6);
        lv_obj_set_style_bg_color(_iconTile, lv_color_hex(NavColors::AccentHex), LV_PART_MAIN);
        lv_obj_set_style_bg_grad_color(_iconTile, lv_color_hex(NavColors::AccentSoftHex), LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(_iconTile, LV_GRAD_DIR_VER, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_iconTile, 255, LV_PART_MAIN);
        lv_obj_set_style_radius(_iconTile, 15, LV_PART_MAIN);
        lv_obj_set_style_border_width(_iconTile, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(_iconTile, 0, LV_PART_MAIN);
        lv_obj_clear_flag(_iconTile, LV_OBJ_FLAG_SCROLLABLE);

        // Nama jalan / tujuan (sekunder)
        _titleLabel = lv_label_create(_heroCard);
        lv_obj_set_pos(_titleLabel, 68, 10);
        lv_obj_set_size(_titleLabel, 144, 31);
        lv_obj_set_style_text_font(_titleLabel, VietnameseFonts::getSemiboldSmallFont(), LV_PART_MAIN);
        lv_obj_set_style_text_color(_titleLabel, lv_color_hex(NavColors::TextMutedHex), LV_PART_MAIN);
        lv_label_set_long_mode(_titleLabel, LV_LABEL_LONG_DOT);
        lv_label_set_text(_titleLabel, "");

        // Instruksi manuver (utama)
        _directionLabel = lv_label_create(_heroCard);
        lv_obj_set_pos(_directionLabel, 12, 58);
        lv_obj_set_size(_directionLabel, 200, 93);
        lv_obj_set_style_text_font(_directionLabel, VietnameseFonts::getSemiboldSmallFont(), LV_PART_MAIN);
        lv_obj_set_style_text_color(_directionLabel, lv_color_hex(NavColors::TextPrimaryHex), LV_PART_MAIN);
        lv_obj_set_style_text_line_space(_directionLabel, 0, LV_PART_MAIN);
        lv_label_set_long_mode(_directionLabel, LV_LABEL_LONG_WRAP);
        lv_label_set_text(_directionLabel, "");

        if (_hasValidIcon) {
            drawNavIconDirectly();
        }
    }

    void createMetricsBar() {
        _metricsBar = lv_obj_create(_screen);
        lv_obj_set_size(_metricsBar, 224, 36);
        lv_obj_set_pos(_metricsBar, 8, 200);
        makeSurface(_metricsBar, NavColors::SurfaceHex, 235, 18, NavColors::BorderHex);
        lv_obj_set_layout(_metricsBar, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(_metricsBar, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(_metricsBar, LV_FLEX_ALIGN_SPACE_EVENLY,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(_metricsBar, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_left(_metricsBar, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_right(_metricsBar, 12, LV_PART_MAIN);

        _distanceLabel = lv_label_create(_metricsBar);
        lv_obj_set_style_text_font(_distanceLabel, VietnameseFonts::getSemiboldSmallFont(), LV_PART_MAIN);
        lv_obj_set_style_text_color(_distanceLabel, lv_color_hex(NavColors::DistanceHex), LV_PART_MAIN);
        lv_label_set_text(_distanceLabel, "");

        _durationLabel = lv_label_create(_metricsBar);
        lv_obj_set_style_text_font(_durationLabel, VietnameseFonts::getNormalFont(), LV_PART_MAIN);
        lv_obj_set_style_text_color(_durationLabel, lv_color_hex(NavColors::TextMutedHex), LV_PART_MAIN);
        lv_label_set_text(_durationLabel, "");

        _speedLabel = lv_label_create(_metricsBar);
        lv_obj_set_style_text_font(_speedLabel, VietnameseFonts::getNormalFont(), LV_PART_MAIN);
        lv_obj_set_style_text_color(_speedLabel, lv_color_hex(NavColors::TextFaintHex), LV_PART_MAIN);
        lv_label_set_text(_speedLabel, "");
    }

    void createIdleCard() {
        _idleCard = lv_obj_create(_screen);
        lv_obj_set_size(_idleCard, 224, 152);
        lv_obj_set_pos(_idleCard, 8, 44);
        makeSurface(_idleCard, NavColors::SurfaceHex, 242, 22, NavColors::BorderHex);
        lv_obj_set_style_clip_corner(_idleCard, true, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(_idleCard, 14, LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(_idleCard, 60, LV_PART_MAIN);
        lv_obj_set_style_shadow_color(_idleCard, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_shadow_ofs_y(_idleCard, 5, LV_PART_MAIN);

        _idleDot = lv_obj_create(_idleCard);
        lv_obj_set_size(_idleDot, 10, 10);
        lv_obj_set_pos(_idleDot, 16, 43);
        lv_obj_set_style_bg_color(_idleDot, lv_color_hex(NavColors::SuccessHex), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_idleDot, 255, LV_PART_MAIN);
        lv_obj_set_style_radius(_idleDot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_border_width(_idleDot, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(_idleDot, 0, LV_PART_MAIN);

        _idleTitle = lv_label_create(_idleCard);
        lv_obj_set_pos(_idleTitle, 36, 32);
        lv_obj_set_style_text_font(_idleTitle, VietnameseFonts::getSemiboldSmallFont(), LV_PART_MAIN);
        lv_obj_set_style_text_color(_idleTitle, lv_color_hex(NavColors::TextPrimaryHex), LV_PART_MAIN);
        lv_label_set_text(_idleTitle, "Navigasi");

        _defaultMessageLabel = lv_label_create(_idleCard);
        lv_obj_set_pos(_defaultMessageLabel, 16, 76);
        lv_obj_set_size(_defaultMessageLabel, 192, 58);
        lv_obj_set_style_text_font(_defaultMessageLabel, VietnameseFonts::getNormalFont(), LV_PART_MAIN);
        lv_obj_set_style_text_color(_defaultMessageLabel, lv_color_hex(NavColors::TextMutedHex), LV_PART_MAIN);
        lv_label_set_long_mode(_defaultMessageLabel, LV_LABEL_LONG_WRAP);
        lv_label_set_text(_defaultMessageLabel, "Mulai navigasi di Google Maps");
    }

public:
    NavigationScreenLVGL() : _iconData(_iconBuffer), _hasValidIcon(false), _iconCRC(0) {
        memset(_iconBuffer, 0, ICON_DATA_SIZE);
    }

    ~NavigationScreenLVGL() {
        // Dữ liệu pixel không còn là static buffer; descriptor hanya menunjuk
        // ke _iconBuffer milik objek ini.
        _navIconDesc.data = nullptr;

        if (_screen != nullptr) {
            lv_obj_del(_screen);
            _screen = nullptr;
        }
        resetPointers();
    }

    bool isScreenReady() {
        return _screen != nullptr;
    }

    // Tạo màn hình điều hướng
    void create() {
        VietnameseFonts::init();

        // Jangan hapus objek screen jika sedang aktif (LVGL akan mengosongkan
        // act_scr). Cukup bersihkan anak-anaknya lalu bangun ulang UI.
        if (_screen == nullptr) {
            _screen = lv_obj_create(NULL);
            if (_screen == nullptr) {
                Serial.println("ERROR: Failed to create navigation screen!");
                return;
            }
        } else {
            lv_obj_clean(_screen);
        }
        resetPointers();

        lv_obj_set_style_bg_color(_screen, lv_color_hex(NavColors::BackgroundHex), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_screen, 255, LV_PART_MAIN);
        lv_obj_clear_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

        // Gambar background peta
        _bgImage = lv_img_create(_screen);
        lv_img_set_src(_bgImage, &bg_img);
        lv_obj_align(_bgImage, LV_ALIGN_CENTER, 0, 0);
        lv_obj_clear_flag(_bgImage, LV_OBJ_FLAG_CLICKABLE);

        createHeader();
        createHeroCard();
        createMetricsBar();
        createIdleCard();

        updateLabels();

        Serial.println("NavigationScreenLVGL: Screen created successfully");
    }

    // Tampilkan màn hình
    void display() {
        if (_screen == nullptr) {
            Serial.println("ERROR: NavigationScreenLVGL - _screen is NULL, cannot display");
            return;
        }

        updateLabels();

        // Pastikan peta tetap di belakang semua komponen UI.
        if (_bgImage != nullptr) {
            lv_obj_move_background(_bgImage);
        }

        if (lv_scr_act() != _screen) {
            lv_scr_load(_screen);
        }

        // Render segera supaya perubahan terlihat tanpa menunggu tick LVGL.
        lv_refr_now(NULL);
    }

    // Cập nhật dữ liệu điều hướng
    void updateNavigation(const AppNavigation &navData) {
        const bool wasActive = _navData.active && _navData.isNavigation;
        const bool willBeActive = navData.active && navData.isNavigation;

        _navData = navData;
        _hasValidIcon = navData.hasIcon;

        if (_hasValidIcon &&
            (_iconCRC != navData.iconCRC || _navIcon == nullptr)) {
            memcpy(_iconBuffer, navData.icon, ICON_DATA_SIZE);
            _iconData = _iconBuffer;
            _iconCRC = navData.iconCRC;
            drawNavIconDirectly();
        } else if (!_hasValidIcon && _navIcon != nullptr) {
            lv_obj_del(_navIcon);
            _navIcon = nullptr;
        }

        updateLabels();

        if (wasActive != willBeActive) {
            Serial.printf("Navigation state: %s\n", willBeActive ? "active" : "inactive");
        }
    }

    // Cập nhật teks dan visibilitas seluruh komponen
    void updateLabels() {
        if (_screen == nullptr) {
            return;
        }

        const bool isActive = _navData.active && _navData.isNavigation;

        // ---- jam (kiri atas) ----
        if (_timeLabel != nullptr) {
            const int hour = ChronosManager::getInstance().getChronos().getHour();
            const int minute = ChronosManager::getInstance().getChronos().getMinute();
            char timeBuf[12];
            snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", hour, minute);
            setLabel(_timeLabel, timeBuf);
        }

        // ---- ETA (kanan atas) ----
        String eta = _navData.eta;
        if (eta == "Navigation") {
            eta = "";
        }
        const bool showEta = isActive && eta.length() > 0;
        setLabel(_etaLabel, showEta ? eta.c_str() : "");

        // ---- nama jalan / tujuan (sekunder) ----
        const bool showTitle = isActive && _navData.title.length() > 0;
        setLabel(_titleLabel, showTitle ? _navData.title.c_str() : "");

        // ---- instruksi manuver (utama) ----
        if (isActive) {
            String direction = _navData.directions;
            if (direction.length() == 0) {
                direction = "Lanjutkan perjalanan";
            }
            applyDirectionStyle(direction);
            setLabel(_directionLabel, direction.c_str());
        }

        // ---- metrik bawah ----
        const bool showDistance = isActive && _navData.distance.length() > 0;
        const bool showDuration = isActive && _navData.duration.length() > 0;
        const bool showSpeed = isActive && _navData.speed.length() > 0;

        setLabel(_distanceLabel, showDistance ? _navData.distance.c_str() : "");
        setLabel(_durationLabel, showDuration ? _navData.duration.c_str() : "");
        setLabel(_speedLabel, showSpeed ? _navData.speed.c_str() : "");

        // ---- visibilitas ----
        setVisible(_idleCard, !isActive);
        setVisible(_heroCard, isActive);
        setVisible(_metricsBar, isActive && (showDistance || showDuration || showSpeed));
        setVisible(_distanceLabel, showDistance);
        setVisible(_durationLabel, showDuration);
        setVisible(_speedLabel, showSpeed);
        setVisible(_etaLabel, showEta);
        setVisible(_titleLabel, showTitle);
        setVisible(_iconTile, isActive && _hasValidIcon);
        setVisible(_directionLabel, isActive);

        layoutHero();
    }

    // Dipertahankan sebagai API publik karena dipanggil NavigationManagerLVGL.
    void updateUIBasedOnNavigationState() {
        const bool isActive = _navData.active && _navData.isNavigation;
        updateLabels();

        if (_lastActiveState != isActive) {
            _lastActiveState = isActive;
            Serial.printf("Navigation UI state: %s\n", isActive ? "active" : "idle");
        }
    }
};

#endif // NAVIGATION_SCREEN_LVGL_H
