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

#include <time.h>


#pragma comment(lib,"libmfx.lib")
#pragma comment(lib,"legacy_stdio_definitions.lib")

#define MSDK_SLEEP(X)                   { Sleep(X); }
#define MAX_INPUT_BITSTREAM_SIZE		(5*1024*1024)
#define MAX_OUTPUT_YUV_COUNT			20	
#define MUTEXT_NAME_YUV					("intel_output_yuv")
#define MUTEXT_NAME_INPUT				("intel_input_bs")
#define EVENT_NAME_YUV					("intel_yuv_evt")
#define SYNC_WAITING_TIME				(60000)        	
#define INTEL_DEC_ASYNC_DEPTH			4	
#define MSDK_ALIGN32(X)                 (((mfxU32)((X)+31)) & (~ (mfxU32)31))

/*
 *	Decode thread
 *
 */
DWORD WINAPI decode_thread_proc(LPVOID param)
{
	intel_ctx *ctx = (intel_ctx *)param;

	do {

		if(!ctx->is_param_inited) {
			// decode header
			LOG("decode header...\n");
			dec_decode_header(ctx);
		}
		else {
			// decode packet
			LOG("decode packet...\n");
			dec_decode_packet(ctx);
		}
		Sleep(1);

	} while (!ctx->is_eof || 0 != dec_get_input_data_len(ctx));

	LOG("output decode cached frame...\n");
	// output decoder cached frame
	dec_handle_cached_frame(ctx);

	intel_dec_show_info(ctx);

	ctx->is_exit = true;
	return 0;
}


intel_ctx *intel_dec_create()
{
	intel_ctx *ctx = new intel_ctx;
	memset(ctx, 0x0, sizeof(intel_ctx));

	ctx->in_bs = new mfxBitstream;
	memset(ctx->in_bs, 0x0, sizeof(mfxBitstream));
	//
	ctx->session = new mfxSession;
	memset(ctx->session, 0x0, sizeof(mfxSession));

	ctx->param = new mfxVideoParam;
	memset(ctx->param, 0x0, sizeof(mfxVideoParam));
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

	// init bitstream
	// init mutex
	ctx->mutex_input = CreateMutexA(NULL, FALSE, MUTEXT_NAME_INPUT);
	sts = dec_init_bitstream(MAX_INPUT_BITSTREAM_SIZE, ctx->in_bs);
	// init session
	sts = dec_init_session(ctx);

	if(MFX_ERR_NONE != sts) {
		return -1;	// Error
	}


	// just support alloc memroy in system memory, donot need mfxFrameAllocator
	memset(ctx->param, 0x0, sizeof(mfxVideoParam));
	ctx->param->mfx.CodecId = dec_get_codec_id_by_type(codec_type, ctx);
	ctx->param->IOPattern = MFX_IOPATTERN_OUT_SYSTEM_MEMORY;



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
	ctx->is_eof = 1;
	dec_wait_thread_exit(ctx);
	// in_bs
	if (ctx->in_bs) {
		if (ctx->in_bs->Data)
			delete[] ctx->in_bs->Data;

		delete ctx->in_bs;
		ctx->in_bs = 0;
	}

	dec_deinit_yuv_output(ctx);

	// close decode
	MFXVideoDECODE_Close(*ctx->session);

	// session
	if (ctx->session) {
		MFXClose(*ctx->session);
		*ctx->session = 0;
	}

	// param
	if (ctx->param) {
		// release ExtParam

		delete ctx->param;
		ctx->param = 0;
	}


	return ret;
}

/*
 *	@desc: put data to decode buffer.
 */
