#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/utils/vpss_helper.h"
#include "cvi_ae.h"
#include "cvi_system.h"
#include "cvi_tdl.h"
#include "cvi_tdl_media.h"
#include "cvi_uvc.h"
#include "cvi_uvc_gadget.h"
#include "sample_comm.h"

//#include "Connector.hpp"

#define VI_FPS 30
#define SLOW_FPS 15

#define CVI_KOMOD_PATH "/mnt/system/ko"
#define CVI_UVC_SCRIPTS_PATH "/etc"

#define UART_DEVICE "/dev/ttyS1"   // uart device name

const char *coco_names[] = {"person",        "bicycle",       "car",           "motorbike",
                            "aeroplane",     "car",           "train",         "car",
                            "boat",          "traffic light", "fire hydrant",  "stop sign",
                            "parking meter", "bench",         "bird",          "cat",
                            "dog",           "horse",         "sheep",         "cow",
                            "elephant",      "bear",          "zebra",         "giraffe",
                            "backpack",      "umbrella",      "handbag",       "tie",
                            "suitcase",      "frisbee",       "skis",          "snowboard",
                            "sports ball",   "kite",          "baseball bat",  "baseball glove",
                            "skateboard",    "surfboard",     "tennis racket", "bottle",
                            "wine glass",    "cup",           "fork",          "knife",
                            "spoon",         "bowl",          "banana",        "apple",
                            "sandwich",      "orange",        "broccoli",      "carrot",
                            "hot dog",       "pizza",         "donut",         "cake",
                            "chair",         "sofa",          "pottedplant",   "bed",
                            "diningtable",   "toilet",        "tvmonitor",     "laptop",
                            "mouse",         "remote",        "keyboard",      "cell phone",
                            "microwave",     "oven",          "toaster",       "sink",
                            "refrigerator",  "book",          "clock",         "vase",
                            "scissors",      "teddy bear",    "hair drier",    "toothbrush"};

cvitdl_handle_t g_tdl_handle = NULL;
cvitdl_service_handle_t g_service_handle = NULL;

static cvtdl_object_t g_obj_meta = {0};

static volatile double frame_cost = 0;
static volatile double tdl_cost = 0;
static volatile double cost = 0;
static volatile bool jobExit = false;
static volatile bool s_ai_init = false;

//static Connector *g_pConnector = nullptr;

#define MUTEXAUTOLOCK_INIT(mutex) pthread_mutex_t AUTOLOCK_##mutex = PTHREAD_MUTEX_INITIALIZER;

#define MutexAutoLock(mutex, lock)                                                                 \
    __attribute__((cleanup(AutoUnLock))) pthread_mutex_t *lock = &AUTOLOCK_##mutex;                \
    pthread_mutex_lock(lock);

__attribute__((always_inline)) inline void AutoUnLock(void *mutex)
{
    pthread_mutex_unlock(*(pthread_mutex_t **)mutex);
}

MUTEXAUTOLOCK_INIT(ResultMutex);

/////////////////

static bool s_uvc_init = false;
typedef struct tagUVC_STREAM_CONTEXT_S
{
    CVI_UVC_DEVICE_CAP_S stDeviceCap;
    CVI_UVC_DATA_SOURCE_S stDataSource;
    UVC_STREAM_ATTR_S stStreamAttr;
    bool bVcapsStart;
    bool bVpssStart;
    bool bVencStart;
    bool bFirstFrame;
    bool bInited;
} UVC_STREAM_CONTEXT_S;

static UVC_STREAM_CONTEXT_S s_stUVCStreamCtx;

typedef struct _MW_CTX_S
{
    SIZE_S stSize;
    SAMPLE_VI_CONFIG_S stViConfig;
} MW_CTX_S;

static MW_CTX_S s_stMwCtx;

////////////////////////

int32_t UVC_STREAM_SetAttr(UVC_STREAM_ATTR_S *pstAttr)
{
    s_stUVCStreamCtx.stStreamAttr = *pstAttr;
    printf("Format: %d, Resolution: %ux%u, FPS: %u, BitRate: %u", pstAttr->enFormat,
           pstAttr->u32Width, pstAttr->u32Height, pstAttr->u32Fps, pstAttr->u32BitRate);
    return 0;
}

static int init_vproc(void)
{
    UVC_STREAM_ATTR_S *pAttr = &s_stUVCStreamCtx.stStreamAttr;
    CVI_UVC_DATA_SOURCE_S *pstSrc = &s_stUVCStreamCtx.stDataSource;
    VPSS_CHN_ATTR_S stChnAttr;
    CVI_VPSS_GetChnAttr(pstSrc->VprocHdl, pstSrc->VprocChnId, &stChnAttr);
    CVI_VPSS_GetChnAttr(pstSrc->VprocHdl, pstSrc->VprocTdlChnId, &stChnAttr);

    stChnAttr.u32Width = pAttr->u32Width;
    stChnAttr.u32Height = pAttr->u32Height;
    CVI_VPSS_SetChnAttr(pstSrc->VprocHdl, pstSrc->VprocChnId, &stChnAttr);
    CVI_VPSS_SetChnAttr(pstSrc->VprocHdl, pstSrc->VprocTdlChnId, &stChnAttr);

    return 0;
}

static void _initInputCfg(chnInputCfg *ipIc)
{
    memset(ipIc, 0, sizeof(chnInputCfg));
    ipIc->rcMode = -1;
    ipIc->iqp = -1;
    ipIc->pqp = -1;
    ipIc->gop = -1;
    ipIc->bitrate = -1;
    ipIc->firstFrmstartQp = -1;
    ipIc->num_frames = -1;
    ipIc->framerate = 30;
    ipIc->maxQp = -1;
    ipIc->minQp = -1;
    ipIc->maxIqp = -1;
    ipIc->minIqp = -1;
}

