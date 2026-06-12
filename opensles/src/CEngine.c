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

/** \file CEngine.c Engine class */

#include "sles_allinclusive.h"


/** \brief Hook called by Object::Realize when an engine is realized */

SLresult CEngine_Realize(void *self, SLboolean async)
{
    CEngine *this = (CEngine *) self;
    // create the sync thread
    int err = pthread_create(&this->mSyncThread, (const pthread_attr_t *) NULL, sync_start, this);
    SLresult result = err_to_result(err);
    if (SL_RESULT_SUCCESS != result)
        return result;
    // initialize the thread pool for asynchronous operations
    result = ThreadPool_init(&this->mEngine.mThreadPool, 0, 0);
    if (SL_RESULT_SUCCESS != result) {
        this->mEngine.mShutdown = SL_BOOLEAN_TRUE;
        (void) pthread_join(this->mSyncThread, (void **) NULL);
        return result;
    }
#ifdef USE_SDL
    SDL_open(&this->mEngine);
#endif
    return SL_RESULT_SUCCESS;
}


/** \brief Hook called by Object::Resume when an engine is resumed */

SLresult CEngine_Resume(void *self, SLboolean async)
{
    return SL_RESULT_SUCCESS;
}


/** \brief Hook called by Object::Destroy when an engine is destroyed */

void CEngine_Destroy(void *self)
{
    CEngine *this = (CEngine *) self;

    // Verify that there are no extant objects
    unsigned instanceCount = this->mEngine.mInstanceCount;
    unsigned instanceMask = this->mEngine.mInstanceMask;
    if ((0 < instanceCount) || (0 != instanceMask)) {
        SL_LOGE("Object::Destroy(%p) for engine ignored; %u total active objects",
            this, instanceCount);
        while (0 != instanceMask) {
            unsigned i = ctz(instanceMask);
            assert(MAX_INSTANCE > i);
            SL_LOGE("Object::Destroy(%p) for engine ignored; active object ID %u at %p",
                this, i + 1, this->mEngine.mInstances[i]);
            instanceMask &= ~(1 << i);
        }
    }

    // If engine was created but not realized, there will be no sync thread yet
    pthread_t zero;
    memset(&zero, 0, sizeof(pthread_t));
    if (0 != memcmp(&zero, &this->mSyncThread, sizeof(pthread_t))) {

        // Announce to the sync thread that engine is shutting down; it polls so should see it soon
        this->mEngine.mShutdown = SL_BOOLEAN_TRUE;
        // Wait for the sync thread to acknowledge the shutdown
        while (!this->mEngine.mShutdownAck) {
            object_cond_wait(&this->mObject);
        }
        // The sync thread should have exited by now, so collect it by joining
        (void) pthread_join(this->mSyncThread, (void **) NULL);

    }

    // Shutdown the thread pool used for asynchronous operations (there should not be any)
    ThreadPool_deinit(&this->mEngine.mThreadPool);

#if defined(ANDROID) && !defined(USE_BACKPORT)
    // free effect data
    //   free EQ data
    if ((0 < this->mEngine.mEqNumPresets) && (NULL != this->mEngine.mEqPresetNames)) {
        for(uint32_t i = 0 ; i < this->mEngine.mEqNumPresets ; i++) {
            if (NULL != this->mEngine.mEqPresetNames[i]) {
                delete [] this->mEngine.mEqPresetNames[i];
            }
        }
        delete [] this->mEngine.mEqPresetNames;
    }
    //   free effect library data
    if (NULL != this->mAndroidEffectCapabilities.mFxDescriptors) {
        delete [] this->mAndroidEffectCapabilities.mFxDescriptors;
    }
#endif

#ifdef USE_SDL
    SDL_close();
#endif

}


/** \brief Hook called by Object::Destroy before an engine is about to be destroyed */

bool CEngine_PreDestroy(void *self)
{
    return true;
}
