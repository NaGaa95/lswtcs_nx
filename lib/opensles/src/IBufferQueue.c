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

/* BufferQueue implementation */

#include "sles_allinclusive.h"


/** Determine the state of the audio player or audio recorder associated with a buffer queue.
 *  Note that PLAYSTATE and RECORDSTATE values are equivalent (where PLAYING == RECORDING).
 */

static SLuint32 getAssociatedState(IBufferQueue *this)
{
    SLuint32 state;
    switch (InterfaceToObjectID(this)) {
    case SL_OBJECTID_AUDIOPLAYER:
        state = ((CAudioPlayer *) this->mThis)->mPlay.mState;
        break;
    case SL_OBJECTID_AUDIORECORDER:
        state = ((CAudioRecorder *) this->mThis)->mRecord.mState;
        break;
    default:
        // unreachable, but just in case we will assume it is stopped
        assert(SL_BOOLEAN_FALSE);
        state = SL_PLAYSTATE_STOPPED;
        break;
    }
    return state;
}

extern int debugPrintf(char *text, ...);
extern int opensles_mono_sfx_gain_percent __attribute__((weak));

static void reset_buffer_header(BufferHeader *header)
{
    header->mBuffer = NULL;
    header->mSize = 0;
}

static void clear_buffer_header(BufferHeader *header)
{
    reset_buffer_header(header);
    header->mOwnedBuffer = NULL;
    header->mOwnedCapacity = 0;
}

void IBufferQueue_FreeBufferHeader(BufferHeader *header)
{
    if (header->mOwnedBuffer != NULL) {
        free(header->mOwnedBuffer);
    }
    clear_buffer_header(header);
}

void IBufferQueue_ReleaseBufferHeader(BufferHeader *header)
{
    reset_buffer_header(header);
}

void IBufferQueue_InitHeaders(IBufferQueue *this)
{
    if (this->mArray == NULL) {
        return;
    }
    BufferHeader *bufferHeader = this->mArray;
    for (unsigned i = 0; i <= this->mNumBuffers; ++i, ++bufferHeader) {
        clear_buffer_header(bufferHeader);
    }
}

void IBufferQueue_FreeOwnedBuffers(IBufferQueue *this)
{
    if (this->mArray == NULL) {
        return;
    }
    BufferHeader *bufferHeader = this->mArray;
    for (unsigned i = 0; i <= this->mNumBuffers; ++i, ++bufferHeader) {
        IBufferQueue_FreeBufferHeader(bufferHeader);
    }
}

static int opensles_output_rate_hz(void)
{
    return (&_opensles_user_freq != NULL && _opensles_user_freq > 0) ?
        _opensles_user_freq : 44100;
}

static int16_t pcm_read_sample(const void *buffer, SLuint32 frame,
    int channels, int bps, int channel)
{
    if (channels == 1) {
        channel = 0;
    }

    if (bps == 8) {
        const uint8_t *src = (const uint8_t *) buffer;
        return (int16_t)(((int)src[(frame * channels) + channel] - 0x80) << 8);
    }

    const int16_t *src = (const int16_t *) buffer;
    return src[(frame * channels) + channel];
}

static int16_t pcm_interpolate_sample(const void *buffer, uint32_t in_frames,
    int channels, int bps, int channel, uint32_t frame, uint32_t fraction,
    uint32_t denominator, const SLint16 previous[2], SLboolean have_previous)
{
    if (frame >= in_frames) {
        frame = in_frames - 1;
    }
    if (channels == 1) {
        channel = 0;
    }
    const int32_t second = pcm_read_sample(buffer, frame, channels, bps, channel);
    const int32_t first = frame > 0 ?
        pcm_read_sample(buffer, frame - 1, channels, bps, channel) :
        (have_previous ? previous[channel] : second);
    const int64_t weighted = (int64_t)first * (denominator - fraction) +
        (int64_t)second * fraction;
    return (int16_t)(weighted / denominator);
}

// A mono source is a front-centre channel, not two independent full-scale
// channels. Equal-power distribution preserves its energy when it is routed
// to stereo and keeps mono SFX from sounding about 3 dB louder than stereo
// music/cutscene audio.
static int pcm_mono_gain_q15(void)
{
    int percent = (&opensles_mono_sfx_gain_percent != NULL) ?
        opensles_mono_sfx_gain_percent : 70;
    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }
    return (23170 * percent + 50) / 100;
}

static int16_t pcm_mono_to_stereo(int16_t sample, int gain_q15)
{
    const int32_t scaled = (int32_t)sample * gain_q15;
    return (int16_t)((scaled + (scaled >= 0 ? 16384 : -16384)) / 32768);
}

