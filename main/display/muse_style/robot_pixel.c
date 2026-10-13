/*
 * Pixel robot avatar for the Muse-style UI.
 *
 * "Mimi", an original character: a small robot girl. Pearl-white helmet
 * head with a porcelain face plate, a pink bob with bangs and a bow, big
 * eyes with lashes, glowing ear pods and a heart antenna, a lilac A-line
 * dress with a heart light on the chest. Cheerful and a bit shy.
 *
 * She is drawn on a 64x64 palette grid with ordered dithering and a hard
 * outline, then scaled up with nearest-neighbour blocks. Each block keeps a
 * darker last row and column, so the panel reads as an LED matrix.
 * Per-pixel work is integer only: the ESP32-C3 has no FPU.
 */
#include "robot_pixel.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#define W ROBOT_PX_W
#define H ROBOT_PX_H
#define TAU 6.2831853f

enum {
    C_BG = 0,
    C_OUT,
    C_P_D, C_P_M, C_P_L, C_P_H,     /* pearl body */
    C_H_D, C_H_M, C_H_L, C_H_H,     /* hair */
    C_F_M, C_F_D,                   /* face plate */
    C_EYE, C_IRIS, C_SHINE,
    C_BLUSH, C_MOUTH, C_TONGUE,
    C_S_D, C_S_M, C_S_L,            /* dress */
    C_BOW, C_BOW_D,
    C_ACC, C_ACC_D, C_GLOW, C_RIM,
    C_SHADOW, C_HEART,
    C_COUNT,
};

static uint8_t s_grid[H][W];
static uint16_t s_pal[C_COUNT];
static uint16_t s_pal_dim[C_COUNT];
static int s_size = W;
static int s_cell = 1;
static int s_ox, s_oy;   /* character offset, applied by put() */

static const uint8_t BAYER[4][4] = {
    { 0, 8, 2, 10 },
    { 12, 4, 14, 6 },
    { 3, 11, 1, 9 },
    { 15, 7, 13, 5 },
};

uint32_t robot_pixel_accent(robot_mode_t mode)
{
    switch (mode) {
    case ROBOT_MODE_BOOT:      return 0xA9C0FF;
    case ROBOT_MODE_IDLE:      return 0xA77DFF;
    case ROBOT_MODE_LISTENING: return 0x5CB8FF;
    case ROBOT_MODE_THINKING:  return 0xE07BFF;
    case ROBOT_MODE_SPEAKING:  return 0x6FF0BF;
    case ROBOT_MODE_ERROR:     return 0xFF5C5C;
    default:                   return 0x7C72D0;
    }
}

