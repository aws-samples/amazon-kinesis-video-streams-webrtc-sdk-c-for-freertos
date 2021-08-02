#include "platform_opts.h"
#if CONFIG_EXAMPLE_KVS_WEBRTC

#define LOG_CLASS "WebRtcSamples"
#include "Samples.h"
#include "fatfs_wrap.h"
#include "example_kvs_webrtc.h"

PSampleConfiguration gSampleConfiguration = NULL;

VOID sigintHandler(INT32 sigNum)
{
    UNUSED_PARAM(sigNum);
    if (gSampleConfiguration != NULL) {
        ATOMIC_STORE_BOOL(&gSampleConfiguration->interrupted, TRUE);
        CVAR_BROADCAST(gSampleConfiguration->cvar);
    }
}
#ifdef ENABLE_DATA_CHANNEL
VOID onDataChannelMessageMaster(UINT64 customData, PRtcDataChannel pDataChannel, BOOL isBinary, PBYTE pMessage, UINT32 pMessageLen)
{
    UNUSED_PARAM(customData);
    UNUSED_PARAM(pDataChannel);
    if (isBinary) {
        DLOGI("DataChannel Binary Message");
    } else {
        DLOGI("xDataChannel String Message: %.*s\n", pMessageLen, pMessage);
    }
}

VOID onDataChannelMessage(UINT64 customData, PRtcDataChannel pDataChannel, BOOL isBinary, PBYTE pMessage, UINT32 pMessageLen)
{
    PSampleStreamingSession session = (PSampleStreamingSession)customData;
    UNUSED_PARAM(pDataChannel);
    if (isBinary) {
        DLOGI("DataChannel Binary Message");
    } else {
        DLOGI("DataChannel String Message: %.*s\n", pMessageLen, pMessage);
        // master echo
        dataChannelSend(session->pRtcDataChannel, isBinary, pMessage, pMessageLen);
    }
}

VOID onDataChannel(UINT64 customData, PRtcDataChannel pRtcDataChannel)
{
    DLOGI("New DataChannel has been opened %s \n", pRtcDataChannel->name);
    dataChannelOnMessage(pRtcDataChannel, customData, onDataChannelMessage);
}
#endif
VOID onConnectionStateChange(UINT64 customData, RTC_PEER_CONNECTION_STATE newState)
{
    STATUS retStatus = STATUS_SUCCESS;
    PSampleStreamingSession pSampleStreamingSession = (PSampleStreamingSession) customData;

    CHK(pSampleStreamingSession != NULL && pSampleStreamingSession->pSampleConfiguration != NULL, STATUS_INTERNAL_ERROR);

    PSampleConfiguration pSampleConfiguration = pSampleStreamingSession->pSampleConfiguration;
    DLOGI("New connection state %u", newState);

    switch (newState) {
        case RTC_PEER_CONNECTION_STATE_CONNECTED:
            ATOMIC_STORE_BOOL(&pSampleConfiguration->connected, TRUE);
            CVAR_BROADCAST(pSampleConfiguration->cvar);
            if (STATUS_FAILED(retStatus = logSelectedIceCandidatesInformation(pSampleStreamingSession))) {
                DLOGW("Failed to get information about selected Ice candidates: 0x%08x", retStatus);
            }
            break;
        case RTC_PEER_CONNECTION_STATE_FAILED:
            // explicit fallthrough
        case RTC_PEER_CONNECTION_STATE_CLOSED:
            // explicit fallthrough
        case RTC_PEER_CONNECTION_STATE_DISCONNECTED:
            ATOMIC_STORE_BOOL(&pSampleStreamingSession->terminateFlag, TRUE);
            CVAR_BROADCAST(pSampleConfiguration->cvar);
            // explicit fallthrough
        default:
            ATOMIC_STORE_BOOL(&pSampleConfiguration->connected, FALSE);
            CVAR_BROADCAST(pSampleConfiguration->cvar);
            break;
    }
    
    CleanUp:

    CHK_LOG_ERR(retStatus);
}

STATUS signalingClientStateChanged(UINT64 customData, SIGNALING_CLIENT_STATE state)
{
    UNUSED_PARAM(customData);
    STATUS retStatus = STATUS_SUCCESS;
    PCHAR pStateStr;

    signalingClientGetStateString(state, &pStateStr);

    DLOGV("Signaling client state changed to %d - '%s'", state, pStateStr);

    // Return success to continue
    return retStatus;
}

PVOID mediaSenderRoutine(PVOID customData)
{
    STATUS retStatus = STATUS_SUCCESS;
    PSampleConfiguration pSampleConfiguration = (PSampleConfiguration) customData;
    TID videoSenderTid = INVALID_TID_VALUE, audioSenderTid = INVALID_TID_VALUE;

    MUTEX_LOCK(pSampleConfiguration->sampleConfigurationObjLock);
    while (!ATOMIC_LOAD_BOOL(&pSampleConfiguration->connected) && !ATOMIC_LOAD_BOOL(&pSampleConfiguration->appTerminateFlag)) {
        CVAR_WAIT(pSampleConfiguration->cvar, pSampleConfiguration->sampleConfigurationObjLock, 5 * HUNDREDS_OF_NANOS_IN_A_SECOND);
    }
    MUTEX_UNLOCK(pSampleConfiguration->sampleConfigurationObjLock);

    CHK(!ATOMIC_LOAD_BOOL(&pSampleConfiguration->appTerminateFlag), retStatus);

    if (pSampleConfiguration->videoSource != NULL) {
        THREAD_CREATE_EX(&videoSenderTid, SAMPLE_VIDEO_THREAD_NAME, SAMPLE_VIDEO_THREAD_SIZE, pSampleConfiguration->videoSource, (PVOID) pSampleConfiguration);
    }

    if (pSampleConfiguration->audioSource != NULL) {
        THREAD_CREATE_EX(&audioSenderTid, SAMPLE_AUDIO_THREAD_NAME, SAMPLE_AUDIO_THREAD_SIZE, pSampleConfiguration->audioSource, (PVOID) pSampleConfiguration);
    }

    if (videoSenderTid != INVALID_TID_VALUE) {
        THREAD_JOIN(videoSenderTid, NULL);
    }

    if (audioSenderTid != INVALID_TID_VALUE) {
        THREAD_JOIN(audioSenderTid, NULL);
    }

CleanUp:

    CHK_LOG_ERR(retStatus);
    return NULL;
}

STATUS signalingClientError(UINT64 customData, STATUS status, PCHAR msg, UINT32 msgLen)
{
    PSampleConfiguration pSampleConfiguration = (PSampleConfiguration) customData;

    DLOGW("Signaling client generated an error 0x%08x - '%.*s'", status, msgLen, msg);

    // We will force re-create the signaling client on the following errors
    if (status == STATUS_SIGNALING_ICE_CONFIG_REFRESH_FAILED || status == STATUS_SIGNALING_RECONNECT_FAILED) {
        ATOMIC_STORE_BOOL(&pSampleConfiguration->recreateSignalingClient, TRUE);
        CVAR_BROADCAST(pSampleConfiguration->cvar);
    }

    return STATUS_SUCCESS;
}

STATUS logSelectedIceCandidatesInformation(PSampleStreamingSession pSampleStreamingSession)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PRtcStats pRtcMetrics = NULL;

    CHK(NULL != (pRtcMetrics = (PRtcStats) MEMALLOC(SIZEOF(RtcStats))), STATUS_NOT_ENOUGH_MEMORY);

    CHK(pSampleStreamingSession != NULL, STATUS_NULL_ARG);
    pRtcMetrics->requestedTypeOfStats = RTC_STATS_TYPE_LOCAL_CANDIDATE;
    CHK_STATUS(rtcPeerConnectionGetMetrics(pSampleStreamingSession->pPeerConnection, NULL, pRtcMetrics));
    DLOGD("Local Candidate IP Address: %s", pRtcMetrics->rtcStatsObject.localIceCandidateStats.address);
    DLOGD("Local Candidate type: %s", pRtcMetrics->rtcStatsObject.localIceCandidateStats.candidateType);
    DLOGD("Local Candidate port: %d", pRtcMetrics->rtcStatsObject.localIceCandidateStats.port);
    DLOGD("Local Candidate priority: %d", pRtcMetrics->rtcStatsObject.localIceCandidateStats.priority);
    DLOGD("Local Candidate transport protocol: %s", pRtcMetrics->rtcStatsObject.localIceCandidateStats.protocol);
    DLOGD("Local Candidate relay protocol: %s", pRtcMetrics->rtcStatsObject.localIceCandidateStats.relayProtocol);
    DLOGD("Local Candidate Ice server source: %s", pRtcMetrics->rtcStatsObject.localIceCandidateStats.url);

    pRtcMetrics->requestedTypeOfStats = RTC_STATS_TYPE_REMOTE_CANDIDATE;
    CHK_STATUS(rtcPeerConnectionGetMetrics(pSampleStreamingSession->pPeerConnection, NULL, pRtcMetrics));
    DLOGD("Remote Candidate IP Address: %s", pRtcMetrics->rtcStatsObject.remoteIceCandidateStats.address);
    DLOGD("Remote Candidate type: %s", pRtcMetrics->rtcStatsObject.localIceCandidateStats.candidateType);
    DLOGD("Remote Candidate port: %d", pRtcMetrics->rtcStatsObject.remoteIceCandidateStats.port);
    DLOGD("Remote Candidate priority: %d", pRtcMetrics->rtcStatsObject.remoteIceCandidateStats.priority);
    DLOGD("Remote Candidate transport protocol: %s", pRtcMetrics->rtcStatsObject.remoteIceCandidateStats.protocol);
