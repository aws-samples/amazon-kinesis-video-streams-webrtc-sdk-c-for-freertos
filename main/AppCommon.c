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
#define LOG_CLASS "AppCommon"
#include "AppCommon.h"
#include "AppCredential.h"
#include "AppDataChannel.h"
#include "AppMetrics.h"
#include "AppMediaSrc.h"
#include "AppMediaSink.h"
#include "AppSignaling.h"
#include "AppWebRTC.h"
#include "hash_table.h"
#include "timer_queue.h"
#include "logger.h"
#include "crc32.h"

static PAppConfiguration gAppConfiguration = NULL; //!< for the system-level signal handler
static BOOL gInitialized = FALSE; //!< for the system-level signal handler

STATUS app_common_createStreamingSession(PAppConfiguration pAppConfiguration, PCHAR peerId, PStreamingSession* ppStreamingSession);
STATUS app_common_freeStreamingSession(PStreamingSession* ppStreamingSession);
PVOID app_common_runMediaSource(PVOID userData);

static STATUS app_common_onMediaSinkHook(PVOID udata, PFrame pFrame)
{
    STATUS retStatus = STATUS_SUCCESS;
    PAppConfiguration pAppConfiguration = (PAppConfiguration) udata;
    PStreamingSession pStreamingSession = NULL;
    PRtcRtpTransceiver pRtcRtpTransceiver = NULL;
    UINT32 i;

    CHK(pAppConfiguration != NULL, STATUS_APP_COMMON_NULL_ARG);

    MUTEX_LOCK(pAppConfiguration->streamingSessionListReadLock);
    for (i = 0; i < pAppConfiguration->streamingSessionCount; ++i) {
        pStreamingSession = pAppConfiguration->streamingSessionList[i];
        if (pFrame->trackId == DEFAULT_VIDEO_TRACK_ID) {
            if (pStreamingSession->firstKeyFrame == FALSE && pFrame->flags != FRAME_FLAG_KEY_FRAME) {
                continue;
            } else {
                pStreamingSession->firstKeyFrame = TRUE;
            }
        }

        pFrame->index = (UINT32) ATOMIC_INCREMENT(&pStreamingSession->frameIndex);

        if (pFrame->trackId == DEFAULT_AUDIO_TRACK_ID) {
            pRtcRtpTransceiver = pStreamingSession->pAudioRtcRtpTransceiver;
        } else {
            pRtcRtpTransceiver = pStreamingSession->pVideoRtcRtpTransceiver;
        }
        retStatus = rtp_writeFrame(pRtcRtpTransceiver, pFrame);
        if (retStatus != STATUS_SUCCESS && retStatus != STATUS_SRTP_NOT_READY_YET) {
            // STATUS_SRTP_NOT_READY_YET
            // srtp session may not ready when we write the frames.
            // #TBD, make it more precisely.
            DLOGW("rtp_writeFrame() failed with 0x%08x", retStatus);
            retStatus = STATUS_SUCCESS;
        }
    }
    MUTEX_UNLOCK(pAppConfiguration->streamingSessionListReadLock);

CleanUp:

    if (pAppConfiguration != NULL && ATOMIC_LOAD_BOOL(&pAppConfiguration->terminateApp)) {
        retStatus = STATUS_APP_COMMON_SHUTDOWN_MEDIA;
    }
    return retStatus;
}

static STATUS app_common_onMediaEosHook(PVOID udata)
{
    STATUS retStatus = STATUS_SUCCESS;
    UNUSED_PARAM(udata);
    DLOGD("Media EOS Hook");
    return retStatus;
}

static VOID app_common_onConnectionStateChange(UINT64 userData, RTC_PEER_CONNECTION_STATE newState)
{
    STATUS retStatus = STATUS_SUCCESS;
    PStreamingSession pStreamingSession = (PStreamingSession) userData;
    PAppConfiguration pAppConfiguration;

    CHK((pStreamingSession != NULL) && (pStreamingSession->pAppConfiguration != NULL), STATUS_INTERNAL_ERROR);

    pAppConfiguration = pStreamingSession->pAppConfiguration;
    DLOGI("New connection state %u", newState);

    switch (newState) {
        case RTC_PEER_CONNECTION_STATE_CONNECTED:
            ATOMIC_STORE_BOOL(&pAppConfiguration->peerConnectionConnected, TRUE);
            CVAR_BROADCAST(pAppConfiguration->cvar);
            if (STATUS_FAILED(retStatus = logSelectedIceCandidatesInformation(pStreamingSession->pPeerConnection))) {
                DLOGW("Failed to get information about selected Ice candidates: 0x%08x", retStatus);
            }
            break;
        case RTC_PEER_CONNECTION_STATE_FAILED:
            // explicit fallthrough
        case RTC_PEER_CONNECTION_STATE_CLOSED:
            // explicit fallthrough
        case RTC_PEER_CONNECTION_STATE_DISCONNECTED:
            ATOMIC_STORE_BOOL(&pStreamingSession->terminateFlag, TRUE);
            CVAR_BROADCAST(pAppConfiguration->cvar);
            // explicit fallthrough
        default:
            ATOMIC_STORE_BOOL(&pAppConfiguration->peerConnectionConnected, FALSE);
            CVAR_BROADCAST(pAppConfiguration->cvar);
            break;
    }

CleanUp:

    CHK_LOG_ERR((retStatus));
}

static STATUS app_common_onSignalingClientStateChanged(UINT64 userData, SIGNALING_CLIENT_STATE state)
{
    UNUSED_PARAM(userData);
    STATUS retStatus = STATUS_SUCCESS;
    PCHAR pStateStr;

    signaling_client_getStateString(state, &pStateStr);
    DLOGD("Signaling client state changed to %d - '%s'", state, pStateStr);

    // Return success to continue
    return retStatus;
}

static STATUS app_common_onSignalingClientError(UINT64 userData, STATUS status, PCHAR msg, UINT32 msgLen)
{
    PAppConfiguration pAppConfiguration = (PAppConfiguration) userData;

    DLOGW("Signaling client generated an error 0x%08x - '%.*s'", status, msgLen, msg);
    // We will force re-create the signaling client on the following errors
    if (status == STATUS_SIGNALING_ICE_CONFIG_REFRESH_FAILED || status == STATUS_SIGNALING_RECONNECT_FAILED) {
        ATOMIC_STORE_BOOL(&pAppConfiguration->restartSignalingClient, TRUE);
        CVAR_BROADCAST(pAppConfiguration->cvar);
    }

    return STATUS_SUCCESS;
}

static VOID app_common_onBandwidthEstimation(UINT64 userData, DOUBLE maxiumBitrate)
{
    UNUSED_PARAM(userData);
    DLOGV("received bitrate suggestion: %f", maxiumBitrate);
}

static VOID app_common_onSenderBandwidthEstimation(UINT64 userData, UINT32 txBytes, UINT32 rxBytes, UINT32 txPacketsCnt, UINT32 rxPacketsCnt,
                                               UINT64 duration)
{
    UNUSED_PARAM(userData);
    UNUSED_PARAM(duration);
    UNUSED_PARAM(rxBytes);
    UNUSED_PARAM(txBytes);
    UINT32 lostPacketsCnt = txPacketsCnt - rxPacketsCnt;
    UINT32 percentLost = lostPacketsCnt * 100 / txPacketsCnt;
    UINT32 bitrate = 1024;
    if (percentLost < 2) {
        // increase encoder bitrate by 2 percent
        bitrate *= 1.02f;
    } else if (percentLost > 5) {
        // decrease encoder bitrate by packet loss percent
        bitrate *= (1.0f - percentLost / 100.0f);
    }
    // otherwise keep bitrate the same

    DLOGS("received sender bitrate estimation: suggested bitrate %u sent: %u bytes %u packets received: %u bytes %u packets in %lu msec, ", bitrate,
          txBytes, txPacketsCnt, rxBytes, rxPacketsCnt, duration / 10000ULL);
}