int32_t sys_init(void)
{
    MMF_VERSION_S stVersion;
    // SAMPLE_INI_CFG_S	   stIniCfg = {
    // 	.enSource  = VI_PIPE_FRAME_SOURCE_DEV,
    // 	.devNum    = 1,
    // 	.enSnsType[0] = SONY_IMX327_MIPI_2M_30FPS_12BIT,
    // 	.enWDRMode[0] = WDR_MODE_NONE,
    // 	.s32BusId[0]  = 3,
    // 	.s32SnsI2cAddr[0] = -1,
    // 	.MipiDev[0]   = 0xFF,
    // 	.enSnsType[1] = SONY_IMX327_SLAVE_MIPI_2M_30FPS_12BIT,
    // 	.s32BusId[1]  = 3,
    // 	.s32SnsI2cAddr[1] = -1,
    // 	.MipiDev[1]   = 0xFF,
    // 	.u8UseMultiSns = 0,
    // };
    SAMPLE_INI_CFG_S stIniCfg;
    stIniCfg.enSource = VI_PIPE_FRAME_SOURCE_DEV;
    stIniCfg.devNum = 1;
    stIniCfg.enSnsType[0] = SONY_IMX327_MIPI_2M_30FPS_12BIT;
    stIniCfg.enWDRMode[0] = WDR_MODE_NONE;
    stIniCfg.s32BusId[0] = 3;
    stIniCfg.s32SnsI2cAddr[0] = -1;
    stIniCfg.MipiDev[0] = 0xFF;
    stIniCfg.enSnsType[1] = SONY_IMX327_SLAVE_MIPI_2M_30FPS_12BIT;
    stIniCfg.s32BusId[1] = 3;
    stIniCfg.s32SnsI2cAddr[1] = -1;
    stIniCfg.MipiDev[1] = 0xFF;
    stIniCfg.u8UseMultiSns = 0;

    CVI_S32 s32Ret = CVI_SUCCESS;
    PIC_SIZE_E enPicSize;

    CVI_SYS_GetVersion(&stVersion);
    printf("MMF Version:%s\n", stVersion.version);

    // Get config from ini if found.
    if (SAMPLE_COMM_VI_ParseIni(&stIniCfg))
    {
        printf("Parse complete\n");
    }

    // Set sensor number
    CVI_VI_SetDevNum(stIniCfg.devNum);
    s_stMwCtx.stViConfig.s32WorkingViNum = stIniCfg.devNum;

    /************************************************
     * step1:  Config VI
     ************************************************/
    s32Ret = SAMPLE_COMM_VI_IniToViCfg(&stIniCfg, &s_stMwCtx.stViConfig);
    if (s32Ret != CVI_SUCCESS)
        return s32Ret;

    /************************************************
     * step2:  Get input size
     ************************************************/
    s32Ret = SAMPLE_COMM_VI_GetSizeBySensor(stIniCfg.enSnsType[0], &enPicSize);
    if (s32Ret != CVI_SUCCESS)
    {
        CVI_TRACE_LOG(CVI_DBG_ERR, "SAMPLE_COMM_VI_GetSizeBySensor failed with %#x\n", s32Ret);
        return s32Ret;
    }

    s32Ret = SAMPLE_COMM_SYS_GetPicSize(enPicSize, &s_stMwCtx.stSize);
    if (s32Ret != CVI_SUCCESS)
    {
        CVI_TRACE_LOG(CVI_DBG_ERR, "SAMPLE_COMM_SYS_GetPicSize failed with %#x\n", s32Ret);
        return s32Ret;
    }

    printf("SAMPLE_COMM_SYS_GetPicSize %dx%d\n", s_stMwCtx.stSize.u32Width,
           s_stMwCtx.stSize.u32Height);

    /************************************************
     * step3:  Init VB pool
     ************************************************/
    VB_CONFIG_S stVbConf;
    CVI_U32 u32BlkSize = 0;

    memset(&stVbConf, 0, sizeof(VB_CONFIG_S));
    stVbConf.u32MaxPoolCnt = 0;

    u32BlkSize = COMMON_GetPicBufferSize(s_stMwCtx.stSize.u32Width, s_stMwCtx.stSize.u32Height,
                                         PIXEL_FORMAT_NV21, DATA_BITWIDTH_8, COMPRESS_MODE_NONE,
                                         DEFAULT_ALIGN);

    stVbConf.astCommPool[stVbConf.u32MaxPoolCnt].u32BlkSize = u32BlkSize;
    stVbConf.astCommPool[stVbConf.u32MaxPoolCnt].u32BlkCnt = 12;
    stVbConf.astCommPool[stVbConf.u32MaxPoolCnt].enRemapMode = VB_REMAP_MODE_CACHED;
    stVbConf.u32MaxPoolCnt++;

    s32Ret = SAMPLE_COMM_SYS_Init(&stVbConf);
    if (s32Ret != CVI_SUCCESS)
    {
        CVI_TRACE_LOG(CVI_DBG_ERR, "sys init failed. s32Ret: 0x%x !\n", s32Ret);
        return s32Ret;
    }

	VI_VPSS_MODE_S stVIVPSSMode = {0};
	VPSS_MODE_S stVPSSMode = {0};

	stVIVPSSMode.aenMode[0] = VI_OFFLINE_VPSS_OFFLINE;
	stVPSSMode.enMode = VPSS_MODE_DUAL;
	stVPSSMode.ViPipe[0] = 0;
	stVPSSMode.ViPipe[1] = 0;
	stVPSSMode.aenInput[0] = VPSS_INPUT_ISP;
	stVPSSMode.aenInput[1] = VPSS_INPUT_MEM;

	CVI_SYS_SetVIVPSSMode(&stVIVPSSMode);
	CVI_SYS_SetVPSSMode(stVPSSMode.enMode);
	if(stVIVPSSMode.aenMode[0] == VI_OFFLINE_VPSS_ONLINE
		|| stVIVPSSMode.aenMode[0] == VI_ONLINE_VPSS_ONLINE) {

		CVI_SYS_SetVPSSModeEx(&stVPSSMode);
	}

	return CVI_SUCCESS;
}

void sys_exit(void) { SAMPLE_COMM_SYS_Exit(); }

