#include "platform_opts.h"
#if CONFIG_EXAMPLE_KVS_WEBRTC

/* Basic setting for kvs webrtc example */
#include "example_kvs_webrtc.h"

/* Config for Ameba-Pro */
#define STACK_SIZE      20*1024

/* Network */
#include "lwip_netconf.h"
#include "wifi_conf.h"
#include "sntp/sntp.h"

/* File system */
#include "ff.h"
#include "fatfs_ext/inc/ff_driver.h"

/* SD */
#include "disk_if/inc/sdcard.h"
#include "fatfs_sdcard_api.h"
fatfs_sd_params_t fatfs_sd;

/* Config for KVS example */
#include "com/amazonaws/kinesis/video/cproducer/Include.h"
#include "Samples.h"
#define FILE_LOGGING_BUFFER_SIZE            (100 * 1024)
#define MAX_NUMBER_OF_LOG_FILES             5
extern PSampleConfiguration gSampleConfiguration;

/* used to monitor skb resource */
extern int skbbuf_used_num;
extern int skbdata_used_num;
extern int max_local_skb_num;
extern int max_skb_buf_num;

/* Video */
#undef ATOMIC_ADD
#include "isp_api.h"
#include "h264_api.h"
#include "sensor.h"

struct h264_kvs_def_setting {
    uint32_t height;
    uint32_t width;
    H264_RC_MODE rcMode;
    uint32_t bitrate;
    uint32_t fps;
    uint32_t gopLen;
    uint32_t output_buffer_size;
    uint8_t isp_stream_id;
    uint32_t isp_hw_slot;
    uint32_t isp_format;
};

#define ISP_SW_BUF_NUM  3    // set 2 for 15fps, set 3 for 30fps

struct h264_kvs_def_setting def_setting = {
    .height = KVS_VIDEO_HEIGHT,
    .width = KVS_VIDEO_WIDTH,
    .rcMode = H264_RC_MODE_CBR,
    .bitrate = 1*1024*1024,
    .fps = 30,
    .gopLen = 30,
    .output_buffer_size = KVS_VIDEO_OUTPUT_BUFFER_SIZE,
    .isp_stream_id = 0,
    .isp_hw_slot = 1,
    .isp_format = ISP_FORMAT_YUV420_SEMIPLANAR,
};

typedef struct isp_s{
    isp_stream_t* stream;
    isp_cfg_t cfg;
    isp_buf_t buf_item[ISP_SW_BUF_NUM];
    xQueueHandle output_ready;
    xQueueHandle output_recycle;//!< the return buffer.
}isp_t;

/* the callback of isp engine */
void isp_frame_cb(void* p)
{
    BaseType_t xTaskWokenByReceive = pdFALSE;
    BaseType_t xHigherPriorityTaskWoken;
    
    isp_t* ctx = (isp_t*)p;
    isp_info_t* info = &ctx->stream->info;
    isp_cfg_t *cfg = &ctx->cfg;
    isp_buf_t buf;
    isp_buf_t queue_item;
    
    int is_output_ready = 0;

    if(info->isp_overflow_flag == 0){
        /** get the available buffer. */
        is_output_ready = xQueueReceiveFromISR(ctx->output_recycle, &buf, &xTaskWokenByReceive) == pdTRUE;
    }else{
        /** this should be take care. */
        info->isp_overflow_flag = 0;
        ISP_DBG_INFO("isp overflow = %d\r\n",cfg->isp_id);
    }
    
    if(is_output_ready){
        /** flush the isp to the available buffer. */
        isp_handle_buffer(ctx->stream, &buf, MODE_EXCHANGE);
        buf.timestamp = xTaskGetTickCountFromISR(); /*update the timestamp*/
        /** send it to the routine of h264 engine. */
        xQueueSendFromISR(ctx->output_ready, &buf, &xHigherPriorityTaskWoken);  
    }else{
        /** drop this frame buffer if there is no available buffer. */
        isp_handle_buffer(ctx->stream, NULL, MODE_SKIP);
    }
    if( xHigherPriorityTaskWoken || xTaskWokenByReceive)
        taskYIELD ();
}

