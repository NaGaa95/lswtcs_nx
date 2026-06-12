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

/* OpenSL ES private and global functions not associated with an interface or class */

#include "sles_allinclusive.h"


/** \brief Return true if the specified interface exists and has been initialized for this object.
 *  Returns false if the class does not support this kind of interface, or the class supports the
 *  interface but this particular object has not had the interface exposed at object creation time
 *  or by DynamicInterface::AddInterface. Note that the return value is not affected by whether
 *  the application has requested access to the interface with Object::GetInterface. Assumes on
 *  entry that the object is locked for either shared or exclusive access.
 */

bool IsInterfaceInitialized(IObject *this, unsigned MPH)
{
    assert(NULL != this);
    assert( /* (MPH_MIN <= MPH) && */ (MPH < (unsigned) MPH_MAX));
    const ClassTable *class__ = this->mClass;
    assert(NULL != class__);
    int index;
    if (0 > (index = class__->mMPH_to_index[MPH])) {
        return false;
    }
    assert(MAX_INDEX >= class__->mInterfaceCount);
    assert(class__->mInterfaceCount > (unsigned) index);
    switch (this->mInterfaceStates[index]) {
    case INTERFACE_EXPOSED:
    case INTERFACE_ADDED:
        return true;
    default:
        return false;
    }
}


/** \brief Map an IObject to it's "object ID" (which is really a class ID) */

SLuint32 IObjectToObjectID(IObject *this)
{
    assert(NULL != this);
    return this->mClass->mObjectID;
}


/** \brief Acquire a strong reference to an object.
 *  Check that object has the specified "object ID" (which is really a class ID) and is in the
 *  realized state.  If so, then acquire a strong reference to it and return true.
 *  Otherwise return false.
 */

SLresult AcquireStrongRef(IObject *object, SLuint32 expectedObjectID)
{
    if (NULL == object) {
        return SL_RESULT_PARAMETER_INVALID;
    }
    // NTH additional validity checks on address here
    SLresult result;
    object_lock_exclusive(object);
    SLuint32 actualObjectID = IObjectToObjectID(object);
    if (expectedObjectID != actualObjectID) {
        SL_LOGE("object %p has object ID %lu but expected %lu", object, actualObjectID,
            expectedObjectID);
        result = SL_RESULT_PARAMETER_INVALID;
    } else if (SL_OBJECT_STATE_REALIZED != object->mState) {
        SL_LOGE("object %p with object ID %lu is not realized", object, actualObjectID);
        result = SL_RESULT_PRECONDITIONS_VIOLATED;
    } else {
        ++object->mStrongRefCount;
        result = SL_RESULT_SUCCESS;
    }
    object_unlock_exclusive(object);
    return result;
}


/** \brief Release a strong reference to an object.
 *  Entry condition: the object is locked.
 *  Exit condition: the object is unlocked.
 *  Finishes the destroy if needed.
 */

void ReleaseStrongRefAndUnlockExclusive(IObject *object)
{
#ifdef USE_DEBUG
    assert(pthread_equal(pthread_self(), object->mOwner));
#endif
    assert(0 < object->mStrongRefCount);
    if ((0 == --object->mStrongRefCount) && (SL_OBJECT_STATE_DESTROYING == object->mState)) {
        // FIXME do the destroy here - merge with IDestroy
        // but can't do this until we move Destroy to the sync thread
        // as Destroy is now a blocking operation, and to avoid a race
    } else {
        object_unlock_exclusive(object);
    }
}


/** \brief Release a strong reference to an object.
 *  Entry condition: the object is unlocked.
 *  Exit condition: the object is unlocked.
 *  Finishes the destroy if needed.
 */

void ReleaseStrongRef(IObject *object)
{
    assert(NULL != object);
    object_lock_exclusive(object);
    ReleaseStrongRefAndUnlockExclusive(object);
}


/** \brief Convert POSIX pthread error code to OpenSL ES result code */

SLresult err_to_result(int err)
{
    if (EAGAIN == err || ENOMEM == err) {
        return SL_RESULT_RESOURCE_ERROR;
    }
    if (0 != err) {
        return SL_RESULT_INTERNAL_ERROR;
    }
    return SL_RESULT_SUCCESS;
}


/** \brief Check the interface IDs passed into a Create operation */

SLresult checkInterfaces(const ClassTable *class__, SLuint32 numInterfaces,
    const SLInterfaceID *pInterfaceIds, const SLboolean *pInterfaceRequired, unsigned *pExposedMask)
{
    assert(NULL != class__ && NULL != pExposedMask);
    // Initially no interfaces are exposed
    unsigned exposedMask = 0;
    const struct iid_vtable *interfaces = class__->mInterfaces;
    SLuint32 interfaceCount = class__->mInterfaceCount;
    SLuint32 i;
    // Expose all implicit interfaces
    for (i = 0; i < interfaceCount; ++i) {
        switch (interfaces[i].mInterface) {
        case INTERFACE_IMPLICIT:
            // there must be an initialization hook present
            if (NULL != MPH_init_table[interfaces[i].mMPH].mInit) {
                exposedMask |= 1 << i;
            }
            break;
        case INTERFACE_EXPLICIT:
        case INTERFACE_DYNAMIC:
        case INTERFACE_UNAVAILABLE:
        case INTERFACE_EXPLICIT_PREREALIZE:
            break;
        default:
            assert(false);
            break;
        }
    }
    if (0 < numInterfaces) {
        if (NULL == pInterfaceIds || NULL == pInterfaceRequired) {
            return SL_RESULT_PARAMETER_INVALID;
        }
        // Loop for each requested interface
        for (i = 0; i < numInterfaces; ++i) {
            SLInterfaceID iid = pInterfaceIds[i];
            if (NULL == iid) {
                return SL_RESULT_PARAMETER_INVALID;
            }
            int MPH, index;
            if ((0 > (MPH = IID_to_MPH(iid))) ||
                    // there must be an initialization hook present
                    (NULL == MPH_init_table[MPH].mInit) ||
                    (0 > (index = class__->mMPH_to_index[MPH]))) {
                // Here if interface was not found, or is not available for this object type
                if (pInterfaceRequired[i]) {
                    // Application said it required the interface, so give up
                    SL_LOGE("class %s interface %lu required but unavailable MPH=%d",
                            class__->mName, i, MPH);
                    return SL_RESULT_FEATURE_UNSUPPORTED;
                }
                // Application said it didn't really need the interface, so ignore
                continue;
            }
            // The requested interface was both found and available, so expose it
            exposedMask |= (1 << index);
            // Note that we ignore duplicate requests, including equal and aliased IDs
        }
    }
    *pExposedMask = exposedMask;
    return SL_RESULT_SUCCESS;
}


