/*****************************************************************************
 *  Copyright (C) 2014 - 2017, Justin Mo.
 *  All rights reserverd.
 *
 *  FileName:  nv_dec.cpp
 *  Author:     Justin Mo(mojing1999@gmail.com)
 *  Date:        2017-05-08
 *  Version:    V0.01
 *  Desc:       This file implement NVIDIA VIDEO DECODER(NVDEC) INTERFACE
 *****************************************************************************/
#include "nv_dec.h"
#include "jm_nv_dec.h"

#if defined(_WIN32)
#define CUDAAPI __stdcall
#else
#define CUDAAPI
#endif

// Callback
static int CUDAAPI cuvid_handle_video_sequence(void *opaque, CUVIDEOFORMAT* format)
{
	nvdec_ctx *ctx = (nvdec_ctx *)opaque;

	int ret = nvdec_create_decoder(format, ctx);

	return 1;
}

// Callback
static int CUDAAPI cuvid_handle_picture_decode(void *opaque, CUVIDPICPARAMS* picparams)
{
	nvdec_ctx *ctx = (nvdec_ctx *)opaque;

	CUresult ret = cuvidDecodePicture(ctx->cudecoder, picparams);

	return (CUDA_SUCCESS == ret ? 1 : 0);

}

// Callback
static int CUDAAPI cuvid_handle_picture_display(void *opaque, CUVIDPARSERDISPINFO* dispinfo)
{
	nvdec_ctx *ctx = (nvdec_ctx *)opaque;

	int ret = nvdec_frame_queue_push(dispinfo, ctx);

	return 1;
}

nvdec_ctx *nvdec_ctx_create()
{
	nvdec_ctx *ctx = new nvdec_ctx;
	memset(ctx, 0x0, sizeof(nvdec_ctx));

	return ctx;
}

int nvdec_decode_init(int codec_type, char *extra_data, int len, nvdec_ctx *ctx)
{
	int ret = 0;
	// 
	ctx->is_first_frame = 1;

	// init cuda
	ret = nvdec_cuda_init(ctx);

	// init frame queue
	ret = nvdec_frame_queue_init(ctx);

	// init cuvid video parser info
	ret = nvdec_create_parser(codec_type, extra_data, len, ctx);

	return 0;
}

int nvdec_decode_deinit(nvdec_ctx *ctx)
{
	//
	nvdec_frame_queue_deinit(ctx);

	//
	nvdec_out_frame_deinit(ctx);


	if (ctx->cuparser) {
		cuvidDestroyVideoParser(ctx->cuparser);
	}

	if (ctx->cudecoder) {
		cuvidDestroyDecoder(ctx->cudecoder);
	}

	// destroy cuda
	cuCtxDestroy(ctx->cuda_ctx);


	delete ctx;
	
	return 0;
}


int nvdec_frame_queue_init(nvdec_ctx *ctx)
{

#define MUTEX_NAME_FOR_QUEUE "mutex_for_queue"
	ctx->queue_max_size = NVDEC_MAX_FRAMES;
	ctx->mutex_for_queue = CreateMutexA(NULL, TRUE, MUTEX_NAME_FOR_QUEUE);//MUTEX_NAME_DEC_BS
	ctx->is_frame_in_use = new int[ctx->queue_max_size];
	memset(ctx->is_frame_in_use, 0x0, ctx->queue_max_size * sizeof(int));

	ctx->frame_queue = new std::queue<CUVIDPARSERDISPINFO *>;


	return 0;
}

int nvdec_frame_queue_deinit(nvdec_ctx *ctx)
{
	// pop frame queue

	// delete frame queue
	if (ctx->frame_queue) {
		delete ctx->frame_queue;
	}

	// delete arr_frames
	if (ctx->is_frame_in_use) {
		delete[] ctx->is_frame_in_use;
	}

	if (ctx->mutex_for_queue) {
		CloseHandle(ctx->mutex_for_queue);
	}
	ctx->queue_max_size = 0;

	return 0;
}

