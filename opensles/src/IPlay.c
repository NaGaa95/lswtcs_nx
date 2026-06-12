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

/* Play implementation */

#include <psp2/kernel/processmgr.h>

#include "sles_allinclusive.h"


static SLresult IPlay_SetPlayState(SLPlayItf self, SLuint32 state)
{
    SL_ENTER_INTERFACE

    switch (state) {
    case SL_PLAYSTATE_STOPPED:
    case SL_PLAYSTATE_PAUSED:
    case SL_PLAYSTATE_PLAYING:
        {
        IPlay *this = (IPlay *) self;
        unsigned attr = ATTR_NONE;
        result = SL_RESULT_SUCCESS;
        CAudioPlayer *audioPlayer = (SL_OBJECTID_AUDIOPLAYER == InterfaceToObjectID(this)) ?
            (CAudioPlayer *) this->mThis : NULL;
        interface_lock_exclusive(this);
#ifdef USE_OUTPUTMIXEXT
        for (;; interface_cond_wait(this)) {

            // We are comparing the old state (left) vs. new state (right).
            // Note that the old state is 3 bits wide, but new state is only 2 bits wide.
            // That is why the old state is on the left and new state is on the right.
            switch ((this->mState << 2) | state) {

            case (SL_PLAYSTATE_STOPPED  << 2) | SL_PLAYSTATE_STOPPED:
            case (SL_PLAYSTATE_PAUSED   << 2) | SL_PLAYSTATE_PAUSED:
            case (SL_PLAYSTATE_PLAYING  << 2) | SL_PLAYSTATE_PLAYING:
               // no-op
                break;

            case (SL_PLAYSTATE_STOPPED  << 2) | SL_PLAYSTATE_PLAYING:
            case (SL_PLAYSTATE_PAUSED   << 2) | SL_PLAYSTATE_PLAYING:
                attr = ATTR_TRANSPORT;
#ifdef LSWTCS
                this->mLastTick = sceKernelGetProcessTimeLow();
#endif
                // set enqueue attribute if queue is non-empty and state becomes PLAYING
                if ((NULL != audioPlayer) && (audioPlayer->mBufferQueue.mFront !=
                    audioPlayer->mBufferQueue.mRear)) {
                    attr |= ATTR_ENQUEUE;
                }
                // fall through

            case (SL_PLAYSTATE_PLAYING  << 2) | SL_PLAYSTATE_PAUSED:
#ifdef LSWTCS
                this->mPosition = sceKernelGetProcessTimeLow() - this->mLastTick;
#endif
            case (SL_PLAYSTATE_STOPPED  << 2) | SL_PLAYSTATE_PAUSED:
                // easy
                this->mState = state;
                break;

            case (SL_PLAYSTATE_STOPPING << 2) | SL_PLAYSTATE_STOPPED:
                // either spurious wakeup, or someone else requested same transition
                continue;

            case (SL_PLAYSTATE_STOPPING << 2) | SL_PLAYSTATE_PAUSED:
            case (SL_PLAYSTATE_STOPPING << 2) | SL_PLAYSTATE_PLAYING:
                // wait for other guy to finish his transition, then retry ours
                continue;

            case (SL_PLAYSTATE_PAUSED   << 2) | SL_PLAYSTATE_STOPPED:
            case (SL_PLAYSTATE_PLAYING  << 2) | SL_PLAYSTATE_STOPPED:
                // tell mixer to stop, then wait for mixer to acknowledge the request to stop
                this->mState = SL_PLAYSTATE_STOPPING;
                continue;

            default:
                // unexpected state
                assert(SL_BOOLEAN_FALSE);
                result = SL_RESULT_INTERNAL_ERROR;
                break;

            }

            break;
        }
#else
        // Here life looks easy for an Android, but there are other troubles in play land
        this->mState = state;
        attr = ATTR_TRANSPORT;
#endif
        interface_unlock_exclusive_attributes(this, attr);
        }
        break;
    default:
        result = SL_RESULT_PARAMETER_INVALID;
        break;
    }

    SL_LEAVE_INTERFACE
}


