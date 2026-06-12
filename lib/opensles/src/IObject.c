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

/* Object implementation */

#include "sles_allinclusive.h"


// Called by a worker thread to handle an asynchronous Object.Realize.
// Parameter self is the Object.

static void HandleRealize(void *self, int unused)
{

    // validate input parameters
    IObject *this = (IObject *) self;
    assert(NULL != this);
    const ClassTable *class__ = this->mClass;
    assert(NULL != class__);
    AsyncHook realize = class__->mRealize;
    SLresult result;
    SLuint8 state;

    // check object state
    object_lock_exclusive(this);
    state = this->mState;
    switch (state) {

    case SL_OBJECT_STATE_REALIZING_1:   // normal case
        if (NULL != realize) {
            this->mState = SL_OBJECT_STATE_REALIZING_2;
            object_unlock_exclusive(this);
            // Note that the mutex is unlocked during the realize hook
            result = (*realize)(this, SL_BOOLEAN_TRUE);
            object_lock_exclusive(this);
            assert(SL_OBJECT_STATE_REALIZING_2 == this->mState);
            state = SL_RESULT_SUCCESS == result ? SL_OBJECT_STATE_REALIZED :
                SL_OBJECT_STATE_UNREALIZED;
        } else {
            result = SL_RESULT_SUCCESS;
            state = SL_OBJECT_STATE_REALIZED;
        }
        break;

    case SL_OBJECT_STATE_REALIZING_1A:  // operation was aborted while on work queue
        result = SL_RESULT_OPERATION_ABORTED;
        state = SL_OBJECT_STATE_UNREALIZED;
        break;

    default:                            // impossible
        assert(SL_BOOLEAN_FALSE);
        result = SL_RESULT_INTERNAL_ERROR;
        break;

    }

    // mutex is locked, update state
    this->mState = state;

    // Make a copy of these, so we can call the callback with mutex unlocked
    slObjectCallback callback = this->mCallback;
    void *context = this->mContext;
    object_unlock_exclusive(this);

    // Note that the mutex is unlocked during the callback
    if (NULL != callback) {
        (*callback)(&this->mItf, context, SL_OBJECT_EVENT_ASYNC_TERMINATION, result, state, NULL);
    }
}


static SLresult IObject_Realize(SLObjectItf self, SLboolean async)
{
    SL_ENTER_INTERFACE

    IObject *this = (IObject *) self;
    SLuint8 state;
    const ClassTable *class__ = this->mClass;
    object_lock_exclusive(this);
    state = this->mState;
    // Reject redundant calls to Realize
    if (SL_OBJECT_STATE_UNREALIZED != state) {
        object_unlock_exclusive(this);
        result = SL_RESULT_PRECONDITIONS_VIOLATED;
    } else {
        // Asynchronous: mark operation pending and cancellable
        if (async && (SL_OBJECTID_ENGINE != class__->mObjectID)) {
            state = SL_OBJECT_STATE_REALIZING_1;
        // Synchronous: mark operation pending and non-cancellable
        } else {
            state = SL_OBJECT_STATE_REALIZING_2;
        }
        this->mState = state;
        object_unlock_exclusive(this);
        switch (state) {
        case SL_OBJECT_STATE_REALIZING_1: // asynchronous on non-Engine
            assert(async);
            result = ThreadPool_add(&this->mEngine->mThreadPool, HandleRealize, this, 0);
            if (SL_RESULT_SUCCESS != result) {
                // Engine was destroyed during realize, or insufficient memory
                object_lock_exclusive(this);
                this->mState = SL_OBJECT_STATE_UNREALIZED;
                object_unlock_exclusive(this);
            }
            break;
        case SL_OBJECT_STATE_REALIZING_2: // synchronous, or asynchronous on Engine
            {
            AsyncHook realize = class__->mRealize;
            // Note that the mutex is unlocked during the realize hook
            result = (NULL != realize) ? (*realize)(this, async) : SL_RESULT_SUCCESS;
            object_lock_exclusive(this);
            assert(SL_OBJECT_STATE_REALIZING_2 == this->mState);
            state = (SL_RESULT_SUCCESS == result) ? SL_OBJECT_STATE_REALIZED :
                SL_OBJECT_STATE_UNREALIZED;
            this->mState = state;
            slObjectCallback callback = this->mCallback;
            void *context = this->mContext;
            object_unlock_exclusive(this);
            // asynchronous Realize on an Engine is actually done synchronously, but still has
            // callback because there is no thread pool yet to do it asynchronously.
            if (async && (NULL != callback)) {
                (*callback)(&this->mItf, context, SL_OBJECT_EVENT_ASYNC_TERMINATION, result, state,
                    NULL);
            }
            }
            break;
        default:                          // impossible
            assert(SL_BOOLEAN_FALSE);
            break;
        }
    }

    SL_LEAVE_INTERFACE
}


