/*****************************************************************************
 *  Copyright (C) 2014 - 2017, Justin Mo.
 *  All rights reserverd.
 *
 *  FileName:  	intel_dec.cpp
 *  Author:    	Justin Mo(mojing1999@gmail.com)
 *  Date:       2017-05-08
 *  Version:    V0.01
 *  Desc:       This file implement This file implement Intel Media SDK Decode
 *****************************************************************************/
#include "intel_enc.h"

#include <time.h>


#pragma comment(lib,"libmfx.lib")
#pragma comment(lib,"legacy_stdio_definitions.lib")


/**
 *	@desc: encode thread
 */
DWORD WINAPI enc_thread_proc(LPVOID param)
{
	intel_enc_ctx *ctx = (intel_enc_ctx *)param;
	mfxStatus sts = MFX_ERR_NONE;

	while (sts == MFX_ERR_NONE)
	{
		sts = enc_encode_frame(ctx);
	}

	// 
	ctx->elapsed_time = clock() - ctx->elapsed_time;
	intel_enc_show_info(ctx);

	int loop = 0;
#define MAX_LOOP_COUNT (100)
	while (!ctx->out_bs_queue->empty() && ++loop < MAX_LOOP_COUNT) {
		LOG("Waiting for bitstream output!\n");
		MSDK_SLEEP(1);
	}

	ctx->thread_exit = true;

	return 0;
}

/**
 *   @desc:  create intel encode context
 *
 *   @return: context pointer - successful, NULL failed
 */
intel_enc_ctx *intel_enc_create()
{
	intel_enc_ctx *ctx = new intel_enc_ctx;

	memset(ctx, 0x0, sizeof(intel_enc_ctx));


	// create object
	ctx->in_param = new intel_enc_param;

	ctx->enc_param = new mfxVideoParam;




	// default param
	intel_enc_default_param(ctx);

	return ctx;
}

/**
 *   @desc:  intel_enc_init before use
 *   @param: in_param:  user define encode param
 *   @param: ctx: encode context return by intel_enc_create()
 *
 *   @return: 0 - successful, else failed
 */
int intel_enc_init(intel_enc_param *in_param, intel_enc_ctx *ctx)
{
	// parse input param
	enc_set_param(in_param, ctx);

	// init session
	enc_session_init(ctx);

	// plugin
	enc_plugin_load(ctx);

	// init encode param
	enc_param_init(ctx);

	// init surfaces
	enc_surfaces_init(ctx);

	// init output bs
	enc_output_bs_init(ctx);


	// encodec init
	MFXVideoENCODE_Init(ctx->session, ctx->enc_param);

	// create thread
	enc_create_thread(ctx);

	ctx->elapsed_time = clock();

	return 0;
}

/**
*	@desc:	intel_enc_deinit
*  @param: ctx: encode context return by intel_enc_create()
*/
int intel_enc_deinit(intel_enc_ctx *ctx)
{
	// show info
	LOG("frames count = %d\n", ctx->num_frames);

	// exit encode thread
	intel_enc_stop_yuv_input(ctx);

	enc_thread_exit(ctx);

	enc_plugin_unload(ctx);



	// deinit surfaces
	enc_surfaces_deinit(ctx);

	// deinit output bs
	enc_output_bs_deinit(ctx);

	// release buffer
	if (ctx->in_param) {
		delete ctx->in_param;
	}

	if (ctx->enc_param) {
		if (ctx->enc_param->ExtParam) {
			for (int i = 0; i < ctx->enc_param->NumExtParam; i++) {
				delete ctx->enc_param->ExtParam[i];
			}

			delete[] ctx->enc_param->ExtParam;
		}
		delete ctx->enc_param;
	}


	if (ctx->sps_pps_buffer) {
		delete[] ctx->sps_pps_buffer;
		ctx->sps_pps_buffer = NULL;
	}
	
	// close enc
	MFXVideoENCODE_Close(ctx->session);

	return 0;
}

void intel_enc_show_info(intel_enc_ctx *ctx)
{
	sprintf_s(ctx->enc_info, MAX_LEN_ENC_INFO,
		"==========================================\n"
		"Codec:\t\t%s\n"
		"Source:\t%d x %d\n"
		"Pixel Format:\t%s\n"
		"Frame Count:\t%d\n"
		"Elapsed Time:\t%d ms\n"
		"Encode FPS:\t%f fps\n"
		"==========================================\n",
		enc_get_codec_id_string(ctx),
		ctx->enc_param->mfx.FrameInfo.Width, ctx->enc_param->mfx.FrameInfo.Height,
		"NV12",
		ctx->num_frames,
		ctx->elapsed_time,
		(double)ctx->num_frames * CLOCKS_PER_SEC / (double)ctx->elapsed_time);

}

