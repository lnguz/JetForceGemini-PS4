// Jet Force Gemini PS4: console services (see ps4_platform.h). Video, audio, pads and log follow the DKC2 and Cave
// Story hosts, which run on the same console.
#include "ps4_platform.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <orbis/AudioOut.h>
#include <orbis/Pad.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include <orbis/VideoOut.h>
#include <orbis/libkernel.h>

// Real prototypes: some OpenOrbis headers declare these without parameters.
int sceSystemServiceHideSplashScreen(void);
int sceSystemServiceLoadExec(const char *path, char *const argv[]);
// Pad.h declares it without parameters: call it through an alias with the real signature.
int pad_set_motion_sensor_state(int32_t handle, bool enable) __asm__("scePadSetMotionSensorState");
typedef struct {
    int32_t event_num;
    uint8_t is_system_ui_overlaid;
    uint8_t is_in_background_execution;
    uint8_t is_cpu_mode7_cpu_normal;
    uint8_t is_game_live_streaming_on_air;
    uint8_t is_out_of_vr_play_area;
    uint8_t reserved[125];
} Ps4SystemStatus; // layout from shadPS4
int sceSystemServiceGetStatus(Ps4SystemStatus *status);

// ------------------------------------------------------------------ log

static FILE *g_log;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

double ps4_time_seconds(void) { return (double)sceKernelGetProcessTime() / 1000000.0; }

void ps4_log(const char *format, ...) {
    char line[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof line, format, args);
    va_end(args);
    if (!g_log) return;
    pthread_mutex_lock(&g_log_mutex);
    fprintf(g_log, "[%8.3f] %s\n", ps4_time_seconds(), line);
    fflush(g_log);
    pthread_mutex_unlock(&g_log_mutex);
}

void ps4_log_open(void) {
    mkdir(JFG_DATA_DIR, 0777);
    FILE *truncate = fopen(JFG_DATA_DIR "/debug.log", "w"); // a fresh log per launch
    if (truncate) fclose(truncate);
    g_log = fopen(JFG_DATA_DIR "/debug.log", "a");
    // The runtime and the port report through stdout/stderr. The console refuses dup2 onto descriptors 1 and 2, so the
    // descriptor inside musl's FILE (offset 0x78, the field fileno() reads; validated in the Downhill port) is pointed
    // at the log instead, only when the layout checks out.
    if (g_log) {
        int fd = open(JFG_DATA_DIR "/debug.log", O_WRONLY | O_APPEND);
        if (fd >= 0 && fileno(stderr) == 2 && fileno(stdout) == 1 &&
            *(int *)((char *)stderr + 0x78) == 2 && *(int *)((char *)stdout + 0x78) == 1) {
            fflush(stdout);
            fflush(stderr);
            *(int *)((char *)stderr + 0x78) = fd;
            *(int *)((char *)stdout + 0x78) = fd;
        }
        setvbuf(stderr, NULL, _IONBF, 0);
        setvbuf(stdout, NULL, _IOLBF, 0);
    }
}

// ---------------------------------------------------------------- video

enum {
    kDisplayBufferSize = PS4_DISPLAY_WIDTH * PS4_DISPLAY_HEIGHT * 4,
    kDirectAlign = 0x200000,
};

static int g_video = -1;
static OrbisKernelEqueue g_flip_queue;
static uint32_t *g_display[PS4_DISPLAY_BUFFERS];
static int g_current_buffer;
static int64_t g_flip_count;

bool ps4_video_init(void) {
    g_video = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, NULL);
    if (g_video < 0) {
        ps4_log("sceVideoOutOpen failed: 0x%08X", (unsigned)g_video);
        return false;
    }
    if (sceKernelCreateEqueue(&g_flip_queue, "jfg flip") != 0) return false;
    sceVideoOutAddFlipEvent(g_flip_queue, g_video, NULL);

    size_t size = ((size_t)kDisplayBufferSize * PS4_DISPLAY_BUFFERS + kDirectAlign - 1) / kDirectAlign * kDirectAlign;
    off_t offset;
    void *memory = NULL;
    if (sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), size, kDirectAlign, 3, &offset) != 0 ||
        sceKernelMapDirectMemory(&memory, size, 0x33, 0, offset, kDirectAlign) != 0) {
        ps4_log("display memory allocation failed");
        return false;
    }
    for (int i = 0; i < PS4_DISPLAY_BUFFERS; i++) {
        g_display[i] = (uint32_t *)((char *)memory + (size_t)i * kDisplayBufferSize);
        memset(g_display[i], 0, kDisplayBufferSize);
    }
    OrbisVideoOutBufferAttribute attribute;
    memset(&attribute, 0, sizeof attribute);
    // 0x80000000 = A8R8G8B8 sRGB, tiling 1 = linear.
    sceVideoOutSetBufferAttribute(&attribute, 0x80000000, 1, 0, PS4_DISPLAY_WIDTH, PS4_DISPLAY_HEIGHT, PS4_DISPLAY_WIDTH);
    if (sceVideoOutRegisterBuffers(g_video, 0, (void **)g_display, PS4_DISPLAY_BUFFERS, &attribute) != 0) {
        ps4_log("sceVideoOutRegisterBuffers failed");
        return false;
    }
    sceVideoOutSetFlipRate(g_video, 0); // 60 Hz
    return true;
}

