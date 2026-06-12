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

/* Record implementation */

#include "sles_allinclusive.h"


static SLresult IRecord_SetRecordState(SLRecordItf self, SLuint32 state)
{
    SL_ENTER_INTERFACE

    switch (state) {
    case SL_RECORDSTATE_STOPPED:
    case SL_RECORDSTATE_PAUSED:
    case SL_RECORDSTATE_RECORDING:
        {
        IRecord *this = (IRecord *) self;
        interface_lock_poke(this);
        this->mState = state;
#ifdef ANDROID
        android_audioRecorder_setRecordState(InterfaceToCAudioRecorder(this), state);
#endif
        interface_unlock_poke(this);
        result = SL_RESULT_SUCCESS;
        }
        break;
    default:
        result = SL_RESULT_PARAMETER_INVALID;
        break;
    }

    SL_LEAVE_INTERFACE
}


static SLresult IRecord_GetRecordState(SLRecordItf self, SLuint32 *pState)
{
    SL_ENTER_INTERFACE

    IRecord *this = (IRecord *) self;
    if (NULL == pState) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        interface_lock_peek(this);
        SLuint32 state = this->mState;
        interface_unlock_peek(this);
        *pState = state;
        result = SL_RESULT_SUCCESS;
    }

    SL_LEAVE_INTERFACE
}


static SLresult IRecord_SetDurationLimit(SLRecordItf self, SLmillisecond msec)
{
    SL_ENTER_INTERFACE

    IRecord *this = (IRecord *) self;
    interface_lock_exclusive(this);
    if (this->mDurationLimit != msec) {
        this->mDurationLimit = msec;
        interface_unlock_exclusive_attributes(this, ATTR_TRANSPORT);
    } else {
        interface_unlock_exclusive(this);
    }
    result = SL_RESULT_SUCCESS;

    SL_LEAVE_INTERFACE
}


static SLresult IRecord_GetPosition(SLRecordItf self, SLmillisecond *pMsec)
{
    SL_ENTER_INTERFACE

    if (NULL == pMsec) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IRecord *this = (IRecord *) self;
        SLmillisecond position;
        interface_lock_shared(this);
#ifdef ANDROID
        // Android does not use the mPosition field for audio recorders
        if (SL_OBJECTID_AUDIORECORDER == InterfaceToObjectID(this)) {
            android_audioRecorder_getPosition(InterfaceToCAudioRecorder(this), &position);
        } else {
            position = this->mPosition;
        }
#else
        position = this->mPosition;
#endif
        interface_unlock_shared(this);
        *pMsec = position;
        result = SL_RESULT_SUCCESS;
    }

    SL_LEAVE_INTERFACE
}


static SLresult IRecord_RegisterCallback(SLRecordItf self, slRecordCallback callback,
    void *pContext)
{
    SL_ENTER_INTERFACE

    IRecord *this = (IRecord *) self;
    interface_lock_exclusive(this);
    this->mCallback = callback;
    this->mContext = pContext;
    interface_unlock_exclusive(this);
    result = SL_RESULT_SUCCESS;

    SL_LEAVE_INTERFACE
}


static SLresult IRecord_SetCallbackEventsMask(SLRecordItf self, SLuint32 eventFlags)
{
    SL_ENTER_INTERFACE

    if (eventFlags & ~(
        SL_RECORDEVENT_HEADATLIMIT  |
        SL_RECORDEVENT_HEADATMARKER |
        SL_RECORDEVENT_HEADATNEWPOS |
        SL_RECORDEVENT_HEADMOVING   |
        SL_RECORDEVENT_HEADSTALLED  |
        SL_RECORDEVENT_BUFFER_FULL)) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IRecord *this = (IRecord *) self;
        interface_lock_exclusive(this);
        if (this->mCallbackEventsMask != eventFlags) {
            this->mCallbackEventsMask = eventFlags;
            interface_unlock_exclusive_attributes(this, ATTR_TRANSPORT);
        } else {
            interface_unlock_exclusive(this);
        }
        result = SL_RESULT_SUCCESS;
    }

    SL_LEAVE_INTERFACE
}


