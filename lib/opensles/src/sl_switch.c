/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/** \file sl_switch.c Nintendo Switch platform backend (SDL2 audio).
 *
 * Replaces the AOSP SDL 1.2 backend (SDL.c): same pull model, the SDL audio
 * thread drains the software mixer via IOutputMixExt_FillBuffer. SDL2 still
 * ships the legacy SDL_OpenAudio/SDL_PauseAudio API used by IEngine.c, and on
 * the Switch it resamples our 44100 Hz mix to audout's native 48 kHz.
 */

#include "sles_allinclusive.h"

#include <SDL2/SDL.h>
#include <switch.h>

#include "pthr.h" // pthr_ensure_fake_tls

// Sample frames per callback. MUST stay 1024: smaller buffers wedge the SDL
// Switch audout backend forever in SWITCHAUDIO_PlayDevice (no completions ->
// total silence, and every mixer-acknowledge handshake hangs with it).
#define SWITCH_AUDIO_SAMPLES 1024

/** \brief usec process clock backing the LSWTCS IPlay::GetPosition additions
 *  (drop-in for the Vita's sceKernelGetProcessTimeLow) */

SceUInt32 sceKernelGetProcessTimeLow(void)
{
    static u64 freq;
    if (freq == 0)
        freq = armGetSystemTickFreq();
    return (SceUInt32)(armGetSystemTick() / (freq / 1000000ull));
}

/** \brief Called by SDL to fill the next audio output buffer */

extern int debugPrintf(char *text, ...); // util.c

static void SDLCALL SDL_callback(void *context, Uint8 *stream, int len)
{
    assert(len > 0);
    // SDL's audio thread was not created through the game-thread trampoline;
    // the mixer invokes game buffer-queue callbacks which read the bionic
    // stack-guard cookie from TPIDR_EL0
    pthr_ensure_fake_tls();
    static int first = 1;
    if (first) {
        // keep the audio mixer off the render/main cores (SDL spawned this
        // thread outside our pthread trampoline, so it'd default onto a hot core)
        pthr_pin_bg_core();
        debugPrintf("SL: audio callback alive (len=%d)\n", len);
        first = 0;
    }
    IEngine *thisEngine = (IEngine *) context;
    // A peek lock would be risky if output mixes are dynamic, so we use SDL_PauseAudio to
    // temporarily disable callbacks during any change to the current output mix, and use a
    // shared lock here
    interface_lock_shared(thisEngine);
    COutputMix *outputMix = thisEngine->mOutputMix;
    interface_unlock_shared(thisEngine);
    if (NULL != outputMix) {
        SLOutputMixExtItf OutputMixExt = &outputMix->mOutputMixExt.mItf;
        IOutputMixExt_FillBuffer(OutputMixExt, stream, (SLuint32) len);
    } else {
        memset(stream, 0, (size_t) len);
    }
}

/** \brief Called during slCreateEngine to initialize the audio output */

void SDL_open(IEngine *thisEngine)
{
    debugPrintf("SL: SDL_open: initializing SDL audio\n");
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        debugPrintf("SL: SDL_InitSubSystem(AUDIO) FAILED: %s\n", SDL_GetError());
        return;
    }

    SDL_AudioSpec fmt;
    SDL_zero(fmt);
    fmt.freq = &_opensles_user_freq != NULL ? _opensles_user_freq : 44100;
    fmt.format = AUDIO_S16;
    fmt.channels = STEREO_CHANNELS;
    fmt.samples = SWITCH_AUDIO_SAMPLES;
    fmt.callback = SDL_callback;
    fmt.userdata = (void *) thisEngine;

    if (SDL_OpenAudio(&fmt, NULL) < 0) {
        debugPrintf("SL: SDL_OpenAudio FAILED: %s\n", SDL_GetError());
        return;
    }
    debugPrintf("SL: SDL audio open (%d Hz), unpausing\n", fmt.freq);
    SDL_PauseAudio(0);
}

/** \brief Called during Object::Destroy for engine to shutdown the audio output */

void SDL_close(void)
{
    SDL_CloseAudio();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}