int32_t vi_init(void)
{
    CVI_S32 s32Ret = CVI_SUCCESS;

    s32Ret = SAMPLE_PLAT_VI_INIT(&s_stMwCtx.stViConfig);
    if (s32Ret != CVI_SUCCESS)
    {
        CVI_TRACE_LOG(CVI_DBG_ERR, "vi init failed. s32Ret: 0x%x !\n", s32Ret);
        return s32Ret;
    }

    // max fps set 30
    VI_PIPE ViPipe = 0;

    ISP_PUB_ATTR_S pubAttr = {0};
    CVI_ISP_GetPubAttr(ViPipe, &pubAttr);
    pubAttr.f32FrameRate = VI_FPS;
    CVI_ISP_SetPubAttr(ViPipe, &pubAttr);

    // slow fps set 15
    ISP_EXPOSURE_ATTR_S stExpAttr;
    CVI_ISP_GetExposureAttr(ViPipe, &stExpAttr);
    stExpAttr.stAuto.stExpTimeRange.u32Max = (1000 / VI_FPS) * 1000;
    CVI_ISP_SetExposureAttr(ViPipe, &stExpAttr);

    s_stUVCStreamCtx.bVcapsStart = true;

    return CVI_SUCCESS;
}

void vi_exit(void)
{
    if (!s_stUVCStreamCtx.bVcapsStart)
        return;

    SAMPLE_COMM_VI_DestroyIsp(&s_stMwCtx.stViConfig);
    SAMPLE_COMM_VI_DestroyVi(&s_stMwCtx.stViConfig);
}

CVI_S32 init_algo_foreign_obj_param(void)
{
    InputPreParam preprocess_cfg =
        CVI_TDL_GetPreParam(g_tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION);

    for (int i = 0; i < 3; i++)
    {
        // printf("asign val %d \n", i);
        preprocess_cfg.factor[i] = 0.003922;
        preprocess_cfg.mean[i] = 0.0;
    }
    preprocess_cfg.format = PIXEL_FORMAT_RGB_888_PLANAR;

    // printf("setup yolov8 param \n");
    CVI_S32 ret =
        CVI_TDL_SetPreParam(g_tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, preprocess_cfg);
    if (ret != CVI_SUCCESS)
    {
        printf("Can not set yolov8 preprocess parameters %#x\n", ret);
        return ret;
    }

    // setup yolo algorithm preprocess
    cvtdl_det_algo_param_t yolov8_param =
        CVI_TDL_GetDetectionAlgoParam(g_tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION);
    yolov8_param.cls = 1;

    // printf("setup yolov8 algorithm param \n");
    ret = CVI_TDL_SetDetectionAlgoParam(g_tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION,
                                        yolov8_param);
    if (ret != CVI_SUCCESS)
    {
        printf("Can not set yolov8 algorithm parameters %#x\n", ret);
        return ret;
    }

    // set theshold
    CVI_TDL_SetModelThreshold(g_tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, 0.35);
    CVI_TDL_SetModelNmsThreshold(g_tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, 0.35);

    return ret;
}

int32_t vpss_init(void)
{
    VPSS_GRP VpssGrp = s_stUVCStreamCtx.stDataSource.VprocHdl;
    VPSS_GRP_ATTR_S stVpssGrpAttr = {0};

    stVpssGrpAttr.stFrameRate.s32SrcFrameRate = -1;
    stVpssGrpAttr.stFrameRate.s32DstFrameRate = -1;
    stVpssGrpAttr.enPixelFormat = VI_PIXEL_FORMAT;
    stVpssGrpAttr.u32MaxW = s_stMwCtx.stSize.u32Width;
    stVpssGrpAttr.u32MaxH = s_stMwCtx.stSize.u32Height;
    /// only for test here. u8VpssDev should be decided by VPSS_MODE and usage.
    stVpssGrpAttr.u8VpssDev = 1;

    VPSS_CHN VpssChn = s_stUVCStreamCtx.stDataSource.VprocChnId;
    CVI_BOOL abChnEnable[VPSS_MAX_PHY_CHN_NUM] = {0};
    VPSS_CHN_ATTR_S astVpssChnAttr[VPSS_MAX_PHY_CHN_NUM] = {0};
    CVI_S32 s32Ret = CVI_SUCCESS;

    for (VpssChn = 0; VpssChn < 2; VpssChn++)
    {
        abChnEnable[VpssChn] = CVI_TRUE;
        astVpssChnAttr[VpssChn].u32Width = s_stUVCStreamCtx.stStreamAttr.u32Width;
        astVpssChnAttr[VpssChn].u32Height = s_stUVCStreamCtx.stStreamAttr.u32Height;
        astVpssChnAttr[VpssChn].enVideoFormat = VIDEO_FORMAT_LINEAR;
        if (s_stUVCStreamCtx.stStreamAttr.enFormat == CVI_UVC_STREAM_FORMAT_YUV420)
            astVpssChnAttr[VpssChn].enPixelFormat = PIXEL_FORMAT_YUYV;
        else
            astVpssChnAttr[VpssChn].enPixelFormat = PIXEL_FORMAT_NV21;

        astVpssChnAttr[VpssChn].stFrameRate.s32SrcFrameRate = VI_FPS;
        astVpssChnAttr[VpssChn].stFrameRate.s32DstFrameRate = s_stUVCStreamCtx.stStreamAttr.u32Fps;
        astVpssChnAttr[VpssChn].u32Depth = 0;
        if (VpssChn == 0)
            astVpssChnAttr[VpssChn].u32Depth = 1;

        astVpssChnAttr[VpssChn].bMirror = CVI_FALSE;
        astVpssChnAttr[VpssChn].bFlip = CVI_FALSE;
        astVpssChnAttr[VpssChn].stAspectRatio.enMode = ASPECT_RATIO_NONE;
        astVpssChnAttr[VpssChn].stAspectRatio.bEnableBgColor = CVI_FALSE;
        astVpssChnAttr[VpssChn].stNormalize.bEnable = CVI_FALSE;
    }

    /*start vpss*/
    s32Ret = SAMPLE_COMM_VPSS_Init(VpssGrp, abChnEnable, &stVpssGrpAttr, astVpssChnAttr);
    if (s32Ret != CVI_SUCCESS)
    {
        SAMPLE_PRT("init vpss group failed. s32Ret: 0x%x !\n", s32Ret);
        return s32Ret;
    }

    s32Ret = SAMPLE_COMM_VPSS_Start(VpssGrp, abChnEnable, &stVpssGrpAttr, astVpssChnAttr);
    if (s32Ret != CVI_SUCCESS)
    {
        SAMPLE_PRT("start vpss group failed. s32Ret: 0x%x !\n", s32Ret);
        return s32Ret;
    }

    s32Ret = SAMPLE_COMM_VI_Bind_VPSS(0, 0, VpssGrp);
    if (s32Ret != CVI_SUCCESS)
    {
        SAMPLE_PRT("vi bind vpss ViChn 0 failed. s32Ret: 0x%x !\n", s32Ret);
        return s32Ret;
    }

    s32Ret = SAMPLE_COMM_VI_Bind_VPSS(0, 1, VpssGrp);
    if (s32Ret != CVI_SUCCESS)
    {
        SAMPLE_PRT("vi bind vpss ViChn 1 failed. s32Ret: 0x%x !\n", s32Ret);
        return s32Ret;
    }

    s_stUVCStreamCtx.bVpssStart = true;

    return s32Ret;
}