static SLresult IRecord_GetCallbackEventsMask(SLRecordItf self, SLuint32 *pEventFlags)
{
    SL_ENTER_INTERFACE

    if (NULL == pEventFlags) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IRecord *this = (IRecord *) self;
        interface_lock_peek(this);
        SLuint32 callbackEventsMask = this->mCallbackEventsMask;
        interface_unlock_peek(this);
        *pEventFlags = callbackEventsMask;
        result = SL_RESULT_SUCCESS;
    }

    SL_LEAVE_INTERFACE
}


static SLresult IRecord_SetMarkerPosition(SLRecordItf self, SLmillisecond mSec)
{
    SL_ENTER_INTERFACE

    IRecord *this = (IRecord *) self;
    interface_lock_exclusive(this);
    if (this->mMarkerPosition != mSec) {
        this->mMarkerPosition = mSec;
        interface_unlock_exclusive_attributes(this, ATTR_TRANSPORT);
    } else {
        interface_unlock_exclusive(this);
    }
    result = SL_RESULT_SUCCESS;

    SL_LEAVE_INTERFACE
}


static SLresult IRecord_ClearMarkerPosition(SLRecordItf self)
{
    SL_ENTER_INTERFACE

    IRecord *this = (IRecord *) self;
    interface_lock_exclusive(this);
    if (this->mMarkerPosition != 0) {
        this->mMarkerPosition = 0;
        interface_unlock_exclusive_attributes(this, ATTR_TRANSPORT);
    } else {
        interface_unlock_exclusive(this);
    }
    result = SL_RESULT_SUCCESS;

    SL_LEAVE_INTERFACE
}


static SLresult IRecord_GetMarkerPosition(SLRecordItf self, SLmillisecond *pMsec)
{
    SL_ENTER_INTERFACE

    if (NULL == pMsec) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IRecord *this = (IRecord *) self;
        interface_lock_peek(this);
        SLmillisecond markerPosition = this->mMarkerPosition;
        interface_unlock_peek(this);
        *pMsec = markerPosition;
        result = SL_RESULT_SUCCESS;
    }

    SL_LEAVE_INTERFACE
}


static SLresult IRecord_SetPositionUpdatePeriod(SLRecordItf self, SLmillisecond mSec)
{
    SL_ENTER_INTERFACE

    if (0 == mSec) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IRecord *this = (IRecord *) self;
        interface_lock_exclusive(this);
        if (this->mPositionUpdatePeriod != mSec) {
            this->mPositionUpdatePeriod = mSec;
            interface_unlock_exclusive_attributes(this, ATTR_TRANSPORT);
        } else {
            interface_unlock_exclusive(this);
        }
        result = SL_RESULT_SUCCESS;
    }

    SL_LEAVE_INTERFACE
}


static SLresult IRecord_GetPositionUpdatePeriod(SLRecordItf self, SLmillisecond *pMsec)
{
    SL_ENTER_INTERFACE

    if (NULL == pMsec) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IRecord *this = (IRecord *) self;
        interface_lock_peek(this);
        SLmillisecond positionUpdatePeriod = this->mPositionUpdatePeriod;
        interface_unlock_peek(this);
        *pMsec = positionUpdatePeriod;
        result = SL_RESULT_SUCCESS;
    }

    SL_LEAVE_INTERFACE
}


static const struct SLRecordItf_ IRecord_Itf = {
    IRecord_SetRecordState,
    IRecord_GetRecordState,
    IRecord_SetDurationLimit,
    IRecord_GetPosition,
    IRecord_RegisterCallback,
    IRecord_SetCallbackEventsMask,
    IRecord_GetCallbackEventsMask,
    IRecord_SetMarkerPosition,
    IRecord_ClearMarkerPosition,
    IRecord_GetMarkerPosition,
    IRecord_SetPositionUpdatePeriod,
    IRecord_GetPositionUpdatePeriod
};

void IRecord_init(void *self)
{
    //SL_LOGV("IRecord_init(%p) entering", self);
    IRecord *this = (IRecord *) self;
    this->mItf = &IRecord_Itf;
    this->mState = SL_RECORDSTATE_STOPPED;
    this->mDurationLimit = 0;
    this->mPosition = 0;
    this->mCallback = NULL;
    this->mContext = NULL;
    this->mCallbackEventsMask = 0;
    this->mMarkerPosition = 0;
    this->mPositionUpdatePeriod = 1000;

}
