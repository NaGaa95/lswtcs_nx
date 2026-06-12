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

/** \file IID_to_MPH.c Interface ID to MPH mapping */

#include "SLES/OpenSLES.h"
#include "MPH.h"
#include <assert.h>
#include <string.h>

extern const struct SLInterfaceID_ SL_IID_array[MPH_MAX];

/** \brief Map SLInterfaceID to its minimal perfect hash (MPH), or -1 if unknown */

int IID_to_MPH(const SLInterfaceID iid)
{
#define MAX_HASH_VALUE 115
  static const unsigned char asso_values[] =
    {
        5, 116, 116,  61, 116, 116,   7, 116, 116,   7,
      116,  12, 116, 116, 116,  46, 116, 116, 116, 116,
      116, 116,  56, 116, 116,  36, 116, 116, 116, 116,
      116,   7,  20, 116, 116, 116, 116, 116, 116, 116,
      116, 116,  41,  20,  36, 116,  21, 116, 116, 116,
      116, 116, 116,  26, 116,   2, 116, 116, 116, 116,
      116,  51, 116,  51,  55, 116, 116, 116, 116, 116,
      116,  16, 116, 116, 116, 116, 116, 116, 116, 116,
       21, 116, 116, 116, 116, 116,  26, 116, 116, 116,
       11, 116, 116, 116, 116, 116,  40,  11, 116,  60,
      116, 116, 116, 116, 116, 116,   6, 116, 116, 116,
      116, 116,   1, 116, 116, 116, 116, 116, 116, 116,
      116,   1,  55,   6, 116, 116, 116, 116,  45, 116,
      116,   1, 116, 116, 116, 116, 116,   1, 116, 116,
      116,  60, 116, 116, 116, 116, 116, 116, 116, 116,
      116,  55, 116, 116, 116, 116, 116, 116, 116, 116,
       10, 116, 116, 116, 116, 116, 116, 116, 116, 116,
       45, 116, 116, 116,  40, 116, 116, 116,  55, 116,
       40, 116, 116, 116, 116, 116, 116, 116, 116, 116,
      116, 116,  15, 116,  35, 116,  45,   5, 116,  50,
      116, 116, 116, 116, 116, 116, 116, 116, 116, 116,
      116, 116,  40, 116, 116,  50, 116, 116, 116, 116,
      116,  25, 116, 116,  15,  35, 116, 116,  10, 116,
      116, 116, 116, 116, 116,  10,   0, 116, 116,  15,
       30, 116, 116, 116, 116, 116,  55, 116, 116, 116,
      116, 116, 116, 116,   0, 116
    };
    static const signed char hash_to_MPH[] = {
        MPH_NULL,
        -1,
        -1,
        -1,
        -1,
        MPH_OUTPUTMIXEXT,
        MPH_3DSOURCE,
        MPH_VIRTUALIZER,
        -1,
        -1,
        MPH_DYNAMICSOURCE,
        MPH_ENGINECAPABILITIES,
        MPH_MIDITEMPO,
        -1,
        -1,
        MPH_RECORD,
        MPH_ANDROIDCONFIGURATION,
        MPH_EQUALIZER,
        -1,
        -1,
        MPH_VISUALIZATION,
        MPH_ANDROIDEFFECTSEND,
        MPH_VOLUME,
        -1,
        -1,
        MPH_3DGROUPING,
        MPH_MUTESOLO,
        MPH_BASSBOOST,
        -1,
        -1,
        MPH_PLAY,
        MPH_EFFECTSEND,
        -1,
        -1,
        -1,
        MPH_BUFFERQUEUE,
        MPH_3DMACROSCOPIC,
        -1,
        -1,
        -1,
        MPH_3DLOCATION,
        MPH_OBJECT,
        -1,
        -1,
        -1,
        MPH_MIDIMESSAGE,
        MPH_ANDROIDEFFECTCAPABILITIES,
        -1,
        -1,
        -1,
        MPH_SEEK,
        MPH_RATEPITCH,
        -1,
        -1,
        -1,
        MPH_PITCH,
        MPH_PRESETREVERB,
        -1,
        -1,
        -1,
        MPH_METADATATRAVERSAL,
        MPH_AUDIODECODERCAPABILITIES,
        -1,
        -1,
        -1,
        MPH_ENVIRONMENTALREVERB,
        MPH_MIDITIME,
        -1,
        -1,
        -1,
        MPH_AUDIOIODEVICECAPABILITIES,
        MPH_3DCOMMIT,
        -1,
        -1,
        -1,
        MPH_DEVICEVOLUME,
        MPH_PLAYBACKRATE,
        -1,
        -1,
        -1,
        MPH_ANDROIDEFFECT,
        MPH_LED,
        -1,
        -1,
        -1,
        MPH_3DDOPPLER,
        MPH_PREFETCHSTATUS,
        -1,
        -1,
        -1,
        MPH_METADATAEXTRACTION,
        MPH_ANDROIDSIMPLEBUFFERQUEUE,
        -1,
        -1,
        -1,
        MPH_OUTPUTMIX,
        MPH_VIBRA,
        -1,
        -1,
        -1,
        MPH_ENGINE,
        MPH_AUDIOENCODERCAPABILITIES,
        -1,
        -1,
        -1,
        MPH_AUDIOENCODER,
        MPH_MIDIMUTESOLO,
        -1,
        -1,
        -1,
        MPH_THREADSYNC,
        -1,
        -1,
        -1,
        -1,
        MPH_DYNAMICINTERFACEMANAGEMENT
    };
    if (&SL_IID_array[0] <= iid && &SL_IID_array[MPH_MAX] > iid)
        return iid - &SL_IID_array[0];
    if (NULL != iid) {
        unsigned key = asso_values[((unsigned char *)iid)[3]] +
            asso_values[((unsigned char *)iid)[0]];
        if (key <= MAX_HASH_VALUE) {
            int MPH = hash_to_MPH[key];
            if (MPH >= 0) {
                assert(MPH < MPH_MAX);
                SLInterfaceID trial = &SL_IID_array[MPH];
                if (!memcmp(iid, trial, sizeof(struct SLInterfaceID_)))
                    return MPH;
            }
        }
    }
    return -1;
}
