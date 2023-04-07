/*
 *  Entropy accumulator implementation
 *
 *  Copyright (C) 2006-2016, ARM Limited, All Rights Reserved
 *  SPDX-License-Identifier: Apache-2.0
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you may
 *  not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  This file is part of mbed TLS (https://tls.mbed.org)
 */

#if !defined(MBEDTLS_CONFIG_FILE)
#include "mbedtls/config.h"
#else
#include MBEDTLS_CONFIG_FILE
#endif

#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "mbedtls/entropy_poll.h"

#if defined(MBEDTLS_ENTROPY_C)

#if defined(MBEDTLS_ENTROPY_HARDWARE_ALT)


static int get_random_bytes(void *buf, unsigned int len)
{
    unsigned int ranbuf;
    unsigned int *lp;
    int i, count;
    count = len / sizeof(unsigned int);
    lp = (unsigned int *) buf;

    for (i = 0; i < count; i ++) {
        lp[i] = rand();
        len -= sizeof(unsigned int);
    }

    if (len > 0) {
        ranbuf = rand();
        memcpy(&lp[i], &ranbuf, len);
    }
    return 0;
}

int mbedtls_hardware_poll( void *data,
                           unsigned char *output, size_t len, size_t *olen )
{
    (void)data;
    get_random_bytes(output, len);
    *olen = len;
    return 0;
}
#endif /* MBEDTLS_ENTROPY_HARDWARE_ALT */

#endif /* MBEDTLS_ENTROPY_C */
