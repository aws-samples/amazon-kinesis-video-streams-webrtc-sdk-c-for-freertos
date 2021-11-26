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
#include "AppFileSrc.h"
#include "AppSignaling.h"
#include "AppWebRTC.h"

static PAppConfiguration gAppConfiguration = NULL; //!< for the system-level signal handler

STATUS createStreamingSession(PAppConfiguration pAppConfiguration, PCHAR peerId, PStreamingSession* ppStreamingSession);
STATUS freeStreamingSession(PStreamingSession* ppStreamingSession);
PVOID mediaSenderRoutine(PVOID userData);

static VOID sigIntHandler(INT32 sigNum)
{
    UNUSED_PARAM(sigNum);
    if (gAppConfiguration != NULL) {
        ATOMIC_STORE_BOOL(&gAppConfiguration->sigInt, TRUE);
        CVAR_BROADCAST(gAppConfiguration->cvar);
    }
}

static STATUS onMediaSinkHook(PVOID udata, PFrame pFrame)
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
        retStatus = writeFrame(pRtcRtpTransceiver, pFrame);
        if (retStatus != STATUS_SUCCESS) {
            // STATUS_SRTP_NOT_READY_YET
            DLOGW("writeFrame() failed with 0x%08x", retStatus);
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

static STATUS onMediaEosHook(PVOID udata)
{
    STATUS retStatus = STATUS_SUCCESS;
    PAppConfiguration pAppConfiguration = (PAppConfiguration) udata;
    UINT32 i;
    // close all the streaming session.
    MUTEX_LOCK(pAppConfiguration->appConfigurationObjLock);
    for (i = 0; i < pAppConfiguration->streamingSessionCount; ++i) {
        DLOGD("terminate the streaming session(%d)", i);
        ATOMIC_STORE_BOOL(&pAppConfiguration->streamingSessionList[i]->terminateFlag, TRUE);
    }
    MUTEX_UNLOCK(pAppConfiguration->appConfigurationObjLock);
    CVAR_BROADCAST(pAppConfiguration->cvar);
    return retStatus;
}

static VOID onConnectionStateChange(UINT64 userData, RTC_PEER_CONNECTION_STATE newState)
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

static STATUS onSignalingClientStateChanged(UINT64 userData, SIGNALING_CLIENT_STATE state)
{
    UNUSED_PARAM(userData);
    STATUS retStatus = STATUS_SUCCESS;
    PCHAR pStateStr;

    signalingClientGetStateString(state, &pStateStr);
    DLOGV("Signaling client state changed to %d - '%s'", state, pStateStr);

    // Return success to continue
    return retStatus;
}

static STATUS onSignalingClientError(UINT64 userData, STATUS status, PCHAR msg, UINT32 msgLen)
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

static VOID onBandwidthEstimationHandler(UINT64 userData, DOUBLE maxiumBitrate)
{
    UNUSED_PARAM(userData);
    DLOGV("received bitrate suggestion: %f", maxiumBitrate);
}

static VOID onSenderBandwidthEstimationHandler(UINT64 userData, UINT32 txBytes, UINT32 rxBytes, UINT32 txPacketsCnt, UINT32 rxPacketsCnt,
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

static STATUS handleRemoteCandidate(PStreamingSession pStreamingSession, PSignalingMessage pSignalingMessage)
{
    STATUS retStatus = STATUS_SUCCESS;
    PRtcIceCandidateInit pIceCandidate = NULL;
    CHK(NULL != (pIceCandidate = (PRtcIceCandidateInit) MEMCALLOC(1, SIZEOF(RtcIceCandidateInit))), STATUS_APP_COMMON_NOT_ENOUGH_MEMORY);

    CHK(pStreamingSession != NULL, STATUS_APP_COMMON_NULL_ARG);
    CHK_STATUS((deserializeRtcIceCandidateInit(pSignalingMessage->payload, pSignalingMessage->payloadLen, pIceCandidate)));
    CHK_STATUS((addIceCandidate(pStreamingSession->pPeerConnection, pIceCandidate->candidate)));

CleanUp:
    SAFE_MEMFREE(pIceCandidate);
    CHK_LOG_ERR((retStatus));
    return retStatus;
}

static STATUS respondWithAnswer(PStreamingSession pStreamingSession)
{
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingMessage pMessage = NULL;
    UINT32 buffLen = MAX_SIGNALING_MESSAGE_LEN;

    CHK(NULL != (pMessage = (PSignalingMessage) MEMCALLOC(1, SIZEOF(SignalingMessage))), STATUS_APP_COMMON_NOT_ENOUGH_MEMORY);
    CHK_STATUS((serializeSessionDescriptionInit(&pStreamingSession->answerSessionDescriptionInit, pMessage->payload, &buffLen)));

    pMessage->version = SIGNALING_MESSAGE_CURRENT_VERSION;
    pMessage->messageType = SIGNALING_MESSAGE_TYPE_ANSWER;
    STRNCPY(pMessage->peerClientId, pStreamingSession->peerId, MAX_SIGNALING_CLIENT_ID_LEN);
    pMessage->payloadLen = (UINT32) STRLEN(pMessage->payload);
    pMessage->correlationId[0] = '\0';
    
    CHK_STATUS((sendAppSignalingMessage(&pStreamingSession->pAppConfiguration->appSignaling, pMessage)));

CleanUp:
    SAFE_MEMFREE(pMessage);
    CHK_LOG_ERR((retStatus));
    return retStatus;
}

static STATUS handleOffer(PAppConfiguration pAppConfiguration, PStreamingSession pStreamingSession, PSignalingMessage pSignalingMessage)
{
    STATUS retStatus = STATUS_SUCCESS;
    PRtcSessionDescriptionInit pOfferSessionDescriptionInit = NULL;
    NullableBool canTrickle;
    BOOL mediaThreadStarted;

    CHK(NULL != (pOfferSessionDescriptionInit = (PRtcSessionDescriptionInit) MEMCALLOC(1, SIZEOF(RtcSessionDescriptionInit))), STATUS_APP_COMMON_NOT_ENOUGH_MEMORY);
    MEMSET(&pStreamingSession->answerSessionDescriptionInit, 0x00, SIZEOF(RtcSessionDescriptionInit));

    CHK_STATUS((deserializeSessionDescriptionInit(pSignalingMessage->payload, pSignalingMessage->payloadLen, pOfferSessionDescriptionInit)));
    CHK_STATUS((setRemoteDescription(pStreamingSession->pPeerConnection, pOfferSessionDescriptionInit)));
    canTrickle = canTrickleIceCandidates(pStreamingSession->pPeerConnection);
    // cannot be null after setRemoteDescription
    CHECK(!NULLABLE_CHECK_EMPTY(canTrickle));
    pStreamingSession->remoteCanTrickleIce = canTrickle.value;
    CHK_STATUS((setLocalDescription(pStreamingSession->pPeerConnection, &pStreamingSession->answerSessionDescriptionInit)));

    // If remote support trickle ice, send answer now. Otherwise answer will be sent once ice candidate gathering is complete.
    if (pStreamingSession->remoteCanTrickleIce) {
        CHK_STATUS((createAnswer(pStreamingSession->pPeerConnection, &pStreamingSession->answerSessionDescriptionInit)));
        CHK_STATUS((respondWithAnswer(pStreamingSession)));
        DLOGD("time taken to send answer %" PRIu64 " ms", (GETTIME() - pStreamingSession->offerReceiveTime) / HUNDREDS_OF_NANOS_IN_A_MILLISECOND);
    }

    mediaThreadStarted = ATOMIC_EXCHANGE_BOOL(&pAppConfiguration->mediaThreadStarted, TRUE);
    if (!mediaThreadStarted) {
        THREAD_CREATE_EX(&pAppConfiguration->mediaSenderTid, "media_control", 4096, mediaSenderRoutine, (PVOID) pAppConfiguration);
    }

CleanUp:
    SAFE_MEMFREE(pOfferSessionDescriptionInit);
    CHK_LOG_ERR((retStatus));

    return retStatus;
}

static VOID onIceCandidateHandler(UINT64 userData, PCHAR candidateJson)
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
        if (getAppSignalingRole(&pStreamingSession->pAppConfiguration->appSignaling) == SIGNALING_CHANNEL_ROLE_TYPE_MASTER &&
            !pStreamingSession->remoteCanTrickleIce) {
            CHK_STATUS((createAnswer(pStreamingSession->pPeerConnection, &pStreamingSession->answerSessionDescriptionInit)));
            CHK_STATUS((respondWithAnswer(pStreamingSession)));
            DLOGD("time taken to send answer %" PRIu64 " ms", (GETTIME() - pStreamingSession->offerReceiveTime) / HUNDREDS_OF_NANOS_IN_A_MILLISECOND);
        }

    } else if (pStreamingSession->remoteCanTrickleIce && ATOMIC_LOAD_BOOL(&pStreamingSession->peerIdReceived)) {
        pMessage->version = SIGNALING_MESSAGE_CURRENT_VERSION;
        pMessage->messageType = SIGNALING_MESSAGE_TYPE_ICE_CANDIDATE;
        STRNCPY(pMessage->peerClientId, pStreamingSession->peerId, MAX_SIGNALING_CLIENT_ID_LEN);
        pMessage->payloadLen = (UINT32) STRNLEN(candidateJson, MAX_SIGNALING_MESSAGE_LEN);
        STRNCPY(pMessage->payload, candidateJson, pMessage->payloadLen);
        pMessage->correlationId[0] = '\0';
        CHK_STATUS((sendAppSignalingMessage(&pStreamingSession->pAppConfiguration->appSignaling, pMessage)));
    }

CleanUp:
    SAFE_MEMFREE(pMessage);
    CHK_LOG_ERR((retStatus));
}

