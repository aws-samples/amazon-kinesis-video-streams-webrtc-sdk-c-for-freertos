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
#define LOG_CLASS "AppMetrics"
#include "AppMetrics.h"

UINT32 getLogLevel(VOID)
{
    PCHAR pLogLevel;
    UINT32 logLevel = LOG_LEVEL_DEBUG;
    // Set the logger log level
    if (NULL == (pLogLevel = GETENV(DEBUG_LOG_LEVEL_ENV_VAR)) || STATUS_SUCCESS != STRTOUI32(pLogLevel, NULL, 10, &logLevel) ||
        logLevel < LOG_LEVEL_VERBOSE || logLevel > LOG_LEVEL_SILENT) {
        logLevel = LOG_LEVEL_WARN;
    }
    return logLevel;
}

STATUS setupFileLogging(PBOOL pEnable)
{
    STATUS retStatus = STATUS_SUCCESS;
    BOOL enable = FALSE;
    // setup the file logging.

    if (NULL != GETENV(ENABLE_FILE_LOGGING)) {
        enable = TRUE;
    } else {
        CHK(FALSE, STATUS_SUCCESS);
    }
    retStatus = createFileLogger(APP_METRICS_FILE_LOGGING_BUFFER_SIZE, APP_METRICS_LOG_FILES_MAX_NUMBER,
                                    (PCHAR) FILE_LOGGER_LOG_FILE_DIRECTORY_PATH, TRUE, TRUE, NULL);
    if (retStatus != STATUS_SUCCESS) {
        DLOGI("peration returned status code: 0x%08x \n", retStatus);
        enable = FALSE;
        retStatus = STATUS_APP_METRICS_SETUP_LOGGER;
    }

CleanUp:
    *pEnable = enable;
    return retStatus;
}

STATUS closeFileLogging(VOID)
{
    STATUS retStatus = STATUS_SUCCESS;
    retStatus = freeFileLogger();
    if (retStatus != STATUS_SUCCESS) {
        retStatus = STATUS_APP_METRICS_FREE_LOGGER;
    }
    return retStatus;
}

STATUS logIceServerStats(PRtcPeerConnection pRtcPeerConnection, UINT32 index)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PRtcStats pRtcMetrics = NULL;
    CHK(pRtcPeerConnection != NULL, STATUS_APP_METRICS_NULL_ARG);
    CHK(NULL != (pRtcMetrics = (PRtcStats) MEMCALLOC(1, SIZEOF(RtcStats))), STATUS_APP_COMMON_NOT_ENOUGH_MEMORY);

    pRtcMetrics->requestedTypeOfStats = RTC_STATS_TYPE_ICE_SERVER;
    pRtcMetrics->rtcStatsObject.iceServerStats.iceServerIndex = index;
    CHK(rtcPeerConnectionGetMetrics(pRtcPeerConnection, NULL, pRtcMetrics) == STATUS_SUCCESS, STATUS_APP_METRICS_ICE_SERVER);
    DLOGD("ICE Server URL: %s", pRtcMetrics->rtcStatsObject.iceServerStats.url);
    DLOGD("ICE Server port: %d", pRtcMetrics->rtcStatsObject.iceServerStats.port);
    DLOGD("ICE Server protocol: %s", pRtcMetrics->rtcStatsObject.iceServerStats.protocol);
    DLOGD("Total requests sent:%" PRIu64, pRtcMetrics->rtcStatsObject.iceServerStats.totalRequestsSent);
    DLOGD("Total responses received: %" PRIu64, pRtcMetrics->rtcStatsObject.iceServerStats.totalResponsesReceived);
    DLOGD("Total round trip time: %" PRIu64 "ms", pRtcMetrics->rtcStatsObject.iceServerStats.totalRoundTripTime / HUNDREDS_OF_NANOS_IN_A_MILLISECOND);

CleanUp:
    SAFE_MEMFREE(pRtcMetrics);
    LEAVES();
    return retStatus;
}