/** \brief Helper shared by decoder and encoder */

SLresult GetCodecCapabilities(SLuint32 codecId, SLuint32 *pIndex,
    SLAudioCodecDescriptor *pDescriptor, const CodecDescriptor *codecDescriptors)
{
    if (NULL == pIndex) {
        return SL_RESULT_PARAMETER_INVALID;
    }
    const CodecDescriptor *cd = codecDescriptors;
    SLuint32 index;
    if (NULL == pDescriptor) {
        for (index = 0 ; NULL != cd->mDescriptor; ++cd) {
            if (cd->mCodecID == codecId) {
                ++index;
            }
        }
        *pIndex = index;
        return SL_RESULT_SUCCESS;
    }
    index = *pIndex;
    for ( ; NULL != cd->mDescriptor; ++cd) {
        if (cd->mCodecID == codecId) {
            if (0 == index) {
                *pDescriptor = *cd->mDescriptor;
#if 0   // Temporary workaround for Khronos bug 6331
                if (0 < pDescriptor->numSampleRatesSupported) {
                    // The malloc is not in the 1.0.1 specification
                    SLmilliHertz *temp = (SLmilliHertz *) malloc(sizeof(SLmilliHertz) *
                        pDescriptor->numSampleRatesSupported);
                    assert(NULL != temp);
                    memcpy(temp, pDescriptor->pSampleRatesSupported, sizeof(SLmilliHertz) *
                        pDescriptor->numSampleRatesSupported);
                    pDescriptor->pSampleRatesSupported = temp;
                } else {
                    pDescriptor->pSampleRatesSupported = NULL;
                }
#endif
                return SL_RESULT_SUCCESS;
            }
            --index;
        }
    }
    return SL_RESULT_PARAMETER_INVALID;
}


/** \brief Check a data locator and make local deep copy */

