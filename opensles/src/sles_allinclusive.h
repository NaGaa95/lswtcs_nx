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

/** \file sles_allinclusive.h Everything including the kitchen sink */

#include "SLES/OpenSLES.h"
#include "SLES/OpenSLES_Android.h"
#include <stddef.h> // offsetof
#include <stdlib.h> // malloc
#include <string.h> // memcmp
#include <stdio.h>  // debugging
#include <assert.h> // debugging
#include <pthread.h>
#include <unistd.h> // usleep
#include <errno.h>

#ifndef __cplusplus
typedef int bool;
#ifndef false
#define false 0
#endif
#ifndef true
#define true 1
#endif
#endif

// The OpenSLES.h definitions of SL_PROFILES_... have casts, so are unusable by preprocessor
#define USE_PROFILES_PHONE    0x1   // == SL_PROFILES_PHONE
#define USE_PROFILES_MUSIC    0x2   // == SL_PROFILES_MUSIC
#define USE_PROFILES_GAME     0x4   // == SL_PROFILES_GAME
// Pseudo profiles, used to decide whether to include code for incomplete or untested features
// Features that are not in union of all profiles: audio recorder, LED, Vibra
#define USE_PROFILES_OPTIONAL 0x8
// Features that are in the intersection of all profiles:
// object priorities, preemption, loss of control, device configuration
#define USE_PROFILES_BASE     0x10

#include "MPH.h"
#include "MPH_to.h"
#include "devices.h"
#include "OpenSLESUT.h"
#include "ThreadPool.h"

typedef struct CAudioPlayer_struct CAudioPlayer;
typedef struct CAudioRecorder_struct CAudioRecorder;
typedef struct C3DGroup_struct C3DGroup;
typedef struct COutputMix_struct COutputMix;

#ifdef USE_SNDFILE
#include <sndfile.h>
#include "SLSndFile.h"
#endif // USE_SNDFILE

//#ifdef USE_SDL
//#include <SDL/SDL_audio.h>
//#endif // USE_SDL

#ifdef ANDROID
#include <utils/Log.h>
#include <utils/KeyedVector.h>
#include "SLES/OpenSLES_AndroidConfiguration.h"
#include "media/AudioSystem.h"
#include "media/mediarecorder.h"
#include "media/AudioRecord.h"
#include "media/AudioTrack.h"
#include "media/mediaplayer.h"
#ifndef USE_BACKPORT
#include "media/AudioEffect.h"
#include "media/EffectApi.h"
#include "media/EffectEqualizerApi.h"
#include "media/EffectBassBoostApi.h"
#include "media/EffectVirtualizerApi.h"
#include "media/EffectPresetReverbApi.h"
#include "media/EffectEnvironmentalReverbApi.h"
#endif
#include <utils/String8.h>
#define ANDROID_SL_MILLIBEL_MAX 0
#include <binder/ProcessState.h>
#include "android_sles_conversions.h"
#ifndef USE_BACKPORT
#include "android_SfPlayer.h"
#include "android_OutputMix.h"
#endif
#include "android_AudioRecorder.h"
#endif

#ifdef LSWTCS
#include <psp2/types.h>
#endif

#define STEREO_CHANNELS 2

#ifdef USE_OUTPUTMIXEXT
#include "OutputMixExt.h"
#endif

#include "sllog.h"

// Hook functions

typedef void (*VoidHook)(void *self);
typedef SLresult (*StatusHook)(void *self);
typedef SLresult (*AsyncHook)(void *self, SLboolean async);
typedef bool (*BoolHook)(void *self);

// Describes how an interface is related to a given class, used in iid_vtable::mInterface

#define INTERFACE_IMPLICIT            0 // no need for application to request prior to GetInterface
#define INTERFACE_EXPLICIT            1 // must be requested explicitly during object creation
#define INTERFACE_DYNAMIC             2 // can be requested after object creation
#define INTERFACE_UNAVAILABLE         3 // this interface is not available on objects of this class
#define INTERFACE_EXPLICIT_PREREALIZE 4 // explicit, and can call GetInterface before Realize
// note that INTERFACE_OPTIONAL is always re-mapped to one of the above

// Profile-specific interfaces

#if USE_PROFILES & USE_PROFILES_BASE
#define INTERFACE_IMPLICIT_BASE       INTERFACE_IMPLICIT
#define INTERFACE_EXPLICIT_BASE       INTERFACE_EXPLICIT
#else
#define INTERFACE_IMPLICIT_BASE       INTERFACE_UNAVAILABLE
#define INTERFACE_EXPLICIT_BASE       INTERFACE_UNAVAILABLE
#endif

#if USE_PROFILES & USE_PROFILES_GAME
#define INTERFACE_DYNAMIC_GAME        INTERFACE_DYNAMIC
#define INTERFACE_EXPLICIT_GAME       INTERFACE_EXPLICIT
#else
#define INTERFACE_DYNAMIC_GAME        INTERFACE_OPTIONAL
#define INTERFACE_EXPLICIT_GAME       INTERFACE_OPTIONAL
#endif

#if USE_PROFILES & USE_PROFILES_MUSIC
#define INTERFACE_DYNAMIC_MUSIC       INTERFACE_DYNAMIC
#else
#define INTERFACE_DYNAMIC_MUSIC       INTERFACE_OPTIONAL
#endif

#if USE_PROFILES & (USE_PROFILES_GAME | USE_PROFILES_MUSIC)
#define INTERFACE_DYNAMIC_GAME_MUSIC  INTERFACE_DYNAMIC
#define INTERFACE_EXPLICIT_GAME_MUSIC INTERFACE_EXPLICIT
#else
#define INTERFACE_DYNAMIC_GAME_MUSIC  INTERFACE_OPTIONAL
#define INTERFACE_EXPLICIT_GAME_MUSIC INTERFACE_OPTIONAL
#endif

#if USE_PROFILES & (USE_PROFILES_GAME | USE_PROFILES_PHONE)
#define INTERFACE_EXPLICIT_GAME_PHONE INTERFACE_EXPLICIT
#else
#define INTERFACE_EXPLICIT_GAME_PHONE INTERFACE_OPTIONAL
#endif

#if USE_PROFILES & USE_PROFILES_OPTIONAL
#define INTERFACE_OPTIONAL            INTERFACE_EXPLICIT
#define INTERFACE_DYNAMIC_OPTIONAL    INTERFACE_DYNAMIC
#else
#define INTERFACE_OPTIONAL            INTERFACE_UNAVAILABLE
#define INTERFACE_DYNAMIC_OPTIONAL    INTERFACE_UNAVAILABLE
#endif

// Describes how an interface is related to a given object

