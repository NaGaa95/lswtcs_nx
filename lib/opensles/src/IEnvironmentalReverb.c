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

/* EnvironmentalReverb implementation */

#include "sles_allinclusive.h"

// Note: all Set operations use exclusive not poke,
// because SetEnvironmentalReverbProperties is exclusive.
// It is safe for the Get operations to use peek,
// on the assumption that the block copy will atomically
// replace each word of the block.


#if defined(ANDROID) && !defined(USE_BACKPORT)
/**
 * returns true if this interface is not associated with an initialized EnvironmentalReverb effect
 */
static inline bool NO_ENVREVERB(IEnvironmentalReverb* ier) {
    return (ier->mEnvironmentalReverbEffect == 0);
}
#endif


static SLresult IEnvironmentalReverb_SetRoomLevel(SLEnvironmentalReverbItf self, SLmillibel room)
{
    SL_ENTER_INTERFACE

    if (!(SL_MILLIBEL_MIN <= room && room <= 0)) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IEnvironmentalReverb *this = (IEnvironmentalReverb *) self;
        interface_lock_exclusive(this);
        this->mProperties.roomLevel = room;
#if !defined(ANDROID) || defined(USE_BACKPORT)
        result = SL_RESULT_SUCCESS;
#else
        if (NO_ENVREVERB(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status = android_erev_setParam(this->mEnvironmentalReverbEffect,
                    REVERB_PARAM_ROOM_LEVEL, &room);
            result = android_fx_statusToResult(status);
        }
#endif
        interface_unlock_exclusive(this);
    }

    SL_LEAVE_INTERFACE
}


static SLresult IEnvironmentalReverb_GetRoomLevel(SLEnvironmentalReverbItf self, SLmillibel *pRoom)
{
    SL_ENTER_INTERFACE

    if (NULL == pRoom) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IEnvironmentalReverb *this = (IEnvironmentalReverb *) self;
        interface_lock_peek(this);
#if !defined(ANDROID) || defined(USE_BACKPORT)
        result = SL_RESULT_SUCCESS;
#else
        if (NO_ENVREVERB(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status = android_erev_getParam(this->mEnvironmentalReverbEffect,
                    REVERB_PARAM_ROOM_LEVEL, &this->mProperties.roomLevel);
            result = android_fx_statusToResult(status);
        }
#endif
        *pRoom = this->mProperties.roomLevel;

        interface_unlock_peek(this);

    }

    SL_LEAVE_INTERFACE
}


static SLresult IEnvironmentalReverb_SetRoomHFLevel(
    SLEnvironmentalReverbItf self, SLmillibel roomHF)
{
    SL_ENTER_INTERFACE

    if (!(SL_MILLIBEL_MIN <= roomHF && roomHF <= 0)) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IEnvironmentalReverb *this = (IEnvironmentalReverb *) self;
        interface_lock_exclusive(this);
        this->mProperties.roomHFLevel = roomHF;
#if !defined(ANDROID) || defined(USE_BACKPORT)
        result = SL_RESULT_SUCCESS;
#else
        if (NO_ENVREVERB(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status = android_erev_setParam(this->mEnvironmentalReverbEffect,
                    REVERB_PARAM_ROOM_HF_LEVEL, &roomHF);
            result = android_fx_statusToResult(status);
        }
#endif
        interface_unlock_exclusive(this);
    }

    SL_LEAVE_INTERFACE
}


static SLresult IEnvironmentalReverb_GetRoomHFLevel(
    SLEnvironmentalReverbItf self, SLmillibel *pRoomHF)
{
    SL_ENTER_INTERFACE

    if (NULL == pRoomHF) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IEnvironmentalReverb *this = (IEnvironmentalReverb *) self;
        interface_lock_peek(this);
#if !defined(ANDROID) || defined(USE_BACKPORT)
        result = SL_RESULT_SUCCESS;
#else
        if (NO_ENVREVERB(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status = android_erev_getParam(this->mEnvironmentalReverbEffect,
                    REVERB_PARAM_ROOM_HF_LEVEL, &this->mProperties.roomHFLevel);
            result = android_fx_statusToResult(status);
        }
#endif
        *pRoomHF = this->mProperties.roomHFLevel;

        interface_unlock_peek(this);
    }

    SL_LEAVE_INTERFACE
}