static SLresult checkDataLocator(void *pLocator, DataLocator *pDataLocator)
{
    if (NULL == pLocator) {
        pDataLocator->mLocatorType = SL_DATALOCATOR_NULL;
        return SL_RESULT_SUCCESS;
    }
    SLresult result;
    SLuint32 locatorType = *(SLuint32 *)pLocator;
    switch (locatorType) {

    case SL_DATALOCATOR_ADDRESS:
        pDataLocator->mAddress = *(SLDataLocator_Address *)pLocator;
        // if length is greater than zero, then the address must be non-NULL
        if ((0 < pDataLocator->mAddress.length) && (NULL == pDataLocator->mAddress.pAddress)) {
            SL_LOGE("pAddress is NULL");
            return SL_RESULT_PARAMETER_INVALID;
        }
        break;

    case SL_DATALOCATOR_BUFFERQUEUE:
    // This is an alias that is _not_ converted; the rest of the code must check for both locator
    // types. That's because it is only an alias for audio players, not audio recorder objects
    // so we have to remember the distinction.
    case SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE:
        pDataLocator->mBufferQueue = *(SLDataLocator_BufferQueue *)pLocator;
        // number of buffers must be specified, there is no default value, and must not be excessive
        if (!((1 <= pDataLocator->mBufferQueue.numBuffers) &&
            (pDataLocator->mBufferQueue.numBuffers <= 255))) {
            SL_LOGE("numBuffers=%u", (unsigned) pDataLocator->mBufferQueue.numBuffers);
            return SL_RESULT_PARAMETER_INVALID;
        }
        break;

    case SL_DATALOCATOR_IODEVICE:
        {
        pDataLocator->mIODevice = *(SLDataLocator_IODevice *)pLocator;
        SLuint32 deviceType = pDataLocator->mIODevice.deviceType;
        SLObjectItf device = pDataLocator->mIODevice.device;
        if (NULL != device) {
            pDataLocator->mIODevice.deviceID = 0;
            SLuint32 expectedObjectID;
            switch (deviceType) {
            case SL_IODEVICE_LEDARRAY:
                expectedObjectID = SL_OBJECTID_LEDDEVICE;
                break;
            case SL_IODEVICE_VIBRA:
                expectedObjectID = SL_OBJECTID_VIBRADEVICE;
                break;
            // audio input and audio output cannot be specified via objects
            case SL_IODEVICE_AUDIOINPUT:
            // worse yet, an SL_IODEVICE enum constant for audio output does not exist yet
            // case SL_IODEVICE_AUDIOOUTPUT:
            default:
                SL_LOGE("invalid deviceType %lu", deviceType);
                pDataLocator->mIODevice.device = NULL;
                return SL_RESULT_PARAMETER_INVALID;
            }
            // check that device has the correct object ID and is realized,
            // and acquire a strong reference to it
            result = AcquireStrongRef((IObject *) device, expectedObjectID);
            if (SL_RESULT_SUCCESS != result) {
                SL_LOGE("locator type is IODEVICE, but device field %p has wrong object ID or is " \
                    "not realized", device);
                pDataLocator->mIODevice.device = NULL;
                return result;
            }
        } else {
            SLuint32 deviceID = pDataLocator->mIODevice.deviceID;
            switch (deviceType) {
            case SL_IODEVICE_LEDARRAY:
                if (SL_DEFAULTDEVICEID_LED != deviceID) {
                    SL_LOGE("invalid LED deviceID %lu", deviceID);
                    return SL_RESULT_PARAMETER_INVALID;
                }
                break;
            case SL_IODEVICE_VIBRA:
                if (SL_DEFAULTDEVICEID_VIBRA != deviceID) {
                    SL_LOGE("invalid vibra deviceID %lu", deviceID);
                    return SL_RESULT_PARAMETER_INVALID;
                }
                break;
            case SL_IODEVICE_AUDIOINPUT:
                if (SL_DEFAULTDEVICEID_AUDIOINPUT != deviceID) {
                    SL_LOGE("invalid audio input deviceID %lu", deviceID);
                    return SL_RESULT_PARAMETER_INVALID;
                }
                break;
            default:
                SL_LOGE("invalid deviceType %lu", deviceType);
                return SL_RESULT_PARAMETER_INVALID;
            }
        }
        }
        break;

    case SL_DATALOCATOR_MIDIBUFFERQUEUE:
        pDataLocator->mMIDIBufferQueue = *(SLDataLocator_MIDIBufferQueue *)pLocator;
        if (0 == pDataLocator->mMIDIBufferQueue.tpqn) {
            pDataLocator->mMIDIBufferQueue.tpqn = 192;
        }
        // number of buffers must be specified, there is no default value, and must not be excessive
        if (!((1 <= pDataLocator->mMIDIBufferQueue.numBuffers) &&
            (pDataLocator->mMIDIBufferQueue.numBuffers <= 255))) {
            SL_LOGE("invalid MIDI buffer queue");
            return SL_RESULT_PARAMETER_INVALID;
        }
        break;

    case SL_DATALOCATOR_OUTPUTMIX:
        pDataLocator->mOutputMix = *(SLDataLocator_OutputMix *)pLocator;
        // check that output mix object has the correct object ID and is realized,
        // and acquire a strong reference to it
        result = AcquireStrongRef((IObject *) pDataLocator->mOutputMix.outputMix,
            SL_OBJECTID_OUTPUTMIX);
        if (SL_RESULT_SUCCESS != result) {
            SL_LOGE("locatorType is SL_DATALOCATOR_OUTPUTMIX, but outputMix field %p does not " \
                "refer to an SL_OBJECTID_OUTPUTMIX or is not realized", \
                pDataLocator->mOutputMix.outputMix);
            pDataLocator->mOutputMix.outputMix = NULL;
            return result;
        }
        break;

    case SL_DATALOCATOR_URI:
        {
        pDataLocator->mURI = *(SLDataLocator_URI *)pLocator;
        if (NULL == pDataLocator->mURI.URI) {
            SL_LOGE("invalid URI");
            return SL_RESULT_PARAMETER_INVALID;
        }
        // NTH verify URI address for validity
        size_t len = strlen((const char *) pDataLocator->mURI.URI);
        SLchar *myURI = (SLchar *) malloc(len + 1);
        if (NULL == myURI) {
            pDataLocator->mURI.URI = NULL;
            return SL_RESULT_MEMORY_FAILURE;
        }
        memcpy(myURI, pDataLocator->mURI.URI, len + 1);
        // Verify that another thread didn't change the NUL-terminator after we used it
        // to determine length of string to copy. It's OK if the string became shorter.
        if ('\0' != myURI[len]) {
            free(myURI);
            pDataLocator->mURI.URI = NULL;
            return SL_RESULT_PARAMETER_INVALID;
        }
        pDataLocator->mURI.URI = myURI;
        }
        break;

    case SL_DATALOCATOR_ANDROIDFD:
        {
        pDataLocator->mFD = *(SLDataLocator_AndroidFD *)pLocator;
        SL_LOGV("Data locator FD: fd=%ld offset=%lld length=%lld", pDataLocator->mFD.fd,
                pDataLocator->mFD.offset, pDataLocator->mFD.length);
        // NTH check against process fd limit
        if (0 > pDataLocator->mFD.fd) {
            return SL_RESULT_PARAMETER_INVALID;
        }
        }
        break;

    default:
        SL_LOGE("invalid locatorType %lu", locatorType);
        return SL_RESULT_PARAMETER_INVALID;
    }

    // Verify that another thread didn't change the locatorType field after we used it
    // to determine sizeof struct to copy.
    if (locatorType != pDataLocator->mLocatorType) {
        return SL_RESULT_PARAMETER_INVALID;
    }
    return SL_RESULT_SUCCESS;
}


/** \brief Free the local deep copy of a data locator */

static void freeDataLocator(DataLocator *pDataLocator)
{
    switch (pDataLocator->mLocatorType) {
    case SL_DATALOCATOR_URI:
        if (NULL != pDataLocator->mURI.URI) {
            free(pDataLocator->mURI.URI);
            pDataLocator->mURI.URI = NULL;
        }
        pDataLocator->mURI.URI = NULL;
        break;
    case SL_DATALOCATOR_IODEVICE:
        if (NULL != pDataLocator->mIODevice.device) {
            ReleaseStrongRef((IObject *) pDataLocator->mIODevice.device);
            pDataLocator->mIODevice.device = NULL;
        }
        break;
    case SL_DATALOCATOR_OUTPUTMIX:
        if (NULL != pDataLocator->mOutputMix.outputMix) {
            ReleaseStrongRef((IObject *) pDataLocator->mOutputMix.outputMix);
            pDataLocator->mOutputMix.outputMix = NULL;
        }
        break;
    default:
        break;
    }
}


/** \brief Check a data format and make local deep copy */

