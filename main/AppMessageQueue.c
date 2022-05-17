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
/******************************************************************************
 * HEADERS
 ******************************************************************************/
#define LOG_CLASS "AppMessageQueue"
#include "AppMessageQueue.h"

/******************************************************************************
 * DEFINITIONS
 ******************************************************************************/
/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
STATUS app_msg_q_createPendingMsgQ(PConnectionMsgQ pConnectionMsgQ, UINT64 hashValue, PPendingMessageQueue* ppPendingMessageQueue)
{
    STATUS retStatus = STATUS_SUCCESS;
    PStackQueue pConnection = NULL;
    PPendingMessageQueue pPendingMessageQueue = NULL;

    CHK((pConnectionMsgQ != NULL) && (ppPendingMessageQueue != NULL), STATUS_APP_MSGQ_NULL_ARG);
    pConnection = pConnectionMsgQ->pMsqQueue;
    CHK(pConnection != NULL, STATUS_APP_MSGQ_NULL_ARG);

    CHK(NULL != (pPendingMessageQueue = (PPendingMessageQueue) MEMCALLOC(1, SIZEOF(PendingMessageQueue))), STATUS_APP_MSGQ_NOT_ENOUGH_MEMORY);
    pPendingMessageQueue->hashValue = hashValue;
    pPendingMessageQueue->createTime = GETTIME();
    CHK(stack_queue_create(&pPendingMessageQueue->messageQueue) == STATUS_SUCCESS, STATUS_APP_MSGQ_CREATE_PENDING_MSQ);
    CHK(stack_queue_enqueue(pConnection, (UINT64) pPendingMessageQueue) == STATUS_SUCCESS, STATUS_APP_MSGQ_PUSH_CONN_MSQ);

CleanUp:
    if (STATUS_FAILED(retStatus) && pPendingMessageQueue != NULL) {
        app_msg_q_freePendingMsgQ(pPendingMessageQueue);
        pPendingMessageQueue = NULL;
    }

    if (ppPendingMessageQueue != NULL) {
        *ppPendingMessageQueue = pPendingMessageQueue;
    }

    return retStatus;
}

STATUS app_msg_q_pushMsqIntoPendingMsgQ(PPendingMessageQueue pPendingMsgQ, PReceivedSignalingMessage pMsg)
{
    STATUS retStatus = STATUS_SUCCESS;
    PReceivedSignalingMessage pReceivedSignalingMessageCopy = NULL;

    CHK((pPendingMsgQ != NULL) && (pMsg != NULL), STATUS_APP_MSGQ_NULL_ARG);
    CHK(NULL != (pReceivedSignalingMessageCopy = (PReceivedSignalingMessage) MEMCALLOC(1, SIZEOF(ReceivedSignalingMessage))),
        STATUS_APP_MSGQ_NOT_ENOUGH_MEMORY);
    *pReceivedSignalingMessageCopy = *pMsg;
    CHK(stack_queue_enqueue(pPendingMsgQ->messageQueue, (UINT64) pReceivedSignalingMessageCopy) == STATUS_SUCCESS, STATUS_APP_MSGQ_PUSH_PENDING_MSGQ);

CleanUp:
    if (STATUS_FAILED(retStatus)) {
        SAFE_MEMFREE(pReceivedSignalingMessageCopy);
    }

    return retStatus;
}

STATUS app_msg_q_handlePendingMsgQ(PPendingMessageQueue pPendingMsgQ, MsgHandleHook msgHandleHook, PVOID uData)
{
    STATUS retStatus = STATUS_SUCCESS;
    BOOL isEmpty = FALSE;
    PStackQueue pMessageQueue = NULL;
    PReceivedSignalingMessage pReceivedSignalingMessage = NULL;
    UINT64 pendingMsg;

    CHK(pPendingMsgQ != NULL, STATUS_APP_MSGQ_NULL_ARG);
    pMessageQueue = pPendingMsgQ->messageQueue;

    do {
        CHK(stack_queue_isEmpty(pMessageQueue, &isEmpty) == STATUS_SUCCESS, STATUS_APP_MSGQ_EMPTY_PENDING_MSGQ);
        if (!isEmpty) {
            pendingMsg = 0;
            CHK(stack_queue_dequeue(pMessageQueue, &pendingMsg) == STATUS_SUCCESS, STATUS_APP_MSGQ_POP_PENDING_MSGQ);
            CHK(pendingMsg != NULL, STATUS_APP_MSGQ_NULL_PENDING_MSG);
            pReceivedSignalingMessage = (PReceivedSignalingMessage) pendingMsg;
            if (msgHandleHook != NULL) {
                CHK(msgHandleHook(uData, &pReceivedSignalingMessage->signalingMessage) == STATUS_SUCCESS, STATUS_APP_MSGQ_HANDLE_PENDING_MSG);
            }
            SAFE_MEMFREE(pReceivedSignalingMessage);
        }
    } while (!isEmpty);

CleanUp:
    app_msg_q_freePendingMsgQ(pPendingMsgQ);
    SAFE_MEMFREE(pReceivedSignalingMessage);
    CHK_LOG_ERR((retStatus));
    return retStatus;
}

STATUS app_msg_q_freePendingMsgQ(PPendingMessageQueue pPendingMessageQueue)
{
    STATUS retStatus = STATUS_SUCCESS;

    // free is idempotent
    CHK(pPendingMessageQueue != NULL, STATUS_APP_MSGQ_NULL_ARG);

    if (pPendingMessageQueue->messageQueue != NULL) {
        stack_queue_clear(pPendingMessageQueue->messageQueue, TRUE);
        stack_queue_free(pPendingMessageQueue->messageQueue);
        pPendingMessageQueue->messageQueue = NULL;
    }

    MEMFREE(pPendingMessageQueue);

CleanUp:
    return retStatus;
}

