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

/* PresetReverb implementation */

#include "sles_allinclusive.h"

#if defined(ANDROID) && !defined(USE_BACKPORT)
/**
 * returns true if this interface is not associated with an initialized PresetReverb effect
 */
static inline bool NO_PRESETREVERB(IPresetReverb* ipr) {
    return (ipr->mPresetReverbEffect == 0);
}
#endif

static SLresult IPresetReverb_SetPreset(SLPresetReverbItf self, SLuint16 preset)
{
    SL_ENTER_INTERFACE

    IPresetReverb *this = (IPresetReverb *) self;
    switch (preset) {
    case SL_REVERBPRESET_NONE:
    case SL_REVERBPRESET_SMALLROOM:
    case SL_REVERBPRESET_MEDIUMROOM:
    case SL_REVERBPRESET_LARGEROOM:
    case SL_REVERBPRESET_MEDIUMHALL:
    case SL_REVERBPRESET_LARGEHALL:
    case SL_REVERBPRESET_PLATE:
        interface_lock_poke(this);
        this->mPreset = preset;
#if !defined(ANDROID) || defined(USE_BACKPORT)
        result = SL_RESULT_SUCCESS;
#else
        if (NO_PRESETREVERB(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status = android_prev_setPreset(this->mPresetReverbEffect, preset);
            result = android_fx_statusToResult(status);
        }
#endif
        interface_unlock_poke(this);
        break;
    default:
        result = SL_RESULT_PARAMETER_INVALID;
        break;
    }

    SL_LEAVE_INTERFACE
}

static SLresult IPresetReverb_GetPreset(SLPresetReverbItf self, SLuint16 *pPreset)
{
    SL_ENTER_INTERFACE

    if (NULL == pPreset) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IPresetReverb *this = (IPresetReverb *) self;
        interface_lock_peek(this);
        SLuint16 preset = SL_REVERBPRESET_NONE;
#if !defined(ANDROID) || defined(USE_BACKPORT)
        preset = this->mPreset;
        result = SL_RESULT_SUCCESS;
#else
        if (NO_PRESETREVERB(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status = android_prev_getPreset(this->mPresetReverbEffect, &preset);
            result = android_fx_statusToResult(status);
        }
#endif
        interface_unlock_peek(this);
        *pPreset = preset;
    }

    SL_LEAVE_INTERFACE
}

static const struct SLPresetReverbItf_ IPresetReverb_Itf = {
    IPresetReverb_SetPreset,
    IPresetReverb_GetPreset
};

void IPresetReverb_init(void *self)
{
    IPresetReverb *this = (IPresetReverb *) self;
    this->mItf = &IPresetReverb_Itf;
    this->mPreset = SL_REVERBPRESET_NONE;

#if defined(ANDROID) && !defined(USE_BACKPORT)
    if (!android_fx_initEffectDescriptor(SL_IID_PRESETREVERB, &this->mPresetReverbDescriptor)) {
        // PresetReverb init failed
        SL_LOGE("PresetReverb initialization failed.");
    }
#endif
}