/**
*	@desc:	return default encode param to user setting.
*  @param: ctx: encode context return by intel_enc_create()
*  @return: intel_enc_param pointer, user custom the param,
*			then use with input param in API intel_enc_init()
*/
intel_enc_param *intel_enc_get_param(intel_enc_ctx *ctx)
{
	return ctx->in_param;
}

int enc_set_param(intel_enc_param *in_param, intel_enc_ctx *ctx)
{
	// parse input param;
	if (in_param->codec_id >= 0 && in_param->codec_id < ENC_SUPPORT_CODEC) {
		ctx->in_param->codec_id = in_param->codec_id;	// MFX_CODEC_AVC - MFX_CODEC_MPEG2
	}
	else {
		ctx->in_param->codec_id = 0;
	}
	

	if (in_param->target_usage >= 1 && in_param->target_usage <= 7) {
		ctx->in_param->target_usage = in_param->target_usage;
	}
	else {
		ctx->in_param->target_usage = 4;	// default MFX_TARGETUSAGE_BALANCED
	}

	ctx->in_param->src_width = in_param->src_width;
	ctx->in_param->src_height = in_param->src_height;

	ctx->in_param->framerate_D = in_param->framerate_D;
	ctx->in_param->framerate_N = in_param->framerate_N;

	ctx->in_param->bitrate_kb = in_param->bitrate_kb;

	ctx->in_param->is_hw = in_param->is_hw;

	LOG("YUV info: %d x %d, fps = %f\n", in_param->src_width, in_param->src_height, (float)in_param->framerate_N/in_param->framerate_D);


	return 0;
}

int intel_enc_default_param(intel_enc_ctx *ctx)
{
	memset(ctx->in_param, 0x0, sizeof(intel_enc_param));

	ctx->in_param->codec_id = 0;	// H.264
	ctx->in_param->target_usage = MFX_TARGETUSAGE_BALANCED;
	ctx->in_param->src_width = INTEL_ENC_DEFAULT_WIDTH;
	ctx->in_param->src_height = INTEL_ENC_DEFAULT_HEIGHT;

	ctx->in_param->framerate_N = 30;
	ctx->in_param->framerate_D = 1;

	ctx->in_param->bitrate_kb = INTEL_ENC_DEFAULT_BITRATE;

	ctx->in_param->is_hw = 1;


	return 0;
}

int intel_enc_input_yuv_frame(uint8_t *yuv, int len, intel_enc_ctx *ctx)
{
	// get free surface
	int index = enc_get_free_surface_index(ctx);
	if (MFX_ERR_NOT_FOUND == index) {
		// Error
		LOG("Error: ----------no free surface\n");
		return -1;
	}

	// copy yuv data to surface
	mfxFrameSurface1 *surf = ctx->surfaces[index];
	mfxFrameInfo *info = &surf->Info;
	mfxFrameData *data = &surf->Data;
	uint16_t w = 0, h = 0, pitch = 0;
	int y_len = 0, uv_len = 0;
	int crop_x = 0, crop_y = 0;
	int i = 0;


	if (info->CropW > 0 && info->CropH > 0) {
		w = info->CropW;
		h = info->CropH;
	}
	else {
		w = info->Width;
		h = info->Height;
	}

	y_len = w * h;
	uv_len = y_len / 2;

	crop_x = info->CropX;
	crop_y = info->CropY;
	pitch = data->Pitch;

	mfxU8 *dst = data->Y;
	uint8_t *src = yuv;

	// Y
	for (i = 0; i < h; i++) {
		dst = data->Y + (crop_y * pitch + crop_x) + i * pitch;
		src = yuv + i * w;
		memcpy(dst, src, w);
	}

	// UV
	crop_x /= 2;
	crop_y /= 2;
	h /= 2;
	src = yuv + y_len;

	for (i = 0; i < h; i++) {
		dst = data->UV + (crop_y * pitch + crop_x) + i * pitch;
		src = yuv + y_len + i * w;
		memcpy(dst, src, w);
	}


	// put surface to queue
	enc_push_yuv_surface(surf, ctx);

	return 0;
}

