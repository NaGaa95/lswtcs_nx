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

// Map minimal perfect hash of an interface ID to its class index.

#include "MPH.h"
#include "MPH_to.h"

// If defined, then compile with C99 such as GNU C, not GNU C++ or non-GNU C.
//#define USE_DESIGNATED_INITIALIZERS

// It is critical that all entries are populated with either a specific index
// or -1. Do not let the compiler use a default initializer of zero, because
// that actually maps to the IObject index. To test this, try defining END to
// generate an extra entry, and verify that you get one error message per class.
//#define END , (-1)  // for testing
#define END           // for production

// Another good test is to use the GNU C compiler with -S option (for assembler output),
// and compile both with and without USE_DESIGNATED_INITIALIZERS.  The resulting .s
// files should be identical for both compilations.

// Important note: if you add any interfaces here, be sure to also
// update the #define for the corresponding INTERFACES_<Class>.

// IObject is the first interface in a class, so the index for MPH_OBJECT must be zero.
// Don't cross streams, otherwise bad things happen.


#if USE_PROFILES & USE_PROFILES_GAME
const signed char MPH_to_3DGroup[MPH_MAX] = {
#ifdef USE_DESIGNATED_INITIALIZERS
    [0 ... MPH_MAX-1] = -1,
    [MPH_OBJECT] = 0,
    [MPH_DYNAMICINTERFACEMANAGEMENT] = 1,
    [MPH_3DLOCATION] = 2,
    [MPH_3DDOPPLER] = 3,
    [MPH_3DSOURCE] = 4,
    [MPH_3DMACROSCOPIC] = 5
#else
    -1,
    3, // MPH_3DDOPPLER
    -1,
    2, // MPH_3DLOCATION
    5, // MPH_3DMACROSCOPIC
    4, // MPH_3DSOURCE
    -1, -1, -1, -1, -1, -1, -1,
    1, // MPH_DYNAMICINTERFACEMANAGEMENT
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    0, // MPH_OBJECT
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, // MPH_OUTPUTMIXEXT
    -1, // MPH_ANDROIDEFFECT
    -1, // MPH_ANDROIDEFFECTCAPABILITIES
    -1, // MPH_ANDROIDEFFECTSEND
    -1, // MPH_ANDROIDCONFIGURATION
    -1  // MPH_ANDROIDSIMPLEBUFFERQUEUE
    END
#endif
};
#endif