int ps4_video_handle(void) { return g_video; }
uint32_t *ps4_video_buffer(int index) { return g_display[index]; }

static void wait_for_flip(int64_t wanted) {
    if (wanted <= 0) return;
    for (;;) {
        OrbisVideoOutFlipStatus status;
        if (sceVideoOutGetFlipStatus(g_video, &status) != 0 || (int64_t)status.flipArg >= wanted) return;
        OrbisKernelEvent event;
        int count;
        if (sceKernelWaitEqueue(g_flip_queue, &event, 1, &count, NULL) != 0) return;
    }
}

uint32_t *ps4_video_begin_frame(int *index) {
    wait_for_flip(g_flip_count - 1); // at most one flip in flight, so the buffer drawn next is off screen
    *index = g_current_buffer;
    return g_display[g_current_buffer];
}

void ps4_video_flip(int index) {
    g_flip_count++;
    sceVideoOutSubmitFlip(g_video, index, ORBIS_VIDEO_OUT_FLIP_VSYNC, g_flip_count);
    g_current_buffer = (index + 1) % PS4_DISPLAY_BUFFERS;
}

// ---------------------------------------------------------------- audio

enum { kOutputRate = 48000, kOutputBlock = 256, kRingFrames = 1 << 14 };

static int16_t g_ring[kRingFrames * 2];
static volatile uint32_t g_ring_write, g_ring_read;
static int g_audio = -1;
static pthread_t g_audio_thread;

static uint32_t ring_fill(void) {
    return __atomic_load_n(&g_ring_write, __ATOMIC_ACQUIRE) - __atomic_load_n(&g_ring_read, __ATOMIC_ACQUIRE);
}

static void *audio_thread(void *unused) {
    (void)unused;
    static int16_t blocks[2][kOutputBlock * 2];
    int current = 0;
    for (;;) {
        int16_t *out = blocks[current];
        uint32_t read = g_ring_read;
        uint32_t available = __atomic_load_n(&g_ring_write, __ATOMIC_ACQUIRE) - read;
        if (available >= kOutputBlock) {
            for (int i = 0; i < kOutputBlock; i++) {
                uint32_t index = (read + (uint32_t)i) & (kRingFrames - 1);
                out[i * 2] = g_ring[index * 2];
                out[i * 2 + 1] = g_ring[index * 2 + 1];
            }
            __atomic_store_n(&g_ring_read, read + kOutputBlock, __ATOMIC_RELEASE);
        } else {
            memset(out, 0, sizeof blocks[0]); // underrun
        }
        sceAudioOutOutput(g_audio, out); // blocks until the previous block played
        current ^= 1;
    }
    return NULL;
}

