// Jet Force Gemini PS4: the console services the port uses (video out, audio out, DualShock 4, log, system).
// Everything mutable lives in /data/jfg.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JFG_DATA_DIR "/data/jfg"

// ---- log: /data/jfg/debug.log, with stdout and stderr redirected into it
void ps4_log_open(void);
void ps4_log(const char *format, ...) __attribute__((format(printf, 1, 2)));
double ps4_time_seconds(void);

// ---- video: 1920x1080 RGBA8 scan-out buffers in garlic memory, registered with sceVideoOut
enum { PS4_DISPLAY_WIDTH = 1920, PS4_DISPLAY_HEIGHT = 1080, PS4_DISPLAY_BUFFERS = 3 };
bool ps4_video_init(void);
int ps4_video_handle(void);
// The buffer to draw next (waits until it is no longer on screen) and its index.
uint32_t *ps4_video_begin_frame(int *index);
// Queues that buffer for the next vertical blank.
void ps4_video_flip(int index);
uint32_t *ps4_video_buffer(int index);

// ---- audio: 48 kHz stereo float pushed into a ring that a thread feeds to sceAudioOut
bool ps4_audio_init(void);
void ps4_audio_push(const float *stereo, size_t frames);
size_t ps4_audio_queued_frames(void);

// ---- DualShock 4 (player 1 is the user who started the game)
typedef struct {
    bool connected;
    uint32_t buttons;              // ORBIS_PAD_BUTTON_* bits
    uint8_t lx, ly, rx, ry;        // 0..255, 128 centre
    uint8_t l2, r2;                // 0..255
    float gyro[3];                 // rad/s (x, y, z)
    float accel[3];                // g
    uint64_t timestamp_us;
} Ps4PadState;

enum { PS4_MAX_PADS = 4 };
void ps4_pads_init(void);
// Refreshes every pad; returns how many are connected.
int ps4_pads_read(Ps4PadState out[PS4_MAX_PADS]);
void ps4_pad_vibrate(int pad, uint8_t large, uint8_t small);
void ps4_pad_light(int pad, uint8_t r, uint8_t g, uint8_t b);

// ---- system
bool ps4_system_ui_open(void);
void ps4_hide_splash(void);
void ps4_crash_install(void); // crash_ps4.c
void ps4_clock_selftest(void); // sem_ps4.c
#include <pthread.h>
void ps4_sample_thread(pthread_t thread); // crash_ps4.c: logs where a thread is
__attribute__((noreturn)) void ps4_exit_to_system(void);

#ifdef __cplusplus
}
#endif
