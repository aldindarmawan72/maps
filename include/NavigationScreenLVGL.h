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
//  NAVIGATION
//   header : jam kecil (10,1), dot BLE (222,12), divider y33
//   kartu  : x8 y38 w224 h146 (radius 14)
//            icon 44x44 (12,4) + jarak (64,2)
//            instruksi 2 baris (12,48,200x62)
//            divider (12,112) + ETA marquee (12,114,200x26)
//   musik  : x8 y188 w224 h40 (radius 12) - terpisah dari kartu
//  HOME
//   header : HANYA dot BLE (jam kecil disembunyikan)
//   jam besar : center horizontal, y 70-119 (number_bold_48)
//   label : "Tidak ada navigasi" satu baris (center, y 126)
//   musik : sama seperti di atas (x8 y188)
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

    // ---- HOME / idle ----
    lv_obj_t* _homeClock = nullptr;
    lv_obj_t* _homeHint = nullptr;

    // ---- now playing (kotak musik terpisah di bawah kartu) ----
    lv_obj_t* _musicBox = nullptr;
    lv_obj_t* _musicIcon = nullptr;
    lv_obj_t* _musicLabel = nullptr;
    bool _showMusic = false;
    bool _lastHasMusic = false;

    // ---- icon ----
    lv_img_dsc_t _navIconDesc = {};
    uint8_t* _iconData = nullptr;
    uint8_t _iconBuffer[ICON_DATA_SIZE];
    bool _hasValidIcon = false;
    uint32_t _iconCRC = 0;

    // ---- data ----
    AppNavigation _navData;
    bool _lastActiveState = false;
    bool _homeMode = false;   // true ketika layar dipakai sebagai HOME
    bool _lastLayoutActive = false;
    bool _lastLayoutStateValid = false;
    bool _lastShowMusicLayout = false;

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

    // --- FIX B: sanitasi teks dari BLE sebelum masuk label LVGL ---
    // Menghapus karakter spasi Unicode khusus (NBSP, narrow no-break space,
    // thin space, ...), zero-width, dan BOM yang tidak ada glyph di font.
    // Buffer harus sudah dialokasikan pemanggil (no heap alloc di render).
    static void sanitizeText(const char* src, char* dst, size_t dstSize) {
        if (dstSize == 0) {
            return;
        }
        if (src == nullptr) {
            dst[0] = '\0';
            return;
        }

        size_t out = 0;
        const uint8_t* p = (const uint8_t*)src;
        bool lastWasSpace = false;

        while (*p != 0 && out + 1 < dstSize) {
            // Deteksi multi-byte UTF-8 dikenal
            if (p[0] == 0xE2 && p[1] != 0 && p[2] != 0) {
                uint8_t b1 = p[1];
                uint8_t b2 = p[2];
                // E2 80 xx generalized
                if (b1 == 0x80) {
                    switch (b2) {
                        case 0x87: // U+2007 figure space
                        case 0x89: // U+2009 thin space
                        case 0x8A: // U+200A hair space
                        case 0x8B: // U+200B zero-width space
                        case 0x8C: // U+200C ZWNJ
                        case 0x8D: // U+200D ZWJ
                        case 0x8E: // U+200E LRM
                        case 0x8F: // U+200F RLM
                        case 0xAF: // U+202F narrow no-break space
                        case 0xA8: // U+2028 line separator
                        case 0xA9: // U+2029 paragraph separator
                            // spasi/zero-width -> spasi biasa (atau buang utk zw)
                            if (b2 == 0x8B || b2 == 0x8C || b2 == 0x8D ||
                                b2 == 0x8E || b2 == 0x8F) {
                                p += 3; // drop zero-width / bidi markers
                                continue;
                            }
                            if (!lastWasSpace) {
                                dst[out++] = ' ';
                                lastWasSpace = true;
                            }
                            p += 3;
                            continue;
                        default:
                            break;
                    }
                }
            } else if (p[0] == 0xC2 && p[1] == 0xA0) {
                // U+00A0 no-break space
                if (!lastWasSpace) {
                    dst[out++] = ' ';
                    lastWasSpace = true;
                }
                p += 2;
                continue;
            } else if (p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
                // U+FEFF BOM / zero-width no-break
                p += 3;
                continue;
            }

            // Karakter kontrol ASCII (< 0x20) dibuang kecuali tab -> spasi
            if (p[0] < 0x20) {
                if (p[0] == '\t' && !lastWasSpace) {
                    dst[out++] = ' ';
                    lastWasSpace = true;
                }
                p++;
                continue;
            }

            if (p[0] == ' ') {
                if (!lastWasSpace) {
                    dst[out++] = ' ';
                    lastWasSpace = true;
                }
                p++;
                continue;
            }

            dst[out++] = (char)p[0];
            lastWasSpace = false;
            p++;
        }

        // trim trailing space
        while (out > 0 && dst[out - 1] == ' ') {
            out--;
        }
        dst[out] = '\0';

        // Opsional: "10.08" -> "10:08" khusus pola HH.MM (2 digit . 2 digit).
        for (size_t i = 0; i + 4 < out + 1 && i + 4 <= out; i++) {
            if (dst[i] >= '0' && dst[i] <= '9' &&
                dst[i + 1] >= '0' && dst[i + 1] <= '9' &&
                dst[i + 2] == '.' &&
                dst[i + 3] >= '0' && dst[i + 3] <= '9' &&
                dst[i + 4] >= '0' && dst[i + 4] <= '9') {
                // Pastikan bukan bagian dari desimal/angka lebih panjang
                bool beforeOk = (i == 0) || !(dst[i - 1] >= '0' && dst[i - 1] <= '9');
                bool afterOk = (i + 5 >= out) || !(dst[i + 5] >= '0' && dst[i + 5] <= '9');
                if (beforeOk && afterOk) {
                    dst[i + 2] = ':';
                }
            }
        }
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

        // Reset total: hapus animasi, set mode CLIP, kosongkan, lalu isi teks
        // baru. Ini mencegah dua animasi berjalan untuk satu label.
        lv_anim_del(label, NULL);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_label_set_text(label, "");
        lv_label_set_text(label, text);

        const lv_font_t* font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
        lv_point_t textSize;
        lv_txt_get_size(&textSize, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        const lv_coord_t available = lv_obj_get_width(label);

        lv_label_set_long_mode(label,
            textSize.x > available ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_CLIP);

        lv_obj_invalidate(label);
        lv_obj_t* parent = lv_obj_get_parent(label);
        if (parent != nullptr) {
            lv_obj_invalidate(parent);
        }
    }

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

    // Atur font & long-mode instruksi. Teks sudah di-sanitize sebelumnya.
    void applyDirectionStyle(const char* direction) {
        if (_directionLabel == nullptr) {
            return;
        }
        const size_t len = strlen(direction);
        lv_font_t* font = VietnameseFonts::getSemiboldSmallFont();
        lv_label_long_mode_t mode = LV_LABEL_LONG_WRAP;

        if (len <= 18) {
            font = VietnameseFonts::getBoldFont();
        } else if (len > 60) {
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
        _homeClock = nullptr;
        _homeHint = nullptr;
        _musicBox = nullptr;
        _musicIcon = nullptr;
        _musicLabel = nullptr;
        _bleDotInitialized = false;
        _lastLayoutStateValid = false;
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

        _etaLabel = createMarqueeLabel(_heroCard, 12, 115, 200, 26,
                                       VietnameseFonts::getNormalFont(), NavColors::TextMutedHex);

        if (_hasValidIcon) {
            drawNavIconDirectly();
        }
    }

    // HOME: jam besar + hint "Tidak ada navigasi". Tidak ada kartu.
    void createHomeUI() {
        _homeClock = lv_label_create(_screen);
        lv_obj_set_size(_homeClock, 240, 49);
        lv_obj_set_pos(_homeClock, 0, 70);
        lv_obj_set_style_text_font(_homeClock, VietnameseFonts::getNumberBoldFont(), LV_PART_MAIN);
        lv_obj_set_style_text_color(_homeClock, lv_color_hex(NavColors::TextPrimaryHex), LV_PART_MAIN);
        lv_obj_set_style_text_align(_homeClock, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_long_mode(_homeClock, LV_LABEL_LONG_CLIP);
        lv_label_set_text(_homeClock, "--:--");

        _homeHint = lv_label_create(_screen);
        lv_obj_set_size(_homeHint, 240, 31);
        lv_obj_set_pos(_homeHint, 0, 126);
        lv_obj_set_style_text_font(_homeHint, VietnameseFonts::getNormalFont(), LV_PART_MAIN);
        lv_obj_set_style_text_color(_homeHint, lv_color_hex(NavColors::TextFaintHex), LV_PART_MAIN);
        lv_obj_set_style_text_align(_homeHint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_long_mode(_homeHint, LV_LABEL_LONG_CLIP);
        lv_label_set_text(_homeHint, "Tidak ada navigasi");
    }

    // Kotak musik terpisah, anak langsung screen, di bawah.
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

    // Terapkan visibilitas semua objek menurut state (satu sumber kebenaran).
    void applyLayoutForState(bool navActive, bool hasMusic) {
        // Kartu navigasi + isinya: hanya saat NAVIGATION aktif.
        setVisible(_heroCard, navActive);
        setVisible(_iconTile, navActive && _hasValidIcon);
        setVisible(_distanceLabel, navActive);
        setVisible(_directionLabel, navActive);
        setVisible(_etaDivider, navActive);
        setVisible(_etaLabel, navActive);

        // HOME: jam besar + hint hanya saat TIDAK ada navigasi.
        setVisible(_homeClock, !navActive);
        setVisible(_homeHint, !navActive);

        // Header jam kecil: sembunyikan di HOME (agar tidak ada dua jam).
        setVisible(_timeLabel, navActive);

        // Kotak musik: tampil di NAVIGATION dan HOME bila ada lagu.
        setVisible(_musicBox, hasMusic);
    }

public:
    NavigationScreenLVGL() : _iconData(_iconBuffer), _hasValidIcon(false), _iconCRC(0) {
        memset(_iconBuffer, 0, ICON_DATA_SIZE);
    }

    ~NavigationScreenLVGL() {
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

    // Setelah create() dipanggil ulang, HOME mode harus dipulihkan pemanggil
    // lewat setHomeMode(). Default false (NAVIGATION).
    void setHomeMode(bool home) {
        _homeMode = home;
        _lastLayoutStateValid = false;
    }

    void create() {
        VietnameseFonts::init();

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
        createHomeUI();
        createMusicBox();

        updateLabels();

        Serial.println("NavigationScreenLVGL: Screen created successfully");
    }

    void display() {
        if (_screen == nullptr) {
            Serial.println("ERROR: NavigationScreenLVGL - _screen is NULL, cannot display");
            return;
        }

        updateLabels();

        if (_bgImage != nullptr) {
            lv_obj_move_background(_bgImage);
        }
        if (_musicBox != nullptr && _showMusic) {
            lv_obj_move_foreground(_musicBox);
        }

        if (lv_scr_act() != _screen) {
            lv_scr_load(_screen);
        }

        lv_refr_now(NULL);
    }

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

    // Update semua teks + visibility. Dipanggil tiap render.
    void updateLabels() {
        if (_screen == nullptr) {
            return;
        }

        const bool isActive = _navData.active && _navData.isNavigation;

        // ---- jam kecil header + jam besar HOME (format sama) ----
        char timeBuf[12];
        const int hour = ChronosManager::getInstance().getChronos().getHour();
        const int minute = ChronosManager::getInstance().getChronos().getMinute();
        snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", hour, minute);
        setLabel(_timeLabel, timeBuf);
        setLabel(_homeClock, timeBuf);

        // ---- dot BLE (hanya ubah bila status berubah) ----
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

        // ---- now playing (judul - artis), disanitasi ----
        MusicInfo music = ChronosManager::getInstance().getMusicInfo();
        char musicRaw[192];
        char musicBuf[192];
        musicRaw[0] = '\0';
        const bool hasMusicRaw = (music.state == 1 && music.title.length() > 0);
        if (hasMusicRaw) {
            if (music.artist.length() > 0) {
                snprintf(musicRaw, sizeof(musicRaw), "%s - %s",
                         music.title.c_str(), music.artist.c_str());
            } else {
                snprintf(musicRaw, sizeof(musicRaw), "%s", music.title.c_str());
            }
        }
        sanitizeText(musicRaw, musicBuf, sizeof(musicBuf));
        const bool hasMusic = (musicBuf[0] != '\0');

        _showMusic = hasMusic;
        setMarqueeText(_musicLabel, musicBuf);

#if MUSIC_DEBUG
        static bool lastHasMusicDbg = false;
        if (hasMusic != lastHasMusicDbg) {
            lastHasMusicDbg = hasMusic;
            Serial.printf("[MUSIC] render hasMusic=%d text='%s'\n",
                          hasMusic ? 1 : 0, musicBuf);
        }
#endif

        // ---- jarak + instruksi manuver, disanitasi ----
        char dirRaw[200];
        char dirBuf[200];
        char distRaw[64];
        char distBuf[64];
        dirRaw[0] = '\0';
        distRaw[0] = '\0';
        if (isActive) {
            const char* src = _navData.directions.c_str();
            if (src[0] == '\0') {
                src = "Lanjutkan perjalanan";
            }
            strncpy(dirRaw, src, sizeof(dirRaw) - 1);
            dirRaw[sizeof(dirRaw) - 1] = '\0';
            strncpy(distRaw, _navData.distance.c_str(), sizeof(distRaw) - 1);
            distRaw[sizeof(distRaw) - 1] = '\0';
        }
        sanitizeText(dirRaw, dirBuf, sizeof(dirBuf));
        sanitizeText(distRaw, distBuf, sizeof(distBuf));

        if (isActive) {
            applyDirectionStyle(dirBuf);
            setLabel(_directionLabel, dirBuf);
            setLabel(_distanceLabel, distBuf);
        } else {
            setLabel(_directionLabel, "");
            setLabel(_distanceLabel, "");
        }

        // ---- ETA (di dalam kartu, di-sanitasi) ----
        char etaRaw[96];
        char etaBuf[96];
        etaRaw[0] = '\0';
        if (isActive) {
            String eta = _navData.eta;
            if (eta == "Navigation") {
                eta = "";
            }
            if (eta.length() > 0) {
                if (eta.startsWith("Tiba")) {
                    strncpy(etaRaw, eta.c_str(), sizeof(etaRaw) - 1);
                } else {
                    snprintf(etaRaw, sizeof(etaRaw), "Tiba %s", eta.c_str());
                }
            } else if (_navData.duration.length() > 0) {
                snprintf(etaRaw, sizeof(etaRaw), "Tiba dalam %s", _navData.duration.c_str());
            } else {
                strncpy(etaRaw, "--", sizeof(etaRaw) - 1);
            }
            etaRaw[sizeof(etaRaw) - 1] = '\0';
        }
        sanitizeText(etaRaw, etaBuf, sizeof(etaBuf));
        setMarqueeText(_etaLabel, etaBuf);

        // ---- terapkan layout (hanya saat state berubah) ----
        const bool navActive = isActive;
        if (!_lastLayoutStateValid ||
            _lastLayoutActive != navActive ||
            _lastShowMusicLayout != hasMusic) {
            _lastLayoutStateValid = true;
            _lastLayoutActive = navActive;
            _lastShowMusicLayout = hasMusic;
            applyLayoutForState(navActive, hasMusic);
        }

        if (hasMusic != _lastHasMusic) {
            _lastHasMusic = hasMusic;
            if (_musicBox != nullptr) {
                lv_obj_invalidate(_musicBox);
            }
        }
    }

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