int intel_dec_put_input_data(uint8_t *data, int len, intel_ctx *ctx)
{
	int ret = 0;
	mfxBitstream *pbs = NULL;

	// mutex lock
	ret = WaitForSingleObject(ctx->mutex_input, INFINITE);
	pbs = ctx->in_bs;

	if (0 == pbs->DataLength)
		pbs->DataOffset = 0;

	if((pbs->DataOffset > 0) && (pbs->DataLength > 0)) {
		memmove(pbs->Data, pbs->Data + pbs->DataOffset, pbs->DataLength);
		pbs->DataOffset = 0;
	}

	if(len > (pbs->MaxLength - pbs->DataLength)) {
		int new_size = 0;
		if(len > pbs->MaxLength) {
			new_size = pbs->MaxLength + len;
		}
		else {
			new_size = pbs->MaxLength * 2;
		}
		dec_extend_bitstream(new_size, pbs);
	}
#if 0
	else {
		// memmove
		memmove(pbs->Data, pbs->Data + pbs->DataOffset, pbs->DataLength);
		pbs->DataOffset = 0;
	}
#endif

	//
	memcpy(pbs->Data + pbs->DataOffset + pbs->DataLength, data, len);
	pbs->DataLength += len;

	ret = pbs->DataLength;

	ReleaseMutex(ctx->mutex_input);
	// return current buffer data length


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
	mfxFrameInfo *info = NULL;

	info = &ctx->param->mfx.FrameInfo;
	int width = info->CropW;
	int height = info->CropH;

	int data_len = 0;
	// output YUV420
	if (NULL == out_buf) {
		*out_len = width * height * 3 / 2;
		return 0;
	}

	*out_len = 0;

	mfxBitstream *bs = dec_pop_yuv_frame(ctx);
	if (NULL == bs) {
		return -1;
	}


	//
	memcpy(out_buf, bs->Data + bs->DataOffset, bs->DataLength);

	*out_len = bs->DataLength;

	// release bitstream
	dec_release_bitstream(bs);

	// event
	if (ctx->is_waiting) {
		//LOG("--- Before SetEvent()\n");
		SetEvent(ctx->event_yuv);
	}

	return 0;
}


int intel_dec_set_eof(int is_eof, intel_ctx *ctx)
{
	ctx->is_eof = is_eof;

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
	// if input data buffer Residual data < buffer size / 2(or 2 MB), decode request more input data,
	// make sure intel decode pipeline running uninterrupted
	if ((ctx->in_bs->DataLength < ctx->in_bs->MaxLength / 2) || (ctx->in_bs->DataLength < 2 * 1024 * 1024))
		return true;

	return false;
}

/*
 *	@desc: return input buffer length, that App can input more data
 */