static SLresult checkDataFormat(void *pFormat, DataFormat *pDataFormat)
{
    SLresult result = SL_RESULT_SUCCESS;

    if (NULL == pFormat) {
        pDataFormat->mFormatType = SL_DATAFORMAT_NULL;
    } else {
        SLuint32 formatType = *(SLuint32 *)pFormat;
        switch (formatType) {

        case SL_DATAFORMAT_PCM:
            pDataFormat->mPCM = *(SLDataFormat_PCM *)pFormat;
            do {

                // check the channel count
                switch (pDataFormat->mPCM.numChannels) {
                case 1:     // mono
                case 2:     // stereo
                    break;
                case 0:     // unknown
                    result = SL_RESULT_PARAMETER_INVALID;
                    break;
                default:    // multi-channel
                        SL_LOGE("WEEE");

                    result = SL_RESULT_CONTENT_UNSUPPORTED;
                    break;
                }
                if (SL_RESULT_SUCCESS != result) {
                    SL_LOGE("numChannels=%u", (unsigned) pDataFormat->mPCM.numChannels);
                    break;
                }

                // check the sampling rate
                switch (pDataFormat->mPCM.samplesPerSec) {
                case SL_SAMPLINGRATE_8:
                case SL_SAMPLINGRATE_11_025:
                case SL_SAMPLINGRATE_12:
                case SL_SAMPLINGRATE_16:
                case SL_SAMPLINGRATE_22_05:
                case SL_SAMPLINGRATE_24:
                case SL_SAMPLINGRATE_32:
                case SL_SAMPLINGRATE_44_1:
                case SL_SAMPLINGRATE_48:
                case SL_SAMPLINGRATE_64:
                case SL_SAMPLINGRATE_88_2:
                case SL_SAMPLINGRATE_96:
                case SL_SAMPLINGRATE_192:
                    break;
                case 0:
                    result = SL_RESULT_PARAMETER_INVALID;
                    break;
                default:
                        SL_LOGE("WEEE");

                    result = SL_RESULT_CONTENT_UNSUPPORTED;
                    break;
                }
                if (SL_RESULT_SUCCESS != result) {
                    SL_LOGE("samplesPerSec=%u", (unsigned) pDataFormat->mPCM.samplesPerSec);
                    break;
                }

                // check the sample bit depth
                switch (pDataFormat->mPCM.bitsPerSample) {
                case SL_PCMSAMPLEFORMAT_FIXED_8:
                case SL_PCMSAMPLEFORMAT_FIXED_16:
                    break;
                case SL_PCMSAMPLEFORMAT_FIXED_20:
                case SL_PCMSAMPLEFORMAT_FIXED_24:
                case SL_PCMSAMPLEFORMAT_FIXED_28:
                case SL_PCMSAMPLEFORMAT_FIXED_32:
                        SL_LOGE("WEEE");

                    result = SL_RESULT_CONTENT_UNSUPPORTED;
                    break;
                default:
                    result = SL_RESULT_PARAMETER_INVALID;
                    break;
                }
                if (SL_RESULT_SUCCESS != result) {
                    SL_LOGE("bitsPerSample=%u", (unsigned) pDataFormat->mPCM.bitsPerSample);
                    break;
                }

                // check the container bit depth
                switch (pDataFormat->mPCM.containerSize) {
                case SL_PCMSAMPLEFORMAT_FIXED_8:
                case SL_PCMSAMPLEFORMAT_FIXED_16:
                    if (pDataFormat->mPCM.containerSize != pDataFormat->mPCM.bitsPerSample) {
                                SL_LOGE("WEEE");

                        result = SL_RESULT_CONTENT_UNSUPPORTED;
                    }
                    break;
                default:
                        SL_LOGE("WEEE");

                    result = SL_RESULT_CONTENT_UNSUPPORTED;
                    break;
                }
                if (SL_RESULT_SUCCESS != result) {
                    SL_LOGE("containerSize=%u", (unsigned) pDataFormat->mPCM.containerSize);
                    break;
                }

                // check the channel mask
                switch (pDataFormat->mPCM.channelMask) {
                case SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT:
                    if (2 != pDataFormat->mPCM.numChannels) {
                        result = SL_RESULT_PARAMETER_INVALID;
                    }
                    break;
                case SL_SPEAKER_FRONT_LEFT:
                case SL_SPEAKER_FRONT_RIGHT:
                case SL_SPEAKER_FRONT_CENTER:
                    if (1 != pDataFormat->mPCM.numChannels) {
                        result = SL_RESULT_PARAMETER_INVALID;
                    }
                    break;
                case 0:
                    pDataFormat->mPCM.channelMask = pDataFormat->mPCM.numChannels == 2 ?
                        SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT : SL_SPEAKER_FRONT_CENTER;
                    break;
                default:
                    result = SL_RESULT_PARAMETER_INVALID;
                    break;
                }
                if (SL_RESULT_SUCCESS != result) {
                    SL_LOGE("channelMask=0x%lx numChannels=%lu", pDataFormat->mPCM.channelMask,
                        pDataFormat->mPCM.numChannels);
                    break;
                }

                // check the endianness / byte order
                switch (pDataFormat->mPCM.endianness) {
                case SL_BYTEORDER_LITTLEENDIAN:
                case SL_BYTEORDER_BIGENDIAN:
                    break;
                // native is proposed but not yet in spec
                default:
                    result = SL_RESULT_PARAMETER_INVALID;
                    break;
                }
                if (SL_RESULT_SUCCESS != result) {
                    SL_LOGE("endianness=%u", (unsigned) pDataFormat->mPCM.endianness);
                    break;
                }

                // here if all checks passed successfully

            } while(0);
            break;

        case SL_DATAFORMAT_MIME:
            pDataFormat->mMIME = *(SLDataFormat_MIME *)pFormat;
            if (NULL != pDataFormat->mMIME.mimeType) {
                // NTH check address for validity
                size_t len = strlen((const char *) pDataFormat->mMIME.mimeType);
                SLchar *myMIME = (SLchar *) malloc(len + 1);
                if (NULL == myMIME) {
                    result = SL_RESULT_MEMORY_FAILURE;
                } else {
                    memcpy(myMIME, pDataFormat->mMIME.mimeType, len + 1);
                    // make sure MIME string was not modified asynchronously
                    if ('\0' != myMIME[len]) {
                        free(myMIME);
                        myMIME = NULL;
                        result = SL_RESULT_PRECONDITIONS_VIOLATED;
                    }
                }
                pDataFormat->mMIME.mimeType = myMIME;
            }
            break;

        default:
            result = SL_RESULT_PARAMETER_INVALID;
            SL_LOGE("formatType=%u", (unsigned) formatType);
            break;

        }

        // make sure format type was not modified asynchronously
        if ((SL_RESULT_SUCCESS == result) && (formatType != pDataFormat->mFormatType)) {
            result = SL_RESULT_PRECONDITIONS_VIOLATED;
        }

    }

    return result;
}


