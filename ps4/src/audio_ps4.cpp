// Jet Force Gemini PS4: replaces src/runtime/audio.cpp (SDL audio). Same work: swap the N64's channels, apply the main
// volume, mix the mods' music and sounds, resample to 48 kHz; the samples go to sceAudioOut (ps4_platform.c).
#include "runtime/audio.hpp"

#include <algorithm>
#include <vector>

#include "config/config.hpp"
#include "ps4_platform.h"

extern "C" void jfg_music_mix(float* samples, size_t frames, uint32_t rate, float gain);  // jfg_music.cpp
extern "C" void jfg_sounds_mix(float* samples, size_t frames, uint32_t rate, float gain); // jfg_music.cpp

namespace dino::runtime {

static uint32_t sample_rate = 32000;          // the game's (set_frequency)
static constexpr uint32_t output_rate = 48000;

// Linear resampler state carried across chunks.
static double resample_position = 0.0;
static float previous[2] = {0.0f, 0.0f};

void queue_samples(int16_t* audio_data, size_t sample_count) {
    static std::vector<float> input;
    static std::vector<float> output;
    const size_t frames = sample_count / 2;
    if (frames == 0) return;
    input.resize(frames * 2);

    // 16-bit to float, swapping the channels back (the runtime's address xor for endianness swaps them).
    const float volume = dino::config::get_main_volume() / 100.0f;
    for (size_t i = 0; i < frames; i++) {
        input[i * 2 + 0] = audio_data[i * 2 + 1] * (0.5f / 32768.0f) * volume;
        input[i * 2 + 1] = audio_data[i * 2 + 0] * (0.5f / 32768.0f) * volume;
    }
    jfg_music_mix(input.data(), frames, sample_rate, volume);
    jfg_sounds_mix(input.data(), frames, sample_rate, volume);

    // Keep the latency bounded as the SDL version did: past 100 ms queued, the chunk is dropped.
    const size_t queued = ps4_audio_queued_frames();
    if (queued > output_rate / 10) return;

    const double step = (double)sample_rate / output_rate;
    output.clear();
    output.reserve((size_t)(frames / step) + 4);
    double position = resample_position;
    while (position < (double)frames) {
        const size_t index = (size_t)position;
        const float t = (float)(position - index);
        for (int c = 0; c < 2; c++) {
            const float a = index > 0 ? input[(index - 1) * 2 + c] : previous[c];
            const float b = input[index * 2 + c];
            output.push_back(a + (b - a) * t);
        }
        position += step;
    }
    resample_position = std::max(0.0, position - (double)frames);
    previous[0] = input[(frames - 1) * 2 + 0];
    previous[1] = input[(frames - 1) * 2 + 1];
    ps4_audio_push(output.data(), output.size() / 2);
}

size_t get_frames_remaining() {
    // Queued output frames in the game's rate, minus one video frame of margin (as the SDL version), so the game's
    // audio thread generates a little ahead and the output never runs dry.
    size_t frames = ps4_audio_queued_frames() * sample_rate / output_rate;
    const size_t margin = sample_rate / 60;
    return frames > margin ? frames - margin : 0;
}

void set_frequency(uint32_t freq) {
    sample_rate = freq;
    resample_position = 0.0;
}

bool reset_audio(uint32_t) {
    if (!ps4_audio_init()) {
        ps4_log("audio unavailable: continuing silent");
    }
    return true; // the game runs silent rather than not at all
}

}
