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
#ifndef __KINESIS_VIDEO_WEBRTC_APP_SIGNALING_INCLUDE__
#define __KINESIS_VIDEO_WEBRTC_APP_SIGNALING_INCLUDE__

#ifdef __cplusplus
extern "C" {
#endif
#include <kvs/webrtc_client.h>
#include "AppConfig.h"
#include "AppError.h"
#include "AppCredential.h"

typedef struct __AppSignaling {
    PAppCredential pAppCredential; //!< the context of credential
    SIGNALING_CLIENT_HANDLE signalingClientHandle;
    SignalingClientCallbacks signalingClientCallbacks;
    ChannelInfo channelInfo;
    SignalingClientInfo clientInfo;
    MUTEX signalingSendMessageLock; //!< per signaling client
    BOOL useTurn;
} AppSignaling, *PAppSignaling;
/**
 * @brief   initialize the context of app signaling
 *
 * @param[in] pAppSignaling the context of appSignaling
 * @param[in] onMessageReceived the callback of receiving the signaling messages
 * @param[in] onStateChanged the callback of the state
 * @param[in] pOnError the callback of error
 * @param[in] udata the user data for these callbacks
 * @param[in] useTurn use the turn server or not
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS app_signaling_init(PAppSignaling pAppSignaling, SignalingClientMessageReceivedFunc onMessageReceived,
                          SignalingClientStateChangedFunc onStateChanged, SignalingClientErrorReportFunc pOnError, UINT64 udata, BOOL useTurn);
/**
 * @brief   initialize the context of app signaling
 *
 * @param[in] pAppSignaling the context of appSignaling
 *
 * @return the role type of signaling channel
 */
SIGNALING_CHANNEL_ROLE_TYPE app_signaling_getRole(PAppSignaling pAppSignaling);
/**
 * @brief   query the information of stun/turn servers
 *
 * @param[in] pAppSignaling the context of appSignaling
 * @param[in] pIceServer the structure of ice servers
 * @param[in] pServerNum the number of servers
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS app_signaling_queryServer(PAppSignaling pAppSignaling, PRtcIceServer pIceServer, PUINT32 pServerNum);
/**
 * @brief   connect to the signaling server
 *
 * @param[in] pAppSignaling the context of appSignaling
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success
 */
STATUS app_signaling_connect(PAppSignaling pAppSignaling);
STATUS app_signaling_fetch(PAppSignaling pAppSignaling);

STATUS app_signaling_create(PAppSignaling pAppSignaling);

STATUS app_signaling_shutdown(PAppSignaling pAppSignaling);
/**
 * @brief   connect to the signaling server
 *
 * @param[in] pAppSignaling the context of appSignaling
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success
 */
STATUS app_signaling_check(PAppSignaling pAppSignaling);
STATUS app_signaling_sendMsg(PAppSignaling pAppSignaling, PSignalingMessage pMessage);
STATUS app_signaling_restart(PAppSignaling pAppSignaling);
STATUS app_signaling_free(PAppSignaling pAppSignaling);

#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_WEBRTC_APP_SIGNALING_INCLUDE__ */