static SLresult IEnvironmentalReverb_SetDecayTime(
    SLEnvironmentalReverbItf self, SLmillisecond decayTime)
{
    SL_ENTER_INTERFACE

    if (!(100 <= decayTime && decayTime <= 20000)) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IEnvironmentalReverb *this = (IEnvironmentalReverb *) self;
        interface_lock_exclusive(this);
        this->mProperties.decayTime = decayTime;
#if !defined(ANDROID) || defined(USE_BACKPORT)
        result = SL_RESULT_SUCCESS;
#else
        if (NO_ENVREVERB(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status = android_erev_setParam(this->mEnvironmentalReverbEffect,
                    REVERB_PARAM_DECAY_TIME, &decayTime);
            result = android_fx_statusToResult(status);
        }
#endif
        interface_unlock_exclusive(this);
    }

    SL_LEAVE_INTERFACE
}


static SLresult IEnvironmentalReverb_GetDecayTime(
    SLEnvironmentalReverbItf self, SLmillisecond *pDecayTime)
{
    SL_ENTER_INTERFACE

    if (NULL == pDecayTime) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IEnvironmentalReverb *this = (IEnvironmentalReverb *) self;
        interface_lock_peek(this);
#if !defined(ANDROID) || defined(USE_BACKPORT)
        result = SL_RESULT_SUCCESS;
#else
        if (NO_ENVREVERB(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status = android_erev_getParam(this->mEnvironmentalReverbEffect,
                    REVERB_PARAM_DECAY_TIME, &this->mProperties.decayTime);
            result = android_fx_statusToResult(status);
        }
#endif
        *pDecayTime = this->mProperties.decayTime;

        interface_unlock_peek(this);
    }

    SL_LEAVE_INTERFACE
}


static SLresult IEnvironmentalReverb_SetDecayHFRatio(
    SLEnvironmentalReverbItf self, SLpermille decayHFRatio)
{
    SL_ENTER_INTERFACE

    if (!(100 <= decayHFRatio && decayHFRatio <= 2000)) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IEnvironmentalReverb *this = (IEnvironmentalReverb *) self;
        interface_lock_exclusive(this);
        this->mProperties.decayHFRatio = decayHFRatio;
#if !defined(ANDROID) || defined(USE_BACKPORT)
        result = SL_RESULT_SUCCESS;
#else
        if (NO_ENVREVERB(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status = android_erev_setParam(this->mEnvironmentalReverbEffect,
                    REVERB_PARAM_DECAY_HF_RATIO, &decayHFRatio);
            result = android_fx_statusToResult(status);
        }
#endif
        interface_unlock_exclusive(this);
    }

    SL_LEAVE_INTERFACE
}


static SLresult IEnvironmentalReverb_GetDecayHFRatio(
    SLEnvironmentalReverbItf self, SLpermille *pDecayHFRatio)
{
    SL_ENTER_INTERFACE

    if (NULL == pDecayHFRatio) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IEnvironmentalReverb *this = (IEnvironmentalReverb *) self;
        interface_lock_peek(this);
#if !defined(ANDROID) || defined(USE_BACKPORT)
        result = SL_RESULT_SUCCESS;
#else
        if (NO_ENVREVERB(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status = android_erev_getParam(this->mEnvironmentalReverbEffect,
                    REVERB_PARAM_DECAY_HF_RATIO, &this->mProperties.decayHFRatio);
            result = android_fx_statusToResult(status);
        }
#endif
        *pDecayHFRatio = this->mProperties.decayHFRatio;

        interface_unlock_peek(this);
    }

    SL_LEAVE_INTERFACE
}


