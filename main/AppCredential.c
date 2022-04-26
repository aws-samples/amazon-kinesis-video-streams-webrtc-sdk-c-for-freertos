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
#define LOG_CLASS "AppCredential"
#include "AppCredential.h"
#include "stack_queue.h"
#include "directory.h"
#include "iot_credential_provider.h"
#include "static_credential_provider.h"

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/
static STATUS traverseDirectoryPEMFileScan(UINT64 userData, DIR_ENTRY_TYPES entryType, PCHAR fullPath, PCHAR fileName)
{
    UNUSED_PARAM(entryType);
    UNUSED_PARAM(fullPath);

    PCHAR certName = (PCHAR) userData;
    UINT32 fileNameLen = STRLEN(fileName);

    if (fileNameLen > ARRAY_SIZE(APP_CA_CERT_PEM_FILE_EXTENSION) + 1 &&
        (STRCMPI(APP_CA_CERT_PEM_FILE_EXTENSION, &fileName[fileNameLen - ARRAY_SIZE(APP_CA_CERT_PEM_FILE_EXTENSION) + 1]) == 0)) {
        certName[0] = FPATHSEPARATOR;
        certName++;
        STRCPY(certName, fileName);
    }

    return STATUS_SUCCESS;
}

STATUS searchSslCert(PAppCredential pAppCredential)
{
    STATUS retStatus = STATUS_SUCCESS;
    struct stat pathStat;
    CHAR certName[MAX_PATH_LEN];

    CHK(pAppCredential != NULL, STATUS_APP_CREDENTIAL_NULL_ARG);
    MEMSET(certName, 0x0, ARRAY_SIZE(certName));
    pAppCredential->pCaCertPath = GETENV(CACERT_PATH_ENV_VAR);

    // if ca cert path is not set from the environment, try to use the one that cmake detected
    // if ca cert path is not set from the environment, try to use the one that cmake detected
    if (pAppCredential->pCaCertPath == NULL) {
        CHK_ERR(STRNLEN(DEFAULT_KVS_CACERT_PATH, MAX_PATH_LEN) > 0, STATUS_INVALID_OPERATION, "No ca cert path given (error:%s)", strerror(errno));
        pAppCredential->pCaCertPath = DEFAULT_KVS_CACERT_PATH;
    } else {
        CHK(pAppCredential->pCaCertPath != NULL, STATUS_APP_CREDENTIAL_MISS_CACERT_PATH);
        // Check if the environment variable is a path
        CHK(0 == FSTAT(pAppCredential->pCaCertPath, &pathStat), STATUS_APP_CREDENTIAL_INVALID_CACERT_PATH);

        if (S_ISDIR(pathStat.st_mode)) {
            CHK_STATUS((directory_traverse(pAppCredential->pCaCertPath, (UINT64) &certName, /* iterate */ FALSE, traverseDirectoryPEMFileScan)));
            CHK(certName[0] != 0x0, STATUS_APP_CREDENTIAL_CACERT_NOT_FOUND);
            STRCAT(pAppCredential->pCaCertPath, certName);
        }
    }

CleanUp:
    CHK_LOG_ERR((retStatus));
    return retStatus;
}

