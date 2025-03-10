//=============================================================================
//
// Adventure Game Studio (AGS)
//
// Copyright (C) 1999-2011 Chris Jones and 2011-20xx others
// The full list of copyright holders can be found in the Copyright.txt
// file, which is part of this source code distribution.
//
// The AGS source code is provided under the Artistic License 2.0.
// A copy of this license can be found in the file License.txt and at
// http://www.opensource.org/licenses/artistic-license-2.0.php
//
//=============================================================================
#include "ac/timer.h"
#include "core/platform.h"
#include <thread>
#include "ac/sys_events.h"
#include "platform/base/agsplatformdriver.h"
#if defined(AGS_DISABLE_THREADS)
#include "media/audio/audio_core.h"
#endif
#if AGS_PLATFORM_OS_EMSCRIPTEN
#include "SDL.h"
#endif

extern volatile bool game_update_suspend;
extern volatile char want_exit, abort_engine;

namespace {

const auto MAXIMUM_FALL_BEHIND = 3; // number of full frames

auto tick_duration = std::chrono::microseconds(1000000LL/40);
auto framerate = 0;
auto framerate_maxed = false;

auto last_tick_time = AGS_Clock::now();
auto next_frame_timestamp = AGS_Clock::now();

}

std::chrono::microseconds GetFrameDuration()
{
    if (framerate_maxed) {
        return std::chrono::microseconds(0);
    }
    return tick_duration;
}

int setTimerFps(int new_fps)
{
    int old_fps = framerate;
    tick_duration = std::chrono::microseconds(1000000LL/new_fps);
    framerate = new_fps;
    framerate_maxed = new_fps >= 1000;
    // Update next frame time
    next_frame_timestamp = last_tick_time + tick_duration;
    return old_fps;
}

bool isTimerFpsMaxed()
{
    return framerate_maxed;
}

void WaitForNextFrame()
{
    // Do the last polls on this frame, if necessary
#if defined(AGS_DISABLE_THREADS)
    audio_core_entry_poll();
#endif

    const auto now = AGS_Clock::now();
    const auto frameDuration = GetFrameDuration();

    // early exit if we're trying to maximise framerate
    if (frameDuration <= std::chrono::milliseconds::zero()) {
        last_tick_time = next_frame_timestamp;
        next_frame_timestamp = now;
#if !AGS_PLATFORM_OS_EMSCRIPTEN
        // suspend while the game is being switched out
        while (game_update_suspend && (want_exit == 0) && (abort_engine == 0)) {
            sys_evt_process_pending();
            platform->YieldCPU();
        }
#endif
        return;
    }

    // jump ahead if we're lagging
    if (next_frame_timestamp < (now - MAXIMUM_FALL_BEHIND*frameDuration)) {
        next_frame_timestamp = now;
    }

    auto frame_time_remaining = next_frame_timestamp - now;
    if (frame_time_remaining > std::chrono::milliseconds::zero()) {
#if AGS_PLATFORM_OS_EMSCRIPTEN
        SDL_Delay(std::chrono::duration_cast<std::chrono::milliseconds>(frame_time_remaining).count());
#else
        std::this_thread::sleep_for(frame_time_remaining);
#endif
    }

    last_tick_time = next_frame_timestamp;
    next_frame_timestamp += frameDuration;

#if !AGS_PLATFORM_OS_EMSCRIPTEN
    // suspend while the game is being switched out
    while (game_update_suspend && (want_exit == 0) && (abort_engine == 0)) {
        sys_evt_process_pending();
        platform->YieldCPU();
    }
#endif
}

void skipMissedTicks()
{
    last_tick_time = AGS_Clock::now();
    next_frame_timestamp = AGS_Clock::now();
}
