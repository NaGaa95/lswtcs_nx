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

/* OutputMixExt implementation */

#include "sles_allinclusive.h"
#include <math.h>
#include <stdint.h>

#ifdef __SWITCH__
#include "pthr.h"
#endif

// OutputMixExt is used by SDL, but is not specific to or dependent on SDL


// stereo is a frame consisting of a pair of 16-bit PCM samples

typedef struct {
    short left;
    short right;
} stereo;


/** \brief Summary of the gain, as an optimization for the mixer */

typedef enum {
    GAIN_MUTE  = 0,  // mValue == 0.0f within epsilon
    GAIN_UNITY = 1,  // mValue == 1.0f within epsilon
    GAIN_OTHER = 2   // 0.0f < mValue < 1.0f
} Summary;

static inline short clamp_i32_to_i16(int value)
{
    if (value > 32767) {
        return 32767;
    }
    if (value < -32768) {
        return -32768;
    }
    return (short)value;
}

static int output_mix_rate_hz(void)
{
    return (&_opensles_user_freq != NULL && _opensles_user_freq > 0) ?
        _opensles_user_freq : 44100;
}

typedef struct {
    slPlayCallback callback;
    SLPlayItf itf;
    void *context;
    SLuint32 event;
    CAudioPlayer *audioPlayer;
} PendingPlayCallback;

#define PLAY_CALLBACK_QUEUE_CAP (MAX_TRACK * 2U)

typedef struct {
    PendingPlayCallback events[PLAY_CALLBACK_QUEUE_CAP];
    unsigned front;
    unsigned count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t thread;
    SLboolean started;
    SLboolean disabled;
} PlayCallbackQueue;

static void complete_play_callback(const PendingPlayCallback *event)
{
    CAudioPlayer *audioPlayer = event->audioPlayer;
    assert(audioPlayer != NULL);
    object_lock_exclusive(&audioPlayer->mObject);
    assert(audioPlayer->mPendingPlayCallbacks > 0);
    if (--audioPlayer->mPendingPlayCallbacks == 0) {
        object_cond_broadcast(&audioPlayer->mObject);
    }
    object_unlock_exclusive(&audioPlayer->mObject);
}

static void *play_callback_thread(void *context)
{
    PlayCallbackQueue *queue = (PlayCallbackQueue *)context;
#ifdef __SWITCH__
    pthr_ensure_fake_tls();
    pthr_pin_bg_core(); // keep play callbacks off the render/main cores
#endif

    for (;;) {
        PendingPlayCallback event;
        int ok = pthread_mutex_lock(&queue->mutex);
        assert(0 == ok);
        while (queue->count == 0) {
            ok = pthread_cond_wait(&queue->cond, &queue->mutex);
            assert(0 == ok);
        }
        event = queue->events[queue->front];
        if (++queue->front == PLAY_CALLBACK_QUEUE_CAP) {
            queue->front = 0;
        }
        --queue->count;
        ok = pthread_mutex_unlock(&queue->mutex);
        assert(0 == ok);

        if (event.callback != NULL) {
            (*event.callback)(event.itf, event.context, event.event);
        }
        complete_play_callback(&event);
    }

    return NULL;
}

static PlayCallbackQueue g_play_callback_queue = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER,
};

static void play_callback_queue_start(void)
{
    PlayCallbackQueue *queue = &g_play_callback_queue;
    int ok = pthread_mutex_lock(&queue->mutex);
    assert(0 == ok);
    if (!queue->started && !queue->disabled) {
        ok = pthread_create(&queue->thread, (const pthread_attr_t *)NULL,
            play_callback_thread, queue);
        if (ok == 0) {
            queue->started = SL_BOOLEAN_TRUE;
            pthread_detach(queue->thread);
        } else {
            queue->disabled = SL_BOOLEAN_TRUE;
        }
    }
    ok = pthread_mutex_unlock(&queue->mutex);
    assert(0 == ok);
}