static SLresult IEnvironmentalReverb_SetReflectionsLevel(
    SLEnvironmentalReverbItf self, SLmillibel reflectionsLevel)
{
    SL_ENTER_INTERFACE

    if (!(SL_MILLIBEL_MIN <= reflectionsLevel && reflectionsLevel <= 1000)) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IEnvironmentalReverb *this = (IEnvironmentalReverb *) self;
        interface_lock_exclusive(this);
        this->mProperties.reflectionsLevel = reflectionsLevel;
#if !defined(ANDROID) || defined(USE_BACKPORT)
        result = SL_RESULT_SUCCESS;
#else
        if (NO_ENVREVERB(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status = android_erev_setParam(this->mEnvironmentalReverbEffect,
                    REVERB_PARAM_REFLECTIONS_LEVEL, &reflectionsLevel);
            result = android_fx_statusToResult(status);
        }
#endif
        interface_unlock_exclusive(this);
    }

    SL_LEAVE_INTERFACE
}


static SLresult IEnvironmentalReverb_GetReflectionsLevel(
    SLEnvironmentalReverbItf self, SLmillibel *pReflectionsLevel)
{
    SL_ENTER_INTERFACE

    if (NULL == pReflectionsLevel) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IEnvironmentalReverb *this = (IEnvironmentalReverb *) self;
        interface_lock_peek(this);
#if !defined(ANDROID) || defined(USE_BACKPORT)
        result = SL_RESULT_SUCCESS;
#else
        if (NO_ENVREVERB(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status = android_erev_getParam(this->mEnvironmentalReverbEffect,
                    REVERB_PARAM_REFLECTIONS_LEVEL, &this->mProperties.reflectionsLevel);
            result = android_fx_statusToResult(status);
        }
#endif
        *pReflectionsLevel = this->mProperties.reflectionsLevel;

        interface_unlock_peek(this);
    }

    SL_LEAVE_INTERFACE
}


static SLresult IEnvironmentalReverb_SetReflectionsDelay(
    SLEnvironmentalReverbItf self, SLmillisecond reflectionsDelay)
{
    SL_ENTER_INTERFACE

    if (!(/* 0 <= reflectionsDelay && */ reflectionsDelay <= 300)) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IEnvironmentalReverb *this = (IEnvironmentalReverb *) self;
        interface_lock_exclusive(this);
        this->mProperties.reflectionsDelay = reflectionsDelay;
#if !defined(ANDROID) || defined(USE_BACKPORT)
        result = SL_RESULT_SUCCESS;
#else
        if (NO_ENVREVERB(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status = android_erev_setParam(this->mEnvironmentalReverbEffect,
                    REVERB_PARAM_REFLECTIONS_DELAY, &reflectionsDelay);
            result = android_fx_statusToResult(status);
        }
#endif
        interface_unlock_exclusive(this);
    }

    SL_LEAVE_INTERFACE
}


static SLresult IEnvironmentalReverb_GetReflectionsDelay(
    SLEnvironmentalReverbItf self, SLmillisecond *pReflectionsDelay)
{
    SL_ENTER_INTERFACE

    if (NULL == pReflectionsDelay) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IEnvironmentalReverb *this = (IEnvironmentalReverb *) self;
        interface_lock_peek(this);
#if !defined(ANDROID) || defined(USE_BACKPORT)
        result = SL_RESULT_SUCCESS;
#else
        if (NO_ENVREVERB(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status = android_erev_getParam(this->mEnvironmentalReverbEffect,
                    REVERB_PARAM_REFLECTIONS_DELAY, &this->mProperties.reflectionsDelay);
            result = android_fx_statusToResult(status);
        }
#endif
        *pReflectionsDelay = this->mProperties.reflectionsDelay;

        interface_unlock_peek(this);
    }

    SL_LEAVE_INTERFACE
}


