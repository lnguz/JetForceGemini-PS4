// Jet Force Gemini PS4: the renderer context the runtime drives (ultramodern::renderer::RendererContext).
//
// Stage 1 (this file): no drawing yet. It takes every display list (send_dl), logs what arrives and presents a status
// screen on every VI update, so the boot, the game threads, the display lists and the video out can be checked on the
// console. Stage 2 replaces present_status() with the F3DJFG/RDP interpreter and the GNM backend.
#include "renderer/renderer.hpp"

#include <atomic>
#include <cstring>
#include <unistd.h>

#include "ultramodern/ultramodern.hpp"
#include "jfg/jfg_rt64.h"
#include "ps4_platform.h"

// ultramodern/src/events.cpp (PS4 diagnostics)
extern std::atomic<uint64_t> ps4_gfx_tasks_enqueued, ps4_gfx_tasks_dequeued, ps4_gfx_tasks_released;
size_t ps4_action_queue_size();

extern "C" {
    extern uint32_t jfg_ps4_threads[64];    // main_ps4.cpp
    extern std::atomic<int> jfg_ps4_thread_count;
}

namespace {

class Ps4RendererContext final : public ultramodern::renderer::RendererContext {
public:
    Ps4RendererContext(uint8_t* rdram, ultramodern::renderer::WindowHandle window) : rdram_(rdram) {
        setup_result = window.video_out >= 0 ? ultramodern::renderer::SetupResult::Success
                                             : ultramodern::renderer::SetupResult::GraphicsDeviceNotFound;
        chosen_api = ultramodern::renderer::GraphicsApi::Auto;
        ps4_log("renderer: stage 1 (no drawing), video out %d, rdram %p", window.video_out, rdram);
    }

    bool valid() override { return setup_result == ultramodern::renderer::SetupResult::Success; }

    bool update_config(const ultramodern::renderer::GraphicsConfig&, const ultramodern::renderer::GraphicsConfig&) override {
        return true;
    }

    void enable_instant_present() override { ps4_log("renderer: instant present on (game started)"); }

    void send_dl(const OSTask* task) override {
        const uint32_t n = ++display_lists_;
        if (n <= 4 || (n % 60) == 0) {
            ps4_log("renderer: display list %u: type %u flags 0x%X ucode 0x%08X size %u data 0x%08X size %u",
                n, task->t.type, task->t.flags, (uint32_t)task->t.ucode, task->t.ucode_size,
                (uint32_t)task->t.data_ptr, task->t.data_size);
        }
    }

    void update_screen() override {
        const uint32_t n = ++screens_;
        const ultramodern::renderer::ViRegs* vi = ultramodern::renderer::get_vi_regs();
        if (n <= 3 || (n % 60) == 0) {
            ps4_log("renderer: screen %u (display lists %u): VI origin 0x%08X width %u", n, display_lists_.load(),
                vi->VI_ORIGIN_REG, vi->VI_WIDTH_REG);
        }
        // What RT64 publishes for the port (jfg_rt64.h): the framebuffer this update shows (the origin is one row in, as
        // RT64's VI::fbAddress estimates) and how many updates there were. jfg_frame_rate.cpp paces 60 fps with them.
        const uint32_t row_bytes = vi->VI_WIDTH_REG * ((vi->VI_STATUS_REG & 3) == 3 ? 4 : 2);
        const uint32_t origin = vi->VI_ORIGIN_REG & 0x00FFFFFF;
        rt64_jfg_screen_fb.store(origin >= row_bytes ? origin - row_bytes : origin, std::memory_order_release);
        rt64_jfg_screen_updates.fetch_add(1, std::memory_order_acq_rel);
        watchdog(n);
        present_status();
        rt64_jfg_present_count.fetch_add(1, std::memory_order_relaxed);
    }

    void shutdown() override {}
    uint32_t get_display_framerate() const override { return 60; }
    float get_resolution_scale() const override { return 1920.0f / 320.0f; }

private:
    // When the display lists stop for 3 s while the VI keeps going, log every N64 thread: its state and the message
    // queue it waits on, so a hang shows what the game is waiting for. Again every 10 s while it lasts.
    void watchdog(uint32_t screen) {
        const uint32_t lists = display_lists_.load();
        if (lists != watch_lists_) {
            watch_lists_ = lists;
            watch_since_ = screen;
            watch_next_ = 180;
            return;
        }
        if (lists == 0 || screen - watch_since_ < watch_next_) return;
        watch_next_ += 600;
        ps4_log("watchdog: no display list for %u screens (last at %u lists); N64 threads:", screen - watch_since_, lists);
        ps4_log("  gfx tasks: enqueued %llu, dequeued %llu, released %llu; action queue ~%zu",
            (unsigned long long)ps4_gfx_tasks_enqueued.load(), (unsigned long long)ps4_gfx_tasks_dequeued.load(),
            (unsigned long long)ps4_gfx_tasks_released.load(), ps4_action_queue_size());
        const int count = jfg_ps4_thread_count.load();
        for (int i = 0; i < count; i++) {
            const uint32_t addr = jfg_ps4_threads[i];
            const OSThread* t = reinterpret_cast<const OSThread*>(rdram_ + (addr - 0x80000000u));
            uint32_t queue = (uint32_t)t->queue;
            int32_t valid = -1, msgs = -1;
            if (queue >= 0x80000000u && queue < 0x80800000u) {
                // blocked_on_recv is the queue's first field, blocked_on_send its second
                const uint32_t mq = queue;
                const int32_t* q = reinterpret_cast<const int32_t*>(rdram_ + (mq - 0x80000000u));
                valid = q[2];
                msgs = q[4];
            }
            ps4_log("  thread %08X id %d pri %d state %u flags %u queue %08X (valid %d of %d)", addr, (int)t->id,
                (int)t->priority, (unsigned)t->state, (unsigned)t->flags, queue, valid, msgs);
            // In no queue: the thread that holds the N64 CPU. Sample where its host thread is, three times.
            if (queue == 0 && t->context != nullptr) {
                const pthread_t native = t->context->host_thread.native_handle();
                for (int k = 0; k < 3; k++) {
                    ps4_log("  sample %d of thread id %d:", k + 1, (int)t->id);
                    ps4_sample_thread(native);
                    usleep(30000);
                }
            }
        }
    }

    // A dark screen with a bar that moves with every display list, so a frozen game is visible at a glance.
    void present_status() {
        int index;
        uint32_t* fb = ps4_video_begin_frame(&index);
        const uint32_t background = 0xFF101828;
        for (int y = 0; y < PS4_DISPLAY_HEIGHT; y++) {
            uint32_t* row = fb + (size_t)y * PS4_DISPLAY_WIDTH;
            for (int x = 0; x < PS4_DISPLAY_WIDTH; x++) row[x] = background;
        }
        const int bar_x = (int)((display_lists_.load() * 8) % (PS4_DISPLAY_WIDTH - 64));
        for (int y = 1000; y < 1016; y++) {
            uint32_t* row = fb + (size_t)y * PS4_DISPLAY_WIDTH;
            for (int x = bar_x; x < bar_x + 64; x++) row[x] = 0xFF30C0FF;
        }
        ps4_video_flip(index);
    }

    uint8_t* rdram_;
    std::atomic<uint32_t> display_lists_{0};
    uint32_t screens_ = 0;
    uint32_t watch_lists_ = 0, watch_since_ = 0, watch_next_ = 180;
};

} // namespace

std::unique_ptr<ultramodern::renderer::RendererContext> dino::renderer::create_render_context(
    uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool) {
    return std::make_unique<Ps4RendererContext>(rdram, window_handle);
}
