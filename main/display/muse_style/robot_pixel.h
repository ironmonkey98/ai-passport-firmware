/*
 * Pixel robot avatar for the Muse-style UI.
 *
 * The robot is drawn on a ROBOT_PX_W x ROBOT_PX_H palette grid and scaled up
 * with nearest-neighbour blocks, one strip at a time, so the full-size image
 * never has to exist in RAM.
 */
#ifndef ROBOT_PIXEL_H
#define ROBOT_PIXEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROBOT_PX_W 64
#define ROBOT_PX_H 64

/* Same order as Muse's muse_mode_t. */
typedef enum {
    ROBOT_MODE_BOOT = 0,
    ROBOT_MODE_IDLE,
    ROBOT_MODE_LISTENING,
    ROBOT_MODE_THINKING,
    ROBOT_MODE_SPEAKING,
    ROBOT_MODE_ERROR,
    ROBOT_MODE_OFF,
    ROBOT_MODE_COUNT,
} robot_mode_t;

typedef struct {
    robot_mode_t mode;
    float t;         /* seconds since start */
    float mode_t;    /* seconds in current mode */
    float level;     /* 0..1 audio level; 0 animates on its own */
    float happy;     /* 0..1 happy reaction */
} robot_pose_t;

/* Accent colour of a mode, as 0xRRGGBB. */
uint32_t robot_pixel_accent(robot_mode_t mode);

/* Render one frame into the grid. */
void robot_pixel_render(const robot_pose_t* pose);

/* The grid of the last frame: ROBOT_PX_W * ROBOT_PX_H palette indexes, row by row. */
const uint8_t* robot_pixel_grid(void);

/* Screen size (square, in pixels) that robot_pixel_scale() writes. */
void robot_pixel_set_size(int px);

/* Write screen pixels [x0, x1] x [y0, y1] as RGB565, stride_px apart. */
void robot_pixel_scale(uint16_t* dst, int stride_px, int x0, int x1, int y0, int y1);

#ifdef __cplusplus
}
#endif

#endif  // ROBOT_PIXEL_H
