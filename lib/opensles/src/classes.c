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

/* Classes vs. interfaces */

#include "sles_allinclusive.h"


#if USE_PROFILES & USE_PROFILES_GAME

// 3DGroup class

static const struct iid_vtable _3DGroup_interfaces[INTERFACES_3DGroup] = {
    {MPH_OBJECT, INTERFACE_IMPLICIT, offsetof(C3DGroup, mObject)},
    {MPH_DYNAMICINTERFACEMANAGEMENT, INTERFACE_IMPLICIT,
        offsetof(C3DGroup, mDynamicInterfaceManagement)},
    {MPH_3DLOCATION, INTERFACE_IMPLICIT, offsetof(C3DGroup, m3DLocation)},
    {MPH_3DDOPPLER, INTERFACE_DYNAMIC_GAME, offsetof(C3DGroup, m3DDoppler)},
    {MPH_3DSOURCE, INTERFACE_EXPLICIT_GAME, offsetof(C3DGroup, m3DSource)},
    {MPH_3DMACROSCOPIC, INTERFACE_OPTIONAL, offsetof(C3DGroup, m3DMacroscopic)},
};

static const ClassTable C3DGroup_class = {
    _3DGroup_interfaces,
    INTERFACES_3DGroup,
    MPH_to_3DGroup,
    "3DGroup",
    sizeof(C3DGroup),
    SL_OBJECTID_3DGROUP,
    NULL,
    NULL,
    NULL,
    C3DGroup_PreDestroy
};

#endif


// AudioPlayer class

static const struct iid_vtable AudioPlayer_interfaces[INTERFACES_AudioPlayer] = {
    {MPH_OBJECT, INTERFACE_IMPLICIT, offsetof(CAudioPlayer, mObject)},
    {MPH_DYNAMICINTERFACEMANAGEMENT, INTERFACE_IMPLICIT_BASE,
        offsetof(CAudioPlayer, mDynamicInterfaceManagement)},
    {MPH_PLAY, INTERFACE_IMPLICIT, offsetof(CAudioPlayer, mPlay)},
    {MPH_3DDOPPLER, INTERFACE_DYNAMIC_GAME, offsetof(CAudioPlayer, m3DDoppler)},
    {MPH_3DGROUPING, INTERFACE_EXPLICIT_GAME, offsetof(CAudioPlayer, m3DGrouping)},
    {MPH_3DLOCATION, INTERFACE_EXPLICIT_GAME, offsetof(CAudioPlayer, m3DLocation)},
    {MPH_3DSOURCE, INTERFACE_EXPLICIT_GAME, offsetof(CAudioPlayer, m3DSource)},
#ifdef USE_SNDFILE
    // FIXME Currently we create an internal buffer queue for playing encoded files
    {MPH_BUFFERQUEUE, INTERFACE_IMPLICIT, offsetof(CAudioPlayer, mBufferQueue)},
#else
    {MPH_BUFFERQUEUE, INTERFACE_EXPLICIT_GAME, offsetof(CAudioPlayer, mBufferQueue)},
#endif
    {MPH_EFFECTSEND, INTERFACE_EXPLICIT_GAME_MUSIC, offsetof(CAudioPlayer, mEffectSend)},
    {MPH_MUTESOLO, INTERFACE_EXPLICIT_GAME, offsetof(CAudioPlayer, mMuteSolo)},
    {MPH_METADATAEXTRACTION, INTERFACE_DYNAMIC_GAME_MUSIC,
        offsetof(CAudioPlayer, mMetadataExtraction)},
    {MPH_METADATATRAVERSAL, INTERFACE_DYNAMIC_GAME_MUSIC,
        offsetof(CAudioPlayer, mMetadataTraversal)},
    {MPH_PREFETCHSTATUS, INTERFACE_EXPLICIT, offsetof(CAudioPlayer, mPrefetchStatus)},
    {MPH_RATEPITCH, INTERFACE_DYNAMIC_GAME, offsetof(CAudioPlayer, mRatePitch)},
    {MPH_SEEK, INTERFACE_EXPLICIT, offsetof(CAudioPlayer, mSeek)},
    // The base Volume interface is explicit, but portions are only for Game and Music profiles
    {MPH_VOLUME, INTERFACE_EXPLICIT, offsetof(CAudioPlayer, mVolume)},
    {MPH_3DMACROSCOPIC, INTERFACE_OPTIONAL, offsetof(CAudioPlayer, m3DMacroscopic)},
    {MPH_BASSBOOST, INTERFACE_DYNAMIC, offsetof(CAudioPlayer, mBassBoost)},
    {MPH_DYNAMICSOURCE, INTERFACE_OPTIONAL, offsetof(CAudioPlayer, mDynamicSource)},
    {MPH_ENVIRONMENTALREVERB, INTERFACE_DYNAMIC,
        offsetof(CAudioPlayer, mEnvironmentalReverb)},
    {MPH_EQUALIZER, INTERFACE_DYNAMIC, offsetof(CAudioPlayer, mEqualizer)},
    {MPH_PITCH, INTERFACE_DYNAMIC_OPTIONAL, offsetof(CAudioPlayer, mPitch)},
    {MPH_PRESETREVERB, INTERFACE_DYNAMIC, offsetof(CAudioPlayer, mPresetReverb)},
    {MPH_PLAYBACKRATE, INTERFACE_DYNAMIC, offsetof(CAudioPlayer, mPlaybackRate)},
    {MPH_VIRTUALIZER, INTERFACE_DYNAMIC, offsetof(CAudioPlayer, mVirtualizer)},
    {MPH_VISUALIZATION, INTERFACE_OPTIONAL, offsetof(CAudioPlayer, mVisualization)},
#ifdef ANDROID
    {MPH_ANDROIDEFFECT, INTERFACE_EXPLICIT, offsetof(CAudioPlayer, mAndroidEffect)},
    {MPH_ANDROIDEFFECTSEND, INTERFACE_EXPLICIT, offsetof(CAudioPlayer, mAndroidEffectSend)},
    {MPH_ANDROIDCONFIGURATION, INTERFACE_EXPLICIT_PREREALIZE,
            offsetof(CAudioPlayer, mAndroidConfiguration)},
#endif
};