static uint16_t rgb565(uint32_t c)
{
    return (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
}

static uint32_t scale_rgb(uint32_t c, int k256)
{
    int r = (int)((c >> 16) & 0xFF) * k256 / 256;
    int g = (int)((c >> 8) & 0xFF) * k256 / 256;
    int b = (int)(c & 0xFF) * k256 / 256;
    r = r > 255 ? 255 : r;
    g = g > 255 ? 255 : g;
    b = b > 255 ? 255 : b;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static uint32_t mix_rgb(uint32_t a, uint32_t b, int k256)
{
    int r = (int)((a >> 16) & 0xFF) * (256 - k256) / 256 + (int)((b >> 16) & 0xFF) * k256 / 256;
    int g = (int)((a >> 8) & 0xFF) * (256 - k256) / 256 + (int)((b >> 8) & 0xFF) * k256 / 256;
    int bl = (int)(a & 0xFF) * (256 - k256) / 256 + (int)(b & 0xFF) * k256 / 256;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

static void set_palette(robot_mode_t mode, int dim256)
{
    uint32_t acc = robot_pixel_accent(mode);
    const uint32_t rgb[C_COUNT] = {
        [C_BG] = 0x000000,
        [C_OUT] = 0x241A35,
        [C_P_D] = 0x9A90B8, [C_P_M] = 0xCDC5E2, [C_P_L] = 0xECE8F7, [C_P_H] = 0xFCFBFF,
        [C_H_D] = 0x8A3C7E, [C_H_M] = 0xD0609F, [C_H_L] = 0xF48EC2, [C_H_H] = 0xFFC8E3,
        [C_F_M] = 0xFFF1EC, [C_F_D] = 0xF1D4D6,
        [C_EYE] = 0x2B1A44,
        [C_IRIS] = mix_rgb(acc, 0x2B1A44, 40),
        [C_SHINE] = 0xFFFFFF,
        [C_BLUSH] = 0xFF9DB8, [C_MOUTH] = 0xD9466F, [C_TONGUE] = 0xFF8FA6,
        [C_S_D] = 0x6A54B4, [C_S_M] = 0x9C88E8, [C_S_L] = 0xC6BAFF,
        [C_BOW] = 0xFF5E92, [C_BOW_D] = 0xC23B6B,
        [C_ACC] = acc,
        [C_ACC_D] = scale_rgb(acc, 115),
        [C_GLOW] = scale_rgb(acc, 52),
        [C_RIM] = mix_rgb(acc, 0xFFFFFF, 90),
        [C_SHADOW] = 0x1A1726,
        [C_HEART] = 0xFF4D7D,
    };
    for (int i = 0; i < C_COUNT; i++) {
        uint32_t c = i == C_BG ? rgb[i] : scale_rgb(rgb[i], dim256);
        s_pal[i] = rgb565(c);
        s_pal_dim[i] = rgb565(scale_rgb(c, 184));
    }
}

/* ---- drawing helpers ---- */

static void put_raw(int x, int y, uint8_t c)
{
    if (x >= 0 && x < W && y >= 0 && y < H) {
        s_grid[y][x] = c;
    }
}

static void put(int x, int y, uint8_t c)
{
    put_raw(x + s_ox, y + s_oy, c);
}

static uint8_t get_raw(int x, int y)
{
    return (x >= 0 && x < W && y >= 0 && y < H) ? s_grid[y][x] : C_BG;
}

static bool is_bg(uint8_t c)
{
    return c == C_BG || c == C_GLOW;
}

/* Pick one of four tones from a light value with ordered dithering. */
static uint8_t tone(int light, int x, int y, const uint8_t t[4])
{
    int v = light + BAYER[y & 3][x & 3] * 8 - 60;
    return v > 150 ? t[3] : v > 40 ? t[2] : v > -70 ? t[1] : t[0];
}

static const uint8_t PEARL[4] = { C_P_D, C_P_M, C_P_L, C_P_H };
static const uint8_t HAIR[4] = { C_H_D, C_H_M, C_H_L, C_H_H };
static const uint8_t FACE[4] = { C_F_D, C_F_D, C_F_M, C_F_M };
static const uint8_t DRESS[4] = { C_S_D, C_S_M, C_S_L, C_S_L };

/*
 * Shaded ellipse. Centre and radii are in half pixels, so shapes can sit on
 * the 31.5 centre line. Light comes from the top left; with outline, the
 * edge ring is drawn dark so parts read apart.
 */
static void ellipse(int cx2, int cy2, int rx2, int ry2, const uint8_t t[4], bool outline,
                    bool rim, int y_max)
{
    /* X, Y and the radii stay under 64 half pixels, so int32 does not overflow */
    int32_t rr = rx2 * rx2 * ry2 * ry2;
    int ix2 = rx2 - 2, iy2 = ry2 - 2;
    int32_t ir = ix2 * ix2 * iy2 * iy2;
    int32_t rx2s = rx2 * rx2, ry2s = ry2 * ry2, ix2s = ix2 * ix2, iy2s = iy2 * iy2;
    for (int y = (cy2 - ry2) / 2 - 1; y <= (cy2 + ry2) / 2 + 1 && y <= y_max; y++) {
        for (int x = (cx2 - rx2) / 2 - 1; x <= (cx2 + rx2) / 2 + 1; x++) {
            int X = 2 * x + 1 - cx2, Y = 2 * y + 1 - cy2;
            int32_t xx = X * X, yy = Y * Y;
            if (xx * ry2s + yy * rx2s > rr) {
                continue;
            }
            if (outline && xx * iy2s + yy * ix2s > ir) {
                put(x, y, C_OUT);
                continue;
            }
            int nx = X * 256 / rx2, ny = Y * 256 / ry2;
            int z = 256 - (nx * nx + ny * ny) / 256;
            if (rim && nx > 150 && z < 90 && BAYER[y & 3][x & 3] < 8) {
                put(x, y, C_RIM);
                continue;
            }
            put(x, y, tone((-nx - 2 * ny) / 3 + z / 2 - 64, x, y, t));
        }
    }
}

/* Shaded capsule from (ax, ay) to (bx, by), radius r2 in half pixels. */
static void capsule(int ax, int ay, int bx, int by, int r2, const uint8_t t[4])
{
    int ax2 = 2 * ax + 1, ay2 = 2 * ay + 1, bx2 = 2 * bx + 1, by2 = 2 * by + 1;
    int dx = bx2 - ax2, dy = by2 - ay2;
    int len2 = dx * dx + dy * dy;
    int x0 = (ax < bx ? ax : bx) - r2 / 2 - 1, x1 = (ax > bx ? ax : bx) + r2 / 2 + 1;
    int y0 = (ay < by ? ay : by) - r2 / 2 - 1, y1 = (ay > by ? ay : by) + r2 / 2 + 1;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            int px = 2 * x + 1 - ax2, py = 2 * y + 1 - ay2;
            int k = len2 > 0 ? (px * dx + py * dy) * 256 / len2 : 0;
            k = k < 0 ? 0 : (k > 256 ? 256 : k);
            int ex = px - dx * k / 256, ey = py - dy * k / 256;
            int d2 = ex * ex + ey * ey;
            if (d2 > r2 * r2) {
                continue;
            }
            if (d2 > (r2 - 2) * (r2 - 2)) {
                put(x, y, C_OUT);
                continue;
            }
            int nx = ex * 256 / r2, ny = ey * 256 / r2;
            put(x, y, tone((-nx - 2 * ny) / 3 + 40, x, y, t));
        }
    }
}

/* Stamp a small bitmap: '.' skips, other characters map through `map`. */
static void stamp(int x, int y, const char* const* rows, int n, const char* keys,
                  const uint8_t* colors)
{
    for (int r = 0; r < n; r++) {
        for (int c = 0; rows[r][c]; c++) {
            for (int k = 0; keys[k]; k++) {
                if (rows[r][c] == keys[k]) {
                    put(x + c, y + r, colors[k]);
                }
            }
        }
    }
}

static void sparkle_raw(int x, int y, int arm, uint8_t c, bool behind)
{
    const int dx[5] = { 0, -1, 1, 0, 0 }, dy[5] = { 0, 0, 0, -1, 1 };
    for (int a = 1; a <= arm; a++) {
        for (int i = 1; i < 5; i++) {
            int px = x + dx[i] * a, py = y + dy[i] * a;
            if (!behind || is_bg(get_raw(px, py))) {
                put_raw(px, py, c);
            }
        }
    }
    if (!behind || is_bg(get_raw(x, y))) {
        put_raw(x, y, C_SHINE);
    }
}

static void heart_raw(int x, int y)
{
    static const char* const ROWS[] = { ".##.##.", "#######", "#######", ".#####.", "..###..", "...#..." };
    for (int r = 0; r < 6; r++) {
        for (int c = 0; c < 7; c++) {
            if (ROWS[r][c] == '#') {
                put_raw(x + c, y + r, (r == 1 && c == 1) ? C_SHINE : C_HEART);
            }
        }
    }
}

/* cos over one turn in 64 steps, x127; sin(i) = COS64[(i + 48) & 63] */
static const int8_t COS64[64] = {
    127, 126, 125, 122, 117, 112, 106, 98, 90, 81, 71, 60, 49, 37, 25, 12,
    0, -12, -25, -37, -49, -60, -71, -81, -90, -98, -106, -112, -117, -122, -125, -126,
    -127, -126, -125, -122, -117, -112, -106, -98, -90, -81, -71, -60, -49, -37, -25, -12,
    0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126,
};

static int cos64(int i) { return COS64[i & 63]; }
static int sin64(int i) { return COS64[(i + 48) & 63]; }

/* Turns seconds times a rate (rad/s) into a 1/64-turn index. */
static int turn64(float t, float rate) { return (int)(t * rate * (64.0f / TAU)); }

static int isqrt(int v)
{
    int r = 0, b = 1 << 14;
    while (b > v) {
        b >>= 2;
    }
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}

/* ---- animation state ---- */

typedef struct {
    int eyes;             /* EYES_* */
    int mouth;            /* MOUTH_* */
    int mouth_open;       /* rows, for MOUTH_OPEN */
    int gx, gy;           /* gaze */
    int lhx, lhy, rhx, rhy;  /* hands */
    bool breathe;
    bool blush_more;
} face_t;

enum { EYES_OPEN, EYES_WIDE, EYES_CLOSED, EYES_HAPPY, EYES_X };
enum { MOUTH_SMILE, MOUTH_SMALL, MOUTH_O, MOUTH_OPEN, MOUTH_HMM, MOUTH_FLAT, MOUTH_GRIN };

static float s_prev_t = -1.0f;
static float s_next_blink = 2.0f, s_blink_at = -10.0f;
static bool s_double_blink;
static float s_next_gaze = 1.5f, s_gaze_x, s_gaze_y, s_gaze_tx, s_gaze_ty;
static uint32_t s_seed = 12345;

static float rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return (float)((s_seed >> 8) & 0xFFFF) / 65535.0f;
}

