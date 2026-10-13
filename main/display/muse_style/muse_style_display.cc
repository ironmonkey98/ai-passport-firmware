// Muse-style screen: a pixel avatar, a status word in the mode's accent
// colour and the subtitle. Used by the AI Passport and the ESP-Hi.
//
// The layout and the strip image decoder follow the Muse Gadget SDK UI
// (github.com/facebookincubator/muse-gadget-sdk, esp32/components/muse/muse_ui.c),
// Copyright (c) Meta Platforms, Inc. and affiliates, Apache License 2.0.
// The avatar (robot_pixel.c, "Mimi") is an original character.

#include "muse_style_display.h"
#include "robot_pixel.h"

#include "application.h"
#include "lvgl_theme.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <src/draw/lv_image_decoder_private.h>
#include <algorithm>
#include <cstring>

#define TAG "MuseStyleDisplay"

namespace {

constexpr int kFrameMs = 40;        // listening, thinking, boot: 25 fps
constexpr int kCalmFrameMs = 80;    // idle, speaking, error: 12.5 fps
constexpr int kSleepFrameMs = 200;  // asleep, or covered by another view

// The C3 has one core: at 25 fps the avatar took 40-47 % of it when idle and
// left speaking with ~4 % idle time next to the Opus decoder (2026-10-13 profile).
// Animations follow wall-clock time, so a lower rate only samples them less often.
int FramePeriodMs(robot_mode_t mode) {
    switch (mode) {
        case ROBOT_MODE_LISTENING:
        case ROBOT_MODE_THINKING:
        case ROBOT_MODE_BOOT:
            return kFrameMs;
        case ROBOT_MODE_OFF:
            return kSleepFrameMs;
        default:
            return kCalmFrameMs;
    }
}
constexpr int kStripRows = 8;  // 192 px wide: 3 KB per strip
constexpr int kMaxDirtyRects = 8;
constexpr int64_t kHappyUs = 2500 * 1000;
constexpr int64_t kErrorUs = 4000 * 1000;

constexpr uint32_t kIconColor = 0xB4B8D6;
constexpr uint32_t kTextColor = 0xE6E8F2;

robot_mode_t ModeOf(DeviceState state) {
    switch (state) {
        case kDeviceStateIdle:
        case kDeviceStateNotifying:
            return ROBOT_MODE_IDLE;
        case kDeviceStateConnecting:
            return ROBOT_MODE_THINKING;
        case kDeviceStateListening:
        case kDeviceStateAudioTesting:
            return ROBOT_MODE_LISTENING;
        case kDeviceStateSpeaking:
            return ROBOT_MODE_SPEAKING;
        case kDeviceStateFatalError:
            return ROBOT_MODE_ERROR;
        default:
            return ROBOT_MODE_BOOT;
    }
}

bool IsHappy(const char* emotion) {
    static const char* const kHappy[] = {"happy", "laughing", "funny",  "loving",
                                         "winking", "cool",   "kissy",  "confident",
                                         "delicious", "silly"};
    for (const char* e : kHappy) {
        if (strcmp(emotion, e) == 0) {
            return true;
        }
    }
    return false;
}

bool IsError(const char* emotion) {
    return strcmp(emotion, "warning") == 0 || strcmp(emotion, "cancel") == 0 ||
           strcmp(emotion, "cloud_off") == 0;
}

// A cell's first screen pixel.
int32_t CellPx(int cell, int size) { return (cell * size + ROBOT_PX_W - 1) / ROBOT_PX_W; }

}  // namespace

MuseStyleDisplay* MuseStyleDisplay::instance_ = nullptr;

MuseStyleDisplay::MuseStyleDisplay(esp_lcd_panel_io_handle_t panel_io,
                                   esp_lcd_panel_handle_t panel, int width, int height,
                                   int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                                   bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y,
                    swap_xy) {
    instance_ = this;
    start_us_ = esp_timer_get_time();
}

MuseStyleDisplay::~MuseStyleDisplay() {
    DisplayLockGuard lock(this);
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
    }
    if (avatar_ != nullptr) {
        lv_obj_delete(avatar_);
    }
    if (strip_ != nullptr) {
        lv_draw_buf_destroy(strip_);
    }
    instance_ = nullptr;
}

void MuseStyleDisplay::SetupUI() {
    if (setup_ui_called_) {
        return;
    }
    // The pixel art is drawn for a black screen.
    current_theme_ = LvglThemeManager::GetInstance().GetTheme("dark");
    LcdDisplay::SetupUI();

    DisplayLockGuard lock(this);
    CreateAvatar();
    ApplyStyle();
    timer_ = lv_timer_create([](lv_timer_t* t) {
        static_cast<MuseStyleDisplay*>(lv_timer_get_user_data(t))->Tick();
    }, kFrameMs, this);
}

