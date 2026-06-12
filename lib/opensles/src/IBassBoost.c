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

/* BassBoost implementation */

#include "sles_allinclusive.h"

#define BASSBOOST_STRENGTH_MIN 0
#define BASSBOOST_STRENGTH_MAX 1000


#if defined(ANDROID) && !defined(USE_BACKPORT)
/**
 * returns true if this interface is not associated with an initialized BassBoost effect
 */
static inline bool NO_BASSBOOST(IBassBoost* v) {
    return (v->mBassBoostEffect == 0);
}
#endif


static SLresult IBassBoost_SetEnabled(SLBassBoostItf self, SLboolean enabled)
{
    SL_ENTER_INTERFACE

    IBassBoost *this = (IBassBoost *) self;
    interface_lock_exclusive(this);
    this->mEnabled = (SLboolean) enabled;
#if !defined(ANDROID) || defined(USE_BACKPORT)
    result = SL_RESULT_SUCCESS;
#else
    if (NO_BASSBOOST(this)) {
        result = SL_RESULT_CONTROL_LOST;
    } else {
        android::status_t status = this->mBassBoostEffect->setEnabled((bool) this->mEnabled);
        result = android_fx_statusToResult(status);
    }
#endif
    interface_unlock_exclusive(this);

    SL_LEAVE_INTERFACE
}


static SLresult IBassBoost_IsEnabled(SLBassBoostItf self, SLboolean *pEnabled)
{
    SL_ENTER_INTERFACE

    if (NULL == pEnabled) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IBassBoost *this = (IBassBoost *) self;
        interface_lock_exclusive(this);
        SLboolean enabled = this->mEnabled;
#if !defined(ANDROID) || defined(USE_BACKPORT)
        *pEnabled = enabled;
        result = SL_RESULT_SUCCESS;
#else
        if (NO_BASSBOOST(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            *pEnabled = (SLboolean) this->mBassBoostEffect->getEnabled();
            result = SL_RESULT_SUCCESS;
        }
#endif
        interface_unlock_exclusive(this);
    }

    SL_LEAVE_INTERFACE
}


static SLresult IBassBoost_SetStrength(SLBassBoostItf self, SLpermille strength)
{
    SL_ENTER_INTERFACE

    if ((BASSBOOST_STRENGTH_MIN > strength) || (BASSBOOST_STRENGTH_MAX < strength)) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IBassBoost *this = (IBassBoost *) self;
        interface_lock_exclusive(this);
#if !defined(ANDROID) || defined(USE_BACKPORT)
        this->mStrength = strength;
        result = SL_RESULT_SUCCESS;
#else
        if (NO_BASSBOOST(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status =
                android_bb_setParam(this->mBassBoostEffect, BASSBOOST_PARAM_STRENGTH, &strength);
            result = android_fx_statusToResult(status);
        }
#endif
        interface_unlock_exclusive(this);
    }

    SL_LEAVE_INTERFACE
}


static SLresult IBassBoost_GetRoundedStrength(SLBassBoostItf self, SLpermille *pStrength)
{
    SL_ENTER_INTERFACE

    if (NULL == pStrength) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IBassBoost *this = (IBassBoost *) self;
        interface_lock_exclusive(this);
        SLpermille strength = this->mStrength;;
#if !defined(ANDROID) || defined(USE_BACKPORT)
        result = SL_RESULT_SUCCESS;
#else
        if (NO_BASSBOOST(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status =
                   android_bb_getParam(this->mBassBoostEffect, BASSBOOST_PARAM_STRENGTH, &strength);
            result = android_fx_statusToResult(status);
        }
#endif
        interface_unlock_exclusive(this);
        *pStrength = strength;
    }

    SL_LEAVE_INTERFACE
}


static SLresult IBassBoost_IsStrengthSupported(SLBassBoostItf self, SLboolean *pSupported)
{
    SL_ENTER_INTERFACE

    if (NULL == pSupported) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
#if !defined(ANDROID) || defined(USE_BACKPORT)
        *pSupported = SL_BOOLEAN_TRUE;
        result = SL_RESULT_SUCCESS;
#else
        IBassBoost *this = (IBassBoost *) self;
        int32_t supported = 0;
        interface_lock_exclusive(this);
        if (NO_BASSBOOST(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status =
                android_bb_getParam(this->mBassBoostEffect, BASSBOOST_PARAM_STRENGTH_SUPPORTED,
                        &supported);
            result = android_fx_statusToResult(status);
        }
        interface_unlock_exclusive(this);
        *pSupported = (SLboolean) (supported != 0);
#endif
    }

    SL_LEAVE_INTERFACE
}


static const struct SLBassBoostItf_ IBassBoost_Itf = {
    IBassBoost_SetEnabled,
    IBassBoost_IsEnabled,
    IBassBoost_SetStrength,
    IBassBoost_GetRoundedStrength,
    IBassBoost_IsStrengthSupported
};

void IBassBoost_init(void *self)
{
    IBassBoost *this = (IBassBoost *) self;
    this->mItf = &IBassBoost_Itf;
    this->mEnabled = SL_BOOLEAN_FALSE;
    this->mStrength = 0;

#if defined(ANDROID) && !defined(USE_BACKPORT)
    if (!android_fx_initEffectDescriptor(SL_IID_BASSBOOST, &this->mBassBoostDescriptor)) {
        // BassBoost init failed
        SL_LOGE("BassBoost initialization failed.");
    }
#endif
}
