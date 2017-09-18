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
#include "intel_dec.h"
#include "jm_intel_dec.h"
#include "mfxplugin.h"

#include <time.h>


#pragma comment(lib,"libmfx.lib")	// MT
//#pragma comment(lib,"libmfx_vs2015.lib")	// MD
#pragma comment(lib,"legacy_stdio_definitions.lib")

#define MSDK_SLEEP(X)                   { Sleep(X); }
#define MSDK_ALIGN32(X)                 (((mfxU32)((X)+31)) & (~ (mfxU32)31))
#define MSDK_CHECK_RESULT(P, X, ERR)    {if ((X) > (P)) {return ERR;}}
#define MSDK_IGNORE_MFX_STS(P, X)       {if ((X) == (P)) {P = MFX_ERR_NONE;}}

#define MAX_INPUT_BITSTREAM_SIZE		(10*1024*1024)
#define FREE_BUF_FOR_MORE_DATA			(5*1024 * 1024)
#define MUX_OFFSET_FOR_INPUT_BITSTREAM	(4*1024*1024)
//#define MAX_OUTPUT_YUV_COUNT			20	
#define NUM_SURFACE_ADDITION			30
#define MUTEXT_NAME_YUV					("intel_output_yuv")
#define MUTEXT_NAME_INPUT				("intel_input_bs")
#define EVENT_NAME_YUV					("intel_yuv_evt")
#define SYNC_WAITING_TIME				(60000)        	
#define INTEL_DEC_ASYNC_DEPTH			4	

#define INDEX_OF_RESERVED_IN_USE 		0



/*
 *	Decode thread
 *
 */
DWORD WINAPI decode_thread_proc(LPVOID param)
{
	intel_ctx *ctx = (intel_ctx *)param;
	mfxStatus sts = MFX_ERR_NONE;

	do {

		if(!ctx->is_param_inited) {
			// decode header
			sts = dec_decode_header(ctx);
		}
		else {
			// decode packet
			sts = dec_decode_packet(ctx);
		}


	} while ((!ctx->is_eof) || (MFX_ERR_NONE == sts));	// user exit, set is_exit=true direct exit

	//
	ctx->elapsed_time = clock() - ctx->elapsed_time;

	intel_dec_show_info(ctx);


	int loop = 0;
#define MAX_LOOP_COUNT (100)
	while (!ctx->out_surf_queue->empty() && ++loop < MAX_LOOP_COUNT) {
		LOG("Waiting for bitstream output!\n");
		MSDK_SLEEP(1);
	}

	ctx->is_exit = true;

	return 0;
}


intel_ctx *intel_dec_create()
{
	intel_ctx *ctx = new intel_ctx;
	memset(ctx, 0x0, sizeof(intel_ctx));

	ctx->dec_param = new mfxVideoParam;
	memset(ctx->dec_param, 0x0, sizeof(mfxVideoParam));
	//

	LOG("intel decode create\n");
	return ctx;
}

/** 
 *   @desc:   Init decode before use
 *	 @param: dec_id: index of runing decode at the same time
 *   @param: codec_type:  0 - H.264,  1 - H.265
 *   @param: out_fmt: output frame format, YUV420, NV12, ARGB ...
 *   @param: ctx: decode context return by intel_dec_create()
 *
 *   @return: 0 - successful, else failed
 */
int intel_dec_init(int codec_type, int out_fmt, intel_ctx *ctx)
{
	int ret = 0;

	mfxStatus sts = MFX_ERR_NONE;

	// 
	ctx->hw_try = 1;	// try hardware decode first
	ctx->out_fmt = out_fmt;

	// init session
	sts = dec_init_session(ctx);
	if(MFX_ERR_NONE != sts) {
		return -1;	// Error
	}


	// init input bitstream
	dec_init_input_bitstream(ctx);

	// just support alloc memroy in system memory, donot need mfxFrameAllocator
	memset(ctx->dec_param, 0x0, sizeof(mfxVideoParam));
	ctx->dec_param->mfx.CodecId = dec_get_codec_id_by_type(codec_type, ctx);
	ctx->dec_param->IOPattern = MFX_IOPATTERN_OUT_SYSTEM_MEMORY;

	// video_dec_plugin
	dec_plugin_load(ctx->dec_param->mfx.CodecId, ctx);

	// waiting for data, param_inited = 0;
	ctx->is_param_inited = 0;


	dec_create_decode_thread(ctx);

	LOG("intel decode init\n");

	return ret;
}