CleanUp:
    SAFE_MEMFREE(pRtcMetrics);
    LEAVES();
    return retStatus;
}

STATUS handleAnswer(PSampleConfiguration pSampleConfiguration, PSampleStreamingSession pSampleStreamingSession, PSignalingMessage pSignalingMessage)
{
    UNUSED_PARAM(pSampleConfiguration);
    STATUS retStatus = STATUS_SUCCESS;
    PRtcSessionDescriptionInit pAnswerSessionDescriptionInit = NULL;

    CHK(NULL != (pAnswerSessionDescriptionInit = (PRtcSessionDescriptionInit) MEMALLOC(SIZEOF(RtcSessionDescriptionInit))), STATUS_NOT_ENOUGH_MEMORY);

    MEMSET(pAnswerSessionDescriptionInit, 0x00, SIZEOF(RtcSessionDescriptionInit));

    CHK_STATUS(deserializeSessionDescriptionInit(pSignalingMessage->payload, pSignalingMessage->payloadLen, pAnswerSessionDescriptionInit));
    CHK_STATUS(setRemoteDescription(pSampleStreamingSession->pPeerConnection, pAnswerSessionDescriptionInit));

CleanUp:

    CHK_LOG_ERR(retStatus);
    SAFE_MEMFREE(pAnswerSessionDescriptionInit);
    return retStatus;
}

STATUS handleOffer(PSampleConfiguration pSampleConfiguration, PSampleStreamingSession pSampleStreamingSession, PSignalingMessage pSignalingMessage)
{
    STATUS retStatus = STATUS_SUCCESS;
    PRtcSessionDescriptionInit pOfferSessionDescriptionInit = NULL;
    NullableBool canTrickle;

    CHK(pSampleConfiguration != NULL && pSignalingMessage != NULL, STATUS_NULL_ARG);
    
    CHK(NULL != (pOfferSessionDescriptionInit = (PRtcSessionDescriptionInit) MEMALLOC(SIZEOF(RtcSessionDescriptionInit))), STATUS_NOT_ENOUGH_MEMORY);

    MEMSET(pOfferSessionDescriptionInit, 0x00, SIZEOF(RtcSessionDescriptionInit));
    MEMSET(&pSampleStreamingSession->answerSessionDescriptionInit, 0x00, SIZEOF(RtcSessionDescriptionInit));

    CHK_STATUS(deserializeSessionDescriptionInit(pSignalingMessage->payload, pSignalingMessage->payloadLen, pOfferSessionDescriptionInit));
    CHK_STATUS(setRemoteDescription(pSampleStreamingSession->pPeerConnection, pOfferSessionDescriptionInit));
    canTrickle = canTrickleIceCandidates(pSampleStreamingSession->pPeerConnection);
    /* cannot be null after setRemoteDescription */
    CHECK(!NULLABLE_CHECK_EMPTY(canTrickle));
    pSampleStreamingSession->remoteCanTrickleIce = canTrickle.value;
    CHK_STATUS(setLocalDescription(pSampleStreamingSession->pPeerConnection, &pSampleStreamingSession->answerSessionDescriptionInit));

    /*
     * If remote support trickle ice, send answer now. Otherwise answer will be sent once ice candidate gathering is complete.
     */
    if (pSampleStreamingSession->remoteCanTrickleIce) {
        CHK_STATUS(createAnswer(pSampleStreamingSession->pPeerConnection, &pSampleStreamingSession->answerSessionDescriptionInit));
        CHK_STATUS(respondWithAnswer(pSampleStreamingSession));
        DLOGD("time taken to send answer %" PRIu64 " ms",
              (GETTIME() - pSampleStreamingSession->offerReceiveTime) / HUNDREDS_OF_NANOS_IN_A_MILLISECOND);
    }

    if (!ATOMIC_LOAD_BOOL(&pSampleConfiguration->mediaThreadStarted)) {
        ATOMIC_STORE_BOOL(&pSampleConfiguration->mediaThreadStarted, TRUE);
        THREAD_CREATE_EX(&pSampleConfiguration->mediaSenderTid, "media_control", 4096, mediaSenderRoutine, (PVOID) pSampleConfiguration);

        if ((retStatus = timerQueueAddTimer(pSampleConfiguration->timerQueueHandle, SAMPLE_STATS_DURATION, SAMPLE_STATS_DURATION,
                                            getIceCandidatePairStatsCallback, (UINT64) pSampleConfiguration,
                                            &pSampleConfiguration->iceCandidatePairStatsTimerId)) != STATUS_SUCCESS) {
            DLOGW("Failed to add getIceCandidatePairStatsCallback to add to timer queue (code 0x%08x). Cannot pull ice candidate pair metrics "
                  "periodically",
                  retStatus);
        }
    }

    // The audio video receive routine should be per streaming session
    if (pSampleConfiguration->receiveAudioVideoSource != NULL) {
        THREAD_CREATE(&pSampleStreamingSession->receiveAudioVideoSenderTid, pSampleConfiguration->receiveAudioVideoSource,
                      (PVOID) pSampleStreamingSession);
    }
CleanUp:
    SAFE_MEMFREE(pOfferSessionDescriptionInit);
    CHK_LOG_ERR(retStatus);

    return retStatus;
}

STATUS sendSignalingMessage(PSampleStreamingSession pSampleStreamingSession, PSignalingMessage pMessage)
{
    STATUS retStatus = STATUS_SUCCESS;
    BOOL locked = FALSE;

    // Validate the input params
    CHK(pSampleStreamingSession != NULL && pSampleStreamingSession->pSampleConfiguration != NULL && pMessage != NULL, STATUS_NULL_ARG);
    CHK(IS_VALID_MUTEX_VALUE(pSampleStreamingSession->pSampleConfiguration->signalingSendMessageLock) &&
            IS_VALID_SIGNALING_CLIENT_HANDLE(pSampleStreamingSession->pSampleConfiguration->signalingClientHandle),
        STATUS_INVALID_OPERATION);

    MUTEX_LOCK(pSampleStreamingSession->pSampleConfiguration->signalingSendMessageLock);
    locked = TRUE;
    CHK_STATUS(signalingClientSendMessage(pSampleStreamingSession->pSampleConfiguration->signalingClientHandle, pMessage));

CleanUp:

    if (locked) {
        MUTEX_UNLOCK(pSampleStreamingSession->pSampleConfiguration->signalingSendMessageLock);
    }

    CHK_LOG_ERR(retStatus);
    return retStatus;
}

STATUS respondWithAnswer(PSampleStreamingSession pSampleStreamingSession)
{
    STATUS retStatus = STATUS_SUCCESS;
    PSignalingMessage pMessage = NULL;
    UINT32 buffLen = MAX_SIGNALING_MESSAGE_LEN;

    CHK(NULL != (pMessage = (PSignalingMessage) MEMALLOC(SIZEOF(SignalingMessage))), STATUS_NOT_ENOUGH_MEMORY);

    CHK_STATUS(serializeSessionDescriptionInit(&pSampleStreamingSession->answerSessionDescriptionInit, pMessage->payload, &buffLen));

    pMessage->version = SIGNALING_MESSAGE_CURRENT_VERSION;
    pMessage->messageType = SIGNALING_MESSAGE_TYPE_ANSWER;
    STRNCPY(pMessage->peerClientId, pSampleStreamingSession->peerId, MAX_SIGNALING_CLIENT_ID_LEN);
    pMessage->payloadLen = (UINT32) STRLEN(pMessage->payload);
    pMessage->correlationId[0] = '\0';

    CHK_STATUS(sendSignalingMessage(pSampleStreamingSession, pMessage));

CleanUp:
    SAFE_MEMFREE(pMessage);
    CHK_LOG_ERR(retStatus);
    return retStatus;
}

VOID onIceCandidateHandler(UINT64 customData, PCHAR candidateJson)
{
    STATUS retStatus = STATUS_SUCCESS;
    PSampleStreamingSession pSampleStreamingSession = (PSampleStreamingSession) customData;
    PSignalingMessage pMessage = NULL;

    CHK(pSampleStreamingSession != NULL, STATUS_NULL_ARG);
    CHK(NULL != (pMessage = (PSignalingMessage) MEMALLOC(SIZEOF(SignalingMessage))), STATUS_NOT_ENOUGH_MEMORY);

    if (candidateJson == NULL) {
        DLOGD("ice candidate gathering finished");
        ATOMIC_STORE_BOOL(&pSampleStreamingSession->candidateGatheringDone, TRUE);

        // if application is master and non-trickle ice, send answer now.
        if (pSampleStreamingSession->pSampleConfiguration->channelInfo.channelRoleType == SIGNALING_CHANNEL_ROLE_TYPE_MASTER &&
            !pSampleStreamingSession->remoteCanTrickleIce) {
            CHK_STATUS(createAnswer(pSampleStreamingSession->pPeerConnection, &pSampleStreamingSession->answerSessionDescriptionInit));
            CHK_STATUS(respondWithAnswer(pSampleStreamingSession));
            DLOGD("time taken to send answer %" PRIu64 " ms",
                  (GETTIME() - pSampleStreamingSession->offerReceiveTime) / HUNDREDS_OF_NANOS_IN_A_MILLISECOND);
        } else if (pSampleStreamingSession->pSampleConfiguration->channelInfo.channelRoleType == SIGNALING_CHANNEL_ROLE_TYPE_VIEWER &&
                   !pSampleStreamingSession->pSampleConfiguration->trickleIce) {
            CVAR_BROADCAST(pSampleStreamingSession->pSampleConfiguration->cvar);
        }

    } else if (pSampleStreamingSession->remoteCanTrickleIce && ATOMIC_LOAD_BOOL(&pSampleStreamingSession->peerIdReceived)) {
        pMessage->version = SIGNALING_MESSAGE_CURRENT_VERSION;
        pMessage->messageType = SIGNALING_MESSAGE_TYPE_ICE_CANDIDATE;
        STRNCPY(pMessage->peerClientId, pSampleStreamingSession->peerId, MAX_SIGNALING_CLIENT_ID_LEN);
        pMessage->payloadLen = (UINT32) STRLEN(candidateJson);
        STRNCPY(pMessage->payload, candidateJson, pMessage->payloadLen);
        pMessage->correlationId[0] = '\0';
        CHK_STATUS(sendSignalingMessage(pSampleStreamingSession, pMessage));
    }

CleanUp:
    SAFE_MEMFREE(pMessage);
    CHK_LOG_ERR(retStatus);
}

