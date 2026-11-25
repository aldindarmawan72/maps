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

// ICON_DATA_SIZE berasal dari ChronosESP32Patched.h (48x48 pixels, 1 bit per
// pixel = 288 bytes) dan sudah tersedia lewat include ChronosManager.h.

// ============================================================================
// Navigation HUD / HOME (240x240)
// ----------------------------------------------------------------------------
//  Header  : jam kiri (x10), dot BLE kanan (x222), divider y33
//  Kartu   : x8  y38  w224 h146  (radius 14)
//            icon 44x44 (12,4) + jarak (64,2)
//            instruksi 2 baris (12,48,200x62)
//            divider (12,112) + ETA marquee (12,114,200x29)
//  Musik   : x8  y188 w224 h40   (radius 12)  -> terpisah dari kartu
//  HOME    : idleCard sama area kartu + jam besar + kotak musik di bawah
// ============================================================================

class NavigationScreenLVGL {
private:
    // ---- screen ----
    lv_obj_t* _screen = nullptr;
    lv_obj_t* _bgImage = nullptr;

    // ---- header ----
    lv_obj_t* _headerBar = nullptr;
    lv_obj_t* _timeLabel = nullptr;
    lv_obj_t* _bleDot = nullptr;
    lv_obj_t* _headerDivider = nullptr;
    uint32_t _bleDotColor = 0;
    bool _bleDotInitialized = false;

    // ---- manoeuvre card ----
    lv_obj_t* _heroCard = nullptr;
    lv_obj_t* _iconTile = nullptr;
    lv_obj_t* _navIcon = nullptr;
    lv_obj_t* _distanceLabel = nullptr;
    lv_obj_t* _directionLabel = nullptr;
    lv_obj_t* _etaDivider = nullptr;
    lv_obj_t* _etaLabel = nullptr;

    // ---- now playing (kotak musik terpisah di bawah kartu) ----
    lv_obj_t* _musicBox = nullptr;
    lv_obj_t* _musicIcon = nullptr;
    lv_obj_t* _musicLabel = nullptr;
    bool _showMusic = false;
    bool _lastHasMusic = false;

    // ---- idle / HOME state ----
    lv_obj_t* _idleCard = nullptr;
    lv_obj_t* _idleTitle = nullptr;
    lv_obj_t* _idleClock = nullptr;
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

    // Marquee label dipakai bersama oleh ETA dan kotak musik.
    static lv_obj_t* createMarqueeLabel(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                                        lv_coord_t w, lv_coord_t h,
                                        lv_font_t* font, uint32_t color) {
        lv_obj_t* label = lv_label_create(parent);
        lv_obj_set_pos(label, x, y);
        lv_obj_set_size(label, w, h);
        lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
        lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
        lv_obj_set_style_anim_speed(label, 30, LV_PART_MAIN);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_label_set_text(label, "");
        return label;
    }