/* ---- the character ---- */

static void draw_eye(int x, int y, int kind, int gx, int gy, bool left)
{
    static const char* const OPEN[] = { "dddd", "dsdd", "dddd", "diid", ".dd." };
    static const char* const WIDE[] = { ".dd.", "dddd", "dsdd", "dddd", "diid", ".dd." };
    static const char* const CLOSED[] = { "....", "....", "d..d", ".dd." };
    static const char* const HAPPY[] = { "....", ".dd.", "d..d" };
    static const char* const XE[] = { "a..a", ".aa.", ".aa.", "a..a" };
    static const char KEYS[] = "dsia";
    static const uint8_t COLS[] = { C_EYE, C_SHINE, C_IRIS, C_ACC };
    int lash = left ? x - 1 : x + 4;
    switch (kind) {
    case EYES_WIDE:
        stamp(x + gx, y - 1 + gy, WIDE, 6, KEYS, COLS);
        put(lash, y - 1, C_EYE);
        break;
    case EYES_CLOSED:
        stamp(x, y, CLOSED, 4, KEYS, COLS);
        put(lash, y + 1, C_EYE);
        break;
    case EYES_HAPPY:
        stamp(x, y, HAPPY, 3, KEYS, COLS);
        break;
    case EYES_X:
        stamp(x, y, XE, 4, KEYS, COLS);
        break;
    default:
        stamp(x + gx, y + gy, OPEN, 5, KEYS, COLS);
        put(lash, y, C_EYE);
        put(left ? lash - 1 : lash + 1, y - 1, C_EYE);
        break;
    }
}