int round_to_int(double value) { return (int)(value + (value >= 0 ? 0.5 : -0.5)); }

void *run_tdl_thread(void *args)
{
    printf("---------------------run_tdl_thread-----------------------\n");
    int32_t s32Ret = 0;
    jobExit = false;
    VIDEO_FRAME_INFO_S stFrame;
    while (jobExit == false)
    {
        // printf("---------------------to do detection-----------------------\n");
        cvtdl_object_t stHandMeta = {0};
        memset(&stHandMeta, 0, sizeof(cvtdl_object_t));

        s32Ret = CVI_VPSS_GetChnFrame(0, 1, &stFrame, 2000);
        if (s32Ret != CVI_SUCCESS)
        {
            printf("run_tdl_thread CVI_VPSS_GetChnFrame failed. s32Ret: 0x%x !\n", s32Ret);
            goto get_frame_error;
        }

        s32Ret = CVI_TDL_Detection(g_tdl_handle, &stFrame, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION,
                                   &stHandMeta);


        for (uint32_t i = 0; i < stHandMeta.size; i++) {
            printf("---- %f %f %f %f %d %f \n", stHandMeta.info[i].bbox.x1,
                stHandMeta.info[i].bbox.y1,
                stHandMeta.info[i].bbox.x2,
                stHandMeta.info[i].bbox.y2,
                stHandMeta.info[i].classes,
                stHandMeta.info[i].bbox.score);
        }

        if (s32Ret != CVI_SUCCESS)
        {
            printf("##run_tdl_thread CVI_TDL_PersonVehicle_Detection failed with "
                   "%#x! \n",
                   s32Ret);
            goto detection_error;
        }
        // printf("tdl thread run -detection %d \n", new_size);

        {
            MutexAutoLock(ResultMutex, lock);
            CVI_TDL_CopyObjectMeta(&stHandMeta, &g_obj_meta);
        }

    //////////////////////////
    detection_error:
    get_frame_error:
        CVI_VPSS_ReleaseChnFrame(0, 1, &stFrame);
        CVI_TDL_Free(&stHandMeta);
    }

    printf("Exit TDL thread\n");

    CVI_TDL_DestroyHandle(g_tdl_handle);
    CVI_TDL_Service_DestroyHandle(g_service_handle);
    CVI_TDL_Free(&g_obj_meta);

    pthread_exit(NULL);
}

int32_t ai_init(void)
{
    printf("111-ai_init\n");
    int32_t s32Ret = 0;
    s32Ret = CVI_TDL_CreateHandle(&g_tdl_handle);
    if (s32Ret != CVI_SUCCESS)
    {
        printf("Create tdl handle failed with %#x!\n", s32Ret);
        return -1;
    }

    s32Ret = CVI_TDL_Service_CreateHandle(&g_service_handle, g_tdl_handle);
    if (s32Ret != CVI_SUCCESS)
    {
        printf("CVI_TDL_Service_CreateHandle failed with %#x!\n", s32Ret);
        return -1;
    }

    // s32Ret = init_param(tdl_handle);
    //  if (s32Ret != CVI_SUCCESS) {
    //  	printf("init_param failed with %#x!\n", s32Ret);
    //  	return -1;
    //  }

    // s32Ret = init_algo_foreign_obj_param();
    // if (s32Ret != CVI_SUCCESS) {
    // 	printf("init_param failed with %#x!\n", s32Ret);
    // 	return -1;
    // }
    s32Ret = CVI_TDL_OpenModel(g_tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION,
                               "/mnt/data/nfs/model/yolov8n_coco80_cv181x_int8.cvimodel");
    // s32Ret = CVI_TDL_OpenModel(g_tdl_handle,
    // CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION,
    // "/system/model/yolov8s_3d_foreign_obj_cv181x_int8.cvimodel");
    CVI_TDL_SetModelThreshold(g_tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, 0.25);
    if (s32Ret != CVI_SUCCESS)
    {
        printf("open model failed with %#x!\n", s32Ret);
        return -1;
    }

    memset(&g_obj_meta, 0, sizeof(cvtdl_object_t));

    pthread_t stTDLThread;
    if (pthread_create(&stTDLThread, NULL, run_tdl_thread, NULL) != 0)
    {
        perror("Failed to create thread");
        return -1;
    }

    s_ai_init = true;

    return 0;
}

void vpss_exit()
{
    if (!s_stUVCStreamCtx.bVpssStart)
        return;

    s_stUVCStreamCtx.bVpssStart = false;

    VPSS_GRP VpssGrp = s_stUVCStreamCtx.stDataSource.VprocHdl;
    SAMPLE_COMM_VI_UnBind_VPSS(0, 0, VpssGrp);
    SAMPLE_COMM_VI_UnBind_VPSS(0, 1, VpssGrp);
    CVI_BOOL abChnEnable[VPSS_MAX_PHY_CHN_NUM] = {0};
    abChnEnable[0] = CVI_TRUE;
    abChnEnable[1] = CVI_TRUE;
    SAMPLE_COMM_VPSS_Stop(VpssGrp, abChnEnable);
}