// Called by a worker thread to handle an asynchronous Object.Resume.
// Parameter self is the Object.

static void HandleResume(void *self, int unused)
{

    // valid input parameters
    IObject *this = (IObject *) self;
    assert(NULL != this);
    const ClassTable *class__ = this->mClass;
    assert(NULL != class__);
    AsyncHook resume = class__->mResume;
    SLresult result;
    SLuint8 state;

    // check object state
    object_lock_exclusive(this);
    state = this->mState;
    switch (state) {

    case SL_OBJECT_STATE_RESUMING_1:    // normal case
        if (NULL != resume) {
            this->mState = SL_OBJECT_STATE_RESUMING_2;
            object_unlock_exclusive(this);
            // Note that the mutex is unlocked during the resume hook
            result = (*resume)(this, SL_BOOLEAN_TRUE);
            object_lock_exclusive(this);
            assert(SL_OBJECT_STATE_RESUMING_2 == this->mState);
            state = SL_RESULT_SUCCESS == result ? SL_OBJECT_STATE_REALIZED :
                SL_OBJECT_STATE_SUSPENDED;
        } else {
            result = SL_RESULT_SUCCESS;
            state = SL_OBJECT_STATE_REALIZED;
        }
        break;

    case SL_OBJECT_STATE_RESUMING_1A:   // operation was aborted while on work queue
        result = SL_RESULT_OPERATION_ABORTED;
        state = SL_OBJECT_STATE_SUSPENDED;
        break;

    default:                            // impossible
        assert(SL_BOOLEAN_FALSE);
        result = SL_RESULT_INTERNAL_ERROR;
        break;

    }

    // mutex is unlocked, update state
    this->mState = state;

    // Make a copy of these, so we can call the callback with mutex unlocked
    slObjectCallback callback = this->mCallback;
    void *context = this->mContext;
    object_unlock_exclusive(this);

    // Note that the mutex is unlocked during the callback
    if (NULL != callback) {
        (*callback)(&this->mItf, context, SL_OBJECT_EVENT_ASYNC_TERMINATION, result, state, NULL);
    }
}


