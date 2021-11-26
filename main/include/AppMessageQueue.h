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
#ifndef __KINESIS_VIDEO_WEBRTC_APP_MESSAGE_QUEUE_INCLUDE__
#define __KINESIS_VIDEO_WEBRTC_APP_MESSAGE_QUEUE_INCLUDE__

#ifdef __cplusplus
extern "C" {
#endif
#include <com/amazonaws/kinesis/video/webrtcclient/Include.h>
#include "AppConfig.h"
#include "AppError.h"

typedef STATUS (*MsgHandleHook)(PVOID udata, PSignalingMessage pSignalingMessage);

typedef struct {
    UINT64 hashValue;
    UINT64 createTime;
    PStackQueue messageQueue;
    MsgHandleHook msgHandleHook;
    PVOID uData;
} PendingMessageQueue, *PPendingMessageQueue;

typedef struct {
    PStackQueue pMsqQueue;
} ConnectionMsgQ, *PConnectionMsgQ;
/**
 * @brief create the pending message queue for the connection.
 *
 * @param[in] pConnectionMsgQ the context of the connection message queue.
 * @param[in] hashValue the hashvalue of this pending message queue.
 * @param[in, out] ppPendingMessageQueue  the context of this pending message queue.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS createPendingMsgQ(PConnectionMsgQ pConnectionMsgQ, UINT64 hashValue, PPendingMessageQueue* ppPendingMessageQueue);
/**
 * @brief push message into the pending message queue.
 *
 * @param[in] pPendingMsgQ the context of the pending message queue.
 * @param[in] pMsg the buffer of the signaling message.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS pushMsqIntoPendingMsgQ(PPendingMessageQueue pPendingMsgQ, PReceivedSignalingMessage pMsg);
/**
 * @brief   This api handles all the pending message but you need to pop pending message queue from connection message queue.
 *          It means you need to use this api with getPendingMsgQByHashVal().
 *
 * @param[in] pPendingMsgQ the context of the pending message queue.
 * @param[in] msgHandleHook the callback of handling the pending messages.
 * @param[in] uData the context of user data.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS handlePendingMsgQ(PPendingMessageQueue pPendingMsgQ, MsgHandleHook msgHandleHook, PVOID uData);
/**
 * @brief free the pending message queue.
 *
 * @param[in] pPendingMessageQueue the context of the pending message queue.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS freePendingMsgQ(PPendingMessageQueue pPendingMessageQueue);
/**
 * @brief create connection queue.
 *
 * @param[in] pConnectionMsgQ the context of the connnection message queue.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS createConnectionMsqQ(PConnectionMsgQ* ppConnectionMsgQ);
/**
 * @brief get the target pending message queue according to the clientHash
 *
 * @param[in] pConnectionMsgQ the context of the connnection message queue.
 * @param[in] clientHash the hash value of the pending message queue.
 * @param[in] remove remove the target pending message queue.
 * @param[in, out]  ppPendingMsgQ the context of the pending message queue.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS getPendingMsgQByHashVal(PConnectionMsgQ pConnectionMsgQ, UINT64 clientHash, BOOL remove, PPendingMessageQueue* ppPendingMsgQ);
/**
 * @brief remove all the expired pending message queues.
 *
 * @param[in] pConnectionMsgQ the context of connection message queue.
 * @param[in] interval the expired interval
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS removeExpiredPendingMsgQ(PConnectionMsgQ pConnectionMsgQ, UINT64 interval);
/**
 * @brief freee the connection queues.
 *
 * @param[in] pConnectionMsgQ the context of connection message queue.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS freeConnectionMsgQ(PConnectionMsgQ* ppConnectionMsgQ);

#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_WEBRTC_APP_MESSAGE_QUEUE_INCLUDE__ */