int intel_dec_deinit(intel_ctx *ctx)
{
	int ret = 0;

	LOG("intel decode deinit...\n");
	// TODO:

	ctx->is_eof = true;

	// thread
	dec_wait_thread_exit(ctx);

	// in_bs
	dec_deinit_input_bitstream(ctx);

	// deinit surfaces
	dec_surfaces_deinit(ctx);

	// plugin
	dec_plugin_unload(ctx->dec_param->mfx.CodecId, ctx);

	// close decode
	MFXVideoDECODE_Close(ctx->session);

	// session
	if (ctx->session) {
		MFXClose(ctx->session);
	}

	// dec_param
	if (ctx->dec_param) {
		// release ExtParam

		delete ctx->dec_param;
		ctx->dec_param = 0;
	}


	return ret;
}

/*
 *	@desc: put data to decode buffer.
 */
int intel_dec_put_input_data(const uint8_t *data, const int len, intel_ctx *ctx)
{
	int ret = 0;
	mfxBitstream *pbs = NULL;

	// mutex lock
	ret = WaitForSingleObject(ctx->mutex_input, INFINITE);
	pbs = ctx->in_bs;

	if (0 == pbs->DataLength)
		pbs->DataOffset = 0;

	if(((pbs->DataOffset > MUX_OFFSET_FOR_INPUT_BITSTREAM) || len > pbs->MaxLength - pbs->MaxLength - pbs->DataOffset) && (pbs->DataLength > 0)) {
		memmove(pbs->Data, pbs->Data + pbs->DataOffset, pbs->DataLength);
		pbs->DataOffset = 0;
	}

	if(len > (pbs->MaxLength - pbs->DataLength)) {
		int new_size = 0;
		if(len > pbs->MaxLength) {
			new_size = pbs->MaxLength + len;
		}
		else {
			new_size = pbs->MaxLength + len * 2;
		}
		dec_extend_bitstream(new_size, pbs);
	}

	//
	memcpy(pbs->Data + pbs->DataOffset + pbs->DataLength, data, len);
	pbs->DataLength += len;

	ret = pbs->DataLength;

	ReleaseMutex(ctx->mutex_input);
	// return current buffer data length

	//
	if (ctx->is_more_data) {
		ctx->is_more_data = 0;
	}

	LOG("intel decode put input data[len=%d] ...\n", len);

	return ret;
}

/*
 *	decode output yuv frame to app.
 *	@param: out_buf[out], NULL - will output yuv frame data len to @param out_len, else copy yuv frame to out_buf.
 *	@param: out_len[out], output yuv frame data len.
 *	@param: ctx[in]
 *
 *	return:  0 - success, else failed.
 */
int intel_dec_output_yuv_frame(uint8_t *out_buf, int *out_len, intel_ctx *ctx)
{
	// TODO: update this function
	mfxFrameInfo *info = NULL;
	mfxFrameData *data = NULL;
	mfxFrameSurface1 *surface = NULL;
	// pop surface
	surface = dec_surface_pop(ctx);
	if (!surface) {
		*out_len = 0;
		return -1;
	}

	// copy data from surface to out_buf
	info = &surface->Info;
	data = &surface->Data;

	int y_len = info->CropW * info->CropH;
	int uv_len = y_len / 2;

	if (*out_len < y_len + uv_len) {
		LOG("Error: out_buf not enough size!\n");
		*out_len = 0;
		return -2;
	}

	switch (info->FourCC)
	{
	case MFX_FOURCC_NV12:
	{
		int w = info->CropW, h = info->CropH;
		int crop_x = info->CropX, crop_y = info->CropY;
		int pitch = data->Pitch;

		int i = 0, j = 0;

		mfxU8 *p = NULL;
		mfxU8 *dst = out_buf;

		// Y
		for (i = 0; i < h; i++) {
			p = data->Y + (crop_y * pitch + crop_x) + i * pitch;
			memcpy(dst + i * w, p, w);
		}

		if (0 == ctx->out_fmt) {
			// NV12
			// UV
			crop_x /= 2;
			crop_y /= 2;
			h /= 2;
			dst = out_buf + y_len;
			for (i = 0; i < h; i++) {
				p = data->UV + (crop_y * pitch + crop_x) + i * pitch;
				memcpy(dst + i*w, p, w);
			}
		}
		else {
			// YV12
			crop_x /= 2;
			crop_y /= 2;
			h /= 2;
			w /= 2;
			//dst = frame_bs->Data + y_len;
			mfxU8 *pu = out_buf + y_len;
			mfxU8 *pv = pu + uv_len / 2;
			for (i = 0; i < h; i++) {
				p = data->UV + (crop_y * pitch + crop_x) + i * pitch;
				for (j = 0; j < w; j++) {
					pu[i * w + j] = p[2 * j];
					pv[i * w + j] = p[2 * j + 1];
				}
			}
		}

		*out_len = (y_len + uv_len);

		break;
	}
	default:
		break;
	}


	// mark surface dequeue
	dec_surface_dequeue_mark(surface);

	return 0;
}