const signed char MPH_to_AudioPlayer[MPH_MAX] = {
#ifdef USE_DESIGNATED_INITIALIZERS
    [0 ... MPH_MAX-1] = -1,
    [MPH_OBJECT] = 0,
    [MPH_DYNAMICINTERFACEMANAGEMENT] = 1,
    [MPH_PLAY] = 2,
    [MPH_3DDOPPLER] = 3,
    [MPH_3DGROUPING] = 4,
    [MPH_3DLOCATION] = 5,
    [MPH_3DSOURCE] = 6,
    [MPH_BUFFERQUEUE] = 7,
    [MPH_EFFECTSEND] = 8,
    [MPH_MUTESOLO] = 9,
    [MPH_METADATAEXTRACTION] = 10,
    [MPH_METADATATRAVERSAL] = 11,
    [MPH_PREFETCHSTATUS] = 12,
    [MPH_RATEPITCH] = 13,
    [MPH_SEEK] = 14,
    [MPH_VOLUME] = 15,
    [MPH_3DMACROSCOPIC] = 16,
    [MPH_BASSBOOST] = 17,
    [MPH_DYNAMICSOURCE] = 18,
    [MPH_ENVIRONMENTALREVERB] = 19,
    [MPH_EQUALIZER] = 20,
    [MPH_PITCH] = 21,
    [MPH_PRESETREVERB] = 22,
    [MPH_PLAYBACKRATE] = 23,
    [MPH_VIRTUALIZER] = 24,
    [MPH_VISUALIZATION] = 25,
#ifdef ANDROID
    [MPH_ANDROIDEFFECT] = 26,
    [MPH_ANDROIDEFFECTSEND] = 27,
    [MPH_ANDROIDCONFIGURATION] = 28,
    [MPH_ANDROIDSIMPLEBUFFERQUEUE] = 7  // alias for [MPH_BUFFERQUEUE]
#endif
#else
    -1,
    3,  // MPH_3DDOPPLER
    4,  // MPH_3DGROUPING
    5,  // MPH_3DLOCATION
    16, // MPH_3DMACROSCOPIC
    6,  // MPH_3DSOURCE
    -1, -1, -1, -1,
    17, // MPH_BASSBOOST
    7,  // MPH_BUFFERQUEUE
    -1,
    1,  // MPH_DYNAMICINTERFACEMANAGEMENT
    18, // MPH_DYNAMICSOURCE
    8,  // MPH_EFFECTSEND
    -1, -1,
    19, // MPH_ENVIRONMENTALREVERB
    20, // MPH_EQUALIZER
    -1,
    10, // MPH_METADATAEXTRACTION
    11, // MPH_METADATATRAVERSAL
    -1, -1, -1, -1,
    9,  // MPH_MUTESOLO
    -1,
    0,  // MPH_OBJECT
    -1,
    21, // MPH_PITCH
    2,  // MPH_PLAY
    23, // MPH_PLAYBACKRATE
    12, // MPH_PREFETCHSTATUS
    22, // MPH_PRESETREVERB
    13, // MPH_RATEPITCH
    -1,
    14, // MPH_SEEK
    -1, -1,
    24, // MPH_VIRTUALIZER
    25, // MPH_VISUALIZATION
    15, // MPH_VOLUME
    -1, // not using MPH_OUTPUTMIXEXT
#ifdef ANDROID
    26, // MPH_ANDROIDEFFECT
    -1, // MPH_ANDROIDEFFECTCAPABILITIES
    27, // MPH_ANDROIDEFFECTSEND
    28, // MPH_ANDROIDCONFIGURATION
    7   // MPH_SIMPLEBUFFERQUEUE    // alias for [MPH_BUFFERQUEUE]
#else
    -1, // not using MPH_ANDROIDEFFECT
    -1, // not using MPH_ANDROIDEFFECTCAPABILITIES
    -1, // not using MPH_ANDROIDEFFECTSEND
    -1, // not using MPH_ANDROIDCONFIGURATION
    7   // MPH_SIMPLEBUFFERQUEUE    // alias for [MPH_BUFFERQUEUE]
#endif
    END
#endif
};

#if (USE_PROFILES & USE_PROFILES_OPTIONAL) || defined(ANDROID)
const signed char MPH_to_AudioRecorder[MPH_MAX] = {
#ifdef USE_DESIGNATED_INITIALIZERS
    [0 ... MPH_MAX-1] = -1,
    [MPH_OBJECT] = 0,
    [MPH_DYNAMICINTERFACEMANAGEMENT] = 1,
    [MPH_RECORD] = 2,
    [MPH_AUDIOENCODER] = 3,
    [MPH_BASSBOOST] = 4,
    [MPH_DYNAMICSOURCE] = 5,
    [MPH_EQUALIZER] = 6,
    [MPH_VISUALIZATION] = 7,
    [MPH_VOLUME] = 8,
#ifdef ANDROID
    [MPH_ANDROIDSIMPLEBUFFERQUEUE] = 9, // this is not an alias
    [MPH_ANDROIDCONFIGURATION] = 10
#endif
#else
    -1, -1, -1, -1, -1, -1, -1,
    3, // MPH_AUDIOENCODER
    -1, -1,
    4, // MPH_BASSBOOST
    -1, // MPH_BUFFERQUEUE (application must specify MPH_ANDROIDSIMPLEBUFFERQUEUE)
    -1,
    1, // MPH_DYNAMICINTERFACEMANAGEMENT
    5, // MPH_DYNAMICSOURCE
    -1, -1, -1, -1,
    6, // MPH_EQUALIZER
    -1, -1, -1, -1, -1, -1, -1, -1, -1,
    0, // MPH_OBJECT
    -1, -1, -1, -1, -1, -1, -1,
    2, // MPH_RECORD
    -1, -1, -1, -1,
    7, // MPH_VISUALIZATION
    8, // MPH_VOLUME
    -1, // not using MPH_OUTPUTMIXEXT
    -1, // not using MPH_ANDROIDEFFECT
    -1, // not using MPH_ANDROIDEFFECTCAPABILITIES
    -1, // not using MPH_ANDROIDEFFECTSEND
#ifdef ANDROID
    10, // MPH_ANDROIDCONFIGURATION
    9   // MPH_ANDROIDSIMPLEBUFFERQUEUE (this is not an alias)
#else
    -1, // not using MPH_ANDROIDCONFIGURATION
    -1  // not using MPH_ANDROIDSIMPLEBUFFERQUEUE
#endif
    END
#endif
};
#endif

