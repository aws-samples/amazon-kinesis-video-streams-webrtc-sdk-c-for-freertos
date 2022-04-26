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
#ifndef __KINESIS_VIDEO_WEBRTC_APP_COMMON_INCLUDE__
#define __KINESIS_VIDEO_WEBRTC_APP_COMMON_INCLUDE__

#ifdef __cplusplus
extern "C" {
#endif

#include <kvs/webrtc_client.h>
#include "hash_table.h"
#include "AppConfig.h"
#include "AppError.h"
#include "AppCredential.h"
#include "AppMediaSrc.h"
#include "AppSignaling.h"
#include "AppMessageQueue.h"
#include "timer_queue.h"
#include "hash_table.h"

typedef struct __StreamingSession StreamingSession;
typedef struct __StreamingSession* PStreamingSession;

typedef struct {
    UINT64 prevNumberOfPacketsSent;
    UINT64 prevNumberOfPacketsReceived;
    UINT64 prevNumberOfBytesSent;
    UINT64 prevNumberOfBytesReceived;
    UINT64 prevPacketsDiscardedOnSend;
    UINT64 prevTs;
} RtcMetricsHistory, *PRtcMetricsHistory;

typedef struct {
    volatile ATOMIC_BOOL terminateApp;           //!< terminate this app.
    volatile ATOMIC_BOOL sigInt;                 //!< the flag to indicate the system-level signal.
    volatile ATOMIC_BOOL mediaThreadStarted;     //!< the flag to indicate the status of the media thread.
    volatile ATOMIC_BOOL restartSignalingClient; //!< the flag to indicate we need to re-sync the singal server.
    volatile ATOMIC_BOOL peerConnectionConnected;
    volatile ATOMIC_BOOL abortMediaControl;

    AppCredential appCredential; //!< the context of app credential.
    AppSignaling appSignaling;   //!< the context of app signaling.
    PAppMediaSrc pAppMediaSrc;
    PVOID pMediaContext;         //!< the context of media.

    TID mediaControlTid;
    startRoutine mediaSource;
    TIMER_QUEUE_HANDLE timerQueueHandle;
    UINT32 iceCandidatePairStatsTimerId; //!< the timer id.
    UINT32 pregenerateCertTimerId;

    PHashTable pRemoteRtcPeerConnections;
    PConnectionMsgQ pRemotePeerPendingSignalingMessages; //!< stores signaling messages before receiving offer or answer.

    MUTEX appConfigurationObjLock;
    CVAR cvar;
    BOOL trickleIce; //!< This is ignored for master. Master can extract the info from offer. Viewer has to know if peer can trickle or
                     //!< not ahead of time.
    BOOL enableFileLogging;

    PStreamingSession streamingSessionList[APP_MAX_CONCURRENT_STREAMING_SESSION];
    UINT32 streamingSessionCount;
    MUTEX streamingSessionListReadLock; //!< the lock of streaming session.
    UINT32 iceUriCount;                 //!< the number of ice server including stun and turn.

} AppConfiguration, *PAppConfiguration;

struct __StreamingSession {
    volatile ATOMIC_BOOL terminateFlag; //!< the flag indicates the termination of this streaming session.
    volatile ATOMIC_BOOL candidateGatheringDone;
    volatile ATOMIC_BOOL peerIdReceived;
    volatile SIZE_T frameIndex;
    PRtcPeerConnection pPeerConnection;
    PRtcRtpTransceiver pVideoRtcRtpTransceiver;
    PRtcRtpTransceiver pAudioRtcRtpTransceiver;
    RtcSessionDescriptionInit answerSessionDescriptionInit;
    PAppConfiguration pAppConfiguration; //!< the context of the app

    CHAR peerId[MAX_SIGNALING_CLIENT_ID_LEN + 1]; //!< https://docs.aws.amazon.com/kinesisvideostreams-webrtc-dg/latest/devguide/kvswebrtc-websocket-apis3.html

    UINT64 offerReceiveTime;
    UINT64 startUpLatency;
    BOOL firstFrame;
    BOOL firstKeyFrame;                  //!< the first key frame of this session is sent or not.
    RtcMetricsHistory rtcMetricsHistory; //!< the metrics of the previous packet.
    BOOL remoteCanTrickleIce;
    PRtcDataChannel pRtcDataChannel;
};
/**
 * @brief   The initialization of this app.
 *
 * @param[in] trickleIce Enable the trickle ICE.
 * @param[in] useTurn Use the turn servers.
 * @param[in, out] ppAppConfiguration the context of this app.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS initApp(BOOL trickleIce, BOOL useTurn, PAppMediaSrc pAppMediaSrc, PAppConfiguration* ppAppConfiguration);
/**
 * @brief start runnning the app.
 *
 * @param[in, out] ppAppConfiguration the context of the app.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS runApp(PAppConfiguration pAppConfiguration);
/**
 * @brief free the context of the app.
 *
 * @param[in, out] ppAppConfiguration the context of the app.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS freeApp(PAppConfiguration* ppAppConfiguration);
/**
 * @brief polling the status of every streaming session, and checking the status of the app.
 *
 * @param[in] pAppConfiguration the context of the app.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS pollApp(PAppConfiguration pAppConfiguration);

STATUS quitApp(VOID);
#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_WEBRTC_APP_COMMON_INCLUDE__ */