STATUS logSelectedIceCandidatesInformation(PRtcPeerConnection pRtcPeerConnection)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    PRtcStats pRtcMetrics = NULL;

    CHK(NULL != (pRtcMetrics = (PRtcStats) MEMCALLOC(1, SIZEOF(RtcStats))), STATUS_NOT_ENOUGH_MEMORY);

    CHK(pRtcPeerConnection != NULL, STATUS_APP_METRICS_NULL_ARG);
    pRtcMetrics->requestedTypeOfStats = RTC_STATS_TYPE_LOCAL_CANDIDATE;
    CHK(rtcPeerConnectionGetMetrics(pRtcPeerConnection, NULL, pRtcMetrics) == STATUS_SUCCESS, STATUS_APP_METRICS_LOCAL_ICE_CANDIDATE);
    DLOGD("Local Candidate IP Address: %s", pRtcMetrics->rtcStatsObject.localIceCandidateStats.address);
    DLOGD("Local Candidate type: %s", pRtcMetrics->rtcStatsObject.localIceCandidateStats.candidateType);
    DLOGD("Local Candidate port: %d", pRtcMetrics->rtcStatsObject.localIceCandidateStats.port);
    DLOGD("Local Candidate priority: %d", pRtcMetrics->rtcStatsObject.localIceCandidateStats.priority);
    DLOGD("Local Candidate transport protocol: %s", pRtcMetrics->rtcStatsObject.localIceCandidateStats.protocol);
    DLOGD("Local Candidate relay protocol: %s", pRtcMetrics->rtcStatsObject.localIceCandidateStats.relayProtocol);
    DLOGD("Local Candidate Ice server source: %s", pRtcMetrics->rtcStatsObject.localIceCandidateStats.url);

    pRtcMetrics->requestedTypeOfStats = RTC_STATS_TYPE_REMOTE_CANDIDATE;
    CHK(rtcPeerConnectionGetMetrics(pRtcPeerConnection, NULL, pRtcMetrics) == STATUS_SUCCESS, STATUS_APP_METRICS_REMOTE_ICE_CANDIDATE);
    DLOGD("Remote Candidate IP Address: %s", pRtcMetrics->rtcStatsObject.remoteIceCandidateStats.address);
    DLOGD("Remote Candidate type: %s", pRtcMetrics->rtcStatsObject.remoteIceCandidateStats.candidateType);
    DLOGD("Remote Candidate port: %d", pRtcMetrics->rtcStatsObject.remoteIceCandidateStats.port);
    DLOGD("Remote Candidate priority: %d", pRtcMetrics->rtcStatsObject.remoteIceCandidateStats.priority);
    DLOGD("Remote Candidate transport protocol: %s", pRtcMetrics->rtcStatsObject.remoteIceCandidateStats.protocol);
CleanUp:
    SAFE_MEMFREE(pRtcMetrics);
    LEAVES();
    return retStatus;
}

STATUS logSignalingClientStats(PSignalingClientMetrics pSignalingClientMetrics)
{
    ENTERS();
    STATUS retStatus = STATUS_SUCCESS;
    CHK(pSignalingClientMetrics != NULL, STATUS_APP_METRICS_NULL_ARG);
    DLOGD("Signaling client connection duration: %" PRIu64 " ms",
          (pSignalingClientMetrics->signalingClientStats.connectionDuration / HUNDREDS_OF_NANOS_IN_A_MILLISECOND));
    DLOGD("Number of signaling client API errors: %d", pSignalingClientMetrics->signalingClientStats.numberOfErrors);
    DLOGD("Number of runtime errors in the session: %d", pSignalingClientMetrics->signalingClientStats.numberOfRuntimeErrors);
    DLOGD("Signaling client uptime: %" PRIu64 " ms",
          (pSignalingClientMetrics->signalingClientStats.connectionDuration / HUNDREDS_OF_NANOS_IN_A_MILLISECOND));
    // This gives the EMA of the createChannel, describeChannel, getChannelEndpoint and deleteChannel calls
    DLOGD("Control Plane API call latency: %" PRIu64 " ms",
          (pSignalingClientMetrics->signalingClientStats.cpApiCallLatency / HUNDREDS_OF_NANOS_IN_A_MILLISECOND));
    // This gives the EMA of the getIceConfig() call.
    DLOGD("Data Plane API call latency: %" PRIu64 " ms",
          (pSignalingClientMetrics->signalingClientStats.dpApiCallLatency / HUNDREDS_OF_NANOS_IN_A_MILLISECOND));
CleanUp:
    LEAVES();
    return retStatus;
}