int intel_enc_input_yuv_yuv420(uint8_t *yuv, int len, intel_enc_ctx *ctx)
{
	// get free surface
	int index = enc_get_free_surface_index(ctx);
	if (MFX_ERR_NOT_FOUND == index) {
		// Error
		LOG("Error: ----------no free surface\n");
		return -1;
	}

	// copy yuv data to surface
	mfxFrameSurface1 *surf = ctx->surfaces[index];
	mfxFrameInfo *info = &surf->Info;
	mfxFrameData *data = &surf->Data;
	uint16_t w = 0, h = 0, pitch = 0;
	int y_len = 0, uv_len = 0;
	int crop_x = 0, crop_y = 0;
	int i = 0;


	if (info->CropW > 0 && info->CropH > 0) {
		w = info->CropW;
		h = info->CropH;
	}
	else {
		w = info->Width;
		h = info->Height;
	}

	y_len = w * h;
	uv_len = y_len / 2;

	crop_x = info->CropX;
	crop_y = info->CropY;
	pitch = data->Pitch;

	mfxU8 *dst = data->Y;
	uint8_t *src = yuv;

	// Y
	for (i = 0; i < h; i++) {
		dst = data->Y + (crop_y * pitch + crop_x) + i * pitch;
		src = yuv + i * w;
		memcpy(dst, src, w);
	}

	// UV
	crop_x /= 2;
	crop_y /= 2;
	h /= 2;
	w /= 2;

	src = yuv + y_len;
	uint8_t *pu = src;
	uint8_t *pv = src + w * h;
	dst = data->UV + crop_x + crop_y * pitch;
	int index_uv = 0;
	int j = 0;

	for (i = 0; i < h; i++) {
		for (j = 0; j < 2 * w; j += 2) {
			dst[i*pitch + j] = pu[index_uv];
			dst[i*pitch + j + 1] = pv[index_uv++];
		}
	}


	// put surface to queue
	enc_push_yuv_surface(surf, ctx);

	return 0;
}

int intel_enc_output_bitstream(uint8_t *out_buf, int *out_len, int *is_keyframe, intel_enc_ctx *ctx)
{
	mfxBitstream *bs = enc_pop_bitstream(ctx);
	int len = 0;
	int buf_size = *out_len;
	*out_len = 0;

	if (bs) {
		len = bs->DataLength;
		if (buf_size < len) {
			// error
			LOG("Error: not enough buffer for output bitstream\n");
			len = buf_size;
		}

		memcpy(out_buf, bs->Data, len);
		*out_len = len;
		*is_keyframe = ((bs->FrameType & MFX_FRAMETYPE_I) || (bs->FrameType & MFX_FRAMETYPE_IDR));

		// Release bitstream
		enc_release_bitstream(bs);

		return 0;
	}

	// please wait a moment for encode output
	MSDK_SLEEP(5);

	return -1;
}

int intel_enc_stop_yuv_input(intel_enc_ctx *ctx)
{
	ctx->is_stop_input = true;

	return ctx->is_stop_input;
}

int intel_enc_is_exit(intel_enc_ctx *ctx)
{
	return ctx->thread_exit;
}

bool intel_enc_need_more_data(intel_enc_ctx *ctx)
{
	LOG("surface queue size = %d\n", ctx->in_surf_queue->size());
	if (ctx->in_surf_queue->size() < NUM_SURFACE_ADDITION) {
		return true;
	}

	return false;
}


mfxStatus enc_session_init(intel_enc_ctx *ctx)
{
	mfxStatus sts = MFX_ERR_NONE;
	mfxVersion ver = { 1, 1 };

	mfxIMPL impl = MFX_IMPL_HARDWARE_ANY;
	sts = MFXInit(impl, &ver, &ctx->session);
	if (MFX_ERR_NONE != sts) {
		impl = MFX_IMPL_HARDWARE;
		sts = MFXInit(impl, &ver, &ctx->session);
	}

	// try software 
	if (MFX_ERR_NONE != sts) {
		sts = MFXInit(MFX_IMPL_SOFTWARE, &ver, &ctx->session);
	}

	if (MFX_ERR_NONE != sts) {
		return sts;
	}

	// query support impl and version
	sts = MFXQueryIMPL(ctx->session, &ctx->impl);
	sts = MFXQueryVersion(ctx->session, &ver);

	// show impl and version
	LOG("IMPL: 0x%04X, Version: V%d.%d\n", ctx->impl, ver.Major, ver.Minor);


	// check current version support feature


	return sts;
}

uint32_t enc_get_mfx_codec_id(int in_codec, intel_enc_ctx *ctx)
{
	uint32_t id = MFX_CODEC_AVC;

	switch (in_codec) {
	case 0:
		return MFX_CODEC_AVC;
	case 1:
		return MFX_CODEC_HEVC;
	case 2:
		return MFX_CODEC_MPEG2;
	case 3:
		return MFX_CODEC_VC1;
	case 4:
		return MFX_CODEC_VP9;

	default:
		return MFX_CODEC_AVC;
	}

	return id;
}

