/*****************************************************************************
*  Copyright (C) 2014 - 2017, Justin Mo.
*  All rights reserverd.
*
*  FileName:  nv_enc.cpp
*  Author:     Justin Mo(mojing1999@gmail.com)
*  Date:        2017-05-08
*  Version:    V0.01
*  Desc:       This file implement NVIDIA VIDEO ENCODE(NVENC) INTERFACE
*****************************************************************************/

#include <stdio.h>
#include <tchar.h>
#include <string.h>


#include "nv_enc.h"

using namespace std;

//#define  LOG printf
#ifdef _DEBUG
#define LOG( ... )				printf( __VA_ARGS__ )
#else
#define LOG( ... )              
#endif

nvenc_ctx *nvenc_ctx_create()
{
	nvenc_ctx *ctx = NULL;

	ctx = new nvenc_ctx;
	memset(ctx, 0x0, sizeof(nvenc_ctx));

	//nv encode param
	SET_VER(ctx->nvenc_param, NV_ENC_INITIALIZE_PARAMS);
	// nv encode config
	SET_VER(ctx->nvenc_config, NV_ENC_CONFIG);


	return ctx;
}

int nvenc_encode_init(nv_enc_param *in_param, nvenc_ctx *ctx)
{
	int ret = 0;
	
	ret = nvenc_loading_libraries(ctx);
	// param init
	ret = nvenc_param_init(in_param, ctx);

	// alloc io buffer
	ret = nvenc_setup_surfaces(ctx);
	

	return ret;
}

int nvenc_encode_deinit(nvenc_ctx *ctx)
{
	// release io buffer
	int ret = nvenc_encoder_close(ctx);


	// TODO:



	delete ctx;
	
	return 0;
}

static uint32_t index_enc_frame = 0;

int nvenc_encode_enc_frame(const unsigned char *in_yuv_buf, const int yuv_len, int *got_packet, nvenc_ctx *ctx)
{
	int ret = 0;
	int is_stop_enc = 0;
    NVENCSTATUS nv_status;
	nvenc_surface *in_surf = NULL;
    NV_ENCODE_API_FUNCTION_LIST *pf_nvenc = ctx->encode_apis;
	
    NV_ENC_PIC_PARAMS pic_params = { 0 };
	SET_VER(pic_params, NV_ENC_PIC_PARAMS);

	if(in_yuv_buf && yuv_len > 0) {	// else stop encoder
		in_surf = nvenc_get_free_frame(ctx);

		if(!in_surf) {
			// Error: can not get free surfaces
			return -1;
		}

		// copy yuv data to surface
		ret = nvenc_upload_frame(in_yuv_buf, yuv_len, in_surf, ctx);

		//
		pic_params.inputBuffer 		= in_surf->input_surface;
		pic_params.bufferFmt = NV_ENC_BUFFER_FORMAT_NV12;// ctx->format;
		pic_params.inputWidth 		= ctx->width;
		pic_params.inputHeight 		= ctx->height;

		//pic_params.inputPitch 		= ctx->pitch;//in_surf->pitch;
		pic_params.outputBitstream 	= in_surf->output_surface;
		pic_params.completionEvent	= in_surf->event_output;
		
		pic_params.pictureStruct 	= NV_ENC_PIC_STRUCT_FRAME;
		pic_params.encodePicFlags 	= 0;
		pic_params.inputTimeStamp 	= index_enc_frame++;	// frame index

		nvenc_codec_specific_pic_params(&pic_params, ctx);

	}
	else {
		// EOF
        pic_params.encodePicFlags = NV_ENC_PIC_FLAG_EOS;
		is_stop_enc = 1;
	}

    nv_status = pf_nvenc->nvEncEncodePicture(ctx->nv_encoder, &pic_params);
    if (nv_status != NV_ENC_SUCCESS &&
        nv_status != NV_ENC_ERR_NEED_MORE_INPUT) {
		// EncodePicture failed
		return -1;
    }


	// output bitstream
	if(!is_stop_enc) {
		// put current frame to output list, only nvEncEncodePicture return NV_ENC_SUCCESS, 
		// encoder output bitstream
		ret = nvenc_push_surface_to_list(in_surf, ctx);
	}

	if(NV_ENC_SUCCESS == nv_status) {
		// TODO:  move output list surface to output ready list,
		// output ready list all frames are now ready for output
		ret = nvenc_check_ready_bitstream(ctx);
#if 0
        while (av_fifo_size(ctx->output_surface_queue) > 0) {
            av_fifo_generic_read(ctx->output_surface_queue, &tmpoutsurf, sizeof(tmpoutsurf), NULL);
            av_fifo_generic_write(ctx->output_surface_ready_queue, &tmpoutsurf, sizeof(tmpoutsurf), NULL);
        }
#endif
	}
	
	if (nvenc_is_output_ready(ctx)) {
		// 
		*got_packet = 1;
	}
	else {
		*got_packet = 0;
	}
	
	return 0;
}

/*
  *		@output encode data(H264/H265), release surfaces
  */
int nvenc_get_output_bitstream(unsigned char *out_buf, int *out_data_len, int *is_keyframe, nvenc_ctx *ctx)
{
	int ret = 0;
	NV_ENCODE_API_FUNCTION_LIST *pf_nvenc = ctx->encode_apis;

	unsigned int slice_mode_data;
	unsigned int *slice_offsets = NULL;
	NV_ENC_LOCK_BITSTREAM lock_params = { 0 };
	NVENCSTATUS nv_status;

	// get output surface from output ready list.
	nvenc_surface *out_surf = nvenc_pop_list_output_ready_item(ctx);

	if (NULL == out_surf) {
		*out_data_len = 0;
		return -1;
	}

	*out_data_len = 0;
	*is_keyframe = 0;

	if (NV_ENC_H264 == ctx->codec_id) {
		slice_mode_data = ctx->nvenc_config.encodeCodecConfig.h264Config.sliceModeData;
	}
	else {
		slice_mode_data = ctx->nvenc_config.encodeCodecConfig.hevcConfig.sliceModeData;
	}

	slice_offsets = new unsigned int[slice_mode_data * sizeof(unsigned int)];

	SET_VER(lock_params, NV_ENC_LOCK_BITSTREAM);
	lock_params.doNotWait = 0;
	lock_params.outputBitstream = out_surf->output_surface;
	lock_params.sliceOffsets = slice_offsets;

	nv_status = pf_nvenc->nvEncLockBitstream(ctx->nv_encoder, &lock_params);
	if (NV_ENC_SUCCESS != nv_status) {
		//
		delete[] slice_offsets;
		return -2;
	}

	memcpy(out_buf, lock_params.bitstreamBufferPtr, lock_params.bitstreamSizeInBytes);
	*out_data_len = lock_params.bitstreamSizeInBytes;

	nv_status = pf_nvenc->nvEncUnlockBitstream(ctx->nv_encoder, out_surf->output_surface);
	if (NV_ENC_SUCCESS != nv_status) {
		// Failed unlocking bitstream buffer, expect the gates of mordor to open
	}

	if (ctx->is_external_alloc) {
		// un map
		pf_nvenc->nvEncUnmapInputResource(ctx->nv_encoder, out_surf->in_map.mappedResource);

		ctx->arr_reg_frames[out_surf->reg_idx].mapped = 0;
		out_surf->input_surface = NULL;
	}

	if ((NV_ENC_PIC_TYPE_IDR == lock_params.pictureType) ||
		(NV_ENC_PIC_TYPE_I == lock_params.pictureType)) {
		*is_keyframe = 1;
	}

	out_surf->lock_count = 0;

	delete[] slice_offsets;

	return ret;
}