static SLboolean enqueue_play_callback(const PendingPlayCallback *event)
{
    PlayCallbackQueue *queue = &g_play_callback_queue;
    int ok = pthread_mutex_lock(&queue->mutex);
    if (ok != 0) {
        return SL_BOOLEAN_FALSE;
    }
    SLboolean queued = SL_BOOLEAN_FALSE;
    if (queue->started && !queue->disabled && queue->count < PLAY_CALLBACK_QUEUE_CAP) {
        unsigned rear = queue->front + queue->count;
        if (rear >= PLAY_CALLBACK_QUEUE_CAP) {
            rear -= PLAY_CALLBACK_QUEUE_CAP;
        }
        queue->events[rear] = *event;
        ++queue->count;
        ok = pthread_cond_signal(&queue->cond);
        assert(0 == ok);
        queued = SL_BOOLEAN_TRUE;
    }
    ok = pthread_mutex_unlock(&queue->mutex);
    assert(0 == ok);
    return queued;
}

static inline SLboolean track_slot_is_active(const IOutputMixExt *outputMix, unsigned index)
{
    assert(index < MAX_TRACK);
    return 0 != (outputMix->mActiveMask[TRACK_MASK_WORD(index)] & TRACK_MASK_BIT(index));
}

static inline void track_slot_activate(IOutputMixExt *outputMix, unsigned index)
{
    assert(index < MAX_TRACK);
    outputMix->mActiveMask[TRACK_MASK_WORD(index)] |= TRACK_MASK_BIT(index);
}

static inline void track_slot_deactivate(IOutputMixExt *outputMix, unsigned index)
{
    assert(index < MAX_TRACK);
    outputMix->mActiveMask[TRACK_MASK_WORD(index)] &= ~TRACK_MASK_BIT(index);
}

static unsigned find_free_track_slot(const IOutputMixExt *outputMix)
{
    for (unsigned index = 0; index < MAX_TRACK; ++index) {
        if (!track_slot_is_active(outputMix, index)) {
            return index;
        }
    }
    return MAX_TRACK;
}


/** \brief Check whether a track has any data for us to read */