#define INTERFACE_UNINITIALIZED 1  ///< /not requested at object creation time
#define INTERFACE_EXPOSED       2  ///< /requested at object creation time
#define INTERFACE_ADDING_1      3  ///< /part 1 of asynchronous AddInterface, pending
#define INTERFACE_ADDING_2      4  ///< /synchronous AddInterface, or part 2 of asynchronous
#define INTERFACE_ADDED         5  ///< /AddInterface has completed
#define INTERFACE_REMOVING      6  ///< /unlocked phase of (synchronous) RemoveInterface
#define INTERFACE_SUSPENDING    7  ///< /suspend in progress
#define INTERFACE_SUSPENDED     8  ///< /suspend has completed
#define INTERFACE_RESUMING_1    9  ///< /part 1 of asynchronous ResumeInterface, pending
#define INTERFACE_RESUMING_2   10  ///< /synchronous ResumeInterface, or part 2 of asynchronous
#define INTERFACE_ADDING_1A    11  ///< /part 1 of asynchronous AddInterface, aborted
#define INTERFACE_RESUMING_1A  12  ///< /part 1 of asynchronous ResumeInterface, aborted


// Maps an interface ID to its offset within the class that exposes it

struct iid_vtable {
    unsigned char mMPH;         // primary MPH for this interface, does not include any aliases
    unsigned char mInterface;   // relationship of interface to this class
    /*size_t*/ unsigned short mOffset;
};

// Per-class const data shared by all instances of the same class

typedef struct {
    const struct iid_vtable *mInterfaces;   // maps interface index to info about that interface
    SLuint32 mInterfaceCount;  // number of possible interfaces
    const signed char *mMPH_to_index;
    const char * const mName;
    size_t mSize;
    SLuint32 mObjectID;
    // hooks
    AsyncHook mRealize;
    AsyncHook mResume;
    VoidHook mDestroy;
    BoolHook mPreDestroy;
} ClassTable;

// BufferHeader describes each element of a BufferQueue, other than the data

typedef struct {
    const void *mBuffer;
    SLuint32 mSize;
} BufferHeader;

#ifdef __cplusplus
#define this this_
#endif

#ifdef USE_SNDFILE

#define SndFile_BUFSIZE 512     // in 16-bit samples
#define SndFile_NUMBUFS 2

struct SndFile {
    // save URI also?
    SLchar *mPathname;
    SNDFILE *mSNDFILE;
    SF_INFO mSfInfo;
    pthread_mutex_t mMutex; // protects mSNDFILE only
    SLboolean mEOF;         // sf_read returned zero sample frames
    SLuint32 mWhich;        // which buffer to use next
    short mBuffer[SndFile_BUFSIZE * SndFile_NUMBUFS];
};

#endif // USE_SNDFILE

/* Our own merged version of SLDataSource and SLDataSink */

typedef union {
    SLuint32 mLocatorType;
    SLDataLocator_Address mAddress;
    SLDataLocator_BufferQueue mBufferQueue;
    SLDataLocator_IODevice mIODevice;
    SLDataLocator_MIDIBufferQueue mMIDIBufferQueue;
    SLDataLocator_OutputMix mOutputMix;
    SLDataLocator_URI mURI;
    SLDataLocator_AndroidFD mFD;
} DataLocator;

typedef union {
    SLuint32 mFormatType;
    SLDataFormat_PCM mPCM;
    SLDataFormat_MIME mMIME;
} DataFormat;

typedef struct {
    union {
        SLDataSource mSource;
        SLDataSink mSink;
        struct {
            DataLocator *pLocator;
            DataFormat *pFormat;
        } mNeutral;
    } u;
    DataLocator mLocator;
    DataFormat mFormat;
} DataLocatorFormat;

/* Interface structures */

typedef struct Object_interface {
    const struct SLObjectItf_ *mItf;    // const
    // field mThis would be redundant within an IObject, so we substitute mEngine
    struct Engine_interface *mEngine;   // const
    const ClassTable *mClass;       // const
    SLuint32 mInstanceID;           // const for debugger and for RPC, 0 means unpublished
    slObjectCallback mCallback;
    void *mContext;
    unsigned mGottenMask;           ///< bit-mask of interfaces exposed or added, then gotten
    unsigned mLossOfControlMask;    // interfaces with loss of control enabled
    unsigned mAttributesMask;       // attributes which have changed since last sync
#if USE_PROFILES & USE_PROFILES_BASE
    SLint32 mPriority;
#endif
    pthread_mutex_t mMutex;
#ifdef USE_DEBUG
    pthread_t mOwner;
    const char *mFile;
    int mLine;
#endif
    pthread_cond_t mCond;
    SLuint8 mState;                 // really SLuint32, but SLuint8 to save space
#if USE_PROFILES & USE_PROFILES_BASE
    SLuint8 mPreemptable;           // really SLboolean, but SLuint8 to save space
#else
    SLuint8 mPadding;
#endif
    SLuint8 mStrongRefCount;        // number of strong references to this object
    // (object cannot be destroyed as long as > 0, and referrers _prefer_ it stay in Realized state)
    // for best alignment, do not add any fields here
#define INTERFACES_Default 1
    SLuint8 mInterfaceStates[INTERFACES_Default];    // state of each of interface
    // do not add any fields here
} IObject;

#include "locks.h"

typedef struct {
    const struct SL3DCommitItf_ *mItf;
    IObject *mThis;
    SLboolean mDeferred;
    SLuint32 mGeneration;   // incremented each master clock cycle
    SLuint32 mWaiting;      // number of threads waiting in Commit
} I3DCommit;

enum CartesianSphericalActive {
    CARTESIAN_COMPUTED_SPHERICAL_SET,
    CARTESIAN_REQUESTED_SPHERICAL_SET,
    CARTESIAN_UNKNOWN_SPHERICAL_SET,
    CARTESIAN_SET_SPHERICAL_COMPUTED,   // not in 1.0.1
    CARTESIAN_SET_SPHERICAL_REQUESTED,  // not in 1.0.1
    CARTESIAN_SET_SPHERICAL_UNKNOWN
};

typedef struct {
    const struct SL3DDopplerItf_ *mItf;
    IObject *mThis;
    // The API allows client to specify either Cartesian and spherical velocities.
    // But an implementation will likely prefer one or the other. So for
    // maximum portablity, we maintain both units and an indication of which
    // unit was set most recently. In addition, we keep a flag saying whether
    // the other unit has been derived yet. It can take significant time
    // to compute the other unit, so this may be deferred to another thread.
    // For this reason we also keep an indication of whether the secondary
    // has been computed yet, and its accuracy.
    // Though only one unit is primary at a time, a union is inappropriate:
    // the application might read in both units (not in 1.0.1),
    // and due to multi-threading concerns.
    SLVec3D mVelocityCartesian;
    struct {
        SLmillidegree mAzimuth;
        SLmillidegree mElevation;
        SLmillidegree mSpeed;
    } mVelocitySpherical;
    enum CartesianSphericalActive mVelocityActive;
    SLpermille mDopplerFactor;
} I3DDoppler;