static void log_pcm_convert_once(uint32_t samplerate, int channels, int bps,
    SLuint32 inSize, SLuint32 outSize)
{
    static uint32_t seen[16];
    static int seen_count = 0;
    uint32_t key = samplerate ^ ((uint32_t)channels << 24) ^ ((uint32_t)bps << 16);
    for (int i = 0; i < seen_count; i++) {
        if (seen[i] == key) {
            return;
        }
    }
    if (seen_count < (int)(sizeof(seen) / sizeof(seen[0]))) {
        seen[seen_count++] = key;
        debugPrintf("SL: PCM queue convert %u mHz %dch %dbit, %u -> %u bytes\n",
            samplerate, channels, bps, inSize, outSize);
    }
}

SLresult IBufferQueue_Enqueue(SLBufferQueueItf self, const void *pBuffer, SLuint32 size)
{
    SL_ENTER_INTERFACE
    result = SL_RESULT_SUCCESS;
    //SL_LOGV("IBufferQueue_Enqueue(%p, %p, %lu)", self, pBuffer, size);

    // Note that Enqueue while a Clear is pending is equivalent to Enqueue followed by Clear
    
    if (NULL == pBuffer || 0 == size) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IBufferQueue *this = (IBufferQueue *) self;
        interface_lock_exclusive(this);
        BufferHeader *oldRear = this->mRear, *newRear;
        if ((newRear = oldRear + 1) == &this->mArray[this->mNumBuffers + 1]) {
            newRear = this->mArray;
        }
        if (newRear == this->mFront) {
            result = SL_RESULT_BUFFER_INSUFFICIENT;
        } else {
            const void *queueBuffer = pBuffer;
            SLuint32 queueSize = size;
            void *ownedBuffer = NULL;
            const int out_rate_hz = opensles_output_rate_hz();
            const uint32_t out_rate_mhz = (uint32_t)out_rate_hz * 1000u;
            const uint32_t in_rate_hz = this->samplerate ? ((this->samplerate + 500u) / 1000u) : out_rate_hz;
            const int bytes_per_sample = (this->bps == 8) ? 1 : ((this->bps == 16) ? 2 : 0);
            if (bytes_per_sample == 0 || (this->channels != 1 && this->channels != 2) || in_rate_hz == 0) {
                result = SL_RESULT_CONTENT_UNSUPPORTED;
            } else if (!(this->samplerate == out_rate_mhz && this->channels == 2 && this->bps == 16)) {
                const uint32_t in_frame_size = (uint32_t)this->channels * (uint32_t)bytes_per_sample;
                const uint32_t in_frames = size / in_frame_size;
                const uint64_t source_span = (uint64_t)in_frames * (uint64_t)out_rate_hz;
                const uint64_t start_phase = this->mResamplePhase;
                const uint64_t out_frames_wide = start_phase < source_span ?
                    (source_span - start_phase + (uint64_t)in_rate_hz - 1U) /
                        (uint64_t)in_rate_hz : 0;
                if (in_frames == 0 || (size % in_frame_size) != 0) {
                    result = SL_RESULT_PARAMETER_INVALID;
                } else if (out_frames_wide > UINT32_MAX / 4U) {
                    result = SL_RESULT_MEMORY_FAILURE;
                }
                const uint32_t out_frames = (uint32_t)out_frames_wide;
                const SLuint32 out_size = out_frames * 4U;
                const SLuint32 alloc_size = out_size ? out_size : 1;
                if (result != SL_RESULT_SUCCESS) {
                    // Leave the circular queue untouched on malformed or oversized input.
                } else if (oldRear->mOwnedBuffer != NULL &&
                        oldRear->mOwnedCapacity >= alloc_size) {
                    ownedBuffer = oldRear->mOwnedBuffer;
                } else {
                    void *newBuffer = realloc(oldRear->mOwnedBuffer, alloc_size);
                    if (newBuffer != NULL) {
                        oldRear->mOwnedBuffer = newBuffer;
                        oldRear->mOwnedCapacity = alloc_size;
                    }
                    ownedBuffer = newBuffer;
                }
                if (ownedBuffer == NULL) {
                    if (result == SL_RESULT_SUCCESS) {
                        result = SL_RESULT_MEMORY_FAILURE;
                    }
                } else {
                    int16_t *dst = (int16_t *)ownedBuffer;
                    const int mono_gain_q15 = pcm_mono_gain_q15();
                    for (uint32_t of = 0; of < out_frames; of++) {
                        const uint64_t source = start_phase +
                            (uint64_t)of * (uint64_t)in_rate_hz;
                        const uint32_t frame = (uint32_t)(source / (uint64_t)out_rate_hz);
                        const uint32_t fraction = (uint32_t)(source % (uint64_t)out_rate_hz);
                        int16_t left = pcm_interpolate_sample(pBuffer, in_frames,
                            this->channels, this->bps, 0, frame, fraction,
                            (uint32_t)out_rate_hz, this->mResampleLast,
                            this->mHaveResampleLast);
                        int16_t right;
                        if (this->channels == 1) {
                            left = pcm_mono_to_stereo(left, mono_gain_q15);
                            right = left;
                        } else {
                            right = pcm_interpolate_sample(pBuffer, in_frames,
                                this->channels, this->bps, 1, frame, fraction,
                                (uint32_t)out_rate_hz, this->mResampleLast,
                                this->mHaveResampleLast);
                        }
                        dst[of * 2 + 0] = left;
                        dst[of * 2 + 1] = right;
                    }
                    this->mResamplePhase = (uint32_t)(start_phase +
                        out_frames_wide * (uint64_t)in_rate_hz - source_span);
                    this->mResampleLast[0] = pcm_read_sample(pBuffer, in_frames,
                        this->channels, this->bps, 0);
                    this->mResampleLast[1] = pcm_read_sample(pBuffer, in_frames,
                        this->channels, this->bps, 1);
                    this->mHaveResampleLast = SL_BOOLEAN_TRUE;
                    log_pcm_convert_once(this->samplerate, this->channels, this->bps, size, out_size);
                    queueBuffer = ownedBuffer;
                    queueSize = out_size;
                }
            }
            if (result != SL_RESULT_SUCCESS) {
                // keep the queue unchanged on conversion/setup failure
            } else {
                reset_buffer_header(oldRear);
                oldRear->mBuffer = queueBuffer;
                oldRear->mSize = queueSize;
                this->mRear = newRear;
                ++this->mState.count;
                result = SL_RESULT_SUCCESS;
            }
        }
        // set enqueue attribute if state is PLAYING and the first buffer is enqueued
        interface_unlock_exclusive_attributes(this, ((SL_RESULT_SUCCESS == result) &&
            (1 == this->mState.count) && (SL_PLAYSTATE_PLAYING == getAssociatedState(this))) ?
            ATTR_ENQUEUE : ATTR_NONE);
    }
    SL_LEAVE_INTERFACE
}