static SLresult IPlay_GetPlayState(SLPlayItf self, SLuint32 *pState)
{
    SL_ENTER_INTERFACE

    if (NULL == pState) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IPlay *this = (IPlay *) self;
        interface_lock_peek(this);
        SLuint32 state = this->mState;
        interface_unlock_peek(this);
        result = SL_RESULT_SUCCESS;
#ifdef USE_OUTPUTMIXEXT
        switch (state) {
        case SL_PLAYSTATE_STOPPED:  // as is
        case SL_PLAYSTATE_PAUSED:
        case SL_PLAYSTATE_PLAYING:
            break;
        case SL_PLAYSTATE_STOPPING: // these states require re-mapping
            state = SL_PLAYSTATE_STOPPED;
            break;
        default:                    // impossible
            assert(SL_BOOLEAN_FALSE);
            state = SL_PLAYSTATE_STOPPED;
            result = SL_RESULT_INTERNAL_ERROR;
            break;
        }
#endif
        *pState = state;
    }

    SL_LEAVE_INTERFACE
}


static SLresult IPlay_GetDuration(SLPlayItf self, SLmillisecond *pMsec)
{
    SL_ENTER_INTERFACE

    if (NULL == pMsec) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        result = SL_RESULT_SUCCESS;
        IPlay *this = (IPlay *) self;
        // even though this is a getter, it can modify state due to caching
        interface_lock_exclusive(this);
        SLmillisecond duration = this->mDuration;
#ifdef ANDROID
        if ((SL_TIME_UNKNOWN == duration) &&
            (SL_OBJECTID_AUDIOPLAYER == InterfaceToObjectID(this))) {
            SLmillisecond temp;
            result = android_audioPlayer_getDuration(this, &temp);
            if (SL_RESULT_SUCCESS == result) {
                duration = temp;
                this->mDuration = duration;
            }
        }
#else
        // will be set by containing AudioPlayer or MidiPlayer object at Realize, if known,
        // otherwise the duration will be updated each time a new maximum position is detected
#endif
        interface_unlock_exclusive(this);
        *pMsec = duration;
    }

    SL_LEAVE_INTERFACE
}


static SLresult IPlay_GetPosition(SLPlayItf self, SLmillisecond *pMsec)
{
    SL_ENTER_INTERFACE

    if (NULL == pMsec) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IPlay *this = (IPlay *) self;
        SLmillisecond position;
        interface_lock_shared(this);
#ifdef ANDROID
        // Android does not use the mPosition field for audio players
        if (SL_OBJECTID_AUDIOPLAYER == InterfaceToObjectID(this)) {
            android_audioPlayer_getPosition(this, &position);
            // note that we do not cache the position
        } else {
            position = this->mPosition;
        }
#else
#ifdef LSWTCS
        if (SL_PLAYSTATE_PLAYING == this->mState) {
            const SceUInt32 tick = sceKernelGetProcessTimeLow();
            this->mPosition += tick - this->mLastTick;
            this->mLastTick = tick;
        }
        position = this->mPosition / 1000;
#else
        // on other platforms we depend on periodic updates to the current position
        position = this->mPosition;
#endif
        //SL_LOGV("POSITION %lu", position);
        // if a seek is pending, then lie about current position so the seek appears synchronous
        if (SL_OBJECTID_AUDIOPLAYER == InterfaceToObjectID(this)) {
            //SL_LOGV("SEEK");
            CAudioPlayer *audioPlayer = (CAudioPlayer *) this->mThis;
            SLmillisecond pos = audioPlayer->mSeek.mPos;
            if (SL_TIME_UNKNOWN != pos) {
                //SL_LOGV("TIME UNKNOWN");
                //position = pos;
            }
        }
#endif
        interface_unlock_shared(this);
        *pMsec = position;
        result = SL_RESULT_SUCCESS;

        //SL_LOGV("IPlay_GetPosition(%p, %p = %lu): %u", this, pMsec, *pMsec, result);
    }

    SL_LEAVE_INTERFACE
}


