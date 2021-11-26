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
#define LOG_CLASS "AppMessageQueue"
#include "AppMessageQueue.h"

STATUS createPendingMsgQ(PConnectionMsgQ pConnectionMsgQ, UINT64 hashValue, PPendingMessageQueue* ppPendingMessageQueue)
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
    CHK(stackQueueCreate(&pPendingMessageQueue->messageQueue) == STATUS_SUCCESS, STATUS_APP_MSGQ_CREATE_PENDING_MSQ);
    CHK(stackQueueEnqueue(pConnection, (UINT64) pPendingMessageQueue) == STATUS_SUCCESS, STATUS_APP_MSGQ_PUSH_CONN_MSQ);

CleanUp:
    if (STATUS_FAILED(retStatus) && pPendingMessageQueue != NULL) {
        freePendingMsgQ(pPendingMessageQueue);
        pPendingMessageQueue = NULL;
    }

    if (ppPendingMessageQueue != NULL) {
        *ppPendingMessageQueue = pPendingMessageQueue;
    }

    return retStatus;
}

STATUS pushMsqIntoPendingMsgQ(PPendingMessageQueue pPendingMsgQ, PReceivedSignalingMessage pMsg)
{
    STATUS retStatus = STATUS_SUCCESS;
    PReceivedSignalingMessage pReceivedSignalingMessageCopy = NULL;

    CHK((pPendingMsgQ != NULL) && (pMsg != NULL), STATUS_APP_MSGQ_NULL_ARG);
    CHK(NULL != (pReceivedSignalingMessageCopy = (PReceivedSignalingMessage) MEMCALLOC(1, SIZEOF(ReceivedSignalingMessage))),
        STATUS_APP_MSGQ_NOT_ENOUGH_MEMORY);
    *pReceivedSignalingMessageCopy = *pMsg;
    CHK(stackQueueEnqueue(pPendingMsgQ->messageQueue, (UINT64) pReceivedSignalingMessageCopy) == STATUS_SUCCESS, STATUS_APP_MSGQ_PUSH_PENDING_MSQ);

CleanUp:
    if (STATUS_FAILED(retStatus)) {
        SAFE_MEMFREE(pReceivedSignalingMessageCopy);
    }

    return retStatus;
}

STATUS handlePendingMsgQ(PPendingMessageQueue pPendingMsgQ, MsgHandleHook msgHandleHook, PVOID uData)
{
    STATUS retStatus = STATUS_SUCCESS;
    BOOL isEmpty = FALSE;
    PStackQueue pMessageQueue = NULL;
    PReceivedSignalingMessage pReceivedSignalingMessage = NULL;
    UINT64 hashValue;

    CHK(pPendingMsgQ != NULL, STATUS_APP_MSGQ_NULL_ARG);
    pMessageQueue = pPendingMsgQ->messageQueue;

    do {
        CHK(stackQueueIsEmpty(pMessageQueue, &isEmpty) == STATUS_SUCCESS, STATUS_APP_MSGQ_EMPTY_PENDING_MSQ);
        if (!isEmpty) {
            hashValue = 0;
            CHK(stackQueueDequeue(pMessageQueue, &hashValue) == STATUS_SUCCESS, STATUS_APP_MSGQ_POP_PENDING_MSQ);
            pReceivedSignalingMessage = (PReceivedSignalingMessage) hashValue;
            if (msgHandleHook != NULL) {
                CHK(msgHandleHook(uData, &pReceivedSignalingMessage->signalingMessage) == STATUS_SUCCESS, STATUS_APP_MSGQ_HANDLE_PENDING_MSQ);
            }
            MEMFREE(pReceivedSignalingMessage);
            pReceivedSignalingMessage = NULL;
        }
    } while (!isEmpty);

CleanUp:
    freePendingMsgQ(pPendingMsgQ);
    SAFE_MEMFREE(pReceivedSignalingMessage);
    CHK_LOG_ERR((retStatus));
    return retStatus;
}

STATUS freePendingMsgQ(PPendingMessageQueue pPendingMessageQueue)
{
    STATUS retStatus = STATUS_SUCCESS;

    // free is idempotent
    CHK(pPendingMessageQueue != NULL, STATUS_APP_MSGQ_NULL_ARG);

    if (pPendingMessageQueue->messageQueue != NULL) {
        stackQueueClear(pPendingMessageQueue->messageQueue, TRUE);
        stackQueueFree(pPendingMessageQueue->messageQueue);
    }

    MEMFREE(pPendingMessageQueue);

CleanUp:
    return retStatus;
}