STATUS initializePeerConnection(PSampleConfiguration pSampleConfiguration, PRtcPeerConnection* ppRtcPeerConnection)
{
    STATUS retStatus = STATUS_SUCCESS;
    PRtcConfiguration pConfiguration = NULL;
    UINT32 i, j, iceConfigCount, uriCount;
    PIceConfigInfo pIceConfigInfo;
    const UINT32 maxTurnServer = 1;
    uriCount = 0;

    CHK(pSampleConfiguration != NULL && ppRtcPeerConnection != NULL, STATUS_NULL_ARG);

    CHK(NULL != (pConfiguration = (PRtcConfiguration) MEMALLOC(SIZEOF(RtcConfiguration))), STATUS_NOT_ENOUGH_MEMORY);
    MEMSET(pConfiguration, 0x00, SIZEOF(RtcConfiguration));

    // Set this to custom callback to enable filtering of interfaces
    pConfiguration->kvsRtcConfiguration.iceSetInterfaceFilterFunc = NULL;

    // Set the  STUN server
    SNPRINTF(pConfiguration->iceServers[0].urls, MAX_ICE_CONFIG_URI_LEN, KINESIS_VIDEO_STUN_URL, pSampleConfiguration->channelInfo.pRegion);

    if (pSampleConfiguration->useTurn) {
        // Set the URIs from the configuration
        CHK_STATUS(awaitGetIceConfigInfoCount(pSampleConfiguration->signalingClientHandle, &iceConfigCount));

        /* signalingClientGetIceConfigInfoCount can return more than one turn server. Use only one to optimize
         * candidate gathering latency. But user can also choose to use more than 1 turn server. */
        for (uriCount = 0, i = 0; i < maxTurnServer; i++) {
            CHK_STATUS(signalingClientGetIceConfigInfo(pSampleConfiguration->signalingClientHandle, i, &pIceConfigInfo));
            for (j = 0; j < pIceConfigInfo->uriCount; j++) {
                CHECK(uriCount < MAX_ICE_SERVERS_COUNT);
                /*
                 * if pConfiguration->iceServers[uriCount + 1].urls is "turn:ip:port?transport=udp" then ICE will try TURN over UDP
                 * if pConfiguration->iceServers[uriCount + 1].urls is "turn:ip:port?transport=tcp" then ICE will try TURN over TCP/TLS
                 * if pConfiguration->iceServers[uriCount + 1].urls is "turns:ip:port?transport=udp", it's currently ignored because sdk dont do TURN
                 * over DTLS yet. if pConfiguration->iceServers[uriCount + 1].urls is "turns:ip:port?transport=tcp" then ICE will try TURN over TCP/TLS
                 * if pConfiguration->iceServers[uriCount + 1].urls is "turn:ip:port" then ICE will try both TURN over UPD and TCP/TLS
                 *
                 * It's recommended to not pass too many TURN iceServers to configuration because it will slow down ice gathering in non-trickle mode.
                 */

                STRNCPY(pConfiguration->iceServers[uriCount + 1].urls, pIceConfigInfo->uris[j], MAX_ICE_CONFIG_URI_LEN);
                STRNCPY(pConfiguration->iceServers[uriCount + 1].credential, pIceConfigInfo->password, MAX_ICE_CONFIG_CREDENTIAL_LEN);
                STRNCPY(pConfiguration->iceServers[uriCount + 1].username, pIceConfigInfo->userName, MAX_ICE_CONFIG_USER_NAME_LEN);

                uriCount++;
            }
        }
    }
    pSampleConfiguration->iceUriCount = uriCount + 1;
    CHK_STATUS(createPeerConnection(pConfiguration, ppRtcPeerConnection));

CleanUp:
    SAFE_MEMFREE(pConfiguration);
    return retStatus;
}

// Return ICE server stats for a specific streaming session
STATUS gatherIceServerStats(PSampleStreamingSession pSampleStreamingSession)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PRtcStats pRtcMetrics = NULL;
    UINT32 j = 0;

    CHK(NULL != (pRtcMetrics = (PRtcStats) MEMALLOC(SIZEOF(RtcStats))), STATUS_NOT_ENOUGH_MEMORY);

    pRtcMetrics->requestedTypeOfStats = RTC_STATS_TYPE_ICE_SERVER;
    for (; j < pSampleStreamingSession->pSampleConfiguration->iceUriCount; j++) {
        pRtcMetrics->rtcStatsObject.iceServerStats.iceServerIndex = j;
        CHK_STATUS(rtcPeerConnectionGetMetrics(pSampleStreamingSession->pPeerConnection, NULL, pRtcMetrics));
        DLOGD("ICE Server URL: %s", pRtcMetrics->rtcStatsObject.iceServerStats.url);
        DLOGD("ICE Server port: %d", pRtcMetrics->rtcStatsObject.iceServerStats.port);
        DLOGD("ICE Server protocol: %s", pRtcMetrics->rtcStatsObject.iceServerStats.protocol);
        DLOGD("Total requests sent:%" PRIu64, pRtcMetrics->rtcStatsObject.iceServerStats.totalRequestsSent);
        DLOGD("Total responses received: %" PRIu64, pRtcMetrics->rtcStatsObject.iceServerStats.totalResponsesReceived);
        DLOGD("Total round trip time: %" PRIu64 "ms",
              pRtcMetrics->rtcStatsObject.iceServerStats.totalRoundTripTime / HUNDREDS_OF_NANOS_IN_A_MILLISECOND);
    }
CleanUp:
    SAFE_MEMFREE(pRtcMetrics);
    LEAVES();
    return retStatus;
}

STATUS awaitGetIceConfigInfoCount(SIGNALING_CLIENT_HANDLE signalingClientHandle, PUINT32 pIceConfigInfoCount)
{
    STATUS retStatus = STATUS_SUCCESS;
    UINT64 elapsed = 0;

    CHK(IS_VALID_SIGNALING_CLIENT_HANDLE(signalingClientHandle) && pIceConfigInfoCount != NULL, STATUS_NULL_ARG);

    while (TRUE) {
        // Get the configuration count
        CHK_STATUS(signalingClientGetIceConfigInfoCount(signalingClientHandle, pIceConfigInfoCount));

        // Return OK if we have some ice configs
        CHK(*pIceConfigInfoCount == 0, retStatus);

        // Check for timeout
        CHK_ERR(elapsed <= ASYNC_ICE_CONFIG_INFO_WAIT_TIMEOUT, STATUS_OPERATION_TIMED_OUT, "Couldn't retrieve ICE configurations in allotted time.");

        THREAD_SLEEP(ICE_CONFIG_INFO_POLL_PERIOD);
        elapsed += ICE_CONFIG_INFO_POLL_PERIOD;
    }

CleanUp:

    return retStatus;
}