int intel_dec_get_input_free_buf_len(intel_ctx *ctx)
{
	return (ctx->in_bs->MaxLength - ctx->in_bs->DataLength);
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
 *	@desc: intel media sdk init session
 */
mfxStatus dec_init_session(intel_ctx *ctx)
{
	mfxStatus sts = MFX_ERR_NONE;
	mfxVersion ver = {1, 1};

	mfxIMPL impl = MFX_IMPL_HARDWARE_ANY;
	sts = MFXInit(impl, &ver, ctx->session);	
	if(MFX_ERR_NONE != sts) {
		impl = MFX_IMPL_HARDWARE;
		sts = MFXInit(impl, &ver, ctx->session);
	}

	// try software 
	if(MFX_ERR_NONE != sts) {
		sts = MFXInit(MFX_IMPL_SOFTWARE, &ver, ctx->session);
	}

	if(MFX_ERR_NONE != sts) {
		return sts;
	}

	// query support impl and version
	sts = MFXQueryIMPL(*ctx->session, &ctx->impl);
	sts = MFXQueryVersion(*ctx->session, &ver);

	// show impl and version
	LOG("IMPL: %04X, Version: V%d.%d", ctx->impl, ver.Major, ver.Minor);


	// check current version support feature


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
	switch (ctx->param->mfx.CodecId)
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

/*	
 *	bitstream function
 */
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

int intel_dec_show_info(intel_ctx *ctx)
{
	uint32_t cur_time;
	cur_time = clock();
	ctx->elapsed_time = clock() - ctx->elapsed_time;
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
		ctx->param->mfx.FrameInfo.Width, ctx->param->mfx.FrameInfo.Height,
		ctx->out_fmt == 0 ? "NV12" : "YV12",
		ctx->num_yuv_frames,
		ctx->elapsed_time,
		(double)ctx->num_yuv_frames * CLOCKS_PER_SEC / (double)ctx->elapsed_time);

	//LOG(ctx->dec_info);
	return 0;
}


int dec_get_input_data_len(intel_ctx *ctx)
{
	return ctx->in_bs->DataLength;
}

int intel_dec_set_yuv_callback(void *user_data, YUV_CALLBACK fn, intel_ctx *ctx)
{
	ctx->user_data = user_data;
	ctx->yuv_callback = fn;

	return 0;
}

int dec_init_yuv_output(intel_ctx *ctx)
{
	int i = 0;

	ctx->num_yuv = (ctx->num_surf > MAX_OUTPUT_YUV_COUNT) ? ctx->num_surf : MAX_OUTPUT_YUV_COUNT;

	ctx->arr_yuv = new mfxBitstream *[ctx->num_yuv];

	for(i = 0; i < ctx->num_yuv; i++) {
		ctx->arr_yuv[i] = new mfxBitstream;
		memset(ctx->arr_yuv[i], 0x0, sizeof(mfxBitstream));
	}

	// init queue
	ctx->yuv_queue = new std::queue<mfxBitstream *>;

	// init mutex
	ctx->mutex_yuv = CreateMutexA(NULL, FALSE, MUTEXT_NAME_YUV);

	// init event
	ctx->event_yuv = CreateEventA(NULL, TRUE, FALSE, EVENT_NAME_YUV);

	LOG("intel decode init yuv output, yuv frame count = %d\n", ctx->num_yuv);
	return 0;
}

int dec_deinit_yuv_output(intel_ctx *ctx)
{
	int i = 0; 

	if (ctx->event_yuv) {
		CloseHandle(ctx->event_yuv);
	}

	if (ctx->mutex_yuv) {
		CloseHandle(ctx->mutex_yuv);
	}

	for (i = 0; i < ctx->num_yuv; i++) {
		delete[] ctx->arr_yuv[i]->Data;
		delete ctx->arr_yuv[i];
		ctx->arr_yuv[i] = 0;
	}

	delete[] ctx->arr_yuv;

	delete ctx->yuv_queue;

	return 0;
}

mfxBitstream *dec_get_free_yuv_bitstream(intel_ctx *ctx)
{
	for(int i = 0; i < ctx->num_yuv; i++) {
		if(0 == ctx->arr_yuv[i]->reserved2) {
			ctx->arr_yuv[i]->reserved2 = 1;	// use mfxBitstream.reserved2 for is_used
			return ctx->arr_yuv[i];
		}
	}

	// TODO: waiting for handle yuv output

	return NULL;
}

int dec_release_bitstream(mfxBitstream *pbs)
{
	pbs->DataLength = 0;
	pbs->DataOffset = 0;
	pbs->reserved2 = 0;		// use mfxBitstream.reserved2 for is_used

	return 0;
}

int dec_push_yuv_frame(mfxBitstream *bs, intel_ctx *ctx)
{
	WaitForSingleObject(ctx->mutex_yuv, INFINITE);

	ctx->yuv_queue->push(bs);

	ReleaseMutex(ctx->mutex_yuv);

	return 0;
}

mfxBitstream *dec_pop_yuv_frame(intel_ctx *ctx)
{
	mfxBitstream *bs = NULL;
	if (!ctx->yuv_queue || ctx->yuv_queue->empty()) {
		return NULL;
	}

	WaitForSingleObject(ctx->mutex_yuv, INFINITE);
	bs = ctx->yuv_queue->front();
	ctx->yuv_queue->pop();

	ReleaseMutex(ctx->mutex_yuv);

	return bs;
}


int dec_alloc_surfaces(intel_ctx *ctx)
{
	mfxStatus sts = MFX_ERR_NONE;

	//
	mfxFrameAllocRequest request;
	memset(&request, 0x0, sizeof(mfxFrameAllocRequest));
	sts = MFXVideoDECODE_QueryIOSurf(*ctx->session, ctx->param, &request);

	mfxU16 num_surfaces = request.NumFrameSuggested;
	ctx->num_surf = request.NumFrameSuggested;


	// allocate surfaces for decoder
	mfxU16 width = (mfxU16)MSDK_ALIGN32(request.Info.Width);
	mfxU16 height = (mfxU16)MSDK_ALIGN32(request.Info.Height);
	mfxU8  bits_pre_pixel = 12;	// NV12 format is a 12 bits per pixel frame info
	mfxU32 surface_size = width * height * bits_pre_pixel / 8;
	ctx->surface_buffers = (mfxU8 *) new mfxU8[num_surfaces * surface_size];

	ctx->surfaces = new mfxFrameSurface1 *[num_surfaces];
	// check pointer

	for (int i = 0; i < num_surfaces; i++) {
		ctx->surfaces[i] = new mfxFrameSurface1;
		memset(ctx->surfaces[i], 0x0, sizeof(mfxFrameSurface1));
		memcpy(&(ctx->surfaces[i]->Info), &(ctx->param->mfx.FrameInfo), sizeof(mfxFrameInfo));
		ctx->surfaces[i]->Data.Y = &ctx->surface_buffers[surface_size * i];
		ctx->surfaces[i]->Data.U = ctx->surfaces[i]->Data.Y + width * height;
		ctx->surfaces[i]->Data.V = ctx->surfaces[i]->Data.U + 1;
		ctx->surfaces[i]->Data.Pitch = width;
	}

	LOG("intel decode alloc surfaces[count=%d]\n", num_surfaces);

	return 0;
}


int dec_conver_surface_to_bistream(mfxFrameSurface1 *surface, intel_ctx *ctx)
{
	mfxBitstream * frame_bs = NULL;
	mfxFrameInfo *info = NULL;
	mfxFrameData *data = NULL;

	mfxU8	*data_buf = NULL;
	int i = 0;

	frame_bs = dec_get_free_yuv_bitstream(ctx);

	if (NULL == frame_bs) {
		// waiting for output yuv release yuv bitstream
		//LOG("--- Before ResetEvent()\n");
		ResetEvent(ctx->event_yuv);
		ctx->is_waiting = 1;
		WaitForSingleObject(ctx->event_yuv, INFINITE); 
		ctx->is_waiting = 0;
		frame_bs = dec_get_free_yuv_bitstream(ctx);
		//LOG("--- After WaitForSingleObject(), frame_bs = %p\n", frame_bs);

		if (NULL == frame_bs) {
			return -1;
		}
	}

	info = &surface->Info;
	data = &surface->Data;

	int y_len = info->CropW * info->CropH;
	int uv_len = y_len / 2;

	// check bitstream data buffer len
	if (frame_bs->MaxLength < y_len + uv_len) {
		dec_extend_bitstream(y_len + uv_len, frame_bs);
	}

	switch (info->FourCC)
	{
	case MFX_FOURCC_NV12:
	{
		int w = info->CropW, h = info->CropH;
		int crop_x = info->CropX, crop_y = info->CropY;
		int pitch = data->Pitch;

		mfxU8 *p = NULL;
		mfxU8 *dst = frame_bs->Data;

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
			dst = frame_bs->Data + y_len;
			for (i = 0; i < h; i++) {
				p = data->UV + (crop_y * pitch + crop_x) + i * pitch;
				memcpy(dst + i*w, p, w);
			}
			frame_bs->DataLength = (y_len + uv_len);
		}
		else {
			// YV12
			crop_x /= 2;
			crop_y /= 2;
			h /= 2;
			w /= 2;
			//dst = frame_bs->Data + y_len;
			mfxU8 *pu = frame_bs->Data + y_len;
			mfxU8 *pv = pu + uv_len / 2;
			int j = 0;
			for (i = 0; i < h; i++) {
				p = data->UV + (crop_y * pitch + crop_x) + i * pitch;
				for (j = 0; j < w; j++) {
					pu[i * w + j] = p[2 * j];
					pv[i * w + j] = p[2 * j + 1];
				}
			}
			frame_bs->DataLength = (y_len + uv_len);
		}
		break;
	}
	default:
		break;
	}

	if (ctx->yuv_callback) {
		ctx->yuv_callback(frame_bs->Data, frame_bs->DataLength, ctx->user_data);
		dec_release_bitstream(frame_bs);
	}
	else {
		// push to queue
		dec_push_yuv_frame(frame_bs, ctx);

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
		if(0 == ctx->surfaces[i]->Data.Locked)
			return i;
	}

	return MFX_ERR_NOT_FOUND;
}

