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
#define LOG_CLASS "AppDataChannel"
#include "AppDataChannel.h"
#include "AppCommon.h"

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

static VOID onDataChannelMessage(UINT64 userData, PRtcDataChannel pDataChannel, BOOL isBinary, PBYTE pMessage, UINT32 pMessageLen)
{
    PStreamingSession session = (PStreamingSession) userData;
    UNUSED_PARAM(pDataChannel);
    if (isBinary) {
        DLOGI("DataChannel Binary Message");
    } else {
        // DLOGI("DataChannel String Message: %.*s\n", pMessageLen, pMessage);
        char c = *(pMessage + pMessageLen - 1);
        *(pMessage + pMessageLen - 1) = 0;
        printf("DataChannel String Message: %s%c\n\r", pMessage, c);
        *(pMessage + pMessageLen - 1) = c;
        // master echo
        data_channel_send(session->pRtcDataChannel, isBinary, pMessage, pMessageLen);
    }
}

VOID onDataChannel(UINT64 userData, PRtcDataChannel pRtcDataChannel)
{
    DLOGI("New DataChannel has been opened %s \n", pRtcDataChannel->name);
    data_channel_onMessage(pRtcDataChannel, userData, onDataChannelMessage);
}
#endif