NVENCSTATUS nvenc_cuda_init(nvenc_ctx *ctx)
{
	CUresult cuda_ret = CUDA_SUCCESS;

	CUdevice cuda_dev = 0;
	CUcontext cuda_ctx_dummy;

	int dev_count = 0;
	int dev_id = 0;		// default use device 0.
	int minor = 0, major = 0;


	// load nv library "nvcuda.dll" and init cuda intefaces
//	__cu(cuInit(0, __CUDA_API_VERSION, &ctx->cuda_lib));
	__cu(cuInit(CU_CTX_SCHED_AUTO, __CUDA_API_VERSION, &ctx->cuda_lib));

	__cu(cuDeviceGetCount(&dev_count));
	if (0 == dev_count) {
		// can not find cuda devices
		return NV_ENC_ERR_NO_ENCODE_DEVICE;
	}

	//
	ctx->cuda_dev_count = dev_count;

	if (dev_id > dev_count - 1) {
		// invalid device id
		LOG("Error: invalid device id[%d]\n", dev_id);
		return NV_ENC_ERR_INVALID_ENCODERDEVICE;
	}

	// get the actual device
	__cu(cuDeviceGet(&cuda_dev, dev_id));

	// check device capability
	__cu(cuDeviceComputeCapability(&major, &minor, dev_id));
	if (((major << 4) + minor) < 0x30) {
		//
		LOG("GPU %d does not have NVENC capabilities exiting\n", dev_id);
		return NV_ENC_ERR_NO_ENCODE_DEVICE;
	}

	// create the cuda context and pop the current one
	__cu(cuCtxCreate(&ctx->cuda_ctx, 0, cuda_dev));

	// preproc32_lowlat.ptx
#define PTX_FILE_NAME ("preproc32_lowlat.ptx")
	// in this branch we use compilation with parameters
	const unsigned int jitNumOptions = 3;
	CUjit_option *jitOptions = new CUjit_option[jitNumOptions];
	void **jitOptVals = new void *[jitNumOptions];

	// set up size of compilation log buffer
	jitOptions[0] = CU_JIT_INFO_LOG_BUFFER_SIZE_BYTES;
	int jitLogBufferSize = 1024;
	jitOptVals[0] = (void *)(size_t)jitLogBufferSize;

	// set up pointer to the compilation log buffer
	jitOptions[1] = CU_JIT_INFO_LOG_BUFFER;
	char *jitLogBuffer = new char[jitLogBufferSize];
	jitOptVals[1] = jitLogBuffer;

	// set up pointer to set the Maximum # of registers for a particular kernel
	jitOptions[2] = CU_JIT_MAX_REGISTERS;
	int jitRegCount = 32;
	jitOptVals[2] = (void *)(size_t)jitRegCount;

	string ptx_source;
	FILE *fp = fopen(PTX_FILE_NAME, "rb");
	if (!fp)
	{
		LOG("Unable to read ptx file %s\n", PTX_FILE_NAME);
		return NV_ENC_ERR_INVALID_PARAM;
	}
	fseek(fp, 0, SEEK_END);
	int file_size = ftell(fp);
	char *buf = new char[file_size + 1];
	fseek(fp, 0, SEEK_SET);
	fread(buf, sizeof(char), file_size, fp);
	fclose(fp);
	buf[file_size] = '\0';
	ptx_source = buf;
	delete[] buf;

	CUresult cuResult = cuModuleLoadDataEx(&ctx->cu_module, ptx_source.c_str(), jitNumOptions, jitOptions, (void **)jitOptVals);
	if (cuResult != CUDA_SUCCESS)
	{
		return NV_ENC_ERR_OUT_OF_MEMORY;
	}

	delete[] jitOptions;
	delete[] jitOptVals;
	delete[] jitLogBuffer;

	__cu(cuModuleGetFunction(&ctx->cuInterleaveUVFunction, ctx->cu_module, "InterleaveUV"));
	//__cu(cuModuleGetFunction(&m_cuScaleNV12Function, m_cuModule, "Scale_Bilinear_NV12"));

	//m_texLuma2D = InitTexture(m_cuModule, "luma_tex", CU_AD_FORMAT_UNSIGNED_INT8, 1);
	//m_texChroma2D = InitTexture(m_cuModule, "chroma_tex", CU_AD_FORMAT_UNSIGNED_INT8, 2);



	__cu(cuCtxPopCurrent(&cuda_ctx_dummy));

	return NV_ENC_SUCCESS;
}

/*
 *		Load nv .dll libraries "nvcuda.dll" and "nvEncodeAPI64.dll"(or "nvEncodeAPI.dll")
 *
 */
int nvenc_loading_libraries(nvenc_ctx *ctx)
{
	NVENCSTATUS nvStatus = NV_ENC_SUCCESS;

	// Loading "nvcuda.dll"
	nvStatus = nvenc_cuda_init(ctx);

	// Loading "nvcuda.dll"
	MYPROC nvEncodeAPICreateInstance; // function pointer to create instance in nvEncodeAPI

#if defined (_WIN64)
	ctx->nvenc_lib = LoadLibrary(TEXT("nvEncodeAPI64.dll"));
#else
	ctx->nvenc_lib = LoadLibrary(TEXT("nvEncodeAPI.dll"));
#endif

	if (!ctx->nvenc_lib) {
		// Error
		return NV_ENC_ERR_OUT_OF_MEMORY;
	}

	nvEncodeAPICreateInstance = (MYPROC)GetProcAddress(ctx->nvenc_lib, "NvEncodeAPICreateInstance");
	if (nvEncodeAPICreateInstance == NULL)
		return NV_ENC_ERR_OUT_OF_MEMORY;

	ctx->encode_apis = new NV_ENCODE_API_FUNCTION_LIST;
	if (ctx->encode_apis == NULL)
		return NV_ENC_ERR_OUT_OF_MEMORY;

	memset(ctx->encode_apis, 0, sizeof(NV_ENCODE_API_FUNCTION_LIST));
	ctx->encode_apis->version = NV_ENCODE_API_FUNCTION_LIST_VER;
	nvStatus = nvEncodeAPICreateInstance(ctx->encode_apis);
	if (nvStatus != NV_ENC_SUCCESS) {
		return nvStatus;
	}

	//
	NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS params = { 0 };// openSessionExParams;
	SET_VER(params, NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS);
	params.device = ctx->cuda_ctx;
	params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
	params.apiVersion = NVENCAPI_VERSION;
	nvStatus = ctx->encode_apis->nvEncOpenEncodeSessionEx(&params, &ctx->nv_encoder);
	if (nvStatus != NV_ENC_SUCCESS) {
		return nvStatus;
	}

	return NV_ENC_SUCCESS;
}