mfxStatus dec_handle_cached_frame(intel_ctx *ctx)
{
	mfxStatus sts = MFX_ERR_NONE;
	
	mfxFrameSurface1* surface_out = NULL;
	mfxSyncPoint syncp;

	int index = 0;
	// retrieve the buffered decoded frames
	while (MFX_ERR_NONE <= sts || MFX_ERR_MORE_SURFACE == sts)	{
		if (MFX_WRN_DEVICE_BUSY == sts) {
			// Wait if devices is busy, then repeat the same call to DecodeFrameAsync
			MSDK_SLEEP(1);
		}

		index = dec_get_free_surface_index(ctx);
		if (MFX_ERR_NOT_FOUND == index) {
			sts = MFX_ERR_MEMORY_ALLOC;
		}

		// lock in_bs
		WaitForSingleObject(ctx->mutex_input, INFINITE);
		sts = MFXVideoDECODE_DecodeFrameAsync(*ctx->session, NULL, ctx->surfaces[index], &surface_out, &syncp);
		ReleaseMutex(ctx->mutex_input);
		// unlock in_bs

		// ignore warnings if output is available
		if (MFX_ERR_NONE < sts && syncp) {
			sts = MFX_ERR_NONE;
		}

		if (MFX_ERR_NONE == sts) {
			MFXVideoCORE_SyncOperation(*ctx->session, syncp, SYNC_WAITING_TIME);
			ctx->num_yuv_frames += 1;
			LOG("---------- YUV Frame count = %d\n", ctx->num_yuv_frames);
			// Output NV12
			dec_conver_surface_to_bistream(surface_out, ctx);
		}

	}


	return sts;
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
        	MSDK_SLEEP(5);
			//continue;
        }

        if (MFX_ERR_MORE_SURFACE == sts || MFX_ERR_NONE == sts)
        {
        	// get free surface index
        	idx = dec_get_free_surface_index(ctx);
			if (MFX_ERR_NOT_FOUND == idx) {
				MSDK_SLEEP(5);
				continue;
			}
			//	return MFX_ERR_MEMORY_ALLOC;
        }

        // lock in_bs
		WaitForSingleObject(ctx->mutex_input, INFINITE);
		sts = MFXVideoDECODE_DecodeFrameAsync(*ctx->session, ctx->in_bs, ctx->surfaces[idx], &surface_out, &syncp);
		ReleaseMutex(ctx->mutex_input);
		// unlock in_bs

        // ignore warnings if output is available
        if(MFX_ERR_NONE < sts && syncp) {
        	sts = MFX_ERR_NONE;
        }

        if(MFX_ERR_NONE == sts) {
        	MFXVideoCORE_SyncOperation(*ctx->session, syncp, SYNC_WAITING_TIME);
			ctx->num_yuv_frames += 1;
			LOG("---------- YUV Frame count = %d\n", ctx->num_yuv_frames);

			// Output NV12
			ret = dec_conver_surface_to_bistream(surface_out, ctx);
			if (-1 == ret) {
				LOG("lost frame......\n");
				MSDK_SLEEP(5);
			}
        }

		MSDK_SLEEP(1);

	}

	return sts;
}