int intel_dec_stop_input_data(intel_ctx *ctx)
{
	ctx->is_eof = true;

	LOG("\nintel decode set eof...\n");
	return 0;
}

bool intel_dec_is_exit(intel_ctx *ctx)
{
	return ctx->is_exit;
}

/*
*	@desc: if intel decode need more input data, return ture, else return false.
*/
bool intel_dec_need_more_data(intel_ctx *ctx)
{
	// if input data buffer free buffer space > FREE_BUF_FOR_MORE_DATA, decode request more input data,
	// make sure intel decode pipeline running uninterrupted
	if ((ctx->in_bs->MaxLength - ctx->in_bs->DataLength > FREE_BUF_FOR_MORE_DATA) || (ctx->is_more_data)) {
		return true;
	}

	return false;
}

/*
 *	@desc: return input buffer length, that App can input more data
 */
int intel_dec_get_input_free_buf_len(intel_ctx *ctx)
{
	return (ctx->in_bs->MaxLength - ctx->in_bs->DataLength);
}

int intel_dec_set_yuv_callback(void *user_data, YUV_CALLBACK fn, intel_ctx *ctx)
{
	ctx->user_data = user_data;
	ctx->yuv_callback = fn;

	return 0;
}

/*
 *	@desc: intel media sdk init session
 */
mfxStatus dec_init_session(intel_ctx *ctx)
{
	mfxStatus sts = MFX_ERR_NONE;
	mfxVersion ver = {1, 1};

	mfxIMPL impl = MFX_IMPL_HARDWARE_ANY;
	sts = MFXInit(impl, &ver, &ctx->session);	
	if(MFX_ERR_NONE != sts) {
		impl = MFX_IMPL_HARDWARE;
		sts = MFXInit(impl, &ver, &ctx->session);
	}

	// try software 
	if(MFX_ERR_NONE != sts) {
		sts = MFXInit(MFX_IMPL_SOFTWARE, &ver, &ctx->session);
	}

	if(MFX_ERR_NONE != sts) {
		return sts;
	}

	// query support impl and version
	sts = MFXQueryIMPL(ctx->session, &ctx->impl);
	sts = MFXQueryVersion(ctx->session, &ver);

	// show impl and version
	LOG("IMPL: 0x%04X, Version: V%d.%d\n", ctx->impl, ver.Major, ver.Minor);


	// TODO: check current version support feature


	return sts;
}

/*
 *	@desc: covert codec type to intel media sdk support codec id
 */
uint32_t dec_get_codec_id_by_type(int codec_type, intel_ctx *ctx)
{
	uint32_t codec_id = MFX_CODEC_AVC;

	switch(codec_type)
	{
		case CODEC_TYPE_AVC:
			codec_id = MFX_CODEC_AVC;
		break;

		case CODEC_TYPE_HEVC:
			codec_id = MFX_CODEC_HEVC;
		break;

		case CODEC_TYPE_MPEG2:
			codec_id = MFX_CODEC_MPEG2;
		break;

		case CODEC_TYPE_VC1:
			codec_id = MFX_CODEC_VC1;
		break;

		case CODEC_TYPE_CAPTURE:
			codec_id = MFX_CODEC_CAPTURE;
		break;

		case CODEC_TYPE_VP9:
			codec_id = MFX_CODEC_VP9;
		break;

		default: 
			codec_id = MFX_CODEC_AVC;
		break;
	}

	return codec_id;
}