SLresult IBufferQueue_Clear(SLBufferQueueItf self)
{
    SL_ENTER_INTERFACE

    result = SL_RESULT_SUCCESS;
    IBufferQueue *this = (IBufferQueue *) self;
    interface_lock_exclusive(this);

#ifdef ANDROID
    if (SL_OBJECTID_AUDIOPLAYER == InterfaceToObjectID(this)) {
        CAudioPlayer *audioPlayer = (CAudioPlayer *) this->mThis;
        // flush associated audio player
        result = android_audioPlayer_bufferQueue_onClear(audioPlayer);
        if (SL_RESULT_SUCCESS == result) {
            this->mFront = &this->mArray[0];
            this->mRear = &this->mArray[0];
            this->mState.count = 0;
            this->mState.playIndex = 0;
            this->mSizeConsumed = 0;
        }
    }
#endif

#ifdef USE_OUTPUTMIXEXT
    // The stock code BLOCKS until the mixer's next pass acknowledges -- up
    // to a mixer period per call, on the game's frame threads (= stutter).
    // Fast path: trylock the output mix; success means the mixer is not
    // mid-pass and cannot start one, so clear inline with zero wait.
    // (Trylock, not lock: the mixer takes outputmix->player, we hold player.)
    SLboolean cleared = SL_BOOLEAN_FALSE;
    CAudioPlayer *audioPlayer = (SL_OBJECTID_AUDIOPLAYER == InterfaceToObjectID(this)) ?
        (CAudioPlayer *) this->mThis : NULL;
    Track *track = (NULL != audioPlayer) ? audioPlayer->mTrack : NULL;
    if (NULL == track) {
        // no mixer track is attached: the mixer will never see this queue
        // (and would never acknowledge -- the stock handshake would hang)
        IBufferQueue_FreeOwnedBuffers(this);
        this->mFront = &this->mArray[0];
        this->mRear = &this->mArray[0];
        this->mState.count = 0;
        this->mState.playIndex = 0;
        this->mResamplePhase = 0;
        this->mHaveResampleLast = SL_BOOLEAN_FALSE;
        cleared = SL_BOOLEAN_TRUE;
    } else {
        COutputMix *outputMix = CAudioPlayer_GetOutputMix(audioPlayer);
        if (object_trylock_exclusive(&outputMix->mObject)) {
            IBufferQueue_FreeOwnedBuffers(this);
            this->mFront = &this->mArray[0];
            this->mRear = &this->mArray[0];
            this->mState.count = 0;
            this->mState.playIndex = 0;
            this->mResamplePhase = 0;
            this->mHaveResampleLast = SL_BOOLEAN_FALSE;
            track->mReader = NULL;
            track->mAvail = 0;
            object_unlock_exclusive(&outputMix->mObject);
            cleared = SL_BOOLEAN_TRUE;
        }
    }
    if (!cleared) {
        // mixer is mid-pass (rare: a pass is microseconds out of each 5.8ms
        // period); fall back to the handshake, acknowledged next pass
        extern unsigned int sceKernelGetProcessTimeLow(void); // sl_switch.c
        extern int debugPrintf(char *text, ...);              // util.c
        static int waits = 0;
        unsigned int t0 = sceKernelGetProcessTimeLow();
        this->mClearRequested = SL_BOOLEAN_TRUE;
        do {
            interface_cond_wait(this);
        } while (this->mClearRequested);
        int n = ++waits;
        if (n <= 12 || (n % 500) == 0)
            debugPrintf("SL: BufferQueue::Clear waited %u us for mixer (wait #%d)\n",
                        sceKernelGetProcessTimeLow() - t0, n);
    }
#endif

    interface_unlock_exclusive(this);

    SL_LEAVE_INTERFACE
}