mfxStatus enc_param_init(intel_enc_ctx *ctx)
{
	mfxStatus sts = MFX_ERR_NONE;

	memset(ctx->enc_param, 0x0, sizeof(mfxVideoParam));
	mfxVideoParam *enc = ctx->enc_param;
	intel_enc_param *in = ctx->in_param;

	enc->mfx.CodecId = enc_get_mfx_codec_id(in->codec_id, ctx);
	enc->mfx.TargetUsage = in->target_usage;
	enc->mfx.TargetKbps = in->bitrate_kb;
	enc->mfx.RateControlMethod = MFX_RATECONTROL_VBR;	// VBR or CBR
	enc->mfx.FrameInfo.FrameRateExtN = in->framerate_N;
	enc->mfx.FrameInfo.FrameRateExtD = in->framerate_D;
	enc->mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;	// Only support NV12 input
	enc->mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
	enc->mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;	// frame or field
	enc->mfx.FrameInfo.CropX = 0;
	enc->mfx.FrameInfo.CropY = 0;
	enc->mfx.FrameInfo.CropW = in->src_width;
	enc->mfx.FrameInfo.CropH = in->src_height;

	enc->mfx.FrameInfo.Width = MSDK_ALIGN16(in->src_width);
	enc->mfx.FrameInfo.Height = (MFX_PICSTRUCT_PROGRESSIVE == enc->mfx.FrameInfo.PicStruct) ?
		MSDK_ALIGN16(in->src_height) : MSDK_ALIGN32(in->src_height);


	enc->IOPattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY;


	//
	//
	enc->mfx.GopRefDist = 1;	// Distance between I- or P- key framesIf GopRefDist = 1, there are no B frames used. 
	enc->mfx.NumRefFrame = 0;

	enc->mfx.GopOptFlag = MFX_GOP_CLOSED;
	enc->mfx.IdrInterval = 0;
	enc->mfx.GopPicSize = 30;

	// ext param
	mfxExtCodingOption *option = new mfxExtCodingOption;
	memset(option, 0x0, sizeof(mfxExtCodingOption));
	option->Header.BufferId = MFX_EXTBUFF_CODING_OPTION;
	option->Header.BufferSz = sizeof(mfxExtCodingOption);
	option->ViewOutput = MFX_CODINGOPTION_ON;
	option->AUDelimiter = MFX_CODINGOPTION_OFF;
	option->RefPicMarkRep = MFX_CODINGOPTION_ON;
	option->PicTimingSEI = MFX_CODINGOPTION_OFF;
	option->SingleSeiNalUnit = MFX_CODINGOPTION_OFF;
	option->ResetRefList = MFX_CODINGOPTION_ON;

	//
	mfxExtCodingOption2 *option2 = new mfxExtCodingOption2;
	memset(option2, 0x0, sizeof(mfxExtCodingOption2));
	option2->Header.BufferId = MFX_EXTBUFF_CODING_OPTION2;
	option2->Header.BufferSz = sizeof(mfxExtCodingOption2);
	option2->RepeatPPS = MFX_CODINGOPTION_OFF;


	mfxExtBuffer** ExtBuffer = new mfxExtBuffer *[2];
	ExtBuffer[0] = (mfxExtBuffer*)option2;
	ExtBuffer[1] = (mfxExtBuffer*)option;
	enc->ExtParam = (mfxExtBuffer**)&ExtBuffer[0];// option;
	enc->NumExtParam = 2;







	//

	sts = MFXVideoENCODE_Query(ctx->session, enc, enc);
	MSDK_IGNORE_MFX_STS(sts, MFX_WRN_INCOMPATIBLE_VIDEO_PARAM);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);


	return sts;
}

mfxStatus enc_get_spspps(intel_enc_ctx *ctx)
{
	mfxStatus sts = MFX_ERR_NONE;
#define BUFFER_SIZE_SPSPPS		512
	mfxU8 sps[BUFFER_SIZE_SPSPPS] = { 0 };
	mfxU8 pps[BUFFER_SIZE_SPSPPS] = { 0 };

	mfxExtCodingOptionSPSPPS extSPSPPS;
	memset(&extSPSPPS, 0x0, sizeof(mfxExtCodingOptionSPSPPS));
	extSPSPPS.Header.BufferId = MFX_EXTBUFF_CODING_OPTION_SPSPPS;
	extSPSPPS.Header.BufferSz = sizeof(mfxExtCodingOptionSPSPPS);
	extSPSPPS.PPSBufSize = BUFFER_SIZE_SPSPPS;
	extSPSPPS.SPSBufSize = BUFFER_SIZE_SPSPPS;
	extSPSPPS.PPSBuffer = pps;
	extSPSPPS.SPSBuffer = sps;

	mfxExtBuffer* encExtParams[1];
	mfxVideoParam par = {};

	encExtParams[0] = (mfxExtBuffer *)&extSPSPPS;
	par.ExtParam = &encExtParams[0];
	par.NumExtParam = 1;

	sts = MFXVideoENCODE_GetVideoParam(ctx->session, &par);

	LOG("get video encode sps pps, sts = %d\n", sts);
	if (MFX_ERR_NONE == sts) {
		ctx->len_sps = extSPSPPS.SPSBufSize;
		ctx->len_pps = extSPSPPS.PPSBufSize;

		if (ctx->sps_pps_buffer == NULL) {
			ctx->sps_pps_buffer = new mfxU8[ctx->len_sps + ctx->len_pps];
			memset(ctx->sps_pps_buffer, 0x0, ctx->len_sps + ctx->len_pps);
			memcpy(ctx->sps_pps_buffer, extSPSPPS.SPSBuffer, extSPSPPS.SPSBufSize);
			memcpy(ctx->sps_pps_buffer + extSPSPPS.SPSBufSize, extSPSPPS.PPSBuffer, extSPSPPS.PPSBufSize);
			LOG("Get video encode SPS PPS data, len = %d\n", ctx->len_sps + ctx->len_pps);
		}
	}

	return sts;
}