char *dec_get_codec_id_string(intel_ctx *ctx)
{
	switch (ctx->dec_param->mfx.CodecId)
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

int dec_init_input_bitstream(intel_ctx *ctx)
{
	ctx->in_bs = new mfxBitstream;
	memset(ctx->in_bs, 0x0, sizeof(mfxBitstream));

	dec_init_bitstream(MAX_INPUT_BITSTREAM_SIZE, ctx->in_bs);

	ctx->mutex_input = CreateMutexA(NULL, FALSE, MUTEXT_NAME_INPUT);

	return 0;
}

int dec_deinit_input_bitstream(intel_ctx *ctx)
{
	if (ctx->mutex_input) {
		CloseHandle(ctx->mutex_input);
		ctx->mutex_input = 0;
	}
	if (ctx->in_bs) {
		if (ctx->in_bs->Data)
			delete[] ctx->in_bs->Data;

		delete ctx->in_bs;
		ctx->in_bs = 0;
	}

	return 0;
}

mfxStatus dec_init_bitstream(int buf_size, mfxBitstream *pbs)
{
	mfxStatus sts = MFX_ERR_NONE;

	memset(pbs, 0x0, sizeof(mfxBitstream));
	pbs->MaxLength = buf_size;
	pbs->Data = new mfxU8[pbs->MaxLength];

	if (!pbs->Data) {
		// Error
		return MFX_ERR_MEMORY_ALLOC;
	}

	return sts;
}

int dec_extend_bitstream(int new_size, mfxBitstream *pbs)
{

	mfxU8 *pData = new mfxU8[new_size];

	LOG("intel decode extend bitstream data[newsize = %d], buf=%p\n", new_size, pbs->Data);

	if(!pbs->Data) {
		pbs->Data		= pData;
		pbs->DataOffset = 0;
		pbs->DataLength = 0;
		pbs->MaxLength	= new_size;

		return new_size;
	}

	// memcpy
	memcpy(pData, pbs->Data + pbs->DataOffset,  pbs->DataLength);

	delete [] pbs->Data;

	pbs->Data = pData;
	pbs->DataOffset = 0;
	pbs->MaxLength = new_size;



	return new_size;
}


// surfaces
int dec_surfaces_init(intel_ctx *ctx)
{
	mfxStatus sts = MFX_ERR_NONE;

	//
	mfxFrameAllocRequest request;
	memset(&request, 0x0, sizeof(mfxFrameAllocRequest));
	sts = MFXVideoDECODE_QueryIOSurf(ctx->session, ctx->dec_param, &request);

	mfxU16 num_surfaces = request.NumFrameSuggested + NUM_SURFACE_ADDITION;
	ctx->num_surf = num_surfaces;


	// allocate surfaces for decoder
	mfxU16 width = (mfxU16)MSDK_ALIGN32(request.Info.Width);
	mfxU16 height = (mfxU16)MSDK_ALIGN32(request.Info.Height);
	mfxU8  bits_pre_pixel = 12;	// NV12 format is a 12 bits per pixel frame info
	mfxU32 surface_size = width * height * bits_pre_pixel / 8;
	ctx->surface_buffers = (mfxU8 *) new mfxU8[num_surfaces * surface_size];

	ctx->surfaces = new mfxFrameSurface1 *[num_surfaces];
	// check pointer
	if (!ctx->surfaces || !ctx->surface_buffers) {
		// error
		LOG("ERROR: enc_surfaces_init() can not alloc memory\n");
		return MFX_ERR_MEMORY_ALLOC;
	}

	for (int i = 0; i < num_surfaces; i++) {
		ctx->surfaces[i] = new mfxFrameSurface1;
		memset(ctx->surfaces[i], 0x0, sizeof(mfxFrameSurface1));
		
		memcpy(&(ctx->surfaces[i]->Info), &(ctx->dec_param->mfx.FrameInfo), sizeof(mfxFrameInfo));
		ctx->surfaces[i]->Data.Y = &ctx->surface_buffers[surface_size * i];
		ctx->surfaces[i]->Data.U = ctx->surfaces[i]->Data.Y + width * height;
		ctx->surfaces[i]->Data.V = ctx->surfaces[i]->Data.U + 1;
		ctx->surfaces[i]->Data.Pitch = width;

		memset(ctx->surfaces[i]->Data.Y, 100, width * height);
		memset(ctx->surfaces[i]->Data.U, 50, (width * height) / 2);
	}

	LOG("intel decode alloc surfaces[count=%d]\n", num_surfaces);

	//
	ctx->out_surf_queue = new std::queue<mfxFrameSurface1 *>;

	
	// init mutex
	ctx->mutex_yuv = CreateMutexA(NULL, FALSE, MUTEXT_NAME_YUV);

	// init event
	ctx->event_yuv = CreateEventA(NULL, TRUE, FALSE, EVENT_NAME_YUV);


	return 0;
}


int dec_surfaces_deinit(intel_ctx *ctx)
{
	if (ctx->surfaces) {
		for (int i = 0; i < ctx->num_surf; i++) {
			if (ctx->surfaces[i]) {
				delete ctx->surfaces[i];
				ctx->surfaces[i] = 0;
			}
		}

		delete[] ctx->surfaces;
		ctx->surfaces = NULL;
	}

	if (ctx->surface_buffers) {
		delete[] ctx->surface_buffers;
		ctx->surface_buffers = NULL;
	}

	if (ctx->out_surf_queue) {
		delete ctx->out_surf_queue;
		ctx->out_surf_queue = NULL;
	}

	if (ctx->mutex_yuv) {
		CloseHandle(ctx->mutex_yuv);
		ctx->mutex_yuv = 0;
	}

	if (ctx->event_yuv) {
		CloseHandle(ctx->event_yuv);
		ctx->event_yuv = 0;
	}

	return 0;

}



/*	
 *	return : if cannot found, return mfxStatus - MFX_ERR_NOT_FOUND
 */
int dec_get_free_surface_index(intel_ctx *ctx)
{
	int i = 0;

	for(i = 0; i < ctx->num_surf; i++) {
		if((0 == ctx->surfaces[i]->Data.Locked) && (0 == ctx->surfaces[i]->reserved[INDEX_OF_RESERVED_IN_USE])) {
			//ctx->surfaces[i]->reserved[INDEX_OF_RESERVED_IN_USE] = 1;
			return i;
		}
	}

	return MFX_ERR_NOT_FOUND;
}

void dec_surface_enquue_mark(mfxFrameSurface1 *surface)
{
	surface->reserved[INDEX_OF_RESERVED_IN_USE] = 1;

	return ;
}

void dec_surface_dequeue_mark(mfxFrameSurface1 *surface)
{
	surface->reserved[INDEX_OF_RESERVED_IN_USE] = 0;

	return ;
}

int dec_surface_push(mfxFrameSurface1 *surface, intel_ctx *ctx)
{
	dec_surface_enquue_mark(surface);

	WaitForSingleObject(ctx->mutex_yuv, INFINITE);
	ctx->out_surf_queue->push(surface);
	ReleaseMutex(ctx->mutex_yuv);
	
	return 0;
}

mfxFrameSurface1 *dec_surface_pop(intel_ctx *ctx)
{
	mfxFrameSurface1 *surface = NULL;

	if(!ctx->out_surf_queue || ctx->out_surf_queue->empty()) {
		return NULL;
	}

	WaitForSingleObject(ctx->mutex_yuv, INFINITE);
	
	surface = ctx->out_surf_queue->front();
	ctx->out_surf_queue->pop();

	ReleaseMutex(ctx->mutex_yuv);

	return surface;
}


int dec_plugin_load(uint32_t codec_id, intel_ctx *ctx)
{

	mfxStatus sts = MFX_ERR_NONE;
	const mfxPluginUID *uid = NULL;

	if (MFX_CODEC_HEVC == codec_id) {
		uid = &MFX_PLUGINID_HEVCD_HW;
	}
	else if (MFX_CODEC_VP9 == codec_id) {
		uid = &MFX_PLUGINID_VP9D_HW;
	}

	if (uid) {
		sts = MFXVideoUSER_Load(ctx->session, uid, 1);
		return (MFX_ERR_NONE == sts ? 0 : -1);
	}

	return -1;
}

int dec_plugin_unload(uint32_t codec_id, intel_ctx *ctx)
{

	mfxStatus sts = MFX_ERR_NONE;
	const mfxPluginUID *uid = NULL;

	if (MFX_CODEC_HEVC == codec_id) {
		uid = &MFX_PLUGINID_HEVCD_HW;
	}
	else if (MFX_CODEC_VP9 == codec_id) {
		uid = &MFX_PLUGINID_VP9D_HW;
	}

	if (uid) {
		sts = MFXVideoUSER_UnLoad(ctx->session, uid);
		return (MFX_ERR_NONE == sts ? 0 : -1);
	}

	return -1;
}


mfxStatus dec_decode_packet(intel_ctx *ctx)
{
	mfxStatus sts = MFX_ERR_NONE;

	int ret = 0;

	int idx = 0;
    mfxSyncPoint syncp;
    mfxFrameSurface1* surface_out = NULL;

	while(MFX_ERR_NONE <= sts || MFX_ERR_MORE_DATA == sts || MFX_ERR_MORE_SURFACE == sts) {
		if(MFX_WRN_DEVICE_BUSY == sts) {
			// waiting
			MSDK_SLEEP(1);
		}

		if (MFX_ERR_MORE_DATA == sts) {
        	// waiting for data input
        	if(ctx->is_eof)	
        		break;	// if no more data input, break;

			LOG("Warning: need more data\n");
			sts = MFX_ERR_NONE;
			ctx->is_more_data = 1;

        	MSDK_SLEEP(1);
			continue;
        }

        if (MFX_ERR_MORE_SURFACE == sts || MFX_ERR_NONE == sts)
        {
        	// get free surface index
        	idx = dec_get_free_surface_index(ctx);
			if (MFX_ERR_NOT_FOUND == idx) {
				LOG("Error: can not get free surface\n");
				MSDK_SLEEP(1);
				continue;
			}
			
        }

        // lock in_bs
		WaitForSingleObject(ctx->mutex_input, INFINITE);
		sts = MFXVideoDECODE_DecodeFrameAsync(ctx->session, ctx->in_bs, ctx->surfaces[idx], &surface_out, &syncp);
		ReleaseMutex(ctx->mutex_input);
		// unlock in_bs

		LOG("MFXVideoDECODE_DecodeFrameAsync sts = %d\n", sts);

        // ignore warnings if output is available
        if(MFX_ERR_NONE < sts && syncp) {
        	sts = MFX_ERR_NONE;
        }

        if(MFX_ERR_NONE == sts) {
        	MFXVideoCORE_SyncOperation(ctx->session, syncp, SYNC_WAITING_TIME);
			ctx->num_yuv_frames += 1;
			LOG("---------- YUV Frame count = %d\n", ctx->num_yuv_frames);

			// Output NV12
			//ret = dec_conver_surface_to_bistream(surface_out, ctx);
			dec_surface_push(surface_out, ctx);
        }

		//MSDK_SLEEP(1);

	}


	MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);
	MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);
	
	while (MFX_ERR_NONE <= sts || MFX_ERR_MORE_SURFACE == sts) {
		if (MFX_WRN_DEVICE_BUSY == sts) {
			// waiting
			MSDK_SLEEP(1);
		}


		idx = dec_get_free_surface_index(ctx);
		if (MFX_ERR_NOT_FOUND == idx) {
			LOG("Error: can not get free surface\n");
			MSDK_SLEEP(2);
			continue;
		}

		sts = MFXVideoDECODE_DecodeFrameAsync(ctx->session, NULL, ctx->surfaces[idx], &surface_out, &syncp);

		LOG("MFXVideoDECODE_DecodeFrameAsync() sts = %d\n", sts);

		if (MFX_ERR_NONE == sts) {
			MFXVideoCORE_SyncOperation(ctx->session, syncp, SYNC_WAITING_TIME);
			ctx->num_yuv_frames += 1;
			LOG("---------- YUV Frame count = %d\n", ctx->num_yuv_frames);

			// Output NV12
			//ret = dec_conver_surface_to_bistream(surface_out, ctx);
			dec_surface_push(surface_out, ctx);
		}

	}

	//MSDK_IGNORE_MFX_STS(sts, MFX_ERR_MORE_DATA);
	//MSDK_CHECK_RESULT(sts, MFX_ERR_NONE, sts);


	return sts;
}