static SLresult IObject_Resume(SLObjectItf self, SLboolean async)
{
    SL_ENTER_INTERFACE

    IObject *this = (IObject *) self;
    const ClassTable *class__ = this->mClass;
    SLuint8 state;
    object_lock_exclusive(this);
    state = this->mState;
    // Reject redundant calls to Resume
    if (SL_OBJECT_STATE_SUSPENDED != state) {
        object_unlock_exclusive(this);
        result = SL_RESULT_PRECONDITIONS_VIOLATED;
    } else {
        // Asynchronous: mark operation pending and cancellable
        if (async) {
            state = SL_OBJECT_STATE_RESUMING_1;
        // Synchronous: mark operatio pending and non-cancellable
        } else {
            state = SL_OBJECT_STATE_RESUMING_2;
        }
        this->mState = state;
        object_unlock_exclusive(this);
        switch (state) {
        case SL_OBJECT_STATE_RESUMING_1: // asynchronous
            assert(async);
            result = ThreadPool_add(&this->mEngine->mThreadPool, HandleResume, this, 0);
            if (SL_RESULT_SUCCESS != result) {
                // Engine was destroyed during resume, or insufficient memory
                object_lock_exclusive(this);
                this->mState = SL_OBJECT_STATE_SUSPENDED;
                object_unlock_exclusive(this);
            }
            break;
        case SL_OBJECT_STATE_RESUMING_2: // synchronous
            {
            AsyncHook resume = class__->mResume;
            // Note that the mutex is unlocked during the resume hook
            result = (NULL != resume) ? (*resume)(this, SL_BOOLEAN_FALSE) : SL_RESULT_SUCCESS;
            object_lock_exclusive(this);
            assert(SL_OBJECT_STATE_RESUMING_2 == this->mState);
            this->mState = (SL_RESULT_SUCCESS == result) ? SL_OBJECT_STATE_REALIZED :
                SL_OBJECT_STATE_SUSPENDED;
            object_unlock_exclusive(this);
            }
            break;
        default:                        // impossible
            assert(SL_BOOLEAN_FALSE);
            break;
        }
    }

    SL_LEAVE_INTERFACE
}


static SLresult IObject_GetState(SLObjectItf self, SLuint32 *pState)
{
    SL_ENTER_INTERFACE

    if (NULL == pState) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IObject *this = (IObject *) self;
        // Note that the state is immediately obsolete, so a peek lock is safe
        object_lock_peek(this);
        SLuint8 state = this->mState;
        object_unlock_peek(this);
        // Re-map the realizing, resuming, and suspending states
        switch (state) {
        case SL_OBJECT_STATE_REALIZING_1:
        case SL_OBJECT_STATE_REALIZING_1A:
        case SL_OBJECT_STATE_REALIZING_2:
        case SL_OBJECT_STATE_DESTROYING:    // application shouldn't call GetState after Destroy
            state = SL_OBJECT_STATE_UNREALIZED;
            break;
        case SL_OBJECT_STATE_RESUMING_1:
        case SL_OBJECT_STATE_RESUMING_1A:
        case SL_OBJECT_STATE_RESUMING_2:
        case SL_OBJECT_STATE_SUSPENDING:
            state = SL_OBJECT_STATE_SUSPENDED;
            break;
        case SL_OBJECT_STATE_UNREALIZED:
        case SL_OBJECT_STATE_REALIZED:
        case SL_OBJECT_STATE_SUSPENDED:
            // These are the "official" object states, return them as is
            break;
        default:
            assert(SL_BOOLEAN_FALSE);
            break;
        }
        *pState = state;
        result = SL_RESULT_SUCCESS;
    }

    SL_LEAVE_INTERFACE
}

static SLresult IObject_GetInterface(SLObjectItf self, const SLInterfaceID iid, void *pInterface)
{
    SL_ENTER_INTERFACE

    if (NULL == pInterface) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        void *interface = NULL;
        if (NULL == iid) {
            result = SL_RESULT_PARAMETER_INVALID;
        } else {
            IObject *this = (IObject *) self;
            const ClassTable *class__ = this->mClass;
            int MPH, index;
            if ((0 > (MPH = IID_to_MPH(iid))) ||
                    // there must an initialization hook present
                    (NULL == MPH_init_table[MPH].mInit) ||
                    (0 > (index = class__->mMPH_to_index[MPH]))) {
                result = SL_RESULT_FEATURE_UNSUPPORTED;
            } else {
                unsigned mask = 1 << index;
                object_lock_exclusive(this);
                if ((SL_OBJECT_STATE_REALIZED != this->mState) && (INTERFACE_EXPLICIT_PREREALIZE !=
                        class__->mInterfaces[index].mInterface)) {
                    // Can't get interface on a suspended/suspending/resuming object
                    // unless this is an explicit pre-realize interface
                    result = SL_RESULT_PRECONDITIONS_VIOLATED;
                } else if ((MPH_MUTESOLO == MPH) && (SL_OBJECTID_AUDIOPLAYER == class__->mObjectID)
                        && (1 == ((CAudioPlayer *) this)->mNumChannels)) {
                    // Can't get the MuteSolo interface of an audio player if the channel count is
                    // mono, but _can_ get the MuteSolo interface if the channel count is unknown
                    result = SL_RESULT_FEATURE_UNSUPPORTED;
                } else {
                    switch (this->mInterfaceStates[index]) {
                    case INTERFACE_EXPOSED:
                    case INTERFACE_ADDED:
                        interface = (char *) this + class__->mInterfaces[index].mOffset;
                        // Note that interface has been gotten,
                        // for debugger and to detect incorrect use of interfaces
                        if (!(this->mGottenMask & mask)) {
                            this->mGottenMask |= mask;
                            ((size_t *) interface)[0] ^= ~0;
                        }
                        result = SL_RESULT_SUCCESS;
                        break;
                    // Can't get interface if uninitialized/suspend(ed,ing)/resuming/adding/removing
                    default:
                        result = SL_RESULT_FEATURE_UNSUPPORTED;
                        break;
                    }
                }
                object_unlock_exclusive(this);
            }
        }
        *(void **)pInterface = interface;
    }

    SL_LEAVE_INTERFACE
}