mfxStatus enc_surfaces_init(intel_enc_ctx *ctx)
{
	mfxStatus sts = MFX_ERR_NONE;

	mfxFrameAllocRequest enc_request;
	memset(&enc_request, 0x0, sizeof(mfxFrameAllocRequest));
	sts = MFXVideoENCODE_QueryIOSurf(ctx->session, ctx->enc_param, &enc_request);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);

	ctx->num_surfaces = enc_request.NumFrameSuggested + NUM_SURFACE_ADDITION;

	// Allocate surfaces for encoder
	uint16_t width = (uint16_t)MSDK_ALIGN32(enc_request.Info.Width);
	uint16_t height = (uint16_t)MSDK_ALIGN32(enc_request.Info.Height);
	ctx->enc_width = width;
	ctx->enc_height = height;

	uint8_t bits = 12;	// NV12 format is a 12 bits per pixel format
	uint32_t surface_size = width * height * bits / 8;
	uint8_t *surf_buffers = (uint8_t*) new uint8_t[surface_size * ctx->num_surfaces];

	ctx->yuv_big_buf = surf_buffers;

	// Allocate surface headers (mfxFrameSurfaces1) for encoder
	ctx->surfaces = new mfxFrameSurface1 *[ctx->num_surfaces];
	// check memory alloce error
	if (!ctx->surfaces || !ctx->yuv_big_buf) {
		// error
		LOG("ERROR: enc_surfaces_init() can not alloc memory\n");
		return MFX_ERR_MEMORY_ALLOC;
	}


	for (int i = 0; i < ctx->num_surfaces; i++) {
		ctx->surfaces[i] = new mfxFrameSurface1;
		
		memset(ctx->surfaces[i], 0x0, sizeof(mfxFrameSurface1));

		memcpy(&(ctx->surfaces[i]->Info), &(ctx->enc_param->mfx.FrameInfo), sizeof(mfxFrameInfo));

		ctx->surfaces[i]->Data.Y = &surf_buffers[surface_size * i];
		ctx->surfaces[i]->Data.U = ctx->surfaces[i]->Data.Y + width * height;
		ctx->surfaces[i]->Data.V = ctx->surfaces[i]->Data.U + 1;
		ctx->surfaces[i]->Data.Pitch = width;

		// Create surface system memory
		memset(ctx->surfaces[i]->Data.Y, 100, width * height);
		memset(ctx->surfaces[i]->Data.U, 50, (width * height) / 2);

	}

	// init queue and mutex
	ctx->in_surf_queue = new std::queue<mfxFrameSurface1 *>;
	ctx->mutex_yuv = CreateMutexA(NULL, FALSE, MUTEX_NAME_INPUT_YUV);


	return sts;
}

void enc_surfaces_deinit(intel_enc_ctx *ctx)
{
	if (ctx->surfaces) {
		for (int i = 0; i < ctx->num_surfaces; i++) {
			if (ctx->surfaces[i]) {
				delete ctx->surfaces[i];
				ctx->surfaces[i] = 0;
			}
		}

		delete[] ctx->surfaces;
		ctx->surfaces = NULL;
	}

	if (ctx->yuv_big_buf) {
		delete[] ctx->yuv_big_buf;
		ctx->yuv_big_buf = NULL;
	}

	if (ctx->in_surf_queue) {
		delete ctx->in_surf_queue;
		ctx->in_surf_queue = NULL;
	}

	if (ctx->mutex_yuv) {
		CloseHandle(ctx->mutex_yuv);
		ctx->mutex_yuv = 0;
	}

	return;
}

int enc_get_free_surface_index(intel_enc_ctx *ctx)
{
	if (ctx->surfaces) {
		for (int i = 0; i < ctx->num_surfaces; i++) {
			if (0 == ctx->surfaces[i]->Data.Locked && 0 == ctx->surfaces[i]->reserved[INDEX_OF_RESERVED_IN_USE]) {
				enc_surface_enquue_mark(ctx->surfaces[i]);
				return i;
			}
		}
	}

	return MFX_ERR_NOT_FOUND;
}

void enc_surface_enquue_mark(mfxFrameSurface1 *surface)
{
	surface->reserved[INDEX_OF_RESERVED_IN_USE] = 1;
}

void enc_surface_dequeue_mark(mfxFrameSurface1 *surface)
{
	surface->reserved[INDEX_OF_RESERVED_IN_USE] = 0;
}


int enc_push_yuv_surface(mfxFrameSurface1 *surf, intel_enc_ctx *ctx)
{
	WaitForSingleObject(ctx->mutex_yuv, INFINITE);
	ctx->in_surf_queue->push(surf);
	ReleaseMutex(ctx->mutex_yuv);

	return 0;
}