mfxStatus dec_decode_header(intel_ctx *ctx)
{
	mfxStatus sts = MFX_ERR_NONE;

	// waiting for bitstreaming enough data for decode head
	WaitForSingleObject(ctx->mutex_input, INFINITE);
	sts = MFXVideoDECODE_DecodeHeader(ctx->session, ctx->in_bs, ctx->dec_param);
	ReleaseMutex(ctx->mutex_input);

	if (MFX_WRN_PARTIAL_ACCELERATION == sts) sts = MFX_ERR_NONE;
	if (MFX_ERR_NONE > sts) 	return sts;

	// only support system memory
	ctx->dec_param->IOPattern = MFX_IOPATTERN_OUT_SYSTEM_MEMORY;
	ctx->dec_param->AsyncDepth = INTEL_DEC_ASYNC_DEPTH;



	sts = MFXVideoDECODE_Query(ctx->session, ctx->dec_param, ctx->dec_param);

	//
	dec_surfaces_init(ctx);
	
	// init output yuv frame array
	//dec_init_yuv_output(ctx);

	// init dec
	sts = MFXVideoDECODE_Init(ctx->session, ctx->dec_param);

	// init start time
	ctx->elapsed_time = clock();

	ctx->is_param_inited = 1;

	return sts;
}


/****************************************************************************************
 *		
 ***************************************************************************************/