bool ps4_audio_init(void) {
    sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_AUDIOOUT);
    int rc = sceAudioOutInit();
    if (rc != 0 && (unsigned)rc != 0x8026000Eu) { // already initialised
        ps4_log("sceAudioOutInit failed: 0x%08X", (unsigned)rc);
        return false;
    }
    int user = ORBIS_USER_SERVICE_USER_ID_SYSTEM;
    int initial = -1;
    if (sceUserServiceGetInitialUser(&initial) == 0 && initial > 0) user = initial;
    g_audio = sceAudioOutOpen(user, ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0, kOutputBlock, kOutputRate,
                              ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
    if (g_audio <= 0)
        g_audio = sceAudioOutOpen(ORBIS_USER_SERVICE_USER_ID_SYSTEM, ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0, kOutputBlock,
                                  kOutputRate, ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
    if (g_audio <= 0) {
        ps4_log("sceAudioOutOpen failed: 0x%08X", (unsigned)g_audio);
        g_audio = -1;
        return false;
    }
    return pthread_create(&g_audio_thread, NULL, audio_thread, NULL) == 0;
}

void ps4_audio_push(const float *stereo, size_t frames) {
    if (g_audio < 0) return;
    uint32_t write = g_ring_write;
    uint32_t space = kRingFrames - ring_fill();
    if (frames > space) frames = space; // full: drop the newest samples
    for (size_t i = 0; i < frames; i++) {
        uint32_t slot = (write + (uint32_t)i) & (kRingFrames - 1);
        for (int c = 0; c < 2; c++) {
            float v = stereo[i * 2 + c] * 32767.0f;
            if (v > 32767.0f) v = 32767.0f;
            if (v < -32768.0f) v = -32768.0f;
            g_ring[slot * 2 + c] = (int16_t)v;
        }
    }
    __atomic_store_n(&g_ring_write, write + (uint32_t)frames, __ATOMIC_RELEASE);
}

size_t ps4_audio_queued_frames(void) { return ring_fill(); }

// ---------------------------------------------------------------- pads

typedef struct {
    int user;
    int handle;
} PadSlot;

static PadSlot g_pads[PS4_MAX_PADS] = {{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}};
static unsigned g_pad_poll;

void ps4_pads_init(void) {
    sceUserServiceInitialize(NULL);
    scePadInit();
}

static void refresh_pads(void) {
    if (g_pad_poll++ % 60 != 0) return; // once a second
    OrbisUserServiceLoginUserIdList list;
    int initial = -1;
    sceUserServiceGetInitialUser(&initial);
    if (sceUserServiceGetLoginUserIdList(&list) != 0) return;
    // Player 1 is the user who started the game, then the other signed-in users.
    int users[PS4_MAX_PADS] = {initial, -1, -1, -1};
    int n = initial > 0 ? 1 : 0;
    for (int i = 0; i < ORBIS_USER_SERVICE_MAX_LOGIN_USERS && n < PS4_MAX_PADS; i++) {
        int id = list.userId[i];
        if (id <= 0 || id == initial) continue;
        users[n++] = id;
    }
    for (int p = 0; p < PS4_MAX_PADS; p++) {
        if (g_pads[p].handle >= 0 && g_pads[p].user == users[p]) continue;
        if (g_pads[p].handle >= 0) scePadClose(g_pads[p].handle);
        g_pads[p].handle = -1;
        g_pads[p].user = users[p];
        if (users[p] > 0) {
            int handle = scePadOpen(users[p], ORBIS_PAD_PORT_TYPE_STANDARD, 0, NULL);
            if (handle >= 0) {
                g_pads[p].handle = handle;
                pad_set_motion_sensor_state(handle, true);
                ps4_log("player %d: DualShock 4 of user %d", p + 1, users[p]);
            }
        }
    }
}

int ps4_pads_read(Ps4PadState out[PS4_MAX_PADS]) {
    refresh_pads();
    int connected = 0;
    for (int p = 0; p < PS4_MAX_PADS; p++) {
        Ps4PadState *s = &out[p];
        memset(s, 0, sizeof *s);
        s->lx = s->ly = s->rx = s->ry = 128;
        if (g_pads[p].handle < 0) continue;
        OrbisPadData data;
        memset(&data, 0, sizeof data);
        if (scePadReadState(g_pads[p].handle, &data) != 0 || !data.connected) continue;
        s->connected = true;
        s->buttons = data.buttons;
        s->lx = data.leftStick.x;
        s->ly = data.leftStick.y;
        s->rx = data.rightStick.x;
        s->ry = data.rightStick.y;
        s->l2 = data.analogButtons.l2;
        s->r2 = data.analogButtons.r2;
        s->gyro[0] = data.vel.x;
        s->gyro[1] = data.vel.y;
        s->gyro[2] = data.vel.z;
        s->accel[0] = data.acell.x;
        s->accel[1] = data.acell.y;
        s->accel[2] = data.acell.z;
        s->timestamp_us = data.timestamp;
        connected++;
    }
    return connected;
}

void ps4_pad_vibrate(int pad, uint8_t large, uint8_t small) {
    if (pad < 0 || pad >= PS4_MAX_PADS || g_pads[pad].handle < 0) return;
    OrbisPadVibeParam param = {large, small};
    scePadSetVibration(g_pads[pad].handle, &param);
}

void ps4_pad_light(int pad, uint8_t r, uint8_t g, uint8_t b) {
    if (pad < 0 || pad >= PS4_MAX_PADS || g_pads[pad].handle < 0) return;
    OrbisPadColor color = {r, g, b, 255};
    scePadSetLightBar(g_pads[pad].handle, &color);
}

// --------------------------------------------------------------- system

bool ps4_system_ui_open(void) {
    Ps4SystemStatus status;
    memset(&status, 0, sizeof status);
    if (sceSystemServiceGetStatus(&status) != 0) return false;
    if (status.is_system_ui_overlaid > 1 || status.is_in_background_execution > 1) return false; // layout guess wrong
    return status.is_system_ui_overlaid || status.is_in_background_execution;
}

void ps4_hide_splash(void) { sceSystemServiceHideSplashScreen(); }

void ps4_exit_to_system(void) {
    ps4_log("exit");
    if (g_log) fclose(g_log);
    g_log = NULL;
    sceSystemServiceLoadExec("exit", NULL);
    for (;;) sceKernelUsleep(100000);
}