static SLresult IBufferQueue_GetState(SLBufferQueueItf self, SLBufferQueueState *pState)
{
    SL_ENTER_INTERFACE

    // Note that GetState while a Clear is pending is equivalent to GetState before the Clear

    if (NULL == pState) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IBufferQueue *this = (IBufferQueue *) self;
        SLBufferQueueState state;
        interface_lock_shared(this);
#ifdef __cplusplus // FIXME Is this a compiler bug?
        state.count = this->mState.count;
        state.playIndex = this->mState.playIndex;
#else
        state = this->mState;
#endif
        interface_unlock_shared(this);
        *pState = state;
        result = SL_RESULT_SUCCESS;
    }

    SL_LEAVE_INTERFACE
}


SLresult IBufferQueue_RegisterCallback(SLBufferQueueItf self,
    slBufferQueueCallback callback, void *pContext)
{
    SL_ENTER_INTERFACE

    IBufferQueue *this = (IBufferQueue *) self;
    interface_lock_exclusive(this);
    // verify pre-condition that media object is in the SL_PLAYSTATE_STOPPED state
    if (SL_PLAYSTATE_STOPPED == getAssociatedState(this)) {
        this->mCallback = callback;
        this->mContext = pContext;
        result = SL_RESULT_SUCCESS;
    } else {
        result = SL_RESULT_PRECONDITIONS_VIOLATED;
    }
    interface_unlock_exclusive(this);

    SL_LEAVE_INTERFACE
}


static const struct SLBufferQueueItf_ IBufferQueue_Itf = {
    IBufferQueue_Enqueue,
    IBufferQueue_Clear,
    IBufferQueue_GetState,
    IBufferQueue_RegisterCallback
};

void IBufferQueue_init(void *self)
{
    //SL_LOGV("IBufferQueue_init(%p) entering", self);
    IBufferQueue *this = (IBufferQueue *) self;
    this->mItf = &IBufferQueue_Itf;
    this->mState.count = 0;
    this->mState.playIndex = 0;
    this->mCallback = NULL;
    this->mContext = NULL;
    this->mNumBuffers = 0;
    this->mClearRequested = SL_BOOLEAN_FALSE;
    this->mArray = NULL;
    this->mFront = NULL;
    this->mRear = NULL;
    this->mResamplePhase = 0;
    this->mResampleLast[0] = 0;
    this->mResampleLast[1] = 0;
    this->mHaveResampleLast = SL_BOOLEAN_FALSE;
#ifdef ANDROID
    this->mSizeConsumed = 0;
#endif
    BufferHeader *bufferHeader = this->mTypical;
    unsigned i;
    for (i = 0; i < BUFFER_HEADER_TYPICAL+1; ++i, ++bufferHeader) {
        clear_buffer_header(bufferHeader);
    }
}


/** \brief Free the buffer queue, if it was larger than typical.
  * Called by CAudioPlayer_Destroy and CAudioRecorder_Destroy.
  */

void IBufferQueue_Destroy(IBufferQueue *this)
{
    IBufferQueue_FreeOwnedBuffers(this);
    if ((NULL != this->mArray) && (this->mArray != this->mTypical)) {
        free(this->mArray);
        this->mArray = NULL;
    }
}