static STATUS app_common_handleRemoteCandidate(PStreamingSession pStreamingSession, PSignalingMessage pSignalingMessage)
{
    STATUS retStatus = STATUS_SUCCESS;
    PRtcIceCandidateInit pIceCandidate = NULL;
    CHK(NULL != (pIceCandidate = (PRtcIceCandidateInit) MEMCALLOC(1, SIZEOF(RtcIceCandidateInit))), STATUS_APP_COMMON_NOT_ENOUGH_MEMORY);

    CHK(pStreamingSession != NULL, STATUS_APP_COMMON_NULL_ARG);
    CHK_STATUS((sdp_deserializeRtcIceCandidateInit(pSignalingMessage->payload, pSignalingMessage->payloadLen, pIceCandidate)));
    CHK_STATUS((pc_addIceCandidate(pStreamingSession->pPeerConnection, pIceCandidate->candidate)));

CleanUp:
    SAFE_MEMFREE(pIceCandidate);
    CHK_LOG_ERR((retStatus));
    return retStatus;
}

static STATUS app_common_respondWithAnswer(PStreamingSession pStreamingSession)
{
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingMessage pMessage = NULL;
    UINT32 buffLen = MAX_SIGNALING_MESSAGE_LEN;

    CHK(NULL != (pMessage = (PSignalingMessage) MEMCALLOC(1, SIZEOF(SignalingMessage))), STATUS_APP_COMMON_NOT_ENOUGH_MEMORY);
    CHK_STATUS((sdp_serializeInit(&pStreamingSession->answerSessionDescriptionInit, pMessage->payload, &buffLen)));

    pMessage->version = SIGNALING_MESSAGE_CURRENT_VERSION;
    pMessage->messageType = SIGNALING_MESSAGE_TYPE_ANSWER;
    STRNCPY(pMessage->peerClientId, pStreamingSession->peerId, MAX_SIGNALING_CLIENT_ID_LEN);
    pMessage->payloadLen = (UINT32) STRLEN(pMessage->payload);
    pMessage->correlationId[0] = '\0';
    
    CHK_STATUS((app_signaling_sendMsg(&pStreamingSession->pAppConfiguration->appSignaling, pMessage)));

CleanUp:
    SAFE_MEMFREE(pMessage);
    CHK_LOG_ERR((retStatus));
    return retStatus;
}

static STATUS app_common_handleOffer(PAppConfiguration pAppConfiguration, PStreamingSession pStreamingSession, PSignalingMessage pSignalingMessage)
{
    STATUS retStatus = STATUS_SUCCESS;
    PRtcSessionDescriptionInit pOfferSessionDescriptionInit = NULL;
    NullableBool canTrickle;
    BOOL mediaThreadStarted;

    CHK(NULL != (pOfferSessionDescriptionInit = (PRtcSessionDescriptionInit) MEMCALLOC(1, SIZEOF(RtcSessionDescriptionInit))), STATUS_APP_COMMON_NOT_ENOUGH_MEMORY);
    MEMSET(&pStreamingSession->answerSessionDescriptionInit, 0x00, SIZEOF(RtcSessionDescriptionInit));

    CHK_STATUS((sdp_deserializeInit(pSignalingMessage->payload, pSignalingMessage->payloadLen, pOfferSessionDescriptionInit)));
    CHK_STATUS((pc_setRemoteDescription(pStreamingSession->pPeerConnection, pOfferSessionDescriptionInit)));
    canTrickle = pc_canTrickleIceCandidates(pStreamingSession->pPeerConnection);
    // cannot be null after pc_setRemoteDescription
    CHECK(!NULLABLE_CHECK_EMPTY(canTrickle));
    pStreamingSession->remoteCanTrickleIce = canTrickle.value;
    CHK_STATUS((pc_setLocalDescription(pStreamingSession->pPeerConnection, &pStreamingSession->answerSessionDescriptionInit)));

    // If remote support trickle ice, send answer now. Otherwise answer will be sent once ice candidate gathering is complete.
    if (pStreamingSession->remoteCanTrickleIce) {
        CHK_STATUS((pc_createAnswer(pStreamingSession->pPeerConnection, &pStreamingSession->answerSessionDescriptionInit)));
        CHK_STATUS((app_common_respondWithAnswer(pStreamingSession)));
        DLOGD("time taken to send answer %" PRIu64 " ms", (GETTIME() - pStreamingSession->offerReceiveTime) / HUNDREDS_OF_NANOS_IN_A_MILLISECOND);
    }

    mediaThreadStarted = ATOMIC_EXCHANGE_BOOL(&pAppConfiguration->mediaThreadStarted, TRUE);
    if (!mediaThreadStarted) {
        THREAD_CREATE_EX(&pAppConfiguration->mediaControlTid, APP_MEDIA_CONTROL_THREAD_NAME, APP_MEDIA_CONTROL_THREAD_SIZE, TRUE, app_common_runMediaSource, (PVOID) pAppConfiguration);
    }else{
        DLOGD("The media source is started");
    }

    // The audio video receive routine should be per streaming session
#ifdef ENABLE_AUDIO_SENDRECV
	app_media_sink_onFrame(pStreamingSession);
#endif

CleanUp:
    SAFE_MEMFREE(pOfferSessionDescriptionInit);
    CHK_LOG_ERR((retStatus));

    return retStatus;
}

static VOID app_common_onIceCandidate(UINT64 userData, PCHAR candidateJson)
{
    STATUS retStatus = STATUS_SUCCESS;
    PStreamingSession pStreamingSession = (PStreamingSession) userData;
    PSignalingMessage pMessage = NULL;

    CHK(pStreamingSession != NULL, STATUS_APP_COMMON_NULL_ARG);
    CHK(NULL != (pMessage = (PSignalingMessage) MEMCALLOC(1, SIZEOF(SignalingMessage))), STATUS_APP_COMMON_NOT_ENOUGH_MEMORY);

    if (candidateJson == NULL) {
        DLOGD("ice candidate gathering finished");
        ATOMIC_STORE_BOOL(&pStreamingSession->candidateGatheringDone, TRUE);

        // if application is master and non-trickle ice, send answer now.
        if (app_signaling_getRole(&pStreamingSession->pAppConfiguration->appSignaling) == SIGNALING_CHANNEL_ROLE_TYPE_MASTER &&
            !pStreamingSession->remoteCanTrickleIce) {
            CHK_STATUS((pc_createAnswer(pStreamingSession->pPeerConnection, &pStreamingSession->answerSessionDescriptionInit)));
            CHK_STATUS((app_common_respondWithAnswer(pStreamingSession)));
            DLOGD("time taken to send answer %" PRIu64 " ms", (GETTIME() - pStreamingSession->offerReceiveTime) / HUNDREDS_OF_NANOS_IN_A_MILLISECOND);
        }

    } else if (pStreamingSession->remoteCanTrickleIce && ATOMIC_LOAD_BOOL(&pStreamingSession->peerIdReceived)) {
        pMessage->version = SIGNALING_MESSAGE_CURRENT_VERSION;
        pMessage->messageType = SIGNALING_MESSAGE_TYPE_ICE_CANDIDATE;
        STRNCPY(pMessage->peerClientId, pStreamingSession->peerId, MAX_SIGNALING_CLIENT_ID_LEN);
        pMessage->payloadLen = (UINT32) STRNLEN(candidateJson, MAX_SIGNALING_MESSAGE_LEN);
        STRNCPY(pMessage->payload, candidateJson, pMessage->payloadLen);
        pMessage->correlationId[0] = '\0';
        CHK_STATUS((app_signaling_sendMsg(&pStreamingSession->pAppConfiguration->appSignaling, pMessage)));
    }

CleanUp:
    SAFE_MEMFREE(pMessage);
    CHK_LOG_ERR((retStatus));
}