const signed char MPH_to_Engine[MPH_MAX] = {
#ifdef USE_DESIGNATED_INITIALIZERS
    [0 ... MPH_MAX-1] = -1,
    [MPH_OBJECT] = 0,
    [MPH_DYNAMICINTERFACEMANAGEMENT] = 1,
    [MPH_ENGINE] = 2,
    [MPH_ENGINECAPABILITIES] = 3,
    [MPH_THREADSYNC] = 4,
    [MPH_AUDIOIODEVICECAPABILITIES] = 5,
    [MPH_AUDIODECODERCAPABILITIES] = 6,
    [MPH_AUDIOENCODERCAPABILITIES] = 7,
    [MPH_3DCOMMIT] = 8,
    [MPH_DEVICEVOLUME] = 9,
#ifdef ANDROID
    [MPH_ANDROIDEFFECTCAPABILITIES] = 10
#endif
#else
    8, // MPH_3DCOMMIT
    -1, -1, -1, -1, -1,
    6, // MPH_AUDIODECODERCAPABILITIES
    -1,
    7, // MPH_AUDIOENCODERCAPABILITIES
    5, // MPH_AUDIOIODEVICECAPABILITIES
    -1, -1,
    9, // MPH_DEVICEVOLUME
    1, // MPH_DYNAMICINTERFACEMANAGEMENT
    -1, -1,
    2, // MPH_ENGINE
    3, // MPH_ENGINECAPABILITIES
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    0, // MPH_OBJECT
    -1, -1, -1, -1, -1, -1, -1, -1, -1,
    4, // MPH_THREADSYNC
    -1, -1, -1, -1,
    -1, // MPH_OUTPUTMIXEXT
#ifdef ANDROID
    -1, // MPH_ANDROIDEFFECT
    10, // MPH_ANDROIDEFFECTCAPABILITIES
    -1, // MPH_ANDROIDEFFECTSEND
    -1, // MPH_ANDROIDCONFIGURATION
    -1  // MPH_ANDROIDSIMPLEBUFFERQUEUE
#else
    -1, // MPH_ANDROIDEFFECT
    -1, // MPH_ANDROIDEFFECTCAPABILITIES
    -1, // MPH_ANDROIDEFFECTSEND
    -1, // MPH_ANDROIDCONFIGURATION
    -1  // MPH_ANDROIDSIMPLEBUFFERQUEUE
#endif
    END
#endif
};

#if USE_PROFILES & USE_PROFILES_OPTIONAL
const signed char MPH_to_LEDDevice[MPH_MAX] = {
#ifdef USE_DESIGNATED_INITIALIZERS
    [0 ... MPH_MAX-1] = -1,
    [MPH_OBJECT] = 0,
    [MPH_DYNAMICINTERFACEMANAGEMENT] = 1,
    [MPH_LED] = 2
#else
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    1, // MPH_DYNAMICINTERFACEMANAGEMENT
    -1, -1, -1, -1, -1, -1,
    2, // MPH_LED
    -1, -1, -1, -1, -1, -1, -1, -1,
    0, // MPH_OBJECT
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, // MPH_OUTPUTMIXEXT
    -1, // MPH_ANDROIDEFFECT
    -1, // MPH_ANDROIDEFFECTCAPABILITIES
    -1, // MPH_ANDROIDEFFECTSEND
    -1, // MPH_ANDROIDCONFIGURATION
    -1  // MPH_ANDROIDSIMPLEBUFFERQUEUE
    END
#endif
};
#endif

#if USE_PROFILES & USE_PROFILES_GAME
const signed char MPH_to_Listener[MPH_MAX] = {
#ifdef USE_DESIGNATED_INITIALIZERS
    [0 ... MPH_MAX-1] = -1,
    [MPH_OBJECT] = 0,
    [MPH_DYNAMICINTERFACEMANAGEMENT] = 1,
    [MPH_3DDOPPLER] = 2,
    [MPH_3DLOCATION] = 3
#else
    -1,
    2, // MPH_3DDOPPLER
    -1,
    3, // MPH_3DLOCATION
    -1, -1, -1, -1, -1, -1, -1, -1, -1,
    1, // MPH_DYNAMICINTERFACEMANAGEMENT
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    0, // MPH_OBJECT
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, // MPH_OUTPUTMIXEXT
    -1, // MPH_ANDROIDEFFECT
    -1, // MPH_ANDROIDEFFECTCAPABILITIES
    -1, // MPH_ANDROIDEFFECTSEND
    -1, // MPH_ANDROIDCONFIGURATION
    -1  // MPH_ANDROIDSIMPLEBUFFERQUEUE
    END
#endif
};
#endif