static void draw_mouth(const face_t* f)
{
    switch (f->mouth) {
    case MOUTH_SMALL:
        put(31, 29, C_MOUTH);
        put(32, 29, C_MOUTH);
        break;
    case MOUTH_O:
        for (int y = 28; y <= 29; y++) {
            put(31, y, C_MOUTH);
            put(32, y, C_MOUTH);
        }
        break;
    case MOUTH_OPEN:
    case MOUTH_GRIN: {
        int x0 = f->mouth == MOUTH_GRIN ? 29 : 30, x1 = f->mouth == MOUTH_GRIN ? 34 : 33;
        int h = f->mouth == MOUTH_GRIN ? 2 : f->mouth_open;
        for (int y = 28; y <= 28 + h; y++) {
            for (int x = x0; x <= x1; x++) {
                bool corner = (x == x0 || x == x1) && (y == 28 + h);
                if (!corner) {
                    put(x, y, (y == 28 + h && h >= 2) ? C_TONGUE : C_MOUTH);
                }
            }
        }
        break;
    }
    case MOUTH_HMM:
        put(32, 29, C_MOUTH);
        put(33, 29, C_MOUTH);
        put(34, 28, C_MOUTH);
        break;
    case MOUTH_FLAT:
        for (int x = 30; x <= 33; x++) {
            put(x, 29, C_MOUTH);
        }
        break;
    default:
        put(30, 28, C_MOUTH);
        put(31, 29, C_MOUTH);
        put(32, 29, C_MOUTH);
        put(33, 28, C_MOUTH);
        break;
    }
}