static SLresult IPlay_RegisterCallback(SLPlayItf self, slPlayCallback callback, void *pContext)
{
    SL_ENTER_INTERFACE

    IPlay *this = (IPlay *) self;
    interface_lock_exclusive(this);
    this->mCallback = callback;
    this->mContext = pContext;
    // omits _attributes b/c noone cares deeply enough about these fields to need quick notification
    interface_unlock_exclusive(this);
    result = SL_RESULT_SUCCESS;

    SL_LEAVE_INTERFACE
}


static SLresult IPlay_SetCallbackEventsMask(SLPlayItf self, SLuint32 eventFlags)
{
    SL_ENTER_INTERFACE

    if (eventFlags & ~(SL_PLAYEVENT_HEADATEND | SL_PLAYEVENT_HEADATMARKER |
            SL_PLAYEVENT_HEADATNEWPOS | SL_PLAYEVENT_HEADMOVING | SL_PLAYEVENT_HEADSTALLED)) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IPlay *this = (IPlay *) self;
        interface_lock_exclusive(this);
        if (this->mEventFlags != eventFlags) {
#ifdef USE_OUTPUTMIXEXT
            // enabling the "head at new position" play event will postpone the next update event
            if (!(this->mEventFlags & SL_PLAYEVENT_HEADATNEWPOS) &&
                    (eventFlags & SL_PLAYEVENT_HEADATNEWPOS)) {
                this->mFramesSincePositionUpdate = 0;
            }
#endif
            this->mEventFlags = eventFlags;
            interface_unlock_exclusive_attributes(this, ATTR_TRANSPORT);
        } else
            interface_unlock_exclusive(this);
        result = SL_RESULT_SUCCESS;
    }

    SL_LEAVE_INTERFACE
}


static SLresult IPlay_GetCallbackEventsMask(SLPlayItf self, SLuint32 *pEventFlags)
{
    SL_ENTER_INTERFACE

    if (NULL == pEventFlags) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IPlay *this = (IPlay *) self;
        interface_lock_peek(this);
        SLuint32 eventFlags = this->mEventFlags;
        interface_unlock_peek(this);
        *pEventFlags = eventFlags;
        result = SL_RESULT_SUCCESS;
    }

    SL_LEAVE_INTERFACE
}


static SLresult IPlay_SetMarkerPosition(SLPlayItf self, SLmillisecond mSec)
{
    SL_ENTER_INTERFACE

    IPlay *this = (IPlay *) self;
    interface_lock_exclusive(this);
    if (this->mMarkerPosition != mSec) {
        this->mMarkerPosition = mSec;
        interface_unlock_exclusive_attributes(this, ATTR_TRANSPORT);
    } else
        interface_unlock_exclusive(this);
    result = SL_RESULT_SUCCESS;

    SL_LEAVE_INTERFACE
}


static SLresult IPlay_ClearMarkerPosition(SLPlayItf self)
{
    SL_ENTER_INTERFACE

    IPlay *this = (IPlay *) self;
    interface_lock_exclusive(this);
#ifdef ANDROID
    if (SL_OBJECTID_AUDIOPLAYER == InterfaceToObjectID(this)) {
        // clearing the marker position is equivalent to setting the marker at 0
        this->mMarkerPosition = 0;
    }
#endif
    interface_unlock_exclusive_attributes(this, ATTR_TRANSPORT);
    result = SL_RESULT_SUCCESS;

    SL_LEAVE_INTERFACE
}


