/*
 * Copyright 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */
#define LOG_CLASS "AppFileSrc"
#include "AppMediaSrc_ESP32_FileSrc.h"
#include "AppCommon.h"
#include "fileio.h"

#define NUMBER_OF_H264_FRAME_FILES               1500
#define NUMBER_OF_OPUS_FRAME_FILES               618
#define DEFAULT_FPS_VALUE                        25

#define FILESRC_AUDIO_FRAME_DURATION (20 * HUNDREDS_OF_NANOS_IN_A_MILLISECOND)
#define FILESRC_VIDEO_FRAME_DURATION (HUNDREDS_OF_NANOS_IN_A_SECOND / DEFAULT_FPS_VALUE)

typedef struct {
    RTC_CODEC codec;
    PBYTE pFrameBuffer;
    UINT32 frameBufferSize;
} CodecStreamConf, *PCodecStreamConf;

typedef struct {
    STATUS codecStatus;
    CodecStreamConf videoStream;
    CodecStreamConf audioStream;
} CodecConfiguration, *PCodecConfiguration;

typedef struct {
    MUTEX codecConfLock;
    CodecConfiguration codecConfiguration;  //!< the configuration of gstreamer.
    // the codec.
    volatile ATOMIC_BOOL shutdownFileSrc;
    volatile ATOMIC_BOOL codecConfigLatched;
    // for meida output.
    PVOID mediaSinkHookUserdata;
    MediaSinkHook mediaSinkHook;
    PVOID mediaEosHookUserdata;
    MediaEosHook mediaEosHook;

} FileSrcContext, *PFileSrcContext;

STATUS readFrameFromDisk(PBYTE pFrame, PUINT32 pSize, PCHAR frameFilePath)
{
    STATUS retStatus = STATUS_SUCCESS;
    UINT64 size = 0;

    if (pSize == NULL) {
        printf("[KVS Master] readFrameFromDisk(): operation returned status code: 0x%08x \n", STATUS_MEDIA_NULL_ARG);
        goto CleanUp;
    }

    size = *pSize;

    // Get the size and read into frame
    retStatus = fileio_read(frameFilePath, TRUE, pFrame, &size);
    if (retStatus != STATUS_SUCCESS) {
        printf("[KVS Master] readFile(): operation returned status code: 0x%08x \n", retStatus);
        goto CleanUp;
    }

CleanUp:

    if (pSize != NULL) {
        *pSize = (UINT32) size;
    }

    return retStatus;
}

PVOID sendVideoPackets(PVOID args)
{
    STATUS retStatus = STATUS_SUCCESS;
    PFileSrcContext pFileSrcContext = (PFileSrcContext) args;
    PCodecStreamConf pCodecStreamConf = NULL;
    Frame frame;
    UINT32 fileIndex = 0, frameSize;
    CHAR filePath[MAX_PATH_LEN + 1];
    STATUS status;
    UINT64 startTime, lastFrameTime, elapsed;

    CHK(pFileSrcContext != NULL, STATUS_MEDIA_NULL_ARG);

    pCodecStreamConf = &pFileSrcContext->codecConfiguration.videoStream;
    pCodecStreamConf->pFrameBuffer = (PBYTE) MEMALLOC(7*1024);
    pCodecStreamConf->frameBufferSize = 7*1024;
    frame.presentationTs = 0;
    startTime = GETTIME();
    lastFrameTime = startTime;

    while (!ATOMIC_LOAD_BOOL(&pFileSrcContext->shutdownFileSrc)) {
        fileIndex = fileIndex % NUMBER_OF_H264_FRAME_FILES + 1;
        snprintf(filePath, MAX_PATH_LEN, "/sdcard/h264SampleFrames/frame-%04d.h264", fileIndex);

        CHK(readFrameFromDisk(NULL, &frameSize, filePath) == STATUS_SUCCESS, STATUS_MEDIA_VIDEO_SINK);

        // Re-alloc if needed
        if (frameSize > pCodecStreamConf->frameBufferSize) {
            CHK((pCodecStreamConf->pFrameBuffer = (UINT8*) MEMREALLOC(pCodecStreamConf->pFrameBuffer, frameSize)) != NULL, STATUS_MEDIA_NOT_ENOUGH_MEMORY);
            pCodecStreamConf->frameBufferSize = frameSize;
        }

        // #TBD
        frame.flags = FRAME_FLAG_KEY_FRAME;
        frame.frameData = pCodecStreamConf->pFrameBuffer;
        frame.size = frameSize;

        CHK(readFrameFromDisk(frame.frameData, &frameSize, filePath) == STATUS_SUCCESS, STATUS_MEDIA_VIDEO_SINK);

        frame.presentationTs += FILESRC_VIDEO_FRAME_DURATION;
        frame.trackId = DEFAULT_VIDEO_TRACK_ID;
        frame.duration = 0;
        frame.version = FRAME_CURRENT_VERSION;
        frame.decodingTs = frame.presentationTs;
        if (pFileSrcContext->mediaSinkHook != NULL) {
            retStatus = pFileSrcContext->mediaSinkHook(pFileSrcContext->mediaSinkHookUserdata, &frame);
        }

        // Adjust sleep in the case the sleep itself and writeFrame take longer than expected. Since sleep makes sure that the thread
        // will be paused at least until the given amount, we can assume that there's no too early frame scenario.
        // Also, it's very unlikely to have a delay greater than FILESRC_VIDEO_FRAME_DURATION, so the logic assumes that this is always
        // true for simplicity.
        elapsed = lastFrameTime - startTime;
        THREAD_SLEEP(FILESRC_VIDEO_FRAME_DURATION - elapsed % FILESRC_VIDEO_FRAME_DURATION);
        lastFrameTime = GETTIME();
    }

CleanUp:
    if(pCodecStreamConf != NULL){
        SAFE_MEMFREE(pCodecStreamConf->pFrameBuffer);
    }
    CHK_LOG_ERR(retStatus);
    /* free resources */
    DLOGD("terminating media source");
    if (pFileSrcContext->mediaEosHook != NULL) {
        retStatus = pFileSrcContext->mediaEosHook(pFileSrcContext->mediaEosHookUserdata);
    }
    return (PVOID) (ULONG_PTR) retStatus;
}