static void draw_character(const face_t* f, float t, int antenna_lit)
{
    /* legs and boots */
    capsule(28, 50, 28, 54, 4, PEARL);
    capsule(35, 50, 35, 54, 4, PEARL);
    static const char* const BOOT_L[] = { ".LSS.", "LSSSD", "oDDDo" };
    static const char* const BOOT_R[] = { ".SSL.", "DSSSL", "oDDDo" };
    static const uint8_t BOOTC[] = { C_S_L, C_S_M, C_S_D, C_OUT };
    stamp(25, 54, BOOT_L, 3, "LSDo", BOOTC);
    stamp(34, 54, BOOT_R, 3, "LSDo", BOOTC);

    /* dress: A-line from the waist, with pleats */
    for (int y = 41; y <= 50; y++) {
        int hw2 = 13 + (y - 41) * 12 / 9;      /* half width, half pixels */
        int xa = (63 - hw2 + 1) / 2, xb = (63 + hw2) / 2;
        for (int x = xa; x <= xb; x++) {
            uint8_t c;
            if (x == xa || x == xb || y == 50) {
                c = C_OUT;
            } else if (y == 49) {
                c = C_S_D;
            } else if (((x * 2 + 1 - 63 + 64) / 2) % 3 == 0) {
                c = C_S_D;
            } else {
                c = tone((32 - x) * 14 + (45 - y) * 6, x, y, DRESS);
            }
            put(x, y, c);
        }
    }

    /* torso */
    ellipse(63, 77, 15, f->breathe ? 13 : 12, PEARL, true, true, 99);
    /* collar */
    static const int CX[3] = { 28, 29, 30 }, CY[3] = { 33, 34, 35 };
    for (int i = 0; i < 3; i++) {
        put(CX[i], CY[i], C_H_M);
        put(63 - CX[i], CY[i], C_H_M);
    }
    /* chest heart light */
    static const char* const CHEST[] = { ".a.a.", "aaaaa", ".aaa.", "..a.." };
    static const uint8_t ACC1[] = { C_ACC };
    static const uint8_t ACCD[] = { C_ACC_D };
    stamp(29, 37, CHEST, 4, "a", antenna_lit ? ACC1 : ACCD);
    put(30, 38, C_SHINE);

    /* neck */
    for (int x = 30; x <= 33; x++) {
        put(x, 31, x == 30 || x == 33 ? C_OUT : C_P_D);
        put(x, 32, x == 30 || x == 33 ? C_OUT : C_P_M);
    }

    /* head, face plate */
    ellipse(63, 40, 32, 28, PEARL, true, true, 99);
    ellipse(63, 47, 21, 16, FACE, false, false, 99);

    /* hair: cap, side locks curling in, bangs */
    ellipse(63, 33, 34, 25, HAIR, true, true, 17);
    for (int y = 16; y <= 31; y++) {
        for (int x = 14; x <= 19; x++) {
            bool round = (y == 31 && (x == 14 || x == 19)) || (y >= 30 && x == 14);
            if (!round) {
                uint8_t c = (x == 14 || y == 31) ? C_OUT : tone((17 - x) * 30 + (24 - y) * 4, x, y, HAIR);
                put(x, y, c);
                put(63 - x, y, (x == 14 || y == 31) ? C_OUT : tone((x - 17) * 30 + (24 - y) * 4 - 40, x, y, HAIR));
            }
        }
    }
    put(20, 30, C_H_M);
    put(43, 30, C_H_M);
    for (int x = 19; x <= 44; x++) {
        int bottom = 15 + ((x + 1) % 5 < 2 ? 1 : 0) + (x < 22 || x > 41 ? 2 : 0);
        for (int y = 11; y <= bottom; y++) {
            put(x, y, y == bottom ? C_H_D : tone((32 - x) * 6 + (13 - y) * 30, x, y, HAIR));
        }
    }
    /* hair shine */
    for (int x = 22; x <= 27; x++) {
        if (BAYER[1][x & 3] < 10) {
            put(x, 10, C_H_H);
        }
    }
    put(36, 10, C_H_H);
    put(37, 10, C_H_H);

    /* ear pods */
    ellipse(28, 45, 8, 9, PEARL, true, false, 99);
    ellipse(99, 45, 8, 9, PEARL, true, false, 99);
    for (int y = 21; y <= 23; y++) {
        put(14, y, antenna_lit ? C_ACC : C_ACC_D);
        put(49, y, antenna_lit ? C_ACC : C_ACC_D);
    }

    /* antenna with a heart tip */
    for (int i = 0; i < 6; i++) {
        put(50 + i / 2, 17 - i, C_P_D);
    }
    static const char* const TIP[] = { "a.a", "aaa", ".a." };
    stamp(52, 8, TIP, 3, "a", antenna_lit ? ACC1 : ACCD);

    /* bow */
    static const char* const BOW[] = { "##...##", "#h#k#h#", "##kkk##", "#d...d#" };
    static const uint8_t BOWC[] = { C_BOW, C_SHINE, C_BOW_D, C_BOW_D };
    stamp(16, 6, BOW, 4, "#hkd", BOWC);

    /* face */
    draw_eye(24, 21, f->eyes, f->gx, f->gy, true);
    draw_eye(36, 21, f->eyes, f->gx, f->gy, false);
    if (f->eyes != EYES_X) {
        put(22, 26, C_BLUSH);
        put(23, 26, C_BLUSH);
        put(40, 26, C_BLUSH);
        put(41, 26, C_BLUSH);
        if (f->blush_more) {
            put(21, 26, C_BLUSH);
            put(42, 26, C_BLUSH);
        }
    }
    draw_mouth(f);

    /* arms: shoulder to hand, in front */
    capsule(25, 35, f->lhx, f->lhy, 4, PEARL);
    capsule(38, 35, f->rhx, f->rhy, 4, PEARL);
    static const char* const HAND[] = { ".ooo.", "oHLLo", "oLLMo", "oLMMo", ".ooo." };
    static const uint8_t HANDC[] = { C_OUT, C_P_H, C_P_L, C_P_M };
    stamp(f->lhx - 2, f->lhy - 2, HAND, 5, "oHLM", HANDC);
    stamp(f->rhx - 2, f->rhy - 2, HAND, 5, "oHLM", HANDC);
    (void)t;
}