typedef struct {
    const struct SL3DGroupingItf_ *mItf;
    IObject *mThis;
    C3DGroup *mGroup;   // strong reference to associated group or NULL
} I3DGrouping;

enum AnglesVectorsActive {
    ANGLES_COMPUTED_VECTORS_SET,    // not in 1.0.1
    ANGLES_REQUESTED_VECTORS_SET,   // not in 1.0.1
    ANGLES_UNKNOWN_VECTORS_SET,
    ANGLES_SET_VECTORS_COMPUTED,
    ANGLES_SET_VECTORS_REQUESTED,
    ANGLES_SET_VECTORS_UNKNOWN
};

typedef struct {
    const struct SL3DLocationItf_ *mItf;
    IObject *mThis;
    SLVec3D mLocationCartesian;
    struct {
        SLmillidegree mAzimuth;
        SLmillidegree mElevation;
        SLmillimeter mDistance;
    } mLocationSpherical;
    enum CartesianSphericalActive mLocationActive;
    struct {
        SLmillidegree mHeading;
        SLmillidegree mPitch;
        SLmillidegree mRoll;
    } mOrientationAngles;
    struct {
        SLVec3D mFront;
        SLVec3D mAbove;
        SLVec3D mUp;
    } mOrientationVectors;
    enum AnglesVectorsActive mOrientationActive;
    // Rotations can be slow, so are deferred.
    SLmillidegree mTheta;
    SLVec3D mAxis;
    SLboolean mRotatePending;
} I3DLocation;

typedef struct {
    const struct SL3DMacroscopicItf_ *mItf;
    IObject *mThis;
    struct {
        SLmillimeter mWidth;
        SLmillimeter mHeight;
        SLmillimeter mDepth;
    } mSize;
    struct {
        SLmillimeter mHeading;
        SLmillimeter mPitch;
        SLmillimeter mRoll;
    } mOrientationAngles;
    struct {
        SLVec3D mFront;
        SLVec3D mAbove;
        SLVec3D mUp;
    } mOrientationVectors;
    enum AnglesVectorsActive mOrientationActive;
    // Rotations can be slow, so are deferred.
    SLmillidegree mTheta;
    SLVec3D mAxis;
    SLboolean mRotatePending;
} I3DMacroscopic;

typedef struct {
    const struct SL3DSourceItf_ *mItf;
    IObject *mThis;
    SLboolean mHeadRelative;
    SLboolean mRolloffMaxDistanceMute;
    SLmillimeter mMaxDistance;
    SLmillimeter mMinDistance;
    SLmillidegree mConeInnerAngle;
    SLmillidegree mConeOuterAngle;
    SLmillibel mConeOuterLevel;
    SLpermille mRolloffFactor;
    SLpermille mRoomRolloffFactor;
    SLuint8 mDistanceModel;
} I3DSource;

typedef struct {
    const struct SLAudioDecoderCapabilitiesItf_ *mItf;
    IObject *mThis;
} IAudioDecoderCapabilities;

typedef struct {
    const struct SLAudioEncoderItf_ *mItf;
    IObject *mThis;
    SLAudioEncoderSettings mSettings;
} IAudioEncoder;

typedef struct {
    const struct SLAudioEncoderCapabilitiesItf_ *mItf;
    IObject *mThis;
} IAudioEncoderCapabilities;

typedef struct {
    const struct SLAudioIODeviceCapabilitiesItf_ *mItf;
    IObject *mThis;
    slAvailableAudioInputsChangedCallback mAvailableAudioInputsChangedCallback;
    void *mAvailableAudioInputsChangedContext;
    slAvailableAudioOutputsChangedCallback mAvailableAudioOutputsChangedCallback;
    void *mAvailableAudioOutputsChangedContext;
    slDefaultDeviceIDMapChangedCallback mDefaultDeviceIDMapChangedCallback;
    void *mDefaultDeviceIDMapChangedContext;
} IAudioIODeviceCapabilities;

typedef struct {
    const struct SLBassBoostItf_ *mItf;
    IObject *mThis;
    SLboolean mEnabled;
    SLpermille mStrength;
#if defined(ANDROID) && !defined(USE_BACKPORT)
    effect_descriptor_t mBassBoostDescriptor;
    android::sp<android::AudioEffect> mBassBoostEffect;
#endif
} IBassBoost;

typedef struct BufferQueue_interface {
    const struct SLBufferQueueItf_ *mItf;
    IObject *mThis;
    SLBufferQueueState mState;
    slBufferQueueCallback mCallback;
    void *mContext;
    // originally SLuint32, but range-checked down to SLuint16
    SLuint16 mNumBuffers;
    /*SLboolean*/ SLuint16 mClearRequested;
    BufferHeader *mArray;
    BufferHeader *mFront, *mRear;
#ifdef ANDROID
    SLuint32 mSizeConsumed;
#endif
    // saves a malloc in the typical case
#define BUFFER_HEADER_TYPICAL 4
    BufferHeader mTypical[BUFFER_HEADER_TYPICAL+1];
	uint32_t samplerate;
	int channels;
	int bps;
} IBufferQueue;

#define MAX_DEVICE 2

typedef struct {
    const struct SLDeviceVolumeItf_ *mItf;
    IObject *mThis;
    SLint32 mVolume[MAX_DEVICE];
} IDeviceVolume;

typedef struct {
    const struct SLDynamicInterfaceManagementItf_ *mItf;
    IObject *mThis;
    slDynamicInterfaceManagementCallback mCallback;
    void *mContext;
} IDynamicInterfaceManagement;

typedef struct {
    const struct SLDynamicSourceItf_ *mItf;
    IObject *mThis;
    SLDataSource *mDataSource;
} IDynamicSource;

// private

struct EnableLevel {
    SLboolean mEnable;
    SLmillibel mSendLevel;
};

// indexes into IEffectSend.mEnableLevels

#define AUX_ENVIRONMENTALREVERB 0
#define AUX_PRESETREVERB        1
#define AUX_MAX                 2

typedef struct {
    const struct SLEffectSendItf_ *mItf;
    IObject *mThis;
    struct EnableLevel mEnableLevels[AUX_MAX];  // wet enable and volume per effect type
} IEffectSend;