static int init_venc()
{
    UVC_STREAM_ATTR_S *pAttr = &s_stUVCStreamCtx.stStreamAttr;
    CVI_UVC_DATA_SOURCE_S *pstSrc = &s_stUVCStreamCtx.stDataSource;

    int32_t s32Ret = 0;
    PIC_SIZE_E enSize = PIC_1440P;
    chnInputCfg pIc;
    VENC_GOP_MODE_E enGopMode = VENC_GOPMODE_NORMALP;
    VENC_GOP_ATTR_S stGopAttr;
    SAMPLE_RC_E enRcMode;
    CVI_U32 u32Profile = 0;
    PAYLOAD_TYPE_E enPayLoad = PT_H264;

    _initInputCfg(&pIc);
    if (pAttr->enFormat == CVI_UVC_STREAM_FORMAT_MJPEG)
    {
        enPayLoad = PT_MJPEG;
        pIc.quality = 60;   // for MJPEG
    }
    else if (pAttr->enFormat == CVI_UVC_STREAM_FORMAT_H264)
    {
        enPayLoad = PT_H264;
    }
    else if (pAttr->enFormat == CVI_UVC_STREAM_FORMAT_H265)
    {
        enPayLoad = PT_H265;
    }

    if (pAttr->u32Height == 1080)
    {
        enSize = PIC_1080P;
    }
    else if (pAttr->u32Height == 1440)
    {
        enSize = PIC_1440P;
    }
    else if (pAttr->u32Height == 720)
    {
        enSize = PIC_720P;
    }
    else if (pAttr->u32Height == 360)
    {
        enSize = PIC_CUSTOMIZE;
        pIc.width = 640;
        pIc.height = 360;
        pIc.quality = 40;
    }
    else
    {
        printf("todo venc size\n %s %d\n", __func__, __LINE__);
    }

    strcpy(pIc.codec, (enPayLoad == PT_MJPEG) ? "mjp" : (enPayLoad == PT_H264) ? "264" : "265");
    pIc.rcMode = (enPayLoad == PT_MJPEG) ? SAMPLE_RC_FIXQP : SAMPLE_RC_CBR;
    pIc.iqp = 38;
    pIc.pqp = 38;
    pIc.gop = 60;
    pIc.bitrate = 2000;
    pIc.firstFrmstartQp = 26;
    pIc.num_frames = -1;
    pIc.framerate = 25;
    pIc.maxQp = 42;
    pIc.minQp = 26;
    pIc.maxIqp = 42;
    pIc.minIqp = 26;
    pIc.minIprop = 1;
    pIc.maxIprop = 100;
    pIc.initialDelay = 1000;
    enRcMode = (SAMPLE_RC_E)pIc.rcMode;

    s32Ret = SAMPLE_COMM_VENC_GetGopAttr(enGopMode, &stGopAttr);
    if (s32Ret != 0)
    {
        printf("[Err]Venc Get GopAttr for %#x!\n", s32Ret);
        return CVI_FAILURE;
    }

    s32Ret = SAMPLE_COMM_VENC_Start(&pIc, pstSrc->VencHdl, enPayLoad, enSize, enRcMode, u32Profile,
                                    CVI_FALSE, &stGopAttr);
    if (s32Ret != 0)
    {
        printf("[Err]Venc Start failed for %#x!\n", s32Ret);
        return CVI_FAILURE;
    }

    return s32Ret;
}

void venc_exit()
{
    CVI_UVC_DATA_SOURCE_S *pstSrc = &s_stUVCStreamCtx.stDataSource;
    s_stUVCStreamCtx.bVencStart = false;
    SAMPLE_COMM_VENC_Stop(pstSrc->VencHdl);
    printf("[%s] SAMPLE_COMM_VENC_Stop(%d) done\n", __FUNCTION__, pstSrc->VencHdl);
}

int32_t ai_exit(void)
{
    printf("111-ai_exit\n");
    jobExit = true;
    s_ai_init = false;

    return 0;
}

int32_t UVC_STREAM_Start(void)
{
    if (s_stUVCStreamCtx.bVpssStart)
        vpss_exit();

    if (vpss_init() != 0)
    {
        printf("vpss_init failed !");
        return -1;
    }

    if (s_stUVCStreamCtx.bVencStart)
        venc_exit();

    if (0 != init_vproc())
    {
        printf("init_vproc failed !");
        return -1;
    }

    if (0 != init_venc())
    {
        printf("init_venc failed !");
        return -1;
    }

    if (0 != ai_init())
    {
        printf("ai_init failed !");
    }

    s_stUVCStreamCtx.bInited = true;

    return 0;
}

int32_t UVC_STREAM_Stop(void)
{
    if (s_stUVCStreamCtx.bInited)
    {
        CVI_UVC_DATA_SOURCE_S *pstSrc = &s_stUVCStreamCtx.stDataSource;
        SAMPLE_COMM_VENC_Stop(pstSrc->VencHdl);

        s_stUVCStreamCtx.bInited = false;
    }

    jobExit = true;

    return 0;
}