/* Hard 1 px outline around the silhouette. */
static void outline_pass(void)
{
    static uint8_t row_above[W];
    memset(row_above, 0, sizeof(row_above));
    for (int y = 0; y < H; y++) {
        uint8_t cur[W];
        memcpy(cur, s_grid[y], W);
        for (int x = 0; x < W; x++) {
            if (cur[x] != C_BG) {
                continue;
            }
            bool edge = (x > 0 && cur[x - 1] != C_BG && cur[x - 1] != C_OUT) ||
                        (x < W - 1 && cur[x + 1] != C_BG && cur[x + 1] != C_OUT) ||
                        (row_above[x] != C_BG && row_above[x] != C_OUT) ||
                        (y < H - 1 && s_grid[y + 1][x] != C_BG && s_grid[y + 1][x] != C_OUT);
            if (edge) {
                s_grid[y][x] = C_OUT;
            }
        }
        memcpy(row_above, cur, W);
    }
}

static void draw_halo(int k256, float t)
{
    /* density falls off from radius 16 to 32; a slow ripple moves through it */
    int phase = (int)(t * 1.7f * 40.0f);
    for (int y = 0; y < H; y++) {
        int dy = (y * 2 + 1 - 66) * 11 / 10;
        for (int x = 0; x < W; x++) {
            if (s_grid[y][x] != C_BG) {
                continue;
            }
            int dx = x * 2 + 1 - 63;
            int d2 = dx * dx + dy * dy;               /* half pixels, squared */
            if (d2 <= 32 * 32 || d2 > 68 * 68) {     /* inside 16 px or past 34 px */
                continue;
            }
            int d4 = isqrt(d2) * 2;                   /* distance in 1/4 px */
            int ripple = ((phase + d4 * 5) & 255) < 128 ? 20 : -20;
            int v = (128 - d4) * k256 / 64 + ripple;
            if (v > (BAYER[y & 3][x & 3] << 4)) {
                s_grid[y][x] = C_GLOW;
            }
        }
    }
}

/* Dots on an ellipse around her; r2 is the radius in half pixels. */
static void dotted_ring(int r2, int spin64, int dots, uint8_t c)
{
    for (int k = 0; k < dots; k++) {
        int i = k * 64 / dots + spin64;
        int x = (63 + cos64(i) * r2 / 127) >> 1;
        int y = (66 + sin64(i) * r2 * 19 / 20 / 127) >> 1;
        if (is_bg(get_raw(x, y))) {
            put_raw(x, y, c);
        }
    }
}