static STATUS app_common_onIceCandidatePairStats(UINT32 timerId, UINT64 currentTime, UINT64 userData)
{
    UNUSED_PARAM(timerId);
    UNUSED_PARAM(currentTime);
    STATUS retStatus = STATUS_SUCCESS;
    PAppConfiguration pAppConfiguration = (PAppConfiguration) userData;
    UINT32 i;
    UINT64 currentMeasureDuration = 0;
    DOUBLE averagePacketsDiscardedOnSend = 0.0;
    DOUBLE averageNumberOfPacketsSentPerSecond = 0.0;
    DOUBLE averageNumberOfPacketsReceivedPerSecond = 0.0;
    DOUBLE outgoingBitrate = 0.0;
    DOUBLE incomingBitrate = 0.0;
    PRtcStats pRtcMetrics = NULL;
    BOOL locked = FALSE;

    CHK_WARN(pAppConfiguration != NULL, STATUS_APP_COMMON_NULL_ARG, "getPeriodicStats(): Passed argument is NULL");
    CHK(NULL != (pRtcMetrics = (PRtcStats) MEMCALLOC(1, SIZEOF(RtcStats))), STATUS_APP_COMMON_NOT_ENOUGH_MEMORY);

    pRtcMetrics->requestedTypeOfStats = RTC_STATS_TYPE_CANDIDATE_PAIR;

    // We need to execute this under the object lock due to race conditions that it could pose
    CHK_WARN(MUTEX_WAITLOCK(pAppConfiguration->appConfigurationObjLock, 50*HUNDREDS_OF_NANOS_IN_A_MILLISECOND) == TRUE, STATUS_APP_COMMON_ACQUIRE_MUTEX, "app_common_onIceCandidatePairStats failed");
    locked = TRUE;

    for (i = 0; i < pAppConfiguration->streamingSessionCount; ++i) {
        if (STATUS_SUCCEEDED(
                metrics_get(pAppConfiguration->streamingSessionList[i]->pPeerConnection, NULL, pRtcMetrics))) {
            currentMeasureDuration = (pRtcMetrics->timestamp - pAppConfiguration->streamingSessionList[i]->rtcMetricsHistory.prevTs) /
                HUNDREDS_OF_NANOS_IN_A_SECOND;
            DLOGD("Current duration: %" PRIu64 " seconds", currentMeasureDuration);

            if (currentMeasureDuration > 0) {
                DLOGD("Selected local candidate ID: %s", pRtcMetrics->rtcStatsObject.iceCandidatePairStats.localCandidateId);
                DLOGD("Selected remote candidate ID: %s", pRtcMetrics->rtcStatsObject.iceCandidatePairStats.remoteCandidateId);
                // TODO: Display state as a string for readability
                DLOGD("Ice Candidate Pair state: %d", pRtcMetrics->rtcStatsObject.iceCandidatePairStats.state);
                DLOGD("Nomination state: %s",
                      pRtcMetrics->rtcStatsObject.iceCandidatePairStats.nominated ? "nominated" : "not nominated");
                averageNumberOfPacketsSentPerSecond =
                    (DOUBLE)(pRtcMetrics->rtcStatsObject.iceCandidatePairStats.packetsSent -
                             pAppConfiguration->streamingSessionList[i]->rtcMetricsHistory.prevNumberOfPacketsSent) /
                    (DOUBLE) currentMeasureDuration;
                DLOGD("Packet send rate: %lf pkts/sec", averageNumberOfPacketsSentPerSecond);

                averageNumberOfPacketsReceivedPerSecond =
                    (DOUBLE)(pRtcMetrics->rtcStatsObject.iceCandidatePairStats.packetsReceived -
                             pAppConfiguration->streamingSessionList[i]->rtcMetricsHistory.prevNumberOfPacketsReceived) /
                    (DOUBLE) currentMeasureDuration;
                DLOGD("Packet receive rate: %lf pkts/sec", averageNumberOfPacketsReceivedPerSecond);

                outgoingBitrate = (DOUBLE)((pRtcMetrics->rtcStatsObject.iceCandidatePairStats.bytesSent -
                                            pAppConfiguration->streamingSessionList[i]->rtcMetricsHistory.prevNumberOfBytesSent) *
                                           8.0) /
                    currentMeasureDuration;
                DLOGD("Outgoing bit rate: %lf bps", outgoingBitrate);

                incomingBitrate = (DOUBLE)((pRtcMetrics->rtcStatsObject.iceCandidatePairStats.bytesReceived -
                                            pAppConfiguration->streamingSessionList[i]->rtcMetricsHistory.prevNumberOfBytesReceived) *
                                           8.0) /
                    currentMeasureDuration;
                DLOGD("Incoming bit rate: %lf bps", incomingBitrate);

                averagePacketsDiscardedOnSend = (DOUBLE)(pRtcMetrics->rtcStatsObject.iceCandidatePairStats.packetsDiscardedOnSend -
                                                         pAppConfiguration->streamingSessionList[i]->rtcMetricsHistory.prevPacketsDiscardedOnSend) /
                    (DOUBLE) currentMeasureDuration;
                DLOGD("Packet discard rate: %lf pkts/sec", averagePacketsDiscardedOnSend);

                DLOGD("Current STUN request round trip time: %lf sec",
                      pRtcMetrics->rtcStatsObject.iceCandidatePairStats.currentRoundTripTime);
                DLOGD("Number of STUN responses received: %llu", pRtcMetrics->rtcStatsObject.iceCandidatePairStats.responsesReceived);

                pAppConfiguration->streamingSessionList[i]->rtcMetricsHistory.prevTs = pRtcMetrics->timestamp;
                pAppConfiguration->streamingSessionList[i]->rtcMetricsHistory.prevNumberOfPacketsSent =
                    pRtcMetrics->rtcStatsObject.iceCandidatePairStats.packetsSent;
                pAppConfiguration->streamingSessionList[i]->rtcMetricsHistory.prevNumberOfPacketsReceived =
                    pRtcMetrics->rtcStatsObject.iceCandidatePairStats.packetsReceived;
                pAppConfiguration->streamingSessionList[i]->rtcMetricsHistory.prevNumberOfBytesSent =
                    pRtcMetrics->rtcStatsObject.iceCandidatePairStats.bytesSent;
                pAppConfiguration->streamingSessionList[i]->rtcMetricsHistory.prevNumberOfBytesReceived =
                    pRtcMetrics->rtcStatsObject.iceCandidatePairStats.bytesReceived;
                pAppConfiguration->streamingSessionList[i]->rtcMetricsHistory.prevPacketsDiscardedOnSend =
                    pRtcMetrics->rtcStatsObject.iceCandidatePairStats.packetsDiscardedOnSend;
            }
        }
    }

CleanUp:

    if (locked) {
        MUTEX_UNLOCK(pAppConfiguration->appConfigurationObjLock);
    }
    SAFE_MEMFREE(pRtcMetrics);
    return retStatus;
}

