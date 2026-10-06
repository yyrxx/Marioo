// Mini Mario - simple platformer for PS3 (PSL1GHT)
// Controls: D-pad left/right = move, X (Cross) = jump
// Stomp enemies from above, collect coins, reach the flag at the end.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <ppu-types.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <rsx/rsx.h>
#include <io/pad.h>

/* ---------------- Video ---------------- */

static gcmContextData *ctx;
static videoResolution res;
static u32 *fb[2];
static u32 fbOff[2];
static int curBuf = 0;
static int firstFlip = 1;
static int running = 1;

static void sysCb(u64 status, u64 param, void *usrdata)
{
    if (status == SYSUTIL_EXIT_GAME) running = 0;
}

static void waitFlip(void)
{
    while (gcmGetFlipStatus() != 0) usleep(200);
    gcmResetFlipStatus();
}

static void flip(int id)
{
    if (!firstFlip) waitFlip();
    else gcmResetFlipStatus();
    gcmSetFlip(ctx, id);
    rsxFlushBuffer(ctx);
    gcmSetWaitFlip(ctx);
    firstFlip = 0;
}

static void initVideo(void)
{
    void *host = memalign(1024 * 1024, 1024 * 1024);
    gcmInitBody(&ctx, 0x100000, 1024 * 1024, host);

    videoState st;
    videoGetState(0, 0, &st);
    videoGetResolution(st.displayMode.resolution, &res);

    videoConfiguration vc;
    memset(&vc, 0, sizeof(vc));
    vc.resolution = st.displayMode.resolution;
    vc.format = VIDEO_BUFFER_FORMAT_XRGB;
    vc.pitch = res.width * 4;
    videoConfigure(0, &vc, NULL, 0);

    gcmSetFlipMode(GCM_FLIP_VSYNC);

    for (int i = 0; i < 2; i++) {
        fb[i] = (u32 *)rsxMemalign(64, res.width * res.height * 4);
        rsxAddressToOffset(fb[i], &fbOff[i]);
        gcmSetDisplayBuffer(i, fbOff[i], res.width * 4, res.width, res.height);
    }
    gcmResetFlipStatus();
}

/* Logical screen is 320x180, scaled up to the real resolution */
#define LW 320
#define LH 180
static int scale, offX, offY;

static void fillRect(float x, float y, float w, float h, u32 color)
{
    int px = offX + (int)(x * scale);
    int py = offY + (int)(y * scale);
    int pw = (int)(w * scale);
    int ph = (int)(h * scale);
    if (px < 0) { pw += px; px = 0; }
    if (py < 0) { ph += py; py = 0; }
    if (px + pw > (int)res.width) pw = res.width - px;
    if (py + ph > (int)res.height) ph = res.height - py;
    if (pw <= 0 || ph <= 0) return;
    u32 *buf = fb[curBuf];
    for (int j = 0; j < ph; j++) {
        u32 *row = buf + (py + j) * res.width + px;
        for (int i = 0; i < pw; i++) row[i] = color;
    }
}

static void clearScreen(u32 color)
{
    u32 *buf = fb[curBuf];
    int n = res.width * res.height;
    for (int i = 0; i < n; i++) buf[i] = color;
}

/* ---------------- Game ---------------- */

typedef struct { float x, y, w, h; } Rect;

#define NPLAT 11
static const Rect plats[NPLAT] = {
    {0, 150, 400, 30},   {440, 150, 300, 30}, {780, 150, 520, 30},
    {120, 115, 50, 8},   {230, 90, 50, 8},    {500, 112, 60, 8},
    {600, 82, 50, 8},    {860, 112, 60, 8},   {960, 85, 60, 8},
    {1060, 112, 50, 8},  {330, 120, 40, 8}
};

#define NCOIN 12
static float coinX[NCOIN] = {135, 245, 345, 515, 615, 700, 875, 975, 1075, 1150, 80, 300};
static float coinY[NCOIN] = {100, 75, 105, 97, 67, 130, 97, 70, 97, 130, 130, 130};
static int coinGot[NCOIN];

#define NENEMY 4
typedef struct { float x, y, dir, minX, maxX; int alive; } Enemy;
static Enemy enemies[NENEMY];

static float px, py, vx, vy;
static int onGround;
static int coins;
static const float PW = 12, PH = 16;
static const float FLAG_X = 1260;

static void resetLevel(void)
{
    px = 20; py = 100; vx = vy = 0; onGround = 0; coins = 0;
    memset(coinGot, 0, sizeof(coinGot));
    Enemy e[NENEMY] = {
        {200, 134, 1, 150, 380, 1},
        {520, 134, 1, 450, 720, 1},
        {820, 134, 1, 790, 1000, 1},
        {1100, 134, 1, 1020, 1240, 1}
    };
    memcpy(enemies, e, sizeof(e));
}