void robot_pixel_render(const robot_pose_t* p)
{
    float t = p->t, mt = p->mode_t;
    float dt = s_prev_t < 0 ? 0.04f : t - s_prev_t;
    dt = dt < 0 ? 0 : (dt > 0.2f ? 0.2f : dt);
    s_prev_t = t;
    bool happy = p->happy > 0.05f && p->mode != ROBOT_MODE_ERROR;
    float lvl = p->level > 0.02f ? p->level : 0.5f + 0.5f * sinf(t * 11.0f) * sinf(t * 3.7f);

    /* blink every 2.2-5.2 s, sometimes twice */
    if (t >= s_next_blink) {
        s_blink_at = t;
        s_double_blink = rnd() < 0.25f;
        s_next_blink = t + 2.2f + rnd() * 3.0f;
    }
    float since = t - s_blink_at;
    bool blink = since < 0.16f || (s_double_blink && since > 0.3f && since < 0.46f);

    /* gaze wanders and eases to its target */
    if (t >= s_next_gaze) {
        s_gaze_tx = rnd() * 2.0f - 1.0f;
        s_gaze_ty = rnd() < 0.3f ? -1.0f : 0.0f;
        s_next_gaze = t + 1.2f + rnd() * 2.4f;
    }
    float ease = 1.0f - expf(-dt * 14.0f);
    s_gaze_x += (s_gaze_tx - s_gaze_x) * ease;
    s_gaze_y += (s_gaze_ty - s_gaze_y) * ease;

    float sway = sinf(t * 1.8f);
    face_t f = {
        .eyes = blink ? EYES_CLOSED : EYES_OPEN,
        .mouth = MOUTH_SMILE,
        .gx = (int)lroundf(s_gaze_x * 0.8f),
        .gy = (int)lroundf(s_gaze_y * 0.6f),
        .lhx = 21, .lhy = 44 + (sway > 0.5f), .rhx = 42, .rhy = 44 + (sway < -0.5f),
        .breathe = sinf(t * 2.4f) > 0.3f,
    };
    int dim = 256, halo = 90;
    int antenna_lit = sinf(t * 3.0f) > -0.2f;
    s_ox = 0;
    s_oy = sinf(t * 1.6f) > 0.6f ? -1 : 0;

    switch (p->mode) {
    case ROBOT_MODE_BOOT:
        s_oy = mt < 0.6f ? (int)((1.0f - mt / 0.6f) * 5.0f) : 0;
        if (mt < 0.9f) {
            f.eyes = EYES_CLOSED;
        }
        f.mouth = MOUTH_SMALL;
        antenna_lit = ((int)(t * 4.0f)) & 1;
        halo = mt < 1.5f ? (int)(mt / 1.5f * 90) : 90;
        break;
    case ROBOT_MODE_LISTENING:
        f.eyes = blink ? EYES_CLOSED : EYES_WIDE;
        f.mouth = MOUTH_O;
        f.gx = f.gy = 0;
        f.lhx = 17; f.lhy = 27; f.rhx = 46; f.rhy = 27;
        antenna_lit = 1;
        halo = 110 + (int)(lvl * 80);
        break;
    case ROBOT_MODE_THINKING:
        f.mouth = MOUTH_HMM;
        f.gx = sinf(t * 2.0f) > 0 ? 1 : -1;
        f.gy = -1;
        f.lhx = 21; f.lhy = 43; f.rhx = 39; f.rhy = 32;
        s_ox = sinf(t * 1.2f) > 0.4f ? 1 : 0;
        antenna_lit = ((int)(t * 3.0f)) & 1;
        break;
    case ROBOT_MODE_SPEAKING:
        f.mouth = MOUTH_OPEN;
        f.mouth_open = 1 + (int)(lvl * 2.5f);
        f.blush_more = true;
        f.lhx = 21; f.lhy = 42;
        f.rhx = 44 + (sinf(t * 4.0f) > 0); f.rhy = 36 - (int)(lvl * 4.0f);
        s_oy = lvl > 0.6f ? -1 : 0;
        break;
    case ROBOT_MODE_ERROR:
        f.eyes = EYES_X;
        f.mouth = MOUTH_FLAT;
        f.lhx = 20; f.lhy = 41; f.rhx = 43; f.rhy = 41;
        s_ox = mt < 0.6f ? (((int)(mt * 30.0f)) & 1 ? 1 : -1) : 0;
        antenna_lit = ((int)(t * 2.0f)) & 1;
        break;
    case ROBOT_MODE_OFF: {
        float k = mt < 1.3f ? 1.0f - mt / 1.3f : 0.0f;
        f.eyes = mt > 0.5f ? EYES_CLOSED : f.eyes;
        f.mouth = MOUTH_SMALL;
        f.rhx = 46 + (sinf(t * 12.0f) > 0 ? 1 : -1); f.rhy = 27;
        if (mt > 1.3f) {
            f.rhx = 42; f.rhy = 44;
        }
        dim = 140 + (int)(k * 116);
        halo = (int)(k * 90);
        antenna_lit = 0;
        break;
    }
    default:
        break;
    }
    if (happy) {
        f.eyes = EYES_HAPPY;
        f.mouth = MOUTH_GRIN;
        f.blush_more = true;
        f.lhx = 18 + (sinf(t * 14.0f) > 0); f.lhy = 26;
        f.rhx = 45 - (sinf(t * 14.0f) > 0); f.rhy = 26;
        s_oy = sinf(t * 9.0f) > 0 ? -2 : 0;
    }

    set_palette(p->mode, dim);
    memset(s_grid, C_BG, sizeof(s_grid));

    /* floor shadow, not moved by the bob */
    for (int y = 57; y <= 59; y++) {
        for (int x = 18; x <= 45; x++) {
            int dx = x * 2 + 1 - 63, dy = (y - 58) * 13;
            if (dx * dx + dy * dy <= 26 * 26 && BAYER[y & 3][x & 3] < 9) {
                s_grid[y][x] = C_SHADOW;
            }
        }
    }

    draw_character(&f, t, antenna_lit);
    s_ox = s_oy = 0;
    outline_pass();
    /* Idle: a still halo. The moving ripple flips cells all over the ring, so
     * nearly the whole avatar was redrawn every frame (2026-10-13 profile). */
    draw_halo(halo, p->mode == ROBOT_MODE_IDLE ? 0.0f : t);

    /* sparkles orbit around her: behind first, then in front */
    if (p->mode != ROBOT_MODE_OFF && p->mode != ROBOT_MODE_ERROR) {
        int n = p->mode == ROBOT_MODE_BOOT ? (int)(mt / 0.4f) : 3;
        float speed = p->mode == ROBOT_MODE_THINKING ? 1.4f : 0.6f;
        for (int i = 0; i < 3 && i < n; i++) {
            int a = turn64(t, speed) + i * 21;
            int x = (63 + cos64(a) * 54 / 127) >> 1;
            int y = (64 + sin64(a) * 40 / 127) >> 1;
            bool behind = sin64(a) < 0;
            int arm = sinf(t * 2.2f + i * 1.7f) > 0.5f ? 2 : 1;
            sparkle_raw(x, y, arm, C_ACC, behind);
        }
    }

    switch (p->mode) {
    case ROBOT_MODE_LISTENING: {
        int r2 = 44 + (int)(mt * 24.0f) % 18;
        dotted_ring(r2, 0, 28, r2 < 52 ? C_ACC : C_ACC_D);
        for (int i = 0; i < 3; i++) {
            float ph = sinf(t * 9.0f + i * 1.3f) * 0.5f + 0.5f;
            int h = 2 + (int)((lvl * 0.8f + 0.2f) * ph * 7.0f);
            int xl = 6 - i * 2, xr = 57 + i * 2;
            for (int y = 24 - h / 2; y <= 24 + h / 2; y++) {
                put_raw(xl, y, i == 0 ? C_RIM : C_ACC);
                put_raw(xr, y, i == 0 ? C_RIM : C_ACC);
            }
        }
        break;
    }
    case ROBOT_MODE_THINKING: {
        static const int DX[3] = { 46, 51, 56 }, DY[3] = { 7, 4, 1 };
        int lit = (int)(t * 3.0f) % 3;
        for (int i = 0; i < 3; i++) {
            for (int yy = 0; yy <= (i == 2); yy++) {
                for (int xx = 0; xx <= (i == 2); xx++) {
                    put_raw(DX[i] + xx, DY[i] + yy, i == lit ? C_RIM : C_ACC_D);
                }
            }
        }
        break;
    }
    case ROBOT_MODE_SPEAKING:
        dotted_ring(54 + (int)(lvl * 4.0f), turn64(t, 0.8f), 32, C_ACC);
        break;
    case ROBOT_MODE_ERROR:
        for (int y = 2; y <= 8; y++) {
            put_raw(55, y, C_ACC);
            put_raw(56, y, C_ACC);
        }
        put_raw(55, 10, C_ACC);
        put_raw(56, 10, C_ACC);
        break;
    default:
        break;
    }

    if (happy) {
        for (int i = 0; i < 2; i++) {
            float rise = fmodf(t * 8.0f + i * 7.0f, 14.0f);
            heart_raw(i ? 50 : 6, (i ? 18 : 14) - (int)rise);
        }
    }
}