int32_t run_ai_draw(VIDEO_FRAME_INFO_S *venc_frame)
{
    // printf("111-run_ai\n");
    int32_t s32Ret = 0;
    if (!s_ai_init)
    {
        return s32Ret;
    }

    cvtdl_object_t stHandMeta = {0};
    memset(&stHandMeta, 0, sizeof(cvtdl_object_t));

    // // 过滤得到80个类别中 人和车
    // uint32_t new_size = 0;
    // for (uint32_t i = 0; i < stHandMeta.size; i++) {
    // 	int cls = stHandMeta.info[i].classes;
    // 	//"person" 0,        "bicycle" 1,       "car" 2,           "motorbike"
    // 3,
    // 	//"aeroplane 4",     "car 5",           "train 6",         "car 7",
    // 	if (cls == 0 || cls == 1 || cls == 2 || cls == 3 || cls == 5 || cls ==
    // 7) { 		stHandMeta.info[new_size] = stHandMeta.info[i]; 		new_size++;
    // 	}
    // }

    {
        MutexAutoLock(ResultMutex, lock);
        CVI_TDL_CopyObjectMeta(&g_obj_meta, &stHandMeta);

        // cvtdl_object_t *src = &g_obj_meta;
        // cvtdl_object_t *dest = &stHandMeta;
        // CVI_TDL_FreeCpp(dest);
        // printf("%s[%d]\n", __FUNCTION__, __LINE__);
        // memset(dest, 0, sizeof(cvtdl_object_t));
        // printf("%s[%d]\n", __FUNCTION__, __LINE__);
        // if (src->size > 0) {
        // 	dest->size = src->size;
        // 	dest->width = src->width;
        // 	dest->height = src->height;
        // 	dest->rescale_type = src->rescale_type;
        // 	if (src->info) {
        // 		printf("%s[%d] ====> src->size = %d\n", __FUNCTION__, __LINE__,
        // src->size); 		int info_size = sizeof(cvtdl_object_info_t) * src->size;
        // 		printf("%s[%d] ====> info_size = %d\n", __FUNCTION__, __LINE__,
        // info_size); 		dest->info = (cvtdl_object_info_t *)malloc(info_size);
        // 		if(dest->info != NULL )
        // 		{
        // 			printf("%s[%d]\n", __FUNCTION__, __LINE__);
        // 			memset(dest->info, 0, info_size);
        // 			for (uint32_t fid = 0; fid < src->size; fid++) {
        // 				printf("%s[%d] ====> fid = %d\n", __FUNCTION__, __LINE__,
        // fid); 				CVI_TDL_CopyInfoCpp(&src->info[fid], &dest->info[fid]);
        // 				printf("%s[%d]\n", __FUNCTION__, __LINE__);
        // 			}
        // 		}
        // 		else
        // 		{
        // 			printf("malloc error null point!!\n");
        // 		}
        // 	}
        // 	printf("%s[%d]\n", __FUNCTION__, __LINE__);
        // }
        // printf("%s[%d]\n", __FUNCTION__, __LINE__);

        // // 过滤得到80个类别中 人和车
        // uint32_t new_size = 0;
        // for (uint32_t i = 0; i < stHandMeta.size; i++) {
        // 	int cls = stHandMeta.info[i].classes;
        // 	//"person" 0,        "bicycle" 1,       "car" 2,           "motorbike"
        // 3,
        // 	//"aeroplane 4",     "car 5",           "train 6",         "car 7",
        // 	if (cls == 0 || cls == 1 || cls == 2 || cls == 3 || cls == 5 || cls ==
        // 7) { 		stHandMeta.info[new_size] = stHandMeta.info[i]; 		new_size++;
        // 	}
        // }

        // stHandMeta.size = new_size;
    }
    // // 过滤得到80个类别中 人和车
    // uint32_t new_size = 0;
    // for (uint32_t i = 0; i < stHandMeta.size; i++) {
    // 	int cls = stHandMeta.info[i].classes;
    // 	//"person" 0,        "bicycle" 1,       "car" 2,           "motorbike"
    // 3,
    // 	//"aeroplane 4",     "car 5",           "train 6",         "car 7",
    // 	if (cls == 0 || cls == 1 || cls == 2 || cls == 3 || cls == 5 || cls ==
    // 7) { 		stHandMeta.info[new_size] = stHandMeta.info[i]; 		new_size++;
    // 	}
    // }

    cvtdl_service_brush_t brushi;
    brushi.color.r = 255;
    brushi.color.g = 0;
    brushi.color.b = 0;
    brushi.size = 2;

    s32Ret =
        CVI_TDL_Service_ObjectDrawRect(g_service_handle, &stHandMeta, venc_frame, true, brushi);
    if (s32Ret != CVI_SUCCESS)
    {
        printf("##CVI_TDL_Service_ObjectDrawRect failed with %#x!\n", s32Ret);
        return s32Ret;
    }

    for (uint32_t i = 0; i < stHandMeta.size; i++)
    {
        // printf("---- %f %f %f %f %d %f \n", stHandMeta.info[i].bbox.x1,
        // 	stHandMeta.info[i].bbox.y1,
        // 	stHandMeta.info[i].bbox.x2,
        // 	stHandMeta.info[i].bbox.y2,
        // 	stHandMeta.info[i].classes,
        // 	stHandMeta.info[i].bbox.score);

        char strinfo[128];
        if (stHandMeta.info[i].classes < 80)
        {
            sprintf(strinfo, "%s %0.2f", coco_names[stHandMeta.info[i].classes],
                    stHandMeta.info[i].bbox.score);
        }
        else
        {
            printf("name id error stHandMeta.info[i].classes = %d\n", stHandMeta.info[i].classes);
            sprintf(strinfo, "%d - %0.2f", stHandMeta.info[i].classes,
                    stHandMeta.info[i].bbox.score);
        }
        // sprintf(strinfo, "%s %0.2f", coco_names[stHandMeta.info[i].classes],
        // stHandMeta.info[i].bbox.score);

        s32Ret = CVI_TDL_Service_ObjectWriteText(strinfo, stHandMeta.info[i].bbox.x1,
                                                 stHandMeta.info[i].bbox.y2, venc_frame, 0, 200, 0);
        if (s32Ret != CVI_SUCCESS)
        {
            printf("##CVI_TDL_Service_ObjectWriteText failed with %#x!\n", s32Ret);
            return s32Ret;
        }

        if ((stHandMeta.info[i].bbox.x1 > 0 || stHandMeta.info[i].bbox.y1 > 0 ||
             stHandMeta.info[i].bbox.x2 > 0 || stHandMeta.info[i].bbox.y2 > 0) &&
            stHandMeta.info[i].bbox.score > 0 && stHandMeta.info[i].bbox.score < 1)
        {
            char buff[256];
            memset(buff, 0, 256);
            sprintf(buff, "type = %s, box[%f,%f,%f,%f], score = %f\r\n",
                    coco_names[stHandMeta.info[i].classes], stHandMeta.info[i].bbox.x1,
                    stHandMeta.info[i].bbox.y1, stHandMeta.info[i].bbox.x2,
                    stHandMeta.info[i].bbox.y2, stHandMeta.info[i].bbox.score);
            // if (g_pConnector != nullptr)
            // {
            //     g_pConnector->SendData((uint8_t *)(buff), 256);
            // }
        }
    }

    char info[128];
    // sprintf(info, "Detect:%d Frame:%0.1f Tdl:%0.1f Venc:%0.1f",
    // stHandMeta.size, frame_cost, tdl_cost, cost);
    sprintf(info, "Detect:%d", stHandMeta.size);
    s32Ret = CVI_TDL_Service_ObjectWriteText(info, 30, 50, venc_frame, 0, 0, 255);
    if (s32Ret != CVI_SUCCESS)
    {
        printf("##CVI_TDL_Service_ObjectWriteText failed with %#x!\n", s32Ret);
        return s32Ret;
    }

    CVI_TDL_Free(&stHandMeta);
    // printf("---->end!\n");

    return s32Ret;
}