static STATUS app_common_onSignalingMessageReceived(UINT64 userData, PReceivedSignalingMessage pReceivedSignalingMessage)
{
    STATUS retStatus = STATUS_SUCCESS;
    PAppConfiguration pAppConfiguration = (PAppConfiguration) userData;
    BOOL peerConnectionFound = FALSE;
    BOOL locked = FALSE;
    BOOL startStats = FALSE;
    UINT32 clientIdHashKey;
    UINT64 hashValue = 0;
    PPendingMessageQueue pPendingMsgQ = NULL;
    PStreamingSession pStreamingSession = NULL;

    CHK(pAppConfiguration != NULL, STATUS_APP_COMMON_NULL_ARG);

    MUTEX_LOCK(pAppConfiguration->appConfigurationObjLock);
    locked = TRUE;
    // find the corresponding streaming session.
    clientIdHashKey = COMPUTE_CRC32((PBYTE) pReceivedSignalingMessage->signalingMessage.peerClientId,
                                    (UINT32) STRLEN(pReceivedSignalingMessage->signalingMessage.peerClientId));
    CHK_STATUS((hash_table_contains(pAppConfiguration->pRemoteRtcPeerConnections, clientIdHashKey, &peerConnectionFound)));

    if (peerConnectionFound) {
        CHK_STATUS((hash_table_get(pAppConfiguration->pRemoteRtcPeerConnections, clientIdHashKey, &hashValue)));
        pStreamingSession = (PStreamingSession) hashValue;
    }

    switch (pReceivedSignalingMessage->signalingMessage.messageType) {
        case SIGNALING_MESSAGE_TYPE_OFFER:
            // Check if we already have an ongoing master session with the same peer
            CHK_ERR(!peerConnectionFound, STATUS_INVALID_OPERATION, "Peer connection %s is in progress. Drop this offfer.",
                    pReceivedSignalingMessage->signalingMessage.peerClientId);
            /*
             * Create new streaming session for each offer, then insert the client id and streaming session into
             * pRemoteRtcPeerConnections for subsequent ice candidate messages. Lastly check if there is
             * any ice candidate messages queued in pRemotePeerPendingSignalingMessages. If so then submit
             * all of them.
             */
            if (pAppConfiguration->streamingSessionCount == ARRAY_SIZE(pAppConfiguration->streamingSessionList)) {
                DLOGW("Max simultaneous streaming session count reached.");

                // Need to remove the pending queue if any.
                // This is a simple optimization as the session cleanup will
                // handle the cleanup of pending message queue after a while
                CHK_STATUS((app_msg_q_getPendingMsgQByHashVal(pAppConfiguration->pRemotePeerPendingSignalingMessages, clientIdHashKey, TRUE, &pPendingMsgQ)));
                CHK(FALSE, retStatus);
            }
            CHK_STATUS((app_common_createStreamingSession(pAppConfiguration, pReceivedSignalingMessage->signalingMessage.peerClientId, &pStreamingSession)));
            pStreamingSession->offerReceiveTime = GETTIME();
            MUTEX_LOCK(pAppConfiguration->streamingSessionListReadLock);
            pAppConfiguration->streamingSessionList[pAppConfiguration->streamingSessionCount++] = pStreamingSession;
            MUTEX_UNLOCK(pAppConfiguration->streamingSessionListReadLock);

            CHK_STATUS((app_common_handleOffer(pAppConfiguration, pStreamingSession, &pReceivedSignalingMessage->signalingMessage)));
            CHK_STATUS((hash_table_put(pAppConfiguration->pRemoteRtcPeerConnections, clientIdHashKey, (UINT64) pStreamingSession)));

            // If there are any ice candidate messages in the queue for this client id, submit them now.
            CHK_STATUS((app_msg_q_getPendingMsgQByHashVal(pAppConfiguration->pRemotePeerPendingSignalingMessages, clientIdHashKey, TRUE, &pPendingMsgQ)));
            CHK_STATUS((app_msg_q_handlePendingMsgQ(pPendingMsgQ, app_common_handleRemoteCandidate, pStreamingSession)));

            startStats = pAppConfiguration->iceCandidatePairStatsTimerId == MAX_UINT32;
            break;

        case SIGNALING_MESSAGE_TYPE_ICE_CANDIDATE:
            /*
             * if peer connection hasn't been created, create an queue to store the ice candidate message. Otherwise
             * submit the signaling message into the corresponding streaming session.
             */
            if (!peerConnectionFound) {
                CHK_STATUS((app_msg_q_getPendingMsgQByHashVal(pAppConfiguration->pRemotePeerPendingSignalingMessages, clientIdHashKey, FALSE, &pPendingMsgQ)));

                if (pPendingMsgQ == NULL) {
                    CHK_STATUS((app_msg_q_createPendingMsgQ(pAppConfiguration->pRemotePeerPendingSignalingMessages, clientIdHashKey, &pPendingMsgQ)));
                }
                CHK_STATUS((app_msg_q_pushMsqIntoPendingMsgQ(pPendingMsgQ, pReceivedSignalingMessage)));
            } else {
                CHK_STATUS((app_common_handleRemoteCandidate(pStreamingSession, &pReceivedSignalingMessage->signalingMessage)));
            }
            break;

        default:
            DLOGD("Unhandled signaling message type %u", pReceivedSignalingMessage->signalingMessage.messageType);
            break;
    }

    MUTEX_UNLOCK(pAppConfiguration->appConfigurationObjLock);
    locked = FALSE;
    #if 0
    if (startStats &&
        STATUS_FAILED(retStatus = timer_queue_addTimer(pAppConfiguration->timerQueueHandle, APP_STATS_DURATION, APP_STATS_DURATION,
                                                  app_common_onIceCandidatePairStats, (UINT64) pAppConfiguration,
                                                  &pAppConfiguration->iceCandidatePairStatsTimerId))) {
        DLOGW("Failed to add app_common_onIceCandidatePairStats to add to timer queue (code 0x%08x). "
              "Cannot pull ice candidate pair metrics periodically",
              retStatus);

        // Reset the returned status
        retStatus = STATUS_SUCCESS;
    }
    #endif

CleanUp:

    if (locked) {
        MUTEX_UNLOCK(pAppConfiguration->appConfigurationObjLock);
    }

    CHK_LOG_ERR((retStatus));
    return retStatus;
}

static STATUS app_common_pregenerateCertTimerCallback(UINT32 timerId, UINT64 currentTime, UINT64 userData)
{
    UNUSED_PARAM(timerId);
    UNUSED_PARAM(currentTime);
    STATUS retStatus = STATUS_SUCCESS;
    BOOL locked = FALSE;
    PAppConfiguration pAppConfiguration = (PAppConfiguration) userData;

    CHK_WARN(pAppConfiguration != NULL, STATUS_APP_COMMON_NULL_ARG, "app_common_pregenerateCertTimerCallback(): Passed argument is NULL");
    CHK_WARN(MUTEX_WAITLOCK(pAppConfiguration->appConfigurationObjLock, 50*HUNDREDS_OF_NANOS_IN_A_MILLISECOND) == TRUE, STATUS_APP_COMMON_ACQUIRE_MUTEX, "app_common_pregenerateCertTimerCallback failed");
    locked = TRUE;

    app_credential_generateCertRoutine(&pAppConfiguration->appCredential);

CleanUp:
    if(locked == TRUE){
        MUTEX_UNLOCK(pAppConfiguration->appConfigurationObjLock);
    }
    return retStatus;
}