static SLresult IObject_RegisterCallback(SLObjectItf self,
    slObjectCallback callback, void *pContext)
{
    SL_ENTER_INTERFACE

    IObject *this = (IObject *) self;
    object_lock_exclusive(this);
    this->mCallback = callback;
    this->mContext = pContext;
    object_unlock_exclusive(this);
    result = SL_RESULT_SUCCESS;

    SL_LEAVE_INTERFACE
}


/** \brief This is internal common code for Abort and Destroy.
 *  Note: called with mutex unlocked, and returns with mutex locked.
 */

static void Abort_internal(IObject *this)
{
    const ClassTable *class__ = this->mClass;
    bool anyAsync = false;
    object_lock_exclusive(this);

    // Abort asynchronous operations on the object
    switch (this->mState) {
    case SL_OBJECT_STATE_REALIZING_1:   // Realize
        this->mState = SL_OBJECT_STATE_REALIZING_1A;
        anyAsync = true;
        break;
    case SL_OBJECT_STATE_RESUMING_1:    // Resume
        this->mState = SL_OBJECT_STATE_RESUMING_1A;
        anyAsync = true;
        break;
    case SL_OBJECT_STATE_REALIZING_1A:  // Realize
    case SL_OBJECT_STATE_REALIZING_2:
    case SL_OBJECT_STATE_RESUMING_1A:   // Resume
    case SL_OBJECT_STATE_RESUMING_2:
        anyAsync = true;
        break;
    case SL_OBJECT_STATE_DESTROYING:
        assert(false);
        break;
    default:
        break;
    }

    // Abort asynchronous operations on interfaces
    SLuint8 *interfaceStateP = this->mInterfaceStates;
    unsigned index;
    for (index = 0; index < class__->mInterfaceCount; ++index, ++interfaceStateP) {
        switch (*interfaceStateP) {
        case INTERFACE_ADDING_1:    // AddInterface
            *interfaceStateP = INTERFACE_ADDING_1A;
            anyAsync = true;
            break;
        case INTERFACE_RESUMING_1:  // ResumeInterface
            *interfaceStateP = INTERFACE_RESUMING_1A;
            anyAsync = true;
            break;
        case INTERFACE_ADDING_1A:   // AddInterface
        case INTERFACE_ADDING_2:
        case INTERFACE_RESUMING_1A: // ResumeInterface
        case INTERFACE_RESUMING_2:
        case INTERFACE_REMOVING:    // RemoveInterface is sync, but partially without a mutex lock
            anyAsync = true;
            break;
        default:
            break;
        }
    }

    // Wait until all asynchronous operations either complete normally or recognize the abort
    while (anyAsync) {
        object_unlock_exclusive(this);
        // FIXME should use condition variable instead of polling
        usleep(20000);
        anyAsync = false;
        object_lock_exclusive(this);
        switch (this->mState) {
        case SL_OBJECT_STATE_REALIZING_1:   // state 1 means it cycled during the usleep window
        case SL_OBJECT_STATE_RESUMING_1:
        case SL_OBJECT_STATE_REALIZING_1A:
        case SL_OBJECT_STATE_REALIZING_2:
        case SL_OBJECT_STATE_RESUMING_1A:
        case SL_OBJECT_STATE_RESUMING_2:
            anyAsync = true;
            break;
        case SL_OBJECT_STATE_DESTROYING:
            assert(false);
            break;
        default:
            break;
        }
        interfaceStateP = this->mInterfaceStates;
        for (index = 0; index < class__->mInterfaceCount; ++index, ++interfaceStateP) {
            switch (*interfaceStateP) {
            case INTERFACE_ADDING_1:    // state 1 means it cycled during the usleep window
            case INTERFACE_RESUMING_1:
            case INTERFACE_ADDING_1A:
            case INTERFACE_ADDING_2:
            case INTERFACE_RESUMING_1A:
            case INTERFACE_RESUMING_2:
            case INTERFACE_REMOVING:
                anyAsync = true;
                break;
            default:
                break;
            }
        }
    }

    // At this point there are no pending asynchronous operations
}