static SLboolean track_check(Track *track, PendingPlayCallback *pending,
    unsigned *pending_count)
{
    assert(NULL != track);
    SLboolean trackHasData = SL_BOOLEAN_FALSE;

    CAudioPlayer *audioPlayer = track->mAudioPlayer;
    if (NULL != audioPlayer) {

        // track is initialized

        // FIXME This lock could block and result in stuttering;
        // a trylock with retry or lockless solution would be ideal
        object_lock_exclusive(&audioPlayer->mObject);
        assert(audioPlayer->mTrack == track);

        SLuint32 framesMixed = track->mFramesMixed;
        if (0 != framesMixed) {
            track->mFramesMixed = 0;
            audioPlayer->mPlay.mFramesSinceLastSeek += framesMixed;
            audioPlayer->mPlay.mFramesSincePositionUpdate += framesMixed;
            const SLuint32 sampleRateMilliHz = audioPlayer->mSampleRateMilliHz;
            if (sampleRateMilliHz != 0) {
                audioPlayer->mPlay.mPosition = audioPlayer->mPlay.mLastSeekPosition +
                    (SLmillisecond)(((unsigned long long)
                    audioPlayer->mPlay.mFramesSinceLastSeek * 1000000ULL) /
                    sampleRateMilliHz);
            }
        }

        // Positional attenuation and per-voice volume can change every game frame.
        // Refresh them every mixer pass, including while a long queue buffer is active.
        track->mGains[0] = audioPlayer->mGains[0];
        track->mGains[1] = audioPlayer->mGains[1];

        SLboolean doBroadcast = SL_BOOLEAN_FALSE;
        const BufferHeader *oldFront;
        slPlayCallback headAtEndCallback = NULL;
        void *headAtEndContext = NULL;

        if (audioPlayer->mBufferQueue.mClearRequested) {
            // application thread(s) that call BufferQueue::Clear while mixer is active
            // will block synchronously until mixer acknowledges the Clear request
            IBufferQueue_FreeOwnedBuffers(&audioPlayer->mBufferQueue);
            audioPlayer->mBufferQueue.mFront = &audioPlayer->mBufferQueue.mArray[0];
            audioPlayer->mBufferQueue.mRear = &audioPlayer->mBufferQueue.mArray[0];
            audioPlayer->mBufferQueue.mState.count = 0;
            audioPlayer->mBufferQueue.mState.playIndex = 0;
            audioPlayer->mBufferQueue.mResamplePhase = 0;
            audioPlayer->mBufferQueue.mHaveResampleLast = SL_BOOLEAN_FALSE;
            audioPlayer->mBufferQueue.mClearRequested = SL_BOOLEAN_FALSE;
            track->mReader = NULL;
            track->mAvail = 0;
            doBroadcast = SL_BOOLEAN_TRUE;
        }

        if (audioPlayer->mDestroyRequested) {
            // an application thread that calls Object::Destroy while mixer is active will block
            // synchronously in the PreDestroy hook until mixer acknowledges the Destroy request
            COutputMix *outputMix = CAudioPlayer_GetOutputMix(audioPlayer);
            unsigned i = track - outputMix->mOutputMixExt.mTracks;
            assert( /* 0 <= i && */ i < MAX_TRACK);
            track->mAudioPlayer = NULL;
            assert(track_slot_is_active(&outputMix->mOutputMixExt, i));
            track_slot_deactivate(&outputMix->mOutputMixExt, i);
            audioPlayer->mTrack = NULL;
            audioPlayer->mDestroyRequested = SL_BOOLEAN_FALSE;
            doBroadcast = SL_BOOLEAN_TRUE;
            goto broadcast;
        }

        switch (audioPlayer->mPlay.mState) {

        case SL_PLAYSTATE_PLAYING:  // continue playing current track data
            if (0 < track->mAvail) {
                trackHasData = SL_BOOLEAN_TRUE;
                break;
            }

            // try to get another buffer from queue
            oldFront = audioPlayer->mBufferQueue.mFront;
            if (oldFront != audioPlayer->mBufferQueue.mRear) {
                assert(0 < audioPlayer->mBufferQueue.mState.count);
                track->mReader = oldFront->mBuffer;
                track->mAvail = oldFront->mSize;
                // note that the buffer stays on the queue while we are reading
                audioPlayer->mPlay.mState = SL_PLAYSTATE_PLAYING;
                trackHasData = SL_BOOLEAN_TRUE;
            } else {
                // no buffers on queue, so playable but not playing
                // NTH should be able to call a desperation callback when completely starved,
                // or call less often than every buffer based on high/low water-marks
                if (track->mReader != NULL) {
                    track->mReader = NULL;
                    if (audioPlayer->mPlay.mEventFlags & SL_PLAYEVENT_HEADATEND) {
                        headAtEndCallback = audioPlayer->mPlay.mCallback;
                        headAtEndContext = audioPlayer->mPlay.mContext;
                    }
                }
#ifdef SYBERIA
				audioPlayer->mPlay.mState = SL_PLAYSTATE_STOPPING;
#endif
            }

            break;

        case SL_PLAYSTATE_STOPPING: // application thread(s) called Play::SetPlayState(STOPPED)
            audioPlayer->mPlay.mPosition = (SLmillisecond) 0;
            audioPlayer->mPlay.mFramesSinceLastSeek = 0;
            audioPlayer->mPlay.mFramesSincePositionUpdate = 0;
            audioPlayer->mPlay.mLastSeekPosition = 0;
            audioPlayer->mPlay.mState = SL_PLAYSTATE_STOPPED;
            // stop cancels a pending seek
            audioPlayer->mSeek.mPos = SL_TIME_UNKNOWN;
            oldFront = audioPlayer->mBufferQueue.mFront;
            if (oldFront != audioPlayer->mBufferQueue.mRear) {
                assert(0 < audioPlayer->mBufferQueue.mState.count);
                track->mReader = oldFront->mBuffer;
                track->mAvail = oldFront->mSize;
            }
            doBroadcast = SL_BOOLEAN_TRUE;
            break;

        case SL_PLAYSTATE_STOPPED:  // idle
        case SL_PLAYSTATE_PAUSED:   // idle
            break;

        default:
            assert(SL_BOOLEAN_FALSE);
            break;
        }

broadcast:
        if (doBroadcast) {
            object_cond_broadcast(&audioPlayer->mObject);
        }

        if (headAtEndCallback != NULL && *pending_count < MAX_TRACK) {
            PendingPlayCallback *event = &pending[(*pending_count)++];
            event->callback = headAtEndCallback;
            event->itf = &audioPlayer->mPlay.mItf;
            event->context = headAtEndContext;
            event->event = SL_PLAYEVENT_HEADATEND;
            event->audioPlayer = audioPlayer;
            ++audioPlayer->mPendingPlayCallbacks;
        }

        object_unlock_exclusive(&audioPlayer->mObject);

    }

    return trackHasData;

}


/** \brief This is the track mixer: fill the specified 16-bit stereo PCM buffer */