int nvdec_frame_queue_push(CUVIDPARSERDISPINFO *disp_info, nvdec_ctx *ctx)
{
	int ret = WaitForSingleObject(ctx->mutex_for_queue, INFINITE);

	ctx->is_frame_in_use[disp_info->picture_index] = true;
	ctx->frame_queue->push(disp_info);

	ReleaseMutex(ctx->mutex_for_queue);

	return 0;
}

CUVIDPARSERDISPINFO *nvdec_frame_queue_pop(nvdec_ctx *ctx)
{
	CUVIDPARSERDISPINFO *info = NULL;

	if (ctx->frame_queue->empty()) {
		return NULL;
	}

	int ret = WaitForSingleObject(ctx->mutex_for_queue, INFINITE);

	info = ctx->frame_queue->front();
	ctx->frame_queue->pop();

	ReleaseMutex(ctx->mutex_for_queue);

	return info;
}

int nvdec_frame_item_release(CUVIDPARSERDISPINFO *info, nvdec_ctx *ctx)
{
	ctx->is_frame_in_use[info->picture_index] = false;

	return 0;
}

int nvdec_cuda_init(nvdec_ctx *ctx)
{
	CUresult cuda_ret = CUDA_SUCCESS;

	CUcontext cuda_ctx_dummy;

	int dev_count = 0;
	int dev_id = 0;		// default use device 0.
	int minor = 0, major = 0;
	int ret = 0;


	// load nv library "nvcuda.dll" and init cuda intefaces
	ret = cuInit(CU_CTX_SCHED_AUTO, __CUDA_API_VERSION, &ctx->cuda_lib);
	ret = cuvidInit(0);


	ret = cuDeviceGetCount(&dev_count);
	if (0 == dev_count) {
		// can not find cuda devices
		return -2;
	}

	//
	ctx->cuda_dev_count = dev_count;

	if (dev_id > dev_count - 1) {
		// invalid device id
		LOG("Error: invalid device id[%d]\n", dev_id);
		return -3;
	}

	// get the actual device
	ret =cuDeviceGet(&ctx->cuda_dev, dev_id);

	// create the cuda context and pop the current one
	ret = cuCtxCreate(&ctx->cuda_ctx, CU_CTX_SCHED_BLOCKING_SYNC, ctx->cuda_dev);


	//ret = cuCtxPopCurrent(&cuda_ctx_dummy);

#if 0
	// get compute capabilities and device name;
	//int major, minor;
	uint32_t total_mem;
	char device_name[256];
	ret = cuDeviceComputeCapability(&major, &minor, ctx->cuda_dev);
	ret = cuDeviceGetName(device_name, 256, ctx->cuda_dev);

	if (sizeof(void *) == 4) {
		// 32 bit OS
		ctx->cuda_module_mgr = new CUmoduleManager("NV12ToARGB_drvapi_Win32.ptx", "", 2, 2, 2);
	}
	else {
		ctx->cuda_module_mgr = new CUmoduleManager("NV12ToARGB_drvapi_x64.ptx", "", 2, 2, 2);
	}

	ctx->cuda_module_mgr->GetCudaFunction("NV12ToARGB_drvapi", &ctx->pfn_nv12toargb);
	ctx->cuda_module_mgr->GetCudaFunction("Passthru_drvapi", &ctx->pfn_pass_thru);

	cuda_ret = cuStreamCreate(&ctx->readback_sid, 0);
	cuda_ret = cuStreamCreate(&ctx->kernel_sid, 0);
#endif

	//

	ret = cuCtxPopCurrent(&cuda_ctx_dummy);


	return 0;
}

/*
 *	@extradata: codec extradata, such as sps pps
 */