STATUS createConnectionMsqQ(PConnectionMsgQ* ppConnectionMsgQ)
{
    STATUS retStatus = STATUS_SUCCESS;
    PConnectionMsgQ pConnectionMsgQ = NULL;

    CHK(ppConnectionMsgQ != NULL, STATUS_APP_MSGQ_NULL_ARG);
    pConnectionMsgQ = MEMCALLOC(1, SIZEOF(ConnectionMsgQ));
    CHK(pConnectionMsgQ != NULL, STATUS_APP_MSGQ_NOT_ENOUGH_MEMORY);
    CHK(stackQueueCreate(&pConnectionMsgQ->pMsqQueue) == STATUS_SUCCESS, STATUS_APP_MSGQ_CREATE_CONN_MSQ);

CleanUp:

    if (STATUS_FAILED(retStatus) && pConnectionMsgQ != NULL) {
        freeConnectionMsgQ(&pConnectionMsgQ);
    }

    if (ppConnectionMsgQ != NULL) {
        *ppConnectionMsgQ = pConnectionMsgQ;
    }

    return retStatus;
}

STATUS getPendingMsgQByHashVal(PConnectionMsgQ pConnectionMsgQ, UINT64 clientHash, BOOL remove, PPendingMessageQueue* ppPendingMsgQ)
{
    STATUS retStatus = STATUS_SUCCESS;
    PStackQueue pConnections = NULL;
    PPendingMessageQueue pPendingMsgQ = NULL;

    StackQueueIterator iterator;
    UINT64 data;
    BOOL iterate = TRUE;

    CHK((pConnectionMsgQ != NULL) && (ppPendingMsgQ != NULL), STATUS_APP_MSGQ_NULL_ARG);

    pConnections = pConnectionMsgQ->pMsqQueue;

    CHK_STATUS((stackQueueGetIterator(pConnections, &iterator)));
    while (iterate && IS_VALID_ITERATOR(iterator)) {
        CHK_STATUS((stackQueueIteratorGetItem(iterator, &data)));
        CHK_STATUS((stackQueueIteratorNext(&iterator)));

        pPendingMsgQ = (PPendingMessageQueue) data;
        if (clientHash == pPendingMsgQ->hashValue) {
            *ppPendingMsgQ = pPendingMsgQ;
            iterate = FALSE;

            // Check if the item needs to be removed
            if (remove) {
                // This is OK to do as we are terminating the iterator anyway
                CHK_STATUS((stackQueueRemoveItem(pConnections, data)));
            }
        }
    }

CleanUp:

    if (ppPendingMsgQ != NULL) {
        *ppPendingMsgQ = pPendingMsgQ;
    }

    return retStatus;
}

STATUS removeExpiredPendingMsgQ(PConnectionMsgQ pConnectionMsgQ, UINT64 interval)
{
    STATUS retStatus = STATUS_SUCCESS;
    PStackQueue pConnection = NULL;
    PPendingMessageQueue pPendingMessageQueue = NULL;
    UINT32 i, count;
    UINT64 data, curTime;

    CHK(pConnectionMsgQ != NULL, STATUS_APP_MSGQ_NULL_ARG);
    pConnection = pConnectionMsgQ->pMsqQueue;

    curTime = GETTIME();
    CHK_STATUS((stackQueueGetCount(pConnection, &count)));

    // Dequeue and enqueue in order to not break the iterator while removing an item
    for (i = 0; i < count; i++) {
        CHK_STATUS((stackQueueDequeue(pConnection, &data)));

        // Check for expiry
        pPendingMessageQueue = (PPendingMessageQueue) data;
        if (pPendingMessageQueue->createTime + interval < curTime) {
            // Message queue has expired and needs to be freed
            freePendingMsgQ(pPendingMessageQueue);
        } else {
            // Enqueue back again as it's still valued
            CHK_STATUS((stackQueueEnqueue(pConnection, data)));
        }
    }

CleanUp:

    return retStatus;
}

STATUS freeConnectionMsgQ(PConnectionMsgQ* ppConnectionMsgQ)
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
    stackQueueGetIterator(pConnection, &iterator);
    while (IS_VALID_ITERATOR(iterator)) {
        stackQueueIteratorGetItem(iterator, &data);
        stackQueueIteratorNext(&iterator);
        freePendingMsgQ((PPendingMessageQueue) data);
    }

    stackQueueClear(pConnection, FALSE);
    stackQueueFree(pConnection);
    pConnection = NULL;

CleanUp:

    SAFE_MEMFREE(pConnectionMsgQ);
    if (ppConnectionMsgQ != NULL) {
        *ppConnectionMsgQ = pConnectionMsgQ;
    }
    return retStatus;
}