PVOID sendAudioPackets(PVOID args)
{
    STATUS retStatus = STATUS_SUCCESS;
    PFileSrcContext pFileSrcContext = (PFileSrcContext) args;
    PCodecStreamConf pCodecStreamConf = NULL;
    Frame frame;
    UINT32 fileIndex = 0, frameSize;
    CHAR filePath[MAX_PATH_LEN + 1];
    STATUS status;

    CHK(pFileSrcContext != NULL, STATUS_MEDIA_NULL_ARG);

    pCodecStreamConf = &pFileSrcContext->codecConfiguration.audioStream;
    pCodecStreamConf->pFrameBuffer = NULL;
    pCodecStreamConf->frameBufferSize = 0;
    frame.presentationTs = 0;

    while (!ATOMIC_LOAD_BOOL(&pFileSrcContext->shutdownFileSrc)) {
        fileIndex = fileIndex % NUMBER_OF_OPUS_FRAME_FILES + 1;
        snprintf(filePath, MAX_PATH_LEN, "/sdcard/opusSampleFrames/sample-%03d.opus", fileIndex);
        
        CHK(readFrameFromDisk(NULL, &frameSize, filePath) == STATUS_SUCCESS, STATUS_MEDIA_AUDIO_SINK);
        // Re-alloc if needed
        if (frameSize > pCodecStreamConf->frameBufferSize) {
            CHK((pCodecStreamConf->pFrameBuffer = (UINT8*) MEMREALLOC(pCodecStreamConf->pFrameBuffer, frameSize)) != NULL, STATUS_MEDIA_NOT_ENOUGH_MEMORY);
            pCodecStreamConf->frameBufferSize = frameSize;
        }
        frame.flags = FRAME_FLAG_KEY_FRAME;
        frame.frameData = pCodecStreamConf->pFrameBuffer;
        frame.size = frameSize;

        CHK(readFrameFromDisk(frame.frameData, &frameSize, filePath) == STATUS_SUCCESS, STATUS_MEDIA_AUDIO_SINK);

        frame.presentationTs += FILESRC_AUDIO_FRAME_DURATION;
        frame.trackId = DEFAULT_AUDIO_TRACK_ID;
        frame.duration = 0;
        frame.version = FRAME_CURRENT_VERSION;
        frame.decodingTs = frame.presentationTs;
        if (pFileSrcContext->mediaSinkHook != NULL) {
            retStatus = pFileSrcContext->mediaSinkHook(pFileSrcContext->mediaSinkHookUserdata, &frame);
        }

        THREAD_SLEEP(FILESRC_AUDIO_FRAME_DURATION);
    }

CleanUp:

    if(pCodecStreamConf != NULL){
        SAFE_MEMFREE(pCodecStreamConf->pFrameBuffer);
    }
    CHK_LOG_ERR(retStatus);
    /* free resources */
    DLOGD("terminating media source");
    if (pFileSrcContext->mediaEosHook != NULL) {
        retStatus = pFileSrcContext->mediaEosHook(pFileSrcContext->mediaEosHookUserdata);
    }
    return (PVOID) (ULONG_PTR) retStatus;
}