int nvdec_create_parser(int codec_type, char *extra_data, int len, nvdec_ctx *ctx)
{
	CUcontext dummy;
	int ret = 0;

	memset(&ctx->cuparse_info, 0, sizeof(CUVIDPARSERPARAMS));
	memset(&ctx->cuparse_ext, 0, sizeof(CUVIDEOFORMATEX));
	memset(&ctx->cuvid_pkt, 0x0, sizeof(CUVIDSOURCEDATAPACKET));

	ctx->cuparse_info.pExtVideoInfo = &ctx->cuparse_ext;

	switch (codec_type)
	{
	case NV_CODEC_AVC:
		ctx->cuparse_info.CodecType = cudaVideoCodec_H264;
		break;

	case NV_CODEC_HEVC:
		ctx->cuparse_info.CodecType = cudaVideoCodec_HEVC;
		break;

	case NV_CODEC_MJPEG:
		ctx->cuparse_info.CodecType = cudaVideoCodec_JPEG;
		break;

	case NV_CODEC_MPEG4:
		ctx->cuparse_info.CodecType = cudaVideoCodec_MPEG4;
		break;

	case NV_CODEC_MPEG2:
		ctx->cuparse_info.CodecType = cudaVideoCodec_MPEG2;
		break;

	case NV_CODEC_VP8:
		ctx->cuparse_info.CodecType = cudaVideoCodec_VP8;
		break;

	case NV_CODEC_VP9:
		ctx->cuparse_info.CodecType = cudaVideoCodec_VP9;
		break;

	case NV_CODEC_VC1:
		ctx->cuparse_info.CodecType = cudaVideoCodec_VC1;
		break;

	default:
		ctx->cuparse_info.CodecType = cudaVideoCodec_H264;
		break;
	}
	
	//
	ctx->cuparse_ext.format.chroma_format = cudaVideoChromaFormat_420;
	ctx->cuparse_ext.format.progressive_sequence = 1;

	// no extent data
	if (extra_data && len > 0) {
		ctx->cuparse_ext.format.seqhdr_data_length = len;
		memcpy(ctx->cuparse_ext.raw_seqhdr_data, extra_data, (sizeof(ctx->cuparse_ext.raw_seqhdr_data) > len ? len : sizeof(ctx->cuparse_ext.raw_seqhdr_data)));
	}


	ctx->cuparse_info.ulMaxNumDecodeSurfaces = NVDEC_MAX_FRAMES;
	ctx->cuparse_info.ulMaxDisplayDelay = 4;
	ctx->cuparse_info.pUserData = ctx;
	ctx->cuparse_info.pfnSequenceCallback = cuvid_handle_video_sequence;
	ctx->cuparse_info.pfnDecodePicture = cuvid_handle_picture_decode;
	ctx->cuparse_info.pfnDisplayPicture = cuvid_handle_picture_display;
	//ctx->cuparse_info.

	ret = cuCtxPushCurrent(ctx->cuda_ctx);

	// test dummy decoder

	// create parser
	ret = cuvidCreateVideoParser(&ctx->cuparser, &ctx->cuparse_info);

	// try to parser
	ctx->cuvid_pkt.payload = ctx->cuparse_ext.raw_seqhdr_data;
	ctx->cuvid_pkt.payload_size = ctx->cuparse_ext.format.seqhdr_data_length;
	if (ctx->cuvid_pkt.payload && ctx->cuvid_pkt.payload_size > 0) {
		ret = cuvidParseVideoData(ctx->cuparser, &ctx->cuvid_pkt);
	}

	ret = cuCtxPopCurrent(&dummy);


	return 0;
}

int nvdec_decode_packet(uint8_t *in_buf, int in_data_len, nvdec_ctx *ctx)
{
	int ret = 0;

	CUcontext dummy;

	if (ctx->is_flush && in_data_len > 0)
		return 0;


	ret = cuCtxPushCurrent(ctx->cuda_ctx);
	
	memset(&ctx->cuvid_pkt, 0, sizeof(ctx->cuvid_pkt));

	if (in_data_len > 0) {
		ctx->cuvid_pkt.payload_size = in_data_len;
		ctx->cuvid_pkt.payload = in_buf;
		// timestamp
		ctx->cuvid_pkt.flags = CUVID_PKT_TIMESTAMP;
		ctx->cuvid_pkt.timestamp = 0;
	}
	else {
		ctx->cuvid_pkt.flags = CUVID_PKT_ENDOFSTREAM;
		ctx->is_flush = 1;
	}

	ret = cuvidParseVideoData(ctx->cuparser, &ctx->cuvid_pkt);



	ret = cuCtxPopCurrent(&dummy);



	return 0;
}