/** \brief Check interface ID compatibility with respect to a particular data locator format */

SLresult checkSourceFormatVsInterfacesCompatibility(const DataLocatorFormat *pDataLocatorFormat,
        SLuint32 numInterfaces, const SLInterfaceID *pInterfaceIds,
            const SLboolean *pInterfaceRequired) {
    // can't request SLSeekItf if data source is a buffer queue
    // FIXME there are other invalid combinations -- see docs
    SLuint32 i;
    switch (pDataLocatorFormat->mLocator.mLocatorType) {
    case SL_DATALOCATOR_BUFFERQUEUE:
    case SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE:
        for (i = 0; i < numInterfaces; i++) {
            // FIXME the == needs work
            if (pInterfaceRequired[i] && (SL_IID_SEEK == pInterfaceIds[i])) {
                SL_LOGE("can't request SL_IID_SEEK with a buffer queue data source");
                return SL_RESULT_FEATURE_UNSUPPORTED;
            }
            if (pInterfaceRequired[i] && (SL_IID_MUTESOLO == pInterfaceIds[i]) &&
                    (SL_DATAFORMAT_PCM == pDataLocatorFormat->mFormat.mFormatType) &&
                    (1 == pDataLocatorFormat->mFormat.mPCM.numChannels)) {
                SL_LOGE("can't request SL_IID_MUTESOLO with a mono buffer queue data source");
                return SL_RESULT_FEATURE_UNSUPPORTED;
            }
        }
        break;
    default:
        break;
    }
    return SL_RESULT_SUCCESS;
}


/** \brief Free the local deep copy of a data format */

static void freeDataFormat(DataFormat *pDataFormat)
{
    switch (pDataFormat->mFormatType) {
    case SL_DATAFORMAT_MIME:
        if (NULL != pDataFormat->mMIME.mimeType) {
            free(pDataFormat->mMIME.mimeType);
            pDataFormat->mMIME.mimeType = NULL;
        }
        break;
    default:
        break;
    }
}


/** \brief Check a data source and make local deep copy */

SLresult checkDataSource(const SLDataSource *pDataSrc, DataLocatorFormat *pDataLocatorFormat)
{
    if (NULL == pDataSrc) {
        SL_LOGE("pDataSrc NULL");
        return SL_RESULT_PARAMETER_INVALID;
    }
    SLDataSource myDataSrc = *pDataSrc;
    SLresult result;
    result = checkDataLocator(myDataSrc.pLocator, &pDataLocatorFormat->mLocator);
    if (SL_RESULT_SUCCESS != result) {
        return result;
    }
    switch (pDataLocatorFormat->mLocator.mLocatorType) {

    case SL_DATALOCATOR_URI:
    case SL_DATALOCATOR_ADDRESS:
    case SL_DATALOCATOR_BUFFERQUEUE:
    case SL_DATALOCATOR_MIDIBUFFERQUEUE:
    case SL_DATALOCATOR_ANDROIDFD:
    case SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE:
        result = checkDataFormat(myDataSrc.pFormat, &pDataLocatorFormat->mFormat);
        if (SL_RESULT_SUCCESS != result) {
            freeDataLocator(&pDataLocatorFormat->mLocator);
            return result;
        }
        break;

    case SL_DATALOCATOR_NULL:
    case SL_DATALOCATOR_OUTPUTMIX:
    default:
        // invalid but fall through; the invalid locator will be caught later
        SL_LOGE("mLocatorType=%u", (unsigned) pDataLocatorFormat->mLocator.mLocatorType);
        // keep going

    case SL_DATALOCATOR_IODEVICE:
        // for these data locator types, ignore the pFormat as it might be uninitialized
        pDataLocatorFormat->mFormat.mFormatType = SL_DATAFORMAT_NULL;
        break;
    }

    pDataLocatorFormat->u.mSource.pLocator = &pDataLocatorFormat->mLocator;
    pDataLocatorFormat->u.mSource.pFormat = &pDataLocatorFormat->mFormat;
    return SL_RESULT_SUCCESS;
}


/** \brief Check a data sink and make local deep copy */

SLresult checkDataSink(const SLDataSink *pDataSink, DataLocatorFormat *pDataLocatorFormat,
        SLuint32 objType)
{
    if (NULL == pDataSink) {
        SL_LOGE("pDataSink NULL");
        return SL_RESULT_PARAMETER_INVALID;
    }
    SLDataSink myDataSink = *pDataSink;
    SLresult result;
    result = checkDataLocator(myDataSink.pLocator, &pDataLocatorFormat->mLocator);
    if (SL_RESULT_SUCCESS != result) {
        return result;
    }
    switch (pDataLocatorFormat->mLocator.mLocatorType) {

    case SL_DATALOCATOR_URI:
    case SL_DATALOCATOR_ADDRESS:
        result = checkDataFormat(myDataSink.pFormat, &pDataLocatorFormat->mFormat);
        if (SL_RESULT_SUCCESS != result) {
            freeDataLocator(&pDataLocatorFormat->mLocator);
            return result;
        }
        break;

    case SL_DATALOCATOR_BUFFERQUEUE:
    case SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE:
        if (SL_OBJECTID_AUDIOPLAYER == objType) {
            SL_LOGE("buffer queue can't be used as data sink for audio player");
            result = SL_RESULT_PARAMETER_INVALID;
        } else if (SL_OBJECTID_AUDIORECORDER == objType) {
#ifdef ANDROID
            if (SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE !=
                pDataLocatorFormat->mLocator.mLocatorType) {
                SL_LOGE("audio recorder source locator must be SL_DATALOCATOR_ANDROIDBUFFERQUEUE");
                result = SL_RESULT_PARAMETER_INVALID;
            } else {
                result = checkDataFormat(myDataSink.pFormat, &pDataLocatorFormat->mFormat);
            }
#else
            SL_LOGE("mLocatorType=%u", (unsigned) pDataLocatorFormat->mLocator.mLocatorType);
            result = SL_RESULT_PARAMETER_INVALID;
#endif
        }
        if (SL_RESULT_SUCCESS != result) {
            freeDataLocator(&pDataLocatorFormat->mLocator);
            return result;
        }
        break;

    case SL_DATALOCATOR_NULL:
    case SL_DATALOCATOR_MIDIBUFFERQUEUE:
    default:
        // invalid but fall through; the invalid locator will be caught later
        SL_LOGE("mLocatorType=%u", (unsigned) pDataLocatorFormat->mLocator.mLocatorType);
        // keep going

    case SL_DATALOCATOR_IODEVICE:
    case SL_DATALOCATOR_OUTPUTMIX:
        // for these data locator types, ignore the pFormat as it might be uninitialized
        pDataLocatorFormat->mFormat.mFormatType = SL_DATAFORMAT_NULL;
        break;
    }

    pDataLocatorFormat->u.mSink.pLocator = &pDataLocatorFormat->mLocator;
    pDataLocatorFormat->u.mSink.pFormat = &pDataLocatorFormat->mFormat;
    return SL_RESULT_SUCCESS;
}