/*
 *	@desc: decode thread.
 */
int dec_create_decode_thread(intel_ctx *ctx)
{
	DWORD thread_id = 0;
	ctx->dec_thread = CreateThread(NULL,
						0, 
						decode_thread_proc,
						ctx,
						0, 
						&thread_id);

	if(!ctx->dec_thread) {
		return -1;
	}

	return 0;
}

int dec_wait_thread_exit(intel_ctx *ctx)
{
	if(ctx->dec_thread) {
		WaitForSingleObject(ctx->dec_thread, INFINITE);
		CloseHandle(ctx->dec_thread);
	}

	return 0;
}


/*	
 *	bitstream function
 */

int intel_dec_show_info(intel_ctx *ctx)
{
	//ctx->elapsed_time = clock() - ctx->elapsed_time;
	sprintf_s(ctx->dec_info, MAX_LEN_DEC_INFO,
		"==========================================\n"
		"Codec:\t\t%s\n"
		"Display:\t%d x %d\n"
		"Pixel Format:\t%s\n"
		"Frame Count:\t%d\n"
		"Elapsed Time:\t%d ms\n"
		"Decode FPS:\t%f fps\n"
		"==========================================\n",
		dec_get_codec_id_string(ctx),
		ctx->dec_param->mfx.FrameInfo.Width, ctx->dec_param->mfx.FrameInfo.Height,
		ctx->out_fmt == 0 ? "NV12" : "YV12",
		ctx->num_yuv_frames,
		ctx->elapsed_time,
		(double)ctx->num_yuv_frames * CLOCKS_PER_SEC / (double)ctx->elapsed_time);

	//LOG(ctx->dec_info);
	return 0;
}