PVOID app_common_runMediaSource(PVOID userData)
{
    STATUS retStatus = STATUS_SUCCESS;
    PAppConfiguration pAppConfiguration = (PAppConfiguration) userData;
    TID mediaSourceTid = INVALID_TID_VALUE;

    DLOGD("The media source is starting.");
    // confirm the peer connection is ready or not.
    ATOMIC_STORE_BOOL(&pAppConfiguration->abortMediaControl, FALSE);

    MUTEX_LOCK(pAppConfiguration->appConfigurationObjLock);
    while (!ATOMIC_LOAD_BOOL(&pAppConfiguration->peerConnectionConnected) && !ATOMIC_LOAD_BOOL(&pAppConfiguration->terminateApp) && !ATOMIC_LOAD_BOOL(&pAppConfiguration->abortMediaControl)) {
        CVAR_WAIT(pAppConfiguration->cvar, pAppConfiguration->appConfigurationObjLock, 5 * HUNDREDS_OF_NANOS_IN_A_SECOND);
    }
    MUTEX_UNLOCK(pAppConfiguration->appConfigurationObjLock);

    CHK(!ATOMIC_LOAD_BOOL(&pAppConfiguration->terminateApp), retStatus);
    CHK(!ATOMIC_LOAD_BOOL(&pAppConfiguration->abortMediaControl), retStatus);
    // run the routine of media sources including video and audio.

    if (pAppConfiguration->mediaSource != NULL) {
        pAppConfiguration->mediaSource(pAppConfiguration->pMediaContext);
    }

CleanUp:
    // clean the flag of the media thread.
    ATOMIC_STORE_BOOL(&pAppConfiguration->mediaThreadStarted, FALSE);
    DLOGD("The media source is terminated.");
    THREAD_EXIT(NULL);
    CHK_LOG_ERR((retStatus));
    return NULL;
}

static STATUS app_common_initializePeerConnection(PAppConfiguration pAppConfiguration, PRtcPeerConnection* ppRtcPeerConnection)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PRtcConfiguration pConfiguration = NULL;
    UINT32 uriCount = MAX_ICE_SERVERS_COUNT;
    UINT64 curTime;
    PRtcCertificate pRtcCertificate = NULL;

    CHK(NULL != (pConfiguration = (PRtcConfiguration) MEMCALLOC(1, SIZEOF(RtcConfiguration))), STATUS_APP_COMMON_NOT_ENOUGH_MEMORY);

    // Set this to custom callback to enable filtering of interfaces
    pConfiguration->kvsRtcConfiguration.iceSetInterfaceFilterFunc = NULL;

    // Set the ICE mode explicitly
    pConfiguration->iceTransportPolicy = ICE_TRANSPORT_POLICY_ALL;

    CHK_STATUS((app_signaling_queryServer(&pAppConfiguration->appSignaling, pConfiguration->iceServers, &uriCount)));

    pAppConfiguration->iceUriCount = uriCount + 1;

    CHK_STATUS((popGeneratedCert(&pAppConfiguration->appCredential, &pRtcCertificate)));

    if (pRtcCertificate != NULL) {
        pConfiguration->certificates[0] = *pRtcCertificate;
        pRtcCertificate = NULL;
    }

    curTime = GETTIME();
    CHK_STATUS((pc_create(pConfiguration, ppRtcPeerConnection)));
    DLOGD("time taken to create peer connection %" PRIu64 " ms", (GETTIME() - curTime) / HUNDREDS_OF_NANOS_IN_A_MILLISECOND);

CleanUp:

    CHK_LOG_ERR((retStatus));

    // Free the certificate which can be NULL as we no longer need it and won't reuse
    rtc_certificate_free(pRtcCertificate);
    SAFE_MEMFREE(pConfiguration);
    LEAVES();
    return retStatus;
}

STATUS app_common_createStreamingSession(PAppConfiguration pAppConfiguration, PCHAR peerId, PStreamingSession* ppStreamingSession)
{
    STATUS retStatus = STATUS_SUCCESS;
    PRtcMediaStreamTrack pVideoTrack = NULL;
    PRtcMediaStreamTrack pAudioTrack = NULL;
    PStreamingSession pStreamingSession = NULL;
    PAppMediaSrc pAppMediaSrc = NULL;
    RtcRtpTransceiverInit audioRtpTransceiverInit;
    RtcRtpTransceiverInit videoRtpTransceiverInit;
    RTC_CODEC codec;

    CHK(NULL != (pVideoTrack = (PRtcMediaStreamTrack) MEMCALLOC(1, SIZEOF(RtcMediaStreamTrack))), STATUS_APP_COMMON_NOT_ENOUGH_MEMORY);
    CHK(NULL != (pAudioTrack = (PRtcMediaStreamTrack) MEMCALLOC(1, SIZEOF(RtcMediaStreamTrack))), STATUS_APP_COMMON_NOT_ENOUGH_MEMORY);

    pStreamingSession = (PStreamingSession) MEMCALLOC(1, SIZEOF(StreamingSession));
    CHK(pStreamingSession != NULL, STATUS_APP_COMMON_NOT_ENOUGH_MEMORY);
    pAppMediaSrc = pAppConfiguration->pAppMediaSrc;
    CHK(pAppMediaSrc != NULL, STATUS_APP_COMMON_NULL_ARG);
    CHK_STATUS((pAppMediaSrc->app_media_source_isReady(pAppConfiguration->pMediaContext)));

    STRCPY(pStreamingSession->peerId, peerId);
    ATOMIC_STORE_BOOL(&pStreamingSession->peerIdReceived, TRUE);

    pStreamingSession->pAppConfiguration = pAppConfiguration;
    pStreamingSession->rtcMetricsHistory.prevTs = GETTIME();
    // if we're the viewer, we control the trickle ice mode
    pStreamingSession->remoteCanTrickleIce = FALSE;

    ATOMIC_STORE_BOOL(&pStreamingSession->terminateFlag, FALSE);
    ATOMIC_STORE_BOOL(&pStreamingSession->candidateGatheringDone, FALSE);

    CHK_STATUS((app_common_initializePeerConnection(pAppConfiguration, &pStreamingSession->pPeerConnection)));
    CHK_STATUS((pc_onIceCandidate(pStreamingSession->pPeerConnection, (UINT64) pStreamingSession, app_common_onIceCandidate)));
    CHK_STATUS((pc_onConnectionStateChange(pStreamingSession->pPeerConnection, (UINT64) pStreamingSession, app_common_onConnectionStateChange)));
#ifdef ENABLE_DATA_CHANNEL
    CHK_STATUS((pc_onDataChannel(pStreamingSession->pPeerConnection, (UINT64) pStreamingSession, onDataChannel)));
#endif

    // Add a SendRecv Transceiver of type video
    CHK_STATUS((pAppMediaSrc->app_media_source_queryVideoCap(pAppConfiguration->pMediaContext, &codec)));
    CHK_STATUS((pc_addSupportedCodec(pStreamingSession->pPeerConnection, codec)));
    pVideoTrack->kind = MEDIA_STREAM_TRACK_KIND_VIDEO;
    pVideoTrack->codec = codec;
    videoRtpTransceiverInit.direction = RTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY;
    STRCPY(pVideoTrack->streamId, APP_VIDEO_TRACK_STREAM_ID);
    STRCPY(pVideoTrack->trackId, APP_VIDEO_TRACK_ID);
    CHK_STATUS(
        (pc_addTransceiver(pStreamingSession->pPeerConnection, pVideoTrack, &videoRtpTransceiverInit, &pStreamingSession->pVideoRtcRtpTransceiver)));

    CHK_STATUS(
        (rtp_transceiver_onBandwidthEstimation(pStreamingSession->pVideoRtcRtpTransceiver, (UINT64) pStreamingSession, app_common_onBandwidthEstimation)));

    // Add a SendRecv Transceiver of type audio
    //CHK_STATUS((pAppMediaSrc->app_media_source_queryAudioCap(pAppConfiguration->pMediaContext, &codec)));
    //CHK_STATUS((pc_addSupportedCodec(pStreamingSession->pPeerConnection, codec)));
    //pAudioTrack->kind = MEDIA_STREAM_TRACK_KIND_AUDIO;
    //pAudioTrack->codec = codec;
#ifdef ENABLE_AUDIO_SENDRECV
    //audioRtpTransceiverInit.direction =  RTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV;
#else
    //audioRtpTransceiverInit.direction =  RTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY;
#endif
    //STRCPY(pAudioTrack->streamId, APP_AUDIO_TRACK_STREAM_ID);
    //STRCPY(pAudioTrack->trackId, APP_AUDIO_TRACK_ID);
    //CHK_STATUS(
    //   (pc_addTransceiver(pStreamingSession->pPeerConnection, pAudioTrack, &audioRtpTransceiverInit, &pStreamingSession->pAudioRtcRtpTransceiver)));

    //CHK_STATUS(
    //   (rtp_transceiver_onBandwidthEstimation(pStreamingSession->pAudioRtcRtpTransceiver, (UINT64) pStreamingSession, app_common_onBandwidthEstimation)));
    // twcc bandwidth estimation
    // CHK_STATUS((peerConnectionOnSenderBandwidthEstimation(pStreamingSession->pPeerConnection, (UINT64) pStreamingSession,
                                                         // app_common_onSenderBandwidthEstimation)));

    pStreamingSession->firstFrame = TRUE;
    pStreamingSession->startUpLatency = 0;

#ifdef ENABLE_DATA_CHANNEL
    CHK_STATUS(data_channel_create(pStreamingSession->pPeerConnection, "kvsDataChannelMaster", NULL, &pStreamingSession->pRtcDataChannel));
    CHK_LOG_ERR(data_channel_onMessage(pStreamingSession->pRtcDataChannel, 0, onDataChannelMessageMaster));
#endif

CleanUp:

    if (STATUS_FAILED(retStatus) && pStreamingSession != NULL) {
        app_common_freeStreamingSession(&pStreamingSession);
        pStreamingSession = NULL;
    }

    *ppStreamingSession = pStreamingSession;
    SAFE_MEMFREE(pVideoTrack);
    SAFE_MEMFREE(pAudioTrack);
    return retStatus;
}