typedef struct Engine_interface {
    const struct SLEngineItf_ *mItf;
    IObject *mThis;
    SLboolean mLossOfControlGlobal;
#ifdef USE_SDL
    COutputMix *mOutputMix; // SDL pulls PCM from an arbitrary IOutputMixExt
#endif
    // Each engine is its own universe.
    SLuint32 mInstanceCount;
    unsigned mInstanceMask; // 1 bit per active object
    unsigned mChangedMask;  // objects which have changed since last sync
#define MAX_INSTANCE 32     // maximum active objects per engine, see mInstanceMask
    IObject *mInstances[MAX_INSTANCE];
    SLboolean mShutdown;
    SLboolean mShutdownAck;
    ThreadPool mThreadPool; // for asynchronous operations
#if defined(ANDROID) && !defined(USE_BACKPORT)
    // FIXME number of presets will only be saved in IEqualizer, preset names will not be stored
    SLuint32 mEqNumPresets;
    char** mEqPresetNames;
#endif
} IEngine;

typedef struct {
    const struct SLEngineCapabilitiesItf_ *mItf;
    IObject *mThis;
    SLboolean mThreadSafe;
    // const
    SLuint32 mMaxIndexLED;
    SLuint32 mMaxIndexVibra;
} IEngineCapabilities;

typedef struct {
    const struct SLEnvironmentalReverbItf_ *mItf;
    IObject *mThis;
    SLEnvironmentalReverbSettings mProperties;
#if defined(ANDROID) && !defined(USE_BACKPORT)
    effect_descriptor_t mEnvironmentalReverbDescriptor;
    android::sp<android::AudioEffect> mEnvironmentalReverbEffect;
#endif
} IEnvironmentalReverb;

struct EqualizerBand {
    SLmilliHertz mMin;
    SLmilliHertz mCenter;
    SLmilliHertz mMax;
};

#if defined(ANDROID) && !defined(USE_BACKPORT)
#define MAX_EQ_BANDS 0
#else
#define MAX_EQ_BANDS 4  // compile-time limit, runtime limit may be smaller
#endif

typedef struct {
    const struct SLEqualizerItf_ *mItf;
    IObject *mThis;
    SLboolean mEnabled;
    SLuint16 mPreset;
    SLmillibel mLevels[MAX_EQ_BANDS];
    // const to end of struct
    SLuint16 mNumPresets;
    SLuint16 mNumBands;
    const struct EqualizerBand *mBands;
    const struct EqualizerPreset *mPresets;
    SLmillibel mBandLevelRangeMin;
    SLmillibel mBandLevelRangeMax;
#if defined(ANDROID) && !defined(USE_BACKPORT)
    effect_descriptor_t mEqDescriptor;
    android::sp<android::AudioEffect> mEqEffect;
#endif
} IEqualizer;

#define MAX_LED_COUNT 32

typedef struct {
    const struct SLLEDArrayItf_ *mItf;
    IObject *mThis;
    SLuint32 mLightMask;
    SLHSL mColors[MAX_LED_COUNT];
    // const
    SLuint8 mCount;
} ILEDArray;

typedef struct {
    const struct SLMetadataExtractionItf_ *mItf;
    IObject *mThis;
    SLuint32 mKeySize;
    const void *mKey;
    SLuint32 mKeyEncoding;
    const SLchar *mValueLangCountry;
    SLuint32 mValueEncoding;
    SLuint8 mFilterMask;
    int mKeyFilter;
} IMetadataExtraction;

typedef struct {
    const struct SLMetadataTraversalItf_ *mItf;
    IObject *mThis;
    SLuint32 mIndex;
    SLuint32 mMode;
    SLuint32 mCount;
    SLuint32 mSize;
} IMetadataTraversal;

typedef struct {
    const struct SLMIDIMessageItf_ *mItf;
    IObject *mThis;
    slMetaEventCallback mMetaEventCallback;
    void *mMetaEventContext;
    slMIDIMessageCallback mMessageCallback;
    void *mMessageContext;
    SLuint8 mMessageTypes;
} IMIDIMessage;

typedef struct {
    const struct SLMIDIMuteSoloItf_ *mItf;
    IObject *mThis;
    SLuint16 mChannelMuteMask;
    SLuint16 mChannelSoloMask;
    SLuint32 mTrackMuteMask;
    SLuint32 mTrackSoloMask;
    // const
    SLuint16 mTrackCount;
} IMIDIMuteSolo;

typedef struct {
    const struct SLMIDITempoItf_ *mItf;
    IObject *mThis;
    SLuint32 mTicksPerQuarterNote;
    SLuint32 mMicrosecondsPerQuarterNote;
} IMIDITempo;

typedef struct {
    const struct SLMIDITimeItf_ *mItf;
    IObject *mThis;
    SLuint32 mDuration;
    SLuint32 mPosition;
    SLuint32 mStartTick;
    SLuint32 mNumTicks;
} IMIDITime;

typedef struct {
    const struct SLMuteSoloItf_ *mItf;
    IObject *mThis;
    // fields that were formerly here are now at CAudioPlayer
} IMuteSolo;

#define MAX_TRACK 32        // see mActiveMask

typedef struct {
    const struct SLOutputMixItf_ *mItf;
    IObject *mThis;
    slMixDeviceChangeCallback mCallback;
    void *mContext;
} IOutputMix;

#ifdef USE_OUTPUTMIXEXT
typedef struct {
    const struct SLOutputMixExtItf_ *mItf;
    IObject *mThis;
    unsigned mActiveMask;   // 1 bit per active track
    Track mTracks[MAX_TRACK];
    SLboolean mDestroyRequested;    ///< Mixer to acknowledge application's call to Object::Destroy
} IOutputMixExt;
#endif

typedef struct {
    const struct SLPitchItf_ *mItf;
    IObject *mThis;
    SLpermille mPitch;
    // const
    SLpermille mMinPitch;
    SLpermille mMaxPitch;
} IPitch;

typedef struct Play_interface {
    const struct SLPlayItf_ *mItf;
    IObject *mThis;
    SLuint32 mState;
    // next 2 fields are read-only to application
    SLmillisecond mDuration;
    SLmillisecond mPosition;
    slPlayCallback mCallback;
    void *mContext;
    SLuint32 mEventFlags;
    // the ISeek trick of using a distinct value doesn't work here because it's readable by app
    SLmillisecond mMarkerPosition;
    SLmillisecond mPositionUpdatePeriod; // Zero means do not do position updates (FIXME ~0)
#ifdef USE_OUTPUTMIXEXT
    SLuint32 mFrameUpdatePeriod;         // mPositionUpdatePeriod in frame units
    SLmillisecond mLastSeekPosition;     // Last known accurate position, set at Seek
    SLuint32 mFramesSinceLastSeek;       // Frames mixed since last known accurate position
    SLuint32 mFramesSincePositionUpdate; // Frames mixed since last position update callback
#endif
#ifdef LSWTCS
    SceUInt32 mLastTick;
#endif
} IPlay;

