#pragma once
// Full-screen recording view on LVGL's top layer, in the Muse pixel look:
// blinking REC dot, big pixel timer, live input-level bars, and how much of
// the recording the server has confirmed on disk. It covers the Muse UI only
// while recording or showing a result; main/display/muse_style is untouched.
//
// Lock order: the LVGL timer holds the display lock and then takes the
// recorder's mutex (Snapshot). The recorder never takes the display lock.
#include <lvgl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "application.h"
#include "display/display.h"
#include "lvgl_theme.h"
#include "passport_recorder.h"

class RecordingOverlay {
public:
    explicit RecordingOverlay(PassportRecorder* recorder) : recorder_(recorder) {}

    // Main task, with the display lock held (call through DisplayLockGuard).
    void Create(Display* display) {
        if (root_ != nullptr) return;
        auto theme = static_cast<LvglTheme*>(display->GetTheme());
        const lv_font_t* font = theme->text_font()->font();
        const int w = display->width(), h = display->height();

        root_ = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(root_);
        lv_obj_set_size(root_, w, h);
        lv_obj_set_style_bg_color(root_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
        lv_obj_set_style_text_font(root_, font, 0);
        lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);

        dot_ = lv_obj_create(root_);
        lv_obj_remove_style_all(dot_);
        lv_obj_set_size(dot_, 14, 14);
        lv_obj_set_style_radius(dot_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot_, lv_color_hex(kRed), 0);
        lv_obj_set_style_bg_opa(dot_, LV_OPA_COVER, 0);
        lv_obj_align(dot_, LV_ALIGN_TOP_LEFT, 16, 20);

        title_ = lv_label_create(root_);
        lv_obj_set_style_text_color(title_, lv_color_hex(kRed), 0);
        lv_label_set_text(title_, "REC");
        lv_obj_align_to(title_, dot_, LV_ALIGN_OUT_RIGHT_MID, 8, 0);

        timer_canvas_ = MakeCanvas(kTimerW, kTimerH, kWhite, &timer_buf_);
        lv_obj_align(timer_canvas_, LV_ALIGN_TOP_MID, 0, 62);

        wave_w_ = std::min(w - 16, kBars * kBarPitch);
        wave_canvas_ = MakeCanvas(wave_w_, kWaveH, kRed, &wave_buf_);
        lv_obj_align(wave_canvas_, LV_ALIGN_TOP_MID, 0, 62 + kTimerH + 28);

        uploaded_ = lv_label_create(root_);
        lv_obj_set_style_text_color(uploaded_, lv_color_hex(kText), 0);
        lv_obj_align(uploaded_, LV_ALIGN_BOTTOM_LEFT, 16, -48);

        chip_dot_ = lv_obj_create(root_);
        lv_obj_remove_style_all(chip_dot_);
        lv_obj_set_size(chip_dot_, 10, 10);
        lv_obj_set_style_radius(chip_dot_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(chip_dot_, LV_OPA_COVER, 0);
        lv_obj_align(chip_dot_, LV_ALIGN_BOTTOM_LEFT, 18, -22);

        chip_ = lv_label_create(root_);
        lv_obj_align(chip_, LV_ALIGN_BOTTOM_LEFT, 36, -16);

        timer_ = lv_timer_create([](lv_timer_t* t) {
            static_cast<RecordingOverlay*>(lv_timer_get_user_data(t))->Tick();
        }, 100, this);
    }

private:
    static constexpr uint32_t kRed = 0xFF4D5E, kWhite = 0xF2F2F7, kText = 0xC9CCDD;
    static constexpr uint32_t kGreen = 0x34E3A6, kAmber = 0xFFC24D, kDim = 0x6B6F80;
    static constexpr int kCell = 5;                 // one pixel-font dot = 5x5 screen pixels
    static constexpr int kGlyphW = 5, kGlyphH = 7;  // 5x7 pixel digits
    static constexpr int kTimerW = (4 * (kGlyphW + 1) + 2) * kCell, kTimerH = kGlyphH * kCell;
    static constexpr int kBars = 36, kBarPitch = 6, kBarW = 4, kWaveH = 64;
    static constexpr uint64_t kResultShowMs = 4000;

    PassportRecorder* recorder_;
    lv_obj_t *root_ = nullptr, *dot_ = nullptr, *title_ = nullptr, *uploaded_ = nullptr;
    lv_obj_t *chip_dot_ = nullptr, *chip_ = nullptr, *timer_canvas_ = nullptr, *wave_canvas_ = nullptr;
    lv_draw_buf_t *timer_buf_ = nullptr, *wave_buf_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    int wave_w_ = 0;
    uint8_t levels_[kBars] = {};
    uint32_t ticks_ = 0, shown_seconds_ = UINT32_MAX;
    bool visible_ = false;

    lv_obj_t* MakeCanvas(int w, int h, uint32_t ink, lv_draw_buf_t** buf) {
        *buf = lv_draw_buf_create(w, h, LV_COLOR_FORMAT_I1, LV_STRIDE_AUTO);
        lv_obj_t* canvas = lv_canvas_create(root_);
        if (*buf == nullptr) return canvas;
        lv_canvas_set_draw_buf(canvas, *buf);
        lv_canvas_set_palette(canvas, 0, lv_color32_make(0, 0, 0, 255));
        lv_canvas_set_palette(canvas, 1, lv_color32_make((ink >> 16) & 0xFF, (ink >> 8) & 0xFF, ink & 0xFF, 255));
        Clear(*buf);
        return canvas;
    }

    static void Clear(lv_draw_buf_t* buf) {
        for (uint32_t y = 0; y < buf->header.h; y++) {
            std::memset(lv_draw_buf_goto_xy(buf, 0, y), 0, buf->header.stride);
        }
    }

    static void Fill(lv_draw_buf_t* buf, int x0, int y0, int w, int h) {
        for (int y = std::max(0, y0); y < std::min<int>(buf->header.h, y0 + h); y++) {
            auto row = static_cast<uint8_t*>(lv_draw_buf_goto_xy(buf, 0, y));
            for (int x = std::max(0, x0); x < std::min<int>(buf->header.w, x0 + w); x++) {
                row[x >> 3] |= uint8_t(0x80 >> (x & 7));
            }
        }
    }

    // 5x7 digits, one bit per column, top row first.
    static const uint8_t* Glyph(char c) {
        static const uint8_t kDigits[10][7] = {
            {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
            {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},
            {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
            {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
            {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
        };
        return c >= '0' && c <= '9' ? kDigits[c - '0'] : nullptr;
    }

    // "MM:SS" in LED-style dots: each lit dot is a (kCell-1) square with a 1 px gap.
    void DrawTimer(uint32_t seconds) {
        char text[8];
        std::snprintf(text, sizeof(text), "%02u:%02u", unsigned(std::min<uint32_t>(seconds / 60, 99)), unsigned(seconds % 60));
        Clear(timer_buf_);
        int x = 0;
        for (const char* p = text; *p; p++) {
            if (*p == ':') {
                Fill(timer_buf_, x, 2 * kCell, kCell - 1, kCell - 1);
                Fill(timer_buf_, x, 4 * kCell, kCell - 1, kCell - 1);
                x += 2 * kCell;
                continue;
            }
            const uint8_t* g = Glyph(*p);
            for (int row = 0; row < kGlyphH; row++) {
                for (int col = 0; col < kGlyphW; col++) {
                    if (g[row] & (0x10 >> col)) Fill(timer_buf_, x + col * kCell, row * kCell, kCell - 1, kCell - 1);
                }
            }
            x += (kGlyphW + 1) * kCell;
        }
        lv_obj_invalidate(timer_canvas_);
    }

    void DrawWave() {
        Clear(wave_buf_);
        const int bars = wave_w_ / kBarPitch;
        for (int i = 0; i < bars; i++) {
            int level = levels_[kBars - bars + i];  // newest on the right
            int h = std::max(2, level * (kWaveH - 4) / 255);
            h -= h % 2;  // keep the bar centred
            Fill(wave_buf_, i * kBarPitch, (kWaveH - h) / 2, kBarW, h);
        }
        lv_obj_invalidate(wave_canvas_);
    }

    // Peak input variance -> 0..255 on a -60..-12 dBFS scale.
    static uint8_t LevelOf(uint32_t variance) {
        if (variance == 0) return 0;
        float db = 10.0f * std::log10(float(variance) / (32768.0f * 32768.0f));
        float k = (db + 60.0f) / 48.0f;
        return uint8_t(std::clamp(k, 0.0f, 1.0f) * 255.0f);
    }

    static void SetChip(lv_obj_t* dot, lv_obj_t* label, uint32_t color, const char* text) {
        lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
        lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
        lv_label_set_text(label, text);
    }

    static void Mmss(char* out, size_t n, uint64_t ms) {
        uint64_t s = ms / 1000;
        std::snprintf(out, n, "%02u:%02u", unsigned(std::min<uint64_t>(s / 60, 99)), unsigned(s % 60));
    }

    void Show(bool on) {
        if (on == visible_) return;
        visible_ = on;
        if (on) {
            lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
            // The Muse screen keeps animating underneath; hiding it stops its
            // redraws (LVGL skips invalidating hidden objects), so the overlay
            // does not compete with the avatar for the single core.
            lv_obj_add_flag(lv_screen_active(), LV_OBJ_FLAG_HIDDEN);
            std::memset(levels_, 0, sizeof(levels_));
            shown_seconds_ = UINT32_MAX;
        } else {
            lv_obj_remove_flag(lv_screen_active(), LV_OBJ_FLAG_HIDDEN);
            lv_obj_invalidate(lv_screen_active());
        }
    }

    // LVGL task. Frees the overlay (objects, canvases, timer) once it has been
    // shown and is done; Create() builds it again for the next recording.
    void Destroy() {
        if (timer_) lv_timer_delete(timer_);
        if (root_) lv_obj_delete(root_);
        if (timer_buf_) lv_draw_buf_destroy(timer_buf_);
        if (wave_buf_) lv_draw_buf_destroy(wave_buf_);
        timer_ = nullptr;
        root_ = dot_ = title_ = uploaded_ = chip_dot_ = chip_ = timer_canvas_ = wave_canvas_ = nullptr;
        timer_buf_ = wave_buf_ = nullptr;
        visible_ = false;
    }

    void Tick() {
        if (timer_buf_ == nullptr || wave_buf_ == nullptr) return;
        auto s = recorder_->Snapshot();
        const uint64_t now = uint64_t(esp_timer_get_time() / 1000);
        const bool result_fresh = s.result != PassportRecorder::Result::None && now - s.result_at_ms < kResultShowMs;
        const bool active = s.state == Recorder::State::StartRequested || s.state == Recorder::State::Recording ||
                            s.state == Recorder::State::Finishing;
        const bool was_visible = visible_;
        Show(active || result_fresh);
        if (!visible_) {
            // Before the first show (Create() runs just ahead of the toggle) keep waiting.
            if (was_visible) Destroy();
            return;
        }
        ticks_++;
        char a[16], b[16], line[64];

        if (!active) {  // result screen
            lv_obj_add_flag(dot_, LV_OBJ_FLAG_HIDDEN);
            Mmss(a, sizeof(a), s.result_duration_ms);
            if (s.result == PassportRecorder::Result::Saved) {
                lv_label_set_text(title_, "已保存");
                lv_obj_set_style_text_color(title_, lv_color_hex(kGreen), 0);
                std::snprintf(line, sizeof(line), "时长 %s，已存到云端", a);
                SetChip(chip_dot_, chip_, kGreen, "可以在网页查看");
            } else if (s.result == PassportRecorder::Result::Partial) {
                lv_label_set_text(title_, "部分保存");
                lv_obj_set_style_text_color(title_, lv_color_hex(kAmber), 0);
                std::snprintf(line, sizeof(line), "时长 %s，有缺失片段", a);
                SetChip(chip_dot_, chip_, kAmber, "已保存可用部分");
            } else {
                lv_label_set_text(title_, "录音失败");
                lv_obj_set_style_text_color(title_, lv_color_hex(kRed), 0);
                std::snprintf(line, sizeof(line), "云端未确认保存");
                SetChip(chip_dot_, chip_, kRed, "请检查网络后重试");
            }
            lv_label_set_text(uploaded_, line);
            return;
        }

        lv_label_set_text(title_, "REC");
        lv_obj_set_style_text_color(title_, lv_color_hex(kRed), 0);
        // Blink the dot once a second while capturing.
        bool dot_on = s.state != Recorder::State::Recording || (ticks_ / 5) % 2 == 0;
        if (dot_on) lv_obj_remove_flag(dot_, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(dot_, LV_OBJ_FLAG_HIDDEN);

        uint32_t seconds = uint32_t(s.duration_ms / 1000);
        if (seconds != shown_seconds_) {
            shown_seconds_ = seconds;
            DrawTimer(seconds);
        }

        uint8_t level = s.state == Recorder::State::Recording ? LevelOf(Application::GetInstance().TakeInputPeakVariance()) : 0;
        std::memmove(levels_, levels_ + 1, kBars - 1);
        levels_[kBars - 1] = level;
        DrawWave();

        Mmss(a, sizeof(a), s.acked_ms);
        Mmss(b, sizeof(b), s.duration_ms);
        const uint64_t lag_ms = s.duration_ms > s.acked_ms ? s.duration_ms - s.acked_ms : 0;
        if (s.state == Recorder::State::StartRequested) {
            lv_label_set_text(uploaded_, "正在连接录音服务…");
            SetChip(chip_dot_, chip_, kDim, s.connected ? "等待云端确认" : "网络未连接");
        } else if (s.state == Recorder::State::Finishing) {
            std::snprintf(line, sizeof(line), "已上传 %s / %s", a, b);
            lv_label_set_text(uploaded_, line);
            SetChip(chip_dot_, chip_, kAmber, "正在保存…");
        } else {
            std::snprintf(line, sizeof(line), "已上传 %s", a);
            lv_label_set_text(uploaded_, line);
            if (!s.connected) {
                std::snprintf(line, sizeof(line), "网络中断 积压%u秒", unsigned(lag_ms / 1000));
                SetChip(chip_dot_, chip_, kRed, line);
            } else if (lag_ms > 3000) {
                std::snprintf(line, sizeof(line), "上传中 积压%u秒", unsigned(lag_ms / 1000));
                SetChip(chip_dot_, chip_, kAmber, line);
            } else {
                SetChip(chip_dot_, chip_, kGreen, "云端已确认");
            }
        }
    }
};
