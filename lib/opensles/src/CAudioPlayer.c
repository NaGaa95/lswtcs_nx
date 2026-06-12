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

/** \file CAudioPlayer.c AudioPlayer class */

#include "sles_allinclusive.h"


/** \brief Hook called by Object::Realize when an audio player is realized */

SLresult CAudioPlayer_Realize(void *self, SLboolean async)
{
    SLresult result = SL_RESULT_SUCCESS;

#ifdef ANDROID
    result = android_audioPlayer_realize((CAudioPlayer *) self, async);
#endif

#ifdef USE_SNDFILE
    result = SndFile_Realize((CAudioPlayer *) self);
#endif
    (void) self;

    // At this point the channel count and sample rate might still be unknown,
    // depending on the data source and the platform implementation.
    // If they are unknown here, then they will be determined during prefetch.

    return result;
}


/** \brief Hook called by Object::Resume when an audio player is resumed */

SLresult CAudioPlayer_Resume(void *self, SLboolean async)
{
    return SL_RESULT_SUCCESS;
}


/** \brief Hook called by Object::Destroy when an audio player is destroyed */

void CAudioPlayer_Destroy(void *self)
{
    CAudioPlayer *this = (CAudioPlayer *) self;
    freeDataLocatorFormat(&this->mDataSource);
    freeDataLocatorFormat(&this->mDataSink);
    IBufferQueue_Destroy(&this->mBufferQueue);
#ifdef USE_SNDFILE
    SndFile_Destroy(this);
#endif
#ifdef ANDROID
    android_audioPlayer_destroy(this);
#endif
}


/** \brief Hook called by Object::Destroy before an audio player is about to be destroyed */

bool CAudioPlayer_PreDestroy(void *self)
{
#ifdef USE_OUTPUTMIXEXT
    CAudioPlayer *this = (CAudioPlayer *) self;
    // Safe to proceed immediately if a track has not yet been assigned
    Track *track = this->mTrack;
    if (NULL == track)
        return true;
    CAudioPlayer *audioPlayer = track->mAudioPlayer;
    if (NULL == audioPlayer)
        return true;
    assert(audioPlayer == this);
    // Fast path (Switch port): trylock the output mix; success means the
    // mixer is not mid-pass, so unlink the track inline instead of blocking
    // up to a mixer period for the handshake. (Trylock, not lock: the mixer
    // takes outputmix->player and we hold the player.)
    COutputMix *outputMix = CAudioPlayer_GetOutputMix(this);
    if (object_trylock_exclusive(&outputMix->mObject)) {
        unsigned i = track - outputMix->mOutputMixExt.mTracks;
        assert( /* 0 <= i && */ i < MAX_TRACK);
        unsigned mask = 1 << i;
        assert(outputMix->mOutputMixExt.mActiveMask & mask);
        track->mAudioPlayer = NULL;
        outputMix->mOutputMixExt.mActiveMask &= ~mask;
        this->mTrack = NULL;
        object_unlock_exclusive(&outputMix->mObject);
        return true;
    }
    // Mixer is mid-pass: request and wait for it to unlink the track
    extern unsigned int sceKernelGetProcessTimeLow(void); // sl_switch.c
    extern int debugPrintf(char *text, ...);              // util.c
    static int waits = 0;
    unsigned int t0 = sceKernelGetProcessTimeLow();
    this->mDestroyRequested = true;
    while (this->mDestroyRequested) {
        object_cond_wait(self);
    }
    // Mixer thread has acknowledged the request
    int n = ++waits;
    if (n <= 12 || (n % 500) == 0)
        debugPrintf("SL: AudioPlayer destroy waited %u us for mixer (wait #%d)\n",
                    sceKernelGetProcessTimeLow() - t0, n);
#endif
    return true;
}


/** \brief Given an audio player, return its data sink, which is guaranteed to be a non-NULL output
 *  mix.  This function is used by effect send.
 */

COutputMix *CAudioPlayer_GetOutputMix(CAudioPlayer *audioPlayer)
{
    assert(NULL != audioPlayer);
    assert(SL_DATALOCATOR_OUTPUTMIX == audioPlayer->mDataSink.mLocator.mLocatorType);
    SLObjectItf outputMix = audioPlayer->mDataSink.mLocator.mOutputMix.outputMix;
    assert(NULL != outputMix);
    return (COutputMix *) outputMix;
}