static STATUS getIceCandidatePairStatsCallback(UINT32 timerId, UINT64 currentTime, UINT64 userData)
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
    MUTEX_LOCK(pAppConfiguration->appConfigurationObjLock);
    locked = TRUE;

    for (i = 0; i < pAppConfiguration->streamingSessionCount; ++i) {
        if (STATUS_SUCCEEDED(
                rtcPeerConnectionGetMetrics(pAppConfiguration->streamingSessionList[i]->pPeerConnection, NULL, pRtcMetrics))) {
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

static STATUS onSignalingMessageReceived(UINT64 userData, PReceivedSignalingMessage pReceivedSignalingMessage)
{
    STATUS retStatus = STATUS_SUCCESS;
    PAppConfiguration pAppConfiguration = (PAppConfiguration) userData;
    BOOL peerConnectionFound = FALSE, locked = FALSE, startStats = FALSE;
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
    CHK_STATUS((hashTableContains(pAppConfiguration->pRemoteRtcPeerConnections, clientIdHashKey, &peerConnectionFound)));

    if (peerConnectionFound) {
        CHK_STATUS((hashTableGet(pAppConfiguration->pRemoteRtcPeerConnections, clientIdHashKey, &hashValue)));
        pStreamingSession = (PStreamingSession) hashValue;
    }

    switch (pReceivedSignalingMessage->signalingMessage.messageType) {
        case SIGNALING_MESSAGE_TYPE_OFFER:
            // Check if we already have an ongoing master session with the same peer
            CHK_ERR(!peerConnectionFound, STATUS_INVALID_OPERATION, "Peer connection %s is in progress",
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
                CHK_STATUS((getPendingMsgQByHashVal(pAppConfiguration->pRemotePeerPendingSignalingMessages, clientIdHashKey, TRUE, &pPendingMsgQ)));
                CHK(FALSE, retStatus);
            }
            CHK_STATUS((createStreamingSession(pAppConfiguration, pReceivedSignalingMessage->signalingMessage.peerClientId, &pStreamingSession)));
            pStreamingSession->offerReceiveTime = GETTIME();
            MUTEX_LOCK(pAppConfiguration->streamingSessionListReadLock);
            pAppConfiguration->streamingSessionList[pAppConfiguration->streamingSessionCount++] = pStreamingSession;
            MUTEX_UNLOCK(pAppConfiguration->streamingSessionListReadLock);

            CHK_STATUS((handleOffer(pAppConfiguration, pStreamingSession, &pReceivedSignalingMessage->signalingMessage)));
            CHK_STATUS((hashTablePut(pAppConfiguration->pRemoteRtcPeerConnections, clientIdHashKey, (UINT64) pStreamingSession)));

            // If there are any ice candidate messages in the queue for this client id, submit them now.
            CHK_STATUS((getPendingMsgQByHashVal(pAppConfiguration->pRemotePeerPendingSignalingMessages, clientIdHashKey, TRUE, &pPendingMsgQ)));
            CHK_STATUS((handlePendingMsgQ(pPendingMsgQ, handleRemoteCandidate, pStreamingSession)));

            startStats = pAppConfiguration->iceCandidatePairStatsTimerId == MAX_UINT32;
            break;

        case SIGNALING_MESSAGE_TYPE_ICE_CANDIDATE:
            /*
             * if peer connection hasn't been created, create an queue to store the ice candidate message. Otherwise
             * submit the signaling message into the corresponding streaming session.
             */
            if (!peerConnectionFound) {
                CHK_STATUS((getPendingMsgQByHashVal(pAppConfiguration->pRemotePeerPendingSignalingMessages, clientIdHashKey, FALSE, &pPendingMsgQ)));

                if (pPendingMsgQ == NULL) {
                    CHK_STATUS((createPendingMsgQ(pAppConfiguration->pRemotePeerPendingSignalingMessages, clientIdHashKey, &pPendingMsgQ)));
                }
                CHK_STATUS((pushMsqIntoPendingMsgQ(pPendingMsgQ, pReceivedSignalingMessage)));
            } else {
                CHK_STATUS((handleRemoteCandidate(pStreamingSession, &pReceivedSignalingMessage->signalingMessage)));
            }
            break;

        default:
            DLOGD("Unhandled signaling message type %u", pReceivedSignalingMessage->signalingMessage.messageType);
            break;
    }

    MUTEX_UNLOCK(pAppConfiguration->appConfigurationObjLock);
    locked = FALSE;

    if (startStats &&
        STATUS_FAILED(retStatus = timerQueueAddTimer(pAppConfiguration->timerQueueHandle, APP_STATS_DURATION, APP_STATS_DURATION,
                                                  getIceCandidatePairStatsCallback, (UINT64) pAppConfiguration,
                                                  &pAppConfiguration->iceCandidatePairStatsTimerId))) {
        DLOGW("Failed to add getIceCandidatePairStatsCallback to add to timer queue (code 0x%08x). "
              "Cannot pull ice candidate pair metrics periodically",
              retStatus);

        // Reset the returned status
        retStatus = STATUS_SUCCESS;
    }

CleanUp:

    if (locked) {
        MUTEX_UNLOCK(pAppConfiguration->appConfigurationObjLock);
    }

    CHK_LOG_ERR((retStatus));
    return retStatus;
}

static STATUS pregenerateCertTimerCallback(UINT32 timerId, UINT64 currentTime, UINT64 userData)
{
    UNUSED_PARAM(timerId);
    UNUSED_PARAM(currentTime);
    STATUS retStatus = STATUS_SUCCESS;
    PAppConfiguration pAppConfiguration = (PAppConfiguration) userData;

    CHK_WARN(pAppConfiguration != NULL, STATUS_APP_COMMON_NULL_ARG, "pregenerateCertTimerCallback(): Passed argument is NULL");

    MUTEX_LOCK(pAppConfiguration->appConfigurationObjLock);
    generateCertRoutine(&pAppConfiguration->appCredential);
    MUTEX_UNLOCK(pAppConfiguration->appConfigurationObjLock);

CleanUp:
    return retStatus;
}

PVOID mediaSenderRoutine(PVOID userData)
{
    STATUS retStatus = STATUS_SUCCESS;
    PAppConfiguration pAppConfiguration = (PAppConfiguration) userData;
    TID mediaSourceTid = INVALID_TID_VALUE;

    MUTEX_LOCK(pAppConfiguration->appConfigurationObjLock);
    while (!ATOMIC_LOAD_BOOL(&pAppConfiguration->peerConnectionConnected) && !ATOMIC_LOAD_BOOL(&pAppConfiguration->terminateApp)) {
        CVAR_WAIT(pAppConfiguration->cvar, pAppConfiguration->appConfigurationObjLock, 5 * HUNDREDS_OF_NANOS_IN_A_SECOND);
    }
    MUTEX_UNLOCK(pAppConfiguration->appConfigurationObjLock);

    CHK(!ATOMIC_LOAD_BOOL(&pAppConfiguration->terminateApp), retStatus);

    if (pAppConfiguration->mediaSource != NULL) {
        pAppConfiguration->mediaSource(pAppConfiguration->pMediaContext);
    }

CleanUp:
    // clean the flag of the media thread.
    ATOMIC_STORE_BOOL(&pAppConfiguration->mediaThreadStarted, FALSE);
    CHK_LOG_ERR((retStatus));
    return NULL;
}

static STATUS initializePeerConnection(PAppConfiguration pAppConfiguration, PRtcPeerConnection* ppRtcPeerConnection)
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

    CHK_STATUS((queryAppSignalingServer(&pAppConfiguration->appSignaling, pConfiguration->iceServers, &uriCount)));

    pAppConfiguration->iceUriCount = uriCount + 1;

    CHK_STATUS((popGeneratedCert(&pAppConfiguration->appCredential, &pRtcCertificate)));

    if (pRtcCertificate != NULL) {
        pConfiguration->certificates[0] = *pRtcCertificate;
        pRtcCertificate = NULL;
    }

    curTime = GETTIME();
    CHK_STATUS((createPeerConnection(pConfiguration, ppRtcPeerConnection)));
    DLOGD("time taken to create peer connection %" PRIu64 " ms", (GETTIME() - curTime) / HUNDREDS_OF_NANOS_IN_A_MILLISECOND);

CleanUp:

    CHK_LOG_ERR((retStatus));

    // Free the certificate which can be NULL as we no longer need it and won't reuse
    freeRtcCertificate(pRtcCertificate);
    SAFE_MEMFREE(pConfiguration);
    LEAVES();
    return retStatus;
}

STATUS createStreamingSession(PAppConfiguration pAppConfiguration, PCHAR peerId, PStreamingSession* ppStreamingSession)
{
    STATUS retStatus = STATUS_SUCCESS;
    PRtcMediaStreamTrack pVideoTrack = NULL;
    PRtcMediaStreamTrack pAudioTrack = NULL;
    PStreamingSession pStreamingSession = NULL;
    RtcRtpTransceiverInit audioRtpTransceiverInit;
    RtcRtpTransceiverInit videoRtpTransceiverInit;
    RTC_CODEC codec;

    CHK(NULL != (pVideoTrack = (PRtcMediaStreamTrack) MEMCALLOC(1, SIZEOF(RtcMediaStreamTrack))), STATUS_APP_COMMON_NOT_ENOUGH_MEMORY);
    CHK(NULL != (pAudioTrack = (PRtcMediaStreamTrack) MEMCALLOC(1, SIZEOF(RtcMediaStreamTrack))), STATUS_APP_COMMON_NOT_ENOUGH_MEMORY);

    pStreamingSession = (PStreamingSession) MEMCALLOC(1, SIZEOF(StreamingSession));
    CHK(pStreamingSession != NULL, STATUS_APP_COMMON_NOT_ENOUGH_MEMORY);

    CHK_STATUS((isMediaSourceReady(pAppConfiguration->pMediaContext)));

    STRCPY(pStreamingSession->peerId, peerId);
    ATOMIC_STORE_BOOL(&pStreamingSession->peerIdReceived, TRUE);

    pStreamingSession->pAppConfiguration = pAppConfiguration;
    pStreamingSession->rtcMetricsHistory.prevTs = GETTIME();
    // if we're the viewer, we control the trickle ice mode
    pStreamingSession->remoteCanTrickleIce = FALSE;

    ATOMIC_STORE_BOOL(&pStreamingSession->terminateFlag, FALSE);
    ATOMIC_STORE_BOOL(&pStreamingSession->candidateGatheringDone, FALSE);

    CHK_STATUS((initializePeerConnection(pAppConfiguration, &pStreamingSession->pPeerConnection)));
    CHK_STATUS((peerConnectionOnIceCandidate(pStreamingSession->pPeerConnection, (UINT64) pStreamingSession, onIceCandidateHandler)));
    CHK_STATUS((peerConnectionOnConnectionStateChange(pStreamingSession->pPeerConnection, (UINT64) pStreamingSession, onConnectionStateChange)));
    #ifdef ENABLE_DATA_CHANNEL
    CHK_STATUS((peerConnectionOnDataChannel(pStreamingSession->pPeerConnection, (UINT64) pStreamingSession, onDataChannel)));
    #endif

    // Add a SendRecv Transceiver of type video
    CHK_STATUS((queryMediaVideoCap(pAppConfiguration->pMediaContext, &codec)));
    CHK_STATUS((addSupportedCodec(pStreamingSession->pPeerConnection, codec)));
    pVideoTrack->kind = MEDIA_STREAM_TRACK_KIND_VIDEO;
    pVideoTrack->codec = codec;
    videoRtpTransceiverInit.direction = RTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY;
    STRCPY(pVideoTrack->streamId, APP_VIDEO_TRACK_STREAM_ID);
    STRCPY(pVideoTrack->trackId, APP_VIDEO_TRACK_ID);
    CHK_STATUS(
        (addTransceiver(pStreamingSession->pPeerConnection, pVideoTrack, &videoRtpTransceiverInit, &pStreamingSession->pVideoRtcRtpTransceiver)));

    CHK_STATUS(
        (transceiverOnBandwidthEstimation(pStreamingSession->pVideoRtcRtpTransceiver, (UINT64) pStreamingSession, onBandwidthEstimationHandler)));

    // Add a SendRecv Transceiver of type audio
    //CHK_STATUS((queryMediaAudioCap(pAppConfiguration->pMediaContext, &codec)));
    //CHK_STATUS((addSupportedCodec(pStreamingSession->pPeerConnection, codec)));
    //pAudioTrack->kind = MEDIA_STREAM_TRACK_KIND_AUDIO;
    //pAudioTrack->codec = codec;
    //audioRtpTransceiverInit.direction = RTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY;
    //STRCPY(pAudioTrack->streamId, APP_AUDIO_TRACK_STREAM_ID);
    //STRCPY(pAudioTrack->trackId, APP_AUDIO_TRACK_ID);
    //CHK_STATUS(
    //    (addTransceiver(pStreamingSession->pPeerConnection, pAudioTrack, &audioRtpTransceiverInit, &pStreamingSession->pAudioRtcRtpTransceiver)));

    //CHK_STATUS(
    //    (transceiverOnBandwidthEstimation(pStreamingSession->pAudioRtcRtpTransceiver, (UINT64) pStreamingSession, onBandwidthEstimationHandler)));
    // twcc bandwidth estimation
    //CHK_STATUS((peerConnectionOnSenderBandwidthEstimation(pStreamingSession->pPeerConnection, (UINT64) pStreamingSession,
    //                                                      onSenderBandwidthEstimationHandler)));

CleanUp:

    if (STATUS_FAILED(retStatus) && pStreamingSession != NULL) {
        freeStreamingSession(&pStreamingSession);
        pStreamingSession = NULL;
    }

    *ppStreamingSession = pStreamingSession;
    SAFE_MEMFREE(pVideoTrack);
    SAFE_MEMFREE(pAudioTrack);
    return retStatus;
}

STATUS freeStreamingSession(PStreamingSession* ppStreamingSession)
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
        CHK_LOG_ERR(
            (timerQueueCancelTimer(pAppConfiguration->timerQueueHandle, pAppConfiguration->iceCandidatePairStatsTimerId, (UINT64) pAppConfiguration)));
        pAppConfiguration->iceCandidatePairStatsTimerId = MAX_UINT32;
    }
    MUTEX_UNLOCK(pAppConfiguration->appConfigurationObjLock);

    CHK_LOG_ERR((closePeerConnection(pStreamingSession->pPeerConnection)));
    CHK_LOG_ERR((freePeerConnection(&pStreamingSession->pPeerConnection)));
    MEMFREE(pStreamingSession);