int nvenc_encoder_close(nvenc_ctx *ctx)
{
	int ret = 0;
	int i = 0;
	NVENCSTATUS nv_status = NV_ENC_SUCCESS;
	NV_ENCODE_API_FUNCTION_LIST *pf_nvenc = ctx->encode_apis;

	if (ctx->nv_encoder) {
		NV_ENC_PIC_PARAMS params = { 0 };
		SET_VER(params, NV_ENC_PIC_PARAMS);
		params.encodePicFlags = NV_ENC_PIC_FLAG_EOS;

		nv_status = pf_nvenc->nvEncEncodePicture(ctx->nv_encoder, &params);
	}

	// un map and register
	if (ctx->is_external_alloc) {
		for (i = 0; i < ctx->nb_surfaces; i++) {
			if (ctx->arr_surfaces[i].input_surface) {
				nv_status = pf_nvenc->nvEncUnmapInputResource(ctx->nv_encoder, ctx->arr_surfaces[i].in_map.mappedResource);
			}

			// release yuv buffer later

			// release output
			nv_status = pf_nvenc->nvEncDestroyBitstreamBuffer(ctx->nv_encoder, ctx->arr_surfaces[i].output_surface);
		}

		for (i = 0; i < ctx->nb_reg_frames; i++) {
			if (ctx->arr_reg_frames[i].reg_ptr) {
				nv_status = pf_nvenc->nvEncUnregisterResource(ctx->nv_encoder, ctx->arr_reg_frames[i].reg_ptr);
			}
		}

		//
		//ret = nvenc_release_yuv_buffer(ctx);
		//
		ret = nvenc_release_reg_frames(ctx);
		ctx->nb_reg_frames = 0;
	}
	else {
		//
		for (i = 0; i < ctx->nb_surfaces; i++) {
			nv_status = pf_nvenc->nvEncDestroyInputBuffer(ctx->nv_encoder, ctx->arr_surfaces[i].input_surface);
			nv_status = pf_nvenc->nvEncDestroyBitstreamBuffer(ctx->nv_encoder, ctx->arr_surfaces[i].output_surface);
		}

	}

	ctx->nb_surfaces = 0;

	// destroy encoder
	nv_status = pf_nvenc->nvEncDestroyEncoder(ctx->nv_encoder);
	ctx->nv_encoder = NULL;
	ctx->is_init = 0;
	FreeLibrary(ctx->nvenc_lib);

	// CUDA
	__cu(cuCtxDestroy(ctx->cuda_ctx));
	ctx->cuda_ctx = NULL;
	FreeLibrary(ctx->cuda_lib);//

	ctx->cuda_dev_count = 0;

	return ret;
}
/*
 *		nv encode param init
 *
 */