STATUS createSampleStreamingSession(PSampleConfiguration pSampleConfiguration, PCHAR peerId, BOOL isMaster,
                                    PSampleStreamingSession* ppSampleStreamingSession)
{
    STATUS retStatus = STATUS_SUCCESS;
    PRtcMediaStreamTrack pVideoTrack = NULL;
    PRtcMediaStreamTrack pAudioTrack = NULL;
    PSampleStreamingSession pSampleStreamingSession = NULL;

    CHK(NULL != (pVideoTrack = (PRtcMediaStreamTrack) MEMALLOC(SIZEOF(RtcMediaStreamTrack))), STATUS_NOT_ENOUGH_MEMORY);
    CHK(NULL != (pAudioTrack = (PRtcMediaStreamTrack) MEMALLOC(SIZEOF(RtcMediaStreamTrack))), STATUS_NOT_ENOUGH_MEMORY);
    MEMSET(pVideoTrack, 0x00, SIZEOF(RtcMediaStreamTrack));
    MEMSET(pAudioTrack, 0x00, SIZEOF(RtcMediaStreamTrack));

    CHK(pSampleConfiguration != NULL && ppSampleStreamingSession != NULL, STATUS_NULL_ARG);
    CHK((isMaster && peerId != NULL) || !isMaster, STATUS_INVALID_ARG);

    pSampleStreamingSession = (PSampleStreamingSession) MEMCALLOC(1, SIZEOF(SampleStreamingSession));
    CHK(pSampleStreamingSession != NULL, STATUS_NOT_ENOUGH_MEMORY);
    
    if (isMaster) {
        STRCPY(pSampleStreamingSession->peerId, peerId);
    } else {
        STRCPY(pSampleStreamingSession->peerId, SAMPLE_VIEWER_CLIENT_ID);
    }
    ATOMIC_STORE_BOOL(&pSampleStreamingSession->peerIdReceived, TRUE);

    pSampleStreamingSession->pSampleConfiguration = pSampleConfiguration;
    pSampleStreamingSession->rtcMetricsHistory.prevTs = GETTIME();
    pSampleStreamingSession->remoteCanTrickleIce = FALSE;
    ATOMIC_STORE_BOOL(&pSampleStreamingSession->terminateFlag, FALSE);
    ATOMIC_STORE_BOOL(&pSampleStreamingSession->candidateGatheringDone, FALSE);

    CHK_STATUS(initializePeerConnection(pSampleConfiguration, &pSampleStreamingSession->pPeerConnection));
    CHK_STATUS(peerConnectionOnIceCandidate(pSampleStreamingSession->pPeerConnection, (UINT64) pSampleStreamingSession, onIceCandidateHandler));
    CHK_STATUS(
        peerConnectionOnConnectionStateChange(pSampleStreamingSession->pPeerConnection, (UINT64) pSampleStreamingSession, onConnectionStateChange));
#ifdef ENABLE_DATA_CHANNEL
    if (pSampleConfiguration->onDataChannel != NULL) {
        CHK_STATUS(peerConnectionOnDataChannel(pSampleStreamingSession->pPeerConnection, (UINT64) pSampleStreamingSession,
                                               pSampleConfiguration->onDataChannel));
    }
#endif

    // Declare that we support H264,Profile=42E01F,level-asymmetry-allowed=1,packetization-mode=1 and Opus
    // Add a SendRecv Transceiver of type video
    CHK_STATUS(addSupportedCodec(pSampleStreamingSession->pPeerConnection, RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_MODE));
    pVideoTrack->kind = MEDIA_STREAM_TRACK_KIND_VIDEO;
    pVideoTrack->codec = RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_MODE;
    STRCPY(pVideoTrack->streamId, "myKvsVideoStream");
    STRCPY(pVideoTrack->trackId, "myVideoTrack");
    RtcRtpTransceiverInit videoRtcRtpTransceiverInit;
    videoRtcRtpTransceiverInit.direction =  RTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY;
    CHK_STATUS(addTransceiver(pSampleStreamingSession->pPeerConnection, pVideoTrack, &videoRtcRtpTransceiverInit, &pSampleStreamingSession->pVideoRtcRtpTransceiver));

    CHK_STATUS(transceiverOnBandwidthEstimation(pSampleStreamingSession->pVideoRtcRtpTransceiver, (UINT64) pSampleStreamingSession,
                                                sampleBandwidthEstimationHandler));

    // Add a SendRecv Transceiver of type video
#if AUDIO_OPUS
    CHK_STATUS(addSupportedCodec(pSampleStreamingSession->pPeerConnection, RTC_CODEC_OPUS));
#elif AUDIO_G711_MULAW
    CHK_STATUS(addSupportedCodec(pSampleStreamingSession->pPeerConnection, RTC_CODEC_MULAW));
#elif AUDIO_G711_ALAW   
    CHK_STATUS(addSupportedCodec(pSampleStreamingSession->pPeerConnection, RTC_CODEC_ALAW));
#endif
    pAudioTrack->kind = MEDIA_STREAM_TRACK_KIND_AUDIO;
#if AUDIO_OPUS
    pAudioTrack->codec = RTC_CODEC_OPUS;
#elif AUDIO_G711_MULAW
    pAudioTrack->codec = RTC_CODEC_MULAW;
#elif AUDIO_G711_ALAW  
    pAudioTrack->codec = RTC_CODEC_ALAW;
#endif
    STRCPY(pAudioTrack->streamId, "myKvsVideoStream");
    STRCPY(pAudioTrack->trackId, "myAudioTrack");
    RtcRtpTransceiverInit audioRtcRtpTransceiverInit;
#ifdef ENABLE_AUDIO_SENDRECV
    audioRtcRtpTransceiverInit.direction =  RTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV;
#else
    audioRtcRtpTransceiverInit.direction =  RTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY;
#endif
    CHK_STATUS(addTransceiver(pSampleStreamingSession->pPeerConnection, pAudioTrack, &audioRtcRtpTransceiverInit, &pSampleStreamingSession->pAudioRtcRtpTransceiver));

    CHK_STATUS(transceiverOnBandwidthEstimation(pSampleStreamingSession->pAudioRtcRtpTransceiver, (UINT64) pSampleStreamingSession,
                                                sampleBandwidthEstimationHandler));
    pSampleStreamingSession->firstFrame = TRUE;
    pSampleStreamingSession->startUpLatency = 0;
#ifdef ENABLE_DATA_CHANNEL
    CHK_STATUS(createDataChannel(pSampleStreamingSession->pPeerConnection, "kvsDataChannelMaster", NULL, &pSampleStreamingSession->pRtcDataChannel));
    CHK_LOG_ERR(dataChannelOnMessage(pSampleStreamingSession->pRtcDataChannel, 0, onDataChannelMessageMaster));
#endif
CleanUp:

    if (STATUS_FAILED(retStatus) && pSampleStreamingSession != NULL) {
        freeSampleStreamingSession(&pSampleStreamingSession);
        pSampleStreamingSession = NULL;
    }

    if (ppSampleStreamingSession != NULL) {
        *ppSampleStreamingSession = pSampleStreamingSession;
    }
    SAFE_MEMFREE(pVideoTrack);
    SAFE_MEMFREE(pAudioTrack);
    return retStatus;
}

STATUS freeSampleStreamingSession(PSampleStreamingSession* ppSampleStreamingSession)
{
    STATUS retStatus = STATUS_SUCCESS;
    PSampleStreamingSession pSampleStreamingSession = NULL;

    CHK(ppSampleStreamingSession != NULL, STATUS_NULL_ARG);
    pSampleStreamingSession = *ppSampleStreamingSession;
    CHK(pSampleStreamingSession != NULL, retStatus);

    DLOGD("Freeing streaming session with peer id: %s ", pSampleStreamingSession->peerId);

    ATOMIC_STORE_BOOL(&pSampleStreamingSession->terminateFlag, TRUE);

    if (pSampleStreamingSession->shutdownCallback != NULL) {
        pSampleStreamingSession->shutdownCallback(pSampleStreamingSession->shutdownCallbackCustomData, pSampleStreamingSession);
    }

    if (IS_VALID_TID_VALUE(pSampleStreamingSession->receiveAudioVideoSenderTid)) {
        THREAD_JOIN(pSampleStreamingSession->receiveAudioVideoSenderTid, NULL);
    }

    CHK_LOG_ERR(closePeerConnection(pSampleStreamingSession->pPeerConnection));
    CHK_LOG_ERR(freePeerConnection(&pSampleStreamingSession->pPeerConnection));
    SAFE_MEMFREE(pSampleStreamingSession);

CleanUp:

    CHK_LOG_ERR(retStatus);

    return retStatus;
}

STATUS streamingSessionOnShutdown(PSampleStreamingSession pSampleStreamingSession, UINT64 customData,
                                  StreamSessionShutdownCallback streamSessionShutdownCallback)
{
    STATUS retStatus = STATUS_SUCCESS;

    CHK(pSampleStreamingSession != NULL && streamSessionShutdownCallback != NULL, STATUS_NULL_ARG);

    pSampleStreamingSession->shutdownCallbackCustomData = customData;
    pSampleStreamingSession->shutdownCallback = streamSessionShutdownCallback;

CleanUp:

    return retStatus;
}

//VOID sampleFrameHandler(UINT64 customData, PFrame pFrame)
//{
//    UNUSED_PARAM(customData);
//    DLOGV("Frame received. TrackId: %" PRIu64 ", Size: %u, Flags %u", pFrame->trackId, pFrame->size, pFrame->flags);
//    PSampleStreamingSession pSampleStreamingSession = (PSampleStreamingSession) customData;
//    if (pSampleStreamingSession->firstFrame) {
//        pSampleStreamingSession->firstFrame = FALSE;
//        pSampleStreamingSession->startUpLatency = (GETTIME() - pSampleStreamingSession->offerReceiveTime) / HUNDREDS_OF_NANOS_IN_A_MILLISECOND;
//        printf("Start up latency from offer to first frame: %" PRIu64 "ms\n", pSampleStreamingSession->startUpLatency);
//    }
//}

VOID sampleBandwidthEstimationHandler(UINT64 customData, DOUBLE maxiumBitrate)
{
    UNUSED_PARAM(customData);
    DLOGV("received bitrate suggestion: %f", maxiumBitrate);
}

STATUS handleRemoteCandidate(PSampleStreamingSession pSampleStreamingSession, PSignalingMessage pSignalingMessage)
{
    STATUS retStatus = STATUS_SUCCESS;
    PRtcIceCandidateInit pIceCandidate = NULL;
    CHK(NULL != (pIceCandidate = (PRtcIceCandidateInit) MEMALLOC(SIZEOF(RtcIceCandidateInit))), STATUS_NOT_ENOUGH_MEMORY);
    CHK(pSampleStreamingSession != NULL && pSignalingMessage != NULL, STATUS_NULL_ARG);

    CHK_STATUS(deserializeRtcIceCandidateInit(pSignalingMessage->payload, pSignalingMessage->payloadLen, pIceCandidate));
    CHK_STATUS(addIceCandidate(pSampleStreamingSession->pPeerConnection, pIceCandidate->candidate));

CleanUp:

    SAFE_MEMFREE(pIceCandidate);
    CHK_LOG_ERR(retStatus);
    return retStatus;
}