static const ClassTable CAudioPlayer_class = {
    AudioPlayer_interfaces,
    INTERFACES_AudioPlayer,
    MPH_to_AudioPlayer,
    "AudioPlayer",
    sizeof(CAudioPlayer),
    SL_OBJECTID_AUDIOPLAYER,
    CAudioPlayer_Realize,
    CAudioPlayer_Resume,
    CAudioPlayer_Destroy,
    CAudioPlayer_PreDestroy
};


#if (USE_PROFILES & USE_PROFILES_OPTIONAL) || defined(ANDROID)

// AudioRecorder class

static const struct iid_vtable AudioRecorder_interfaces[INTERFACES_AudioRecorder] = {
    {MPH_OBJECT, INTERFACE_IMPLICIT, offsetof(CAudioRecorder, mObject)},
    {MPH_DYNAMICINTERFACEMANAGEMENT, INTERFACE_IMPLICIT_BASE,
        offsetof(CAudioRecorder, mDynamicInterfaceManagement)},
    {MPH_RECORD, INTERFACE_IMPLICIT, offsetof(CAudioRecorder, mRecord)},
    {MPH_AUDIOENCODER, INTERFACE_EXPLICIT, offsetof(CAudioRecorder, mAudioEncoder)},
    {MPH_BASSBOOST, INTERFACE_DYNAMIC_OPTIONAL, offsetof(CAudioRecorder, mBassBoost)},
    {MPH_DYNAMICSOURCE, INTERFACE_OPTIONAL, offsetof(CAudioRecorder, mDynamicSource)},
    {MPH_EQUALIZER, INTERFACE_DYNAMIC_OPTIONAL, offsetof(CAudioRecorder, mEqualizer)},
    {MPH_VISUALIZATION, INTERFACE_OPTIONAL, offsetof(CAudioRecorder, mVisualization)},
    {MPH_VOLUME, INTERFACE_OPTIONAL, offsetof(CAudioRecorder, mVolume)},
    {MPH_ANDROIDSIMPLEBUFFERQUEUE, INTERFACE_EXPLICIT, offsetof(CAudioRecorder, mBufferQueue)},
#ifdef ANDROID
    {MPH_ANDROIDCONFIGURATION, INTERFACE_EXPLICIT_PREREALIZE,
            offsetof(CAudioRecorder, mAndroidConfiguration)},
#endif
};