void MuseStyleDisplay::CreateAvatar() {
    int fit = std::min(width_ - 16, height_ - 120);
    int size = ROBOT_PX_W * std::max(1, fit / ROBOT_PX_W);
    robot_pixel_set_size(size);

    avatar_src_.header.magic = LV_IMAGE_HEADER_MAGIC;
    avatar_src_.header.cf = LV_COLOR_FORMAT_RGB565;
    avatar_src_.header.w = size;
    avatar_src_.header.h = size;
    avatar_src_.header.stride = size * sizeof(uint16_t);
    avatar_src_.data = reinterpret_cast<const uint8_t*>(&avatar_src_);  // LVGL skips sources without data

    strip_ = lv_draw_buf_create(size, kStripRows, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    if (strip_ == nullptr) {
        ESP_LOGE(TAG, "No memory for the avatar strip");
        return;
    }

    static bool decoder_ready = false;
    if (!decoder_ready) {
        lv_image_decoder_t* dec = lv_image_decoder_create();
        lv_image_decoder_set_info_cb(dec, DecoderInfo);
        lv_image_decoder_set_open_cb(dec, DecoderOpen);
        lv_image_decoder_set_get_area_cb(dec, DecoderGetArea);
        lv_image_decoder_set_close_cb(dec, DecoderClose);
        dec->name = "robot";
        decoder_ready = true;
    }

    robot_pose_t pose = {ROBOT_MODE_BOOT, 0, 0, 0, 0};
    robot_pixel_render(&pose);

    avatar_ = lv_image_create(lv_screen_active());
    lv_image_set_src(avatar_, &avatar_src_);
    lv_obj_align(avatar_, LV_ALIGN_CENTER, 0, 4);
    // Above the background container, below the bars and popups.
    lv_obj_move_to_index(avatar_, 1);
}

void MuseStyleDisplay::ApplyStyle() {
    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    if (container_ != nullptr) {
        lv_obj_set_style_bg_image_src(container_, nullptr, 0);
        lv_obj_set_style_bg_color(container_, lv_color_black(), 0);
    }
    if (emoji_box_ != nullptr) {
        lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    }
    if (top_bar_ != nullptr) {
        lv_obj_set_style_bg_opa(top_bar_, LV_OPA_TRANSP, 0);
    }
    for (lv_obj_t* icon : {network_label_, mute_label_, battery_label_}) {
        if (icon != nullptr) {
            lv_obj_set_style_text_color(icon, lv_color_hex(kIconColor), 0);
        }
    }
    // Status word below the icons, as in Muse.
    if (status_bar_ != nullptr && top_bar_ != nullptr) {
        lv_obj_update_layout(top_bar_);
        lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, lv_obj_get_height(top_bar_) - 4);
    }
    uint32_t accent = robot_pixel_accent(mode_ < 0 ? ROBOT_MODE_BOOT : (robot_mode_t)mode_);
    for (lv_obj_t* label : {status_label_, notification_label_}) {
        if (label != nullptr) {
            lv_obj_set_style_text_color(label, lv_color_hex(accent), 0);
        }
    }
    if (bottom_bar_ != nullptr) {
        lv_obj_set_style_bg_opa(bottom_bar_, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bottom_bar_, lv_color_black(), 0);
    }
    if (chat_message_label_ != nullptr) {
        lv_obj_set_style_text_color(chat_message_label_, lv_color_hex(kTextColor), 0);
    }
    last_grid_valid_ = false;
}

void MuseStyleDisplay::SetTheme(Theme* theme) {
    (void)theme;
    // Keep the dark theme (fonts from assets still apply) and our colours.
    LcdDisplay::SetTheme(LvglThemeManager::GetInstance().GetTheme("dark"));
    DisplayLockGuard lock(this);
    ApplyStyle();
}

void MuseStyleDisplay::SetEmotion(const char* emotion) {
    if (emotion == nullptr) {
        return;
    }
    DisplayLockGuard lock(this);
    int64_t now = esp_timer_get_time();
    sleepy_ = strcmp(emotion, "sleepy") == 0;
    if (IsHappy(emotion)) {
        happy_until_us_ = now + kHappyUs;
    } else if (IsError(emotion)) {
        error_until_us_ = now + kErrorUs;
    }
}

void MuseStyleDisplay::Tick() {
    if (avatar_ == nullptr || strip_ == nullptr) {
        return;
    }
    // Covered by another view (e.g. the recording overlay hides the screen's
    // children): no one sees the avatar, so don't render it.
    if (lv_obj_has_flag(avatar_, LV_OBJ_FLAG_HIDDEN)) {
        lv_timer_set_period(timer_, kSleepFrameMs);
        mode_ = -1;  // repaint fully and restore the rate when shown again
        return;
    }
    int64_t now = esp_timer_get_time();
    robot_mode_t mode = ModeOf(Application::GetInstance().GetDeviceState());
    if (sleepy_) {
        mode = ROBOT_MODE_OFF;
    } else if (now < error_until_us_ && mode != ROBOT_MODE_LISTENING &&
               mode != ROBOT_MODE_SPEAKING) {
        mode = ROBOT_MODE_ERROR;
    }

    if (mode != mode_) {
        mode_ = mode;
        mode_since_us_ = now;
        uint32_t accent = robot_pixel_accent(mode);
        lv_obj_set_style_text_color(status_label_, lv_color_hex(accent), 0);
        lv_obj_set_style_text_color(notification_label_, lv_color_hex(accent), 0);
        lv_timer_set_period(timer_, FramePeriodMs(mode));
        last_grid_valid_ = false;  // the palette changes with the mode
    }

    robot_pose_t pose = {
        .mode = mode,
        .t = (now - start_us_) / 1e6f,
        .mode_t = (now - mode_since_us_) / 1e6f,
        .level = 0,
        .happy = now < happy_until_us_ ? 1.0f : 0.0f,
    };
    robot_pixel_render(&pose);
    InvalidateChangedCells();
}

