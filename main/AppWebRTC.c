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
#define LOG_CLASS "AppWebRTC"
#include "AppWebRTC.h"

STATUS initWebRtc(PAppConfiguration pAppConfiguration)
{
    STATUS retStatus = STATUS_SUCCESS;
    retStatus = initKvsWebRtc();
    if (retStatus != STATUS_SUCCESS) {
        retStatus = STATUS_APP_WEBRTC_INIT;
    }
    return retStatus;
}

STATUS deinitWebRtc(PAppConfiguration pAppConfiguration)
{
    STATUS retStatus = STATUS_SUCCESS;
    retStatus = deinitKvsWebRtc();
    if (retStatus != STATUS_SUCCESS) {
        retStatus = STATUS_APP_WEBRTC_DEINIT;
    }
    return retStatus;
}