static const ClassTable CAudioRecorder_class = {
    AudioRecorder_interfaces,
    INTERFACES_AudioRecorder,
    MPH_to_AudioRecorder,
    "AudioRecorder",
    sizeof(CAudioRecorder),
    SL_OBJECTID_AUDIORECORDER,
    CAudioRecorder_Realize,
    CAudioRecorder_Resume,
    CAudioRecorder_Destroy,
    CAudioRecorder_PreDestroy
};

#endif


// Engine class

static const struct iid_vtable Engine_interfaces[INTERFACES_Engine] = {
    {MPH_OBJECT, INTERFACE_IMPLICIT, offsetof(CEngine, mObject)},
    {MPH_DYNAMICINTERFACEMANAGEMENT, INTERFACE_IMPLICIT_BASE,
        offsetof(CEngine, mDynamicInterfaceManagement)},
    {MPH_ENGINE, INTERFACE_IMPLICIT, offsetof(CEngine, mEngine)},
    {MPH_ENGINECAPABILITIES, INTERFACE_IMPLICIT_BASE, offsetof(CEngine, mEngineCapabilities)},
    {MPH_THREADSYNC, INTERFACE_IMPLICIT_BASE, offsetof(CEngine, mThreadSync)},
    {MPH_AUDIOIODEVICECAPABILITIES, INTERFACE_IMPLICIT_BASE,
        offsetof(CEngine, mAudioIODeviceCapabilities)},
    {MPH_AUDIODECODERCAPABILITIES, INTERFACE_EXPLICIT_BASE,
        offsetof(CEngine, mAudioDecoderCapabilities)},
    {MPH_AUDIOENCODERCAPABILITIES, INTERFACE_EXPLICIT_BASE,
        offsetof(CEngine, mAudioEncoderCapabilities)},
    {MPH_3DCOMMIT, INTERFACE_EXPLICIT_GAME, offsetof(CEngine, m3DCommit)},
    {MPH_DEVICEVOLUME, INTERFACE_OPTIONAL, offsetof(CEngine, mDeviceVolume)},
#ifdef ANDROID
    {MPH_ANDROIDEFFECTCAPABILITIES, INTERFACE_EXPLICIT,
        offsetof(CEngine, mAndroidEffectCapabilities)},
#endif
};

static const ClassTable CEngine_class = {
    Engine_interfaces,
    INTERFACES_Engine,
    MPH_to_Engine,
    "Engine",
    sizeof(CEngine),
    SL_OBJECTID_ENGINE,
    CEngine_Realize,
    CEngine_Resume,
    CEngine_Destroy,
    CEngine_PreDestroy
};


#if USE_PROFILES & USE_PROFILES_OPTIONAL

// LEDDevice class

static const struct iid_vtable LEDDevice_interfaces[INTERFACES_LEDDevice] = {
    {MPH_OBJECT, INTERFACE_IMPLICIT, offsetof(CLEDDevice, mObject)},
    {MPH_DYNAMICINTERFACEMANAGEMENT, INTERFACE_IMPLICIT,
        offsetof(CLEDDevice, mDynamicInterfaceManagement)},
    {MPH_LED, INTERFACE_IMPLICIT, offsetof(CLEDDevice, mLEDArray)},
};

static const ClassTable CLEDDevice_class = {
    LEDDevice_interfaces,
    INTERFACES_LEDDevice,
    MPH_to_LEDDevice,
    "LEDDevice",
    sizeof(CLEDDevice),
    SL_OBJECTID_LEDDEVICE,
    NULL,
    NULL,
    NULL,
    NULL
};

#endif


#if USE_PROFILES & USE_PROFILES_GAME

// Listener class

