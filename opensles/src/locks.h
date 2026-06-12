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

/** \file locks.h Mutual exclusion and condition variables */

#ifdef USE_DEBUG
extern void object_lock_exclusive_(IObject *this, const char *file, int line);
extern void object_unlock_exclusive_(IObject *this, const char *file, int line);
extern void object_unlock_exclusive_attributes_(IObject *this, unsigned attr,
    const char *file, int line);
extern void object_cond_wait_(IObject *this, const char *file, int line);
#else
extern void object_lock_exclusive(IObject *this);
extern void object_unlock_exclusive(IObject *this);
extern void object_unlock_exclusive_attributes(IObject *this, unsigned attr);
extern void object_cond_wait(IObject *this);
#endif
extern void object_cond_signal(IObject *this);
extern void object_cond_broadcast(IObject *this);

#ifdef USE_DEBUG
#define object_lock_exclusive(this) object_lock_exclusive_((this), __FILE__, __LINE__)
#define object_unlock_exclusive(this) object_unlock_exclusive_((this), __FILE__, __LINE__)
#define object_unlock_exclusive_attributes(this, attr) \
    object_unlock_exclusive_attributes_((this), (attr), __FILE__, __LINE__)
#define object_cond_wait(this) object_cond_wait_((this), __FILE__, __LINE__)
#endif

// Currently shared locks are implemented as exclusive, but don't count on it

#define object_lock_shared(this)   object_lock_exclusive(this)
#define object_unlock_shared(this) object_unlock_exclusive(this)

// Currently interface locks are actually on whole object, but don't count on it.
// These operations are undefined on IObject, as it lacks an mThis.
// If you have an IObject, then use the object_ functions instead.

#define interface_lock_exclusive(this)   object_lock_exclusive(InterfaceToIObject(this))
#define interface_unlock_exclusive(this) object_unlock_exclusive(InterfaceToIObject(this))
#define interface_unlock_exclusive_attributes(this, attr) \
    object_unlock_exclusive_attributes(InterfaceToIObject(this), (attr))
#define interface_lock_shared(this)      object_lock_shared(InterfaceToIObject(this))
#define interface_unlock_shared(this)    object_unlock_shared(InterfaceToIObject(this))
#define interface_cond_wait(this)        object_cond_wait(InterfaceToIObject(this))
#define interface_cond_signal(this)      object_cond_signal(InterfaceToIObject(this))
#define interface_cond_broadcast(this)   object_cond_broadcast(InterfaceToIObject(this))

// Peek and poke are an optimization for small atomic fields that don't "matter"

#define object_lock_peek(this)      /* object_lock_shared(this) */
#define object_unlock_peek(this)    /* object_unlock_shared(this) */
#define interface_lock_poke(this)   /* interface_lock_exclusive(this) */
#define interface_unlock_poke(this) /* interface_unlock_exclusive(this) */
#define interface_lock_peek(this)   /* interface_lock_shared(this) */
#define interface_unlock_peek(this) /* interface_unlock_shared(this) */
