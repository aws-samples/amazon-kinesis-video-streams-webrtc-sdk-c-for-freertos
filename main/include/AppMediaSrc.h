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
#ifndef __AWS_KVS_WEBRTC_APP_MEDIA_SRC_INCLUDE__
#define __AWS_KVS_WEBRTC_APP_MEDIA_SRC_INCLUDE__

#ifdef __cplusplus
extern "C" {
#endif

#include <kvs/webrtc_client.h>
#include "AppConfig.h"
#include "AppError.h"

typedef STATUS(*MediaSinkHook)(PVOID udata, PFrame pFrame);
typedef STATUS(*MediaEosHook)(PVOID udata);
typedef PVOID PMediaContext;

typedef struct __AppMediaSrc{
	STATUS (*app_media_source_init)(PMediaContext *ppMediaContext);
	STATUS (*app_media_source_isReady)(PMediaContext pMediaContext);
	STATUS (*app_media_source_queryVideoCap)(PMediaContext pMediaContext, RTC_CODEC *pCodec);
	STATUS (*app_media_source_queryAudioCap)(PMediaContext pMediaContext, RTC_CODEC *pCodec);
	STATUS (*app_media_source_linkSinkHook)(PMediaContext pMediaContext, MediaSinkHook mediaSinkHook, PVOID udata);
	STATUS (*app_media_source_linkEosHook)(PMediaContext pMediaContext, MediaEosHook mediaEosHook, PVOID udata);
	PVOID (*app_media_source_run)(PVOID pArgs);
	STATUS (*app_media_source_shutdown)(PMediaContext pMediaContext);
	STATUS (*app_media_source_isShutdown)(PMediaContext pMediaContext, PBOOL pShutdown);
	STATUS (*app_media_source_detroy)(PMediaContext *ppMediaContext);
}AppMediaSrc, *PAppMediaSrc;

#ifdef __cplusplus
}
#endif
#endif /* __AWS_KVS_WEBRTC_APP_MEDIA_SRC_INCLUDE__ */