static const struct iid_vtable Listener_interfaces[INTERFACES_Listener] = {
    {MPH_OBJECT, INTERFACE_IMPLICIT, offsetof(CListener, mObject)},
    {MPH_DYNAMICINTERFACEMANAGEMENT, INTERFACE_IMPLICIT,
        offsetof(CListener, mDynamicInterfaceManagement)},
    {MPH_3DDOPPLER, INTERFACE_DYNAMIC_GAME, offsetof(CListener, m3DDoppler)},
    {MPH_3DLOCATION, INTERFACE_EXPLICIT_GAME, offsetof(CListener, m3DLocation)},
};

static const ClassTable CListener_class = {
    Listener_interfaces,
    INTERFACES_Listener,
    MPH_to_Listener,
    "Listener",
    sizeof(CListener),
    SL_OBJECTID_LISTENER,
    NULL,
    NULL,
    NULL,
    NULL
};

#endif


#if USE_PROFILES & (USE_PROFILES_GAME | USE_PROFILES_MUSIC)

// MetadataExtractor class

static const struct iid_vtable MetadataExtractor_interfaces[INTERFACES_MetadataExtractor] = {
    {MPH_OBJECT, INTERFACE_IMPLICIT, offsetof(CMetadataExtractor, mObject)},
    {MPH_DYNAMICINTERFACEMANAGEMENT, INTERFACE_IMPLICIT,
        offsetof(CMetadataExtractor, mDynamicInterfaceManagement)},
    {MPH_DYNAMICSOURCE, INTERFACE_IMPLICIT, offsetof(CMetadataExtractor, mDynamicSource)},
    {MPH_METADATAEXTRACTION, INTERFACE_IMPLICIT, offsetof(CMetadataExtractor, mMetadataExtraction)},
    {MPH_METADATATRAVERSAL, INTERFACE_IMPLICIT, offsetof(CMetadataExtractor, mMetadataTraversal)},
};

static const ClassTable CMetadataExtractor_class = {
    MetadataExtractor_interfaces,
    INTERFACES_MetadataExtractor,
    MPH_to_MetadataExtractor,
    "MetadataExtractor",
    sizeof(CMetadataExtractor),
    SL_OBJECTID_METADATAEXTRACTOR,
    NULL,
    NULL,
    NULL,
    NULL
};

#endif


#if USE_PROFILES & USE_PROFILES_GAME

// MidiPlayer class