static void IObject_AbortAsyncOperation(SLObjectItf self)
{
    SL_ENTER_INTERFACE_VOID

    IObject *this = (IObject *) self;
    Abort_internal(this);
    object_unlock_exclusive(this);

    SL_LEAVE_INTERFACE_VOID
}

#ifdef SYBERIA
extern IObject *players[20];
#endif

void IObject_Destroy(SLObjectItf self)
{
    SL_ENTER_INTERFACE_VOID

    IObject *this = (IObject *) self;
#ifdef SYBERIA
	for (int i = 0; i < 20; i++) {
		if (players[i] == self) {
			players[i] = NULL;
			break;
		}
	}
#endif
    // mutex is unlocked
    Abort_internal(this);
    // mutex is locked
    const ClassTable *class__ = this->mClass;
    BoolHook preDestroy = class__->mPreDestroy;
    // The pre-destroy hook is called with mutex locked, and should block until it is safe to
    // destroy.  It is OK to unlock the mutex temporarily, as it long as it re-locks the mutex
    // before returning.
    if (NULL != preDestroy) {
        bool okToDestroy = (*preDestroy)(this);
        if (!okToDestroy) {
            object_unlock_exclusive(this);
            // unfortunately Destroy doesn't return a result
            SL_LOGE("Object::Destroy(%p) not allowed", this);
            SL_LEAVE_INTERFACE_VOID
        }
    }
    this->mState = SL_OBJECT_STATE_DESTROYING;
    VoidHook destroy = class__->mDestroy;
    // const, no lock needed
    IEngine *thisEngine = this->mEngine;
    unsigned i = this->mInstanceID;
    assert(MAX_INSTANCE >= i);
    // avoid a recursive lock on the engine when destroying the engine itself
    if (thisEngine->mThis != this) {
        interface_lock_exclusive(thisEngine);
    }
    // An unpublished object has a slot reserved, but the ID hasn't been chosen yet
    assert(0 < thisEngine->mInstanceCount);
    --thisEngine->mInstanceCount;
    // If object is published, then remove it from exposure to sync thread and debugger
    if (0 != i) {
        --i;
        assert(0 != thisEngine->mInstanceMask);
        thisEngine->mInstanceMask &= ~(1 << i);
        assert(thisEngine->mInstances[i] == this);
        thisEngine->mInstances[i] = NULL;
    }
    // avoid a recursive unlock on the engine when destroying the engine itself
    if (thisEngine->mThis != this) {
        interface_unlock_exclusive(thisEngine);
    }
    // The destroy hook is called with mutex locked
    if (NULL != destroy) {
        (*destroy)(this);
    }
    // Call the deinitializer for each currently exposed interface,
    // whether it is implicit, explicit, optional, or dynamically added.
    // The deinitializers are called in the reverse order that the
    // initializers were called, so that IObject_deinit is called last.
    unsigned incorrect = 0;
    unsigned index = class__->mInterfaceCount;
    const struct iid_vtable *x = &class__->mInterfaces[index];
    SLuint8 *interfaceStateP = &this->mInterfaceStates[index];
    for ( ; index > 0; --index) {
        --x;
        SLuint32 state = *--interfaceStateP;
        switch (state) {
        case INTERFACE_UNINITIALIZED:
            break;
        case INTERFACE_EXPOSED:     // quiescent states
        case INTERFACE_ADDED:
        case INTERFACE_SUSPENDED:
            {
            VoidHook deinit = MPH_init_table[x->mMPH].mDeinit;
            if (NULL != deinit) {
                (*deinit)((char *) this + x->mOffset);
            }
            }
            break;
        case INTERFACE_ADDING_1:    // active states indicate incorrect use of API
        case INTERFACE_ADDING_1A:
        case INTERFACE_ADDING_2:
        case INTERFACE_RESUMING_1:
        case INTERFACE_RESUMING_1A:
        case INTERFACE_RESUMING_2:
        case INTERFACE_REMOVING:
        case INTERFACE_SUSPENDING:
            ++incorrect;
            break;
        default:
            assert(SL_BOOLEAN_FALSE);
            break;
        }
    }
    // The mutex is unlocked and destroyed by IObject_deinit, which is the last deinitializer
#ifdef USE_DEBUG
    memset(this, 0x55, class__->mSize);
#endif
    free(this);
    // one or more interfaces was actively changing at time of Destroy
    assert(incorrect == 0);

    SL_LEAVE_INTERFACE_VOID
}


