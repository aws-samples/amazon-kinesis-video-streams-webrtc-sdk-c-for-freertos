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
#define LOG_CLASS "AppMain"
#include "AppCommon.h"
#include "instrumented_allocators.h"

INT32 WebRTCAppMain(INT32 argc, CHAR* argv[])
{
    STATUS retStatus = STATUS_SUCCESS;
    PAppConfiguration pAppConfiguration = NULL;
    SET_INSTRUMENTED_ALLOCATORS();

    printf("[WebRTC] Starting\n");

    retStatus = initApp(TRUE, TRUE, &pAppConfiguration);
    if (retStatus != STATUS_SUCCESS) {
        printf("[WebRTC] initApp(): operation returned status code: 0x%08x \n", retStatus);
        goto CleanUp;
    }

    retStatus = runApp(pAppConfiguration);
    if (retStatus != STATUS_SUCCESS) {
        printf("[WebRTC] runApp(): operation returned status code: 0x%08x \n", retStatus);
    }

    // Checking for termination
    retStatus = pollApp(pAppConfiguration);
    if (retStatus != STATUS_SUCCESS) {
        printf("[WebRTC] pollApp(): operation returned status code: 0x%08x \n", retStatus);
        goto CleanUp;
    }
    printf("[WebRTC] streaming session terminated\n");

CleanUp:

    if (retStatus != STATUS_SUCCESS) {
        printf("[WebRTC] terminated with status code 0x%08x \n", retStatus);
    }

    printf("[WebRTC] cleaning up....\n");

    if (pAppConfiguration != NULL) {
        retStatus = freeApp(&pAppConfiguration);
        if (retStatus != STATUS_SUCCESS) {
            printf("[WebRTC] freeApp(): operation returned status code: 0x%08x \n", retStatus);
        }
    }
    printf("[WebRTC] cleanup done\n");

    RESET_INSTRUMENTED_ALLOCATORS();
    // https://www.gnu.org/software/libc/manual/html_node/Exit-Status.html
    // We can only return with 0 - 127. Some platforms treat exit code >= 128
    // to be a success code, which might give an unintended behaviour.
    // Some platforms also treat 1 or 0 differently, so it's better to use
    // EXIT_FAILURE and EXIT_SUCCESS macros for portability.
    return STATUS_FAILED(retStatus) ? EXIT_FAILURE : EXIT_SUCCESS;
}