int nvenc_param_init(nv_enc_param *in_param, nvenc_ctx *ctx)
{
	NVENCSTATUS nvStatus = NV_ENC_SUCCESS;
	NV_ENCODE_API_FUNCTION_LIST *pf_nvenc = ctx->encode_apis;

	SET_VER(ctx->nvenc_param, NV_ENC_INITIALIZE_PARAMS);
	NV_ENC_INITIALIZE_PARAMS	*param = &ctx->nvenc_param;

	ctx->codec_id = in_param->codec_id;	// H.264 or H.265
	
	GUID in_guid = in_param->codec_id == NV_ENC_H264 ? NV_ENC_CODEC_H264_GUID : NV_ENC_CODEC_HEVC_GUID;
	// check encode GUID
	nvStatus = check_validate_encode_guid(in_guid, ctx);
	if (NV_ENC_SUCCESS != nvStatus) {
		// Error
		return nvStatus;
	}


	ctx->codec_guid = in_guid;

	ctx->is_external_alloc = in_param->is_external_alloc;
	ctx->width = in_param->src_width;
	ctx->height = in_param->src_height;
//	ctx->pitch = ctx->width;
	ctx->format = (NV_ENC_BUFFER_FORMAT)in_param->in_fmt;


	param->encodeGUID = in_guid;
	param->presetGUID = nvenc_get_preset_guid(in_param->preset);

	param->encodeWidth		= in_param->src_width;
	param->encodeHeight		= in_param->src_height;
	param->darWidth			= in_param->dst_width;
	param->darHeight		= in_param->dst_height;

	param->frameRateNum		= in_param->fps;
	param->frameRateDen		= 1;

	param->enableEncodeAsync	= 0;
	param->enablePTD			= 1;
	param->reportSliceOffsets	= 0;
	param->enableSubFrameWrite	= 0;

	param->encodeConfig			= &ctx->nvenc_config;

	param->maxEncodeWidth		= in_param->src_width;
	param->maxEncodeHeight		= in_param->src_height;

	// apply preset
	NV_ENC_PRESET_CONFIG preset_cfg;
	memset(&preset_cfg, 0x0, sizeof(NV_ENC_PRESET_CONFIG));
	SET_VER(preset_cfg, NV_ENC_PRESET_CONFIG);
	SET_VER(preset_cfg.presetCfg, NV_ENC_CONFIG);

	nvStatus = pf_nvenc->nvEncGetEncodePresetConfig(ctx->nv_encoder,
		param->encodeGUID, param->presetGUID, &preset_cfg);
	if (NV_ENC_SUCCESS != nvStatus) {
		// Error
		return nvStatus;
	}

	memcpy(&ctx->nvenc_config, &preset_cfg.presetCfg, sizeof(NV_ENC_CONFIG));

	ctx->nvenc_config.gopLength = in_param->gop_len;
	ctx->nvenc_config.frameIntervalP = in_param->num_bframe + 1;

	//
	ctx->nvenc_config.frameFieldMode = NV_ENC_PARAMS_FRAME_FIELD_MODE_FRAME;
	ctx->nvenc_config.mvPrecision = NV_ENC_MV_PRECISION_QUARTER_PEL;

	ctx->nvenc_config.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;// in_param->rc_mode;
	ctx->nvenc_config.rcParams.averageBitRate = in_param->bitrate_kb * 1000;
	//ctx->nvenc_config.rcParams.maxBitRate = in_param->bitrate_kb * 1000 * 2;

	if (NV_ENC_HEVC == in_param->codec_id) {
		ctx->nvenc_config.encodeCodecConfig.hevcConfig.chromaFormatIDC = 1;
	}
	else {
		ctx->nvenc_config.encodeCodecConfig.h264Config.chromaFormatIDC =  1;
	}

#if 0
	// intraRefreshEnableFlag
	if (NV_ENC_HEVC == in_param->codec_id) {
		ctx->nvenc_config.encodeCodecConfig.hevcConfig.enableIntraRefresh = 1;
		ctx->nvenc_config.encodeCodecConfig.hevcConfig.intraRefreshPeriod = 0;// change if need
		ctx->nvenc_config.encodeCodecConfig.hevcConfig.intraRefreshCnt = 0;		// user can change in_param
	}
	else {
		ctx->nvenc_config.encodeCodecConfig.h264Config.enableIntraRefresh = 1;
		ctx->nvenc_config.encodeCodecConfig.h264Config.intraRefreshPeriod = 0;
		ctx->nvenc_config.encodeCodecConfig.h264Config.intraRefreshCnt = 0;
	}

	if (NV_ENC_HEVC == in_param->codec_id) {
		enc_ctx->nvenc_config.encodeCodecConfig.hevcConfig.maxNumRefFramesInDPB = 16;
	}
	else{
		enc_ctx->nvenc_config.encodeCodecConfig.h264Config.maxNumRefFrames = 16;
	}
#endif

	if (NV_ENC_HEVC == in_param->codec_id) {
		ctx->nvenc_config.encodeCodecConfig.hevcConfig.idrPeriod = in_param->gop_len;
	}
	else if (NV_ENC_H264 == in_param->codec_id)
	{
		ctx->nvenc_config.encodeCodecConfig.h264Config.idrPeriod = in_param->gop_len;
	}


	//
	NV_ENC_CAPS_PARAM caps_param = { 0 };
	int async_mode = 0;
	SET_VER(caps_param, NV_ENC_CAPS_PARAM);

	caps_param.capsToQuery = NV_ENC_CAPS_ASYNC_ENCODE_SUPPORT;
	pf_nvenc->nvEncGetEncodeCaps(ctx->nv_encoder, param->encodeGUID, &caps_param, &async_mode);
	param->enableEncodeAsync = async_mode;
	ctx->async_mode = async_mode;

	nvenc_init(ctx);

#if 0	// if enableMEOnly = 1 or 2
	caps_param.capsToQuery = NV_ENC_CAPS_SUPPORT_MEONLY_MODE;
	param->enableMEOnlyMode = true;
	int meonly_mode = 0;
	nvStatus = pf_nvenc->nvEncGetEncodeCaps(enc_ctx->nv_encoder, param->encodeGUID, &caps_param, &meonly_mode);
	if (NV_ENC_SUCCESS != nvStatus) {
		// Error
		return nvStatus;
	}
	else {
		if (meonly_mode == 1) {
			// support 
		}
		else {
			// error
			return NV_ENC_ERR_UNSUPPORTED_DEVICE;
		}
	}
#endif

	return 0;
}

NVENCSTATUS check_validate_encode_guid(GUID in_guid, nvenc_ctx *ctx)
{
	NVENCSTATUS nvStatus = NV_ENC_SUCCESS;
    NV_ENCODE_API_FUNCTION_LIST *pf_nvenc = ctx->encode_apis;

	unsigned int guid_count = 0, is_found = 0;
	unsigned int arr_enc_guid_size = 0;
	GUID *arr_enc_guid;

	nvStatus = pf_nvenc->nvEncGetEncodeGUIDCount(ctx->nv_encoder, &guid_count);
	if (NV_ENC_SUCCESS != nvStatus) {
		// Error
		return nvStatus;
	}

	arr_enc_guid = new GUID[guid_count];
	memset(arr_enc_guid, 0x0, sizeof(GUID)*guid_count);

	nvStatus = pf_nvenc->nvEncGetEncodeGUIDs(ctx->nv_encoder, arr_enc_guid, guid_count, &arr_enc_guid_size);
	if (NV_ENC_SUCCESS != nvStatus) {
		// Error
		delete[] arr_enc_guid;
		return nvStatus;
	}

	//assert(arr_enc_guid_size <= guid_count);

	is_found = 0;
	for (int i = 0; i < arr_enc_guid_size; i++) {
		if (in_guid == arr_enc_guid[i]) {
			is_found = 1;
			break;
		}
	}

	delete[] arr_enc_guid;

	if (is_found)
		return NV_ENC_SUCCESS;
	else
		return NV_ENC_ERR_INVALID_PARAM;
}

GUID nvenc_get_preset_guid(int in_preset) 
{
	switch (in_preset)
	{
	case 1:	// "hp"
		return NV_ENC_PRESET_HP_GUID;

	case 2: // "hq"
		return NV_ENC_PRESET_HQ_GUID;

	case 3:	// "latency hq"
		return NV_ENC_PRESET_LOW_LATENCY_HQ_GUID;

	case 4:	// "latency hp"
		return NV_ENC_PRESET_LOW_LATENCY_HP_GUID;

	default:
		return NV_ENC_PRESET_DEFAULT_GUID;
	}

	return NV_ENC_PRESET_DEFAULT_GUID;
}

NVENCSTATUS nvenc_init(nvenc_ctx *ctx)
{
	NVENCSTATUS nvStatus = NV_ENC_SUCCESS;

	nvStatus = ctx->encode_apis->nvEncInitializeEncoder(ctx->nv_encoder, &ctx->nvenc_param);
	if (NV_ENC_SUCCESS != nvStatus) {
		// error
		return nvStatus;
	}

	nvenc_get_spspps(ctx);

	ctx->async_depth = 1;	// low latency
	ctx->is_init = 1;

	//
	nvenc_debug_show_support_input_format(ctx);

	return nvStatus;
}