static int overlap(float ax, float ay, float aw, float ah,
                   float bx, float by, float bw, float bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static void update(int left, int right, int jump, int jumpPressed)
{
    /* horizontal */
    float target = 0;
    if (left) target = -1.8f;
    if (right) target = 1.8f;
    vx += (target - vx) * 0.25f;

    if (jumpPressed && onGround) { vy = -5.2f; onGround = 0; }
    if (!jump && vy < -2.0f) vy = -2.0f; /* variable jump height */
    vy += 0.28f;
    if (vy > 7) vy = 7;

    /* move X + collide */
    px += vx;
    if (px < 0) px = 0;
    for (int i = 0; i < NPLAT; i++) {
        const Rect *p = &plats[i];
        if (overlap(px, py, PW, PH, p->x, p->y, p->w, p->h)) {
            if (vx > 0) px = p->x - PW;
            else if (vx < 0) px = p->x + p->w;
            vx = 0;
        }
    }

    /* move Y + collide */
    py += vy;
    onGround = 0;
    for (int i = 0; i < NPLAT; i++) {
        const Rect *p = &plats[i];
        if (overlap(px, py, PW, PH, p->x, p->y, p->w, p->h)) {
            if (vy > 0) { py = p->y - PH; onGround = 1; }
            else if (vy < 0) { py = p->y + p->h; }
            vy = 0;
        }
    }

    /* fell into a gap */
    if (py > LH + 40) resetLevel();

    /* coins */
    for (int i = 0; i < NCOIN; i++) {
        if (!coinGot[i] && overlap(px, py, PW, PH, coinX[i], coinY[i], 8, 8)) {
            coinGot[i] = 1;
            coins++;
        }
    }

    /* enemies */
    for (int i = 0; i < NENEMY; i++) {
        Enemy *e = &enemies[i];
        if (!e->alive) continue;
        e->x += e->dir * 0.6f;
        if (e->x < e->minX) e->dir = 1;
        if (e->x > e->maxX) e->dir = -1;
        if (overlap(px, py, PW, PH, e->x, e->y, 14, 16)) {
            if (vy > 0 && py + PH - 6 < e->y + 4) {
                e->alive = 0;
                vy = -4.0f;
            } else {
                resetLevel();
                return;
            }
        }
    }

    /* reached the flag */
    if (px > FLAG_X) resetLevel();
}

static void draw(void)
{
    float cam = px - LW / 2.0f;
    if (cam < 0) cam = 0;
    if (cam > 1300 - LW) cam = 1300 - LW;

    clearScreen(0x005C94FC);                   /* sky */
    /* letterbox bars stay black-ish via sky; fine for a test */

    /* platforms */
    for (int i = 0; i < NPLAT; i++) {
        const Rect *p = &plats[i];
        u32 c = (p->h > 20) ? 0x00C84C0C : 0x00B8723C;
        fillRect(p->x - cam, p->y, p->w, p->h, c);
        fillRect(p->x - cam, p->y, p->w, 3, 0x0000A800); /* grass top */
    }

    /* coins */
    for (int i = 0; i < NCOIN; i++)
        if (!coinGot[i]) fillRect(coinX[i] - cam, coinY[i], 8, 8, 0x00FCD800);

    /* enemies */
    for (int i = 0; i < NENEMY; i++)
        if (enemies[i].alive) {
            fillRect(enemies[i].x - cam, enemies[i].y, 14, 16, 0x00884400);
            fillRect(enemies[i].x - cam + 3, enemies[i].y + 4, 3, 3, 0x00FFFFFF);
            fillRect(enemies[i].x - cam + 8, enemies[i].y + 4, 3, 3, 0x00FFFFFF);
        }

    /* flag */
    fillRect(FLAG_X + 10 - cam, 90, 3, 60, 0x00FFFFFF);
    fillRect(FLAG_X + 13 - cam, 90, 14, 10, 0x0000C800);

    /* player: red cap, skin, blue overalls */
    fillRect(px - cam, py, PW, 5, 0x00E40000);
    fillRect(px - cam, py + 5, PW, 5, 0x00FCB8A0);
    fillRect(px - cam, py + 10, PW, 6, 0x000000FC);

    /* HUD: one yellow square per coin */
    for (int i = 0; i < coins; i++)
        fillRect(6 + i * 10, 6, 8, 8, 0x00FCD800);
}

int main(void)
{
    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sysCb, NULL);
    initVideo();
    ioPadInit(7);

    scale = res.height / LH;
    if (scale < 1) scale = 1;
    offX = ((int)res.width - LW * scale) / 2;
    offY = ((int)res.height - LH * scale) / 2;

    resetLevel();
    int prevJump = 0;

    while (running) {
        sysUtilCheckCallback();

        padInfo pi;
        padData pd;
        int left = 0, right = 0, jump = 0;
        ioPadGetInfo(&pi);
        if (pi.status[0]) {
            ioPadGetData(0, &pd);
            left = pd.BTN_LEFT;
            right = pd.BTN_RIGHT;
            jump = pd.BTN_CROSS;
        }

        update(left, right, jump, jump && !prevJump);
        prevJump = jump;

        draw();
        flip(curBuf);
        curBuf ^= 1;
    }

    gcmSetWaitFlip(ctx);
    ioPadEnd();
    return 0;
}