STATUS traverseDirectoryPEMFileScan(UINT64 customData, DIR_ENTRY_TYPES entryType, PCHAR fullPath, PCHAR fileName)
{
    UNUSED_PARAM(entryType);
    UNUSED_PARAM(fullPath);

    PCHAR pCertName = (PCHAR) customData;
    UINT32 fileNameLen = STRLEN(fileName);

    if (fileNameLen > ARRAY_SIZE(CA_CERT_PEM_FILE_EXTENSION) + 1 &&
        (STRCMPI(CA_CERT_PEM_FILE_EXTENSION, &fileName[fileNameLen - ARRAY_SIZE(CA_CERT_PEM_FILE_EXTENSION) + 1]) == 0)) {
        pCertName[0] = FPATHSEPARATOR;
        pCertName++;
        STRCPY(pCertName, fileName);
    }

    return STATUS_SUCCESS;
}

STATUS lookForSslCert(PSampleConfiguration* ppSampleConfiguration)
{
    STATUS retStatus = STATUS_SUCCESS;
    struct stat pathStat;
    CHAR certName[MAX_PATH_LEN];
    PSampleConfiguration pSampleConfiguration = *ppSampleConfiguration;

    MEMSET(certName, 0x0, ARRAY_SIZE(certName));
//#ifdef KVSWEBRTC_HAVE_GETENV
//    pSampleConfiguration->pCaCertPath = getenv(CACERT_PATH_ENV_VAR);
//#else
//    pSampleConfiguration->pCaCertPath = NULL;
//#endif
    pSampleConfiguration->pCaCertPath = TEMP_CERT_PATH;
    printf("cert path:%s \n", pSampleConfiguration->pCaCertPath);
    {

        BOOL exists = FALSE;
        if(fileExists(pSampleConfiguration->pCaCertPath, &exists) != STATUS_SUCCESS){
            DLOGD("check cert failed");
        }

        DLOGD("cert:%s is %x", exists);
    }
    #if 0
    // if ca cert path is not set from the environment, try to use the one that cmake detected
    if (pSampleConfiguration->pCaCertPath == NULL) {
        CHK_ERR(STRNLEN(DEFAULT_KVS_CACERT_PATH, MAX_PATH_LEN) > 0, STATUS_INVALID_OPERATION, "No ca cert path given (error:%s)", strerror(errno));
        pSampleConfiguration->pCaCertPath = DEFAULT_KVS_CACERT_PATH;
    } else {
        // Check if the environment variable is a path
        CHK(0 == FSTAT(pSampleConfiguration->pCaCertPath, &pathStat), STATUS_DIRECTORY_ENTRY_STAT_ERROR);

        if (S_ISDIR(pathStat.st_mode)) {
            CHK_STATUS(traverseDirectory(pSampleConfiguration->pCaCertPath, (UINT64) &certName, /* iterate */ FALSE, traverseDirectoryPEMFileScan));

            if (certName[0] != 0x0) {
                STRCAT(pSampleConfiguration->pCaCertPath, certName);
            } else {
                DLOGW("Cert not found in path set...checking if CMake detected a path\n");
                CHK_ERR(STRNLEN(DEFAULT_KVS_CACERT_PATH, MAX_PATH_LEN) > 0, STATUS_INVALID_OPERATION, "No ca cert path given (error:%s)",
                        strerror(errno));
                DLOGI("CMake detected cert path\n");
                pSampleConfiguration->pCaCertPath = DEFAULT_KVS_CACERT_PATH;
            }
        }
    }
    #endif
    printf("look for ssl cert successfully\n");
CleanUp:

    CHK_LOG_ERR(retStatus);
    return retStatus;
}

STATUS createSampleConfiguration(PCHAR channelName, SIGNALING_CHANNEL_ROLE_TYPE roleType, BOOL trickleIce, BOOL useTurn,
                                 PSampleConfiguration* ppSampleConfiguration)
{
    STATUS retStatus = STATUS_SUCCESS;
    PCHAR pAccessKey = NULL, pSecretKey = NULL, pSessionToken, pLogLevel;
    PSampleConfiguration pSampleConfiguration = NULL;
    UINT32 logLevel = LOG_LEVEL_DEBUG;

    CHK(ppSampleConfiguration != NULL, STATUS_NULL_ARG);

    CHK(NULL != (pSampleConfiguration = (PSampleConfiguration) MEMCALLOC(1, SIZEOF(SampleConfiguration))), STATUS_NOT_ENOUGH_MEMORY);

    pAccessKey = KVS_WEBRTC_ACCESS_KEY;
    pSecretKey = KVS_WEBRTC_SECRET_KEY;
    CHK_ERR(pAccessKey != NULL, STATUS_INVALID_OPERATION, "AWS_ACCESS_KEY_ID must be set");
    CHK_ERR(pSecretKey != NULL, STATUS_INVALID_OPERATION, "AWS_SECRET_ACCESS_KEY must be set");
    
    
    pSessionToken = GETENV(SESSION_TOKEN_ENV_VAR);
    pSampleConfiguration->enableFileLogging = FALSE;
    #if 0
    if (NULL != getenv(ENABLE_FILE_LOGGING)) {
        pSampleConfiguration->enableFileLogging = TRUE;
    }
    #endif
#ifdef KVSWEBRTC_HAVE_GETENV
    if ((pSampleConfiguration->channelInfo.pRegion = getenv(DEFAULT_REGION_ENV_VAR)) == NULL) {
        pSampleConfiguration->channelInfo.pRegion = DEFAULT_AWS_REGION;
    }
#else
    pSampleConfiguration->channelInfo.pRegion = DEFAULT_AWS_REGION;
#endif

    CHK_STATUS(lookForSslCert(&pSampleConfiguration));

    // Set the logger log level
    if (NULL == (pLogLevel = GETENV(DEBUG_LOG_LEVEL_ENV_VAR)) || STATUS_SUCCESS != STRTOUI32(pLogLevel, NULL, 10, &logLevel) ||
        logLevel < LOG_LEVEL_VERBOSE || logLevel > LOG_LEVEL_SILENT) {
        logLevel = LOG_LEVEL_WARN;
    }
    logLevel = KVS_WEBRTC_LOG_LEVEL;

    SET_LOGGER_LOG_LEVEL(logLevel);

    CHK_STATUS(
        createStaticCredentialProvider(pAccessKey, 0, pSecretKey, 0, pSessionToken, 0, MAX_UINT64, &pSampleConfiguration->pCredentialProvider));

    pSampleConfiguration->mediaSenderTid = INVALID_TID_VALUE;
    pSampleConfiguration->signalingClientHandle = INVALID_SIGNALING_CLIENT_HANDLE_VALUE;
    pSampleConfiguration->sampleConfigurationObjLock = MUTEX_CREATE(TRUE);
    pSampleConfiguration->cvar = CVAR_CREATE();
    pSampleConfiguration->streamingSessionListReadLock = MUTEX_CREATE(FALSE);
    pSampleConfiguration->signalingSendMessageLock = MUTEX_CREATE(FALSE);
    /* This is ignored for master. Master can extract the info from offer. Viewer has to know if peer can trickle or
     * not ahead of time. */
    pSampleConfiguration->trickleIce = trickleIce;
    pSampleConfiguration->useTurn = useTurn;

    pSampleConfiguration->channelInfo.version = CHANNEL_INFO_CURRENT_VERSION;
    pSampleConfiguration->channelInfo.pChannelName = channelName;
    pSampleConfiguration->channelInfo.pKmsKeyId = NULL;
    pSampleConfiguration->channelInfo.tagCount = 0;
    pSampleConfiguration->channelInfo.pTags = NULL;
    pSampleConfiguration->channelInfo.channelType = SIGNALING_CHANNEL_TYPE_SINGLE_MASTER;
    pSampleConfiguration->channelInfo.channelRoleType = roleType;
    pSampleConfiguration->channelInfo.cachingPolicy = SIGNALING_API_CALL_CACHE_TYPE_NONE;
    pSampleConfiguration->channelInfo.cachingPeriod = SIGNALING_API_CALL_CACHE_TTL_SENTINEL_VALUE;
    pSampleConfiguration->channelInfo.asyncIceServerConfig = FALSE;
    pSampleConfiguration->channelInfo.retry = TRUE;
    pSampleConfiguration->channelInfo.reconnect = TRUE;
    pSampleConfiguration->channelInfo.pCertPath = pSampleConfiguration->pCaCertPath;
    pSampleConfiguration->channelInfo.messageTtl = 0; // Default is 60 seconds

    pSampleConfiguration->signalingClientCallbacks.version = SIGNALING_CLIENT_CALLBACKS_CURRENT_VERSION;
    pSampleConfiguration->signalingClientCallbacks.errorReportFn = signalingClientError;
    pSampleConfiguration->signalingClientCallbacks.stateChangeFn = signalingClientStateChanged;
    pSampleConfiguration->signalingClientCallbacks.customData = (UINT64) pSampleConfiguration;

    pSampleConfiguration->clientInfo.version = SIGNALING_CLIENT_INFO_CURRENT_VERSION;
    pSampleConfiguration->clientInfo.loggingLevel = logLevel;
    pSampleConfiguration->iceCandidatePairStatsTimerId = MAX_UINT32;

    ATOMIC_STORE_BOOL(&pSampleConfiguration->interrupted, FALSE);
    ATOMIC_STORE_BOOL(&pSampleConfiguration->mediaThreadStarted, FALSE);
    ATOMIC_STORE_BOOL(&pSampleConfiguration->appTerminateFlag, FALSE);
    ATOMIC_STORE_BOOL(&pSampleConfiguration->recreateSignalingClient, FALSE);
    ATOMIC_STORE_BOOL(&pSampleConfiguration->connected, FALSE);

    //CHK_STATUS(timerQueueCreate(&pSampleConfiguration->timerQueueHandle));
    CHK_STATUS(timerQueueCreateEx(&pSampleConfiguration->timerQueueHandle, SAMPLE_TIMER_NAME, SAMPLE_TIMER_SIZE));

    pSampleConfiguration->iceUriCount = 0;

    CHK_STATUS(hashTableCreateWithParams(SAMPLE_HASH_TABLE_BUCKET_COUNT, SAMPLE_HASH_TABLE_BUCKET_LENGTH,
                                         &pSampleConfiguration->pPendingSignalingMessageForRemoteClient));
    CHK_STATUS(hashTableCreateWithParams(SAMPLE_HASH_TABLE_BUCKET_COUNT, SAMPLE_HASH_TABLE_BUCKET_LENGTH,
                                         &pSampleConfiguration->pRtcPeerConnectionForRemoteClient));

CleanUp:

    if (STATUS_FAILED(retStatus)) {
        freeSampleConfiguration(&pSampleConfiguration);
    }

    if (ppSampleConfiguration != NULL) {
        *ppSampleConfiguration = pSampleConfiguration;
    }

    return retStatus;
}