int nvenc_create_yuv_buffer(int nb_buf, nvenc_ctx *ctx)
{
	int yuv_len = 0;
	
	ctx->arr_yuv_buf = new nv_frame_buf[nb_buf];
	ctx->nb_yuv_buf = nb_buf;
	memset(ctx->arr_yuv_buf, 0x0, ctx->nb_yuv_buf * sizeof(nv_frame_buf));
	// yuv format
	if(NV_ENC_BUFFER_FORMAT_NV12 == ctx->format || 
		NV_ENC_BUFFER_FORMAT_YV12 == ctx->format) {
		// YUV 420 12 bit for pixel
		yuv_len = ctx->width * ctx->height * 3 / 2;
	}
	else {
		// YUV 444 24 bit for pixel
		yuv_len = ctx->width * ctx->height * 4;
	}
	
	
	for(int i = 0; i < ctx->nb_yuv_buf; i++) {
		ctx->arr_yuv_buf[i].big_buf_len = yuv_len;
		//ctx->arr_yuv_buf[i].big_buf = new unsigned char[yuv_len];
#if 0
		unsigned int nv12_strid = 1920;
		CUdeviceptr nv12_prt;
		CUcontext ctx_old = ctx->cuda_ctx;
		cuCtxPushCurrent(ctx_old);
		__cu(cuMemAllocPitch((CUdeviceptr *)&ctx->arr_yuv_buf[i].big_buf, (size_t *)&nv12_strid, ctx->width, ctx->height * 3 / 2, 16));
		//CUresult err = cuMemAllocPitch(&nv12_prt, &nv12_strid, ctx->width, ctx->height, 16);

		cuCtxPopCurrent(&ctx_old);
		//ctx->arr_yuv_buf[i].big_buf = (unsigned char *)nv12_prt;
#endif 
		// TODO:  current support for YUV420
		ctx->arr_yuv_buf[i].line_size[0] = ctx->width;
		ctx->arr_yuv_buf[i].yuv_addr[0] = ctx->arr_yuv_buf[i].big_buf;
		
		ctx->arr_yuv_buf[i].line_size[1] = ctx->width/2;
		ctx->arr_yuv_buf[i].yuv_addr[1] = ctx->arr_yuv_buf[i].big_buf + ctx->width*ctx->height;
		
		ctx->arr_yuv_buf[i].line_size[2] = ctx->width/2;
		ctx->arr_yuv_buf[i].yuv_addr[2] = ctx->arr_yuv_buf[i].yuv_addr[1] + ctx->width*ctx->height / 4;
	}


	return 0;

}

int nvenc_release_yuv_buffer(nvenc_ctx *ctx)
{

	for(int i = 0; i < ctx->nb_yuv_buf; i++) {
		delete [] ctx->arr_yuv_buf[i].big_buf;
	}

	
	delete [] ctx->arr_yuv_buf;
	ctx->nb_yuv_buf = 0;
	ctx->arr_yuv_buf = NULL;

	return 0;
}

#if 0
int nvenc_extern_alloc_surface(nvenc_ctx *ctx)
{

}
#endif

int nvenc_setup_surfaces(nvenc_ctx *ctx)
{
	int ret = 0;

	// surfaces
	ctx->arr_surfaces = new nvenc_surface[MAX_NV_ENC_FRAME_NUM];
	if (!ctx->arr_surfaces)	{
		// error: can not alloc
		return -1;
	}
	ctx->nb_surfaces = MAX_NV_ENC_FRAME_NUM;
	memset(ctx->arr_surfaces, 0x0, ctx->nb_surfaces * sizeof(nvenc_surface));


	// if is_external_alloc = 1, create system memory
	if(ctx->is_external_alloc) {
		//ret = nvenc_create_yuv_buffer(ctx->nb_surfaces, ctx);
		// TODO: alloc cuda surface
		// create register frame
		ret = nvenc_create_reg_frames(ctx->nb_surfaces, ctx);
	}

	// alloc surface
	for (int i = 0; i < ctx->nb_surfaces; i++) {
		ret = nvenc_alloc_surfaces(i, ctx);
	}






	ctx->list_output = new std::list<nvenc_surface *>;
	ctx->list_output_ready = new std::list<nvenc_surface *>;
#define MUTEX_NAME_OUTPUT	_T("mutex_output_ready")
	ctx->mutex_ouput_ready = CreateMutex(NULL, FALSE, MUTEX_NAME_OUTPUT);//MUTEX_NAME_DEC_BS



	return 0;
}

int nvenc_create_reg_frames(int nb_frames, nvenc_ctx *ctx)
{
	
	ctx->arr_reg_frames = new nv_reg_frame[nb_frames];
	ctx->nb_reg_frames = nb_frames;

	memset(ctx->arr_reg_frames, 0x0, ctx->nb_reg_frames * sizeof(nv_reg_frame));



	return 0;
}

int nvenc_release_reg_frames(nvenc_ctx *ctx)
{
	if(ctx->arr_reg_frames) {
		delete [] ctx->arr_reg_frames;

		ctx->arr_reg_frames = NULL;
		ctx->nb_reg_frames = 0;
	}
	
	return 0;
}


int nvenc_alloc_surfaces(int idx, nvenc_ctx *ctx)
{
	NVENCSTATUS nvStatus = NV_ENC_SUCCESS;
	NV_ENCODE_API_FUNCTION_LIST *pf_nvenc = ctx->encode_apis;
	NV_ENC_CREATE_BITSTREAM_BUFFER out_buf = { 0 };
	SET_VER(out_buf, NV_ENC_CREATE_BITSTREAM_BUFFER);

	if (ctx->is_external_alloc) {
		// alloc surface in system memory, nvenc_surface.yuv_buf;

		// register frame
		nvenc_register_frame(idx, ctx);
		
	}
	else {
		NV_ENC_CREATE_INPUT_BUFFER allocSurf = { 0 };
		SET_VER(allocSurf, NV_ENC_CREATE_INPUT_BUFFER);
		allocSurf.width = (ctx->width + 31) & ~31;
		allocSurf.height = (ctx->height + 31) & ~31;
		allocSurf.memoryHeap = NV_ENC_MEMORY_HEAP_SYSMEM_CACHED;
		allocSurf.bufferFmt = ctx->format;

		nvStatus = pf_nvenc->nvEncCreateInputBuffer(ctx->nv_encoder, &allocSurf);
		if (nvStatus != NV_ENC_SUCCESS) {
			// Error, can not create input buffer in video memory
			return -1;
		}

		ctx->arr_surfaces[idx].input_surface = allocSurf.inputBuffer;
		//ctx->arr_surfaces[idx]
	}

	ctx->arr_surfaces[idx].lock_count = 0;


	if (ctx->async_mode) {
		// register async event

		NV_ENC_EVENT_PARAMS eventParams;

		memset(&eventParams, 0, sizeof(eventParams));
		SET_VER(eventParams, NV_ENC_EVENT_PARAMS);


		eventParams.completionEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

		nvStatus = pf_nvenc->nvEncRegisterAsyncEvent(ctx->nv_encoder, &eventParams);
		if (nvStatus != NV_ENC_SUCCESS)
		{
			// error
			return -2;
		}

		ctx->arr_surfaces[idx].event_output = eventParams.completionEvent;

		ctx->arr_surfaces[idx].is_wait_for_event = true;
	}



#define  MAX_OUTPUT_BS_SIZE (3*1024*1024)	// 3 MB
	out_buf.memoryHeap = NV_ENC_MEMORY_HEAP_SYSMEM_CACHED;
	out_buf.size = MAX_OUTPUT_BS_SIZE;
	nvStatus = pf_nvenc->nvEncCreateBitstreamBuffer(ctx->nv_encoder, &out_buf);
	if (NV_ENC_SUCCESS != nvStatus) {
		// Error, can not alloc output bitstream buffer
		if (ctx->is_external_alloc) {
			pf_nvenc->nvEncDestroyInputBuffer(ctx->nv_encoder, ctx->arr_surfaces[idx].input_surface);
		}
		return nvStatus;
	}

	//
	ctx->arr_surfaces[idx].output_surface = out_buf.bitstreamBuffer;
	ctx->arr_surfaces[idx].output_bs_size = out_buf.size;

	return 0;
}


