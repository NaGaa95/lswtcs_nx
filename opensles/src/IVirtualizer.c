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

/* Virtualizer implementation */

#include "sles_allinclusive.h"

#define VIRTUALIZER_STRENGTH_MIN 0
#define VIRTUALIZER_STRENGTH_MAX 1000


#if defined(ANDROID) && !defined(USE_BACKPORT)
/**
 * returns true if this interface is not associated with an initialized Virtualizer effect
 */
static inline bool NO_VIRTUALIZER(IVirtualizer* v) {
    return (v->mVirtualizerEffect == 0);
}
#endif


static SLresult IVirtualizer_SetEnabled(SLVirtualizerItf self, SLboolean enabled)
{
    SL_ENTER_INTERFACE

    IVirtualizer *this = (IVirtualizer *) self;
    interface_lock_exclusive(this);
    this->mEnabled = (SLboolean) enabled;
#if !defined(ANDROID) || defined(USE_BACKPORT)
    result = SL_RESULT_SUCCESS;
#else
    if (NO_VIRTUALIZER(this)) {
        result = SL_RESULT_CONTROL_LOST;
    } else {
        android::status_t status =
                this->mVirtualizerEffect->setEnabled((bool) this->mEnabled);
        result = android_fx_statusToResult(status);
    }
#endif
    interface_unlock_exclusive(this);

    SL_LEAVE_INTERFACE

}


static SLresult IVirtualizer_IsEnabled(SLVirtualizerItf self, SLboolean *pEnabled)
{
    SL_ENTER_INTERFACE

     if (NULL == pEnabled) {
         result = SL_RESULT_PARAMETER_INVALID;
     } else {
         IVirtualizer *this = (IVirtualizer *) self;
         interface_lock_exclusive(this);
         SLboolean enabled = this->mEnabled;
#if !defined(ANDROID) || defined(USE_BACKPORT)
         *pEnabled = enabled;
         result = SL_RESULT_SUCCESS;
#else
         if (NO_VIRTUALIZER(this)) {
             result = SL_RESULT_CONTROL_LOST;
         } else {
             *pEnabled = (SLboolean) this->mVirtualizerEffect->getEnabled();
             result = SL_RESULT_SUCCESS;
         }
#endif
         interface_unlock_exclusive(this);
     }

     SL_LEAVE_INTERFACE
}


static SLresult IVirtualizer_SetStrength(SLVirtualizerItf self, SLpermille strength)
{
    SL_ENTER_INTERFACE

     if ((VIRTUALIZER_STRENGTH_MIN > strength) || (VIRTUALIZER_STRENGTH_MAX < strength)) {
         result = SL_RESULT_PARAMETER_INVALID;
     } else {
         IVirtualizer *this = (IVirtualizer *) self;
         interface_lock_exclusive(this);
#if !defined(ANDROID) || defined(USE_BACKPORT)
         this->mStrength = strength;
         result = SL_RESULT_SUCCESS;
#else
         if (NO_VIRTUALIZER(this)) {
             result = SL_RESULT_CONTROL_LOST;
         } else {
             android::status_t status = android_virt_setParam(this->mVirtualizerEffect,
                     VIRTUALIZER_PARAM_STRENGTH, &strength);
             result = android_fx_statusToResult(status);
         }
#endif
         interface_unlock_exclusive(this);
     }

     SL_LEAVE_INTERFACE
}


static SLresult IVirtualizer_GetRoundedStrength(SLVirtualizerItf self, SLpermille *pStrength)
{
    SL_ENTER_INTERFACE

    if (NULL == pStrength) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IVirtualizer *this = (IVirtualizer *) self;
        interface_lock_exclusive(this);
        SLpermille strength = this->mStrength;;
#if !defined(ANDROID) || defined(USE_BACKPORT)
        result = SL_RESULT_SUCCESS;
#else
        if (NO_VIRTUALIZER(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status = android_virt_getParam(this->mVirtualizerEffect,
                           VIRTUALIZER_PARAM_STRENGTH, &strength);
            result = android_fx_statusToResult(status);
        }
#endif
        interface_unlock_exclusive(this);
        *pStrength = strength;
    }

    SL_LEAVE_INTERFACE
}


static SLresult IVirtualizer_IsStrengthSupported(SLVirtualizerItf self, SLboolean *pSupported)
{
    SL_ENTER_INTERFACE

    if (NULL == pSupported) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
#if !defined(ANDROID) || defined(USE_BACKPORT)
        *pSupported = SL_BOOLEAN_TRUE;
        result = SL_RESULT_SUCCESS;
#else
        IVirtualizer *this = (IVirtualizer *) self;
        int32_t supported = 0;
        interface_lock_exclusive(this);
        if (NO_VIRTUALIZER(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status =
                android_virt_getParam(this->mVirtualizerEffect,
                        VIRTUALIZER_PARAM_STRENGTH_SUPPORTED, &supported);
            result = android_fx_statusToResult(status);
        }
        interface_unlock_exclusive(this);
        *pSupported = (SLboolean) (supported != 0);
#endif
    }

    SL_LEAVE_INTERFACE
}


static const struct SLVirtualizerItf_ IVirtualizer_Itf = {
    IVirtualizer_SetEnabled,
    IVirtualizer_IsEnabled,
    IVirtualizer_SetStrength,
    IVirtualizer_GetRoundedStrength,
    IVirtualizer_IsStrengthSupported
};

void IVirtualizer_init(void *self)
{
    IVirtualizer *this = (IVirtualizer *) self;
    this->mItf = &IVirtualizer_Itf;
    this->mEnabled = SL_BOOLEAN_FALSE;
    this->mStrength = 0;

#if defined(ANDROID) && !defined(USE_BACKPORT)
    if (!android_fx_initEffectDescriptor(SL_IID_VIRTUALIZER, &this->mVirtualizerDescriptor)) {
        // Virtualizer init failed
        SL_LOGE("Virtualizer initialization failed.");
    }
#endif
}