/** \brief Free the local deep copy of a data locator format */

void freeDataLocatorFormat(DataLocatorFormat *dlf)
{
    freeDataLocator(&dlf->mLocator);
    freeDataFormat(&dlf->mFormat);
}


/* Interface initialization hooks */

extern void
    I3DCommit_init(void *),
    I3DDoppler_init(void *),
    I3DGrouping_deinit(void *),
    I3DGrouping_init(void *),
    I3DLocation_init(void *),
    I3DMacroscopic_init(void *),
    I3DSource_init(void *),
    IAndroidConfiguration_init(void *),
    IAndroidEffectCapabilities_init(void *),
    IAndroidEffectSend_init(void *),
    IAndroidEffect_init(void *),
    IAudioDecoderCapabilities_init(void *),
    IAudioEncoderCapabilities_init(void *),
    IAudioEncoder_init(void *),
    IAudioIODeviceCapabilities_init(void *),
    IBassBoost_init(void *),
    IBufferQueue_init(void *),
    IDeviceVolume_init(void *),
    IDynamicInterfaceManagement_init(void *),
    IDynamicSource_init(void *),
    IEffectSend_init(void *),
    IEngineCapabilities_init(void *),
    IEngine_init(void *),
    IEnvironmentalReverb_init(void *),
    IEqualizer_init(void *),
    ILEDArray_init(void *),
    IMIDIMessage_init(void *),
    IMIDIMuteSolo_init(void *),
    IMIDITempo_init(void *),
    IMIDITime_init(void *),
    IMetadataExtraction_init(void *),
    IMetadataTraversal_init(void *),
    IMuteSolo_init(void *),
    IObject_init(void *),
    IOutputMixExt_init(void *),
    IOutputMix_init(void *),
    IPitch_init(void *),
    IPlay_init(void *),
    IPlaybackRate_init(void *),
    IPrefetchStatus_init(void *),
    IPresetReverb_init(void *),
    IRatePitch_init(void *),
    IRecord_init(void *),
    ISeek_init(void *),
    IThreadSync_init(void *),
    IVibra_init(void *),
    IVirtualizer_init(void *),
    IVisualization_init(void *),
    IVolume_init(void *);

extern void
    IObject_deinit(void *);

#if !(USE_PROFILES & USE_PROFILES_MUSIC)
#define IDynamicSource_init         NULL
#define IMetadataExtraction_init    NULL
#define IMetadataTraversal_init     NULL
//#define IPlaybackRate_init          NULL
#define IVisualization_init         NULL
#endif

#if !(USE_PROFILES & USE_PROFILES_GAME)
#define I3DCommit_init      NULL
#define I3DDoppler_init     NULL
#define I3DGrouping_init    NULL
#define I3DLocation_init    NULL
#define I3DMacroscopic_init NULL
#define I3DSource_init      NULL
#define IMIDIMessage_init   NULL
#define IMIDIMuteSolo_init  NULL
#define IMIDITempo_init     NULL
#define IMIDITime_init      NULL
#define IPitch_init         NULL
#define IRatePitch_init     NULL
#define I3DGrouping_deinit  NULL
#endif

#if !(USE_PROFILES & USE_PROFILES_BASE)
#define IAudioDecoderCapabilities_init   NULL
#define IAudioEncoderCapabilities_init   NULL
#define IAudioEncoder_init               NULL
#define IAudioIODeviceCapabilities_init  NULL
#define IDeviceVolume_init               NULL
#define IDynamicInterfaceManagement_init NULL
#define IEngineCapabilities_init         NULL
#define IOutputMix_init                  NULL
#define IThreadSync_init                 NULL
#endif

#if !(USE_PROFILES & USE_PROFILES_OPTIONAL)
#define ILEDArray_init  NULL
#define IVibra_init     NULL
#endif

#ifndef ANDROID
#define IAndroidConfiguration_init      NULL
#define IAndroidEffect_init             NULL
#define IAndroidEffectCapabilities_init NULL
#define IAndroidEffectSend_init         NULL
#endif

#ifndef USE_OUTPUTMIXEXT
#define IOutputMixExt_init  NULL
#endif