void IOutputMixExt_FillBuffer(SLOutputMixExtItf self, void *pBuffer, SLuint32 size)
{
    SL_ENTER_INTERFACE_VOID

    // Force to be a multiple of a frame, assumes stereo 16-bit PCM
    size &= ~3;
    if (size == 0) {
        SL_LEAVE_INTERFACE_VOID
    }
    const unsigned sampleCount = size / sizeof(short);
    int32_t mixAccum[sampleCount];
    memset(mixAccum, 0, sizeof(mixAccum));
    SLboolean mixBufferHasData = SL_BOOLEAN_FALSE;
    IOutputMixExt *this = (IOutputMixExt *) self;
    IObject *thisObject = this->mThis;
    PendingPlayCallback pending[MAX_TRACK];
    unsigned pending_count = 0;
    // This lock should never block, except when the application destroys the output mix object
    object_lock_exclusive(thisObject);
    SLboolean mixActive = SL_BOOLEAN_TRUE;
    // If the output mix is marked for destruction, then acknowledge the request
    if (this->mDestroyRequested) {
        IEngine *thisEngine = thisObject->mEngine;
        interface_lock_exclusive(thisEngine);
        assert(&thisEngine->mOutputMix->mObject == thisObject);
        thisEngine->mOutputMix = NULL;
        // Note we don't attempt to connect another output mix, even if there is one
        interface_unlock_exclusive(thisEngine);
        // Acknowledge the destroy request, and notify the pre-destroy hook
        this->mDestroyRequested = SL_BOOLEAN_FALSE;
        object_cond_broadcast(thisObject);
        mixActive = SL_BOOLEAN_FALSE;
    }
    for (unsigned i = 0; mixActive && i < MAX_TRACK; ++i) {
        if (!track_slot_is_active(this, i)) {
            continue;
        }
        Track *track = &this->mTracks[i];

        // track is allocated

        if (!track_check(track, pending, &pending_count)) {
            continue;
        }

        // track is playing
        int32_t *mixWriter = mixAccum;
        unsigned desired = size;
        SLboolean trackContributedToMix = SL_BOOLEAN_FALSE;
        Summary summaries[STEREO_CHANNELS];
        unsigned channel;
        for (channel = 0; channel < STEREO_CHANNELS; ++channel) {
            float gain = track->mGains[channel];
            Summary summary;
            if (gain <= 0.001) {
                summary = GAIN_MUTE;
            } else if (gain >= 0.999) {
                summary = GAIN_UNITY;
            } else {
                summary = GAIN_OTHER;
            }
            summaries[channel] = summary;
        }
        while (desired > 0) {
            unsigned actual = desired;
            if (track->mAvail < actual) {
                actual = track->mAvail;
            }
            // force actual to be a frame multiple
            if (actual > 0) {
                assert(NULL != track->mReader);
                const stereo *source = (const stereo *) track->mReader;
                const unsigned frames = actual / sizeof(stereo);
                unsigned j;
                if (GAIN_MUTE != summaries[0] || GAIN_MUTE != summaries[1]) {
                    if (GAIN_UNITY != summaries[0] || GAIN_UNITY != summaries[1]) {
                        for (j = 0; j < frames; ++j, mixWriter += STEREO_CHANNELS, ++source) {
                            mixWriter[0] += (int)(source->left * track->mGains[0]);
                            mixWriter[1] += (int)(source->right * track->mGains[1]);
                        }
                    } else {
                        for (j = 0; j < frames; ++j, mixWriter += STEREO_CHANNELS, ++source) {
                            mixWriter[0] += source->left;
                            mixWriter[1] += source->right;
                        }
                    }
                    trackContributedToMix = SL_BOOLEAN_TRUE;
                } else {
                    mixWriter += frames * STEREO_CHANNELS;
                }
                desired -= actual;
                track->mReader = (char *) track->mReader + actual;
                track->mAvail -= actual;
                if (track->mAvail == 0) {
                    IBufferQueue *bufferQueue = &track->mAudioPlayer->mBufferQueue;
                    interface_lock_exclusive(bufferQueue);
                    BufferHeader *oldFront, *newFront, *rear;
                    oldFront = bufferQueue->mFront;
                    rear = bufferQueue->mRear;
                    // a buffer stays on queue while playing, so it better still be there
                    assert(oldFront != rear);
                    newFront = oldFront;
                    if (++newFront == &bufferQueue->mArray[bufferQueue->mNumBuffers + 1]) {
                        newFront = bufferQueue->mArray;
                    }
                    bufferQueue->mFront = (BufferHeader *) newFront;
                    assert(0 < bufferQueue->mState.count);
                    --bufferQueue->mState.count;
                    IBufferQueue_ReleaseBufferHeader(oldFront);
                    if (newFront != rear) {
                        // we don't acknowledge application requests between buffers
                        // within the same mixer frame
                        assert(0 < bufferQueue->mState.count);
                        track->mReader = newFront->mBuffer;
                        track->mAvail = newFront->mSize;
                    }
                    // else we would set play state to playable but not playing during next mixer
                    // frame if the queue is still empty at that time
                    ++bufferQueue->mState.playIndex;
                    slBufferQueueCallback callback = bufferQueue->mCallback;
                    void *context = bufferQueue->mContext;
                    interface_unlock_exclusive(bufferQueue);
                    // The callback function is called on each buffer completion
                    if (NULL != callback) {
                        (*callback)((SLBufferQueueItf) bufferQueue, context);
                        // Maybe it enqueued another buffer, or maybe it didn't.
                        // We will find out later during the next mixer frame.
                    }
                }
                // no lock, but safe because noone else updates this field
                track->mFramesMixed += actual >> 2;    // sizeof(short) * STEREO_CHANNELS
                continue;
            }
            // we need more data: desired > 0 but actual == 0
            if (track_check(track, pending, &pending_count)) {
                continue;
            }
            break;
        }
        if (trackContributedToMix) {
            mixBufferHasData = SL_BOOLEAN_TRUE;
        }
    }

    // Stereo-linked, sample-rate limiter. The old block-wide limiter changed
    // gain once per 1024-frame callback and held the whole mix down for roughly
    // a second after one loud SFX, which was audible as pumping/choppy dialogue.
    // Updating per frame keeps transients safe without modulating an entire block.
    if (mixBufferHasData) {
        const float ceiling = 30000.0f;
        const float release = 1.0f / (output_mix_rate_hz() * 0.080f);
        float gain = this->mLimiterGain;
        short *out = (short *)pBuffer;
        for (unsigned i = 0; i < sampleCount; i += STEREO_CHANNELS) {
            const int64_t left = mixAccum[i];
            const int64_t right = mixAccum[i + 1];
            const int64_t leftMagnitude = left < 0 ? -left : left;
            const int64_t rightMagnitude = right < 0 ? -right : right;
            const int64_t peak = leftMagnitude > rightMagnitude ?
                leftMagnitude : rightMagnitude;
            const float target = peak > (int64_t)ceiling ?
                ceiling / (float)peak : 1.0f;
            if (target < gain) {
                gain = target; // immediate attack prevents clipping
            } else {
                gain += (target - gain) * release;
            }
            out[i] = clamp_i32_to_i16((int)(left * gain));
            out[i + 1] = clamp_i32_to_i16((int)(right * gain));
        }
        this->mLimiterGain = gain;
    } else {
        this->mLimiterGain = 1.0f;
    }
    object_unlock_exclusive(thisObject);
    // No active tracks, so output silence
    if (!mixBufferHasData) {
        memset(pBuffer, 0, size);
    }

    for (unsigned i = 0; i < pending_count; ++i) {
        if (!enqueue_play_callback(&pending[i])) {
            // Queue startup failure or a full queue must not lose HEADATEND.
            // The game callback is short and does not re-enter OpenSL.
            (*pending[i].callback)(pending[i].itf, pending[i].context, pending[i].event);
            complete_play_callback(&pending[i]);
        }
    }

    SL_LEAVE_INTERFACE_VOID
}