PVOID sendVideoPackets(PVOID args)
{
    STATUS retStatus = STATUS_SUCCESS;
    PSampleConfiguration pSampleConfiguration = (PSampleConfiguration) args;
    RtcEncoderStats encoderStats;
    Frame frame;
    STATUS status;
    UINT32 i;
    MEMSET(&encoderStats, 0x00, SIZEOF(RtcEncoderStats));

    u8 start_transfer = 0;
    
    int ret;
    isp_init_cfg_t isp_init_cfg;
    isp_t isp_ctx;

    if (pSampleConfiguration == NULL) {
        printf("[KVS Master] sendVideoPackets(): operation returned status code: 0x%08x \n\r", STATUS_NULL_ARG);
        goto CleanUp;
    }

    frame.presentationTs = 0;

    printf("[H264] init video related settings\n\r");
    // [H264] init video related settings
    /**
     * setup the hardware of isp/h264
    */
    memset(&isp_init_cfg, 0, sizeof(isp_init_cfg));
    isp_init_cfg.pin_idx = ISP_PIN_IDX;
    isp_init_cfg.clk = SENSOR_CLK_USE;
    isp_init_cfg.ldc = LDC_STATE;
    isp_init_cfg.fps = SENSOR_FPS;
    isp_init_cfg.isp_fw_location = ISP_FW_LOCATION;
    
    video_subsys_init(&isp_init_cfg);
#if CONFIG_LIGHT_SENSOR
	init_sensor_service();
#else
	ir_cut_init(NULL);
	ir_cut_enable(1);
#endif
    
    /* setup the sw module of h264 engine */
    printf("[H264] create encoder\n\r");
    // [H264] create encoder
    struct h264_context* h264_ctx;
    ret = h264_create_encoder(&h264_ctx);
    if (ret != H264_OK) {
        printf("\n\rh264_create_encoder err %d\n\r",ret);
        goto exit;
    }

    printf("[H264] get & set encoder parameters\n\r");
    // [H264] get & set encoder parameters
    struct h264_parameter h264_parm;
    ret = h264_get_parm(h264_ctx, &h264_parm);
    if (ret != H264_OK) {
        printf("\n\rh264_get_parmeter err %d\n\r",ret);
        goto exit;
    }

    h264_parm.height = def_setting.height;
    h264_parm.width = def_setting.width;
    h264_parm.rcMode = def_setting.rcMode;
    h264_parm.bps = def_setting.bitrate;
    h264_parm.ratenum = def_setting.fps;
    h264_parm.gopLen = def_setting.gopLen;

    ret = h264_set_parm(h264_ctx, &h264_parm);
    if (ret != H264_OK) {
        printf("\n\rh264_set_parmeter err %d\n\r",ret);
        goto exit;
    }

    printf("[H264] init encoder\n\r");
    // [H264] init encoder
    ret = h264_init_encoder(h264_ctx);
    if (ret != H264_OK) {
        printf("\n\rh264_init_encoder_buffer err %d\n\r",ret);
        goto exit;
    }

    // [ISP] init ISP
    /* setup the sw module of isp engine */
    printf("[ISP] init ISP\n\r");
    memset(&isp_ctx,0,sizeof(isp_ctx));
    isp_ctx.output_ready = xQueueCreate(ISP_SW_BUF_NUM, sizeof(isp_buf_t));
    isp_ctx.output_recycle = xQueueCreate(ISP_SW_BUF_NUM, sizeof(isp_buf_t));
    
    isp_ctx.cfg.isp_id = def_setting.isp_stream_id;
    isp_ctx.cfg.format = def_setting.isp_format;
    isp_ctx.cfg.width = def_setting.width;
    isp_ctx.cfg.height = def_setting.height;
    isp_ctx.cfg.fps = def_setting.fps;
    isp_ctx.cfg.hw_slot_num = def_setting.isp_hw_slot;

    isp_ctx.stream = isp_stream_create(&isp_ctx.cfg);

    isp_stream_set_complete_callback(isp_ctx.stream, isp_frame_cb, (void*)&isp_ctx);

    for (int i=0; i<ISP_SW_BUF_NUM; i++ ) {
        /** sensor is at yuv420 mode. */
        unsigned char *ptr =(unsigned char *) malloc(def_setting.width*def_setting.height*3/2);
        if (ptr==NULL) {
            printf("[ISP] Allocate isp buffer[%d] failed\n\r",i);
            while(1);
        }
        isp_ctx.buf_item[i].slot_id = i;
        isp_ctx.buf_item[i].y_addr = (uint32_t) ptr;
        isp_ctx.buf_item[i].uv_addr = isp_ctx.buf_item[i].y_addr + def_setting.width*def_setting.height;
        /** looks like this isp has ping-pong buffers. */
        if (i<def_setting.isp_hw_slot) {        // config hw slot
            isp_handle_buffer(isp_ctx.stream, &isp_ctx.buf_item[i], MODE_SETUP);
        }
        /** backup buffer. */
        else {                                  // extra sw buffer
            if(xQueueSend(isp_ctx.output_recycle, &isp_ctx.buf_item[i], 0)!= pdPASS) {
                printf("[ISP] Queue send fail\n\r");
                while(1);
            }
        }
    }

    isp_stream_apply(isp_ctx.stream);
    isp_stream_start(isp_ctx.stream);

    while (!ATOMIC_LOAD_BOOL(&pSampleConfiguration->appTerminateFlag)) 
    {
        VIDEO_BUFFER video_buf;
        isp_buf_t isp_buf;
        // [ISP] get isp data
        /**
         * get the isp buffe from the isr of isp engine.
        */
        if(xQueueReceive(isp_ctx.output_ready, &isp_buf, 10) != pdTRUE) {
            continue;
        }
        
        // [H264] encode data
        /** allocate the output buffer of h264 engine. */
        video_buf.output_buffer_size = def_setting.output_buffer_size;
        video_buf.output_buffer = malloc(video_buf.output_buffer_size);
        if (video_buf.output_buffer== NULL) {
            printf("Allocate output buffer fail\n\r");
            continue;
        }
        /** trigger the h264 engine. */
        ret = h264_encode_frame(h264_ctx, &isp_buf, &video_buf);
        if (ret != H264_OK) {
            printf("\n\rh264_encode_frame err %d\n\r",ret);
            if (video_buf.output_buffer != NULL)
                free(video_buf.output_buffer);
            continue;
        }

        // [ISP] put back isp buffer
        /** return the isp buffer. */
        xQueueSend(isp_ctx.output_recycle, &isp_buf, 10);

        /* Check if the frame is I frame */
        if (!start_transfer) {
            if (h264_is_i_frame(video_buf.output_buffer)) {
                start_transfer = 1;
            }
            else {
                if (video_buf.output_buffer != NULL)
                    free(video_buf.output_buffer);
                continue;
            }
        }

        frame.frameData = video_buf.output_buffer;
        frame.size = video_buf.output_size;

        // based on bitrate of samples/h264SampleFrames/frame-*
        encoderStats.width = h264_parm.width;
        encoderStats.height = h264_parm.height;
        encoderStats.targetBitrate = h264_parm.bps;
        frame.presentationTs = getEpochTimestampInHundredsOfNanos(&isp_buf.timestamp);
        //printf("video timestamp = %llu\n\r", frame.presentationTs);

        /* wait for skb resource release */
        if((skbdata_used_num > (max_skb_buf_num - 5)) || (skbbuf_used_num > (max_local_skb_num - 5))){
            if (video_buf.output_buffer != NULL){
                free(video_buf.output_buffer);
            }
            continue; //skip this frame and wait for skb resource release.
        }

        MUTEX_LOCK(pSampleConfiguration->streamingSessionListReadLock);
        for (i = 0; i < pSampleConfiguration->streamingSessionCount; ++i) {
            status = writeFrame(pSampleConfiguration->sampleStreamingSessionList[i]->pVideoRtcRtpTransceiver, &frame);
            encoderStats.encodeTimeMsec = 4; // update encode time to an arbitrary number to demonstrate stats update
            updateEncoderStats(pSampleConfiguration->sampleStreamingSessionList[i]->pVideoRtcRtpTransceiver, &encoderStats);
            if (status != STATUS_SRTP_NOT_READY_YET) {
                if (status != STATUS_SUCCESS) {
#ifdef VERBOSE
                    printf("writeFrame() failed with 0x%08x\n\r", status);
#endif
                }
            }
        }
        MUTEX_UNLOCK(pSampleConfiguration->streamingSessionListReadLock);
        free(video_buf.output_buffer);
    }

exit:
CleanUp:

    video_subsys_deinit(&isp_init_cfg);
    h264_free_encoder(&h264_ctx);
    vQueueDelete(isp_ctx.output_ready);
    vQueueDelete(isp_ctx.output_recycle);

    isp_stream_stop(isp_ctx.stream);
    isp_stream_cancel(isp_ctx.stream);
    isp_stream_destroy(isp_ctx.stream);

    for (int i=0; i<ISP_SW_BUF_NUM; i++ ){
    unsigned char* ptr = (unsigned char*) isp_ctx.buf_item[i].y_addr;
    if (ptr) 
        free(ptr);
    }

    CHK_LOG_ERR(retStatus);
    return (PVOID)(ULONG_PTR) retStatus;
}