STATUS app_msg_q_createConnectionMsqQ(PConnectionMsgQ* ppConnectionMsgQ)
{
    STATUS retStatus = STATUS_SUCCESS;
    PConnectionMsgQ pConnectionMsgQ = NULL;

    CHK(ppConnectionMsgQ != NULL, STATUS_APP_MSGQ_NULL_ARG);
    pConnectionMsgQ = MEMCALLOC(1, SIZEOF(ConnectionMsgQ));
    CHK(pConnectionMsgQ != NULL, STATUS_APP_MSGQ_NOT_ENOUGH_MEMORY);
    CHK(stack_queue_create(&pConnectionMsgQ->pMsqQueue) == STATUS_SUCCESS, STATUS_APP_MSGQ_CREATE_CONN_MSQ);

CleanUp:

    if (STATUS_FAILED(retStatus) && pConnectionMsgQ != NULL) {
        app_msg_q_freeConnectionMsgQ(&pConnectionMsgQ);
    }

    if (ppConnectionMsgQ != NULL) {
        *ppConnectionMsgQ = pConnectionMsgQ;
    }

    return retStatus;
}

STATUS app_msg_q_getPendingMsgQByHashVal(PConnectionMsgQ pConnectionMsgQ, UINT64 clientHash, BOOL remove, PPendingMessageQueue* ppPendingMsgQ)
{
    STATUS retStatus = STATUS_SUCCESS;
    PStackQueue pConnections = NULL;
    PPendingMessageQueue pPendingMsgQ = NULL;

    StackQueueIterator iterator;
    UINT64 data;
    BOOL iterate = TRUE;

    CHK((pConnectionMsgQ != NULL) && (ppPendingMsgQ != NULL), STATUS_APP_MSGQ_NULL_ARG);

    pConnections = pConnectionMsgQ->pMsqQueue;

    CHK_STATUS((stack_queue_iterator_get(pConnections, &iterator)));
    while (iterate && IS_VALID_ITERATOR(iterator)) {
        CHK_STATUS((stack_queue_iterator_getItem(iterator, &data)));
        CHK_STATUS((stack_queue_iterator_getNext(&iterator)));

        pPendingMsgQ = (PPendingMessageQueue) data;
        if (clientHash == pPendingMsgQ->hashValue) {
            *ppPendingMsgQ = pPendingMsgQ;
            iterate = FALSE;

            // Check if the item needs to be removed
            if (remove) {
                // This is OK to do as we are terminating the iterator anyway
                CHK_STATUS((stack_queue_removeItem(pConnections, data)));
            }
        }
    }

CleanUp:

    if (ppPendingMsgQ != NULL) {
        *ppPendingMsgQ = pPendingMsgQ;
    }

    return retStatus;
}

STATUS app_msg_q_removeExpiredPendingMsgQ(PConnectionMsgQ pConnectionMsgQ, UINT64 interval)
{
    STATUS retStatus = STATUS_SUCCESS;
    PStackQueue pConnection = NULL;
    PPendingMessageQueue pPendingMessageQueue = NULL;
    UINT32 i, count;
    UINT64 data, curTime;

    CHK(pConnectionMsgQ != NULL, STATUS_APP_MSGQ_NULL_ARG);
    pConnection = pConnectionMsgQ->pMsqQueue;

    curTime = GETTIME();
    CHK_STATUS((stack_queue_getCount(pConnection, &count)));

    // Dequeue and enqueue in order to not break the iterator while removing an item
    for (i = 0; i < count; i++) {
        CHK_STATUS((stack_queue_dequeue(pConnection, &data)));

        // Check for expiry
        pPendingMessageQueue = (PPendingMessageQueue) data;
        CHK(pPendingMessageQueue != NULL, STATUS_APP_MSGQ_NULL_PENDING_MSGQ);
        if (pPendingMessageQueue->createTime + interval < curTime) {
            // Message queue has expired and needs to be freed
            app_msg_q_freePendingMsgQ(pPendingMessageQueue);
            DLOGD("Remove expired pending msgQ.");
        } else {
            // Enqueue back again as it's still valued
            CHK_STATUS((stack_queue_enqueue(pConnection, data)));
        }
    }

CleanUp:

    return retStatus;
}

STATUS app_msg_q_freeConnectionMsgQ(PConnectionMsgQ* ppConnectionMsgQ)
{
    STATUS retStatus = STATUS_SUCCESS;
    PConnectionMsgQ pConnectionMsgQ = NULL;
    PStackQueue pConnection = NULL;
    StackQueueIterator iterator;
    UINT64 data;

    CHK(ppConnectionMsgQ != NULL, STATUS_APP_MSGQ_NULL_ARG);
    pConnectionMsgQ = *ppConnectionMsgQ;
    CHK(pConnectionMsgQ != NULL, STATUS_APP_MSGQ_NULL_ARG);
    pConnection = pConnectionMsgQ->pMsqQueue;

    CHK(pConnection != NULL, STATUS_APP_MSGQ_NULL_ARG);

    // Iterate and free all the pending queues
    stack_queue_iterator_get(pConnection, &iterator);
    while (IS_VALID_ITERATOR(iterator)) {
        stack_queue_iterator_getItem(iterator, &data);
        stack_queue_iterator_getNext(&iterator);
        app_msg_q_freePendingMsgQ((PPendingMessageQueue) data);
    }

    stack_queue_clear(pConnection, FALSE);
    stack_queue_free(pConnection);
    pConnection = NULL;

CleanUp:

    SAFE_MEMFREE(pConnectionMsgQ);
    if (ppConnectionMsgQ != NULL) {
        *ppConnectionMsgQ = pConnectionMsgQ;
    }
    return retStatus;
}