const uint8_t* robot_pixel_grid(void)
{
    return &s_grid[0][0];
}

#define MAX_PX 512
static uint8_t s_cell_of[MAX_PX];   /* screen pixel -> cell */
static bool s_last_of[MAX_PX];      /* screen pixel is the last of its cell */

void robot_pixel_set_size(int px)
{
    s_size = px < W ? W : (px > MAX_PX ? MAX_PX : px);
    s_cell = s_size / W;
    /* Pixel p is in cell floor(p * 64 / size); a cell starts at ceil(c * size / 64). */
    for (int p = 0; p < s_size; p++) {
        int c = p * W / s_size;
        s_cell_of[p] = (uint8_t)c;
        s_last_of[p] = s_cell >= 3 && ((c + 1) * s_size + W - 1) / W - 1 == p;
    }
}

void robot_pixel_scale(uint16_t* dst, int stride_px, int x0, int x1, int y0, int y1)
{
    for (int y = y0; y <= y1; y++) {
        const uint8_t* row = s_grid[s_cell_of[y]];
        bool y_last = s_last_of[y];
        uint16_t* out = dst + (y - y0) * stride_px;
        for (int x = x0; x <= x1; x++) {
            uint8_t c = row[s_cell_of[x]];
            out[x - x0] = (y_last || s_last_of[x]) ? s_pal_dim[c] : s_pal[c];
        }
    }
}