STATUS app_media_source_detroy(PMediaContext* ppMediaContext)
{
    STATUS retStatus = STATUS_SUCCESS;
    PFileSrcContext pFileSrcContext;

    CHK(ppMediaContext != NULL, STATUS_MEDIA_NULL_ARG);
    pFileSrcContext = (PFileSrcContext) *ppMediaContext;
    CHK(pFileSrcContext != NULL, STATUS_MEDIA_NULL_ARG);
    if (IS_VALID_MUTEX_VALUE(pFileSrcContext->codecConfLock)) {
        MUTEX_FREE(pFileSrcContext->codecConfLock);
    }

    MEMFREE(pFileSrcContext);
    *ppMediaContext = pFileSrcContext = NULL;
CleanUp:
    return retStatus;
}

STATUS app_media_source_init(PMediaContext* ppMediaContext)
{
    STATUS retStatus = STATUS_SUCCESS;
    PFileSrcContext pFileSrcContext = NULL;
    PCodecConfiguration pGstConfiguration;
    PCodecStreamConf pVideoStream;
    PCodecStreamConf pAudioStream;

    CHK(ppMediaContext != NULL, STATUS_MEDIA_NULL_ARG);
    *ppMediaContext = NULL;
    CHK(NULL != (pFileSrcContext = (PFileSrcContext) MEMCALLOC(1, SIZEOF(FileSrcContext))), STATUS_MEDIA_NOT_ENOUGH_MEMORY);
    ATOMIC_STORE_BOOL(&pFileSrcContext->shutdownFileSrc, FALSE);
    ATOMIC_STORE_BOOL(&pFileSrcContext->codecConfigLatched, TRUE);

    pGstConfiguration = &pFileSrcContext->codecConfiguration;
    pGstConfiguration->codecStatus = STATUS_SUCCESS;
    pVideoStream = &pGstConfiguration->videoStream;
    pAudioStream = &pGstConfiguration->audioStream;
    pVideoStream->codec = RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_MODE;
    pAudioStream->codec = RTC_CODEC_OPUS;

    pFileSrcContext->codecConfLock = MUTEX_CREATE(TRUE);
    CHK(IS_VALID_MUTEX_VALUE(pFileSrcContext->codecConfLock), STATUS_MEDIA_INVALID_MUTEX);

    // get the sdp information of rtsp server.
    //CHK_STATUS((discoverMediaSource(pFileSrcContext)));
    *ppMediaContext = pFileSrcContext;

CleanUp:

    if (STATUS_FAILED(retStatus)) {
        if (pFileSrcContext != NULL) {
            app_media_source_detroy(pFileSrcContext);
        }
    }

    return retStatus;
}

STATUS app_media_source_isReady(PMediaContext pMediaContext)
{
    STATUS retStatus = STATUS_SUCCESS;
    PFileSrcContext pFileSrcContext = (PFileSrcContext) pMediaContext;
    CHK(pFileSrcContext != NULL, STATUS_MEDIA_NULL_ARG);
    //if (!ATOMIC_LOAD_BOOL(&pFileSrcContext->codecConfigLatched)) {
    //    discoverMediaSource(pFileSrcContext);
    //}

    if (ATOMIC_LOAD_BOOL(&pFileSrcContext->codecConfigLatched)) {
        retStatus = STATUS_SUCCESS;
    } else {
        retStatus = STATUS_MEDIA_NOT_READY;
    }

CleanUp:

    return retStatus;
}

STATUS app_media_source_queryVideoCap(PMediaContext pMediaContext, RTC_CODEC* pCodec)
{
    STATUS retStatus = STATUS_SUCCESS;
    PFileSrcContext pFileSrcContext = (PFileSrcContext) pMediaContext;
    PCodecStreamConf pVideoStream;
    CHK((pFileSrcContext != NULL) && (pCodec != NULL), STATUS_MEDIA_NULL_ARG);
    CHK(ATOMIC_LOAD_BOOL(&pFileSrcContext->codecConfigLatched), STATUS_MEDIA_NOT_READY);
    pVideoStream = &pFileSrcContext->codecConfiguration.videoStream;
    *pCodec = pVideoStream->codec;
CleanUp:
    return retStatus;
}

