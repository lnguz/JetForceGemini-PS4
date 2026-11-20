// Jet Force Gemini PS4: the part of SDL2 (2.30 API) the port calls, implemented on the console's own services.
// The port's input code (src/input/input.cpp), its controls and the RmlUi SDL platform are kept as they are; the events
// and game controllers they see come from the DualShock 4 (ps4_platform.c). There is no keyboard, mouse or window.
#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>

#include "ps4_platform.h"

#include <orbis/Pad.h>

namespace {

struct Ps4Controller {
    int index;                 // pad slot (0 = player 1)
    SDL_JoystickID instance;   // grows with every connection, as in SDL
    bool open;
    bool gyro, accel;          // sensors enabled by the caller
};

std::mutex g_mutex;
std::deque<SDL_Event> g_events;
Ps4PadState g_pads[PS4_MAX_PADS];
Ps4PadState g_prev[PS4_MAX_PADS];
Ps4Controller g_controllers[PS4_MAX_PADS];
SDL_JoystickID g_next_instance = 0;
uint32_t g_start_ms;

uint32_t now_ms() { return (uint32_t)(ps4_time_seconds() * 1000.0); }

// SDL_GameControllerButton → DualShock 4 button bit.
const uint32_t kButtonBits[SDL_CONTROLLER_BUTTON_MAX] = {
    ORBIS_PAD_BUTTON_CROSS,      // A
    ORBIS_PAD_BUTTON_CIRCLE,     // B
    ORBIS_PAD_BUTTON_SQUARE,     // X
    ORBIS_PAD_BUTTON_TRIANGLE,   // Y
    ORBIS_PAD_BUTTON_TOUCH_PAD,  // BACK
    0,                           // GUIDE (the PS button belongs to the system)
    ORBIS_PAD_BUTTON_OPTIONS,    // START
    ORBIS_PAD_BUTTON_L3,         // LEFTSTICK
    ORBIS_PAD_BUTTON_R3,         // RIGHTSTICK
    ORBIS_PAD_BUTTON_L1,         // LEFTSHOULDER
    ORBIS_PAD_BUTTON_R1,         // RIGHTSHOULDER
    ORBIS_PAD_BUTTON_UP,         // DPAD_UP
    ORBIS_PAD_BUTTON_DOWN,       // DPAD_DOWN
    ORBIS_PAD_BUTTON_LEFT,       // DPAD_LEFT
    ORBIS_PAD_BUTTON_RIGHT,      // DPAD_RIGHT
    0, 0, 0, 0, 0,               // MISC1, PADDLE1-4
    ORBIS_PAD_BUTTON_TOUCH_PAD,  // TOUCHPAD
};

Sint16 stick_axis(uint8_t v) {
    int centred = ((int)v - 128) * 256;
    if (centred > 32767) centred = 32767;
    if (centred < -32768) centred = -32768;
    return (Sint16)centred;
}

Sint16 trigger_axis(uint8_t v) { return (Sint16)((int)v * 32767 / 255); }

Sint16 axis_value(const Ps4PadState& s, int axis) {
    switch (axis) {
    case SDL_CONTROLLER_AXIS_LEFTX: return stick_axis(s.lx);
    case SDL_CONTROLLER_AXIS_LEFTY: return stick_axis(s.ly);
    case SDL_CONTROLLER_AXIS_RIGHTX: return stick_axis(s.rx);
    case SDL_CONTROLLER_AXIS_RIGHTY: return stick_axis(s.ry);
    case SDL_CONTROLLER_AXIS_TRIGGERLEFT: return trigger_axis(s.l2);
    case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: return trigger_axis(s.r2);
    }
    return 0;
}

void push(const SDL_Event& e) { g_events.push_back(e); }

// Reads the pads and turns the changes into SDL events (called once per SDL_PollEvent burst).
void pump() {
    Ps4PadState now[PS4_MAX_PADS];
    ps4_pads_read(now);
    const uint32_t t = now_ms();
    for (int p = 0; p < PS4_MAX_PADS; p++) {
        Ps4Controller& c = g_controllers[p];
        const Ps4PadState& s = now[p];
        const Ps4PadState& prev = g_pads[p];
        if (s.connected && !prev.connected) {
            c.index = p;
            c.instance = g_next_instance++;
            SDL_Event e{};
            e.type = SDL_CONTROLLERDEVICEADDED;
            e.cdevice.timestamp = t;
            e.cdevice.which = p; // device index
            push(e);
        } else if (!s.connected && prev.connected) {
            SDL_Event e{};
            e.type = SDL_CONTROLLERDEVICEREMOVED;
            e.cdevice.timestamp = t;
            e.cdevice.which = c.instance;
            push(e);
            c.open = false;
        }
        if (!s.connected) continue;
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
            const uint32_t bit = kButtonBits[b];
            if (!bit || b == SDL_CONTROLLER_BUTTON_TOUCHPAD) continue;
            const bool down = (s.buttons & bit) != 0, was = (prev.buttons & bit) != 0;
            if (down == was) continue;
            SDL_Event e{};
            e.type = down ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
            e.cbutton.timestamp = t;
            e.cbutton.which = c.instance;
            e.cbutton.button = (Uint8)b;
            e.cbutton.state = down ? SDL_PRESSED : SDL_RELEASED;
            push(e);
        }
        for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX; a++) {
            const Sint16 v = axis_value(s, a), was = axis_value(prev, a);
            if (v == was) continue;
            SDL_Event e{};
            e.type = SDL_CONTROLLERAXISMOTION;
            e.caxis.timestamp = t;
            e.caxis.which = c.instance;
            e.caxis.axis = (Uint8)a;
            e.caxis.value = v;
            push(e);
        }
        if (c.open && c.accel) {
            SDL_Event e{};
            e.type = SDL_CONTROLLERSENSORUPDATE;
            e.csensor.timestamp = t;
            e.csensor.which = c.instance;
            e.csensor.sensor = SDL_SENSOR_ACCEL;
            for (int i = 0; i < 3; i++) e.csensor.data[i] = s.accel[i] * SDL_STANDARD_GRAVITY; // g → m/s²
            push(e);
        }
        if (c.open && c.gyro) {
            SDL_Event e{};
            e.type = SDL_CONTROLLERSENSORUPDATE;
            e.csensor.timestamp = t;
            e.csensor.which = c.instance;
            e.csensor.sensor = SDL_SENSOR_GYRO;
            for (int i = 0; i < 3; i++) e.csensor.data[i] = s.gyro[i]; // rad/s
            push(e);
        }
    }
    std::memcpy(g_prev, g_pads, sizeof g_pads);
    std::memcpy(g_pads, now, sizeof g_pads);
}