static SLresult IEnvironmentalReverb_SetReverbLevel(
    SLEnvironmentalReverbItf self, SLmillibel reverbLevel)
{
    SL_ENTER_INTERFACE

    if (!(SL_MILLIBEL_MIN <= reverbLevel && reverbLevel <= 2000)) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IEnvironmentalReverb *this = (IEnvironmentalReverb *) self;
        interface_lock_exclusive(this);
        this->mProperties.reverbLevel = reverbLevel;
#if !defined(ANDROID) || defined(USE_BACKPORT)
        result = SL_RESULT_SUCCESS;
#else
        if (NO_ENVREVERB(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status = android_erev_setParam(this->mEnvironmentalReverbEffect,
                    REVERB_PARAM_REVERB_LEVEL, &reverbLevel);
            result = android_fx_statusToResult(status);
        }
#endif
        interface_unlock_exclusive(this);
    }

    SL_LEAVE_INTERFACE
}


static SLresult IEnvironmentalReverb_GetReverbLevel(
    SLEnvironmentalReverbItf self, SLmillibel *pReverbLevel)
{
    SL_ENTER_INTERFACE

    if (NULL == pReverbLevel) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IEnvironmentalReverb *this = (IEnvironmentalReverb *) self;
        interface_lock_peek(this);
#if !defined(ANDROID) || defined(USE_BACKPORT)
        result = SL_RESULT_SUCCESS;
#else
        if (NO_ENVREVERB(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status = android_erev_getParam(this->mEnvironmentalReverbEffect,
                    REVERB_PARAM_REVERB_LEVEL, &this->mProperties.reverbLevel);
            result = android_fx_statusToResult(status);
        }
#endif
        *pReverbLevel = this->mProperties.reverbLevel;

        interface_unlock_peek(this);
    }

    SL_LEAVE_INTERFACE
}


static SLresult IEnvironmentalReverb_SetReverbDelay(
    SLEnvironmentalReverbItf self, SLmillisecond reverbDelay)
{
    SL_ENTER_INTERFACE

    if (!(/* 0 <= reverbDelay && */ reverbDelay <= 100)) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IEnvironmentalReverb *this = (IEnvironmentalReverb *) self;
        interface_lock_exclusive(this);
        this->mProperties.reverbDelay = reverbDelay;
#if !defined(ANDROID) || defined(USE_BACKPORT)
        result = SL_RESULT_SUCCESS;
#else
        if (NO_ENVREVERB(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status = android_erev_setParam(this->mEnvironmentalReverbEffect,
                    REVERB_PARAM_REVERB_DELAY, &reverbDelay);
            result = android_fx_statusToResult(status);
        }
#endif
        interface_unlock_exclusive(this);
    }

    SL_LEAVE_INTERFACE
}


static SLresult IEnvironmentalReverb_GetReverbDelay(
    SLEnvironmentalReverbItf self, SLmillisecond *pReverbDelay)
{
    SL_ENTER_INTERFACE

    if (NULL == pReverbDelay) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IEnvironmentalReverb *this = (IEnvironmentalReverb *) self;
        interface_lock_peek(this);
#if !defined(ANDROID) || defined(USE_BACKPORT)
        result = SL_RESULT_SUCCESS;
#else
        if (NO_ENVREVERB(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status = android_erev_getParam(this->mEnvironmentalReverbEffect,
                    REVERB_PARAM_REVERB_DELAY, &this->mProperties.reverbDelay);
            result = android_fx_statusToResult(status);
        }
#endif
        *pReverbDelay = this->mProperties.reverbDelay;

        interface_unlock_peek(this);
    }

    SL_LEAVE_INTERFACE
}


static SLresult IEnvironmentalReverb_SetDiffusion(
    SLEnvironmentalReverbItf self, SLpermille diffusion)
{
    SL_ENTER_INTERFACE

    if (!(0 <= diffusion && diffusion <= 1000)) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IEnvironmentalReverb *this = (IEnvironmentalReverb *) self;
        interface_lock_exclusive(this);
        this->mProperties.diffusion = diffusion;
#if !defined(ANDROID) || defined(USE_BACKPORT)
        result = SL_RESULT_SUCCESS;
#else
        if (NO_ENVREVERB(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status = android_erev_setParam(this->mEnvironmentalReverbEffect,
                    REVERB_PARAM_DIFFUSION, &diffusion);
            result = android_fx_statusToResult(status);
        }
#endif
        interface_unlock_exclusive(this);
    }

    SL_LEAVE_INTERFACE
}