// Most frames change only a few cells. Redraw those as at most kMaxDirtyRects
// bands of rows, instead of pushing the whole avatar over SPI every frame.
void MuseStyleDisplay::InvalidateChangedCells() {
    const uint8_t* grid = robot_pixel_grid();
    if (!last_grid_valid_) {
        memcpy(last_grid_, grid, sizeof(last_grid_));
        last_grid_valid_ = true;
        lv_obj_invalidate(avatar_);
        return;
    }

    lv_area_t rects[kMaxDirtyRects];
    int n = 0;
    for (int cy = 0; cy < ROBOT_PX_H; cy++) {
        const uint8_t* row = grid + cy * ROBOT_PX_W;
        uint8_t* last = last_grid_ + cy * ROBOT_PX_W;
        int x1 = -1, x2 = -1;
        for (int cx = 0; cx < ROBOT_PX_W; cx++) {
            if (row[cx] != last[cx]) {
                last[cx] = row[cx];
                x1 = x1 < 0 ? cx : x1;
                x2 = cx;
            }
        }
        if (x2 < 0) {
            continue;
        }
        lv_area_t* prev = n > 0 ? &rects[n - 1] : nullptr;
        if (prev != nullptr && (prev->y2 == cy - 1 || n == kMaxDirtyRects)) {
            prev->x1 = LV_MIN(prev->x1, x1);
            prev->x2 = LV_MAX(prev->x2, x2);
            prev->y2 = cy;
        } else {
            rects[n++] = {x1, cy, x2, cy};
        }
    }

    lv_area_t at;
    lv_obj_get_coords(avatar_, &at);
    int size = avatar_src_.header.w;
    for (int i = 0; i < n; i++) {
        lv_area_t a = {
            at.x1 + CellPx(rects[i].x1, size),
            at.y1 + CellPx(rects[i].y1, size),
            at.x1 + CellPx(rects[i].x2 + 1, size) - 1,
            at.y1 + CellPx(rects[i].y2 + 1, size) - 1,
        };
        lv_obj_invalidate_area(avatar_, &a);
    }
}

lv_result_t MuseStyleDisplay::DecoderInfo(lv_image_decoder_t* dec, lv_image_decoder_dsc_t* dsc,
                                          lv_image_header_t* header) {
    (void)dec;
    if (instance_ == nullptr || dsc->src != &instance_->avatar_src_) {
        return LV_RESULT_INVALID;
    }
    *header = instance_->avatar_src_.header;
    return LV_RESULT_OK;
}

// LVGL holds its decoder lock around open and close.
lv_result_t MuseStyleDisplay::DecoderOpen(lv_image_decoder_t* dec, lv_image_decoder_dsc_t* dsc) {
    (void)dec;
    if (instance_ == nullptr || instance_->strip_busy_) {
        return LV_RESULT_INVALID;
    }
    instance_->strip_busy_ = true;
    dsc->user_data = instance_->strip_;
    return LV_RESULT_OK;  // the work is in get_area
}

lv_result_t MuseStyleDisplay::DecoderGetArea(lv_image_decoder_t* dec,
                                             lv_image_decoder_dsc_t* dsc, const lv_area_t* full,
                                             lv_area_t* area) {
    (void)dec;
    int32_t y1 = area->y1 == LV_COORD_MIN ? full->y1 : area->y2 + 1;
    if (y1 > full->y2) {
        return LV_RESULT_INVALID;
    }
    int32_t y2 = LV_MIN(y1 + kStripRows - 1, full->y2);
    int32_t w = lv_area_get_width(full);
    lv_draw_buf_t* buf = lv_draw_buf_reshape(static_cast<lv_draw_buf_t*>(dsc->user_data),
                                             LV_COLOR_FORMAT_RGB565, w, y2 - y1 + 1,
                                             LV_STRIDE_AUTO);
    if (buf == nullptr) {
        return LV_RESULT_INVALID;
    }
    robot_pixel_scale(reinterpret_cast<uint16_t*>(buf->data),
                      buf->header.stride / sizeof(uint16_t), full->x1, full->x2, y1, y2);
    area->x1 = full->x1;
    area->x2 = full->x2;
    area->y1 = y1;
    area->y2 = y2;
    dsc->decoded = buf;
    return LV_RESULT_OK;
}

void MuseStyleDisplay::DecoderClose(lv_image_decoder_t* dec, lv_image_decoder_dsc_t* dsc) {
    (void)dec;
    if (instance_ != nullptr && dsc->user_data == instance_->strip_) {
        instance_->strip_busy_ = false;
    }
}