STATUS app_media_source_queryAudioCap(PMediaContext pMediaContext, RTC_CODEC* pCodec)
{
    STATUS retStatus = STATUS_SUCCESS;
    PFileSrcContext pFileSrcContext = (PFileSrcContext) pMediaContext;
    PCodecStreamConf pAudioStream;
    CHK((pFileSrcContext != NULL), STATUS_MEDIA_NULL_ARG);
    CHK(ATOMIC_LOAD_BOOL(&pFileSrcContext->codecConfigLatched), STATUS_MEDIA_NOT_READY);
    pAudioStream = &pFileSrcContext->codecConfiguration.audioStream;
    *pCodec = pAudioStream->codec;
CleanUp:
    return retStatus;
}

STATUS app_media_source_linkSinkHook(PMediaContext pMediaContext, MediaSinkHook mediaSinkHook, PVOID udata)
{
    STATUS retStatus = STATUS_SUCCESS;
    PFileSrcContext pFileSrcContext = (PFileSrcContext) pMediaContext;
    CHK(pFileSrcContext != NULL, STATUS_MEDIA_NULL_ARG);
    pFileSrcContext->mediaSinkHook = mediaSinkHook;
    pFileSrcContext->mediaSinkHookUserdata = udata;
CleanUp:
    return retStatus;
}

STATUS app_media_source_linkEosHook(PMediaContext pMediaContext, MediaEosHook mediaEosHook, PVOID udata)
{
    STATUS retStatus = STATUS_SUCCESS;
    PFileSrcContext pFileSrcContext = (PFileSrcContext) pMediaContext;
    CHK(pFileSrcContext != NULL, STATUS_MEDIA_NULL_ARG);
    pFileSrcContext->mediaEosHook = mediaEosHook;
    pFileSrcContext->mediaEosHookUserdata = udata;
CleanUp:
    return retStatus;
}

PVOID app_media_source_run(PVOID args)
{
    STATUS retStatus = STATUS_SUCCESS;
    PFileSrcContext pFileSrcContext = (PFileSrcContext) args;
    TID videoSenderTid = INVALID_TID_VALUE, audioSenderTid = INVALID_TID_VALUE;

    CHK(pFileSrcContext != NULL, STATUS_MEDIA_NULL_ARG);

    ATOMIC_STORE_BOOL(&pFileSrcContext->shutdownFileSrc, FALSE);
    DLOGI("media source is starting");

    THREAD_CREATE_EX(&videoSenderTid, APP_MEDIA_VIDEO_SENDER_THREAD_NAME, APP_MEDIA_VIDEO_SENDER_THREAD_SIZE, TRUE, sendVideoPackets, (PVOID) pFileSrcContext);
#ifdef ENABLE_AUDIO
    THREAD_CREATE_EX(&audioSenderTid, APP_MEDIA_AUDIO_SENDER_THREAD_NAME, APP_MEDIA_AUDIO_SENDER_THREAD_SIZE, TRUE, sendAudioPackets, (PVOID) pFileSrcContext);
#endif

    if (videoSenderTid != INVALID_TID_VALUE) {
        //#TBD, the thread_join does not work.
        THREAD_JOIN(videoSenderTid, NULL);
    }

    if (audioSenderTid != INVALID_TID_VALUE) {
        THREAD_JOIN(audioSenderTid, NULL);
    }
CleanUp:
    return (PVOID)(ULONG_PTR) retStatus;
}

STATUS app_media_source_shutdown(PMediaContext pMediaContext)
{
    STATUS retStatus = STATUS_SUCCESS;
    PFileSrcContext pFileSrcContext = (PFileSrcContext) pMediaContext;
    CHK(pFileSrcContext != NULL, STATUS_MEDIA_NULL_ARG);
    ATOMIC_STORE_BOOL(&pFileSrcContext->shutdownFileSrc, TRUE);
    DLOGD("shutdown media source");
CleanUp:
    return retStatus;
}

AppMediaSrc gAppMediaSrc = {
    .app_media_source_init = app_media_source_init,
    .app_media_source_isReady = app_media_source_isReady,
    .app_media_source_queryVideoCap = app_media_source_queryVideoCap,
    .app_media_source_queryAudioCap = app_media_source_queryAudioCap,
    .app_media_source_linkSinkHook = app_media_source_linkSinkHook,
    .app_media_source_linkEosHook = app_media_source_linkEosHook,
    .app_media_source_run = app_media_source_run,
    .app_media_source_shutdown = app_media_source_shutdown,
    .app_media_source_isShutdown = NULL,
    .app_media_source_detroy = app_media_source_detroy
};

#ifdef ENABLE_AUDIO_SENDRECV
PVOID app_media_sink_onFrame(PVOID pArgs)
{
    STATUS retStatus = STATUS_SUCCESS;

    return (PVOID)(ULONG_PTR) retStatus;
}
#endif
