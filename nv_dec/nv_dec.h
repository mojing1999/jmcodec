/*****************************************************************************
 *  Copyright (C) 2014 - 2017, Justin Mo.
 *  All rights reserverd.
 *
 *  FileName:  nv_dec.h
 *  Author:     Justin Mo(mojing1999@gmail.com)
 *  Date:        2017-05-08
 *  Version:    V0.01
 *  Desc:       This file implement NVIDIA VIDEO DECODER(NVDEC) INTERFACE
 *****************************************************************************/
#ifndef _NV_DECODER_H_
#define _NV_DECODER_H_
#include <Windows.h>
#include <stdint.h>
#include <queue>

#if 1
#include "dynlink_cuda.h"
#include "dynlink_nvcuvid.h"
#include "dynlink_cuviddec.h"
#include "dynlink_builtin_types.h"
#include "cudaModuleMgr.h"
#endif


#ifdef _DEBUG
    #define LOG( ... )				printf( __VA_ARGS__ )
#else
    #define LOG( ... )              
#endif

#define NVDEC_MAX_FRAMES 10
#define MAX_LEN_DEC_INFO	(1024)
 //#define __cu(a) do { CUresult  ret; if ((ret = (a)) != CUDA_SUCCESS) { /*fprintf(stderr, "%s has returned CUDA error %d\n", #a, ret); */return -1;}} while(0)


enum NV_CODEC_TYPE {
	NV_CODEC_AVC = 0,	// H.264
	NV_CODEC_HEVC,	// H.265
	NV_CODEC_MJPEG,
	NV_CODEC_MPEG4,
	NV_CODEC_MPEG2,
	NV_CODEC_VP8,
	NV_CODEC_VP9,
	NV_CODEC_VC1,
};



typedef struct cuvid_parsed_frame
{
	CUVIDPARSERDISPINFO		disp_info;
	int	second_field;
	int is_deinterlacing;
	int	is_free;
}cuvid_frame;

typedef struct _nv_frame_buf
{
	unsigned char *big_buf;
	int			big_buf_len;
	int			data_len;
	int			pitch;
	int			width;
	int			height;
}nv_frame_buf;


typedef struct nvdec_ctx
{
	// cuda
	HINSTANCE	cuda_lib;	// "nvcuda.dll"
	CUdevice	cuda_dev;
	CUcontext	cuda_ctx;
	int			cuda_dev_count;

	// cuvid
	CUvideodecoder			cudecoder;
	CUvideoparser			cuparser;


	CUVIDPARSERPARAMS		cuparse_info;
	CUVIDEOFORMATEX			cuparse_ext;
	CUVIDDECODECREATEINFO	dec_create_info;

	CUVIDSOURCEDATAPACKET	cuvid_pkt;

	// packet list
	int			queue_max_size;
	int			*is_frame_in_use;
	std::queue<CUVIDPARSERDISPINFO *>	*frame_queue;
	HANDLE		mutex_for_queue;


	int out_fmt;	// 0: NV12 , 1: ARGB
	CUvideoctxlock	ctx_lock;

#if 0
	// NV nv12toargb function
	CUmoduleManager   *cuda_module_mgr;
	CUmodule           cumod_nv12toargb;
	CUfunction         pfn_nv12toargb;
	CUfunction         pfn_pass_thru;

	CUstream			readback_sid; 
	CUstream			kernel_sid;
#endif
	//
	//int		is_flush;
	bool		is_eof;
	bool		is_exit;
	uint32_t	num_frames;
	uint32_t	elapsed_time;
	char		dec_info[MAX_LEN_DEC_INFO];

	//int		is_readback;

	// output
	nv_frame_buf *out_frame;
	int num_out_frames;
	int cur_out_frame_idx;
	nv_frame_buf *cur_out_frame;
	int is_first_frame;


}nvdec_ctx;


nvdec_ctx *nvdec_ctx_create();
int nvdec_decode_init(int codec_type, int out_fmt, char *extra_data, int len, nvdec_ctx *ctx);
int nvdec_decode_deinit(nvdec_ctx *ctx);

int nvdec_frame_queue_init(nvdec_ctx *ctx);
int nvdec_frame_queue_deinit(nvdec_ctx *ctx);
int nvdec_frame_queue_push(CUVIDPARSERDISPINFO *disp_info, nvdec_ctx *ctx);
CUVIDPARSERDISPINFO *nvdec_frame_queue_pop(nvdec_ctx *ctx);
int nvdec_frame_item_release(CUVIDPARSERDISPINFO *info, nvdec_ctx *ctx);

int nvdec_cuda_init(nvdec_ctx *ctx);
bool nvdec_cuda_hw_support();

int nvdec_decode_packet(uint8_t *in_buf, int in_data_len, nvdec_ctx *ctx);
int nvdec_decode_output_frame(int *got_frame, nvdec_ctx *ctx);
int nvdec_decode_frame(uint8_t *in_buf, int in_data_len, int *got_frame, nvdec_ctx *ctx);
nv_frame_buf *nvdec_get_free_frame(nvdec_ctx *ctx);

int nvdec_create_parser(int codec_type, char *extra_data, int len, nvdec_ctx *ctx);
int nvdec_create_decoder(CUVIDEOFORMAT* format, nvdec_ctx *ctx);

int nvdec_out_frame_init(uint32_t pitch, uint32_t width, uint32_t height, nvdec_ctx *ctx);
int nvdec_out_frame_deinit(nvdec_ctx *ctx);

void nvdec_set_eof(bool is_eof, nvdec_ctx *ctx);
bool nvdec_is_exit(nvdec_ctx *ctx);

char *nvdec_get_codec_id_string(nvdec_ctx *ctx);
void nvdec_show_info(nvdec_ctx *ctx);






#endif	// _NV_DECODER_H_