int32_t UVC_STREAM_CopyBitStream(void *dst)
{
    int32_t s32Ret = 0;

    CVI_UVC_DATA_SOURCE_S *pstSrc = &s_stUVCStreamCtx.stDataSource;
    VIDEO_FRAME_INFO_S venc_frame;
    VENC_STREAM_S stStream = {0};
    CVI_VPSS_GetChnFrame(pstSrc->VprocHdl, pstSrc->VprocChnId, &venc_frame, 1000);

    /////////////
    s32Ret = run_ai_draw(&venc_frame);

    int32_t s32SetFrameMilliSec = 20000;
    VENC_CHN_ATTR_S stVencChnAttr;
    VENC_CHN_STATUS_S stStat;

    s32Ret = CVI_VENC_SendFrame(pstSrc->VencHdl, &venc_frame, s32SetFrameMilliSec);
    if (s32Ret != 0)
    {
        printf("CVI_VENC_SendFrame failed! %d\n", s32Ret);
        return -1;
    }

    s32Ret = CVI_VENC_GetChnAttr(pstSrc->VencHdl, &stVencChnAttr);
    if (s32Ret != 0)
    {
        printf("CVI_VENC_GetChnAttr, VencChn[%d], s32Ret = %d\n", pstSrc->VprocHdl, s32Ret);
        return -1;
    }

    s32Ret = CVI_VENC_QueryStatus(pstSrc->VencHdl, &stStat);
    if (s32Ret != 0)
    {
        printf("CVI_VENC_QueryStatus failed with %#x!\n", s32Ret);
        return -1;
    }

    if (!stStat.u32CurPacks)
    {
        printf("NOTE: Current frame is NULL!\n");
        return -1;
    }

    stStream.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S) * stStat.u32CurPacks);
    if (stStream.pstPack == NULL)
    {
        printf("malloc memory failed!\n");
        return -1;
    }

    s32Ret = CVI_VENC_GetStream(pstSrc->VencHdl, &stStream, -1);
    if (s32Ret != 0)
    {
        printf("CVI_VENC_GetStream failed with %#x!\n", s32Ret);
        free(stStream.pstPack);
        stStream.pstPack = NULL;
        return -1;
    }

    uint32_t bitstream_size = 0;
    for (unsigned i = 0; i < stStream.u32PackCount; i++)
    {
        VENC_PACK_S *ppack;
        ppack = &stStream.pstPack[i];

        memcpy(dst + bitstream_size, ppack->pu8Addr + ppack->u32Offset,
               ppack->u32Len - ppack->u32Offset);
        bitstream_size += ppack->u32Len - ppack->u32Offset;
    }

    s32Ret = CVI_VENC_ReleaseStream(pstSrc->VencHdl, &stStream);
    if (s32Ret != 0)
    {
        printf("CVI_VENC_ReleaseStream, s32Ret = %d\n", s32Ret);
        free(stStream.pstPack);
        stStream.pstPack = NULL;
        return -1;
    }

    free(stStream.pstPack);
    stStream.pstPack = NULL;

    CVI_VPSS_ReleaseChnFrame(pstSrc->VprocHdl, pstSrc->VprocChnId, &venc_frame);

    return bitstream_size;
}

int32_t UVC_STREAM_ReqIDR(void)
{
    // TODO: implement
    return 0;
}

/** UVC Context */
static UVC_CONTEXT_S s_stUVCCtx = {
    .bRun = false, .bPCConnect = false, .TskId = (pthread_t)-1, .Tsk2Id = (pthread_t)-1};

bool g_bPushVencData = false;

static void *UVC_CheckTask(void *pvArg)
{
    int32_t ret = 0;
    while (s_stUVCCtx.bRun)
    {
        ret = UVC_GADGET_DeviceCheck();

        if (ret < 0)
        {
            printf("UVC_GADGET_DeviceCheck %x\n", ret);
            break;
        }
        else if (ret == 0)
        {
            printf("Timeout Do Nothing\n");
            if (false != g_bPushVencData)
            {
                g_bPushVencData = false;
            }
        }
    }

    return NULL;
}

static int32_t UVC_LoadMod(void)
{
    static bool first = true;

    if (first == false)
        return 0;

    first = false;
    // printf("Uvc insmod ko successfully!");
    // cvi_insmod(CVI_KOMOD_PATH"/videobuf2-memops.ko", NULL);
    // cvi_system("echo 449 >/sys/class/gpio/export");
    // cvi_system("echo 450 >/sys/class/gpio/export");

    // cvi_system("echo \"out\" >/sys/class/gpio/gpio449/direction");
    // cvi_system("echo \"out\" >/sys/class/gpio/gpio450/direction");

    // cvi_system("echo 0 >/sys/class/gpio/gpio449/value");
    // cvi_system("echo 1 >/sys/class/gpio/gpio450/value");

    // cvi_insmod(CVI_KOMOD_PATH"/usbcore.ko", NULL);
    // cvi_insmod(CVI_KOMOD_PATH"/dwc2.ko", NULL);
    // cvi_insmod(CVI_KOMOD_PATH"/configfs.ko", NULL);
    // cvi_insmod(CVI_KOMOD_PATH"/libcomposite.ko", NULL);
    // cvi_insmod(CVI_KOMOD_PATH"/videobuf2-vmalloc.ko", NULL);
    // cvi_insmod(CVI_KOMOD_PATH"/usb_f_uvc.ko", NULL);
    // cvi_insmod(CVI_KOMOD_PATH"/u_audio.ko", NULL);
    // cvi_insmod(CVI_KOMOD_PATH"/usb_f_uac1.ko", NULL);
    // cvi_system("echo device > /proc/cviusb/otg_role");
    // cvi_system(CVI_UVC_SCRIPTS_PATH"/run_usb.sh probe uvc");
    // cvi_system(CVI_UVC_SCRIPTS_PATH"/ConfigUVC.sh");
    // cvi_system(CVI_UVC_SCRIPTS_PATH"/run_usb.sh start");
    // cvi_system("devmem 0x030001DC 32 0x8844");
    return 0;
}

