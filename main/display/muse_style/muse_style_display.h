#ifndef MUSE_STYLE_DISPLAY_H
#define MUSE_STYLE_DISPLAY_H

#include "display/lcd_display.h"

#include <lvgl.h>
#include <cstdint>

// Muse-style screen for portrait panels: a pixel avatar in the middle, a
// status word in the mode's accent colour above it and the subtitle below.
// It reuses LcdDisplay's widgets for the status bar, notifications, the
// subtitle and the low battery popup, so the stock XiaoZhi texts still show.
class MuseStyleDisplay : public SpiLcdDisplay {
public:
    MuseStyleDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                     int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                     bool swap_xy);
    ~MuseStyleDisplay();

    virtual void SetupUI() override;
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetTheme(Theme* theme) override;

private:
    lv_obj_t* avatar_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    lv_image_dsc_t avatar_src_ = {};
    lv_draw_buf_t* strip_ = nullptr;
    bool strip_busy_ = false;
    uint8_t last_grid_[64 * 64] = {};
    bool last_grid_valid_ = false;

    int mode_ = -1;
    int64_t start_us_ = 0;
    int64_t mode_since_us_ = 0;
    int64_t happy_until_us_ = 0;
    int64_t error_until_us_ = 0;
    bool sleepy_ = false;

    void ApplyStyle();
    void CreateAvatar();
    void Tick();
    void InvalidateChangedCells();

    static MuseStyleDisplay* instance_;
    static lv_result_t DecoderInfo(lv_image_decoder_t* dec, lv_image_decoder_dsc_t* dsc,
                                   lv_image_header_t* header);
    static lv_result_t DecoderOpen(lv_image_decoder_t* dec, lv_image_decoder_dsc_t* dsc);
    static lv_result_t DecoderGetArea(lv_image_decoder_t* dec, lv_image_decoder_dsc_t* dsc,
                                      const lv_area_t* full, lv_area_t* area);
    static void DecoderClose(lv_image_decoder_t* dec, lv_image_decoder_dsc_t* dsc);
};

#endif  // MUSE_STYLE_DISPLAY_H