STATUS app_common_freeStreamingSession(PStreamingSession* ppStreamingSession)
{
    STATUS retStatus = STATUS_SUCCESS;
    PStreamingSession pStreamingSession = NULL;
    PAppConfiguration pAppConfiguration;

    pStreamingSession = *ppStreamingSession;
    CHK(pStreamingSession->pAppConfiguration != NULL, STATUS_APP_COMMON_NULL_ARG);
    pAppConfiguration = pStreamingSession->pAppConfiguration;

    DLOGD("Freeing streaming session with peer id: %s ", pStreamingSession->peerId);

    ATOMIC_STORE_BOOL(&pStreamingSession->terminateFlag, TRUE);

    // De-initialize the session stats timer if there are no active sessions
    // NOTE: we need to perform this under the lock which might be acquired by
    // the running thread but it's OK as it's re-entrant
    MUTEX_LOCK(pAppConfiguration->appConfigurationObjLock);
    if (pAppConfiguration->iceCandidatePairStatsTimerId != MAX_UINT32 && pAppConfiguration->streamingSessionCount == 0) {
        //#TBD. this may causes deadlock.

        CHK_LOG_ERR(
            (timer_queue_cancelTimer(pAppConfiguration->timerQueueHandle, pAppConfiguration->iceCandidatePairStatsTimerId, (UINT64) pAppConfiguration)));
        pAppConfiguration->iceCandidatePairStatsTimerId = MAX_UINT32;
    }
    MUTEX_UNLOCK(pAppConfiguration->appConfigurationObjLock);

    CHK_LOG_ERR((pc_close(pStreamingSession->pPeerConnection)));
    CHK_LOG_ERR((pc_free(&pStreamingSession->pPeerConnection)));
    MEMFREE(pStreamingSession);

CleanUp:

    CHK_LOG_ERR((retStatus));

    return retStatus;
}

static STATUS app_common_gatherIceServerStats(PStreamingSession pStreamingSession)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 j = 0;

    for (; j < pStreamingSession->pAppConfiguration->iceUriCount; j++) {
        CHK_STATUS((app_metrics_logIceServerStats(pStreamingSession->pPeerConnection, j)));
    }

CleanUp:
    LEAVES();
    return retStatus;
}

