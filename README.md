# amazon-kinesis-video-streams-webrtc-sdk-c-for-freertos

This project demonstrate how to port [Amazon Kinesis Video WebRTC C SDK](https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c) to FreeRTOS.  It uses the [ESP-Wrover-Kit](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/hw-reference/esp32/get-started-wrover-kit.html) as a reference platform.  You may follow the same procedure to port to other hardware platforms.

## Clone projects

Please git clone this project using the command below.  This will git sub-module all depended submodules under main/lib.

```
git submodule update --init --recursive
```

## Reference platform

We use [ESP IDF 4.3.1](https://github.com/espressif/esp-idf/releases/tag/v4.3.1) and the [ESP-Wrover-Kit](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/hw-reference/esp32/get-started-wrover-kit.html) as the reference platform. Please follow the [Espressif instructions](https://docs.espressif.com/projects/esp-idf/en/stable/get-started/index.html) to set up the environment. 

 There is one modification you need to apply to the ESP IDF.

We have used [mbedtls-2.16.11-idf](https://github.com/espressif/mbedtls) in this project. 

WebRTC needs the functionality of DTLS-for-SRTP ([RFC5764](https://tools.ietf.org/html/rfc5764)). mbedTLS does not support this specification when this project was created. There is one pull request in mbedTLS ([#3235](https://github.com/ARMmbed/mbedtls/pull/3235#)) in progress for adding that support.  Before mbedTLS adopts that pull request,  we need to apply it on top of mbedtls-2.16.11-idf.  If your IDF installation has a mbedTLS version lower than 2.16.11-idf, please upgrade it. 

In this project, we have included all patches from the pull request [pr1813-2.16.6](https://gitlab.linphone.org/BC/public/external/mbedtls/tree/pr1813-2.16.6) under the patches/mbedtls directory, therefore you do not need to pull those patches by yourself.  

Apply the patch located in the patch/mbedtls directory of this project.

```
esp-idf/components/mbedtls/mbedtls$ git am your_demo_path/patch/mbedtls/*
```

## Apply patches

Next, patch depended libraries for using with WebRTC.

### [libwebsockets](https://github.com/warmcat/libwebsockets/releases/tag/v4.1.0-rc1)

This project uses v4.1.0-rc1 of libwebsockets. Please apply patches located in patch/libwebsockets directory.

```
main/lib/libwebsockets$ git am ../../../patch/libwebsockets/*
```

### [libsrtp](https://github.com/cisco/libsrtp/releases/tag/v2.3.0)

This project uses v2.3.0 of libsrtp.  Please apply patches located in patch/libsrtp directory.

```
main/lib/libsrtp$ git am ../../../patch/libsrtp/*
```

### [mbedtls-2.16.11-idf](https://github.com/espressif/mbedtls)

This has been described in the “Reference platform” section above.

### [usrsctp](https://github.com/sctplab/usrsctp/commit/939d48f9632d69bf170c7a84514b312b6b42257d)

The usrsctp library is needed by the data channel feature of WebRTC only.  The library is not included in this project at this point of time. Please check back later for availability.

If you run into problems when "git am" patches, you can use the following commands to resolve the problem. Or try "git am --abort" the process of git am, then "git apply" individual patches sequentially (in the order of the sequence number indicated by the file name).

```
$git apply --reject ../../../patch/problem-lib/problems.patch
// fix *.rej
$git add .
$git am --continue
```

## Configure the project

Use menuconfig of ESP IDF to configure the project.

```
idf.py menuconfig
```

- These parameters under Example Configuration Options must be set.

- - ESP_WIFI_SSID
  - ESP_WIFI_PASSWORD
  - ESP_MAXIMUM_RETRY
  - AWS_ACCESS_KEY_ID
  - AWS_SECRET_ACCESS_KEY
  - AWS_DEFAULT_REGION
  - AWS_KVS_CHANNEL
  - AWS_KVS_LOG_LEVEL

- The modifications needed by this project can be seen in the sdkconfig file located at the root directory. 

### Video source

This project uses pre-recorded h.264 frame files for video streaming.  Please put the files on a SD card.  The files should look like:

/sdcard/h264SampleFrames/frame-%04d.h264. 

 The “%04d” part of the file name should be replaced by a sequence number of the frame.

Please note that you can not use J-TAG and SD card simultaneously on ESP-Wrover-Kit because they share some pins.

[Generate video source](https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c/blob/master/samples/h264SampleFrames/README.md)

Given a video file videotestsrc,  the following GStreamer command generates video frame files. If you want to reduce the number of video files, please modify related setting in sample code.



```
sh
gst-launch-1.0 videotestsrc pattern=ball num-buffers=1500 ! timeoverlay ! videoconvert ! video/x-raw,format=I420,width=1280,height=720,framerate=5/1 ! queue ! x264enc bframes=0 speed-preset=veryfast bitrate=128 byte-stream=TRUE tune=zerolatency ! video/x-h264,stream-format=byte-stream,alignment=au,profile=baseline ! multifilesink location="frame-%04d.h264" index=1
```



### Build and Flash

Build the project and flash it to the board, then run monitor tool to view serial output:

```
idf.py -p PORT flash monitor
```

(To exit the serial monitor, type `Ctrl-]`.)

See the Getting Started Guide of ESP IDF for full steps to configure and use ESP-IDF to build projects.

### RTOS-specific changes

The original WebRTC C SDK written for Linux used stack memory in many places.  When porting the SDK to RTOS, we have changed some of those to use the heap.  In this project, the maximum stack consumption is 20KB.  We’ve set several specific parameters in WebRTC in order to reduce runtime memory consumption. 

```
MAX_SESSION_DESCRIPTION_INIT_SDP_LEN
MAX_MEDIA_STREAM_ID_LEN
ICE_HASH_TABLE_BUCKET_COUNT
MAX_UPDATE_VERSION_LEN
MAX_ARN_LEN
MAX_URI_CHAR_LEN
MAX_PATH_LEN
DEFAULT_TIMER_QUEUE_TIMER_COUNT
```

### Known limitations and issues

This project does not use audio at this point of time. When running on the ESP-Wrover-Kit, this project can only run at low frame rate and low bit rate.  

The current implementation does not support data channel. Please check back later for availability of the data channel feature.

**[The m-line mismatch](https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c/issues/803)**

When using the [WebRTC SDK Test Page](https://awslabs.github.io/amazon-kinesis-video-streams-webrtc-sdk-js/examples/index.html) to validate the demo, you may get m-line mismatch errors.  Different browsers have different behaviors.  To work around such errors, you need to run the [sample](https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-js/#Development) in [amazon-kinesis-video-streams-webrtc-sdk-js](https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-js), and disable audio functionality of audio. This patch disables the audio functionality.  A later release of this project may eliminate the need for this.

patch/amazon-kinesis-video-streams-webrtc-sdk-js/0001-diable-offerToReceiveAudio.patch`

## Security

See [CONTRIBUTING](CONTRIBUTING.md#security-issue-notifications) for more information.

## License

This project is licensed under the Apache-2.0 License.

