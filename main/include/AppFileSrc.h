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
#ifndef __KINESIS_VIDEO_WEBRTC_APP_RTSP_SRC_INCLUDE__
#define __KINESIS_VIDEO_WEBRTC_APP_RTSP_SRC_INCLUDE__

#ifdef __cplusplus
extern "C" {
#endif

#include <com/amazonaws/kinesis/video/webrtcclient/Include.h>
#include "AppConfig.h"
#include "AppError.h"

typedef STATUS (*MediaSinkHook)(PVOID udata, PFrame pFrame);
typedef STATUS (*MediaEosHook)(PVOID udata);
typedef PVOID PMediaContext;
/**
 * @brief   initialize the context of media.
 * @param[in, out] ppMediaContext create the context of the media source, initialize it and return it.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS initMediaSource(PMediaContext* ppMediaContext);
/**
 * @brief   polling the status of media source.
 * @param[in] pMediaContext the context of the media source.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS isMediaSourceReady(PMediaContext pMediaContext);
/**
 * @brief   query the video capability of media.
 * @param[in] pMediaContext the context of the media source.
 * @param[in, out] pCodec the codec of the media source.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS queryMediaVideoCap(PMediaContext pMediaContext, RTC_CODEC* pCodec);
/**
 * @brief   query the audio capability of media.
 * @param[in] pMediaContext the context of the media source.
 * @param[in, out] pCodec the codec of the media source.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS queryMediaAudioCap(PMediaContext pMediaContext, RTC_CODEC* pCodec);
/**
 * @brief   link the hook function with the media sink.
 *
 *          YOU MUST BE AWARE OF RETURNING ERROR IN THE HOOK CAUSES STREAM TERMINATED.
 *
 * @param[in] pMediaContext the context of the media source.
 * @param[in] mediaSinkHook the function pointer for the hook of media sink.
 * @param[in] udata the user data for the hook.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS linkMeidaSinkHook(PMediaContext pMediaContext, MediaSinkHook mediaSinkHook, PVOID udata);
/**
 * @brief   link the eos hook function with the media source.
 * @param[in] pMediaContext the context of the media source.
 * @param[in] mediaSinkHook the function pointer for the eos hook of media source.
 * @param[in] udata the user data for the hook.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS linkMeidaEosHook(PMediaContext pMediaContext, MediaEosHook mediaEosHook, PVOID udata);
/**
 * @brief   the main thread of media source.
 * @param[in] args the context of the media source.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
PVOID runMediaSource(PVOID args);
/**
 * @brief   shutdown the media source and the main thread will be terminated as well.
 * @param[in] pMediaContext the context of the media source.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS shutdownMediaSource(PMediaContext pMediaContext);
/**
 * @brief   destroy the context of media source.
 * @param[in] PMediaContext the context of the media source.
 *
 * @return STATUS code of the execution. STATUS_SUCCESS on success.
 */
STATUS detroyMediaSource(PMediaContext* ppMediaContext);

#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_WEBRTC_APP_RTSP_SRC_INCLUDE__ */