    // Sinkronkan teks marquee. Saat teks berubah, reset total animasi supaya
    // tidak ada sisa teks lama / dua animasi berjalan bersamaan.
    static void setMarqueeText(lv_obj_t* label, const char* text) {
        if (label == nullptr) {
            return;
        }
        if (text == nullptr) {
            text = "";
        }

        const char* current = lv_label_get_text(label);
        if (current != nullptr && strcmp(current, text) == 0) {
            return; // tidak berubah: biarkan animasi yang sudah ada
        }

        lv_anim_del(label, NULL);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_label_set_text(label, "");
        lv_label_set_text(label, text);

        const lv_font_t* font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
        lv_point_t textSize;
        lv_txt_get_size(&textSize, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        const lv_coord_t available = lv_obj_get_width(label);

        if (textSize.x > available) {
            lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        } else {
            lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        }

        lv_obj_invalidate(label);
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

    void resetPointers() {
        _bgImage = nullptr;
        _headerBar = nullptr;
        _timeLabel = nullptr;
        _bleDot = nullptr;
        _headerDivider = nullptr;
        _heroCard = nullptr;
        _iconTile = nullptr;
        _navIcon = nullptr;
        _distanceLabel = nullptr;
        _directionLabel = nullptr;
        _etaDivider = nullptr;
        _etaLabel = nullptr;
        _musicBox = nullptr;
        _musicIcon = nullptr;
        _musicLabel = nullptr;
        _idleCard = nullptr;
        _idleTitle = nullptr;
        _idleClock = nullptr;
        _defaultMessageLabel = nullptr;
        _bleDotInitialized = false;
    }

    // ------------------------------------------------------------------
    // Builders
    // ------------------------------------------------------------------
    void createHeader() {
        _headerBar = lv_obj_create(_screen);
        lv_obj_set_size(_headerBar, 240, 34);
        lv_obj_set_pos(_headerBar, 0, 0);
        lv_obj_set_style_bg_color(_headerBar, lv_color_hex(NavColors::BackgroundHex), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_headerBar, 255, LV_PART_MAIN);
        lv_obj_set_style_radius(_headerBar, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(_headerBar, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(_headerBar, 0, LV_PART_MAIN);
        lv_obj_clear_flag(_headerBar, LV_OBJ_FLAG_SCROLLABLE);

        _timeLabel = lv_label_create(_headerBar);
        lv_obj_set_style_text_font(_timeLabel, VietnameseFonts::getSemiboldSmallFont(), LV_PART_MAIN);
        lv_obj_set_style_text_color(_timeLabel, lv_color_hex(NavColors::TextPrimaryHex), LV_PART_MAIN);
        lv_obj_set_pos(_timeLabel, 10, 1);
        lv_label_set_text(_timeLabel, "--:--");

        // Dot status BLE (pengganti ETA di pojok kanan header)
        _bleDot = lv_obj_create(_headerBar);
        lv_obj_set_size(_bleDot, 10, 10);
        lv_obj_set_pos(_bleDot, 222, 12);
        lv_obj_set_style_radius(_bleDot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_bleDot, 255, LV_PART_MAIN);
        lv_obj_set_style_border_width(_bleDot, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(_bleDot, 0, LV_PART_MAIN);
        lv_obj_clear_flag(_bleDot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(_bleDot, lv_color_hex(NavColors::BleDisconnectedHex), LV_PART_MAIN);
        _bleDotColor = NavColors::BleDisconnectedHex;

        _headerDivider = lv_obj_create(_headerBar);
        lv_obj_set_size(_headerDivider, 240, 1);
        lv_obj_set_pos(_headerDivider, 0, 33);
        lv_obj_set_style_bg_color(_headerDivider, lv_color_hex(NavColors::BorderHex), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_headerDivider, 255, LV_PART_MAIN);
        lv_obj_set_style_radius(_headerDivider, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(_headerDivider, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(_headerDivider, 0, LV_PART_MAIN);
    }

    void createHeroCard() {
        _heroCard = lv_obj_create(_screen);
        lv_obj_set_size(_heroCard, 224, 146);
        lv_obj_set_pos(_heroCard, 8, 38);
        makeSurface(_heroCard, NavColors::SurfaceHex, 255, 14, NavColors::BorderHex);
        lv_obj_set_style_clip_corner(_heroCard, true, LV_PART_MAIN);

        // Tile biru untuk icon manuver (44x44)
        _iconTile = lv_obj_create(_heroCard);
        lv_obj_set_size(_iconTile, 44, 44);
        lv_obj_set_pos(_iconTile, 12, 4);
        lv_obj_set_style_bg_color(_iconTile, lv_color_hex(NavColors::AccentHex), LV_PART_MAIN);
        lv_obj_set_style_bg_grad_color(_iconTile, lv_color_hex(NavColors::AccentSoftHex), LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(_iconTile, LV_GRAD_DIR_VER, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_iconTile, 255, LV_PART_MAIN);
        lv_obj_set_style_radius(_iconTile, 10, LV_PART_MAIN);
        lv_obj_set_style_border_width(_iconTile, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(_iconTile, 0, LV_PART_MAIN);
        lv_obj_clear_flag(_iconTile, LV_OBJ_FLAG_SCROLLABLE);

        // Jarak (kanan icon)
        _distanceLabel = lv_label_create(_heroCard);
        lv_obj_set_pos(_distanceLabel, 64, 2);
        lv_obj_set_size(_distanceLabel, 148, 43);
        lv_obj_set_style_text_font(_distanceLabel, VietnameseFonts::getBoldFont(), LV_PART_MAIN);
        lv_obj_set_style_text_color(_distanceLabel, lv_color_hex(NavColors::DistanceHex), LV_PART_MAIN);
        lv_label_set_long_mode(_distanceLabel, LV_LABEL_LONG_CLIP);
        lv_label_set_text(_distanceLabel, "");

        // Instruksi manuver (2 baris)
        _directionLabel = lv_label_create(_heroCard);
        lv_obj_set_pos(_directionLabel, 12, 48);
        lv_obj_set_size(_directionLabel, 200, 62);
        lv_obj_set_style_text_font(_directionLabel, VietnameseFonts::getSemiboldSmallFont(), LV_PART_MAIN);
        lv_obj_set_style_text_color(_directionLabel, lv_color_hex(NavColors::TextPrimaryHex), LV_PART_MAIN);
        lv_obj_set_style_text_line_space(_directionLabel, 0, LV_PART_MAIN);
        lv_label_set_long_mode(_directionLabel, LV_LABEL_LONG_WRAP);
        lv_label_set_text(_directionLabel, "");

        // Garis pemisah + baris ETA (bekas posisi musik)
        _etaDivider = lv_obj_create(_heroCard);
        lv_obj_set_size(_etaDivider, 200, 1);
        lv_obj_set_pos(_etaDivider, 12, 112);
        lv_obj_set_style_bg_color(_etaDivider, lv_color_hex(NavColors::BorderHex), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_etaDivider, 255, LV_PART_MAIN);
        lv_obj_set_style_radius(_etaDivider, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(_etaDivider, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(_etaDivider, 0, LV_PART_MAIN);

        _etaLabel = createMarqueeLabel(_heroCard, 12, 114, 200, 29,
                                       VietnameseFonts::getNormalFont(), NavColors::TextMutedHex);

        if (_hasValidIcon) {
            drawNavIconDirectly();
        }
    }

    // Kotak musik terpisah, anak langsung screen, di bawah kartu.
    void createMusicBox() {
        _musicBox = lv_obj_create(_screen);
        lv_obj_set_size(_musicBox, 224, 40);
        lv_obj_set_pos(_musicBox, 8, 188);
        makeSurface(_musicBox, NavColors::SurfaceHex, 255, 12, NavColors::BorderHex);
        lv_obj_set_style_clip_corner(_musicBox, true, LV_PART_MAIN);
        lv_obj_add_flag(_musicBox, LV_OBJ_FLAG_HIDDEN);

        _musicIcon = lv_label_create(_musicBox);
        lv_obj_set_pos(_musicIcon, 12, 12);
        lv_label_set_text(_musicIcon, LV_SYMBOL_AUDIO);
        lv_obj_set_style_text_color(_musicIcon, lv_color_hex(NavColors::AccentSoftHex), LV_PART_MAIN);

        _musicLabel = createMarqueeLabel(_musicBox, 34, 4, 178, 31,
                                         VietnameseFonts::getSemiboldSmallFont(), NavColors::TextMutedHex);
    }

    void createIdleCard() {
        _idleCard = lv_obj_create(_screen);
        lv_obj_set_size(_idleCard, 224, 146);
        lv_obj_set_pos(_idleCard, 8, 38);
        makeSurface(_idleCard, NavColors::SurfaceHex, 255, 14, NavColors::BorderHex);
        lv_obj_set_style_clip_corner(_idleCard, true, LV_PART_MAIN);

        _idleTitle = lv_label_create(_idleCard);
        lv_obj_set_pos(_idleTitle, 16, 10);
        lv_obj_set_width(_idleTitle, 192);
        lv_obj_set_style_text_font(_idleTitle, VietnameseFonts::getSemiboldSmallFont(), LV_PART_MAIN);
        lv_obj_set_style_text_color(_idleTitle, lv_color_hex(NavColors::TextMutedHex), LV_PART_MAIN);
        lv_label_set_text(_idleTitle, "Tidak ada navigasi");

        // Jam besar di tengah (HOME)
        _idleClock = lv_label_create(_idleCard);
        lv_obj_set_pos(_idleClock, 12, 50);
        lv_obj_set_size(_idleClock, 200, 49);
        lv_obj_set_style_text_font(_idleClock, VietnameseFonts::getNumberBoldFont(), LV_PART_MAIN);
        lv_obj_set_style_text_color(_idleClock, lv_color_hex(NavColors::TextPrimaryHex), LV_PART_MAIN);
        lv_obj_set_style_text_align(_idleClock, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_text(_idleClock, "--:--");

        _defaultMessageLabel = lv_label_create(_idleCard);
        lv_obj_set_pos(_defaultMessageLabel, 16, 106);
        lv_obj_set_size(_defaultMessageLabel, 192, 34);
        lv_obj_set_style_text_font(_defaultMessageLabel, VietnameseFonts::getNormalFont(), LV_PART_MAIN);
        lv_obj_set_style_text_color(_defaultMessageLabel, lv_color_hex(NavColors::TextFaintHex), LV_PART_MAIN);
        lv_label_set_long_mode(_defaultMessageLabel, LV_LABEL_LONG_WRAP);
        lv_label_set_text(_defaultMessageLabel, "Mulai navigasi di Google Maps");
    }

public:
    NavigationScreenLVGL() : _iconData(_iconBuffer), _hasValidIcon(false), _iconCRC(0) {
        memset(_iconBuffer, 0, ICON_DATA_SIZE);
    }

    ~NavigationScreenLVGL() {
        // Dữ liệu pixel không còn là static buffer; descriptor chỉ trỏ
        // tới _iconBuffer của objek ini.
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

    // Tạo màn hình điều hướng / HOME
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

        createHeader();
        createHeroCard();
        createIdleCard();
        createMusicBox();

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
        if (_musicBox != nullptr && _showMusic) {
            lv_obj_move_foreground(_musicBox);
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

        // ---- jam (header kiri + jam besar HOME) ----
        char timeBuf[12];
        const int hour = ChronosManager::getInstance().getChronos().getHour();
        const int minute = ChronosManager::getInstance().getChronos().getMinute();
        snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", hour, minute);
        setLabel(_timeLabel, timeBuf);
        setLabel(_idleClock, timeBuf);

        // ---- dot status BLE (hanya ubah warna bila berubah) ----
        if (_bleDot != nullptr) {
            const bool bleConnected = ChronosManager::getInstance().isConnected();
            const uint32_t dotColor = bleConnected
                                          ? NavColors::BleConnectedHex
                                          : NavColors::BleDisconnectedHex;
            if (!_bleDotInitialized || dotColor != _bleDotColor) {
                _bleDotInitialized = true;
                _bleDotColor = dotColor;
                lv_obj_set_style_bg_color(_bleDot, lv_color_hex(dotColor), LV_PART_MAIN);
            }
        }

        // ---- now playing (judul - artis) ----
        MusicInfo music = ChronosManager::getInstance().getMusicInfo();
        char musicBuf[160];
        musicBuf[0] = '\0';
        bool hasMusic = false;
        if (music.state == 1 && music.title.length() > 0) {
            hasMusic = true;
            if (music.artist.length() > 0) {
                snprintf(musicBuf, sizeof(musicBuf), "%s - %s",
                         music.title.c_str(), music.artist.c_str());
            } else {
                snprintf(musicBuf, sizeof(musicBuf), "%s", music.title.c_str());
            }
        }

        _showMusic = hasMusic;
        setMarqueeText(_musicLabel, musicBuf);
        setVisible(_musicBox, hasMusic);

        if (hasMusic != _lastHasMusic) {
            _lastHasMusic = hasMusic;
            if (_musicBox != nullptr) {
                lv_obj_invalidate(_musicBox);
            }
        }

        // ---- jarak + instruksi manuver ----
        String direction = _navData.directions;
        if (isActive) {
            if (direction.length() == 0) {
                direction = "Lanjutkan perjalanan";
            }
            applyDirectionStyle(direction);
            setLabel(_directionLabel, direction.c_str());
            setLabel(_distanceLabel, _navData.distance.c_str());
        } else {
            setLabel(_directionLabel, "");
            setLabel(_distanceLabel, "");
        }

        // ---- ETA (di dalam kartu, pakai marquee yang sama) ----
        String etaText;
        if (isActive) {
            String eta = _navData.eta;
            if (eta == "Navigation") {
                eta = "";
            }
            if (eta.length() > 0) {
                if (eta.startsWith("Tiba")) {
                    etaText = eta;
                } else {
                    etaText = "Tiba " + eta;
                }
            } else if (_navData.duration.length() > 0) {
                etaText = "Tiba dalam " + _navData.duration;
            } else {
                etaText = "--";
            }
        }
        setMarqueeText(_etaLabel, etaText.c_str());

        // ---- visibilitas ----
        setVisible(_idleCard, !isActive);
        setVisible(_heroCard, isActive);
        setVisible(_etaDivider, isActive);
        setVisible(_etaLabel, isActive);
        setVisible(_distanceLabel, isActive);
        setVisible(_iconTile, isActive && _hasValidIcon);
        setVisible(_directionLabel, isActive);
        setVisible(_musicBox, hasMusic);
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
