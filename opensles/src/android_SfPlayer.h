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
#include <binder/ProcessState.h>
#include <sys/stat.h>
#include <media/AudioTrack.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AHandler.h>
#include <media/stagefright/foundation/ALooper.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/FileSource.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaExtractor.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/OMXClient.h>
#include <media/stagefright/OMXCodec.h>

#include "NuCachedSource2.h"
#include "NuHTTPDataSource.h"
#include "ThrottledSource.h"

#define DURATION_CACHED_HIGH_US  30000000 // 30s
#define DURATION_CACHED_MED_US   10000000 // 10s
#define DURATION_CACHED_LOW_US    2000000 //  2s

/*
 * by how much time data is written to the AudioTrack ahead of the scheduled render time
 */
#define RENDER_SAFETY_DELAY_US 5000 // 5ms

#define SIZE_CACHED_HIGH_BYTES 1000000
#define SIZE_CACHED_MED_BYTES   700000
#define SIZE_CACHED_LOW_BYTES   400000

/*
 * events sent to mNotifyClient during prepare, prefetch, and playback
 */
#define EVENT_PREPARED                "prep"
#define EVENT_PREFETCHSTATUSCHANGE    "prsc"
#define EVENT_PREFETCHFILLLEVELUPDATE "pflu"
#define EVENT_ENDOFSTREAM             "eos"

#define SFPLAYER_SUCCESS 1
#define SFPLAYER_FD_FIND_FILE_SIZE ((int64_t)0xFFFFFFFFFFFFFFFFll)

#define NO_FILL_LEVEL_UPDATE -1
#define NO_STATUS_UPDATE kStatusUnknown


typedef struct AudioPlayback_Parameters_struct {
    int streamType;
    int sessionId;
    android::AudioTrack::callback_t trackcb;
    void* trackcbUser;
} AudioPlayback_Parameters;


namespace android {

    typedef void (*notif_client_t)(int event, const int data1, void* notifUser);

struct SfPlayer : public AHandler {
    SfPlayer(AudioPlayback_Parameters *app);

    enum CacheStatus {
        kStatusUnknown = -1,
        kStatusEmpty = 0,
        kStatusLow,
        kStatusIntermediate,
        kStatusEnough,
        kStatusHigh
    };

    enum {
        kEventPrepared                = 'prep',
        kEventPrefetchStatusChange    = 'prsc',
        kEventPrefetchFillLevelUpdate = 'pflu',
        kEventEndOfStream             = 'eos',
    };

    void armLooper();
    void useAudioTrack(AudioTrack* pTrack);
    void setNotifListener(const notif_client_t cbf, void* notifUser);

    void setDataSource(const char *uri);
    void setDataSource(const int fd, const int64_t offset, const int64_t length);

    void setCacheFillUpdateThreshold(int16_t thr) { mCacheFillNotifThreshold = thr; }

    void prepare();
    void play();
    void pause();
    void stop();
    void seek(int64_t timeMsec);
    void loop(bool loop);
    bool wantPrefetch();
    void startPrefetch_async();

    /**
     * returns the duration in microseconds, -1 if unknown
     */
    int64_t getDurationUsec() { return mDurationUsec; }
    int32_t getNumChannels()  { return mNumChannels; }
    int32_t getSampleRateHz() { return mSampleRateHz; }
    AudioTrack* getAudioTrack() { return mAudioTrack; }
    uint32_t getPositionMsec();

protected:
    virtual ~SfPlayer();
    virtual void onMessageReceived(const sp<AMessage> &msg);

private:

    enum {
        kDataLocatorNone = 'none',
        kDataLocatorUri  = 'uri',
        kDataLocatorFd   = 'fd',
    };

    enum {
        kWhatPrepare    = 'prep',
        kWhatDecode     = 'deco',
        kWhatRender     = 'rend',
        kWhatCheckCache = 'cach',
        kWhatNotif      = 'noti',
        kWhatPlay       = 'play',
        kWhatPause      = 'paus',
        kWhatSeek       = 'seek',
        kWhatLoop       = 'loop',
    };

    enum {
        kFlagPlaying   = 1,
        kFlagPreparing = 2,
        kFlagBuffering = 4,
        kFlagSeeking   = 8,
        kFlagLooping   = 16,
    };

    struct FdInfo {
        int fd;
        int64_t offset;
        int64_t length;
    };

    union DataLocator {
        char* uri;
        FdInfo fdi;
    };

    // mutex used for seek flag and seek time read/write
    Mutex       mSeekLock;

    AudioTrack *mAudioTrack;

    sp<ALooper> mRenderLooper;
    sp<DataSource> mDataSource;
    sp<MediaSource> mAudioSource;
    uint32_t mFlags;
    int64_t mBitrate;  // in bits/sec
    int32_t mNumChannels;
    int32_t mSampleRateHz;
    int64_t mTimeDelta;
    int64_t mDurationUsec;
    CacheStatus mCacheStatus;
    int64_t mSeekTimeMsec;
    int64_t mLastDecodedPositionUs;
    int16_t mCacheFill; // cache fill level in permille
    int16_t mLastNotifiedCacheFill; // last cache fill level communicated to the listener
    int16_t mCacheFillNotifThreshold; // threshold in cache fill level for cache fill to be reported
    AudioPlayback_Parameters mPlaybackParams;

    DataLocator mDataLocator;
    int         mDataLocatorType;

    notif_client_t mNotifyClient;
    void*          mNotifyUser;

    // mutex used for protecting the decode buffer
    Mutex       mDecodeBufferLock;
    // buffer passed from decoder to renderer
    MediaBuffer *mDecodeBuffer;

    // message handlers
    void onPrepare(const sp<AMessage> &msg);
    void onDecode();
    void onRender(const sp<AMessage> &msg);
    void onCheckCache(const sp<AMessage> &msg);
    void onNotify(const sp<AMessage> &msg);
    void onPlay();
    void onPause();
    void onSeek(const sp<AMessage> &msg);
    void onLoop(const sp<AMessage> &msg);

    CacheStatus getCacheRemaining(bool *eos);
    int64_t getPositionUsec();
    void reachedEndOfStream();
    void notifyStatus();
    void notifyCacheFill();
    void notifyPrepared(status_t prepareRes);
    void notify(const sp<AMessage> &msg, bool async);

    void resetDataLocator();

    DISALLOW_EVIL_CONSTRUCTORS(SfPlayer);
};

} // namespace android
