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

#ifndef __MPH_H
#define __MPH_H

// Minimal perfect hash for each interface ID

#define MPH_NONE                      (-1)
#define MPH_MIN                         0

#define MPH_3DCOMMIT                    0
#define MPH_3DDOPPLER                   1
#define MPH_3DGROUPING                  2
#define MPH_3DLOCATION                  3
#define MPH_3DMACROSCOPIC               4
#define MPH_3DSOURCE                    5
#define MPH_AUDIODECODERCAPABILITIES    6
#define MPH_AUDIOENCODER                7
#define MPH_AUDIOENCODERCAPABILITIES    8
#define MPH_AUDIOIODEVICECAPABILITIES   9
#define MPH_BASSBOOST                  10
#define MPH_BUFFERQUEUE                11
#define MPH_DEVICEVOLUME               12
#define MPH_DYNAMICINTERFACEMANAGEMENT 13
#define MPH_DYNAMICSOURCE              14
#define MPH_EFFECTSEND                 15
#define MPH_ENGINE                     16
#define MPH_ENGINECAPABILITIES         17
#define MPH_ENVIRONMENTALREVERB        18
#define MPH_EQUALIZER                  19
#define MPH_LED                        20
#define MPH_METADATAEXTRACTION         21
#define MPH_METADATATRAVERSAL          22
#define MPH_MIDIMESSAGE                23
#define MPH_MIDIMUTESOLO               24
#define MPH_MIDITEMPO                  25
#define MPH_MIDITIME                   26
#define MPH_MUTESOLO                   27
#define MPH_NULL                       28
#define MPH_OBJECT                     29
#define MPH_OUTPUTMIX                  30
#define MPH_PITCH                      31
#define MPH_PLAY                       32
#define MPH_PLAYBACKRATE               33
#define MPH_PREFETCHSTATUS             34
#define MPH_PRESETREVERB               35
#define MPH_RATEPITCH                  36
#define MPH_RECORD                     37
#define MPH_SEEK                       38
#define MPH_THREADSYNC                 39
#define MPH_VIBRA                      40
#define MPH_VIRTUALIZER                41
#define MPH_VISUALIZATION              42
#define MPH_VOLUME                     43
// end Khronos standard interfaces

// The lack of ifdef on the remaining is intentional

// start non-standard and platform-independent interface IDs
#define MPH_OUTPUTMIXEXT               44
// end non-standard and platform-independent interface IDs

// start non-standard and platform-specific interface IDs
#define MPH_ANDROIDEFFECT              45
#define MPH_ANDROIDEFFECTCAPABILITIES  46
#define MPH_ANDROIDEFFECTSEND          47
#define MPH_ANDROIDCONFIGURATION       48
#define MPH_ANDROIDSIMPLEBUFFERQUEUE   49
// end non-standard and platform-specific interface IDs

// total number
#define MPH_MAX                        50

#endif // !defined(__MPH_H)