int nvdec_decode_output_frame(int *got_frame, nvdec_ctx *ctx)
{
	int ret = 0;
	CUcontext dummy;
	CUVIDPARSERDISPINFO *disp_info = NULL;
	CUdeviceptr mapped_frame = 0;
	*got_frame = 0;


	disp_info = nvdec_frame_queue_pop(ctx);

	CCtxAutoLock lck(ctx->ctx_lock);

	ret = cuCtxPushCurrent(ctx->cuda_ctx);

	if (disp_info) {
		CUVIDPROCPARAMS params;
		unsigned int pitch = 0;
		unsigned int width = 0;
		unsigned int height = 0;


		memset(&params, 0, sizeof(CUVIDPROCPARAMS));

		params.progressive_frame = disp_info->progressive_frame;
		params.top_field_first = disp_info->top_field_first;
		//params.second_field = parsed_frame->second_field;
		params.unpaired_field = (disp_info->repeat_first_field < 0);

		ret = cuvidMapVideoFrame(ctx->cudecoder, disp_info->picture_index, &mapped_frame, &pitch, &params);
		width = ctx->dec_create_info.ulTargetWidth;

		height = ctx->dec_create_info.ulTargetHeight;

		if (ctx->is_first_frame ) {
			nvdec_out_frame_init(pitch, height, ctx);
		}

		ctx->cur_out_frame = nvdec_get_free_frame(ctx);

		// Output NV12
		ctx->cur_out_frame->pitch = pitch;
		ret = cuMemcpyDtoH(ctx->cur_out_frame->big_buf, mapped_frame, (pitch * height * 3 / 2));
		ctx->cur_out_frame->data_len = (pitch * height * 3 / 2);

		*got_frame = 1;

		//
		nvdec_frame_item_release(disp_info, ctx);
	}
	
	if (mapped_frame) {
		ret = cuvidUnmapVideoFrame(ctx->cudecoder, mapped_frame);
	}
	ret = cuCtxPopCurrent(&dummy);
	// check frame queue


	return 0;


}


int nvdec_decode_frame(uint8_t *in_buf, int in_data_len, int *got_frame, nvdec_ctx *ctx)
{
	int ret = 0;
	*got_frame = 0;
	// decode packet
	if (!ctx->is_flush) {
		ret = nvdec_decode_packet(in_buf, in_data_len, ctx);
	}

	// output frame
	ret = nvdec_decode_output_frame(got_frame, ctx);
	
	return 0;
}

int nvdec_create_decoder(CUVIDEOFORMAT* format, nvdec_ctx *ctx)
{
	int ret = 0;
	CUVIDDECODECREATEINFO *cuinfo = NULL;
	cuinfo = &ctx->dec_create_info;


	memset(cuinfo, 0, sizeof(CUVIDDECODECREATEINFO));
	cuinfo->CodecType = format->codec;

	cuinfo->ChromaFormat = format->chroma_format;
	cuinfo->OutputFormat = cudaVideoSurfaceFormat_NV12;
	cuinfo->DeinterlaceMode = cudaVideoDeinterlaceMode_Adaptive;

	cuinfo->ulWidth = format->coded_width;
	cuinfo->ulHeight = format->coded_height;

	cuinfo->ulTargetWidth = format->display_area.right - format->display_area.left;		//cuinfo->ulWidth;
	cuinfo->ulTargetHeight = format->display_area.bottom - format->display_area.top;	//cuinfo->ulHeight;

	cuinfo->display_area.left = 0;
	cuinfo->display_area.top = 0;
	cuinfo->display_area.right = (short)cuinfo->ulTargetWidth;
	cuinfo->display_area.bottom = (short)cuinfo->ulTargetHeight;

	cuinfo->ulNumDecodeSurfaces = NVDEC_MAX_FRAMES;
	cuinfo->ulNumOutputSurfaces = 1;
	cuinfo->ulCreationFlags = cudaVideoCreate_PreferCUVID;// cudaVideoCreate_PreferCUDA;
	cuinfo->bitDepthMinus8 = format->bit_depth_luma_minus8;
	cuinfo->DeinterlaceMode = cudaVideoDeinterlaceMode_Weave;
	cuinfo->vidLock = ctx->ctx_lock;


	ret = cuvidCreateDecoder(&ctx->cudecoder, cuinfo);

	return 0;
}