mfxFrameSurface1 *enc_pop_yuv_surface(intel_enc_ctx *ctx)
{
	mfxFrameSurface1 *surf = NULL;

	if (ctx->in_surf_queue->empty()) {
		return NULL;
	}

	WaitForSingleObject(ctx->mutex_yuv, INFINITE);
	surf = ctx->in_surf_queue->front();
	ctx->in_surf_queue->pop();
	ReleaseMutex(ctx->mutex_yuv);

	return surf;
}



int enc_output_bs_init(intel_enc_ctx *ctx)
{
	ctx->num_bs = MAX_OUTPUT_BS_COUNT;
	ctx->arr_bs = new mfxBitstream *[ctx->num_bs];
	if (!ctx->arr_bs) {
		LOG("Error: enc_output_bs_init() alloc memory error\n");
		return -1;
	}

	memset(ctx->arr_bs, 0x0, sizeof(mfxBitstream *) * ctx->num_bs);

	for (int i = 0; i < ctx->num_bs; i++) {
		ctx->arr_bs[i] = new mfxBitstream;
		memset(ctx->arr_bs[i], 0x0, sizeof(mfxBitstream));

		enc_init_bitstream(DEFAULT_OUTPUT_BS_SIZE, ctx->arr_bs[i]);
	}

	ctx->out_bs_queue = new std::queue<mfxBitstream *>;

	// Create Mutex
	ctx->mutex_bs = CreateMutexA(NULL, FALSE, MUTEX_NAME_OUTPUT_BS);

	return 0;
}

int enc_output_bs_deinit(intel_enc_ctx *ctx)
{
	if (ctx->out_bs_queue) {
		delete ctx->out_bs_queue;
		ctx->out_bs_queue = NULL;
	}

	for (int i = 0; i < ctx->num_bs; i++) {
		if (ctx->arr_bs[i]) {
			if (ctx->arr_bs[i]->Data) {
				delete[] ctx->arr_bs[i]->Data;
			}

			delete ctx->arr_bs[i];
			ctx->arr_bs[i] = NULL;
		}
	}

	delete[] ctx->arr_bs;
	ctx->arr_bs = NULL;

	if (ctx->mutex_bs) {
		CloseHandle(ctx->mutex_bs);
	}

	return 0;
}

int enc_get_free_bitstream_index(intel_enc_ctx *ctx)
{
	for (int i = 0; i < ctx->num_bs; i++) {
		if (0 == ctx->arr_bs[i]->reserved2) {
			ctx->arr_bs[i]->reserved2 = 1;	// use mfxBitstream.reserved2 for is_used
			return i;
		}
	}


	return MFX_ERR_NOT_FOUND;

}

int enc_release_bitstream(mfxBitstream *pbs)
{
	pbs->DataLength = 0;
	pbs->DataOffset = 0;
	pbs->reserved2 = 0;		// use mfxBitstream.reserved2 for is_used

	return 0;
}


int enc_push_bitstream(mfxBitstream *bs, intel_enc_ctx *ctx)
{
	WaitForSingleObject(ctx->mutex_bs, INFINITE);
	ctx->out_bs_queue->push(bs);
	ReleaseMutex(ctx->mutex_bs);


	return 0;
}
mfxBitstream *enc_pop_bitstream(intel_enc_ctx *ctx)
{
	mfxBitstream *bs = NULL;

	if (ctx->out_bs_queue->empty()) {
		return NULL;
	}

	WaitForSingleObject(ctx->mutex_bs, INFINITE);
	bs = ctx->out_bs_queue->front();
	ctx->out_bs_queue->pop();
	ReleaseMutex(ctx->mutex_bs);

	return bs;
}