STATUS logSignalingClientStats(PSignalingClientMetrics pSignalingClientMetrics)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    CHK(pSignalingClientMetrics != NULL, STATUS_NULL_ARG);
    DLOGD("Signaling client connection duration: %" PRIu64 " ms",
          (pSignalingClientMetrics->signalingClientStats.connectionDuration / HUNDREDS_OF_NANOS_IN_A_MILLISECOND));
    DLOGD("Number of signaling client API errors: %d", pSignalingClientMetrics->signalingClientStats.numberOfErrors);
    DLOGD("Number of runtime errors in the session: %d", pSignalingClientMetrics->signalingClientStats.numberOfRuntimeErrors);
    DLOGD("Signaling client uptime: %" PRIu64 " ms",
          (pSignalingClientMetrics->signalingClientStats.connectionDuration / HUNDREDS_OF_NANOS_IN_A_MILLISECOND));
    // This gives the EMA of the signalingCreateChannel, signalingDescribeChannel, signalingGetChannelEndpoint and signalingDeleteChannel calls
    DLOGD("Control Plane API call latency: %" PRIu64 " ms",
          (pSignalingClientMetrics->signalingClientStats.cpApiCallLatency / HUNDREDS_OF_NANOS_IN_A_MILLISECOND));
    // This gives the EMA of the signalingGetIceConfig() call.
    DLOGD("Data Plane API call latency: %" PRIu64 " ms",
          (pSignalingClientMetrics->signalingClientStats.dpApiCallLatency / HUNDREDS_OF_NANOS_IN_A_MILLISECOND));
CleanUp:
    LEAVES();
    return retStatus;
}

STATUS getIceCandidatePairStatsCallback(UINT32 timerId, UINT64 currentTime, UINT64 customData)
{
    UNUSED_PARAM(timerId);
    UNUSED_PARAM(currentTime);
    STATUS retStatus = STATUS_SUCCESS;
    PSampleConfiguration pSampleConfiguration = (PSampleConfiguration) customData;
    UINT32 i;
    pSampleConfiguration->rtcIceCandidatePairMetrics.requestedTypeOfStats = RTC_STATS_TYPE_CANDIDATE_PAIR;
    UINT64 currentMeasureDuration = 0;
    DOUBLE averagePacketsDiscardedOnSend = 0.0;
    DOUBLE averageNumberOfPacketsSentPerSecond = 0.0;
    DOUBLE averageNumberOfPacketsReceivedPerSecond = 0.0;
    DOUBLE outgoingBitrate = 0.0;
    DOUBLE incomingBitrate = 0.0;

    if (pSampleConfiguration == NULL) {
        DLOGW("[KVS Master] getPeriodicStats(): operation returned status code: 0x%08x \n", STATUS_NULL_ARG);
        goto CleanUp;
    }
    for (i = 0; i < pSampleConfiguration->streamingSessionCount; ++i) {
        if (STATUS_SUCCEEDED(rtcPeerConnectionGetMetrics(pSampleConfiguration->sampleStreamingSessionList[i]->pPeerConnection, NULL,
                                                         &pSampleConfiguration->rtcIceCandidatePairMetrics))) {
            currentMeasureDuration = (pSampleConfiguration->rtcIceCandidatePairMetrics.timestamp -
                                      pSampleConfiguration->sampleStreamingSessionList[i]->rtcMetricsHistory.prevTs) /
                HUNDREDS_OF_NANOS_IN_A_SECOND;
            DLOGD("Current duration: %" PRIu64 " seconds", currentMeasureDuration);
            if (currentMeasureDuration > 0) {
                DLOGD("Selected local candidate ID: %s",
                      pSampleConfiguration->rtcIceCandidatePairMetrics.rtcStatsObject.iceCandidatePairStats.localCandidateId);
                DLOGD("Selected remote candidate ID: %s",
                      pSampleConfiguration->rtcIceCandidatePairMetrics.rtcStatsObject.iceCandidatePairStats.remoteCandidateId);
                // TODO: Display state as a string for readability
                DLOGD("Ice Candidate Pair state: %d", pSampleConfiguration->rtcIceCandidatePairMetrics.rtcStatsObject.iceCandidatePairStats.state);
                DLOGD("Nomination state: %s",
                      pSampleConfiguration->rtcIceCandidatePairMetrics.rtcStatsObject.iceCandidatePairStats.nominated ? "nominated"
                                                                                                                      : "not nominated");
                averageNumberOfPacketsSentPerSecond =
                    (DOUBLE)(pSampleConfiguration->rtcIceCandidatePairMetrics.rtcStatsObject.iceCandidatePairStats.packetsSent -
                             pSampleConfiguration->sampleStreamingSessionList[i]->rtcMetricsHistory.prevNumberOfPacketsSent) /
                    (DOUBLE) currentMeasureDuration;
                DLOGD("Packet send rate: %lf pkts/sec", averageNumberOfPacketsSentPerSecond);

                averageNumberOfPacketsReceivedPerSecond =
                    (DOUBLE)(pSampleConfiguration->rtcIceCandidatePairMetrics.rtcStatsObject.iceCandidatePairStats.packetsReceived -
                             pSampleConfiguration->sampleStreamingSessionList[i]->rtcMetricsHistory.prevNumberOfPacketsReceived) /
                    (DOUBLE) currentMeasureDuration;
                DLOGD("Packet receive rate: %lf pkts/sec", averageNumberOfPacketsReceivedPerSecond);

                outgoingBitrate = (DOUBLE)((pSampleConfiguration->rtcIceCandidatePairMetrics.rtcStatsObject.iceCandidatePairStats.bytesSent -
                                            pSampleConfiguration->sampleStreamingSessionList[i]->rtcMetricsHistory.prevNumberOfBytesSent) *
                                           8.0) /
                    currentMeasureDuration;
                DLOGD("Outgoing bit rate: %lf bps", outgoingBitrate);

                incomingBitrate = (DOUBLE)((pSampleConfiguration->rtcIceCandidatePairMetrics.rtcStatsObject.iceCandidatePairStats.bytesReceived -
                                            pSampleConfiguration->sampleStreamingSessionList[i]->rtcMetricsHistory.prevNumberOfBytesReceived) *
                                           8.0) /
                    currentMeasureDuration;
                DLOGD("Incoming bit rate: %lf bps", incomingBitrate);

                averagePacketsDiscardedOnSend =
                    (DOUBLE)(pSampleConfiguration->rtcIceCandidatePairMetrics.rtcStatsObject.iceCandidatePairStats.packetsDiscardedOnSend -
                             pSampleConfiguration->sampleStreamingSessionList[i]->rtcMetricsHistory.prevPacketsDiscardedOnSend) /
                    (DOUBLE) currentMeasureDuration;
                DLOGD("Packet discard rate: %lf pkts/sec", averagePacketsDiscardedOnSend);

                DLOGD("Current STUN request round trip time: %lf sec",
                      pSampleConfiguration->rtcIceCandidatePairMetrics.rtcStatsObject.iceCandidatePairStats.currentRoundTripTime);
                DLOGD("Number of STUN responses received: %llu",
                      pSampleConfiguration->rtcIceCandidatePairMetrics.rtcStatsObject.iceCandidatePairStats.responsesReceived);

                pSampleConfiguration->sampleStreamingSessionList[i]->rtcMetricsHistory.prevTs =
                    pSampleConfiguration->rtcIceCandidatePairMetrics.timestamp;
                pSampleConfiguration->sampleStreamingSessionList[i]->rtcMetricsHistory.prevNumberOfPacketsSent =
                    pSampleConfiguration->rtcIceCandidatePairMetrics.rtcStatsObject.iceCandidatePairStats.packetsSent;
                pSampleConfiguration->sampleStreamingSessionList[i]->rtcMetricsHistory.prevNumberOfPacketsReceived =
                    pSampleConfiguration->rtcIceCandidatePairMetrics.rtcStatsObject.iceCandidatePairStats.packetsReceived;
                pSampleConfiguration->sampleStreamingSessionList[i]->rtcMetricsHistory.prevNumberOfBytesSent =
                    pSampleConfiguration->rtcIceCandidatePairMetrics.rtcStatsObject.iceCandidatePairStats.bytesSent;
                pSampleConfiguration->sampleStreamingSessionList[i]->rtcMetricsHistory.prevNumberOfBytesReceived =
                    pSampleConfiguration->rtcIceCandidatePairMetrics.rtcStatsObject.iceCandidatePairStats.bytesReceived;
                pSampleConfiguration->sampleStreamingSessionList[i]->rtcMetricsHistory.prevPacketsDiscardedOnSend =
                    pSampleConfiguration->rtcIceCandidatePairMetrics.rtcStatsObject.iceCandidatePairStats.packetsDiscardedOnSend;
            }
        }
    }
CleanUp:
    return retStatus;
}