#if USE_PROFILES & (USE_PROFILES_GAME | USE_PROFILES_MUSIC)
const signed char MPH_to_MetadataExtractor[MPH_MAX] = {
#ifdef USE_DESIGNATED_INITIALIZERS
    [0 ... MPH_MAX-1] = -1,
    [MPH_OBJECT] = 0,
    [MPH_DYNAMICINTERFACEMANAGEMENT] = 1,
    [MPH_DYNAMICSOURCE] = 2,
    [MPH_METADATAEXTRACTION] = 3,
    [MPH_METADATATRAVERSAL] = 4
#else
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    1, // MPH_DYNAMICINTERFACEMANAGEMENT
    2, // MPH_DYNAMICSOURCE
    -1, -1, -1, -1, -1, -1,
    3, // MPH_METADATAEXTRACTION
    4, // MPH_METADATATRAVERSAL
    -1, -1, -1, -1, -1, -1,
    0, // MPH_OBJECT
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, // MPH_OUTPUTMIXEXT
    -1, // MPH_ANDROIDEFFECT
    -1, // MPH_ANDROIDEFFECTCAPABILITIES
    -1, // MPH_ANDROIDEFFECTSEND
    -1, // MPH_ANDROIDCONFIGURATION
    -1  // MPH_ANDROIDSIMPLEBUFFERQUEUE
    END
#endif
};
#endif

#if USE_PROFILES & USE_PROFILES_GAME
const signed char MPH_to_MidiPlayer[MPH_MAX] = {
#ifdef USE_DESIGNATED_INITIALIZERS
    [0 ... MPH_MAX-1] = -1,
    [MPH_OBJECT] = 0,
    [MPH_DYNAMICINTERFACEMANAGEMENT] = 1,
    [MPH_PLAY] = 2,
    [MPH_3DDOPPLER] = 3,
    [MPH_3DGROUPING] = 4,
    [MPH_3DLOCATION] = 5,
    [MPH_3DSOURCE] = 6,
    [MPH_BUFFERQUEUE] = 7,
    [MPH_EFFECTSEND] = 8,
    [MPH_MUTESOLO] = 9,
    [MPH_METADATAEXTRACTION] = 10,
    [MPH_METADATATRAVERSAL] = 11,
    [MPH_MIDIMESSAGE] = 12,
    [MPH_MIDITIME] = 13,
    [MPH_MIDITEMPO] = 14,
    [MPH_MIDIMUTESOLO] = 15,
    [MPH_PREFETCHSTATUS] = 16,
    [MPH_SEEK] = 17,
    [MPH_VOLUME] = 18,
    [MPH_3DMACROSCOPIC] = 19,
    [MPH_BASSBOOST] = 20,
    [MPH_DYNAMICSOURCE] = 21,
    [MPH_ENVIRONMENTALREVERB] = 22,
    [MPH_EQUALIZER] = 23,
    [MPH_PITCH] = 24,
    [MPH_PRESETREVERB] = 25,
    [MPH_PLAYBACKRATE] = 26,
    [MPH_VIRTUALIZER] = 27,
    [MPH_VISUALIZATION] = 28,
#else
    -1,
    3,  // MPH_3DDOPPLER
    4,  // MPH_3DGROUPING
    5,  // MPH_3DLOCATION
    19, // MPH_3DMACROSCOPIC
    6,  // MPH_3DSOURCE
    -1, -1, -1, -1,
    20, // MPH_BASSBOOST
    7,  // MPH_BUFFERQUEUE
    -1,
    1,  // MPH_DYNAMICINTERFACEMANAGEMENT
    21, // MPH_DYNAMICSOURCE
    8,  // MPH_EFFECTSEND
    -1, -1,
    22, // MPH_ENVIRONMENTALREVERB
    23, // MPH_EQUALIZER
    -1,
    10, // MPH_METADATAEXTRACTION
    11, // MPH_METADATATRAVERSAL
    12, // MPH_MIDIMESSAGE
    15, // MPH_MIDIMUTESOLO
    14, // MPH_MIDITEMPO
    13, // MPH_MIDITIME
    9,  // MPH_MUTESOLO
    -1,
    0,  // MPH_OBJECT
    -1,
    24, // MPH_PITCH
    2,  // MPH_PLAY
    26, // MPH_PLAYBACKRATE
    16, // MPH_PREFETCHSTATUS
    25, // MPH_PRESETREVERB
    -1, -1,
    17, // MPH_SEEK
    -1, -1,
    27, // MPH_VIRTUALIZER
    28, // MPH_VISUALIZATION
    18, // MPH_VOLUME
    -1, // MPH_OUTPUTMIXEXT
    -1, // MPH_ANDROIDEFFECT
    -1, // MPH_ANDROIDEFFECTCAPABILITIES
    -1, // MPH_ANDROIDEFFECTSEND
    -1, // MPH_ANDROIDCONFIGURATION
    -1  // MPH_ANDROIDSIMPLEBUFFERQUEUE
    END
#endif
};
#endif