static const struct iid_vtable MidiPlayer_interfaces[INTERFACES_MidiPlayer] = {
    {MPH_OBJECT, INTERFACE_IMPLICIT, offsetof(CMidiPlayer, mObject)},
    {MPH_DYNAMICINTERFACEMANAGEMENT, INTERFACE_IMPLICIT,
        offsetof(CMidiPlayer, mDynamicInterfaceManagement)},
    {MPH_PLAY, INTERFACE_IMPLICIT, offsetof(CMidiPlayer, mPlay)},
    {MPH_3DDOPPLER, INTERFACE_DYNAMIC_GAME, offsetof(C3DGroup, m3DDoppler)},
    {MPH_3DGROUPING, INTERFACE_EXPLICIT_GAME, offsetof(CMidiPlayer, m3DGrouping)},
    {MPH_3DLOCATION, INTERFACE_EXPLICIT_GAME, offsetof(CMidiPlayer, m3DLocation)},
    {MPH_3DSOURCE, INTERFACE_EXPLICIT_GAME, offsetof(CMidiPlayer, m3DSource)},
    {MPH_BUFFERQUEUE, INTERFACE_EXPLICIT_GAME, offsetof(CMidiPlayer, mBufferQueue)},
    {MPH_EFFECTSEND, INTERFACE_EXPLICIT_GAME, offsetof(CMidiPlayer, mEffectSend)},
    {MPH_MUTESOLO, INTERFACE_EXPLICIT_GAME, offsetof(CMidiPlayer, mMuteSolo)},
    {MPH_METADATAEXTRACTION, INTERFACE_DYNAMIC_GAME, offsetof(CMidiPlayer, mMetadataExtraction)},
    {MPH_METADATATRAVERSAL, INTERFACE_DYNAMIC_GAME, offsetof(CMidiPlayer, mMetadataTraversal)},
    {MPH_MIDIMESSAGE, INTERFACE_EXPLICIT_GAME_PHONE, offsetof(CMidiPlayer, mMIDIMessage)},
    {MPH_MIDITIME, INTERFACE_EXPLICIT_GAME_PHONE, offsetof(CMidiPlayer, mMIDITime)},
    {MPH_MIDITEMPO, INTERFACE_EXPLICIT_GAME_PHONE, offsetof(CMidiPlayer, mMIDITempo)},
    {MPH_MIDIMUTESOLO, INTERFACE_EXPLICIT_GAME, offsetof(CMidiPlayer, mMIDIMuteSolo)},
    {MPH_PREFETCHSTATUS, INTERFACE_EXPLICIT_GAME_PHONE, offsetof(CMidiPlayer, mPrefetchStatus)},
    {MPH_SEEK, INTERFACE_EXPLICIT_GAME_PHONE, offsetof(CMidiPlayer, mSeek)},
    {MPH_VOLUME, INTERFACE_EXPLICIT_GAME_PHONE, offsetof(CMidiPlayer, mVolume)},
    {MPH_3DMACROSCOPIC, INTERFACE_OPTIONAL, offsetof(CMidiPlayer, m3DMacroscopic)},
    {MPH_BASSBOOST, INTERFACE_DYNAMIC_OPTIONAL, offsetof(CMidiPlayer, mBassBoost)},
    {MPH_DYNAMICSOURCE, INTERFACE_OPTIONAL, offsetof(CMidiPlayer, mDynamicSource)},
    {MPH_ENVIRONMENTALREVERB, INTERFACE_DYNAMIC_OPTIONAL,
        offsetof(CMidiPlayer, mEnvironmentalReverb)},
    {MPH_EQUALIZER, INTERFACE_DYNAMIC_OPTIONAL, offsetof(CMidiPlayer, mEqualizer)},
    {MPH_PITCH, INTERFACE_DYNAMIC_OPTIONAL, offsetof(CMidiPlayer, mPitch)},
    {MPH_PRESETREVERB, INTERFACE_DYNAMIC_OPTIONAL, offsetof(CMidiPlayer, mPresetReverb)},
    {MPH_PLAYBACKRATE, INTERFACE_DYNAMIC_OPTIONAL, offsetof(CMidiPlayer, mPlaybackRate)},
    {MPH_VIRTUALIZER, INTERFACE_DYNAMIC_OPTIONAL, offsetof(CMidiPlayer, mVirtualizer)},
    {MPH_VISUALIZATION, INTERFACE_OPTIONAL, offsetof(CMidiPlayer, mVisualization)},
};

static const ClassTable CMidiPlayer_class = {
    MidiPlayer_interfaces,
    INTERFACES_MidiPlayer,
    MPH_to_MidiPlayer,
    "MidiPlayer",
    sizeof(CMidiPlayer),
    SL_OBJECTID_MIDIPLAYER,
    NULL,
    NULL,
    NULL,
    NULL
};

#endif


// OutputMix class