static SLresult IObject_SetPriority(SLObjectItf self, SLint32 priority, SLboolean preemptable)
{
    SL_ENTER_INTERFACE

#if USE_PROFILES & USE_PROFILES_BASE
    IObject *this = (IObject *) self;
    object_lock_exclusive(this);
    this->mPriority = priority;
    this->mPreemptable = SL_BOOLEAN_FALSE != preemptable; // normalize
    object_unlock_exclusive(this);
    result = SL_RESULT_SUCCESS;
#else
    result = SL_RESULT_FEATURE_UNSUPPORTED;
#endif

    SL_LEAVE_INTERFACE
}


static SLresult IObject_GetPriority(SLObjectItf self, SLint32 *pPriority, SLboolean *pPreemptable)
{
    SL_ENTER_INTERFACE

#if USE_PROFILES & USE_PROFILES_BASE
    if (NULL == pPriority || NULL == pPreemptable) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IObject *this = (IObject *) self;
        object_lock_shared(this);
        SLint32 priority = this->mPriority;
        SLboolean preemptable = this->mPreemptable;
        object_unlock_shared(this);
        *pPriority = priority;
        *pPreemptable = preemptable;
        result = SL_RESULT_SUCCESS;
    }
#else
    result = SL_RESULT_FEATURE_UNSUPPORTED;
#endif

    SL_LEAVE_INTERFACE
}


static SLresult IObject_SetLossOfControlInterfaces(SLObjectItf self,
    SLint16 numInterfaces, SLInterfaceID *pInterfaceIDs, SLboolean enabled)
{
    SL_ENTER_INTERFACE

#if USE_PROFILES & USE_PROFILES_BASE
    result = SL_RESULT_SUCCESS;
    if (0 < numInterfaces) {
        SLuint32 i;
        if (NULL == pInterfaceIDs) {
            result = SL_RESULT_PARAMETER_INVALID;
        } else {
            IObject *this = (IObject *) self;
            const ClassTable *class__ = this->mClass;
            unsigned lossOfControlMask = 0;
            // The cast is due to a typo in the spec, bug 6482
            for (i = 0; i < (SLuint32) numInterfaces; ++i) {
                SLInterfaceID iid = pInterfaceIDs[i];
                if (NULL == iid) {
                    result = SL_RESULT_PARAMETER_INVALID;
                    goto out;
                }
                int MPH, index;
                // We ignore without error any invalid MPH or index, but spec is unclear
                if ((0 <= (MPH = IID_to_MPH(iid))) &&
                        // no need to check for an initialization hook
                        // (NULL == MPH_init_table[MPH].mInit) ||
                        (0 <= (index = class__->mMPH_to_index[MPH]))) {
                    lossOfControlMask |= (1 << index);
                }
            }
            object_lock_exclusive(this);
            if (enabled) {
                this->mLossOfControlMask |= lossOfControlMask;
            } else {
                this->mLossOfControlMask &= ~lossOfControlMask;
            }
            object_unlock_exclusive(this);
        }
    }
out:
#else
    result = SL_RESULT_FEATURE_UNSUPPORTED;
#endif

    SL_LEAVE_INTERFACE
}