STATUS initApp(BOOL trickleIce, BOOL useTurn, PAppMediaSrc pAppMediaSrc, PAppConfiguration* ppAppConfiguration)
{
    STATUS retStatus = STATUS_SUCCESS;
    PAppConfiguration pAppConfiguration = NULL;
    PAppSignaling pAppSignaling = NULL;
    PCHAR pChannel = NULL;

    SET_LOGGER_LOG_LEVEL(getLogLevel());

    CHK(ppAppConfiguration != NULL, STATUS_APP_COMMON_NULL_ARG);
    CHK(pAppMediaSrc != NULL, STATUS_APP_COMMON_NULL_ARG);
    CHK((pChannel = GETENV(APP_WEBRTC_CHANNEL)) != NULL, STATUS_APP_COMMON_CHANNEL_NAME);
    CHK(NULL != (pAppConfiguration = (PAppConfiguration) MEMCALLOC(1, SIZEOF(AppConfiguration))), STATUS_APP_COMMON_NOT_ENOUGH_MEMORY);

    pAppSignaling = &pAppConfiguration->appSignaling;
    pAppSignaling->signalingClientHandle = INVALID_SIGNALING_CLIENT_HANDLE_VALUE;
    pAppConfiguration->mediaControlTid = INVALID_TID_VALUE;
    pAppConfiguration->timerQueueHandle = INVALID_TIMER_QUEUE_HANDLE_VALUE;
    pAppConfiguration->iceCandidatePairStatsTimerId = MAX_UINT32;
    pAppConfiguration->pregenerateCertTimerId = MAX_UINT32;

    DLOGD("initializing the app with channel(%s)", pChannel);

    app_metrics_setupFileLogging(&pAppConfiguration->enableFileLogging);
    CHK_STATUS((app_credential_create(&pAppConfiguration->appCredential)));

    pAppConfiguration->appConfigurationObjLock = MUTEX_CREATE(TRUE);
    CHK(IS_VALID_MUTEX_VALUE(pAppConfiguration->appConfigurationObjLock), STATUS_APP_COMMON_INVALID_MUTEX);
    pAppConfiguration->cvar = CVAR_CREATE();
    pAppConfiguration->streamingSessionListReadLock = MUTEX_CREATE(FALSE);
    CHK(IS_VALID_MUTEX_VALUE(pAppConfiguration->streamingSessionListReadLock), STATUS_APP_COMMON_INVALID_MUTEX);
    CHK(timer_queue_createEx(&pAppConfiguration->timerQueueHandle, APP_COMMON_TIMER_NAME, APP_COMMON_TIMER_SIZE) == STATUS_SUCCESS, STATUS_APP_COMMON_TIMER);

    pAppConfiguration->trickleIce = trickleIce;
    pAppSignaling->pAppCredential = &pAppConfiguration->appCredential;
    pAppSignaling->channelInfo.pRegion = GETENV(DEFAULT_REGION_ENV_VAR) == NULL ? DEFAULT_AWS_REGION : GETENV(DEFAULT_REGION_ENV_VAR);
    pAppSignaling->channelInfo.version = CHANNEL_INFO_CURRENT_VERSION;
    pAppSignaling->channelInfo.pChannelName = pChannel;
    pAppSignaling->channelInfo.pKmsKeyId = NULL;
    pAppSignaling->channelInfo.tagCount = 0;
    pAppSignaling->channelInfo.pTags = NULL;
    pAppSignaling->channelInfo.channelType = SIGNALING_CHANNEL_TYPE_SINGLE_MASTER;
    pAppSignaling->channelInfo.channelRoleType = SIGNALING_CHANNEL_ROLE_TYPE_MASTER;
    pAppSignaling->channelInfo.cachingPolicy = SIGNALING_API_CALL_CACHE_TYPE_NONE;
    pAppSignaling->channelInfo.cachingPeriod = SIGNALING_API_CALL_CACHE_TTL_SENTINEL_VALUE;
    pAppSignaling->channelInfo.retry = TRUE;
    pAppSignaling->channelInfo.reconnect = TRUE;
    pAppSignaling->channelInfo.pCertPath = pAppConfiguration->appCredential.pCaCertPath;
    pAppSignaling->channelInfo.messageTtl = 0; // Default is 60 seconds

    pAppSignaling->clientInfo.version = SIGNALING_CLIENT_INFO_CURRENT_VERSION;
    //pAppSignaling->clientInfo.loggingLevel = getLogLevel();
    //pAppSignaling->clientInfo.cacheFilePath = NULL; // Use the default path
    STRCPY(pAppSignaling->clientInfo.clientId, APP_MASTER_CLIENT_ID);
    
    CHK_STATUS((app_signaling_init(pAppSignaling, app_common_onSignalingMessageReceived, app_common_onSignalingClientStateChanged, app_common_onSignalingClientError,
                                 (UINT64) pAppConfiguration, useTurn)));

    ATOMIC_STORE_BOOL(&pAppConfiguration->sigInt, FALSE);
    ATOMIC_STORE_BOOL(&pAppConfiguration->mediaThreadStarted, FALSE);
    ATOMIC_STORE_BOOL(&pAppConfiguration->terminateApp, FALSE);
    ATOMIC_STORE_BOOL(&pAppConfiguration->restartSignalingClient, FALSE);
    ATOMIC_STORE_BOOL(&pAppConfiguration->peerConnectionConnected, FALSE);
    ATOMIC_STORE_BOOL(&pAppConfiguration->abortMediaControl, FALSE);

    pAppConfiguration->iceUriCount = 0;

    CHK_STATUS((app_msg_q_createConnectionMsqQ(&pAppConfiguration->pRemotePeerPendingSignalingMessages)));
    CHK_STATUS(
        (hash_table_createWithParams(APP_HASH_TABLE_BUCKET_COUNT, APP_HASH_TABLE_BUCKET_LENGTH, &pAppConfiguration->pRemoteRtcPeerConnections)));

    pAppConfiguration->pAppMediaSrc = pAppMediaSrc;
    // the initialization of media source.
    CHK_STATUS((pAppMediaSrc->app_media_source_init(&pAppConfiguration->pMediaContext)));
    CHK_STATUS((pAppMediaSrc->app_media_source_linkSinkHook(pAppConfiguration->pMediaContext, app_common_onMediaSinkHook, pAppConfiguration)));
    CHK_STATUS((pAppMediaSrc->app_media_source_linkEosHook(pAppConfiguration->pMediaContext, app_common_onMediaEosHook, pAppConfiguration)));

    pAppConfiguration->mediaSource = pAppMediaSrc->app_media_source_run;
    DLOGD("The intialization of the media source is completed successfully");

    // Initalize KVS WebRTC. This must be done before anything else, and must only be done once.
    CHK_STATUS((app_webrtc_init(pAppConfiguration)));
    DLOGD("The initialization of WebRTC  is completed successfully");
    gAppConfiguration = pAppConfiguration;

    // Start the cert pre-gen timer callback
    if (APP_PRE_GENERATE_CERT) {
        CHK_LOG_ERR((retStatus = timer_queue_addTimer(pAppConfiguration->timerQueueHandle, 0, APP_PRE_GENERATE_CERT_PERIOD, app_common_pregenerateCertTimerCallback,
                                                 (UINT64) pAppConfiguration, &pAppConfiguration->pregenerateCertTimerId)));
    }

    gInitialized = TRUE;

CleanUp:

    if (STATUS_FAILED(retStatus) && pAppConfiguration != NULL) {
        freeApp(&pAppConfiguration);
    }

    if (ppAppConfiguration != NULL) {
        *ppAppConfiguration = pAppConfiguration;
    }

    return retStatus;
}

STATUS runApp(PAppConfiguration pAppConfiguration)
{
    STATUS retStatus = STATUS_SUCCESS;
    retStatus = app_signaling_connect(&pAppConfiguration->appSignaling);
    if (retStatus != STATUS_SUCCESS) {
        DLOGD("operation returned status code: 0x%08x ", retStatus);
    }

    return retStatus;
}

STATUS freeApp(PAppConfiguration* ppAppConfiguration)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PAppConfiguration pAppConfiguration = NULL;
    PAppMediaSrc pAppMediaSrc = NULL;
    UINT32 i;
    BOOL locked = FALSE;

    CHK(ppAppConfiguration != NULL, STATUS_APP_COMMON_NULL_ARG);
    pAppConfiguration = *ppAppConfiguration;
    CHK(pAppConfiguration != NULL, STATUS_APP_COMMON_NULL_ARG);

    // Kick of the termination sequence
    ATOMIC_STORE_BOOL(&pAppConfiguration->terminateApp, TRUE);

    pAppMediaSrc = pAppConfiguration->pAppMediaSrc;
    if(pAppMediaSrc != NULL){
        pAppMediaSrc->app_media_source_shutdown(pAppConfiguration->pMediaContext);
    }

    if (pAppConfiguration->mediaControlTid != INVALID_TID_VALUE) {
        ATOMIC_STORE_BOOL(&pAppConfiguration->abortMediaControl, TRUE);
        THREAD_JOIN(pAppConfiguration->mediaControlTid, NULL);
        pAppConfiguration->mediaControlTid = INVALID_TID_VALUE;
    }

    app_signaling_free(&pAppConfiguration->appSignaling);

    if (IS_VALID_MUTEX_VALUE(pAppConfiguration->appConfigurationObjLock)) {
        MUTEX_LOCK(pAppConfiguration->appConfigurationObjLock);
        locked = TRUE;
    }

    app_msg_q_freeConnectionMsgQ(&pAppConfiguration->pRemotePeerPendingSignalingMessages);
    hash_table_clear(pAppConfiguration->pRemoteRtcPeerConnections);
    hash_table_free(pAppConfiguration->pRemoteRtcPeerConnections);

    for (i = 0; i < pAppConfiguration->streamingSessionCount; ++i) {
        //#TBD, need to add the feature of metrics.
        #if 0
        retStatus = app_common_gatherIceServerStats(pAppConfiguration->streamingSessionList[i]);
        if (STATUS_FAILED(retStatus)) {
            DLOGW("Failed to ICE Server Stats for streaming session %d: %08x", i, retStatus);
        }
        #endif
        app_common_freeStreamingSession(&pAppConfiguration->streamingSessionList[i]);
    }
    if (locked) {
        MUTEX_UNLOCK(pAppConfiguration->appConfigurationObjLock);
    }

    app_webrtc_deinit(pAppConfiguration);
    if(pAppMediaSrc != NULL){
        pAppMediaSrc->app_media_source_detroy(&pAppConfiguration->pMediaContext);
    }

    if (IS_VALID_CVAR_VALUE(pAppConfiguration->cvar) && IS_VALID_MUTEX_VALUE(pAppConfiguration->appConfigurationObjLock)) {
        CVAR_BROADCAST(pAppConfiguration->cvar);
        MUTEX_LOCK(pAppConfiguration->appConfigurationObjLock);
        MUTEX_UNLOCK(pAppConfiguration->appConfigurationObjLock);
    }

    if (IS_VALID_MUTEX_VALUE(pAppConfiguration->appConfigurationObjLock)) {
        MUTEX_FREE(pAppConfiguration->appConfigurationObjLock);
        pAppConfiguration->appConfigurationObjLock = INVALID_MUTEX_VALUE;
    }

    if (IS_VALID_MUTEX_VALUE(pAppConfiguration->streamingSessionListReadLock)) {
        MUTEX_FREE(pAppConfiguration->streamingSessionListReadLock);
        pAppConfiguration->streamingSessionListReadLock = INVALID_MUTEX_VALUE;
    }

    if (IS_VALID_CVAR_VALUE(pAppConfiguration->cvar)) {
        CVAR_FREE(pAppConfiguration->cvar);
    }

    if (IS_VALID_TIMER_QUEUE_HANDLE(pAppConfiguration->timerQueueHandle)) {
        if (pAppConfiguration->iceCandidatePairStatsTimerId != MAX_UINT32) {
            retStatus =
                timer_queue_cancelTimer(pAppConfiguration->timerQueueHandle, pAppConfiguration->iceCandidatePairStatsTimerId, (UINT64) pAppConfiguration);
            if (STATUS_FAILED(retStatus)) {
                DLOGE("Failed to cancel stats timer with: 0x%08x", retStatus);
            }
            pAppConfiguration->iceCandidatePairStatsTimerId = MAX_UINT32;
        }

        if (pAppConfiguration->pregenerateCertTimerId != MAX_UINT32) {
            retStatus =
                timer_queue_cancelTimer(pAppConfiguration->timerQueueHandle, pAppConfiguration->pregenerateCertTimerId, (UINT64) pAppConfiguration);
            if (STATUS_FAILED(retStatus)) {
                DLOGE("Failed to cancel certificate pre-generation timer with: 0x%08x", retStatus);
            }
            pAppConfiguration->pregenerateCertTimerId = MAX_UINT32;
        }
        timer_queue_free(&pAppConfiguration->timerQueueHandle);
    }

    app_credential_destroy(&pAppConfiguration->appCredential);
    if (pAppConfiguration->enableFileLogging) {
        app_metrics_closeFileLogging();
    }

    MEMFREE(*ppAppConfiguration);
    *ppAppConfiguration = NULL;
    gInitialized = FALSE;