typedef struct {
    const struct SLPlaybackRateItf_ *mItf;
    IObject *mThis;
    SLpermille mRate;
    SLuint32 mProperties;
    // const
    SLpermille mMinRate;
    SLpermille mMaxRate;
    SLpermille mStepSize;
    SLuint32 mCapabilities;
} IPlaybackRate;

typedef struct {
    const struct SLPrefetchStatusItf_ *mItf;
    IObject *mThis;
    SLuint32 mStatus;
    SLpermille mLevel;
    slPrefetchCallback mCallback;
    void *mContext;
    SLuint32 mCallbackEventsMask;
    SLpermille mFillUpdatePeriod;
} IPrefetchStatus;

typedef struct {
    const struct SLPresetReverbItf_ *mItf;
    IObject *mThis;
    SLuint16 mPreset;
#if defined(ANDROID) && !defined(USE_BACKPORT)
    effect_descriptor_t mPresetReverbDescriptor;
    android::sp<android::AudioEffect> mPresetReverbEffect;
#endif
} IPresetReverb;

typedef struct {
    const struct SLRatePitchItf_ *mItf;
    IObject *mThis;
    SLpermille mRate;
    // const
    SLpermille mMinRate;
    SLpermille mMaxRate;
} IRatePitch;

typedef struct {
    const struct SLRecordItf_ *mItf;
    IObject *mThis;
    SLuint32 mState;
    SLmillisecond mDurationLimit;
    SLmillisecond mPosition;
    slRecordCallback mCallback;
    void *mContext;
    SLuint32 mCallbackEventsMask;
    SLmillisecond mMarkerPosition;
    SLmillisecond mPositionUpdatePeriod;
} IRecord;

typedef struct {
    const struct SLSeekItf_ *mItf;
    IObject *mThis;
    SLmillisecond mPos;     // mPos != SL_TIME_UNKNOWN means pending seek request
    SLboolean mLoopEnabled;
    SLmillisecond mStartPos;
    SLmillisecond mEndPos;
} ISeek;

typedef struct {
    const struct SLThreadSyncItf_ *mItf;
    IObject *mThis;
    SLboolean mInCriticalSection;
    SLuint32 mWaiting;  // number of threads waiting
    pthread_t mOwner;
} IThreadSync;

typedef struct {
    const struct SLVibraItf_ *mItf;
    IObject *mThis;
    SLboolean mVibrate;
    SLmilliHertz mFrequency;
    SLpermille mIntensity;
} IVibra;

typedef struct {
    const struct SLVirtualizerItf_ *mItf;
    IObject *mThis;
    SLboolean mEnabled;
    SLpermille mStrength;
#if defined(ANDROID) && !defined(USE_BACKPORT)
    effect_descriptor_t mVirtualizerDescriptor;
    android::sp<android::AudioEffect> mVirtualizerEffect;
#endif
} IVirtualizer;


typedef struct {
    const struct SLVisualizationItf_ *mItf;
    IObject *mThis;
    slVisualizationCallback mCallback;
    void *mContext;
    SLmilliHertz mRate;
} IVisualization;

typedef struct /*Volume_interface*/ {
    const struct SLVolumeItf_ *mItf;
    IObject *mThis;
    // Values as specified by the application
    SLmillibel mLevel;
    SLpermille mStereoPosition;
    SLuint8 /*SLboolean*/ mMute;
    SLuint8 /*SLboolean*/ mEnableStereoPosition;
} IVolume;

/* Class structures */

/*typedef*/ struct C3DGroup_struct {
    IObject mObject;
#define INTERFACES_3DGroup 6 // see MPH_to_3DGroup in MPH_to.c for list of interfaces
    SLuint8 mInterfaceStates2[INTERFACES_3DGroup - INTERFACES_Default];
    IDynamicInterfaceManagement mDynamicInterfaceManagement;
    I3DLocation m3DLocation;
    I3DDoppler m3DDoppler;
    I3DSource m3DSource;
    I3DMacroscopic m3DMacroscopic;
    unsigned mMemberMask;   // set of member objects
} /*C3DGroup*/;

#ifdef ANDROID

// FIXME Move these into the I... section above

typedef struct {
    const struct SLAndroidEffectItf_ *mItf;
    IObject *mThis;
    android::KeyedVector<SLuint32, android::AudioEffect* > *mEffects;
} IAndroidEffect;

typedef struct {
    const struct SLAndroidEffectCapabilitiesItf_ *mItf;
    IObject *mThis;
    SLuint32 mNumFx;
    effect_descriptor_t* mFxDescriptors;
} IAndroidEffectCapabilities;

typedef struct {
    const struct SLAndroidEffectSendItf_ *mItf;
    IObject *mThis;
    // only one send per interface for now (1 bus)
    SLboolean mEnabled;
    SLmillibel mSendLevel; //android::KeyedVector<SLuint32, SLmillibel> mSendLevels;
} IAndroidEffectSend;

typedef struct {
    const struct SLAndroidConfigurationItf_ *mItf;
    IObject *mThis;
} IAndroidConfiguration;

#if defined(ANDROID) && !defined(USE_BACKPORT)
// FIXME this include is done here so the effect structures have been defined. Messy.
#include "android_Effect.h"
#endif


/*
 * Used to define the mapping from an OpenSL ES audio player to an Android
 * media framework object
 */
enum AndroidObject_type {
    INVALID_TYPE     =-1,
    MEDIAPLAYER      = 0,
    AUDIOTRACK_PULL  = 1,
    NUM_AUDIOPLAYER_MAP_TYPES
};

enum AndroidObject_state {
    ANDROID_UNINITIALIZED = -1,
    ANDROID_PREPARING,
    ANDROID_READY,
    NUM_ANDROID_STATES
};

#endif  // ANDROID