static int32_t UVC_UnLoadMod(void)
{
    // printf("Do nothing now, due to the ko can NOT rmmod successfully!");
    // cvi_rmmod(CVI_KOMOD_PATH "/videobuf2-memops.ko");
    // cvi_rmmod(CVI_KOMOD_PATH "/videobuf2-vmalloc.ko");
    // cvi_rmmod(CVI_KOMOD_PATH "/configfs.ko");
    // cvi_rmmod(CVI_KOMOD_PATH "/libcomposite.ko");
    // cvi_rmmod(CVI_KOMOD_PATH "/u_serial.ko");
    // cvi_rmmod(CVI_KOMOD_PATH "/usb_f_acm.ko");
    // cvi_rmmod(CVI_KOMOD_PATH "/cvi_usb_f_cvg.ko");
    // cvi_rmmod(CVI_KOMOD_PATH "/usb_f_uvc.ko");
    // cvi_rmmod(CVI_KOMOD_PATH "/u_audio.ko");
    // cvi_rmmod(CVI_KOMOD_PATH "/usb_f_uac1.ko");
    // cvi_rmmod(CVI_KOMOD_PATH "/usb_f_serial.ko");
    // cvi_rmmod(CVI_KOMOD_PATH "/usb_f_mass_storage.ko");
    // cvi_rmmod(CVI_KOMOD_PATH "/u_ether.ko");
    // cvi_rmmod(CVI_KOMOD_PATH "/usb_f_ecm.ko");
    // cvi_rmmod(CVI_KOMOD_PATH "/usb_f_eem.ko");
    // cvi_rmmod(CVI_KOMOD_PATH "/usb_f_rndis.ko");
    // cvi_rmmod(CVI_KOMOD_PATH "/cv183x_usb_gadget.ko");

    return 0;
}

int32_t UVC_Init(const CVI_UVC_DEVICE_CAP_S *pstCap, const CVI_UVC_DATA_SOURCE_S *pstDataSrc,
                 CVI_UVC_BUFFER_CFG_S *pstBufferCfg)
{

    UVC_LoadMod();

    s_stUVCStreamCtx.stDeviceCap = *pstCap;
    s_stUVCStreamCtx.stDataSource = *pstDataSrc;
    UVC_GADGET_Init(pstCap, pstBufferCfg->u32BufSize);

    // TODO: Do we need handle CVI_UVC_BUFFER_CFG_S?
    return 0;
}

int32_t UVC_Deinit(void)
{
    UVC_UnLoadMod();   // TODO, Not work right now

    return 0;
}

int32_t UVC_Start(const char *pDevPath)
{

    if (false == s_stUVCCtx.bRun)
    {
        strcpy(s_stUVCCtx.szDevPath, pDevPath);

        if (UVC_GADGET_DeviceOpen(pDevPath))
        {
            printf("UVC_GADGET_DeviceOpen Failed!");
            return -1;
        }

        s_stUVCCtx.bPCConnect = false;
        s_stUVCCtx.bRun = true;
        if (pthread_create(&s_stUVCCtx.TskId, NULL, UVC_CheckTask, NULL))
        {
            printf("UVC_CheckTask create thread failed!\n");
            s_stUVCCtx.bRun = false;
            return -1;
        }
        printf("UVC_CheckTask create thread successful\n");
    }
    else
    {
        printf("UVC already started\n");
    }

    return 0;
}

int32_t UVC_Stop(void)
{
    if (false == s_stUVCCtx.bRun)
    {
        printf("UVC not run\n");
        return 0;
    }

    s_stUVCCtx.bRun = false;
    pthread_join(s_stUVCCtx.TskId, NULL);

    return UVC_GADGET_DeviceClose();
}

UVC_CONTEXT_S *UVC_GetCtx(void) { return &s_stUVCCtx; }

// int uart_exit(void)
// {
//     if (g_pConnector != nullptr)
//     {
//         g_pConnector->DeinitDev();
//         delete g_pConnector;
//         g_pConnector = nullptr;
//     }
//     return 0;
// }

// int uart_init(void)
// {
//     int ret = 0;
//     g_pConnector = Connector::getInstance();

//     ret = g_pConnector->InitDev(UART_DEVICE);

//     return ret;
// }

int uvc_init(void)
{
    char uvc_devname[32] = "/dev/video0";
    if (access(uvc_devname, F_OK) != 0)
    {
        printf("file %s not found\n", uvc_devname);
        return -1;
    }

    CVI_UVC_DEVICE_CAP_S stDeviceCap = {0};
    CVI_UVC_DATA_SOURCE_S stDataSource = {0};
    CVI_UVC_BUFFER_CFG_S stBuffer = {0};
    stDataSource.AcapHdl = 0;
    stDataSource.VcapHdl = 0;
    stDataSource.VencHdl = 0;
    stDataSource.VprocChnId = 0;
    stDataSource.VprocTdlChnId = 1;
    stDataSource.VprocHdl = 0;

    // if (uart_init() != 0){
    // 	printf("uart_init Failed !");
    // }

    if (sys_init() != 0)
    {
        printf("sys_init Failed !");
        return -1;
    }

    if (vi_init() != 0)
    {
        printf("vi_init Failed !");
        sys_exit();
        return -1;
    }

    if (UVC_Init(&stDeviceCap, &stDataSource, &stBuffer) != 0)
    {
        printf("UVC_Init Failed !");
        goto failed;
    }

    if (UVC_Start(uvc_devname) != 0)
    {
        printf("UVC_Start Failed !");
        goto failed;
    }

    s_uvc_init = true;

    return 0;

failed:
    vi_exit();
    sys_exit();
    return -1;
}

void uvc_exit(void)
{
    if (!s_uvc_init)
    {
        printf("uvc not init\n");
        return;
    }

    if (!s_ai_init)
    {
        printf("AI not init\n");
    }
    else
    {
        printf("AI exit done\n");
        ai_exit();
    }

    if (UVC_Stop() != 0)
        printf("UVC Stop Failed !");

    if (UVC_Deinit() != 0)
        printf("UVC Deinit Failed !");

    vpss_exit();
    printf("vpss exit done\n");

    vi_exit();
    printf("vi exit done\n");

    sys_exit();
    printf("sys exit done\n");

    //uart_exit();
    printf("uart exit done\n");
}