static SLresult IPlay_GetMarkerPosition(SLPlayItf self, SLmillisecond *pMsec)
{
    SL_ENTER_INTERFACE

    if (NULL == pMsec) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IPlay *this = (IPlay *) self;
        interface_lock_peek(this);
        SLmillisecond markerPosition = this->mMarkerPosition;
        interface_unlock_peek(this);
        *pMsec = markerPosition;
        result = SL_RESULT_SUCCESS;
    }

    SL_LEAVE_INTERFACE
}


static SLresult IPlay_SetPositionUpdatePeriod(SLPlayItf self, SLmillisecond mSec)
{
    SL_ENTER_INTERFACE

    if (0 == mSec) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IPlay *this = (IPlay *) self;
        interface_lock_exclusive(this);
        if (this->mPositionUpdatePeriod != mSec) {
            this->mPositionUpdatePeriod = mSec;
#ifdef ANDROID
            if (SL_OBJECTID_AUDIOPLAYER == InterfaceToObjectID(this)) {
                // result = android_audioPlayer_useEventMask(this, this->mEventFlags);
            }
#endif
#ifdef USE_OUTPUTMIXEXT
            if (SL_OBJECTID_AUDIOPLAYER == InterfaceToObjectID(this)) {
                CAudioPlayer *audioPlayer = (CAudioPlayer *) this->mThis;
                SLuint32 frameUpdatePeriod = ((long long) mSec *
                    (long long) audioPlayer->mSampleRateMilliHz) / 1000000LL;
                if (0 == frameUpdatePeriod)
                    frameUpdatePeriod = ~0;
                this->mFrameUpdatePeriod = frameUpdatePeriod;
                // setting a new update period postpones the next callback
                this->mFramesSincePositionUpdate = 0;
            }
#endif
            interface_unlock_exclusive_attributes(this, ATTR_TRANSPORT);
        } else
            interface_unlock_exclusive(this);
        result = SL_RESULT_SUCCESS;
    }

    SL_LEAVE_INTERFACE
}


static SLresult IPlay_GetPositionUpdatePeriod(SLPlayItf self, SLmillisecond *pMsec)
{
    SL_ENTER_INTERFACE

    if (NULL == pMsec) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IPlay *this = (IPlay *) self;
        interface_lock_peek(this);
        SLmillisecond positionUpdatePeriod = this->mPositionUpdatePeriod;
        interface_unlock_peek(this);
        *pMsec = positionUpdatePeriod;
        result = SL_RESULT_SUCCESS;
    }

    SL_LEAVE_INTERFACE
}


static const struct SLPlayItf_ IPlay_Itf = {
    IPlay_SetPlayState,
    IPlay_GetPlayState,
    IPlay_GetDuration,
    IPlay_GetPosition,
    IPlay_RegisterCallback,
    IPlay_SetCallbackEventsMask,
    IPlay_GetCallbackEventsMask,
    IPlay_SetMarkerPosition,
    IPlay_ClearMarkerPosition,
    IPlay_GetMarkerPosition,
    IPlay_SetPositionUpdatePeriod,
    IPlay_GetPositionUpdatePeriod
};

void IPlay_init(void *self)
{
    IPlay *this = (IPlay *) self;
    this->mItf = &IPlay_Itf;
    this->mState = SL_PLAYSTATE_STOPPED;
    this->mDuration = SL_TIME_UNKNOWN;  // will be set by containing player object
    this->mPosition = (SLmillisecond) 0;
    this->mCallback = NULL;
    this->mContext = NULL;
    this->mEventFlags = 0;
    this->mMarkerPosition = 0;
    this->mPositionUpdatePeriod = 1000;
#ifdef USE_OUTPUTMIXEXT
    this->mFrameUpdatePeriod = 0;   // because we don't know the sample rate yet
    this->mLastSeekPosition = 0;
    this->mFramesSinceLastSeek = 0;
    this->mFramesSincePositionUpdate = 0;
#endif
#ifdef LSWTCS
    this->mLastTick = 0;
#endif
}