/* Audio */
#include "audio_api.h"
#if (!(AUDIO_G711_MULAW || AUDIO_G711_ALAW) && AUDIO_OPUS)
#include "opusenc.h"
#endif

audio_t audio_obj;

#define TX_PAGE_SIZE 320  //64*N bytes, max: 4032
#define RX_PAGE_SIZE 320  //64*N bytes, max: 4032
#define DMA_PAGE_NUM 2   //Only 2 page

u8 dma_txdata[TX_PAGE_SIZE*DMA_PAGE_NUM]__attribute__ ((aligned (0x20))); 
u8 dma_rxdata[RX_PAGE_SIZE*DMA_PAGE_NUM]__attribute__ ((aligned (0x20)));

//opus parameter
#define SAMPLE_RATE 8000
#define CHANNELS 1
#define APPLICATION   OPUS_APPLICATION_RESTRICTED_LOWDELAY

#if (AUDIO_G711_MULAW || AUDIO_G711_ALAW)
// extern from g711_codec.c
extern uint8_t encodeA(short pcm_val);
extern short decodeA(uint8_t a_val);
extern uint8_t encodeU(short pcm_val);
extern short decodeU(uint8_t u_val);
#endif

xQueueHandle audio_queue;
xQueueHandle audio_queue_recv;