/*static*/ const struct MPH_init MPH_init_table[MPH_MAX] = {
    { /* MPH_3DCOMMIT, */ I3DCommit_init, NULL, NULL },
    { /* MPH_3DDOPPLER, */ I3DDoppler_init, NULL, NULL },
    { /* MPH_3DGROUPING, */ I3DGrouping_init, NULL, I3DGrouping_deinit },
    { /* MPH_3DLOCATION, */ I3DLocation_init, NULL, NULL },
    { /* MPH_3DMACROSCOPIC, */ I3DMacroscopic_init, NULL, NULL },
    { /* MPH_3DSOURCE, */ I3DSource_init, NULL, NULL },
    { /* MPH_AUDIODECODERCAPABILITIES, */ IAudioDecoderCapabilities_init, NULL, NULL },
    { /* MPH_AUDIOENCODER, */ IAudioEncoder_init, NULL, NULL },
    { /* MPH_AUDIOENCODERCAPABILITIES, */ IAudioEncoderCapabilities_init, NULL, NULL },
    { /* MPH_AUDIOIODEVICECAPABILITIES, */ IAudioIODeviceCapabilities_init, NULL, NULL },
    { /* MPH_BASSBOOST, */ IBassBoost_init, NULL, NULL },
    { /* MPH_BUFFERQUEUE, */ IBufferQueue_init, NULL, NULL },
    { /* MPH_DEVICEVOLUME, */ IDeviceVolume_init, NULL, NULL },
    { /* MPH_DYNAMICINTERFACEMANAGEMENT, */ IDynamicInterfaceManagement_init, NULL, NULL },
    { /* MPH_DYNAMICSOURCE, */ IDynamicSource_init, NULL, NULL },
    { /* MPH_EFFECTSEND, */ IEffectSend_init, NULL, NULL },
    { /* MPH_ENGINE, */ IEngine_init, NULL, NULL },
    { /* MPH_ENGINECAPABILITIES, */ IEngineCapabilities_init, NULL, NULL },
    { /* MPH_ENVIRONMENTALREVERB, */ IEnvironmentalReverb_init, NULL, NULL },
    { /* MPH_EQUALIZER, */ IEqualizer_init, NULL, NULL },
    { /* MPH_LED, */ ILEDArray_init, NULL, NULL },
    { /* MPH_METADATAEXTRACTION, */ IMetadataExtraction_init, NULL, NULL },
    { /* MPH_METADATATRAVERSAL, */ IMetadataTraversal_init, NULL, NULL },
    { /* MPH_MIDIMESSAGE, */ IMIDIMessage_init, NULL, NULL },
    { /* MPH_MIDITIME, */ IMIDITime_init, NULL, NULL },
    { /* MPH_MIDITEMPO, */ IMIDITempo_init, NULL, NULL },
    { /* MPH_MIDIMUTESOLO, */ IMIDIMuteSolo_init, NULL, NULL },
    { /* MPH_MUTESOLO, */ IMuteSolo_init, NULL, NULL },
    { /* MPH_NULL, */ NULL, NULL, NULL },
    { /* MPH_OBJECT, */ IObject_init, NULL, IObject_deinit },
    { /* MPH_OUTPUTMIX, */ IOutputMix_init, NULL, NULL },
    { /* MPH_PITCH, */ IPitch_init, NULL, NULL },
    { /* MPH_PLAY, */ IPlay_init, NULL, NULL },
    { /* MPH_PLAYBACKRATE, */ IPlaybackRate_init, NULL, NULL },
    { /* MPH_PREFETCHSTATUS, */ IPrefetchStatus_init, NULL, NULL },
    { /* MPH_PRESETREVERB, */ IPresetReverb_init, NULL, NULL },
    { /* MPH_RATEPITCH, */ IRatePitch_init, NULL, NULL },
    { /* MPH_RECORD, */ IRecord_init, NULL, NULL },
    { /* MPH_SEEK, */ ISeek_init, NULL, NULL },
    { /* MPH_THREADSYNC, */ IThreadSync_init, NULL, NULL },
    { /* MPH_VIBRA, */ IVibra_init, NULL, NULL },
    { /* MPH_VIRTUALIZER, */ IVirtualizer_init, NULL, NULL },
    { /* MPH_VISUALIZATION, */ IVisualization_init, NULL, NULL },
    { /* MPH_VOLUME, */ IVolume_init, NULL, NULL },
    { /* MPH_OUTPUTMIXEXT, */ IOutputMixExt_init, NULL, NULL },
    { /* MPH_ANDROIDEFFECT */ IAndroidEffect_init, NULL, NULL },
    { /* MPH_ANDROIDEFFECTCAPABILITIES */ IAndroidEffectCapabilities_init, NULL, NULL },
    { /* MPH_ANDROIDEFFECTSEND */ IAndroidEffectSend_init, NULL, NULL },
    { /* MPH_ANDROIDCONFIGURATION */ IAndroidConfiguration_init, NULL, NULL },
    { /* MPH_ANDROIDSIMPLEBUFFERQUEUE, */ IBufferQueue_init /* alias */, NULL, NULL }
};


/** \brief Construct a new instance of the specified class, exposing selected interfaces */

IObject *construct(const ClassTable *class__, unsigned exposedMask, SLEngineItf engine)
{
    IObject *this;
    // Do not change this to malloc; we depend on the object being memset to zero
    this = (IObject *) calloc(1, class__->mSize);
    if (NULL != this) {
        unsigned lossOfControlMask = 0;
        // a NULL engine means we are constructing the engine
        IEngine *thisEngine = (IEngine *) engine;
        if (NULL == thisEngine) {
            thisEngine = &((CEngine *) this)->mEngine;
        } else {
            interface_lock_exclusive(thisEngine);
            if (MAX_INSTANCE <= thisEngine->mInstanceCount) {
                SL_LOGE("Too many objects");
                interface_unlock_exclusive(thisEngine);
                free(this);
                return NULL;
            }
            // pre-allocate a pending slot, but don't assign bit from mInstanceMask yet
            ++thisEngine->mInstanceCount;
            assert(((unsigned) ~0) != thisEngine->mInstanceMask);
            interface_unlock_exclusive(thisEngine);
            // const, no lock needed
            if (thisEngine->mLossOfControlGlobal) {
                lossOfControlMask = ~0;
            }
        }
        this->mLossOfControlMask = lossOfControlMask;
        this->mClass = class__;
        this->mEngine = thisEngine;
        const struct iid_vtable *x = class__->mInterfaces;
        SLuint8 *interfaceStateP = this->mInterfaceStates;
        SLuint32 index;
        for (index = 0; index < class__->mInterfaceCount; ++index, ++x, exposedMask >>= 1) {
            SLuint8 state;
            if (exposedMask & 1) {
                void *self = (char *) this + x->mOffset;
                // IObject does not have an mThis, so [1] is not always defined
                if (index) {
                    ((IObject **) self)[1] = this;
                }
                VoidHook init = MPH_init_table[x->mMPH].mInit;
                // paranoid double-check for presence of an initialization hook
                if (NULL != init) {
                    (*init)(self);
                }
                // IObject does not require a call to GetInterface
                if (index) {
                    ((size_t *) self)[0] ^= ~0;
                }
                state = INTERFACE_EXPOSED;
            } else {
                state = INTERFACE_UNINITIALIZED;
            }
            *interfaceStateP++ = state;
        }
        // note that the new object is not yet published; creator must call IObject_Publish
    }
    return this;
}


