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
#ifndef __KINESIS_VIDEO_WEBRTC_APP_CONFIG_INCLUDE__
#define __KINESIS_VIDEO_WEBRTC_APP_CONFIG_INCLUDE__

#ifdef __cplusplus
extern "C" {
#endif

#include <kvs/WebRTCClient.h>

#define APP_MAX_CONCURRENT_STREAMING_SESSION 10
#define APP_MASTER_CLIENT_ID                 "ProducerMaster"
#define APP_VIEWER_CLIENT_ID                 "ConsumerViewer"
#define APP_CLEANUP_WAIT_PERIOD              (5 * HUNDREDS_OF_NANOS_IN_A_SECOND)
#define APP_STATS_DURATION                   (60 * HUNDREDS_OF_NANOS_IN_A_SECOND)
#define APP_PENDING_MESSAGE_CLEANUP_DURATION (20 * HUNDREDS_OF_NANOS_IN_A_SECOND)
#define APP_PRE_GENERATE_CERT                TRUE
#define APP_PRE_GENERATE_CERT_PERIOD         (1000 * HUNDREDS_OF_NANOS_IN_A_MILLISECOND)
#define APP_CA_CERT_PEM_FILE_EXTENSION       ".pem"

#define APP_METRICS_FILE_LOGGING_BUFFER_SIZE (100 * 1024)
#define APP_METRICS_LOG_FILES_MAX_NUMBER     5

#define APP_HASH_TABLE_BUCKET_COUNT  50
#define APP_HASH_TABLE_BUCKET_LENGTH 2

#define APP_WEBRTC_CHANNEL                 ((PCHAR) "AWS_WEBRTC_CHANNEL")
#define APP_IOT_CORE_CREDENTIAL_ENDPOINT   ((PCHAR) "AWS_IOT_CORE_CREDENTIAL_ENDPOINT")
#define APP_IOT_CORE_CERT                  ((PCHAR) "AWS_IOT_CORE_CERT")
#define APP_IOT_CORE_PRIVATE_KEY           ((PCHAR) "AWS_IOT_CORE_PRIVATE_KEY")
#define APP_IOT_CORE_ROLE_ALIAS            ((PCHAR) "AWS_IOT_CORE_ROLE_ALIAS")
#define APP_IOT_CORE_THING_NAME            ((PCHAR) "AWS_IOT_CORE_THING_NAME")
#define APP_ECS_AUTH_TOKEN                 ((PCHAR) "AWS_CONTAINER_AUTHORIZATION_TOKEN")
#define APP_ECS_CREDENTIALS_FULL_URI       ((PCHAR) "AWS_CONTAINER_CREDENTIALS_FULL_URI")
#define APP_MEDIA_RTSP_URL                 ((PCHAR) "AWS_RTSP_URL")
#define APP_MEDIA_RTSP_USERNAME            ((PCHAR) "AWS_RTSP_USERNAME")
#define APP_MEDIA_RTSP_PASSWORD            ((PCHAR) "AWS_RTSP_PASSWORD")
#define APP_MEDIA_RTSP_USERNAME_LEN        MAX_CHANNEL_NAME_LEN
#define APP_MEDIA_RTSP_PASSWORD_LEN        MAX_CHANNEL_NAME_LEN
#define APP_MEDIA_GST_ELEMENT_NAME_MAX_LEN 256

#define APP_VIDEO_TRACK_STREAM_ID "myKvsVideoStream"
#define APP_VIDEO_TRACK_ID        "myVideoTrack"
#define APP_AUDIO_TRACK_STREAM_ID "myKvsVideoStream"
#define APP_AUDIO_TRACK_ID        "myAudioTrack"

#define SAMPLE_HASH_TABLE_BUCKET_COUNT  50
#define SAMPLE_HASH_TABLE_BUCKET_LENGTH 2

#define SAMPLE_VIDEO_THREAD_NAME "videosource"
#define SAMPLE_VIDEO_THREAD_SIZE 8192

#define SAMPLE_AUDIO_THREAD_NAME "audiosource"
#define SAMPLE_AUDIO_THREAD_SIZE 4096

#define SAMPLE_TIMER_NAME "sampleTimer"
#define SAMPLE_TIMER_SIZE 10240

#ifdef __cplusplus
}
#endif
#endif /* __KINESIS_VIDEO_WEBRTC_APP_CONFIG_INCLUDE__ */