static const struct SLOutputMixExtItf_ IOutputMixExt_Itf = {
    IOutputMixExt_FillBuffer
};

void IOutputMixExt_init(void *self)
{
    play_callback_queue_start();
    IOutputMixExt *this = (IOutputMixExt *) self;
    this->mItf = &IOutputMixExt_Itf;
    memset(this->mActiveMask, 0, sizeof(this->mActiveMask));
    this->mLimiterGain = 1.0f;
    Track *track = &this->mTracks[0];
    unsigned i;
    for (i = 0; i < MAX_TRACK; ++i, ++track) {
        track->mAudioPlayer = NULL;
    }
    this->mDestroyRequested = SL_BOOLEAN_FALSE;
}


/** \brief Called by Engine::CreateAudioPlayer to allocate a track */

SLresult IOutputMixExt_checkAudioPlayerSourceSink(CAudioPlayer *this)
{
    this->mTrack = NULL;

    // check the source for compatibility
    switch (this->mDataSource.mLocator.mLocatorType) {
    case SL_DATALOCATOR_BUFFERQUEUE:
    case SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE:
        switch (this->mDataSource.mFormat.mFormatType) {
        case SL_DATAFORMAT_PCM:
#ifdef USE_SDL
            // SDL is hard-coded to 44.1 kHz, and there is no sample rate converter
           // if (SL_SAMPLINGRATE_44_1 != this->mDataSource.mFormat.mPCM.samplesPerSec)
           //       SL_LOGE("WEEE");
           // return SL_RESULT_CONTENT_UNSUPPORTED;
#endif
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }

    // check the sink for compatibility
    const SLDataSink *pAudioSnk = &this->mDataSink.u.mSink;
    Track *track = NULL;
    switch (*(SLuint32 *)pAudioSnk->pLocator) {
    case SL_DATALOCATOR_OUTPUTMIX:
        {
        // pAudioSnk->pFormat is ignored
        IOutputMixExt *omExt = &((COutputMix *) ((SLDataLocator_OutputMix *)
            pAudioSnk->pLocator)->outputMix)->mOutputMixExt;
        // allocate an entry within OutputMix for this track
        interface_lock_exclusive(omExt);
        unsigned i = find_free_track_slot(omExt);
        if (i == MAX_TRACK) {
            interface_unlock_exclusive(omExt);
            // All track slots full in output mix
            extern int debugPrintf(char *text, ...);
            static unsigned limitHits = 0;
            const unsigned hit = ++limitHits;
            if (hit <= 8 || (hit % 100U) == 0) {
                debugPrintf("SL: audio player limit reached (%u tracks, hit #%u)\n",
                    (unsigned)MAX_TRACK, hit);
            }
            return SL_RESULT_MEMORY_FAILURE;
        }
        track_slot_activate(omExt, i);
        track = &omExt->mTracks[i];
        track->mAudioPlayer = NULL;    // only field that is accessed before full initialization
        interface_unlock_exclusive(omExt);
        this->mTrack = track;
        this->mGains[0] = 1.0f;
        this->mGains[1] = 1.0f;
        this->mDestroyRequested = SL_BOOLEAN_FALSE;
        }
        break;
    default:
        return SL_RESULT_CONTENT_UNSUPPORTED;
    }

    assert(NULL != track);
    track->mBufferQueue = &this->mBufferQueue;
    track->mAudioPlayer = this;
    track->mReader = NULL;
    track->mAvail = 0;
    track->mGains[0] = 1.0f;
    track->mGains[1] = 1.0f;
    track->mFramesMixed = 0;
    return SL_RESULT_SUCCESS;
}


/** \brief Called when a gain-related field (mute, solo, volume, stereo position, etc.) updated */

void audioPlayerGainUpdate(CAudioPlayer *audioPlayer)
{
    SLboolean mute = audioPlayer->mVolume.mMute;
    SLuint8 muteMask = audioPlayer->mMuteMask;
    SLuint8 soloMask = audioPlayer->mSoloMask;
    SLmillibel level = audioPlayer->mVolume.mLevel;
    SLboolean enableStereoPosition = audioPlayer->mVolume.mEnableStereoPosition;
    SLpermille stereoPosition = audioPlayer->mVolume.mStereoPosition;

    if (soloMask) {
        muteMask |= ~soloMask;
    }
    if (mute || !(~muteMask & 3)) {
        audioPlayer->mGains[0] = 0.0f;
        audioPlayer->mGains[1] = 0.0f;
    } else {
        float playerGain = powf(10.0f, level / 2000.0f);
        unsigned channel;
        for (channel = 0; channel < STEREO_CHANNELS; ++channel) {
            float gain;
            if (muteMask & (1 << channel)) {
                gain = 0.0f;
            } else {
                gain = playerGain;
                if (enableStereoPosition) {
                    switch (channel) {
                    case 0:
                        if (stereoPosition > 0) {
                            gain *= (1000 - stereoPosition) / 1000.0f;
                        }
                        break;
                    case 1:
                        if (stereoPosition < 0) {
                            gain *= (1000 + stereoPosition) / 1000.0f;
                        }
                        break;
                    default:
                        assert(SL_BOOLEAN_FALSE);
                        break;
                    }
                }
            }
            audioPlayer->mGains[channel] = gain;
        }
    }
}