/*typedef*/ struct CAudioPlayer_struct {
    IObject mObject;
#ifdef ANDROID
#define INTERFACES_AudioPlayer 29 // see MPH_to_AudioPlayer in MPH_to.c for list of interfaces
#else
#define INTERFACES_AudioPlayer 26 // see MPH_to_AudioPlayer in MPH_to.c for list of interfaces
#endif
    SLuint8 mInterfaceStates2[INTERFACES_AudioPlayer - INTERFACES_Default];
    IDynamicInterfaceManagement mDynamicInterfaceManagement;
    IPlay mPlay;
    I3DDoppler m3DDoppler;
    I3DGrouping m3DGrouping;
    I3DLocation m3DLocation;
    I3DSource m3DSource;
    IBufferQueue mBufferQueue;
    IEffectSend mEffectSend;
    IMetadataExtraction mMetadataExtraction;
    IMetadataTraversal mMetadataTraversal;
    IPrefetchStatus mPrefetchStatus;
    IRatePitch mRatePitch;
    ISeek mSeek;
    IVolume mVolume;
    IMuteSolo mMuteSolo;
#ifdef ANDROID
    IAndroidEffect mAndroidEffect;
    IAndroidEffectSend mAndroidEffectSend;
    IAndroidConfiguration mAndroidConfiguration;
#endif
    // optional interfaces
    I3DMacroscopic m3DMacroscopic;
    IBassBoost mBassBoost;
    IDynamicSource mDynamicSource;
    IEnvironmentalReverb mEnvironmentalReverb;
    IEqualizer mEqualizer;
    IPitch mPitch;
    IPresetReverb mPresetReverb;
    IPlaybackRate mPlaybackRate;
    IVirtualizer mVirtualizer;
    IVisualization mVisualization;
    // rest of fields are not related to the interfaces
    DataLocatorFormat mDataSource;
    DataLocatorFormat mDataSink;
    // cached data for this instance
    SLuint8 /*SLboolean*/ mMute;
    // Formerly at IMuteSolo
    SLuint8 mMuteMask;      // Mask for which channels are muted: bit 0=left, 1=right
    SLuint8 mSoloMask;      // Mask for which channels are soloed: bit 0=left, 1=right
    SLuint8 mNumChannels;   // 0 means unknown, then const once it is known, range 1 <= x <= 8
    SLuint32 mSampleRateMilliHz;// 0 means unknown, then const once it is known
    // Formerly at IEffectSend
    /**
     * Dry volume modified by effect send interfaces: SLEffectSendItf and SLAndroidEffectSendItf
     */
    SLmillibel mDirectLevel;
    // implementation-specific data for this instance
#ifdef USE_OUTPUTMIXEXT
    Track *mTrack;
    float mGains[STEREO_CHANNELS];  ///< Computed gain based on volume, mute, solo, stereo position
    SLboolean mDestroyRequested;    ///< Mixer to acknowledge application's call to Object::Destroy
#endif
#ifdef USE_SNDFILE
    struct SndFile mSndFile;
#endif // USE_SNDFILE
#ifdef ANDROID
    android::Mutex          *mpLock;
    enum AndroidObject_type mAndroidObjType;
    enum AndroidObject_state mAndroidObjState;
    /** identifies which group of effects ("session") this player belongs to */
    int mSessionId;
    /** identifies the Android stream type playback will occur on */
    int mStreamType;
    /** plays the PCM data for this player */
    android::AudioTrack *mAudioTrack;
#ifndef USE_BACKPORT
    android::sp<android::SfPlayer> mSfPlayer;
#endif
    /**
     * Amplification (can be attenuation) factor derived for the VolumeLevel
     */
    float mAmplFromVolLevel;
    /**
     * Left/right amplification (can be attenuations) factors derived for the StereoPosition
     */
    float mAmplFromStereoPos[STEREO_CHANNELS];
    /**
     * Attenuation factor derived from direct level
     */
    float mAmplFromDirectLevel;
#endif
} /*CAudioPlayer*/;


/*typedef*/ struct CAudioRecorder_struct {
    // mandated interfaces
    IObject mObject;
#ifdef ANDROID
#define INTERFACES_AudioRecorder 11 // see MPH_to_AudioRecorder in MPH_to.c for list of interfaces
#else
#define INTERFACES_AudioRecorder 9  // see MPH_to_AudioRecorder in MPH_to.c for list of interfaces
#endif
    SLuint8 mInterfaceStates2[INTERFACES_AudioRecorder - INTERFACES_Default];
    IDynamicInterfaceManagement mDynamicInterfaceManagement;
    IRecord mRecord;
    IAudioEncoder mAudioEncoder;
    // optional interfaces
    IBassBoost mBassBoost;
    IDynamicSource mDynamicSource;
    IEqualizer mEqualizer;
    IVisualization mVisualization;
    IVolume mVolume;
#ifdef ANDROID
    IBufferQueue mBufferQueue;
    IAndroidConfiguration mAndroidConfiguration;
#endif
    // rest of fields are not related to the interfaces
    DataLocatorFormat mDataSource;
    DataLocatorFormat mDataSink;
    // cached data for this instance
    SLuint8 mNumChannels;   // 0 means unknown, then const once it is known, range 1 <= x <= 8
    SLuint32 mSampleRateMilliHz;// 0 means unknown, then const once it is known
    // implementation-specific data for this instance
#ifdef ANDROID
    android::AudioRecord *mAudioRecord;
    int mRecordSource;
#endif
} /*CAudioRecorder*/;


typedef struct {
    // mandated implicit interfaces
    IObject mObject;
#ifdef ANDROID
#define INTERFACES_Engine 11 // see MPH_to_Engine in MPH_to.c for list of interfaces
#else
#define INTERFACES_Engine 10 // see MPH_to_Engine in MPH_to.c for list of interfaces
#endif
    SLuint8 mInterfaceStates2[INTERFACES_Engine - INTERFACES_Default];
    IDynamicInterfaceManagement mDynamicInterfaceManagement;
    IEngine mEngine;
    IEngineCapabilities mEngineCapabilities;
    IThreadSync mThreadSync;
    // mandated explicit interfaces
    IAudioIODeviceCapabilities mAudioIODeviceCapabilities;
    IAudioDecoderCapabilities mAudioDecoderCapabilities;
    IAudioEncoderCapabilities mAudioEncoderCapabilities;
    I3DCommit m3DCommit;
#ifdef ANDROID
    IAndroidEffectCapabilities mAndroidEffectCapabilities;
#endif
    // optional interfaces
    IDeviceVolume mDeviceVolume;
    // rest of fields are not related to the interfaces
    pthread_t mSyncThread;
} CEngine;

typedef struct {
    // mandated interfaces
    IObject mObject;
#define INTERFACES_LEDDevice 3 // see MPH_to_LEDDevice in MPH_to.c for list of interfaces
    SLuint8 mInterfaceStates2[INTERFACES_LEDDevice - INTERFACES_Default];
    IDynamicInterfaceManagement mDynamicInterfaceManagement;
    ILEDArray mLEDArray;
    SLuint32 mDeviceID;
} CLEDDevice;