static SLresult IEnvironmentalReverb_GetDiffusion(SLEnvironmentalReverbItf self,
     SLpermille *pDiffusion)
{
    SL_ENTER_INTERFACE

    if (NULL == pDiffusion) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IEnvironmentalReverb *this = (IEnvironmentalReverb *) self;
        interface_lock_peek(this);
#if !defined(ANDROID) || defined(USE_BACKPORT)
        result = SL_RESULT_SUCCESS;
#else
        if (NO_ENVREVERB(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status = android_erev_getParam(this->mEnvironmentalReverbEffect,
                    REVERB_PARAM_DIFFUSION, &this->mProperties.diffusion);
            result = android_fx_statusToResult(status);
        }
#endif
        *pDiffusion = this->mProperties.diffusion;

        interface_unlock_peek(this);
    }

    SL_LEAVE_INTERFACE
}


static SLresult IEnvironmentalReverb_SetDensity(SLEnvironmentalReverbItf self,
    SLpermille density)
{
    SL_ENTER_INTERFACE

    if (!(0 <= density && density <= 1000)) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IEnvironmentalReverb *this = (IEnvironmentalReverb *) self;
        interface_lock_exclusive(this);
#if !defined(ANDROID) || defined(USE_BACKPORT)
        result = SL_RESULT_SUCCESS;
#else
        if (NO_ENVREVERB(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status = android_erev_setParam(this->mEnvironmentalReverbEffect,
                    REVERB_PARAM_DENSITY, &density);
            result = android_fx_statusToResult(status);
        }
#endif
        interface_unlock_exclusive(this);
    }

    SL_LEAVE_INTERFACE
}


static SLresult IEnvironmentalReverb_GetDensity(SLEnvironmentalReverbItf self,
    SLpermille *pDensity)
{
    SL_ENTER_INTERFACE

    if (NULL == pDensity) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IEnvironmentalReverb *this = (IEnvironmentalReverb *) self;
        interface_lock_peek(this);
#if !defined(ANDROID) || defined(USE_BACKPORT)
        result = SL_RESULT_SUCCESS;
#else
        if (NO_ENVREVERB(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status = android_erev_getParam(this->mEnvironmentalReverbEffect,
                    REVERB_PARAM_DENSITY, &this->mProperties.density);
            result = android_fx_statusToResult(status);
        }
#endif
        *pDensity = this->mProperties.density;

        interface_unlock_peek(this);
    }

    SL_LEAVE_INTERFACE
}


static SLresult IEnvironmentalReverb_SetEnvironmentalReverbProperties(SLEnvironmentalReverbItf self,
    const SLEnvironmentalReverbSettings *pProperties)
{
    SL_ENTER_INTERFACE

    result = SL_RESULT_PARAMETER_INVALID;
    do {
        if (NULL == pProperties)
            break;
        SLEnvironmentalReverbSettings properties = *pProperties;
        if (!(SL_MILLIBEL_MIN <= properties.roomLevel && properties.roomLevel <= 0))
            break;
        if (!(SL_MILLIBEL_MIN <= properties.roomHFLevel && properties.roomHFLevel <= 0))
            break;
        if (!(100 <= properties.decayTime && properties.decayTime <= 20000))
            break;
        if (!(100 <= properties.decayHFRatio && properties.decayHFRatio <= 2000))
            break;
        if (!(SL_MILLIBEL_MIN <= properties.reflectionsLevel &&
            properties.reflectionsLevel <= 1000))
            break;
        if (!(/* 0 <= properties.reflectionsDelay && */ properties.reflectionsDelay <= 300))
            break;
        if (!(SL_MILLIBEL_MIN <= properties.reverbLevel && properties.reverbLevel <= 2000))
            break;
        if (!(/* 0 <= properties.reverbDelay && */ properties.reverbDelay <= 100))
            break;
        if (!(0 <= properties.diffusion && properties.diffusion <= 1000))
            break;
        if (!(0 <= properties.density && properties.density <= 1000))
            break;
        IEnvironmentalReverb *this = (IEnvironmentalReverb *) self;
        interface_lock_exclusive(this);
        this->mProperties = properties;
#if !defined(ANDROID) || defined(USE_BACKPORT)
        result = SL_RESULT_SUCCESS;
#else
        if (NO_ENVREVERB(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status = android_erev_setParam(this->mEnvironmentalReverbEffect,
                    REVERB_PARAM_PROPERTIES, &properties);
            result = android_fx_statusToResult(status);
        }
#endif
        interface_unlock_exclusive(this);
    } while (0);

    SL_LEAVE_INTERFACE
}


static SLresult IEnvironmentalReverb_GetEnvironmentalReverbProperties(
    SLEnvironmentalReverbItf self, SLEnvironmentalReverbSettings *pProperties)
{
    SL_ENTER_INTERFACE

    if (NULL == pProperties) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IEnvironmentalReverb *this = (IEnvironmentalReverb *) self;
        interface_lock_shared(this);
#if !defined(ANDROID) || defined(USE_BACKPORT)
        result = SL_RESULT_SUCCESS;
#else
        if (NO_ENVREVERB(this)) {
            result = SL_RESULT_CONTROL_LOST;
        } else {
            android::status_t status = android_erev_getParam(this->mEnvironmentalReverbEffect,
                    REVERB_PARAM_PROPERTIES, &this->mProperties);
            result = android_fx_statusToResult(status);
        }
#endif
        *pProperties = this->mProperties;

        interface_unlock_shared(this);
    }

    SL_LEAVE_INTERFACE
}