STATUS freePendingSignalingMessageQueue(UINT64 customData, PHashEntry pHashEntry)
{
    UNUSED_PARAM(customData);
    PStackQueue pStackQueue = (PStackQueue) pHashEntry->value;
    stackQueueClear(pStackQueue, TRUE);
    stackQueueFree(pStackQueue);
    return STATUS_SUCCESS;
}

STATUS freeSampleConfiguration(PSampleConfiguration* ppSampleConfiguration)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSampleConfiguration pSampleConfiguration;
    UINT32 i;
    BOOL locked = FALSE;

    CHK(ppSampleConfiguration != NULL, STATUS_NULL_ARG);
    pSampleConfiguration = *ppSampleConfiguration;

    hashTableIterateEntries(pSampleConfiguration->pPendingSignalingMessageForRemoteClient, (UINT64) NULL, freePendingSignalingMessageQueue);
    hashTableClear(pSampleConfiguration->pPendingSignalingMessageForRemoteClient);
    hashTableFree(pSampleConfiguration->pPendingSignalingMessageForRemoteClient);

    hashTableClear(pSampleConfiguration->pRtcPeerConnectionForRemoteClient);
    hashTableFree(pSampleConfiguration->pRtcPeerConnectionForRemoteClient);

    CHK(pSampleConfiguration != NULL, retStatus);

    if (IS_VALID_MUTEX_VALUE(pSampleConfiguration->sampleConfigurationObjLock)) {
        MUTEX_LOCK(pSampleConfiguration->sampleConfigurationObjLock);
        locked = TRUE;
    }
    for (i = 0; i < pSampleConfiguration->streamingSessionCount; ++i) {
        retStatus = gatherIceServerStats(pSampleConfiguration->sampleStreamingSessionList[i]);
        if (STATUS_FAILED(retStatus)) {
            DLOGW("Failed to ICE Server Stats for streaming session %d: %08x", i, retStatus);
        }
        freeSampleStreamingSession(&pSampleConfiguration->sampleStreamingSessionList[i]);
    }
    if (locked) {
        MUTEX_UNLOCK(pSampleConfiguration->sampleConfigurationObjLock);
    }
    deinitKvsWebRtc();

    SAFE_MEMFREE(pSampleConfiguration->pVideoFrameBuffer);
    SAFE_MEMFREE(pSampleConfiguration->pAudioFrameBuffer);

    if (IS_VALID_CVAR_VALUE(pSampleConfiguration->cvar) && IS_VALID_MUTEX_VALUE(pSampleConfiguration->sampleConfigurationObjLock)) {
        CVAR_BROADCAST(pSampleConfiguration->cvar);
        MUTEX_LOCK(pSampleConfiguration->sampleConfigurationObjLock);
        MUTEX_UNLOCK(pSampleConfiguration->sampleConfigurationObjLock);
    }

    if (IS_VALID_MUTEX_VALUE(pSampleConfiguration->sampleConfigurationObjLock)) {
        MUTEX_FREE(pSampleConfiguration->sampleConfigurationObjLock);
    }

    if (IS_VALID_MUTEX_VALUE(pSampleConfiguration->streamingSessionListReadLock)) {
        MUTEX_FREE(pSampleConfiguration->streamingSessionListReadLock);
    }

    if (IS_VALID_MUTEX_VALUE(pSampleConfiguration->signalingSendMessageLock)) {
        MUTEX_FREE(pSampleConfiguration->signalingSendMessageLock);
    }

    if (IS_VALID_CVAR_VALUE(pSampleConfiguration->cvar)) {
        CVAR_FREE(pSampleConfiguration->cvar);
    }

    freeStaticCredentialProvider(&pSampleConfiguration->pCredentialProvider);

    if (pSampleConfiguration->iceCandidatePairStatsTimerId != MAX_UINT32) {
        CHK_STATUS(timerQueueCancelTimer(pSampleConfiguration->timerQueueHandle, pSampleConfiguration->iceCandidatePairStatsTimerId,
                                         (UINT64) pSampleConfiguration));
        pSampleConfiguration->iceCandidatePairStatsTimerId = MAX_UINT32;
    }
    if (IS_VALID_TIMER_QUEUE_HANDLE(pSampleConfiguration->timerQueueHandle)) {
        timerQueueFree(&pSampleConfiguration->timerQueueHandle);
    }

    MEMFREE(*ppSampleConfiguration);
    *ppSampleConfiguration = NULL;

CleanUp:

    LEAVES();
    return retStatus;
}
/**
 * @brief the cleanup function of this app_main
*/
STATUS sessionCleanupWait(PSampleConfiguration pSampleConfiguration)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PSampleStreamingSession pSampleStreamingSession = NULL;
    UINT32 i, clientIdHash;
    BOOL locked = FALSE, peerConnectionFound = FALSE;
    SIGNALING_CLIENT_STATE signalingClientState;

    CHK(pSampleConfiguration != NULL, STATUS_NULL_ARG);

    while (!ATOMIC_LOAD_BOOL(&pSampleConfiguration->interrupted)) {
        // Keep the main set of operations interlocked until cvar wait which would atomically unlock
        MUTEX_LOCK(pSampleConfiguration->sampleConfigurationObjLock);
        locked = TRUE;

        // scan and cleanup terminated streaming session
        for (i = 0; i < pSampleConfiguration->streamingSessionCount; ++i) {
            if (ATOMIC_LOAD_BOOL(&pSampleConfiguration->sampleStreamingSessionList[i]->terminateFlag)) {
                pSampleStreamingSession = pSampleConfiguration->sampleStreamingSessionList[i];

                MUTEX_LOCK(pSampleConfiguration->streamingSessionListReadLock);

                // swap with last element and decrement count
                pSampleConfiguration->streamingSessionCount--;
                pSampleConfiguration->sampleStreamingSessionList[i] =
                    pSampleConfiguration->sampleStreamingSessionList[pSampleConfiguration->streamingSessionCount];

                // Remove from the hash table
                clientIdHash = COMPUTE_CRC32((PBYTE) pSampleStreamingSession->peerId, (UINT32) STRLEN(pSampleStreamingSession->peerId));
                CHK_STATUS(hashTableContains(pSampleConfiguration->pRtcPeerConnectionForRemoteClient, clientIdHash, &peerConnectionFound));
                if (peerConnectionFound) {
                    CHK_STATUS(hashTableRemove(pSampleConfiguration->pRtcPeerConnectionForRemoteClient, clientIdHash));
                }

                MUTEX_UNLOCK(pSampleConfiguration->streamingSessionListReadLock);

                CHK_STATUS(freeSampleStreamingSession(&pSampleStreamingSession));
            }
        }

        // Check if we need to re-create the signaling client on-the-fly
        if (ATOMIC_LOAD_BOOL(&pSampleConfiguration->recreateSignalingClient) &&
            STATUS_SUCCEEDED(signalingClientFree(&pSampleConfiguration->signalingClientHandle)) &&
            STATUS_SUCCEEDED(signalingClientCreate(&pSampleConfiguration->clientInfo, &pSampleConfiguration->channelInfo,
                                                       &pSampleConfiguration->signalingClientCallbacks, pSampleConfiguration->pCredentialProvider,
                                                       &pSampleConfiguration->signalingClientHandle))) {
            // Re-set the variable again
            ATOMIC_STORE_BOOL(&pSampleConfiguration->recreateSignalingClient, FALSE);
        }

        // Check the signaling client state and connect if needed
        if (IS_VALID_SIGNALING_CLIENT_HANDLE(pSampleConfiguration->signalingClientHandle)) {
            CHK_STATUS(signalingClientGetCurrentState(pSampleConfiguration->signalingClientHandle, &signalingClientState));
            if (signalingClientState == SIGNALING_CLIENT_STATE_READY) {
                UNUSED_PARAM(signalingClientConnect(pSampleConfiguration->signalingClientHandle));
            }
        }

        // periodically wake up and clean up terminated streaming session
        CVAR_WAIT(pSampleConfiguration->cvar, pSampleConfiguration->sampleConfigurationObjLock, SAMPLE_SESSION_CLEANUP_WAIT_PERIOD);
        MUTEX_UNLOCK(pSampleConfiguration->sampleConfigurationObjLock);
        locked = FALSE;
    }

CleanUp:

    CHK_LOG_ERR(retStatus);

    if (locked) {
        MUTEX_UNLOCK(pSampleConfiguration->sampleConfigurationObjLock);
    }

    LEAVES();
    return retStatus;
}