typedef struct {
    // mandated interfaces
    IObject mObject;
#define INTERFACES_Listener 4 // see MPH_to_Listener in MPH_to.c for list of interfaces
    SLuint8 mInterfaceStates2[INTERFACES_Listener - INTERFACES_Default];
    IDynamicInterfaceManagement mDynamicInterfaceManagement;
    I3DDoppler m3DDoppler;
    I3DLocation m3DLocation;
} CListener;

typedef struct {
    // mandated interfaces
    IObject mObject;
#define INTERFACES_MetadataExtractor 5 // see MPH_to_MetadataExtractor in MPH_to.c for list of
                                       // interfaces
    SLuint8 mInterfaceStates2[INTERFACES_MetadataExtractor - INTERFACES_Default];
    IDynamicInterfaceManagement mDynamicInterfaceManagement;
    IDynamicSource mDynamicSource;
    IMetadataExtraction mMetadataExtraction;
    IMetadataTraversal mMetadataTraversal;
} CMetadataExtractor;

typedef struct {
    // mandated interfaces
    IObject mObject;

#define INTERFACES_MidiPlayer 29 // see MPH_to_MidiPlayer in MPH_to.c for list of interfaces
    SLuint8 mInterfaceStates2[INTERFACES_MidiPlayer - INTERFACES_Default];
    IDynamicInterfaceManagement mDynamicInterfaceManagement;
    IPlay mPlay;
    I3DDoppler m3DDoppler;
    I3DGrouping m3DGrouping;
    I3DLocation m3DLocation;
    I3DSource m3DSource;
    IBufferQueue mBufferQueue;
    IEffectSend mEffectSend;
    IMetadataExtraction mMetadataExtraction;
    IMetadataTraversal mMetadataTraversal;
    IMIDIMessage mMIDIMessage;
    IMIDITime mMIDITime;
    IMIDITempo mMIDITempo;
    IMIDIMuteSolo mMIDIMuteSolo;
    IPrefetchStatus mPrefetchStatus;
    ISeek mSeek;
    IVolume mVolume;
    IMuteSolo mMuteSolo;
    // optional interfaces
    I3DMacroscopic m3DMacroscopic;
    IBassBoost mBassBoost;
    IDynamicSource mDynamicSource;
    IEnvironmentalReverb mEnvironmentalReverb;
    IEqualizer mEqualizer;
    IPitch mPitch;
    IPresetReverb mPresetReverb;
    IPlaybackRate mPlaybackRate;
    IVirtualizer mVirtualizer;
    IVisualization mVisualization;
} CMidiPlayer;

/*typedef*/ struct COutputMix_struct {
    // mandated interfaces
    IObject mObject;
#ifdef ANDROID
#define INTERFACES_OutputMix 12 // see MPH_to_OutputMix in MPH_to.c for list of interfaces
#else
#define INTERFACES_OutputMix 11 // see MPH_to_OutputMix in MPH_to.c for list of interfaces
#endif
    SLuint8 mInterfaceStates2[INTERFACES_OutputMix - INTERFACES_Default];
    IDynamicInterfaceManagement mDynamicInterfaceManagement;
    IOutputMix mOutputMix;
#ifdef USE_OUTPUTMIXEXT
    IOutputMixExt mOutputMixExt;
#endif
    IEnvironmentalReverb mEnvironmentalReverb;
    IEqualizer mEqualizer;
    IPresetReverb mPresetReverb;
    IVirtualizer mVirtualizer;
    IVolume mVolume;
    // optional interfaces
    IBassBoost mBassBoost;
    IVisualization mVisualization;
#ifdef ANDROID
    IAndroidEffect mAndroidEffect;
#endif
    // implementation-specific data for this instance
} /*COutputMix*/;

typedef struct {
    // mandated interfaces
    IObject mObject;
#define INTERFACES_VibraDevice 3 // see MPH_to_VibraDevice in MPH_to.c for list of interfaces
    SLuint8 mInterfaceStates2[INTERFACES_VibraDevice - INTERFACES_Default];
    IDynamicInterfaceManagement mDynamicInterfaceManagement;
    IVibra mVibra;
    //
    SLuint32 mDeviceID;
} CVibraDevice;

struct MPH_init {
    // unsigned char mMPH;
    VoidHook mInit;
    VoidHook mResume;
    VoidHook mDeinit;
};

extern /*static*/ int IID_to_MPH(const SLInterfaceID iid);
extern /*static*/ const struct MPH_init MPH_init_table[MPH_MAX];
extern SLresult checkInterfaces(const ClassTable *class__,
    SLuint32 numInterfaces, const SLInterfaceID *pInterfaceIds,
    const SLboolean *pInterfaceRequired, unsigned *pExposedMask);
extern IObject *construct(const ClassTable *class__,
    unsigned exposedMask, SLEngineItf engine);
extern const ClassTable *objectIDtoClass(SLuint32 objectID);
extern const struct SLInterfaceID_ SL_IID_array[MPH_MAX];
extern SLuint32 IObjectToObjectID(IObject *object);
extern void IObject_Publish(IObject *this);
extern void IObject_Destroy(SLObjectItf self);

// Map an interface to it's "object ID" (which is really a class ID).
// Note: this operation is undefined on IObject, as it lacks an mThis.
// If you have an IObject, then use IObjectToObjectID directly.

#define InterfaceToObjectID(this) IObjectToObjectID((this)->mThis)

// Map an interface to it's corresponding IObject.
// Note: this operation is undefined on IObject, as it lacks an mThis.
// If you have an IObject, then you're done -- you already have what you need.

#define InterfaceToIObject(this) ((this)->mThis)

#define InterfaceToCAudioPlayer(this) (((CAudioPlayer*)InterfaceToIObject(this)))

#define InterfaceToCAudioRecorder(this) (((CAudioRecorder*)InterfaceToIObject(this)))

#ifdef ANDROID
#include "android_AudioPlayer.h"
#endif

extern SLresult checkDataSource(const SLDataSource *pDataSrc,
        DataLocatorFormat *myDataSourceLocator);
extern SLresult checkDataSink(const SLDataSink *pDataSink, DataLocatorFormat *myDataSinkLocator,
        SLuint32 objType);
extern SLresult checkSourceFormatVsInterfacesCompatibility(
        const DataLocatorFormat *pDataLocatorFormat,
        SLuint32 numInterfaces, const SLInterfaceID *pInterfaceIds,
        const SLboolean *pInterfaceRequired);
extern void freeDataLocatorFormat(DataLocatorFormat *dlf);

extern bool C3DGroup_PreDestroy(void *self);