nvenc_surface *nvenc_get_free_frame(nvenc_ctx *ctx)
{
	int i = 0;
	for(i = 0; i < ctx->nb_surfaces; i++) {
		if(!ctx->arr_surfaces[i].lock_count) {
			ctx->arr_surfaces[i].lock_count = 1;
			return &ctx->arr_surfaces[i];
		}
	}

	return NULL;
}

void nvenc_codec_specific_pic_params(NV_ENC_PIC_PARAMS *params, nvenc_ctx *ctx)
{
	switch(ctx->codec_id) {
	case NV_ENC_H264:
        params->codecPicParams.h264PicParams.sliceMode =
            ctx->nvenc_config.encodeCodecConfig.h264Config.sliceMode;
        params->codecPicParams.h264PicParams.sliceModeData =
            ctx->nvenc_config.encodeCodecConfig.h264Config.sliceModeData;

		break;
	case NV_ENC_HEVC:
        params->codecPicParams.hevcPicParams.sliceMode =
            ctx->nvenc_config.encodeCodecConfig.hevcConfig.sliceMode;
        params->codecPicParams.hevcPicParams.sliceModeData =
            ctx->nvenc_config.encodeCodecConfig.hevcConfig.sliceModeData;

		break;
	}
}


/*
  *		@register all yuv frame buffer
  *
  */
int nvenc_register_frame(int idx, nvenc_ctx *ctx)
{
	NVENCSTATUS nv_status = NV_ENC_SUCCESS;
	// if input buffer use system memory, need register to nv device
	NV_ENCODE_API_FUNCTION_LIST *pf_nvenc = ctx->encode_apis;

	NV_ENC_REGISTER_RESOURCE reg = { 0 };
	SET_VER(reg, NV_ENC_REGISTER_RESOURCE);

	// TODO: alloc	CUdeviceptr in_cuda_surf
	CCudaAutoLock cuLock(ctx->cuda_ctx);

	if (NV_ENC_BUFFER_FORMAT_NV12 == ctx->format) {
		__cu(cuMemAllocPitch((CUdeviceptr *)&ctx->arr_surfaces[idx].in_cuda_surf, &ctx->arr_surfaces[idx].in_cuda_stride, ctx->width, ctx->height * 3 / 2, 16));
	}
	else if (NV_ENC_BUFFER_FORMAT_YV12 == ctx->format) {
		__cu(cuMemAllocPitch((CUdeviceptr *)&ctx->arr_surfaces[idx].in_cuda_surf, &ctx->arr_surfaces[idx].in_cuda_stride, ctx->width, ctx->height * 3 / 2, 16));
		// for UV temp ptr
		__cu(cuMemAlloc(&ctx->uv_tmp_ptr[0], ctx->width * ctx->height / 4));
		__cu(cuMemAlloc(&ctx->uv_tmp_ptr[1], ctx->width * ctx->height / 4));


	}
	else if (NV_ENC_BUFFER_FORMAT_ARGB == ctx->format
			|| NV_ENC_BUFFER_FORMAT_ABGR == ctx->format) {
		__cu(cuMemAllocPitch((CUdeviceptr *)&ctx->arr_surfaces[idx].in_cuda_surf, &ctx->arr_surfaces[idx].in_cuda_stride, ctx->width * 4, ctx->height, 16));
	}

	//nv_frame_buf *yuv_frame = ctx->arr_surfaces[idx].yuv_buf;
	
	reg.resourceType	   = NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR;
	reg.width			   = ctx->width;
    reg.height             = ctx->height;
	reg.bufferFormat = NV_ENC_BUFFER_FORMAT_NV12;// ctx->format;
    reg.pitch              = ctx->arr_surfaces[idx].in_cuda_stride;
    reg.resourceToRegister = (void *)ctx->arr_surfaces[idx].in_cuda_surf;

	//CUcontext ctx_old = ctx->cuda_ctx;
	//cuCtxPushCurrent(ctx_old);

	nv_status = pf_nvenc->nvEncRegisterResource(ctx->nv_encoder, &reg);
	

	if(NV_ENC_SUCCESS != nv_status) {
		// Error registering an input resource
		return -2;
	}

	ctx->arr_reg_frames[idx].ptr = ctx->arr_surfaces[idx].in_cuda_surf;
	ctx->arr_reg_frames[idx].reg_ptr = reg.registeredResource;
	
	
	return 0;
}

int nvenc_get_register_frame_index(nvenc_surface *nv_frame, nvenc_ctx *ctx)
{
	int i = 0;

	
	for(i = 0; i < ctx->nb_reg_frames; i++) {
		if(ctx->arr_reg_frames[i].ptr == ctx->arr_surfaces[i].in_cuda_surf)
			return i;
	}

	// error, we will register all frame in init api
	return -1;
}