mfxStatus enc_encode_frame(intel_enc_ctx *ctx)
{
	mfxStatus sts = MFX_ERR_NONE;

	mfxFrameSurface1 *surface = NULL;
	mfxBitstream *bs = NULL;
	mfxSyncPoint syncp;

	//bool output_enc_cached = false;
	int idx_bs = -1;


	while (MFX_ERR_NONE <= sts || MFX_ERR_MORE_DATA == sts) {
		if (ctx->is_stop_input && ctx->in_surf_queue->empty()) {
			LOG("Warning: No more YUV frame input!\n");
			//sts = MFX_ERR_MORE_DATA;
			break;
		}
		else {
			// get surfaces
			surface = enc_pop_yuv_surface(ctx);
			if (!surface && !ctx->is_stop_input) {
				// error, need more yuv data for encode
				LOG("Warning: Waiting for more YUV data for encode!\n");
				//sts = MFX_ERR_MORE_DATA;
				MSDK_SLEEP(5);
				continue;
				//break;
			}
		}

		// get free output bs
		idx_bs = enc_get_free_bitstream_index(ctx);
		if (MFX_ERR_NOT_FOUND == idx_bs) {
			// Wait for handle output bitstream
			MSDK_SLEEP(5);
			sts = MFX_ERR_NONE;
			LOG("Error: not free output bs\n");
			continue;
		}

		bs = ctx->arr_bs[idx_bs];

		for (;;) {

			sts = MFXVideoENCODE_EncodeFrameAsync(ctx->session, NULL, surface, bs, &syncp);

			if (MFX_ERR_NONE < sts && !syncp) {
				if (MFX_WRN_DEVICE_BUSY == sts)
					MSDK_SLEEP(1);
			}
			else if(MFX_ERR_NONE < sts && syncp) {
				sts = MFX_ERR_NONE;
				break;
			}
			else if (MFX_ERR_NOT_ENOUGH_BUFFER == sts) {
				// Allocate more bitstream buffer memory here if needed...
				break;
			}
			else {
				break;
			}
		}

		// Release YUV surface in queue
		LOG("enc_encode_frame() sts = %d\n", sts);
		if (surface) {
			//surface->reserved[SURFACE_IN_USE] = 0;
			enc_surface_dequeue_mark(surface);
		}

		if (MFX_ERR_NONE == sts) {
			// output bs
			sts = MFXVideoCORE_SyncOperation(ctx->session, syncp, MAX_TIME_ENC_EYNCP);

			if (MFX_ERR_NONE == sts) {
				enc_push_bitstream(bs, ctx);
			}
			// Frame count ++
			if (0 == ctx->num_frames) {
				ctx->elapsed_time = clock();
			}

			ctx->num_frames++;
		}
		else {
			enc_release_bitstream(bs);

		}

		//MSDK_SLEEP(1);

	}

	MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);


	// output encode cached frame
	while (MFX_ERR_NONE <= sts) {

		// bs
		idx_bs = enc_get_free_bitstream_index(ctx);
		if (MFX_ERR_NOT_FOUND == idx_bs) {
			// Wait for handle output bitstream
			MSDK_SLEEP(5);
			sts = MFX_ERR_NONE;
			LOG("Error: not free output bs\n");
			continue;
		}

		bs = ctx->arr_bs[idx_bs];

		for (;;) {
			sts = MFXVideoENCODE_EncodeFrameAsync(ctx->session, NULL, NULL, bs, &syncp);
			if (MFX_ERR_NONE < sts && !syncp) {     // Repeat the call if warning and no output
				if (MFX_WRN_DEVICE_BUSY == sts)
					MSDK_SLEEP(1);  // Wait if device is busy, then repeat the same call
			}
			else if (MFX_ERR_NONE < sts && syncp) {
				sts = MFX_ERR_NONE;     // Ignore warnings if output is available
				break;
			}
			else
				break;
		}

		if (MFX_ERR_NONE == sts) {
			sts = MFXVideoCORE_SyncOperation(ctx->session, syncp, MAX_TIME_ENC_EYNCP);
			if (MFX_ERR_NONE == sts) {
				enc_push_bitstream(bs, ctx);
			}
			// Frame count ++
			ctx->num_frames++;
		}
		else {
			enc_release_bitstream(bs);
		}

	}

	return sts;
}

/*
*	bitstream function
*/
int enc_init_bitstream(int buf_size, mfxBitstream *pbs)
{
	memset(pbs, 0x0, sizeof(mfxBitstream));
	pbs->MaxLength = buf_size;
	pbs->Data = new mfxU8[pbs->MaxLength];

	if (!pbs->Data) {
		// Error
		LOG("ERROR: enc_init_bitstream() alloc memory error\n");
		return -1;
	}

	return 0;
}

int enc_extend_bitstream(int new_size, mfxBitstream *pbs)
{

	mfxU8 *pData = new mfxU8[new_size];

	LOG("intel encode extend bitstream data[newsize = %d], buf=%p\n", new_size, pbs->Data);

	if (!pbs->Data) {
		pbs->Data = pData;
		pbs->DataOffset = 0;
		pbs->DataLength = 0;
		pbs->MaxLength = new_size;

		return new_size;
	}

	// memcpy
	memcpy(pData, pbs->Data + pbs->DataOffset, pbs->DataLength);

	delete[] pbs->Data;

	pbs->Data = pData;
	pbs->DataOffset = 0;
	pbs->MaxLength = new_size;



	return new_size;
}

int enc_create_thread(intel_enc_ctx *ctx) 
{
	DWORD thread_id = 0;

	ctx->enc_thread = CreateThread(NULL,
		0,
		enc_thread_proc,
		ctx,
		0,
		&thread_id);

	if (!ctx->enc_thread) {
		// error
		return -1;
	}

	return thread_id;
}

int enc_thread_exit(intel_enc_ctx *ctx)
{
	ctx->thread_exit = true;
	if (ctx->enc_thread) {
		WaitForSingleObject(ctx->enc_thread, INFINITE);
		CloseHandle(ctx->enc_thread);
		ctx->enc_thread = 0;
	}

	return 0;
}

