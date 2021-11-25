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
#ifndef __KINESIS_VIDEO_WEBRTC_APP_CREDENTIAL_INCLUDE__
#define __KINESIS_VIDEO_WEBRTC_APP_CREDENTIAL_INCLUDE__

#ifdef __cplusplus
extern "C" {
#endif
#include <com/amazonaws/kinesis/video/webrtcclient/Include.h>
#include "AppConfig.h"
#include "AppError.h"

typedef enum { APP_CREDENTIAL_TYPE_NA = 0, APP_CREDENTIAL_TYPE_STATIC, APP_CREDENTIAL_TYPE_IOT_CERT, APP_CREDENTIAL_TYPE_ECS } AppCredentialType;

typedef struct {
    AppCredentialType credentialType;           //!< the type of app credential.
    PCHAR pCaCertPath;                          //!< the path of rootCA.
    PAwsCredentialProvider pCredentialProvider; //!< the handler of aws credential provider.
    MUTEX generateCertLock;                     //!< the lock for the access of generated cert.
    PStackQueue generatedCertificates;          // Max MAX_RTCCONFIGURATION_CERTIFICATES certificates.
} AppCredential, *PAppCredential;
/**
 * @brief search the ssl cert according to the environmental variable.
 *
 * @param[in] pAppCredential the context of the app credential.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS searchSslCert(PAppCredential pAppCredential);
/**
 * @brief the routine of generating the cert, and push the generated cert into the queue.
 *
 * @param[in] pAppCredential the context of the app credential.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS generateCertRoutine(PAppCredential pAppCredential);
/**
 * @brief pop the generated cert out of the queue.
 *
 * @param[in] pAppCredential the context of the app credential.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS popGeneratedCert(PAppCredential pAppCredential, PRtcCertificate* ppRtcCertificate);
/**
 * @brief create the app credential according to environmental variables.
 *
 * @param[in] pAppCredential the context of the app credential.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS createCredential(PAppCredential pAppCredential);
/**
 * @brief destroy the app credential.
 *
 * @param[in] pAppCredential the context of the app credential.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS destroyCredential(PAppCredential pAppCredential);

#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_WEBRTC_APP_CREDENTIAL_INCLUDE__ */