extern SLresult CAudioPlayer_Realize(void *self, SLboolean async);
extern SLresult CAudioPlayer_Resume(void *self, SLboolean async);
extern void CAudioPlayer_Destroy(void *self);
extern bool CAudioPlayer_PreDestroy(void *self);

extern SLresult CAudioRecorder_Realize(void *self, SLboolean async);
extern SLresult CAudioRecorder_Resume(void *self, SLboolean async);
extern void CAudioRecorder_Destroy(void *self);
extern bool CAudioRecorder_PreDestroy(void *self);

extern SLresult CEngine_Realize(void *self, SLboolean async);
extern SLresult CEngine_Resume(void *self, SLboolean async);
extern void CEngine_Destroy(void *self);
extern bool CEngine_PreDestroy(void *self);

extern SLresult COutputMix_Realize(void *self, SLboolean async);
extern SLresult COutputMix_Resume(void *self, SLboolean async);
extern void COutputMix_Destroy(void *self);
extern bool COutputMix_PreDestroy(void *self);

#ifdef USE_SDL
extern void SDL_open(IEngine *thisEngine);
extern void SDL_close(void);
#endif
#define SL_OBJECT_STATE_REALIZING_1  ((SLuint32) 0x4) // async realize on work queue
#define SL_OBJECT_STATE_REALIZING_2  ((SLuint32) 0x5) // sync realize, or async realize hook
#define SL_OBJECT_STATE_RESUMING_1   ((SLuint32) 0x6) // async resume on work queue
#define SL_OBJECT_STATE_RESUMING_2   ((SLuint32) 0x7) // sync resume, or async resume hook
#define SL_OBJECT_STATE_SUSPENDING   ((SLuint32) 0x8) // suspend in progress
#define SL_OBJECT_STATE_REALIZING_1A ((SLuint32) 0x9) // abort while async realize on work queue
#define SL_OBJECT_STATE_RESUMING_1A  ((SLuint32) 0xA) // abort while async resume on work queue
#define SL_OBJECT_STATE_DESTROYING   ((SLuint32) 0xB) // destroy object when no strong references
extern void *sync_start(void *arg);
extern SLresult err_to_result(int err);

#ifdef __GNUC__
#define ctz __builtin_ctz
#else
extern unsigned ctz(unsigned);
#endif
extern const char * const interface_names[MPH_MAX];
#include "platform.h"

// Attributes

#define ATTR_NONE       ((unsigned) 0x0)      // none
#define ATTR_GAIN       ((unsigned) 0x1 << 0) // player volume, channel mute, channel solo,
                                              // player stereo position, player mute
#define ATTR_TRANSPORT  ((unsigned) 0x1 << 1) // play state, looping
#define ATTR_POSITION   ((unsigned) 0x1 << 2) // requested position (a.k.a. seek position)
#define ATTR_ENQUEUE    ((unsigned) 0x1 << 3) // buffer queue became non-empty and in playing state

#define SL_DATALOCATOR_NULL 0    // application specified a NULL value for pLocator
#define SL_DATAFORMAT_NULL 0     // application specified a NULL or undefined value for pFormat

// Trace debugging

// Always defined, but may be a no-op if trace support is disabled at compile-time
extern void slTraceSetEnabled(unsigned enabled);

#define SL_TRACE_ENTER          0x1
#define SL_TRACE_LEAVE_FAILURE  0x2
#define SL_TRACE_LEAVE_VOID     0x4
#define SL_TRACE_LEAVE_SUCCESS  0x8
#define SL_TRACE_LEAVE          (SL_TRACE_LEAVE_FAILURE | SL_TRACE_LEAVE_VOID | \
                                    SL_TRACE_LEAVE_SUCCESS)
#define SL_TRACE_ALL            (SL_TRACE_ENTER | SL_TRACE_LEAVE)
#ifndef SL_TRACE_DEFAULT
#define SL_TRACE_DEFAULT        (SL_TRACE_LEAVE_FAILURE)
#endif

#ifndef USE_TRACE

#define SL_ENTER_GLOBAL SLresult result;
#define SL_LEAVE_GLOBAL return result;
#define SL_ENTER_INTERFACE SLresult result;
#define SL_LEAVE_INTERFACE return result;
#define SL_ENTER_INTERFACE_VOID
#define SL_LEAVE_INTERFACE_VOID return;

#else

extern void slTraceEnterGlobal(const char *function);
extern void slTraceLeaveGlobal(const char *function, SLresult result);
extern void slTraceEnterInterface(const char *function);
extern void slTraceLeaveInterface(const char *function, SLresult result);
extern void slTraceEnterInterfaceVoid(const char *function);
extern void slTraceLeaveInterfaceVoid(const char *function);
#define SL_ENTER_GLOBAL SLresult result; slTraceEnterGlobal(__FUNCTION__);
#define SL_LEAVE_GLOBAL slTraceLeaveGlobal(__FUNCTION__, result); return result;
#define SL_ENTER_INTERFACE SLresult result; slTraceEnterInterface(__FUNCTION__);
#define SL_LEAVE_INTERFACE slTraceLeaveInterface(__FUNCTION__, result); return result;
#define SL_ENTER_INTERFACE_VOID slTraceEnterInterfaceVoid(__FUNCTION__);
#define SL_LEAVE_INTERFACE_VOID slTraceLeaveInterfaceVoid(__FUNCTION__); return;

#endif

#ifdef USE_OUTPUTMIXEXT

#define SL_PLAYSTATE_STOPPING ((SLuint32) 0x4) // Play::Stop while PLAYING
// If we needed it, could have PLAYING mean mixer is currently reading from front buffer,
// while PLAYABLE would mean application requested PLAYING, but buffer queue is empty

#endif

#ifdef USE_SNDFILE
extern void audioPlayerTransportUpdate(CAudioPlayer *audioPlayer);
#endif

extern SLresult IBufferQueue_Enqueue(SLBufferQueueItf self, const void *pBuffer, SLuint32 size);
extern SLresult IBufferQueue_Clear(SLBufferQueueItf self);
extern SLresult IBufferQueue_RegisterCallback(SLBufferQueueItf self,
    slBufferQueueCallback callback, void *pContext);
extern void IBufferQueue_Destroy(IBufferQueue *this);

extern bool IsInterfaceInitialized(IObject *this, unsigned MPH);
extern SLresult AcquireStrongRef(IObject *object, SLuint32 expectedObjectID);
extern void ReleaseStrongRef(IObject *object);
extern void ReleaseStrongRefAndUnlockExclusive(IObject *object);

extern COutputMix *CAudioPlayer_GetOutputMix(CAudioPlayer *audioPlayer);

extern int _opensles_user_freq __attribute__((weak));