// NV output NV12 YUV420
int nvdec_out_frame_init(uint32_t pitch, uint32_t height, nvdec_ctx *ctx)
{
	int ret = 0;
#define MAX_OUTPUT_FRAMES 1
	ctx->num_out_frames = MAX_OUTPUT_FRAMES;
	ctx->out_frame = new nv_frame_buf[ctx->num_out_frames];
	memset(ctx->out_frame, 0, ctx->num_out_frames * sizeof(nv_frame_buf));

	//ctx->cur_out_frame = &ctx->out_frame[0];
	ctx->cur_out_frame_idx = 0;

	//ctx->is_first_frame = 1;
	
	nv_frame_buf *tmp = NULL;
	int len = 0;

	for (int i = 0; i < ctx->num_out_frames; i++) {
		tmp = &ctx->out_frame[i];
		len = pitch * height * 3 / 2;

		tmp->big_buf_len = len;
		//tmp->big_buf =  new unsigned char[len];//
		ret = cuMemAllocHost((void**)&tmp->big_buf, len);

	}

	ctx->is_first_frame = 0;
	return ret;
}

nv_frame_buf *nvdec_get_free_frame(nvdec_ctx *ctx)
{
	nv_frame_buf *frame = NULL;

	if (0 == ctx->num_out_frames)
		return frame;

	if (1 == ctx->num_out_frames) {
		frame = ctx->out_frame;
	}
	else {
		// 
		int index = 0;
		index = (ctx->cur_out_frame_idx++) % ctx->num_out_frames;
		frame = &ctx->out_frame[index];
	}
		

	return frame;
}

int nvdec_out_frame_deinit(nvdec_ctx *ctx)
{
	int ret = 0;

	nv_frame_buf *tmp = NULL;

	if (ctx->out_frame) {
		for (int i = 0; i < ctx->num_out_frames; i++) {
			tmp = &ctx->out_frame[i];
			cuMemFreeHost((void*)tmp->big_buf);
			//delete[] tmp->big_buf;
		}
		delete[] ctx->out_frame;
		ctx->out_frame = NULL;
	}
	
	ctx->is_first_frame = 1;

	return ret;
}



/****************************************************************************************
 *		output interfaces
 ***************************************************************************************/
/** 
 *	@desc:	 create decode handle
 *	 
 *	@return: handle for use
 */
JMTDLL_FUNC handle_nvdec jm_nvdec_create_handle()
{
	return (handle_nvdec)nvdec_ctx_create();
}

/** 
 *   @desc:   Init decode before use
 *   @param: codec_type:  0 - H.264,  1 - H.265
 *   @param: extra_data: sps or pps buffer, = NULL is OK
 *   @param: len: extra_data length
 *   @param: handle: decode handle return by jm_nvdec_create_handle()
 *
 *   @return: 0 - successful, else failed
 */
JMTDLL_FUNC int jm_nvdec_init(int codec_type, char *extra_data, int len, handle_nvdec handle)
{
	return nvdec_decode_init(codec_type, extra_data, len, (nvdec_ctx*)handle);
}