int dec_get_stream_info(int *width, int *height, float *frame_rate, intel_ctx *ctx)
{
	mfxFrameInfo *info = NULL;
	*width = 0;
	*height = 0;
	*frame_rate = 0.0;

	if (!ctx->is_param_inited) {
		return -1;
	}

	info = &ctx->dec_param->mfx.FrameInfo;
	*width = info->Width;//info->CropW;
	*height = info->Height;	// info->CropH;

	*frame_rate = (float)info->FrameRateExtN / (float)info->FrameRateExtD;
		
	return 0;

}


bool intel_dec_is_hw_support()
{
	mfxStatus sts = MFX_ERR_NONE;
	mfxVersion ver = { 1, 1 };
	mfxSession session;

	mfxIMPL impl = MFX_IMPL_HARDWARE_ANY;
	sts = MFXInit(impl, &ver, &session);
	if (MFX_ERR_NONE != sts) {
		impl = MFX_IMPL_HARDWARE;
		sts = MFXInit(impl, &ver, &session);
	}


	return (MFX_ERR_NONE == sts) ? true : false;
}

/****************************************************************************************
 *		output interfaces
 ***************************************************************************************/
 /**
 *  @desc:   create decode handle
 *
 *  @return: handle for use
 */
JMDLL_FUNC handle_inteldec jm_intel_dec_create_handle()
{
	return (handle_inteldec)intel_dec_create();
}