static const struct SLEnvironmentalReverbItf_ IEnvironmentalReverb_Itf = {
    IEnvironmentalReverb_SetRoomLevel,
    IEnvironmentalReverb_GetRoomLevel,
    IEnvironmentalReverb_SetRoomHFLevel,
    IEnvironmentalReverb_GetRoomHFLevel,
    IEnvironmentalReverb_SetDecayTime,
    IEnvironmentalReverb_GetDecayTime,
    IEnvironmentalReverb_SetDecayHFRatio,
    IEnvironmentalReverb_GetDecayHFRatio,
    IEnvironmentalReverb_SetReflectionsLevel,
    IEnvironmentalReverb_GetReflectionsLevel,
    IEnvironmentalReverb_SetReflectionsDelay,
    IEnvironmentalReverb_GetReflectionsDelay,
    IEnvironmentalReverb_SetReverbLevel,
    IEnvironmentalReverb_GetReverbLevel,
    IEnvironmentalReverb_SetReverbDelay,
    IEnvironmentalReverb_GetReverbDelay,
    IEnvironmentalReverb_SetDiffusion,
    IEnvironmentalReverb_GetDiffusion,
    IEnvironmentalReverb_SetDensity,
    IEnvironmentalReverb_GetDensity,
    IEnvironmentalReverb_SetEnvironmentalReverbProperties,
    IEnvironmentalReverb_GetEnvironmentalReverbProperties
};

static const SLEnvironmentalReverbSettings IEnvironmentalReverb_default = {
    SL_MILLIBEL_MIN, // roomLevel
    0,               // roomHFLevel
    1000,            // decayTime
    500,             // decayHFRatio
    SL_MILLIBEL_MIN, // reflectionsLevel
    20,              // reflectionsDelay
    SL_MILLIBEL_MIN, // reverbLevel
    40,              // reverbDelay
    1000,            // diffusion
    1000             // density
};

void IEnvironmentalReverb_init(void *self)
{
    IEnvironmentalReverb *this = (IEnvironmentalReverb *) self;
    this->mItf = &IEnvironmentalReverb_Itf;
    this->mProperties = IEnvironmentalReverb_default;

#if defined(ANDROID) && !defined(USE_BACKPORT)
    if (!android_fx_initEffectDescriptor(SL_IID_ENVIRONMENTALREVERB,
            &this->mEnvironmentalReverbDescriptor)) {
        // PresetReverb init failed
        SL_LOGE("EnvironmentalReverb initialization failed.");
    }
#endif
}
