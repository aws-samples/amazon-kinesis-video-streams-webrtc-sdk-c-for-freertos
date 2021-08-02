#ifndef _EXAMPLE_KVS_WEBRTC_H_
#define _EXAMPLE_KVS_WEBRTC_H_

void example_kvs_webrtc(void);

/* Enter your AWS KVS key here */
#define KVS_WEBRTC_ACCESS_KEY   "xxxxxxxxxxxxxxxxxxxx"
#define KVS_WEBRTC_SECRET_KEY   "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"

/* Setting your signaling channel name */
#define KVS_WEBRTC_CHANNEL_NAME "xxxxxxxxxxxxxxxxxxxx"

/* Cert path */
#define TEMP_CERT_PATH          "0://cert.pem"

/* log level */
#define KVS_WEBRTC_LOG_LEVEL    LOG_LEVEL_WARN  //LOG_LEVEL_VERBOSE

/* Video resolution setting */
#include "sensor.h"
#if SENSOR_USE == SENSOR_PS5270
    #define KVS_VIDEO_HEIGHT    VIDEO_1440SQR_HEIGHT
    #define KVS_VIDEO_WIDTH     VIDEO_1440SQR_WIDTH
#else
    #define KVS_VIDEO_HEIGHT    VIDEO_720P_HEIGHT   //VIDEO_1080P_HEIGHT
    #define KVS_VIDEO_WIDTH     VIDEO_720P_WIDTH    //VIDEO_1080P_WIDTH
#endif
#define KVS_VIDEO_OUTPUT_BUFFER_SIZE    KVS_VIDEO_HEIGHT*KVS_VIDEO_WIDTH/10

/* Audio format setting */
#define AUDIO_G711_MULAW        1
#define AUDIO_G711_ALAW         0
#define AUDIO_OPUS              0

/* Enable two-way audio communication (not support opus format now)*/
//#define ENABLE_AUDIO_SENDRECV

/* (Not Ready Now) Enable data channel */
//#define ENABLE_DATA_CHANNEL

#endif /* _EXAMPLE_KVS_WEBRTC_H_ */

