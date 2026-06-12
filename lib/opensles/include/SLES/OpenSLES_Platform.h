/*
 * Copyright (c) 2007-2009 The Khronos Group Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and /or associated documentation files (the "Materials "), to
 * deal in the Materials without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Materials, and to permit persons to whom the Materials are
 * furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Materials.
 *
 * THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE MATERIALS OR THE USE OR OTHER DEALINGS IN THE
 * MATERIALS.
 *
 * OpenSLES_Platform.h - OpenSL ES version 1.0
 *
 */

/****************************************************************************/
/* NOTE: This file contains definitions for the base types and the          */
/* SLAPIENTRY macro. This file **WILL NEED TO BE EDITED** to provide        */
/* the correct definitions specific to the platform being used.             */
/****************************************************************************/

#ifndef _OPENSLES_PLATFORM_H_
#define _OPENSLES_PLATFORM_H_

#include <stdint.h>

// IMPORTANT: SLuint32/SLint32 must be exactly 32-bit to match the ABI of the
// Android NDK's libOpenSLES that libTTapp.so was built against. The stock
// Khronos header uses `unsigned long`, which is 32-bit on 32-bit ARM (so the
// Vita port worked) but 64-bit on aarch64 -- that mismatch doubles the size of
// every SLEngineOption / SLDataFormat_PCM / SLDataLocator_* struct and breaks
// all data passed between the game and this library (Switch crash log: the
// game's THREADSAFE engine option read back as 0x1_00000001). Pin to fixed
// widths so the struct layouts match the game's.
typedef uint8_t                     sl_uint8_t;
typedef int8_t                      sl_int8_t;
typedef uint16_t                    sl_uint16_t;
typedef int16_t                     sl_int16_t;
typedef uint32_t                    sl_uint32_t;
typedef int32_t                     sl_int32_t;
typedef int64_t                     sl_int64_t;

#ifndef SLAPIENTRY
#ifdef __GNUC__
#define SLAPIENTRY                 /* override per-platform */
#else
#define SLAPIENTRY __declspec(dllimport)
#endif
#endif

#endif /* _OPENSLES_PLATFORM_H_ */
