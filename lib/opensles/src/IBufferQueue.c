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

static void pcm_convert_exact_up_stereo_i16(int16_t *dst, const void *buffer,
    uint32_t in_frames, uint32_t repeat, int channels, int bps)
{
    if (bps == 16) {
        const int16_t *src = (const int16_t *)buffer;
        for (uint32_t frame = 0; frame < in_frames; ++frame) {
            const int16_t left = src[frame * channels];
            const int16_t right = (channels == 1) ? left : src[frame * channels + 1];
            for (uint32_t r = 0; r < repeat; ++r) {
                *dst++ = left;
                *dst++ = right;
            }
        }
    } else {
        const uint8_t *src = (const uint8_t *)buffer;
        for (uint32_t frame = 0; frame < in_frames; ++frame) {
            const int16_t left = (int16_t)(((int)src[frame * channels] - 0x80) << 8);
            const int16_t right = (channels == 1) ? left :
                (int16_t)(((int)src[frame * channels + 1] - 0x80) << 8);
            for (uint32_t r = 0; r < repeat; ++r) {
                *dst++ = left;
                *dst++ = right;
            }
        }
    }
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
                uint32_t out_frames = (uint32_t)((((uint64_t)in_frames * (uint64_t)out_rate_hz) +
                    (uint64_t)(in_rate_hz / 2)) / (uint64_t)in_rate_hz);
                if (in_frames > 0 && out_frames == 0) {
                    out_frames = 1;
                }
                const SLuint32 out_size = out_frames * (SLuint32)(2 * sizeof(int16_t));
                const SLuint32 alloc_size = out_size ? out_size : 1;
                if (oldRear->mOwnedBuffer != NULL &&
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
                    result = SL_RESULT_MEMORY_FAILURE;
                } else {
                    int16_t *dst = (int16_t *)ownedBuffer;
                    if (out_rate_hz >= (int)in_rate_hz &&
                        ((uint32_t)out_rate_hz % in_rate_hz) == 0) {
                        pcm_convert_exact_up_stereo_i16(dst, pBuffer, in_frames,
                            (uint32_t)out_rate_hz / in_rate_hz,
                            this->channels, this->bps);
                    } else {
                        for (uint32_t of = 0; of < out_frames; of++) {
                            uint32_t sf = (uint32_t)(((uint64_t)of * (uint64_t)in_rate_hz) /
                                (uint64_t)out_rate_hz);
                            if (sf >= in_frames) {
                                sf = in_frames ? (in_frames - 1) : 0;
                            }
                            dst[of * 2 + 0] = pcm_read_sample(pBuffer, sf, this->channels, this->bps, 0);
                            dst[of * 2 + 1] = pcm_read_sample(pBuffer, sf, this->channels, this->bps, 1);
                        }
                    }
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
        cleared = SL_BOOLEAN_TRUE;
    } else {
        COutputMix *outputMix = CAudioPlayer_GetOutputMix(audioPlayer);
        if (object_trylock_exclusive(&outputMix->mObject)) {
            IBufferQueue_FreeOwnedBuffers(this);
            this->mFront = &this->mArray[0];
            this->mRear = &this->mArray[0];
            this->mState.count = 0;
            this->mState.playIndex = 0;
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