const signed char MPH_to_OutputMix[MPH_MAX] = {
#ifdef USE_DESIGNATED_INITIALIZERS
    [0 ... MPH_MAX-1] = -1,
    [MPH_OBJECT] = 0,
    [MPH_DYNAMICINTERFACEMANAGEMENT] = 1,
    [MPH_OUTPUTMIX] = 2,
#ifdef USE_OUTPUTMIXEXT
    [MPH_OUTPUTMIXEXT] = 3,
#endif
    [MPH_ENVIRONMENTALREVERB] = 4,
    [MPH_EQUALIZER] = 5,
    [MPH_PRESETREVERB] = 6,
    [MPH_VIRTUALIZER] = 7,
    [MPH_VOLUME] = 8,
    [MPH_BASSBOOST] = 9,
    [MPH_VISUALIZATION] = 10,
#ifdef ANDROID
    [MPH_ANDROIDEFFECT] = 11
#endif
#else
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    9,  // MPH_BASSBOOST
    -1, -1,
    1,  // MPH_DYNAMICINTERFACEMANAGEMENT
    -1, -1, -1, -1,
    4,  // MPH_ENVIRONMENTALREVERB
    5,  // MPH_EQUALIZER
    -1, -1, -1, -1, -1, -1, -1, -1, -1,
    0,  // MPH_OBJECT
    2,  // MPH_OUTPUTMIX
    -1, -1, -1, -1,
    6,  // MPH_PRESETREVERB
    -1, -1, -1, -1, -1,
    7,  // MPH_VIRTUALIZER
    10, // MPH_VISUALIZATION
    8,  // MPH_VOLUME
#ifdef USE_OUTPUTMIXEXT
    3,  // MPH_OUTPUTMIXEXT
#else
    -1,
#endif
#ifdef ANDROID
    11, // MPH_ANDROIDEFFECT
#else
    -1,
#endif
    -1, // MPH_ANDROIDEFFECTCAPABILITIES
    -1, // MPH_ANDROIDEFFECTSEND
    -1, // MPH_ANDROIDCONFIGURATION
    -1  // MPH_ANDROIDSIMPLEBUFFERQUEUE
    END
#endif
};

#if USE_PROFILES & USE_PROFILES_OPTIONAL
const signed char MPH_to_Vibra[MPH_MAX] = {
#ifdef USE_DESIGNATED_INITIALIZERS
    [0 ... MPH_MAX-1] = -1,
    [MPH_OBJECT] = 0,
    [MPH_DYNAMICINTERFACEMANAGEMENT] = 1,
    [MPH_VIBRA] = 2
#else
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    1, // MPH_DYNAMICINTERFACEMANAGEMENT
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    0, // MPH_OBJECT
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    2, // MPH_VIBRA
    -1, -1, -1,
    -1, // MPH_OUTPUTMIXEXT
    -1, // MPH_ANDROIDEFFECT
    -1, // MPH_ANDROIDEFFECTCAPABILITIES
    -1, // MPH_ANDROIDEFFECTSEND
    -1, // MPH_ANDROIDCONFIGURATION
    -1  // MPH_ANDROIDSIMPLEBUFFERQUEUE
    END
#endif
};
#endif
