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

/* MuteSolo implementation */

#include "sles_allinclusive.h"


static SLresult IMuteSolo_SetChannelMute(SLMuteSoloItf self, SLuint8 chan, SLboolean mute)
{
    SL_ENTER_INTERFACE

    IMuteSolo *this = (IMuteSolo *) self;
    IObject *thisObject = this->mThis;
    if (SL_OBJECTID_AUDIOPLAYER != IObjectToObjectID(thisObject)) {
        result = SL_RESULT_FEATURE_UNSUPPORTED;
    } else {
        CAudioPlayer *ap = (CAudioPlayer *) thisObject;
        interface_lock_exclusive(this);
        SLuint8 numChannels = ap->mNumChannels;
        if (1 >= numChannels) {
            interface_unlock_exclusive(this);
            result = SL_RESULT_FEATURE_UNSUPPORTED;
        } else if (numChannels <= chan) {
            interface_unlock_exclusive(this);
            result = SL_RESULT_PARAMETER_INVALID;
        } else {
            SLuint8 mask = 1 << chan;
            SLuint8 oldMuteMask = ap->mMuteMask;
            if (mute) {
                ap->mMuteMask |= mask;
            } else {
                ap->mMuteMask &= ~mask;
            }
            interface_unlock_exclusive_attributes(this, oldMuteMask != ap->mMuteMask ? ATTR_GAIN :
                ATTR_NONE);
            result = SL_RESULT_SUCCESS;
        }
    }

    SL_LEAVE_INTERFACE
}


static SLresult IMuteSolo_GetChannelMute(SLMuteSoloItf self, SLuint8 chan, SLboolean *pMute)
{
    SL_ENTER_INTERFACE

    if (NULL == pMute) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IMuteSolo *this = (IMuteSolo *) self;
        IObject *thisObject = this->mThis;
        if (SL_OBJECTID_AUDIOPLAYER != IObjectToObjectID(thisObject)) {
            result = SL_RESULT_FEATURE_UNSUPPORTED;
        } else {
            CAudioPlayer *ap = (CAudioPlayer *) thisObject;
            SLboolean mute;
            interface_lock_shared(this);
            SLuint8 numChannels = ap->mNumChannels;
            if (1 >= numChannels) {
                mute = SL_BOOLEAN_FALSE;
                result = SL_RESULT_FEATURE_UNSUPPORTED;
            } else if (numChannels <= chan) {
                mute = SL_BOOLEAN_FALSE;
                result = SL_RESULT_PARAMETER_INVALID;
            } else {
                SLuint8 mask = ap->mMuteMask;
                mute = (SLboolean) ((mask >> chan) & 1);
                result = SL_RESULT_SUCCESS;
            }
            interface_unlock_shared(this);
            *pMute = mute;
        }
    }

    SL_LEAVE_INTERFACE
}


static SLresult IMuteSolo_SetChannelSolo(SLMuteSoloItf self, SLuint8 chan, SLboolean solo)
{
    SL_ENTER_INTERFACE

    IMuteSolo *this = (IMuteSolo *) self;
    IObject *thisObject = this->mThis;
    if (SL_OBJECTID_AUDIOPLAYER != IObjectToObjectID(thisObject)) {
        result = SL_RESULT_FEATURE_UNSUPPORTED;
    } else {
        CAudioPlayer *ap = (CAudioPlayer *) thisObject;
        interface_lock_exclusive(this);
        SLuint8 numChannels = ap->mNumChannels;
        if (1 >= numChannels) {
            interface_unlock_exclusive(this);
            result = SL_RESULT_FEATURE_UNSUPPORTED;
        } else if (numChannels <= chan) {
            interface_unlock_exclusive(this);
            result = SL_RESULT_PARAMETER_INVALID;
        } else {
            SLuint8 mask = 1 << chan;
            SLuint8 oldSoloMask = ap->mSoloMask;
            if (solo) {
                ap->mSoloMask |= mask;
            } else {
                ap->mSoloMask &= ~mask;
            }
            interface_unlock_exclusive_attributes(this, oldSoloMask != ap->mSoloMask ? ATTR_GAIN :
                ATTR_NONE);
            result = SL_RESULT_SUCCESS;
        }
    }

    SL_LEAVE_INTERFACE
}


static SLresult IMuteSolo_GetChannelSolo(SLMuteSoloItf self, SLuint8 chan, SLboolean *pSolo)
{
    SL_ENTER_INTERFACE

    if (NULL == pSolo) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IMuteSolo *this = (IMuteSolo *) self;
        IObject *thisObject = this->mThis;
        if (SL_OBJECTID_AUDIOPLAYER != IObjectToObjectID(thisObject)) {
            result = SL_RESULT_FEATURE_UNSUPPORTED;
        } else {
            CAudioPlayer *ap = (CAudioPlayer *) thisObject;
            SLboolean solo;
            interface_lock_shared(this);
            SLuint8 numChannels = ap->mNumChannels;
            if (1 >= numChannels) {
                solo = SL_BOOLEAN_FALSE;
                result = SL_RESULT_FEATURE_UNSUPPORTED;
            } else if (numChannels <= chan) {
                solo = SL_BOOLEAN_FALSE;
                result = SL_RESULT_PARAMETER_INVALID;
            } else {
                SLuint8 mask = ap->mSoloMask;
                solo = (SLboolean) ((mask >> chan) & 1);
                result = SL_RESULT_SUCCESS;
            }
            interface_unlock_shared(this);
            *pSolo = solo;
        }
    }

    SL_LEAVE_INTERFACE
}


static SLresult IMuteSolo_GetNumChannels(SLMuteSoloItf self, SLuint8 *pNumChannels)
{
    SL_ENTER_INTERFACE

    if (NULL == pNumChannels) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IMuteSolo *this = (IMuteSolo *) self;
        IObject *thisObject = this->mThis;
        if (SL_OBJECTID_AUDIOPLAYER != IObjectToObjectID(thisObject)) {
            result = SL_RESULT_FEATURE_UNSUPPORTED;
        } else {
            CAudioPlayer *ap = (CAudioPlayer *) thisObject;
            object_lock_shared(thisObject);
            SLuint8 numChannels = ap->mNumChannels;
            object_unlock_shared(thisObject);
            *pNumChannels = numChannels;
            result = 0 < numChannels ? SL_RESULT_SUCCESS : SL_RESULT_PRECONDITIONS_VIOLATED;
        }
    }

    SL_LEAVE_INTERFACE
}


static const struct SLMuteSoloItf_ IMuteSolo_Itf = {
    IMuteSolo_SetChannelMute,
    IMuteSolo_GetChannelMute,
    IMuteSolo_SetChannelSolo,
    IMuteSolo_GetChannelSolo,
    IMuteSolo_GetNumChannels
};

void IMuteSolo_init(void *self)
{
    IMuteSolo *this = (IMuteSolo *) self;
    this->mItf = &IMuteSolo_Itf;
}