static const struct iid_vtable OutputMix_interfaces[INTERFACES_OutputMix] = {
    {MPH_OBJECT, INTERFACE_IMPLICIT, offsetof(COutputMix, mObject)},
    {MPH_DYNAMICINTERFACEMANAGEMENT, INTERFACE_IMPLICIT_BASE,
        offsetof(COutputMix, mDynamicInterfaceManagement)},
    {MPH_OUTPUTMIX, INTERFACE_IMPLICIT, offsetof(COutputMix, mOutputMix)},
#ifdef USE_OUTPUTMIXEXT
    // FIXME should be internal implicit (available, but not advertised
    // publicly or included in possible interface count)
    {MPH_OUTPUTMIXEXT, INTERFACE_IMPLICIT, offsetof(COutputMix, mOutputMixExt)},
#else
    {MPH_OUTPUTMIXEXT, INTERFACE_UNAVAILABLE, 0},
#endif
    {MPH_ENVIRONMENTALREVERB, INTERFACE_DYNAMIC_GAME, offsetof(COutputMix, mEnvironmentalReverb)},
    {MPH_EQUALIZER, INTERFACE_DYNAMIC_GAME_MUSIC, offsetof(COutputMix, mEqualizer)},
    {MPH_PRESETREVERB, INTERFACE_DYNAMIC_MUSIC, offsetof(COutputMix, mPresetReverb)},
    {MPH_VIRTUALIZER, INTERFACE_DYNAMIC_GAME_MUSIC, offsetof(COutputMix, mVirtualizer)},
    // The overall Volume interface is explicit,
    // but portions of Volume are mandated only in Game and Music profiles
    {MPH_VOLUME, INTERFACE_EXPLICIT, offsetof(COutputMix, mVolume)},
    {MPH_BASSBOOST, INTERFACE_DYNAMIC_OPTIONAL, offsetof(COutputMix, mBassBoost)},
    {MPH_VISUALIZATION, INTERFACE_OPTIONAL, offsetof(COutputMix, mVisualization)},
#ifdef ANDROID
    {MPH_ANDROIDEFFECT, INTERFACE_EXPLICIT, offsetof(COutputMix, mAndroidEffect)},
#endif
};

static const ClassTable COutputMix_class = {
    OutputMix_interfaces,
    INTERFACES_OutputMix,
    MPH_to_OutputMix,
    "OutputMix",
    sizeof(COutputMix),
    SL_OBJECTID_OUTPUTMIX,
    COutputMix_Realize,
    COutputMix_Resume,
    COutputMix_Destroy,
    COutputMix_PreDestroy
};


#if USE_PROFILES & USE_PROFILES_OPTIONAL

// Vibra class

static const struct iid_vtable VibraDevice_interfaces[INTERFACES_VibraDevice] = {
    {MPH_OBJECT, INTERFACE_IMPLICIT, offsetof(CVibraDevice, mObject)},
    {MPH_DYNAMICINTERFACEMANAGEMENT, INTERFACE_IMPLICIT,
        offsetof(CVibraDevice, mDynamicInterfaceManagement)},
    {MPH_VIBRA, INTERFACE_IMPLICIT, offsetof(CVibraDevice, mVibra)},
};

static const ClassTable CVibraDevice_class = {
    VibraDevice_interfaces,
    INTERFACES_VibraDevice,
    MPH_to_Vibra,
    "VibraDevice",
    sizeof(CVibraDevice),
    SL_OBJECTID_VIBRADEVICE,
    NULL,
    NULL,
    NULL,
    NULL
};

#endif


static const ClassTable * const classes[] = {
    // Do not change order of these entries; they are in numerical order
    &CEngine_class,
#if USE_PROFILES & USE_PROFILES_OPTIONAL
    &CLEDDevice_class,
    &CVibraDevice_class,
#else
    NULL,
    NULL,
#endif
    &CAudioPlayer_class,
#if (USE_PROFILES & USE_PROFILES_OPTIONAL) || defined(ANDROID)
    &CAudioRecorder_class,
#else
    NULL,
#endif
#if USE_PROFILES & (USE_PROFILES_GAME | USE_PROFILES_PHONE)
    &CMidiPlayer_class,
#else
    NULL,
#endif
#if USE_PROFILES & USE_PROFILES_GAME
    &CListener_class,
    &C3DGroup_class,
#else
    NULL,
    NULL,
#endif
    &COutputMix_class,
#if USE_PROFILES & (USE_PROFILES_GAME | USE_PROFILES_MUSIC)
    &CMetadataExtractor_class
#else
    NULL
#endif
};


/* \brief Map SL_OBJECTID to class or NULL if object ID not supported */

const ClassTable *objectIDtoClass(SLuint32 objectID)
{
    // object ID is the engine and always present
    assert(NULL != classes[0]);
    SLuint32 objectID0 = classes[0]->mObjectID;
    if ((objectID0 <= objectID) && ((objectID0 + sizeof(classes)/sizeof(classes[0])) > objectID)) {
        return classes[objectID - objectID0];
    }
    return NULL;
}