/** 
 *   @desc:   destroy decode handle
 *   @param: handle: decode handle return by jm_nvdec_create_handle()
 *
 *   @return: 0 - successful, else failed
 */
JMTDLL_FUNC int jm_nvdec_deinit(handle_nvdec handle)
{
	return nvdec_decode_deinit((nvdec_ctx*)handle);
}

/** 
 *   @desc:   decode video frame
 *   @param: in_buf[in]: video frame data
 *   @param: in_data_len[in]: data length
 *   @param: got_frame[out]: 1 - if decode output YUV frame, else 0.
 *   @param: handle: decode handle fater init by jm_nvdec_init()
 *
 *   @return: 0 - successful, else failed
 */
JMTDLL_FUNC int jm_nvdec_decode_frame(unsigned char *in_buf, int in_data_len, int *got_frame, handle_nvdec handle)
{
	nvdec_ctx *ctx = (nvdec_ctx*)handle;
	return nvdec_decode_frame(in_buf, in_data_len, got_frame, ctx);
}


/** 
 *   @desc:   if got_frame get 1 from jm_nvdec_decode_frame(), call this function to get YUV data
 *   @param: out_fmt[in]: YUV format type, 0 - NV12, 1 - YUV420(YV12)
 *   @param: out_buf[out]: output YUV data buffer
 *   @param: out_len[in][out]: [in] out_buf buffer size, if < YUV420 frame size, will return error(-1), [out]YUV420 frame size.
 *   @param: handle: decode handle fater init by jm_nvdec_init()
 *
 *   @return: 0 - successful, else failed
 */
JMTDLL_FUNC int jm_nvdec_output_frame(int out_fmt, unsigned char *out_buf, int *out_len, handle_nvdec handle)
{
	nvdec_ctx *ctx = (nvdec_ctx *)handle;
	int width	= ctx->dec_create_info.ulWidth;
	int height	= ctx->dec_create_info.ulHeight;
	int pitch	= ctx->cur_out_frame->pitch;
	const unsigned char *py = ctx->cur_out_frame->big_buf;
	const unsigned char *puv = ctx->cur_out_frame->big_buf + pitch * height;
	unsigned char *pdst = out_buf;	// out put YUV NV12 buffer

	if (*out_len < (width * height * 3 / 2))
		return -1;

	*out_len = 0;


	int x, y, w2, h2;
	int xy_offset = width * height;

	if (0 == out_fmt) {
		// NV12
		for (y = 0; y < height * 3 / 2; y++) {
			memcpy(&pdst[y*width], py, width);
			py += pitch;
		}
	}
	else {
		// YUV420 - YV12
		// luma
		for (y = 0; y < height; y++) {
			memcpy(&pdst[y*width], py, width);
			py += pitch;
		}

		// chroma
		w2 = width >> 1;
		h2 = height >> 1;
		int uv_offset = w2 * h2;

		for (y = 0; y < h2; y++) {
			for (x = 0; x < w2; x++) {
				pdst[xy_offset + y * w2 + x] = puv[x * 2];
				pdst[xy_offset + uv_offset + y * w2 + x] = puv[x * 2 + 1];
			}
			puv += pitch;
		}

	}


	//memcpy(out_buf, ctx->cur_out_frame->big_buf, ctx->cur_out_frame->data_len);
	*out_len = width * height * 3 / 2;

	return *out_len;
}

/** 
 *   @desc:   get YUV frame resolution (width x height), this function will called after successfully decode first frame.
 *   @param: disp_width[out]: width
 *   @param: disp_height[out]: height
 *   @param: handle: decode handle fater init by jm_nvdec_init()
 *
 *   @return: 0 - successful, else failed
 */
JMTDLL_FUNC int jm_nvdec_stream_info(int *disp_width, int *disp_height, handle_nvdec handle)
{
	nvdec_ctx *ctx = (nvdec_ctx *)handle;
	*disp_width = ctx->dec_create_info.ulWidth;
	*disp_height = ctx->dec_create_info.ulHeight;

	return 0;
}