Ps4Controller* as_controller(SDL_GameController* gc) { return reinterpret_cast<Ps4Controller*>(gc); }

Uint8 g_keyboard[SDL_NUM_SCANCODES];

} // namespace

extern "C" {

// ---- init, hints, errors
int SDL_Init(Uint32) { g_start_ms = now_ms(); return 0; }
int SDL_InitSubSystem(Uint32) { return 0; }
void SDL_Quit(void) {}
SDL_bool SDL_SetHint(const char*, const char*) { return SDL_TRUE; }
const char* SDL_GetError(void) { return ""; }
const char* SDL_GetCurrentVideoDriver(void) { return "ps4"; }
void SDL_free(void* p) { std::free(p); }
int SDL_ShowSimpleMessageBox(Uint32, const char* title, const char* message, SDL_Window*) {
    ps4_log("[message] %s: %s", title ? title : "", message ? message : "");
    return 0;
}

// ---- time
Uint32 SDL_GetTicks(void) { return now_ms() - g_start_ms; }
Uint64 SDL_GetPerformanceCounter(void) { return (Uint64)(ps4_time_seconds() * 1000000.0); }
Uint64 SDL_GetPerformanceFrequency(void) { return 1000000; }

// ---- events
int SDL_PollEvent(SDL_Event* event) {
    std::lock_guard lock{ g_mutex };
    if (g_events.empty()) {
        // One pad read per burst: the caller drains the queue, then the next call reads the pads again.
        static bool drained = false;
        if (drained) { drained = false; return 0; }
        pump();
        drained = true;
        if (g_events.empty()) { drained = false; return 0; }
    }
    if (event) *event = g_events.front();
    g_events.pop_front();
    return 1;
}

// ---- keyboard, mouse, cursor, clipboard: none on the console
const Uint8* SDL_GetKeyboardState(int* numkeys) { if (numkeys) *numkeys = SDL_NUM_SCANCODES; return g_keyboard; }
SDL_Keymod SDL_GetModState(void) { return KMOD_NONE; }
const char* SDL_GetScancodeName(SDL_Scancode) { return ""; }
Uint32 SDL_GetMouseState(int* x, int* y) { if (x) *x = 0; if (y) *y = 0; return 0; }
int SDL_CaptureMouse(SDL_bool) { return 0; }
int SDL_SetRelativeMouseMode(SDL_bool) { return 0; }
int SDL_ShowCursor(int) { return 0; }
SDL_Cursor* SDL_CreateSystemCursor(SDL_SystemCursor) { return nullptr; }
void SDL_SetCursor(SDL_Cursor*) {}
void SDL_FreeCursor(SDL_Cursor*) {}
char* SDL_GetClipboardText(void) { char* s = (char*)std::malloc(1); if (s) s[0] = 0; return s; }
int SDL_SetClipboardText(const char*) { return 0; }

// ---- game controllers (every connected DualShock 4 is one)
int SDL_NumJoysticks(void) {
    std::lock_guard lock{ g_mutex };
    int n = 0;
    for (const auto& s : g_pads) n += s.connected ? 1 : 0;
    return n;
}
SDL_bool SDL_IsGameController(int index) {
    std::lock_guard lock{ g_mutex };
    return (index >= 0 && index < PS4_MAX_PADS && g_pads[index].connected) ? SDL_TRUE : SDL_FALSE;
}
SDL_GameController* SDL_GameControllerOpen(int index) {
    std::lock_guard lock{ g_mutex };
    if (index < 0 || index >= PS4_MAX_PADS || !g_pads[index].connected) return nullptr;
    g_controllers[index].open = true;
    return reinterpret_cast<SDL_GameController*>(&g_controllers[index]);
}
void SDL_GameControllerClose(SDL_GameController* gc) { if (gc) as_controller(gc)->open = false; }
SDL_Joystick* SDL_GameControllerGetJoystick(SDL_GameController* gc) { return reinterpret_cast<SDL_Joystick*>(gc); }
SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick* j) {
    return j ? reinterpret_cast<Ps4Controller*>(j)->instance : -1;
}
const char* SDL_GameControllerName(SDL_GameController*) { return "DualShock 4"; }
SDL_GameControllerType SDL_GameControllerGetType(SDL_GameController*) { return SDL_CONTROLLER_TYPE_PS4; }
Uint8 SDL_GameControllerGetButton(SDL_GameController* gc, SDL_GameControllerButton button) {
    if (!gc || button < 0 || button >= SDL_CONTROLLER_BUTTON_MAX) return 0;
    const uint32_t bit = kButtonBits[button];
    return (bit && (g_pads[as_controller(gc)->index].buttons & bit)) ? 1 : 0;
}
Sint16 SDL_GameControllerGetAxis(SDL_GameController* gc, SDL_GameControllerAxis axis) {
    if (!gc) return 0;
    return axis_value(g_pads[as_controller(gc)->index], axis);
}
SDL_bool SDL_GameControllerHasSensor(SDL_GameController*, SDL_SensorType type) {
    return (type == SDL_SENSOR_GYRO || type == SDL_SENSOR_ACCEL) ? SDL_TRUE : SDL_FALSE;
}
int SDL_GameControllerSetSensorEnabled(SDL_GameController* gc, SDL_SensorType type, SDL_bool enabled) {
    if (!gc) return -1;
    if (type == SDL_SENSOR_GYRO) as_controller(gc)->gyro = enabled;
    if (type == SDL_SENSOR_ACCEL) as_controller(gc)->accel = enabled;
    return 0;
}
int SDL_GameControllerRumble(SDL_GameController* gc, Uint16 low, Uint16 high, Uint32) {
    if (!gc) return -1;
    // The N64 Rumble Pak has one motor: drive the DualShock 4's large motor with the stronger of the two.
    const Uint16 strength = low > high ? low : high;
    ps4_pad_vibrate(as_controller(gc)->index, (uint8_t)(strength >> 8), 0);
    return 0;
}

} // extern "C"