/**
 *   @desc:   Init decode before use
 *   @param: codec_type:  0 - H.264,  1 - H.265
 *   @param: out_fmt: output YUV frame format, 0 - NV12, 1 - YV12
 *   @param: handle: decode handle return by jm_intel_dec_create_handle()
 *
 *   @return: 0 - successful, else failed
 */
JMDLL_FUNC int jm_intel_dec_init(int codec_type, int out_fmt, handle_inteldec handle)
{
	return intel_dec_init(codec_type, out_fmt, (intel_ctx *)handle);
}

/**
 *   @desc:  destroy decode handle
 *   @param: handle: decode handle return by jm_intel_dec_create_handle()
 *
 *   @return: 0 - successful, else failed
 */
JMDLL_FUNC int jm_intel_dec_deinit(handle_inteldec handle)
{
	return intel_dec_deinit((intel_ctx *)handle);
}

/**
 *   @desc:   decode video frame
 *   @param: in_buf[in]: video frame data,
 *   @param: in_data_len[in]: data length
 *   @param: handle: decode handle fater init by jm_intel_dec_init()
 *
 *   @return: 0 - successful, else failed
 */
JMDLL_FUNC int jm_intel_dec_input_data(unsigned char *in_buf, int in_data_len, handle_inteldec handle)
{
	return intel_dec_put_input_data(in_buf, in_data_len, (intel_ctx *)handle);
}

/**
*   @desc:   get yuv frame, if no data output, will return failed.
*   @param: out_buf[out]: output YUV data buffer
*   @param: out_len[out]: we cab set out_buf = NULL, output yuv frame size.
*   @param: handle: decode handle fater init by jm_intel_dec_init()
*
*   @return: 0 - successful, else failed
*/
JMDLL_FUNC int jm_intel_dec_output_frame(unsigned char *out_buf, int *out_len, handle_inteldec handle)
{
	return intel_dec_output_yuv_frame(out_buf, out_len, (intel_ctx *)handle);
}

/*
*	@desc:	no more data input, set eof to decode, output decoder cached frame
*/
JMDLL_FUNC int jm_intel_dec_set_eof(int is_eof, handle_inteldec handle)
{
	return intel_dec_stop_input_data((intel_ctx *)handle);
}


/**
 *   @desc:  show decode informationt
 *   @param: handle: decode handle fater init by jm_intel_dec_init()
 *
 *   @return: return char *
 */
JMDLL_FUNC char *jm_intel_dec_info(handle_inteldec handle)
{
	intel_ctx *ctx = (intel_ctx *)handle;

	return ctx->dec_info;
}

JMDLL_FUNC int jm_intel_get_stream_info(int *width, int *height, float *frame_rate, handle_inteldec handle)
{
	return dec_get_stream_info(width, height, frame_rate, (intel_ctx *)handle);
}

JMDLL_FUNC bool jm_intel_dec_need_more_data(handle_inteldec handle)
{
	return intel_dec_need_more_data((intel_ctx *)handle);
}

JMDLL_FUNC int jm_intel_dec_free_buf_len(handle_inteldec handle)
{
	return intel_dec_get_input_free_buf_len((intel_ctx *)handle);
}

JMDLL_FUNC int jm_intel_dec_set_yuv_callback(void *user_data, HANDLE_YUV_CALLBACK callback, handle_inteldec handle)
{
	return intel_dec_set_yuv_callback(user_data, (YUV_CALLBACK)callback, (intel_ctx *)handle);
}

JMDLL_FUNC bool jm_intel_dec_is_exit(handle_inteldec handle)
{
	return intel_dec_is_exit((intel_ctx *)handle);
}

JMDLL_FUNC bool jm_intel_is_hw_support()
{
	return intel_dec_is_hw_support();
}