STATUS app_credential_generateCertRoutine(PAppCredential pAppCredential)
{
    STATUS retStatus = STATUS_SUCCESS;
    BOOL locked = FALSE;
    UINT32 certCount;
    PRtcCertificate pRtcCertificate = NULL;

    CHK(pAppCredential != NULL, STATUS_APP_CREDENTIAL_NULL_ARG);

    MUTEX_LOCK(pAppCredential->generateCertLock);
    locked = TRUE;

    // Quick check if there is anything that needs to be done.
    CHK_STATUS((stack_queue_getCount(pAppCredential->generatedCertificates, &certCount)));
    CHK(certCount <= MAX_RTCCONFIGURATION_CERTIFICATES, retStatus);

    // Generate the certificate with the keypair
    CHK(rtc_certificate_create(&pRtcCertificate) == STATUS_SUCCESS, STATUS_APP_CREDENTIAL_CERT_CREATE);

    // Add to the stack queue
    CHK(stack_queue_enqueue(pAppCredential->generatedCertificates, (UINT64) pRtcCertificate) == STATUS_SUCCESS, STATUS_APP_CREDENTIAL_CERT_STACK);

    DLOGD("New certificate has been pre-generated and added to the queue");

    // Reset it so it won't be freed on exit
    pRtcCertificate = NULL;

    MUTEX_UNLOCK(pAppCredential->generateCertLock);
    locked = FALSE;

CleanUp:

    if (pRtcCertificate != NULL) {
        rtc_certificate_free(pRtcCertificate);
    }

    if (locked) {
        MUTEX_UNLOCK(pAppCredential->generateCertLock);
    }

    return retStatus;
}

STATUS popGeneratedCert(PAppCredential pAppCredential, PRtcCertificate* ppRtcCertificate)
{
    STATUS retStatus = STATUS_SUCCESS;
    BOOL locked = FALSE;
    UINT64 data;
    PRtcCertificate pRtcCertificate = NULL;

    CHK((pAppCredential != NULL) && (ppRtcCertificate != NULL), STATUS_APP_CREDENTIAL_NULL_ARG);

    *ppRtcCertificate = NULL;

    MUTEX_LOCK(pAppCredential->generateCertLock);
    locked = TRUE;
    // Check if we have any pregenerated certs and use them
    // NOTE: We are running under the config lock
    retStatus = stack_queue_dequeue(pAppCredential->generatedCertificates, &data);
    CHK(retStatus == STATUS_SUCCESS || retStatus == STATUS_NOT_FOUND, retStatus);

    if (retStatus == STATUS_NOT_FOUND) {
        retStatus = STATUS_SUCCESS;
    } else {
        // Use the pre-generated cert and get rid of it to not reuse again
        pRtcCertificate = (PRtcCertificate) data;
    }

    *ppRtcCertificate = pRtcCertificate;
    MUTEX_UNLOCK(pAppCredential->generateCertLock);
    locked = FALSE;

CleanUp:
    if (locked) {
        MUTEX_UNLOCK(pAppCredential->generateCertLock);
    }
    return retStatus;
}