CleanUp:

    CHK_LOG_ERR((retStatus));

    return retStatus;
}

static STATUS gatherIceServerStats(PStreamingSession pStreamingSession)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    UINT32 j = 0;

    for (; j < pStreamingSession->pAppConfiguration->iceUriCount; j++) {
        CHK_STATUS((logIceServerStats(pStreamingSession->pPeerConnection, j)));
    }
CleanUp:
    LEAVES();
    return retStatus;
}

STATUS initApp(BOOL trickleIce, BOOL useTurn, PAppConfiguration* ppAppConfiguration)
{
    STATUS retStatus = STATUS_SUCCESS;
    PAppConfiguration pAppConfiguration = NULL;
    PAppSignaling pAppSignaling = NULL;
    PCHAR pChannel = NULL;

    SET_LOGGER_LOG_LEVEL(getLogLevel());
    //signal(SIGINT, sigIntHandler);

    CHK(ppAppConfiguration != NULL, STATUS_APP_COMMON_NULL_ARG);
    CHK((pChannel = GETENV(APP_WEBRTC_CHANNEL)) != NULL, STATUS_APP_COMMON_CHANNEL_NAME);
    CHK(NULL != (pAppConfiguration = (PAppConfiguration) MEMCALLOC(1, SIZEOF(AppConfiguration))), STATUS_APP_COMMON_NOT_ENOUGH_MEMORY);

    pAppSignaling = &pAppConfiguration->appSignaling;
    pAppSignaling->signalingClientHandle = INVALID_SIGNALING_CLIENT_HANDLE_VALUE;
    pAppConfiguration->mediaSenderTid = INVALID_TID_VALUE;
    pAppConfiguration->timerQueueHandle = INVALID_TIMER_QUEUE_HANDLE_VALUE;
    pAppConfiguration->iceCandidatePairStatsTimerId = MAX_UINT32;
    pAppConfiguration->pregenerateCertTimerId = MAX_UINT32;

    DLOGD("initializing the app with channel(%s)", pChannel);

    setupFileLogging(&pAppConfiguration->enableFileLogging);
    CHK_STATUS((createCredential(&pAppConfiguration->appCredential)));

    pAppConfiguration->appConfigurationObjLock = MUTEX_CREATE(TRUE);
    CHK(IS_VALID_MUTEX_VALUE(pAppConfiguration->appConfigurationObjLock), STATUS_APP_COMMON_INVALID_MUTEX);
    pAppConfiguration->cvar = CVAR_CREATE();
    pAppConfiguration->streamingSessionListReadLock = MUTEX_CREATE(FALSE);
    CHK(IS_VALID_MUTEX_VALUE(pAppConfiguration->streamingSessionListReadLock), STATUS_APP_COMMON_INVALID_MUTEX);
    CHK(timerQueueCreateEx(&pAppConfiguration->timerQueueHandle, SAMPLE_TIMER_NAME, SAMPLE_TIMER_SIZE) == STATUS_SUCCESS, STATUS_APP_COMMON_TIMER);

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
    pAppSignaling->channelInfo.asyncIceServerConfig = FALSE;
    pAppSignaling->channelInfo.retry = TRUE;
    pAppSignaling->channelInfo.reconnect = TRUE;
    pAppSignaling->channelInfo.pCertPath = pAppConfiguration->appCredential.pCaCertPath;
    pAppSignaling->channelInfo.messageTtl = 0; // Default is 60 seconds

    pAppSignaling->clientInfo.version = SIGNALING_CLIENT_INFO_CURRENT_VERSION;
    pAppSignaling->clientInfo.loggingLevel = getLogLevel();
    //pAppSignaling->clientInfo.cacheFilePath = NULL; // Use the default path
    STRCPY(pAppSignaling->clientInfo.clientId, APP_MASTER_CLIENT_ID);
    
    CHK_STATUS((initAppSignaling(pAppSignaling, onSignalingMessageReceived, onSignalingClientStateChanged, onSignalingClientError,
                                 (UINT64) pAppConfiguration, useTurn)));

    ATOMIC_STORE_BOOL(&pAppConfiguration->sigInt, FALSE);
    ATOMIC_STORE_BOOL(&pAppConfiguration->mediaThreadStarted, FALSE);
    ATOMIC_STORE_BOOL(&pAppConfiguration->terminateApp, FALSE);
    ATOMIC_STORE_BOOL(&pAppConfiguration->restartSignalingClient, FALSE);
    ATOMIC_STORE_BOOL(&pAppConfiguration->peerConnectionConnected, FALSE);
    
    pAppConfiguration->iceUriCount = 0;

    CHK_STATUS((createConnectionMsqQ(&pAppConfiguration->pRemotePeerPendingSignalingMessages)));
    CHK_STATUS(
        (hashTableCreateWithParams(APP_HASH_TABLE_BUCKET_COUNT, APP_HASH_TABLE_BUCKET_LENGTH, &pAppConfiguration->pRemoteRtcPeerConnections)));
    
    // the initialization of media source.
    CHK_STATUS((initMediaSource(&pAppConfiguration->pMediaContext)));
    CHK_STATUS((linkMeidaSinkHook(pAppConfiguration->pMediaContext, onMediaSinkHook, pAppConfiguration)));
    CHK_STATUS((linkMeidaEosHook(pAppConfiguration->pMediaContext, onMediaEosHook, pAppConfiguration)));
    pAppConfiguration->mediaSource = runMediaSource;
    DLOGD("The intialization of the media source is completed successfully");

    // Initalize KVS WebRTC. This must be done before anything else, and must only be done once.
    CHK_STATUS((initWebRtc(pAppConfiguration)));
    DLOGD("The initialization of WebRTC  is completed successfully");
    gAppConfiguration = pAppConfiguration;

    // Start the cert pre-gen timer callback
    if (APP_PRE_GENERATE_CERT) {
        CHK_LOG_ERR((retStatus = timerQueueAddTimer(pAppConfiguration->timerQueueHandle, 0, APP_PRE_GENERATE_CERT_PERIOD, pregenerateCertTimerCallback,
                                                 (UINT64) pAppConfiguration, &pAppConfiguration->pregenerateCertTimerId)));
    }
    
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
    retStatus = connectAppSignaling(&pAppConfiguration->appSignaling);
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
    UINT32 i;
    BOOL locked = FALSE;

    CHK(ppAppConfiguration != NULL, STATUS_APP_COMMON_NULL_ARG);
    pAppConfiguration = *ppAppConfiguration;
    CHK(pAppConfiguration != NULL, STATUS_APP_COMMON_NULL_ARG);

    // Kick of the termination sequence
    ATOMIC_STORE_BOOL(&pAppConfiguration->terminateApp, TRUE);

    if (pAppConfiguration->mediaSenderTid != INVALID_TID_VALUE) {
        THREAD_JOIN(pAppConfiguration->mediaSenderTid, NULL);
    }

    freeAppSignaling(&pAppConfiguration->appSignaling);
    freeConnectionMsgQ(&pAppConfiguration->pRemotePeerPendingSignalingMessages);

    hashTableClear(pAppConfiguration->pRemoteRtcPeerConnections);
    hashTableFree(pAppConfiguration->pRemoteRtcPeerConnections);

    if (IS_VALID_MUTEX_VALUE(pAppConfiguration->appConfigurationObjLock)) {
        MUTEX_LOCK(pAppConfiguration->appConfigurationObjLock);
        locked = TRUE;
    }

    for (i = 0; i < pAppConfiguration->streamingSessionCount; ++i) {
        retStatus = gatherIceServerStats(pAppConfiguration->streamingSessionList[i]);
        if (STATUS_FAILED(retStatus)) {
            DLOGW("Failed to ICE Server Stats for streaming session %d: %08x", i, retStatus);
        }
        freeStreamingSession(&pAppConfiguration->streamingSessionList[i]);
    }

    if (locked) {
        MUTEX_UNLOCK(pAppConfiguration->appConfigurationObjLock);
    }
    deinitWebRtc(pAppConfiguration);
    detroyMediaSource(&pAppConfiguration->pMediaContext);

    if (IS_VALID_CVAR_VALUE(pAppConfiguration->cvar) && IS_VALID_MUTEX_VALUE(pAppConfiguration->appConfigurationObjLock)) {
        CVAR_BROADCAST(pAppConfiguration->cvar);
        MUTEX_LOCK(pAppConfiguration->appConfigurationObjLock);
        MUTEX_UNLOCK(pAppConfiguration->appConfigurationObjLock);
    }

    if (IS_VALID_MUTEX_VALUE(pAppConfiguration->appConfigurationObjLock)) {
        MUTEX_FREE(pAppConfiguration->appConfigurationObjLock);
    }

    if (IS_VALID_MUTEX_VALUE(pAppConfiguration->streamingSessionListReadLock)) {
        MUTEX_FREE(pAppConfiguration->streamingSessionListReadLock);
    }

    if (IS_VALID_CVAR_VALUE(pAppConfiguration->cvar)) {
        CVAR_FREE(pAppConfiguration->cvar);
    }

    if (IS_VALID_TIMER_QUEUE_HANDLE(pAppConfiguration->timerQueueHandle)) {
        if (pAppConfiguration->iceCandidatePairStatsTimerId != MAX_UINT32) {
            retStatus =
                timerQueueCancelTimer(pAppConfiguration->timerQueueHandle, pAppConfiguration->iceCandidatePairStatsTimerId, (UINT64) pAppConfiguration);
            if (STATUS_FAILED(retStatus)) {
                DLOGE("Failed to cancel stats timer with: 0x%08x", retStatus);
            }
            pAppConfiguration->iceCandidatePairStatsTimerId = MAX_UINT32;
        }

        if (pAppConfiguration->pregenerateCertTimerId != MAX_UINT32) {
            retStatus =
                timerQueueCancelTimer(pAppConfiguration->timerQueueHandle, pAppConfiguration->pregenerateCertTimerId, (UINT64) pAppConfiguration);
            if (STATUS_FAILED(retStatus)) {
                DLOGE("Failed to cancel certificate pre-generation timer with: 0x%08x", retStatus);
            }
            pAppConfiguration->pregenerateCertTimerId = MAX_UINT32;
        }

        timerQueueFree(&pAppConfiguration->timerQueueHandle);
    }

    destroyCredential(&pAppConfiguration->appCredential);

    if (pAppConfiguration->enableFileLogging) {
        closeFileLogging();
    }

    MEMFREE(*ppAppConfiguration);
    *ppAppConfiguration = NULL;

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

                // swap with last element and decrement count
                pAppConfiguration->streamingSessionCount--;
                if (pAppConfiguration->streamingSessionCount > 0) {
                    pAppConfiguration->streamingSessionList[i] = pAppConfiguration->streamingSessionList[pAppConfiguration->streamingSessionCount];
                    i--;
                }

                // Remove from the hash table
                clientIdHashKey = COMPUTE_CRC32((PBYTE) pStreamingSession->peerId, (UINT32) STRLEN(pStreamingSession->peerId));
                CHK_STATUS((hashTableContains(pAppConfiguration->pRemoteRtcPeerConnections, clientIdHashKey, &peerConnectionFound)));
                if (peerConnectionFound) {
                    CHK_STATUS((hashTableRemove(pAppConfiguration->pRemoteRtcPeerConnections, clientIdHashKey)));
                }
                if (pAppConfiguration->streamingSessionCount == 0) {
                    shutdownMediaSource(pAppConfiguration->pMediaContext);
                }
                MUTEX_UNLOCK(pAppConfiguration->streamingSessionListReadLock);

                freeStreamingSession(&pStreamingSession);
            }
        }

        // Check if we need to re-create the signaling client on-the-fly
        if (ATOMIC_LOAD_BOOL(&pAppConfiguration->restartSignalingClient) && STATUS_SUCCEEDED(restartAppSignaling(&pAppConfiguration->appSignaling))) {
            // Re-set the variable again
            ATOMIC_STORE_BOOL(&pAppConfiguration->restartSignalingClient, FALSE);
        }
        CHK_STATUS((checkAppSignaling(&pAppConfiguration->appSignaling)));

        // Check if any lingering pending message queues
        CHK_STATUS((removeExpiredPendingMsgQ(pAppConfiguration->pRemotePeerPendingSignalingMessages, APP_PENDING_MESSAGE_CLEANUP_DURATION)));
        // periodically wake up and clean up terminated streaming session
        CVAR_WAIT(pAppConfiguration->cvar, pAppConfiguration->appConfigurationObjLock, APP_CLEANUP_WAIT_PERIOD);
        MUTEX_UNLOCK(pAppConfiguration->appConfigurationObjLock);
        locked = FALSE;
    }

CleanUp:

    CHK_LOG_ERR((retStatus));

    if (locked) {
        MUTEX_UNLOCK(pAppConfiguration->appConfigurationObjLock);
    }

    LEAVES();
    return retStatus;
}