/* Initial global entry points */


/** \brief slCreateEngine Function */

SLresult SLAPIENTRY slCreateEngine(SLObjectItf *pEngine, SLuint32 numOptions,
    const SLEngineOption *pEngineOptions, SLuint32 numInterfaces,
    const SLInterfaceID *pInterfaceIds, const SLboolean *pInterfaceRequired)
{
    SL_ENTER_GLOBAL

    do {

#ifdef ANDROID
        android::ProcessState::self()->startThreadPool();
#ifndef USE_BACKPORT
        android::DataSource::RegisterDefaultSniffers();
#endif
#endif

        if (NULL == pEngine) {
            result = SL_RESULT_PARAMETER_INVALID;
            break;
        }
        *pEngine = NULL;

        if ((0 < numOptions) && (NULL == pEngineOptions)) {
            SL_LOGE("numOptions=%lu and pEngineOptions=NULL", numOptions);
            result = SL_RESULT_PARAMETER_INVALID;
            break;
        }

        // default values
        SLboolean threadSafe = SL_BOOLEAN_TRUE;
        SLboolean lossOfControlGlobal = SL_BOOLEAN_FALSE;

        // process engine options
        SLuint32 i;
        const SLEngineOption *option = pEngineOptions;
        result = SL_RESULT_SUCCESS;
        for (i = 0; i < numOptions; ++i, ++option) {
            switch (option->feature) {
            case SL_ENGINEOPTION_THREADSAFE:
                threadSafe = SL_BOOLEAN_FALSE != (SLboolean) option->data; // normalize
                break;
            case SL_ENGINEOPTION_LOSSOFCONTROL:
                lossOfControlGlobal = SL_BOOLEAN_FALSE != (SLboolean) option->data; // normalize
                break;
            default:
                SL_LOGE("unknown engine option: feature=%lu data=%lu",
                    option->feature, option->data);
                result = SL_RESULT_PARAMETER_INVALID;
                break;
            }
        }
        if (SL_RESULT_SUCCESS != result) {
            break;
        }

        unsigned exposedMask;
        const ClassTable *pCEngine_class = objectIDtoClass(SL_OBJECTID_ENGINE);
        assert(NULL != pCEngine_class);
        result = checkInterfaces(pCEngine_class, numInterfaces,
            pInterfaceIds, pInterfaceRequired, &exposedMask);
        if (SL_RESULT_SUCCESS != result) {
            break;
        }

        CEngine *this = (CEngine *) construct(pCEngine_class, exposedMask, NULL);
        if (NULL == this) {
            result = SL_RESULT_MEMORY_FAILURE;
            break;
        }

        this->mObject.mLossOfControlMask = lossOfControlGlobal ? ~0 : 0;
        this->mEngine.mLossOfControlGlobal = lossOfControlGlobal;
        this->mEngineCapabilities.mThreadSafe = threadSafe;
        *pEngine = &this->mObject.mItf;

    } while(0);

    SL_LEAVE_GLOBAL
}


/** \brief slQueryNumSupportedEngineInterfaces Function */

SLresult SLAPIENTRY slQueryNumSupportedEngineInterfaces(SLuint32 *pNumSupportedInterfaces)
{
    SL_ENTER_GLOBAL

    if (NULL == pNumSupportedInterfaces) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        const ClassTable *class__ = objectIDtoClass(SL_OBJECTID_ENGINE);
        assert(NULL != class__);
        SLuint32 count = 0;
        SLuint32 i;
        for (i = 0; i < class__->mInterfaceCount; ++i) {
            switch (class__->mInterfaces[i].mInterface) {
            case INTERFACE_IMPLICIT:
            case INTERFACE_EXPLICIT:
            case INTERFACE_EXPLICIT_PREREALIZE:
            case INTERFACE_DYNAMIC:
                ++count;
                break;
            case INTERFACE_UNAVAILABLE:
                break;
            default:
                assert(false);
                break;
            }
        }
        *pNumSupportedInterfaces = count;
        result = SL_RESULT_SUCCESS;
    }

    SL_LEAVE_GLOBAL
}


/** \brief slQuerySupportedEngineInterfaces Function */

SLresult SLAPIENTRY slQuerySupportedEngineInterfaces(SLuint32 index, SLInterfaceID *pInterfaceId)
{
    SL_ENTER_GLOBAL

    if (NULL == pInterfaceId) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        *pInterfaceId = NULL;
        const ClassTable *class__ = objectIDtoClass(SL_OBJECTID_ENGINE);
        assert(NULL != class__);
        result = SL_RESULT_PARAMETER_INVALID;   // will be reset later
        SLuint32 i;
        for (i = 0; i < class__->mInterfaceCount; ++i) {
            switch (class__->mInterfaces[i].mInterface) {
            case INTERFACE_IMPLICIT:
            case INTERFACE_EXPLICIT:
            case INTERFACE_EXPLICIT_PREREALIZE:
            case INTERFACE_DYNAMIC:
                break;
            case INTERFACE_UNAVAILABLE:
                continue;
            default:
                assert(false);
                break;
            }
            if (index == 0) {
                // The engine has no aliases, but if it did, this would return only the primary
                *pInterfaceId = &SL_IID_array[class__->mInterfaces[i].mMPH];
                result = SL_RESULT_SUCCESS;
                break;
            }
            --index;
        }
    }

    SL_LEAVE_GLOBAL
}
