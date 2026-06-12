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

/* PlaybackRate implementation */

#include "sles_allinclusive.h"


static SLresult IPlaybackRate_SetRate(SLPlaybackRateItf self, SLpermille rate)
{
    SL_ENTER_INTERFACE

    IPlaybackRate *this = (IPlaybackRate *) self;
    // const, so no lock needed
    if (!(this->mMinRate <= rate && rate <= this->mMaxRate)) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        interface_lock_exclusive(this);
        this->mRate = rate;
        interface_unlock_exclusive(this);
#ifdef ANDROID
        CAudioPlayer *ap = (SL_OBJECTID_AUDIOPLAYER == InterfaceToObjectID(this)) ?
                (CAudioPlayer *) this->mThis : NULL;
        if (NULL != ap) {
            result = android_audioPlayer_setPlayRate(ap, rate, true);
        } else {
            result = SL_RESULT_PARAMETER_INVALID;
        }
#else
        result = SL_RESULT_SUCCESS;
#endif
    }

    SL_LEAVE_INTERFACE
}


static SLresult IPlaybackRate_GetRate(SLPlaybackRateItf self, SLpermille *pRate)
{
    SL_ENTER_INTERFACE

    if (NULL == pRate) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IPlaybackRate *this = (IPlaybackRate *) self;
        interface_lock_peek(this);
        SLpermille rate = this->mRate;
        interface_unlock_peek(this);
        *pRate = rate;
        result = SL_RESULT_SUCCESS;
    }

    SL_LEAVE_INTERFACE
}


static SLresult IPlaybackRate_SetPropertyConstraints(SLPlaybackRateItf self, SLuint32 constraints)
{
    SL_ENTER_INTERFACE

    IPlaybackRate *this = (IPlaybackRate *) self;
    this->mProperties = constraints;
#ifdef ANDROID
    // verify property support before storing
    CAudioPlayer *ap = (SL_OBJECTID_AUDIOPLAYER == InterfaceToObjectID(this)) ?
            (CAudioPlayer *) this->mThis : NULL;
    if (NULL != ap) {
        result = android_audioPlayer_setPlaybackRateBehavior(ap, constraints);
    } else {
        result = SL_RESULT_PARAMETER_INVALID;
    }

#else
    result = SL_RESULT_SUCCESS;
#endif
    interface_lock_poke(this);
    if (result == SL_RESULT_SUCCESS) {
        this->mProperties = constraints;
    }
    interface_unlock_poke(this);

    SL_LEAVE_INTERFACE
}


static SLresult IPlaybackRate_GetProperties(SLPlaybackRateItf self, SLuint32 *pProperties)
{
    SL_ENTER_INTERFACE

    if (NULL == pProperties) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IPlaybackRate *this = (IPlaybackRate *) self;
        interface_lock_peek(this);
        SLuint32 properties = this->mProperties;
        interface_unlock_peek(this);
        *pProperties = properties;
        result = SL_RESULT_SUCCESS;
    }

    SL_LEAVE_INTERFACE
}


static SLresult IPlaybackRate_GetCapabilitiesOfRate(SLPlaybackRateItf self,
    SLpermille rate, SLuint32 *pCapabilities)
{
    SL_ENTER_INTERFACE

    if (NULL == pCapabilities) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IPlaybackRate *this = (IPlaybackRate *) self;
        // const, so no lock needed
        if (!(this->mMinRate <= rate && rate <= this->mMaxRate)) {
            result = SL_RESULT_PARAMETER_INVALID;
        } else {
            SLuint32 capabilities = 0;
#ifdef ANDROID
            CAudioPlayer *ap = (SL_OBJECTID_AUDIOPLAYER == InterfaceToObjectID(this)) ?
                    (CAudioPlayer *) this->mThis : NULL;
            if (NULL != ap) {
                android_audioPlayer_getCapabilitiesOfRate(ap, &capabilities);
            }
#else
            capabilities = this->mCapabilities;
#endif
            *pCapabilities = capabilities;
            result = SL_RESULT_SUCCESS;
        }
    }

    SL_LEAVE_INTERFACE
}


static SLresult IPlaybackRate_GetRateRange(SLPlaybackRateItf self, SLuint8 index,
    SLpermille *pMinRate, SLpermille *pMaxRate, SLpermille *pStepSize, SLuint32 *pCapabilities)
{
    SL_ENTER_INTERFACE

    // only one range
    if (NULL == pMinRate || NULL == pMaxRate || NULL == pStepSize || NULL == pCapabilities ||
        (0 < index)) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IPlaybackRate *this = (IPlaybackRate *) self;
        interface_lock_shared(this);
        SLpermille minRate = this->mMinRate;
        SLpermille maxRate = this->mMaxRate;
        SLpermille stepSize = this->mStepSize;
        SLuint32 capabilities = this->mCapabilities;
        interface_unlock_shared(this);
        *pMinRate = minRate;
        *pMaxRate = maxRate;
        *pStepSize = stepSize;
        *pCapabilities = capabilities;
        result = SL_RESULT_SUCCESS;
    }

    SL_LEAVE_INTERFACE
}


static const struct SLPlaybackRateItf_ IPlaybackRate_Itf = {
    IPlaybackRate_SetRate,
    IPlaybackRate_GetRate,
    IPlaybackRate_SetPropertyConstraints,
    IPlaybackRate_GetProperties,
    IPlaybackRate_GetCapabilitiesOfRate,
    IPlaybackRate_GetRateRange
};

void IPlaybackRate_init(void *self)
{
    //SL_LOGV("IPlaybackRate_init called");
    IPlaybackRate *this = (IPlaybackRate *) self;
    this->mItf = &IPlaybackRate_Itf;
    this->mProperties = SL_RATEPROP_NOPITCHCORAUDIO;
    this->mRate = 1000;
    // const
    this->mMinRate = 500;
    this->mMaxRate = 2000;
    this->mStepSize = 0;
#ifdef ANDROID
    this->mStepSize = 0;
    // for an AudioPlayer, mCapabilities will be initialized in sles_to_android_audioPlayerCreate
#endif
    // The generic implementation sets no capabilities because the generic
    // implementation alone doesn't support any.
    this->mCapabilities = 0;
    // SL_RATEPROP_SILENTAUDIO | SL_RATEPROP_STAGGEREDAUDIO | SL_RATEPROP_NOPITCHCORAUDIO |
    // SL_RATEPROP_PITCHCORAUDIO
}