int nvenc_convert_yuv_data_to_nv12(const unsigned char *in_yuv_buf, const int yuv_len, nvenc_surface *nv_frame, nvenc_ctx *ctx)
{
	CCudaAutoLock cuLock(ctx->cuda_ctx);
	// copy luma
	CUDA_MEMCPY2D copyParam;
	memset(&copyParam, 0, sizeof(copyParam));
	if (NV_ENC_BUFFER_FORMAT_NV12 == ctx->format) {
		// copy NV12
		copyParam.dstMemoryType = CU_MEMORYTYPE_DEVICE;
		copyParam.dstDevice = nv_frame->in_cuda_surf;
		copyParam.dstPitch = nv_frame->in_cuda_stride;
		copyParam.srcMemoryType = CU_MEMORYTYPE_HOST;
		copyParam.srcHost = in_yuv_buf;
		copyParam.srcPitch = ctx->width;
		copyParam.WidthInBytes = ctx->width;
		copyParam.Height = ctx->height  * 3 / 2;
		__cu(cuMemcpy2D(&copyParam));
	}
	else if (NV_ENC_BUFFER_FORMAT_YV12 == ctx->format) {
		// TODO: YV12
		copyParam.dstMemoryType = CU_MEMORYTYPE_DEVICE;
		copyParam.dstDevice = nv_frame->in_cuda_surf;
		copyParam.dstPitch = nv_frame->in_cuda_stride;
		copyParam.srcMemoryType = CU_MEMORYTYPE_HOST;
		copyParam.srcHost = in_yuv_buf;
		copyParam.srcPitch = ctx->width;
		copyParam.WidthInBytes = ctx->width;
		copyParam.Height = ctx->height;// *3 / 2;
		__cu(cuMemcpy2D(&copyParam));
#if 1
		// copy chroma
		int y_len = ctx->width * ctx->height;
		__cu(cuMemcpyHtoD(ctx->uv_tmp_ptr[0], in_yuv_buf+ y_len, y_len/4));
		__cu(cuMemcpyHtoD(ctx->uv_tmp_ptr[1], in_yuv_buf+ y_len*5/4, y_len/4));



#define BLOCK_X 32
#define BLOCK_Y 16
		int chromaHeight = ctx->height / 2;
		int chromaWidth = ctx->width / 2;
		dim3 block(BLOCK_X, BLOCK_Y, 1);
		dim3 grid((chromaWidth + BLOCK_X - 1) / BLOCK_X, (chromaHeight + BLOCK_Y - 1) / BLOCK_Y, 1);
#undef BLOCK_Y
#undef BLOCK_X

		CUdeviceptr dNV12Chroma = (CUdeviceptr)((unsigned char*)nv_frame->in_cuda_surf + nv_frame->in_cuda_stride * ctx->height);
		void *args[8] = { &ctx->uv_tmp_ptr[0], &ctx->uv_tmp_ptr[1], &dNV12Chroma, &chromaWidth, &chromaHeight, &chromaWidth, &chromaWidth, &nv_frame->in_cuda_stride };

		__cu(cuLaunchKernel(ctx->cuInterleaveUVFunction, grid.x, grid.y, grid.z,
			block.x, block.y, block.z,
			0,
			NULL, args, NULL));
		CUresult cuResult = cuStreamQuery(NULL);
		if (!((cuResult == CUDA_SUCCESS) || (cuResult == CUDA_ERROR_NOT_READY)))
		{
			return NV_ENC_ERR_GENERIC;
		}
#endif
	}
	else if (NV_ENC_BUFFER_FORMAT_ARGB == ctx->format
		|| NV_ENC_BUFFER_FORMAT_ABGR == ctx->format) {
#if 0	// RGB
		copyParam.dstMemoryType = CU_MEMORYTYPE_DEVICE;
		copyParam.dstDevice = nv_frame->in_cuda_surf;
		copyParam.dstPitch = nv_frame->in_cuda_stride;
		copyParam.srcMemoryType = CU_MEMORYTYPE_HOST;// CU_MEMORYTYPE_HOST;// CU_MEMORYTYPE_ARRAY;// CU_MEMORYTYPE_HOST;
		copyParam.srcHost = in_yuv_buf;
		copyParam.srcPitch = ctx->width * 4;	// RGB
		copyParam.WidthInBytes = ctx->width * 4;
		copyParam.Height = ctx->height;
		__cu(cuMemcpy2D(&copyParam));
#endif
		__cu(cuMemcpyHtoD(nv_frame->in_cuda_surf, in_yuv_buf, ctx->width * ctx->height * 4));
		//__cu(cuMemcpy(nv_frame->in_cuda_surf, (CUdeviceptr)in_yuv_buf, ctx->width * ctx->height * 4));
	}


	
	return 0;
}

int nvenc_upload_frame(const unsigned char *in_yuv_buf, const int yuv_len, nvenc_surface *nv_frame, nvenc_ctx *ctx)
{
	int ret = 0;
    NVENCSTATUS nv_status;
    NV_ENCODE_API_FUNCTION_LIST *p_nvenc = ctx->encode_apis;

	if(ctx->is_external_alloc) {
		// TODO: copy yuv data to sufrace
		nvenc_convert_yuv_data_to_nv12(in_yuv_buf, yuv_len, nv_frame, ctx);


		
		int reg_idx = nvenc_get_register_frame_index(nv_frame, ctx);//nvenc_register_frame(nv_frame->yuv_buf, ctx);
		if(-1 == reg_idx) {
			// error
			return -1;
		}

		// map
		memset(&nv_frame->in_map, 0x0, sizeof(NV_ENC_MAP_INPUT_RESOURCE));
        nv_frame->in_map.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
        nv_frame->in_map.registeredResource = ctx->arr_reg_frames[reg_idx].reg_ptr;
        nv_status = p_nvenc->nvEncMapInputResource(ctx->nv_encoder, &nv_frame->in_map);
        if (nv_status != NV_ENC_SUCCESS) {
            //Error mapping an input resource
            return -2;
        }

        ctx->arr_reg_frames[reg_idx].mapped 	= 1;
        nv_frame->reg_idx                   = reg_idx;
        nv_frame->input_surface             = nv_frame->in_map.mappedResource;
//		nv_frame->pitch = nv_frame->in_cuda_stride;

	}
	else {
		NV_ENC_LOCK_INPUT_BUFFER lockBufferParams = { 0 };
		SET_VER(lockBufferParams, NV_ENC_LOCK_INPUT_BUFFER);

		lockBufferParams.inputBuffer = nv_frame->input_surface;

		nv_status = p_nvenc->nvEncLockInputBuffer(ctx->nv_encoder, &lockBufferParams);
		if (nv_status != NV_ENC_SUCCESS) {
			//Failed locking nvenc input buffer
			return -1;
		}

		//nv_frame->pitch = lockBufferParams.pitch;
		memcpy(lockBufferParams.bufferDataPtr, in_yuv_buf, yuv_len);
		//res = nvenc_copy_frame(avctx, nvenc_frame, &lockBufferParams, frame);

		nv_status = p_nvenc->nvEncUnlockInputBuffer(ctx->nv_encoder, nv_frame->input_surface);
		if (nv_status != NV_ENC_SUCCESS) {
			//Failed unlocking input buffer!
			return -2;
		}

	}


	return 0;
}