static const struct SLObjectItf_ IObject_Itf = {
    IObject_Realize,
    IObject_Resume,
    IObject_GetState,
    IObject_GetInterface,
    IObject_RegisterCallback,
    IObject_AbortAsyncOperation,
    IObject_Destroy,
    IObject_SetPriority,
    IObject_GetPriority,
    IObject_SetLossOfControlInterfaces,
};


/** \brief This must be the first initializer called for an object */

void IObject_init(void *self)
{
    IObject *this = (IObject *) self;
    this->mItf = &IObject_Itf;
    // initialized in construct:
    // mClass
    // mInstanceID
    // mLossOfControlMask
    // mEngine
    // mInstanceStates
    this->mState = SL_OBJECT_STATE_UNREALIZED;
    this->mGottenMask = 1;  // IObject
    this->mAttributesMask = 0;
    this->mCallback = NULL;
    this->mContext = NULL;
#if USE_PROFILES & USE_PROFILES_BASE
    this->mPriority = SL_PRIORITY_NORMAL;
    this->mPreemptable = SL_BOOLEAN_FALSE;
#endif
    this->mStrongRefCount = 0;
    int ok;
    ok = pthread_mutex_init(&this->mMutex, (const pthread_mutexattr_t *) NULL);
    assert(0 == ok);
#ifdef USE_DEBUG
    memset(&this->mOwner, 0, sizeof(pthread_t));
    this->mFile = NULL;
    this->mLine = 0;
#endif
    ok = pthread_cond_init(&this->mCond, (const pthread_condattr_t *) NULL);
    assert(0 == ok);
}


/** \brief This must be the last deinitializer called for an object */

void IObject_deinit(void *self)
{
    IObject *this = (IObject *) self;
#ifdef USE_DEBUG
    assert(pthread_equal(pthread_self(), this->mOwner));
#endif
    int ok;
    ok = pthread_cond_destroy(&this->mCond);
    assert(0 == ok);
    // equivalent to object_unlock_exclusive, but without the rigmarole
    ok = pthread_mutex_unlock(&this->mMutex);
    assert(0 == ok);
    ok = pthread_mutex_destroy(&this->mMutex);
    assert(0 == ok);
    // redundant: this->mState = SL_OBJECT_STATE_UNREALIZED;
}


/** \brief Publish a new object after it is fully initialized.
 *  Publishing will expose the object to sync thread and debugger,
 *  and make it safe to return the SLObjectItf to the application.
 */

void IObject_Publish(IObject *this)
{
    IEngine *thisEngine = this->mEngine;
    interface_lock_exclusive(thisEngine);
    // construct earlier reserved a pending slot, but did not choose the actual slot number
    unsigned availMask = ~thisEngine->mInstanceMask;
    assert(availMask);
    unsigned i = ctz(availMask);
    assert(MAX_INSTANCE > i);
    assert(NULL == thisEngine->mInstances[i]);
    thisEngine->mInstances[i] = this;
    thisEngine->mInstanceMask |= 1 << i;
    // avoid zero as a valid instance ID
    this->mInstanceID = i + 1;
    interface_unlock_exclusive(thisEngine);
}
