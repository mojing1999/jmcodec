/*****************************************************************************
 *  Copyright (C) 2014 - 2017, Justin Mo.
 *  All rights reserverd.
 *
 *  FileName:  	intel_dec.h
 *  Author:     Justin Mo(mojing1999@gmail.com)
 *  Date:       2017-06-10
 *  Version:    V0.01
 *  Desc:       This file implement Intel Media SDK Decode
 *****************************************************************************/
#ifndef _INTEL_DECODER_H_
#define _INTEL_DECODER_H_
#include <Windows.h>
#include <stdint.h>
#include <queue>

#include "mfxvideo.h"
#include "mfxvideo++.h"
#include "mfxplugin.h"


#ifdef _DEBUG
    #define LOG( ... )				printf( __VA_ARGS__ )
#else
    #define LOG( ... )              
#endif


#define MAX_LEN_DEC_INFO	(1024)
typedef int (*YUV_CALLBACK)(unsigned char *out_buf, int out_len, void *user_data);

enum  
{
	CODEC_TYPE_AVC,
	CODEC_TYPE_HEVC,
	CODEC_TYPE_MPEG2,
	CODEC_TYPE_VC1,
	CODEC_TYPE_CAPTURE,
	CODEC_TYPE_VP9
};

typedef struct _intel_ctx
{
	//
	mfxBitstream	*in_bs;
	HANDLE 			mutex_input;

	mfxSession		*session;
	mfxIMPL 		impl;

	//MFXVideoDECODE	*dec;
	mfxVideoParam	*param;
	int 			is_param_inited;

	// output
	mfxFrameSurface1	**surfaces;
	uint32_t			num_surf;
	mfxU8 				*surface_buffers;

	//
	mfxBitstream	**arr_yuv;
	int 			num_yuv;
	std::queue<mfxBitstream *> *yuv_queue;
	HANDLE			mutex_yuv;
	HANDLE			event_yuv;
	bool			is_waiting;
	//

	HANDLE 			dec_thread;

	int 	is_eof;		// no more decode data input, exit decode thread, and output SDK cached frames

	int		out_fmt;	// 0 - NV12, 1 - YV12

	int 	hw_try;

	uint32_t num_yuv_frames;
	uint32_t elapsed_time;

	bool		is_exit;

	void	*user_data;
	YUV_CALLBACK yuv_callback;

	char	dec_info[MAX_LEN_DEC_INFO];

}intel_ctx;

// 
intel_ctx *intel_dec_create();
int intel_dec_init(int codec_type, int out_fmt, intel_ctx *ctx);
int intel_dec_deinit(intel_ctx *ctx);

//
int intel_dec_put_input_data(uint8_t *data, int len, intel_ctx *ctx);
int intel_dec_output_yuv_frame(uint8_t *out_buf, int *out_len, intel_ctx *ctx);

//
bool intel_dec_is_exit(intel_ctx *ctx);
bool intel_dec_need_more_data(intel_ctx *ctx);
int intel_dec_get_input_free_buf_len(intel_ctx *ctx);
int intel_dec_set_eof(int is_eof, intel_ctx *ctx);

int intel_dec_set_yuv_callback(void *user_data, YUV_CALLBACK fn, intel_ctx *ctx);

int intel_dec_show_info(intel_ctx *ctx);

int dec_get_input_data_len(intel_ctx *ctx);

// init 
mfxStatus dec_init_session(intel_ctx *ctx);
uint32_t dec_get_codec_id_by_type(int codec_type, intel_ctx *ctx);
char *dec_get_codec_id_string(intel_ctx *ctx);

int dec_create_decode_thread(intel_ctx *ctx);
int dec_wait_thread_exit(intel_ctx *ctx);


// bitstream
mfxStatus dec_init_bitstream(int buf_size, mfxBitstream *pbs);
int dec_extend_bitstream(int new_size, mfxBitstream *pbs);

int dec_init_yuv_output(intel_ctx *ctx);
int dec_deinit_yuv_output(intel_ctx *ctx);

mfxBitstream *dec_get_free_yuv_bitstream(intel_ctx *ctx);
int dec_release_bitstream(mfxBitstream *pbs);

int dec_push_yuv_frame(mfxBitstream *bs, intel_ctx *ctx);
mfxBitstream *dec_pop_yuv_frame(intel_ctx *ctx);


int dec_alloc_surfaces(intel_ctx *ctx);
int dec_get_free_surface_index(intel_ctx *ctx);
int dec_conver_surface_to_bistream(mfxFrameSurface1 *surface, intel_ctx *ctx);

mfxStatus dec_handle_cached_frame(intel_ctx *ctx);
mfxStatus dec_decode_packet(intel_ctx *ctx);
mfxStatus dec_decode_header(intel_ctx *ctx);

int dec_get_stream_info(int *width, int *height, float frame_rate, intel_ctx *ctx);

bool intel_dec_is_hw_support();
#endif	// _INTEL_DECODER_H_
