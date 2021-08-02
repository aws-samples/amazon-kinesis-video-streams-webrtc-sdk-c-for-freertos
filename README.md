# amazon-kinesis-video-streams-webrtc-sdk-c-for-freertos

This project demonstrate how to port [Amazon Kinesis Video WebRTC C SDK](https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c) to FreeRTOS.  It uses the [ESP-Wrover-Kit](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/hw-reference/esp32/get-started-wrover-kit.html) and [Realtek-AmebaPro](https://www.amebaiot.com/en/amebapro) as reference platform.  You may follow the same procedure to port to other hardware platforms.

## ESP-Wrover-Kit Example

This section describe how to run example on [ESP-Wrover-kit](https://www.espressif.com/en/products/hardware/esp-wrover-kit/overview).

### Clone projects

Please git clone this project using the command below.  This will git sub-module all depended submodules under main/lib.

```
git submodule update --init --recursive
```

### Reference platform

We use [ESP IDF 4.1](https://github.com/espressif/esp-idf/releases/tag/v4.1) and the [ESP-Wrover-Kit](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/hw-reference/esp32/get-started-wrover-kit.html) as the reference platform. Please follow the [Espressif instructions](https://docs.espressif.com/projects/esp-idf/en/stable/get-started/index.html) to set up the environment. 

 There are two modifications you need to apply to the ESP IDF.

First, after fetching the esp-idf, please apply the patches located in the patch/esp-idf directory of this project to your IDF installation:

```
esp-idf$ git am your_demo_path/patch/esp-idf/*
```

Second, make sure the version of mbedTLS in your IDF installation is [2.16.6](https://github.com/ARMmbed/mbedtls/releases/tag/mbedtls-2.16.6) or later.  We have used [mbedtls-2.16.6](https://github.com/ARMmbed/mbedtls/releases/tag/mbedtls-2.16.6) in this project. 

WebRTC needs the functionality of DTLS-for-SRTP ([RFC5764](https://tools.ietf.org/html/rfc5764)). mbedTLS does not support this specification when this project was created. There is one pull request in mbedTLS ([#3235](https://github.com/ARMmbed/mbedtls/pull/3235#)) in progress for adding that support.  Before mbedTLS adopts that pull request,  we need to apply it on top of mbedTLS 2.16.6.  If your IDF installation has a mbedTLS version lower than 2.16.6, please upgrade it. 

In this project, we have included all patches from the pull request [pr1813-2.16.6](https://gitlab.linphone.org/BC/public/external/mbedtls/tree/pr1813-2.16.6) under the patches/mbedtls directory, therefore you do not need to pull those patches by yourself.  

Apply patches located in the patch/mbedtls directory of this project.

```
esp-idf/components/mbedtls$ rm -rf mbedtls/
esp-idf/components/mbedtls$ git clone git@github.com:ARMmbed/mbedtls.git
esp-idf/components/mbedtls$ cd mbedtls/
esp-idf/components/mbedtls/mbedtls$ git checkout mbedtls-2.16.6
esp-idf/components/mbedtls/mbedtls$ git am your_demo_path/patch/mbedtls/*
```

### Apply patches

Next, patch depended libraries for using with WebRTC.

#### [libwebsockets](https://github.com/warmcat/libwebsockets/releases/tag/v4.1.0-rc1)

This project uses v4.1.0-rc1 of libwebsockets. Please apply patches located in patch/libwebsockets directory.

```
main/lib/libwebsockets$ git am ../../../patch/libwebsockets/*
```

#### [libsrtp](https://github.com/cisco/libsrtp/releases/tag/v2.3.0)

This project uses v2.3.0 of libsrtp.  Please apply patches located in patch/libsrtp directory.

```
main/lib/libsrtp$ git am ../../../patch/libsrtp/*
```

#### [mbedtls-2.16.6](https://github.com/ARMmbed/mbedtls/releases/tag/mbedtls-2.16.6)

This has been described in the “Reference platform” section above.

#### [usrsctp](https://github.com/sctplab/usrsctp/commit/939d48f9632d69bf170c7a84514b312b6b42257d)

The usrsctp library is needed by the data channel feature of WebRTC only.  The library is not included in this project at this point of time. Please check back later for availability.

If you run into problems when "git am" patches, you can use the following commands to resolve the problem. Or try "git am --abort" the process of git am, then "git apply" individual patches sequentially (in the order of the sequence number indicated by the file name).

```
$git apply --reject ../../../patch/problem-lib/problems.patch
// fix *.rej
$git add .
$git am --continue
```

### Configure the project

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

#### Video source

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

#### RTOS-specific changes

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

#### Known limitations and issues

This project does not use audio at this point of time. When running on the ESP-Wrover-Kit, this project can only run at low frame rate and low bit rate.  

The current implementation does not support data channel. Please check back later for availability of the data channel feature.

**[The m-line mismatch](https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-c/issues/803)**

When using the [WebRTC SDK Test Page](https://awslabs.github.io/amazon-kinesis-video-streams-webrtc-sdk-js/examples/index.html) to validate the demo, you may get m-line mismatch errors.  Different browsers have different behaviors.  To work around such errors, you need to run the [sample](https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-js/#Development) in [amazon-kinesis-video-streams-webrtc-sdk-js](https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-js), and disable audio functionality of audio. This patch disables the audio functionality.  A later release of this project may eliminate the need for this.

patch/amazon-kinesis-video-streams-webrtc-sdk-js/0001-diable-offerToReceiveAudio.patch`


## Realtek AmebaPro example

This section describe how to run example on [AmebaPro](https://www.amebaiot.com/en/amebapro), the video and audio sources are the camera and microphone on the AmebaPro EVB. Before run this example, these hardware are required.  

- An evaluation board of [AmebaPro](https://www.amebaiot.com/en/amebapro/).
- A camera sensor for AmebaPro.

### Prepare AmebaPro SDK environment

If you don't have a AmebaPro SDK environment, please run the following command to download AmebaPro SDK:

```
git clone --recurse-submodules https://github.com/ambiot/ambpro1_sdk.git
```

the command will also download kvs webrtc repository in submodule simultaneously.

### Check the model of camera sensor 

Please check your camera sensor model, and define it in <AmebaPro_SDK>/project/realtek_amebapro_v0_example/inc/sensor.h.

```
#define SENSOR_USE      	SENSOR_XXXX
```

### Configure Example Setting

Before run the example, you need to check the KVS example is enabled in <AmebaPro_SDK>/project/realtek_amebapro_v0_example/inc/platform_opts.h.

```
/* For KVS WebRTC example */
#define CONFIG_EXAMPLE_KVS_WEBRTC             1
```

You also need to edit file "*<AmebaPro_SDK>/lib_amazon/amazon-kinesis-video-streams-webrtc-sdk-c-for-freertos/main_amebapro/example_kvs_webrtc.h*", and replace these settings:

```
/* Enter your AWS KVS key here */
#define KVS_WEBRTC_ACCESS_KEY   "xxxxxxxxxxxxxxxxxxxx"
#define KVS_WEBRTC_SECRET_KEY   "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"

/* Setting your signaling channel name */
#define KVS_WEBRTC_CHANNEL_NAME "xxxxxxxxxxxxxxxxxxxx"
```

### Build and Run Example

To build the example run the following command:

```
cd <AmebaPro_SDK>/project/realtek_amebapro_v0_example/GCC-RELEASE
make all
```

The image is located in <AmebaPro_SDK>/project/realtek_amebapro_v0_example/GCC-RELEASE/application_is/flash_is.bin

Make sure your AmebaPro is connected and powered on. Use the Realtek image tool to flash image and check the logs.

[How to use Realtek image tool? See section 1.3 in AmebaPro's application note](https://github.com/ambiot/ambpro1_sdk/blob/main/doc/AN0300%20Realtek%20AmebaPro%20application%20note.en.pdf)

### Configure WiFi Connection

While runnung the example, you may need to configure WiFi connection by using these commands in uart terminal.

```
ATW0=<WiFi_SSID> : Set the WiFi AP to be connected
ATW1=<WiFi_Password> : Set the WiFi AP password
ATWC : Initiate the connection
```

If everything works fine, you should see the following logs.

```
Interface 0 IP address : XXX.XXX.X.XXX
WIFI initialized
...
SD_Init 0
The card is inited 0
wifi connected
[KVS Master] Using trickleICE by default
cert path:0://cert.pem
look for ssl cert successfully
[KVS Master] Created signaling channel My_KVS_Signaling_Channel
[KVS Master] Finished setting audio and video handlers
[KVS Master] KVS WebRTC initialization completed successfully
...
[KVS Master] Signaling client created successfully
[KVS Master] Signaling client connection to socket established
[KVS Master] Channel My_KVS_Signaling_Channel set up done
```

You can run the [sample](https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-js/#Development) in [amazon-kinesis-video-streams-webrtc-sdk-js](https://github.com/awslabs/amazon-kinesis-video-streams-webrtc-sdk-js) to validate the demo.

## Security

See [CONTRIBUTING](CONTRIBUTING.md#security-issue-notifications) for more information.

## License

This project is licensed under the Apache-2.0 License.

