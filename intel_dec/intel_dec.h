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
	int				reserve[16];
	mfxBitstream	*in_bs;
	HANDLE 			mutex_input;

	mfxSession		session;
	mfxIMPL 		impl;

	//MFXVideoDECODE	*dec;
	mfxVideoParam	*dec_param;
	int 			is_param_inited;

	// output
	mfxFrameSurface1	**surfaces;
	uint32_t			num_surf;
	mfxU8 				*surface_buffers;

	std::queue<mfxFrameSurface1 *> *out_surf_queue;
	HANDLE			mutex_yuv;
	HANDLE			event_yuv;

	HANDLE 			dec_thread;

	bool			is_more_data;
	bool 	        is_eof;		// no more decode data input, exit decode thread, and output SDK cached frames
	bool		    is_exit;

	int		        out_fmt;	// 0 - NV12, 1 - YV12

	int 	        hw_try;

	void	        *user_data;
	YUV_CALLBACK    yuv_callback;

	uint32_t        num_yuv_frames;
	uint32_t        elapsed_time;

	char	        dec_info[MAX_LEN_DEC_INFO];

}intel_ctx;


// 
intel_ctx *intel_dec_create();
int intel_dec_init(int codec_type, int out_fmt, intel_ctx *ctx);
int intel_dec_deinit(intel_ctx *ctx);

//
int intel_dec_put_input_data(const uint8_t *data, const int len, intel_ctx *ctx);
int intel_dec_output_yuv_frame(uint8_t *out_buf, int *out_len, intel_ctx *ctx);

//
int intel_dec_stop_input_data(intel_ctx *ctx);
bool intel_dec_is_exit(intel_ctx *ctx);
bool intel_dec_need_more_data(intel_ctx *ctx);
int intel_dec_get_input_free_buf_len(intel_ctx *ctx);

int intel_dec_set_yuv_callback(void *user_data, YUV_CALLBACK fn, intel_ctx *ctx);

// init 
mfxStatus dec_init_session(intel_ctx *ctx);
uint32_t dec_get_codec_id_by_type(int codec_type, intel_ctx *ctx);
char *dec_get_codec_id_string(intel_ctx *ctx);

int dec_init_input_bitstream(intel_ctx *ctx);
int dec_deinit_input_bitstream(intel_ctx *ctx);

mfxStatus dec_init_bitstream(int buf_size, mfxBitstream *pbs);
int dec_extend_bitstream(int new_size, mfxBitstream *pbs);

int dec_surfaces_init(intel_ctx *ctx);
int dec_surfaces_deinit(intel_ctx *ctx);

int dec_get_free_surface_index(intel_ctx *ctx);
void dec_surface_enquue_mark(mfxFrameSurface1 *surface);
void dec_surface_dequeue_mark(mfxFrameSurface1 *surface);

int dec_surface_push(mfxFrameSurface1 *surface, intel_ctx *ctx);
mfxFrameSurface1 *dec_surface_pop(intel_ctx *ctx);

//int dec_conver_surface_to_bistream(mfxFrameSurface1 *surface, intel_ctx *ctx);

int dec_plugin_load(uint32_t codec_id, intel_ctx *ctx);
int dec_plugin_unload(uint32_t codec_id, intel_ctx *ctx);



mfxStatus dec_handle_cached_frame(intel_ctx *ctx);
mfxStatus dec_decode_packet(intel_ctx *ctx);
mfxStatus dec_decode_header(intel_ctx *ctx);


int dec_create_decode_thread(intel_ctx *ctx);
int dec_wait_thread_exit(intel_ctx *ctx);

// TODO:
int intel_dec_show_info(intel_ctx *ctx);

int dec_get_stream_info(int *width, int *height, float *frame_rate, intel_ctx *ctx);

bool intel_dec_is_hw_support();


#endif	// _INTEL_DECODER_H_