int nvenc_get_spspps(nvenc_ctx *ctx)
{
	int ret = 0;
	NV_ENCODE_API_FUNCTION_LIST *pf_nvenc = ctx->encode_apis;
    NVENCSTATUS nv_status;
    uint32_t outSize = 0;
    char tmpHeader[256];
    NV_ENC_SEQUENCE_PARAM_PAYLOAD payload = { 0 };

	SET_VER(payload, NV_ENC_SEQUENCE_PARAM_PAYLOAD);

	payload.spsppsBuffer = tmpHeader;
	payload.inBufferSize = sizeof(tmpHeader);
	payload.outSPSPPSPayloadSize = &outSize;

	nv_status = pf_nvenc->nvEncGetSequenceParams(ctx->nv_encoder, &payload);
    if (nv_status != NV_ENC_SUCCESS) {
		// GetSequenceParams failed
        return -1;
    }
	
	ctx->spspps_buf = new unsigned char [outSize];

	memset(ctx->spspps_buf, 0x0, outSize);
	memcpy(ctx->spspps_buf, tmpHeader, outSize);

	// parse sps and pps
	int sps_start = 0, pps_start = 0;
	for (int i = 0; i < outSize; i++) {
		if (tmpHeader[i] == 0x00 &&
			tmpHeader[i + 1] == 0x00 &&
			tmpHeader[i + 2] == 0x00 &&
			tmpHeader[i + 3] == 0x01) {
			if (tmpHeader[i + 4] == 0x67) {
				// sps
				sps_start = i;
			}
			else if (tmpHeader[i + 4] == 0x68) {
				// pps
				pps_start = i;
			}
		}
	}
	ctx->sps_len = pps_start - sps_start;
	ctx->pps_len = outSize - pps_start;


	return 0;
}


int nvenc_push_surface_to_list(nvenc_surface *in_surface, nvenc_ctx *ctx)
{
	// waiting for mutex

	ctx->list_output->push_back(in_surface);

	// release mutex
	return 0;
}

int nvenc_check_ready_bitstream(nvenc_ctx *ctx)
{
	nvenc_surface *item = NULL;
	// move output list surface to ready list

	// waiting for mutex
	int ret = WaitForSingleObject(ctx->mutex_ouput_ready, INFINITE);
	int count = ctx->list_output->size();
	for (int i = 0; i < count; i++) {
		item = ctx->list_output->front();
		ctx->list_output->pop_front();

		// push back to ready list
		ctx->list_output_ready->push_back(item);
	}

	// release mutex
	ReleaseMutex(ctx->mutex_ouput_ready);
	return 0;
}

int nvenc_is_output_ready(nvenc_ctx *ctx)
{
	return  !ctx->list_output_ready->empty();
}

nvenc_surface *nvenc_pop_list_output_ready_item(nvenc_ctx *ctx)
{
	nvenc_surface *out_surf = NULL;

	// waiting for mutex
	int ret = WaitForSingleObject(ctx->mutex_ouput_ready, INFINITE);
	if (ctx->list_output_ready->empty()) {
		ReleaseMutex(ctx->mutex_ouput_ready);
		return NULL;
	}

	out_surf = ctx->list_output_ready->front();
	ctx->list_output_ready->pop_front();

	// release mutex
	ReleaseMutex(ctx->mutex_ouput_ready);
	return out_surf;
}

// show debug information
int nvenc_debug_show_support_input_format(nvenc_ctx *ctx)
{
	NVENCSTATUS nv_status;
	unsigned int num_infmt = 0;
	NV_ENC_BUFFER_FORMAT *infmt = NULL;

	NV_ENCODE_API_FUNCTION_LIST *pf_nvenc = ctx->encode_apis;

	nv_status = pf_nvenc->nvEncGetInputFormatCount(ctx->nv_encoder, ctx->codec_guid, &num_infmt);
	if (nv_status != NV_ENC_SUCCESS)
	{
		return (-1);
	}

	infmt = new NV_ENC_BUFFER_FORMAT[num_infmt];

	nv_status = pf_nvenc->nvEncGetInputFormats(ctx->nv_encoder, ctx->codec_guid, infmt, num_infmt, &num_infmt);
#if 0
	printf("====== support input format =======\n");
	for (int i = 0; i < num_infmt; i++) {
		printf("NO.%d : 0x%X", i, infmt[i]);
	}
#endif
	return 0;
}

int nvenc_cuda_memory_alloc_host(void **buf, int buf_len, nvenc_ctx *ctx)
{
	CCudaAutoLock culock(ctx->cuda_ctx);
	//__cu(cuMemAllocHost(buf, buf_len));
	__cu(cuMemHostAlloc(buf, buf_len, CU_MEMHOSTALLOC_WRITECOMBINED));
	//__cu(cuMemAlloc(buf, buf_len));

	//CUdeviceptr dptr;
	//__cu(cuMemAlloc(&dptr, buf_len));

	//*buf = (void*)dptr;

	return 0;
}

int nvenc_cuda_memory_release_host(void *buf, nvenc_ctx *ctx)
{
	CCudaAutoLock culock(ctx->cuda_ctx);
	__cu(cuMemFreeHost(buf));

	return 0;
}


/**********************************************************************************************
			NV Encode export
 *********************************************************************************************/

JMDLL_FUNC handle_nvenc jm_nvenc_create_handle()
{
	return nvenc_ctx_create();
}

JMDLL_FUNC int jm_nvenc_init(nv_enc_param *in_param, handle_nvenc handle)
{
	return nvenc_encode_init(in_param, (nvenc_ctx *)handle);
}

JMDLL_FUNC int jm_nvenc_deinit(handle_nvenc handle)
{
	return nvenc_encode_deinit((nvenc_ctx *)handle);
}

JMDLL_FUNC int jm_nvenc_enc_frame(const unsigned char *in_yuv_buf, const int yuv_len, int *got_packet, handle_nvenc handle)
{
	return nvenc_encode_enc_frame(in_yuv_buf, yuv_len, got_packet, (nvenc_ctx*)handle);
}

JMDLL_FUNC int jm_nvenc_get_bitstream(unsigned char *out_buf, int *out_data_len, int *is_keyframe, handle_nvenc handle)
{
	return nvenc_get_output_bitstream(out_buf, out_data_len, is_keyframe, (nvenc_ctx *)handle);
}

JMDLL_FUNC int jm_nvenc_get_spspps_len(int *sps_len, int *pps_len, handle_nvenc handle)
{
	nvenc_ctx *ctx = (nvenc_ctx *)handle;

	*sps_len = ctx->sps_len;
	*pps_len = ctx->pps_len;
	return 0;
}

JMDLL_FUNC int jm_nvenc_get_spspps(unsigned char *out_buf, handle_nvenc handle)
{
	nvenc_ctx *ctx = (nvenc_ctx *)handle;

	memcpy(out_buf, ctx->spspps_buf, ctx->sps_len + ctx->pps_len);

	return 0;
}

JMDLL_FUNC int jm_nvenc_memory_alloc_host(void **buf, int buf_len, handle_nvenc handle)
{
	return nvenc_cuda_memory_alloc_host(buf, buf_len, (nvenc_ctx *)handle);
}

JMDLL_FUNC int jm_nvenc_memory_release_host(void *buf, handle_nvenc handle)
{
	return nvenc_cuda_memory_release_host(buf, (nvenc_ctx *)handle);
}