STATUS submitPendingIceCandidate(PStackQueue pPendingMessageQueue, PSampleStreamingSession pSampleStreamingSession)
{
    STATUS retStatus = STATUS_SUCCESS;
    BOOL noPendingSignalingMessageForClient = FALSE;
    PReceivedSignalingMessage pReceivedSignalingMessage = NULL;
    UINT64 hashValue;

    do {
        CHK_STATUS(stackQueueIsEmpty(pPendingMessageQueue, &noPendingSignalingMessageForClient));
        if (!noPendingSignalingMessageForClient) {
            hashValue = 0;
            CHK_STATUS(stackQueueDequeue(pPendingMessageQueue, &hashValue));
            pReceivedSignalingMessage = (PReceivedSignalingMessage) hashValue;
            CHK(pReceivedSignalingMessage != NULL, STATUS_INTERNAL_ERROR);
            if (pReceivedSignalingMessage->signalingMessage.messageType == SIGNALING_MESSAGE_TYPE_ICE_CANDIDATE) {
                CHK_STATUS(handleRemoteCandidate(pSampleStreamingSession, &pReceivedSignalingMessage->signalingMessage));
            }
            SAFE_MEMFREE(pReceivedSignalingMessage);
        }
    } while (!noPendingSignalingMessageForClient);
    CHK_STATUS(stackQueueFree(pPendingMessageQueue));

CleanUp:

    SAFE_MEMFREE(pReceivedSignalingMessage);
    CHK_LOG_ERR(retStatus);
    return retStatus;
}
/**
 * @brief the handler of sdp messages.
*/
STATUS signalingMessageReceived(UINT64 customData, PReceivedSignalingMessage pReceivedSignalingMessage)
{
    STATUS retStatus = STATUS_SUCCESS;
    PSampleConfiguration pSampleConfiguration = (PSampleConfiguration) customData;
    BOOL peerConnectionFound = FALSE;
    BOOL locked = TRUE;
    UINT32 clientIdHash;
    UINT64 hashValue = 0;
    PStackQueue pPendingMessageQueue = NULL;
    PSampleStreamingSession pSampleStreamingSession = NULL;
    PReceivedSignalingMessage pReceivedSignalingMessageCopy = NULL;

    CHK(pSampleConfiguration != NULL, STATUS_NULL_ARG);

    MUTEX_LOCK(pSampleConfiguration->sampleConfigurationObjLock);
    locked = TRUE;

    clientIdHash = COMPUTE_CRC32((PBYTE) pReceivedSignalingMessage->signalingMessage.peerClientId,
                                 (UINT32) STRLEN(pReceivedSignalingMessage->signalingMessage.peerClientId));
    CHK_STATUS(hashTableContains(pSampleConfiguration->pRtcPeerConnectionForRemoteClient, clientIdHash, &peerConnectionFound));
    if (peerConnectionFound) {
        CHK_STATUS(hashTableGet(pSampleConfiguration->pRtcPeerConnectionForRemoteClient, clientIdHash, &hashValue));
        pSampleStreamingSession = (PSampleStreamingSession) hashValue;
    }

    switch (pReceivedSignalingMessage->signalingMessage.messageType) {
        case SIGNALING_MESSAGE_TYPE_OFFER:
            DLOGD("offer.");
            /*
             * Create new streaming session for each offer, then insert the client id and streaming session into
             * pRtcPeerConnectionForRemoteClient for subsequent ice candidate messages. Lastly check if there is
             * any ice candidate messages queued in pPendingSignalingMessageForRemoteClient. If so then submit
             * all of them.
             */
            if (pSampleConfiguration->streamingSessionCount == SIZEOF(pSampleConfiguration->sampleStreamingSessionList)) {
                DLOGW("Max simultaneous streaming session count reached.");
                CHK(FALSE, retStatus);
            }
            CHK_STATUS(createSampleStreamingSession(pSampleConfiguration, pReceivedSignalingMessage->signalingMessage.peerClientId, TRUE,
                                                    &pSampleStreamingSession));
            pSampleStreamingSession->offerReceiveTime = GETTIME();
            MUTEX_LOCK(pSampleConfiguration->streamingSessionListReadLock);
            pSampleConfiguration->sampleStreamingSessionList[pSampleConfiguration->streamingSessionCount++] = pSampleStreamingSession;
            MUTEX_UNLOCK(pSampleConfiguration->streamingSessionListReadLock);

            CHK_STATUS(handleOffer(pSampleConfiguration, pSampleStreamingSession, &pReceivedSignalingMessage->signalingMessage));
            CHK_STATUS(hashTablePut(pSampleConfiguration->pRtcPeerConnectionForRemoteClient, clientIdHash, (UINT64) pSampleStreamingSession));

            // If there are any ice candidate messages in the queue for this client id, submit them now.
            if (STATUS_SUCCEEDED(hashTableGet(pSampleConfiguration->pPendingSignalingMessageForRemoteClient, clientIdHash, &hashValue))) {
                pPendingMessageQueue = (PStackQueue) hashValue;

                CHK_STATUS(submitPendingIceCandidate(pPendingMessageQueue, pSampleStreamingSession));
                // NULL the pointer to avoid it being freed in the cleanup
                pPendingMessageQueue = NULL;

                CHK_STATUS(hashTableRemove(pSampleConfiguration->pPendingSignalingMessageForRemoteClient, clientIdHash));
            }
            break;

        case SIGNALING_MESSAGE_TYPE_ANSWER:
            DLOGD("answer.");
            /*
             * for viewer, pSampleStreamingSession should've already been created. insert the client id and
             * streaming session into pRtcPeerConnectionForRemoteClient for subsequent ice candidate messages.
             * Lastly check if there is any ice candidate messages queued in pPendingSignalingMessageForRemoteClient.
             * If so then submit all of them.
             */
            pSampleStreamingSession = pSampleConfiguration->sampleStreamingSessionList[0];
            CHK_STATUS(handleAnswer(pSampleConfiguration, pSampleStreamingSession, &pReceivedSignalingMessage->signalingMessage));
            CHK_STATUS(hashTablePut(pSampleConfiguration->pRtcPeerConnectionForRemoteClient, clientIdHash, (UINT64) pSampleStreamingSession));

            hashValue = 0;
            // If there are any ice candidate messages in the queue for this client id, submit them now.
            if (STATUS_SUCCEEDED(hashTableGet(pSampleConfiguration->pPendingSignalingMessageForRemoteClient, clientIdHash, &hashValue))) {
                pPendingMessageQueue = (PStackQueue) hashValue;
                CHK_STATUS(submitPendingIceCandidate(pPendingMessageQueue, pSampleStreamingSession));
                // NULL the pointer to avoid it being freed in the cleanup
                pPendingMessageQueue = NULL;
                CHK_STATUS(hashTableRemove(pSampleConfiguration->pPendingSignalingMessageForRemoteClient, clientIdHash));
            }
            break;

        case SIGNALING_MESSAGE_TYPE_ICE_CANDIDATE:
            /*
             * if peer connection hasn't been created, create an queue to store the ice candidate message. Otherwise
             * submit the signaling message into the corresponding streaming session.
             */
            
            if (!peerConnectionFound) {
                DLOGD("queue the ice candidates.");
                hashValue = 0;
                if (STATUS_HASH_KEY_NOT_PRESENT ==
                    hashTableGet(pSampleConfiguration->pPendingSignalingMessageForRemoteClient, clientIdHash, &hashValue)) {
                    pPendingMessageQueue = (PStackQueue) hashValue;
                    CHK_STATUS(stackQueueCreate(&pPendingMessageQueue));
                    CHK_STATUS(
                        hashTablePut(pSampleConfiguration->pPendingSignalingMessageForRemoteClient, clientIdHash, (UINT64) pPendingMessageQueue));
                } else {
                    pPendingMessageQueue = (PStackQueue) hashValue;
                }	                

                pReceivedSignalingMessageCopy = (PReceivedSignalingMessage) MEMCALLOC(1, SIZEOF(ReceivedSignalingMessage));
                *pReceivedSignalingMessageCopy = *pReceivedSignalingMessage;

                CHK_STATUS(stackQueueEnqueue(pPendingMessageQueue, (UINT64) pReceivedSignalingMessageCopy));

                // NULL the pointers to not free any longer
                pPendingMessageQueue = NULL;
                pReceivedSignalingMessageCopy = NULL;
            } else {
                DLOGD("handle the ice candidates.");
                CHK_STATUS(handleRemoteCandidate(pSampleStreamingSession, &pReceivedSignalingMessage->signalingMessage));
            }
            break;

        default:
            DLOGD("Unhandled signaling message type %u", pReceivedSignalingMessage->signalingMessage.messageType);
            break;
    }

CleanUp:


    SAFE_MEMFREE(pReceivedSignalingMessageCopy);
    if (pPendingMessageQueue != NULL) {
        stackQueueFree(pPendingMessageQueue);
    }

    if (locked) {
        MUTEX_UNLOCK(pSampleConfiguration->sampleConfigurationObjLock);
    }

    CHK_LOG_ERR(retStatus);
    return retStatus;
}
/**
 * @brief return epoch time in hundreds of nanosecond
*/
uint64_t getEpochTimestampInHundredsOfNanos( void* pTick )
{
    uint64_t timestamp;

    long sec;
    long usec;
    unsigned int tick;
    unsigned int tickDiff;

    sntp_get_lasttime(&sec, &usec, &tick);

    if ((void*)pTick == NULL)
        tickDiff = xTaskGetTickCount() - tick;
    else
        tickDiff = (*(PUINT32)pTick) - tick;

    sec += tickDiff / configTICK_RATE_HZ;
    usec += ( ( tickDiff % configTICK_RATE_HZ ) / portTICK_RATE_MS ) * 1000;

    if ( usec >= 1000000 ){
        usec -= 1000000;
        sec ++;
    }

    timestamp = ((uint64_t)sec * 1000 + usec / 1000 ) * HUNDREDS_OF_NANOS_IN_A_MILLISECOND;

    return timestamp;
}

#endif  /* CONFIG_EXAMPLE_KVS_WEBRTC */