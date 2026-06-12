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

#include "math.h"

//-----------------------------------------------------------------------------
// Android to OpenSL ES
//----------------------
static inline SLuint32 android_to_sles_streamType(int type) {
    return (SLuint32) type;
}


static inline SLuint32 android_to_sles_sampleRate(uint32_t srHz) {
    // convert to milliHertz
    return (SLuint32) srHz*1000;
}


//-----------------------------------------------------------------------------
// OpenSL ES to Android
//----------------------
static inline int sles_to_android_streamType(SLuint32 type) {
    return (int)type;
}


static inline uint32_t sles_to_android_sampleRate(SLuint32 sampleRateMilliHertz) {
    return (uint32_t)(sampleRateMilliHertz / 1000);
}

static inline int sles_to_android_sampleFormat(SLuint32 pcmFormat) {
    switch (pcmFormat) {
        case SL_PCMSAMPLEFORMAT_FIXED_16:
            return android::AudioSystem::PCM_16_BIT;
            break;
        case SL_PCMSAMPLEFORMAT_FIXED_8:
            return android::AudioSystem::PCM_8_BIT;
            break;
        case SL_PCMSAMPLEFORMAT_FIXED_20:
        case SL_PCMSAMPLEFORMAT_FIXED_24:
        case SL_PCMSAMPLEFORMAT_FIXED_28:
        case SL_PCMSAMPLEFORMAT_FIXED_32:
        default:
            return android::AudioSystem::INVALID_FORMAT;
    }
}


static inline int sles_to_android_channelMask(SLuint32 nbChannels, SLuint32 channelMask) {
    // FIXME handle channel mask mapping between SL ES and Android
    return (nbChannels == 1 ?
            android::AudioSystem::CHANNEL_OUT_MONO :
            android::AudioSystem::CHANNEL_OUT_STEREO);
}


static inline float sles_to_android_amplification(SLmillibel level) {
    // FIXME use the FX Framework conversions
    return pow(10, (float)level/2000);
}