CleanUp:

    LEAVES();
    return retStatus;
}

STATUS pollApp(PAppConfiguration pAppConfiguration)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PStreamingSession pStreamingSession = NULL;
    UINT32 i, clientIdHashKey;
    BOOL locked = FALSE, peerConnectionFound = FALSE;
    BOOL sessionLocked = FALSE;
    BOOL shutdownMediaSource = FALSE;

    CHK(pAppConfiguration != NULL, STATUS_APP_COMMON_NULL_ARG);

    while (!ATOMIC_LOAD_BOOL(&pAppConfiguration->sigInt)) {

        // Keep the main set of operations interlocked until cvar wait which would atomically unlock
        MUTEX_LOCK(pAppConfiguration->appConfigurationObjLock);
        locked = TRUE;

        // scan and cleanup terminated streaming session
        for (i = 0; i < pAppConfiguration->streamingSessionCount; ++i) {
            if (ATOMIC_LOAD_BOOL(&pAppConfiguration->streamingSessionList[i]->terminateFlag)) {
                pStreamingSession = pAppConfiguration->streamingSessionList[i];

                MUTEX_LOCK(pAppConfiguration->streamingSessionListReadLock);
                sessionLocked = TRUE;
                // swap with last element and decrement count
                pAppConfiguration->streamingSessionCount--;
                if (pAppConfiguration->streamingSessionCount > 0) {
                    pAppConfiguration->streamingSessionList[i] = pAppConfiguration->streamingSessionList[pAppConfiguration->streamingSessionCount];
                    i--;
                }

                // Remove from the hash table
                clientIdHashKey = COMPUTE_CRC32((PBYTE) pStreamingSession->peerId, (UINT32) STRLEN(pStreamingSession->peerId));
                CHK_STATUS((hash_table_contains(pAppConfiguration->pRemoteRtcPeerConnections, clientIdHashKey, &peerConnectionFound)));
                if (peerConnectionFound) {
                    CHK_STATUS((hash_table_remove(pAppConfiguration->pRemoteRtcPeerConnections, clientIdHashKey)));
                }
                if (pAppConfiguration->streamingSessionCount == 0) {
                    shutdownMediaSource = TRUE;
                }
                MUTEX_UNLOCK(pAppConfiguration->streamingSessionListReadLock);
                sessionLocked = FALSE;
                app_common_freeStreamingSession(&pStreamingSession);
            }
        }
        // Check if we need to re-create the signaling client on-the-fly
        if (ATOMIC_LOAD_BOOL(&pAppConfiguration->restartSignalingClient) && STATUS_SUCCEEDED(app_signaling_restart(&pAppConfiguration->appSignaling))) {
            // Re-set the variable again
            ATOMIC_STORE_BOOL(&pAppConfiguration->restartSignalingClient, FALSE);
        }

        CHK_STATUS((app_signaling_check(&pAppConfiguration->appSignaling)));
        // Check if any lingering pending message queues
        CHK_STATUS((app_msg_q_removeExpiredPendingMsgQ(pAppConfiguration->pRemotePeerPendingSignalingMessages, APP_PENDING_MESSAGE_CLEANUP_DURATION)));

        if(shutdownMediaSource == TRUE){
            PAppMediaSrc pAppMediaSrc = pAppConfiguration->pAppMediaSrc;
            if(pAppMediaSrc != NULL){
                pAppMediaSrc->app_media_source_shutdown(pAppConfiguration->pMediaContext);
            }

            if (pAppConfiguration->mediaControlTid != INVALID_TID_VALUE) {
                ATOMIC_STORE_BOOL(&pAppConfiguration->abortMediaControl, TRUE);
                MUTEX_UNLOCK(pAppConfiguration->appConfigurationObjLock);
                THREAD_JOIN(pAppConfiguration->mediaControlTid, NULL);
                MUTEX_LOCK(pAppConfiguration->appConfigurationObjLock);
                pAppConfiguration->mediaControlTid = INVALID_TID_VALUE;
            }
            shutdownMediaSource = FALSE;
        }
        // periodically wake up and clean up terminated streaming session
        CVAR_WAIT(pAppConfiguration->cvar, pAppConfiguration->appConfigurationObjLock, APP_CLEANUP_WAIT_PERIOD);
        MUTEX_UNLOCK(pAppConfiguration->appConfigurationObjLock);
        locked = FALSE;
    }

CleanUp:

    CHK_LOG_ERR((retStatus));
    if(sessionLocked){
        MUTEX_UNLOCK(pAppConfiguration->streamingSessionListReadLock);
    }
    if (locked) {
        MUTEX_UNLOCK(pAppConfiguration->appConfigurationObjLock);
    }
    DLOGD("pollApp exits");
    LEAVES();
    return retStatus;
}

STATUS quitApp(VOID)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 counter = 0;
    DLOGD("Quit webrtc app.");
    if(gAppConfiguration != NULL){
        ATOMIC_STORE_BOOL(&gAppConfiguration->sigInt, TRUE);
        CVAR_BROADCAST(gAppConfiguration->cvar);
    }

    while(gInitialized && counter < 100){
        THREAD_SLEEP(50 * HUNDREDS_OF_NANOS_IN_A_MILLISECOND);
        counter++;
    }

    if(counter >= 100){
        DLOGE("Failed to quit app.");
        retStatus = STATUS_APP_COMMON_QUIT_APP;
    }

    LEAVES();
    return retStatus;
}