mfxStatus dec_decode_header(intel_ctx *ctx)
{
	mfxStatus sts = MFX_ERR_NONE;

	// waiting for bitstreaming enough data for decode head
	sts = MFXVideoDECODE_DecodeHeader(*ctx->session, ctx->in_bs, ctx->param);
	if (MFX_WRN_PARTIAL_ACCELERATION == sts) sts = MFX_ERR_NONE;
	if (MFX_ERR_NONE > sts) 	return sts;

	// only support system memory
	ctx->param->IOPattern = MFX_IOPATTERN_OUT_SYSTEM_MEMORY;
	ctx->param->AsyncDepth = INTEL_DEC_ASYNC_DEPTH;



	sts = MFXVideoDECODE_Query(*ctx->session, ctx->param, ctx->param);

	//
	dec_alloc_surfaces(ctx);
	
	// init output yuv frame array
	dec_init_yuv_output(ctx);

	// init dec
	MFXVideoDECODE_Init(*ctx->session, ctx->param);

	// init start time
	ctx->elapsed_time = clock();

	ctx->is_param_inited = 1;

	return sts;
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
	return intel_dec_set_eof(is_eof, (intel_ctx *)handle);
}


/**
 *   @desc:  show decode informationt
 *   @param: handle: decode handle fater init by jm_intel_dec_init()
 *
 *   @return: return char *
 */
JMDLL_FUNC char *jm_intel_dec_stream_info(handle_inteldec handle)
{
	intel_ctx *ctx = (intel_ctx *)handle;

	return ctx->dec_info;
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