char *enc_get_codec_id_string(intel_enc_ctx *ctx)
{
	switch (ctx->enc_param->mfx.CodecId)
	{
	case MFX_CODEC_AVC:
		return "H.264";

	case MFX_CODEC_HEVC:
		return "H.265";

	case MFX_CODEC_MPEG2:
		return "MPEG2";

	case MFX_CODEC_VC1:
		return "VC1";

	case MFX_CODEC_CAPTURE:
		return "CAPTURE";

	case MFX_CODEC_VP9:
		return "VP9";

	default:
		return "UNKNOW";
	}

	return "UNKNOW";
}

int enc_plugin_load(intel_enc_ctx *ctx)
{
	mfxStatus sts = MFX_ERR_NONE;
	const mfxPluginUID *uid = NULL;
	uint32_t codec_id = enc_get_mfx_codec_id(ctx->in_param->codec_id, ctx);

	if (MFX_CODEC_HEVC == codec_id) {
		uid = &MFX_PLUGINID_HEVCE_HW;
	}
	else if (MFX_CODEC_VP9 == codec_id) {
		uid = &MFX_PLUGINID_VP9E_HW;
	}

	if (uid) {
		sts = MFXVideoUSER_Load(ctx->session, uid, 1);
		return (MFX_ERR_NONE == sts ? 0 : -1);
	}

	return -1;

}

int enc_plugin_unload(intel_enc_ctx *ctx)
{
	mfxStatus sts = MFX_ERR_NONE;
	const mfxPluginUID *uid = NULL;
	uint32_t codec_id = enc_get_mfx_codec_id(ctx->in_param->codec_id, ctx);

	if (MFX_CODEC_HEVC == codec_id) {
		uid = &MFX_PLUGINID_HEVCE_HW;
	}
	else if (MFX_CODEC_VP9 == codec_id) {
		uid = &MFX_PLUGINID_VP9E_HW;
	}

	if (uid) {
		sts = MFXVideoUSER_UnLoad(ctx->session, uid);
		return (MFX_ERR_NONE == sts ? 0 : -1);
	}

	return -1;
}

/*******************************************************************************
 *		Export function
 ******************************************************************************/

JMDLL_FUNC handle_intelenc jm_intel_enc_create_handle()
{
	return intel_enc_create();
}

JMDLL_FUNC intel_enc_param *jm_intel_enc_default_param(handle_intelenc handle)
{
	return intel_enc_get_param((intel_enc_ctx *)handle);
}

JMDLL_FUNC int jm_intel_enc_init(intel_enc_param *in_param, handle_intelenc handle)
{
	return intel_enc_init(in_param, (intel_enc_ctx *)handle);
}

JMDLL_FUNC int jm_intel_enc_deinit(handle_intelenc handle)
{
	return intel_enc_deinit((intel_enc_ctx *)handle);
}

JMDLL_FUNC int jm_intel_enc_encode_yuv_frame(unsigned char *yuv, int len, handle_intelenc handle)
{
	return intel_enc_input_yuv_frame(yuv, len, (intel_enc_ctx *)handle);
}

JMDLL_FUNC int jm_intel_enc_encode_yuv_yuv420(unsigned char *yuv, int len, handle_intelenc handle)
{
	return intel_enc_input_yuv_yuv420(yuv, len, (intel_enc_ctx *)handle);
}

JMDLL_FUNC int jm_intel_enc_output_bitstream(unsigned char *out_buf, int *out_len, int *is_keyframe, handle_intelenc handle)
{
	return intel_enc_output_bitstream(out_buf, out_len, is_keyframe, (intel_enc_ctx *)handle);
}

JMDLL_FUNC int jm_intel_enc_set_eof(handle_intelenc handle)
{
	return intel_enc_stop_yuv_input((intel_enc_ctx *)handle);
}

JMDLL_FUNC bool jm_intel_enc_is_exit(handle_intelenc handle)
{
	return intel_enc_is_exit((intel_enc_ctx *)handle);
}

JMDLL_FUNC bool jm_intel_enc_more_data(handle_intelenc handle)
{
	return intel_enc_need_more_data((intel_enc_ctx *)handle);
}

JMDLL_FUNC char *jm_intel_enc_info(handle_intelenc handle)
{
	return ((intel_enc_ctx *)handle)->enc_info;
}

JMDLL_FUNC  char * jm_intel_enc_get_spspps(int *sps_len, int *pps_len, handle_intelenc handle)
{
	intel_enc_ctx * ctx = (intel_enc_ctx *)handle;
	if (!ctx->sps_pps_buffer) {
		enc_get_spspps(ctx);
	}

	*sps_len = ctx->len_sps;
	*pps_len = ctx->len_pps;

	return (char *)ctx->sps_pps_buffer;
}