typedef struct audio_buf_s{
    uint8_t *data_buf;
    uint32_t timestamp;
}audio_buf_t;

void audio_tx_complete_irq(u32 arg, u8 *pbuf){}

void audio_rx_complete_irq(u32 arg, u8 *pbuf)
{
    audio_t *obj = (audio_t *)arg; 

    if (audio_get_rx_error_cnt(obj) != 0x00) {
        dbg_printf("rx page error !!! \r\n");
    }
    
    audio_buf_t au_buf;
    au_buf.timestamp = xTaskGetTickCountFromISR();
    au_buf.data_buf = pbuf;
    
    BaseType_t xHigherPriorityTaskWoken;
    
    if( xQueueSendFromISR(audio_queue, (void *)&au_buf, &xHigherPriorityTaskWoken) != pdTRUE){
        //printf("\n\rAudio queue full.\n\r");
    }
    
    if( xHigherPriorityTaskWoken)
      taskYIELD ();

    audio_set_rx_page(&audio_obj); // submit a new page for receive   
}

PVOID sendAudioPackets(PVOID args)
{
    STATUS retStatus = STATUS_SUCCESS;
    PSampleConfiguration pSampleConfiguration = (PSampleConfiguration) args;
    Frame frame;
    UINT32 i;
    STATUS status;

    if (pSampleConfiguration == NULL) {
        printf("[KVS Master] sendAudioPackets(): operation returned status code: 0x%08x \n", STATUS_NULL_ARG);
        goto CleanUp;
    }

    frame.presentationTs = 0;

    //Audio Init    
    audio_init(&audio_obj, OUTPUT_SINGLE_EDNED, MIC_DIFFERENTIAL, AUDIO_CODEC_2p8V); 
    audio_set_param(&audio_obj, ASR_8KHZ, WL_16BIT);
    audio_mic_analog_gain(&audio_obj, ENABLE, MIC_30DB);
    //Init RX dma
    audio_set_rx_dma_buffer(&audio_obj, dma_rxdata, RX_PAGE_SIZE);    
    audio_rx_irq_handler(&audio_obj, (audio_irq_handler)audio_rx_complete_irq, (u32)&audio_obj);
    //Init TX dma
    audio_set_tx_dma_buffer(&audio_obj, dma_txdata, TX_PAGE_SIZE);    
    audio_tx_irq_handler(&audio_obj, (audio_irq_handler)audio_tx_complete_irq, (u32)&audio_obj);
    //Create a queue to receive the RX buffer from audio_in
    audio_queue = xQueueCreate(6, sizeof(audio_buf_t));
    xQueueReset(audio_queue);
    //Audio TX and RX Start
    audio_trx_start(&audio_obj);
    printf("\n\rAudio Start.\n\r");

#if AUDIO_OPUS
    //Holds the state of the opus encoder
    OpusEncoder *opus_encoder;
    int err;
    //Create a new opus encoder state
    opus_encoder = opus_encoder_create(SAMPLE_RATE, CHANNELS, APPLICATION, &err);   
    //Perform a CTL function on an Opus encoder.
    opus_encoder_ctl(opus_encoder, OPUS_SET_FORCE_CHANNELS(2));
    opus_encoder_ctl(opus_encoder, OPUS_SET_COMPLEXITY(1));
    opus_encoder_ctl(opus_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(opus_encoder, OPUS_SET_BITRATE(500*30)); 
    opus_encoder_ctl(opus_encoder, OPUS_SET_LSB_DEPTH(8)); // Input precision in bits, between 8 and 24 (default: 24).    
#endif

    short buf_16bit[TX_PAGE_SIZE/2];
    unsigned char buf_8bit[TX_PAGE_SIZE/2];
    audio_buf_t audio_buf;

    while (!ATOMIC_LOAD_BOOL(&pSampleConfiguration->appTerminateFlag)) 
    {
        if(xQueueReceive(audio_queue, (void*)&audio_buf, portMAX_DELAY) == pdTRUE)
        {
            memcpy((void*)buf_16bit, (void*)audio_buf.data_buf, TX_PAGE_SIZE);
#if AUDIO_OPUS
            //Encode the data with OPUS encoder
            frame.size = opus_encode(opus_encoder, buf_16bit, TX_PAGE_SIZE/2, buf_8bit, TX_PAGE_SIZE/2);
#elif AUDIO_G711_MULAW
            //Encode the data with G711 MULAW encoder
            for (int j = 0; j < TX_PAGE_SIZE/2; j++){
                buf_8bit[j] = encodeU(buf_16bit[j]);
            }
            frame.size = TX_PAGE_SIZE/2;
#elif AUDIO_G711_ALAW  
            //Encode the data with G711 ALAW encoder
            for (int j = 0; j < TX_PAGE_SIZE/2; j++){
                buf_8bit[j] = encodeA(buf_16bit[j]);
            }
            frame.size = TX_PAGE_SIZE/2;
#endif
        }
        else
            continue;

        frame.frameData = buf_8bit;
        frame.presentationTs = getEpochTimestampInHundredsOfNanos(&audio_buf.timestamp);
        //printf("audio timestamp = %llu\n\r", frame.presentationTs);

        // wait for skb resource release
        if((skbdata_used_num > (max_skb_buf_num - 5)) || (skbbuf_used_num > (max_local_skb_num - 5))){
            //skip this frame and wait for skb resource release.
            continue;
        }

        MUTEX_LOCK(pSampleConfiguration->streamingSessionListReadLock);
        for (i = 0; i < pSampleConfiguration->streamingSessionCount; ++i) {
            status = writeFrame(pSampleConfiguration->sampleStreamingSessionList[i]->pAudioRtcRtpTransceiver, &frame);
            if (status != STATUS_SRTP_NOT_READY_YET) {
                if (status != STATUS_SUCCESS) {
#ifdef VERBOSE
                    printf("writeFrame() failed with 0x%08x\n", status);
#endif
                }
            }
        }
        MUTEX_UNLOCK(pSampleConfiguration->streamingSessionListReadLock);
    }

CleanUp:

    audio_trx_stop(&audio_obj);
    audio_deinit(&audio_obj);
    vQueueDelete(audio_queue);
#if AUDIO_OPUS
    opus_encoder_destroy(opus_encoder);
#endif

    return (PVOID)(ULONG_PTR) retStatus;
}

PVOID sampleReceiveAudioFrame(PVOID args)
{
    //Create a queue to receive the G711 audio frame from viewer
    audio_queue_recv = xQueueCreate(80, TX_PAGE_SIZE/2);
    xQueueReset(audio_queue_recv);
    
    STATUS retStatus = STATUS_SUCCESS;
    PSampleStreamingSession pSampleStreamingSession = (PSampleStreamingSession) args;
    if (pSampleStreamingSession == NULL) {
        printf("[KVS Master] sampleReceiveAudioFrame(): operation returned status code: 0x%08x \n", STATUS_NULL_ARG);
        goto CleanUp;
    }

    retStatus = transceiverOnFrame(pSampleStreamingSession->pAudioRtcRtpTransceiver, (UINT64) pSampleStreamingSession, sampleFrameHandler);
    if (retStatus != STATUS_SUCCESS) {
        printf("[KVS Master] transceiverOnFrame(): operation returned status code: 0x%08x \n", retStatus);
        goto CleanUp;
    }
    
    u8 *ptx_addre;
    unsigned char buf_g711_recv[TX_PAGE_SIZE/2];
    short buf_16bit_dec[TX_PAGE_SIZE/2];
    
    int time_start = xTaskGetTickCount();
    int time_last = time_start;
    int time_val;
    
    while (!ATOMIC_LOAD_BOOL(&pSampleStreamingSession->terminateFlag)) 
    {
        if(xQueueReceive(audio_queue_recv, (void*)buf_g711_recv, 0) == pdTRUE)
        {
            #if AUDIO_G711_MULAW
            // Decode the data with G711 MULAW decoder
            for (int j = 0; j < TX_PAGE_SIZE/2; j++){
                buf_16bit_dec[j] = decodeU(buf_g711_recv[j]);
            }
            #elif AUDIO_G711_ALAW 
            // Decode the data with G711 ALAW decoder
            for (int j = 0; j < TX_PAGE_SIZE/2; j++){
                buf_16bit_dec[j] = decodeA(buf_g711_recv[j]);
            }
            #endif

            ptx_addre = audio_get_tx_page_adr(&audio_obj);
            memcpy((void*)ptx_addre, (void*)buf_16bit_dec, TX_PAGE_SIZE);
            audio_set_tx_page(&audio_obj, ptx_addre); // loopback -> can hear the sound from audio jack on amebapro
        }
        else
            continue;

        time_val = time_last - time_start;
        vTaskDelay(20 - time_val%20);
        time_last = xTaskGetTickCount();
    }

CleanUp:

    return (PVOID)(ULONG_PTR) retStatus;
}

unsigned char buf_8bit_recv[TX_PAGE_SIZE/2];

VOID sampleFrameHandler(UINT64 customData, PFrame pFrame)
{
    UNUSED_PARAM(customData);
    DLOGV("Frame received. TrackId: %" PRIu64 ", Size: %u, Flags %u", pFrame->trackId, pFrame->size, pFrame->flags);
    memcpy((void*)buf_8bit_recv, (void*)pFrame->frameData, pFrame->size);    

    if( xQueueSendFromISR(audio_queue_recv, (void *)buf_8bit_recv, NULL) != pdTRUE){
        DLOGV("\n\rAudio_sound queue full.\n\r");
    } 

    PSampleStreamingSession pSampleStreamingSession = (PSampleStreamingSession) customData;
    if (pSampleStreamingSession->firstFrame) {
        pSampleStreamingSession->firstFrame = FALSE;
        pSampleStreamingSession->startUpLatency = (GETTIME() - pSampleStreamingSession->offerReceiveTime) / HUNDREDS_OF_NANOS_IN_A_MILLISECOND;
        printf("Start up latency from offer to first frame: %" PRIu64 "ms\n", pSampleStreamingSession->startUpLatency);
    }
}

UCHAR wifi_ip[16];
UCHAR* ameba_get_ip(void){
    extern struct netif xnetif[NET_IF_NUM];
    UCHAR *ip = LwIP_GetIP(&xnetif[0]);
    memset(wifi_ip, 0, sizeof(wifi_ip)/sizeof(wifi_ip[0]));
    memcpy(wifi_ip, ip, 4);
    return wifi_ip;
}

static int amebapro_platform_init(void)
{
    /* initialize HW crypto, do this if mbedtls using AES hardware crypto */
    platform_set_malloc_free( (void*(*)( size_t ))calloc, vPortFree);

    /* CRYPTO_Init -> Configure mbedtls to use FreeRTOS mutexes -> mbedtls_threading_set_alt(...) */
    CRYPTO_Init();

    FRESULT res = fatfs_sd_init();
    if(res < 0){
        printf("fatfs_sd_init fail (%d)\n\r", res);
        return -1;
    }
    fatfs_sd_get_param(&fatfs_sd);

    while( wifi_is_ready_to_transceive( RTW_STA_INTERFACE ) != RTW_SUCCESS ){
        vTaskDelay( 200 / portTICK_PERIOD_MS );
    }
    printf( "wifi connected\r\n" );

    sntp_init();
    while( getEpochTimestampInHundredsOfNanos(NULL) < 10000000000000000ULL ){
        vTaskDelay( 200 / portTICK_PERIOD_MS );
    }

    return 0;
}

void example_kvs_webrtc_thread(void* param){

    printf("=== KVS Example ===\n\r");

    // amebapro platform init
    if (amebapro_platform_init() < 0) {
        printf("platform init fail\n\r");
        goto fail;
    }

    STATUS retStatus = STATUS_SUCCESS;
    PSampleConfiguration pSampleConfiguration = NULL;
    SignalingClientMetrics signalingClientMetrics;
    signalingClientMetrics.version = 0;

    // do trickleIce by default
    printf("[KVS Master] Using trickleICE by default\n\r");
    retStatus =
        createSampleConfiguration(KVS_WEBRTC_CHANNEL_NAME, SIGNALING_CHANNEL_ROLE_TYPE_MASTER, TRUE, TRUE, &pSampleConfiguration);

    if (retStatus != STATUS_SUCCESS) {
        printf("[KVS Master] createSampleConfiguration(): operation returned status code: 0x%08x \n\r", retStatus);
        goto CleanUp;
    }
    printf("[KVS Master] Created signaling channel %s\n\r", KVS_WEBRTC_CHANNEL_NAME);

    if (pSampleConfiguration->enableFileLogging) {
        retStatus = createFileLogger(FILE_LOGGING_BUFFER_SIZE, MAX_NUMBER_OF_LOG_FILES, (PCHAR) FILE_LOGGER_LOG_FILE_DIRECTORY_PATH, TRUE, TRUE, NULL);
        if (retStatus != STATUS_SUCCESS) {
            printf("[KVS Master] createFileLogger(): operation returned status code: 0x%08x \n\r", retStatus);
            pSampleConfiguration->enableFileLogging = FALSE;
        }
    }

    // Set the video handlers
    pSampleConfiguration->videoSource = sendVideoPackets;
    pSampleConfiguration->audioSource = sendAudioPackets;
#ifdef ENABLE_AUDIO_SENDRECV    
    pSampleConfiguration->receiveAudioVideoSource = sampleReceiveAudioFrame;
#endif
#ifdef ENABLE_DATA_CHANNEL
    pSampleConfiguration->onDataChannel = onDataChannel;
#endif
    pSampleConfiguration->mediaType = SAMPLE_STREAMING_AUDIO_VIDEO;

    printf("[KVS Master] Finished setting audio and video handlers\n\r");

    // Initialize KVS WebRTC. This must be done before anything else, and must only be done once.
    retStatus = initKvsWebRtc();
    if (retStatus != STATUS_SUCCESS) {
        printf("[KVS Master] initKvsWebRtc(): operation returned status code: 0x%08x \n\r", retStatus);
        goto CleanUp;
    }
    printf("[KVS Master] KVS WebRTC initialization completed successfully\n\r");

    pSampleConfiguration->signalingClientCallbacks.messageReceivedFn = signalingMessageReceived;

    strcpy(pSampleConfiguration->clientInfo.clientId, SAMPLE_MASTER_CLIENT_ID);

    retStatus = signalingClientCreate(&pSampleConfiguration->clientInfo, &pSampleConfiguration->channelInfo,
                                            &pSampleConfiguration->signalingClientCallbacks, pSampleConfiguration->pCredentialProvider,
                                            &pSampleConfiguration->signalingClientHandle);
    if (retStatus != STATUS_SUCCESS) {
        printf("[KVS Master] signalingClientCreate(): operation returned status code: 0x%08x \n\r", retStatus);
        goto CleanUp;
    }
    printf("[KVS Master] Signaling client created successfully\n\r");

    // Enable the processing of the messages
    retStatus = signalingClientConnect(pSampleConfiguration->signalingClientHandle);
    if (retStatus != STATUS_SUCCESS) {
        printf("[KVS Master] signalingClientConnect(): operation returned status code: 0x%08x \n\r", retStatus);
        goto CleanUp;
    }
    printf("[KVS Master] Signaling client connection to socket established\n\r");

    gSampleConfiguration = pSampleConfiguration;

    printf("[KVS Master] Channel %s set up done \n\r", KVS_WEBRTC_CHANNEL_NAME);

    // Checking for termination
    retStatus = sessionCleanupWait(pSampleConfiguration);
    if (retStatus != STATUS_SUCCESS) {
        printf("[KVS Master] sessionCleanupWait(): operation returned status code: 0x%08x \n\r", retStatus);
        goto CleanUp;
    }

    printf("[KVS Master] Streaming session terminated\n\r");

CleanUp:
    if (retStatus != STATUS_SUCCESS) {
        printf("[KVS Master] Terminated with status code 0x%08x", retStatus);
    }

    printf("[KVS Master] Cleaning up....\n\r");
    if (pSampleConfiguration != NULL) {
        // Kick of the termination sequence
        ATOMIC_STORE_BOOL(&pSampleConfiguration->appTerminateFlag, TRUE);

        // Join the threads
        if (pSampleConfiguration->mediaSenderTid != INVALID_TID_VALUE) {
            THREAD_JOIN(pSampleConfiguration->mediaSenderTid, NULL);
        }

        if (pSampleConfiguration->enableFileLogging) {
            freeFileLogger();
        }
        retStatus = signalingClientGetMetrics(pSampleConfiguration->signalingClientHandle, &signalingClientMetrics);
        if (retStatus == STATUS_SUCCESS) {
            logSignalingClientStats(&signalingClientMetrics);
        } else {
            printf("[KVS Master] signalingClientGetMetrics() operation returned status code: 0x%08x", retStatus);
        }
        retStatus = signalingClientFree(&pSampleConfiguration->signalingClientHandle);
        if (retStatus != STATUS_SUCCESS) {
            printf("[KVS Master] freeSignalingClient(): operation returned status code: 0x%08x", retStatus);
        }

        retStatus = freeSampleConfiguration(&pSampleConfiguration);
        if (retStatus != STATUS_SUCCESS) {
            printf("[KVS Master] freeSampleConfiguration(): operation returned status code: 0x%08x", retStatus);
        }
    }
    printf("[KVS Master] Cleanup done\n\r");

fail:
    fatfs_sd_close();

exit:
    vTaskDelete(NULL);
}

void example_kvs_webrtc(void)
{
    if(xTaskCreate(example_kvs_webrtc_thread, ((const char*)"example_kvs_webrtc_thread"), STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS)
        printf("\n\r%s xTaskCreate(example_kvs_webrtc_thread) failed", __FUNCTION__);
}
#endif /* CONFIG_EXAMPLE_KVS_WEBRTC */