STATUS app_credential_create(PAppCredential pAppCredential)
{
    STATUS retStatus = STATUS_SUCCESS;
    PCHAR pAccessKey, pSecretKey, pSessionToken;
    PCHAR pIotCoreCredentialEndPoint, pIotCoreCert, pIotCorePrivateKey, pIotCoreRoleAlias, pIotCoreThingName;
    PCHAR pEcsToken, pEcsCredentialFullUri;

    CHK(pAppCredential != NULL, STATUS_APP_CREDENTIAL_NULL_ARG);
    pAppCredential->credentialType = APP_CREDENTIAL_TYPE_NA;
    pAppCredential->pCredentialProvider = NULL;
    pAppCredential->generateCertLock = INVALID_MUTEX_VALUE;
    pAppCredential->generatedCertificates = NULL;

    CHK_STATUS((searchSslCert(pAppCredential)));

    if (((pAccessKey = GETENV(ACCESS_KEY_ENV_VAR)) != NULL) && ((pSecretKey = GETENV(SECRET_KEY_ENV_VAR)) != NULL)) {
        pSessionToken = GETENV(SESSION_TOKEN_ENV_VAR);
        CHK(static_credential_provider_create(pAccessKey, 0, pSecretKey, 0, pSessionToken, 0, MAX_UINT64, &pAppCredential->pCredentialProvider) ==
                STATUS_SUCCESS,
            STATUS_APP_CREDENTIAL_ALLOCATE_STATIC);
        pAppCredential->credentialType = APP_CREDENTIAL_TYPE_STATIC;
    } else if (((pIotCoreThingName = GETENV(APP_IOT_CORE_THING_NAME)) != NULL) &&
               ((pIotCoreCredentialEndPoint = GETENV(APP_IOT_CORE_CREDENTIAL_ENDPOINT)) != NULL) && ((pIotCoreCert = GETENV(APP_IOT_CORE_CERT))) &&
               ((pIotCorePrivateKey = GETENV(APP_IOT_CORE_PRIVATE_KEY)) != NULL) && ((pIotCoreRoleAlias = GETENV(APP_IOT_CORE_ROLE_ALIAS)) != NULL)) {
        CHK(iot_credential_provider_create(pIotCoreCredentialEndPoint, pIotCoreCert, pIotCorePrivateKey, pAppCredential->pCaCertPath,
                                           pIotCoreRoleAlias, pIotCoreThingName, &pAppCredential->pCredentialProvider) == STATUS_SUCCESS,
            STATUS_APP_CREDENTIAL_ALLOCATE_IOT);
        pAppCredential->credentialType = APP_CREDENTIAL_TYPE_IOT_CERT;
    } else {
        CHK(FALSE, STATUS_APP_CREDENTIAL_ALLOCATE_NA);
    }

    pAppCredential->generateCertLock = MUTEX_CREATE(FALSE);
    CHK(IS_VALID_MUTEX_VALUE(pAppCredential->generateCertLock), STATUS_APP_CREDENTIAL_INVALID_MUTEX);
    CHK(stack_queue_create(&pAppCredential->generatedCertificates) == STATUS_SUCCESS, STATUS_APP_CREDENTIAL_PREGENERATED_CERT_QUEUE);

CleanUp:

    if (STATUS_FAILED(retStatus)) {
        if (pAppCredential != NULL) {
            app_credential_destroy(pAppCredential);
        }
    }

    return retStatus;
}

STATUS app_credential_destroy(PAppCredential pAppCredential)
{
    STATUS retStatus = STATUS_SUCCESS;
    StackQueueIterator iterator;
    UINT64 data;
    CHK(pAppCredential != NULL, STATUS_APP_CREDENTIAL_NULL_ARG);

    if (pAppCredential->generatedCertificates != NULL) {
        stack_queue_iterator_get(pAppCredential->generatedCertificates, &iterator);
        while (IS_VALID_ITERATOR(iterator)) {
            stack_queue_iterator_getItem(iterator, &data);
            stack_queue_iterator_getNext(&iterator);
            rtc_certificate_free((PRtcCertificate) data);
        }

        CHK_LOG_ERR((stack_queue_clear(pAppCredential->generatedCertificates, FALSE)));
        CHK_LOG_ERR((stack_queue_free(pAppCredential->generatedCertificates)));
        pAppCredential->generatedCertificates = NULL;
    }

    if (pAppCredential->credentialType == APP_CREDENTIAL_TYPE_STATIC) {
        CHK(static_credential_provider_free(&pAppCredential->pCredentialProvider) == STATUS_SUCCESS, STATUS_APP_CREDENTIAL_DESTROY_STATIC);
    } else if (pAppCredential->credentialType == APP_CREDENTIAL_TYPE_IOT_CERT) {
        CHK(iot_credential_provider_free(&pAppCredential->pCredentialProvider) == STATUS_SUCCESS, STATUS_APP_CREDENTIAL_DESTROY_IOT);
    } else {
        retStatus = STATUS_APP_CREDENTIAL_DESTROY_NA;
    }

    if (IS_VALID_MUTEX_VALUE(pAppCredential->generateCertLock)) {
        MUTEX_FREE(pAppCredential->generateCertLock);
        pAppCredential->generateCertLock = INVALID_MUTEX_VALUE;
    }

CleanUp:
    if (pAppCredential != NULL) {
        pAppCredential->credentialType = APP_CREDENTIAL_TYPE_NA;
    }
    return retStatus;
}
