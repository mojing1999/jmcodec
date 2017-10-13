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
#ifndef _NV_ENCODER_H_
#define _NV_ENCODER_H_
#include <Windows.h>
#include <list>

#include "pisoft_nv_enc.h"

#include "dynlink_cuda.h"
#include "dynlink_builtin_types.h"

#include "nvEncodeAPI.h"


#define SET_VER(configStruct, type) {configStruct.version = type##_VER;}

#if defined(WIN32) || defined(_WIN32) || defined(WIN64) || defined(_WIN64)
typedef HMODULE CUDADRIVER;
#else
typedef void *CUDADRIVER;
#endif

#if defined(WIN64) || defined(_WIN64) 
#define NVENCODE_API_LIB ("nvEncodeAPI64.dll")
#elif defined(WIN32) || defined(_WIN32)
#define NVENCODE_API_LIB ("nvEncodeAPI.dll")
#elif defined(NV_UNIX)
#define NVENCODE_API_LIB ("libnvidia-encode.so")
#endif

#if defined (NV_WINDOWS)
#include "d3d9.h"
#define NVENCAPI __stdcall
#pragma warning(disable : 4996)
#elif defined (NV_UNIX)
#include <dlfcn.h>
#include <string.h>
#define NVENCAPI
#endif

#define DEFAULT_I_QFACTOR -0.8f
#define DEFAULT_B_QFACTOR 1.25f
#define DEFAULT_I_QOFFSET 0.f
#define DEFAULT_B_QOFFSET 1.25f



#define __cu(a) do { CUresult  ret; if ((ret = (a)) != CUDA_SUCCESS) { /*fprintf(stderr, "%s has returned CUDA error %d\n", #a, ret); */return NV_ENC_ERR_GENERIC;}} while(0)

typedef NVENCSTATUS(NVENCAPI *MYPROC)(NV_ENCODE_API_FUNCTION_LIST*);

enum
{
	NV_ENC_H264 = 0,
	NV_ENC_HEVC = 1,
};

enum NV_PIX_FMT
{
	FRAME_PIX_FMT_YUV420P = 0,
	FRAME_PIX_FMT_NV12,
	FRAME_PIX_FMT_YUV420_10BIT,
	FRAME_PIX_FMT_YUV444,
	FRAME_PIX_FMT_YUV444_10BIT,
	FRAME_PIX_FMT_ARGB,
	FRAME_PIX_FMT_ABGR,
};



typedef struct _nv_output_bs
{
    unsigned char *buf;
    int            buf_size;
    int            data_len;
    int            is_keyframe;
}nv_out_bitstream;

typedef struct _nv_frame_buf
{
	unsigned char *big_buf;
	int			big_buf_len;
	int			data_len;
	int			line_size[3];
	unsigned char *yuv_addr[3];
//    int         is_keyframe;
}nv_frame_buf;


#define MAX_NV_ENC_FRAME_NUM 10
typedef struct _nvenc_surface
{
	NV_ENC_INPUT_PTR			input_surface;		// frame buffer in video memory
	int 						reg_idx;
	NV_ENC_OUTPUT_PTR			output_surface;
	int							output_bs_size;

	HANDLE						event_output;
	int							is_wait_for_event;

	NV_ENC_MAP_INPUT_RESOURCE	in_map;
	//nv_frame_buf				*yuv_buf;	// alloc frame buffer in system memory

	CUdeviceptr					in_cuda_surf;	// is_external_alloc = 1, alloc cuda surface
	uint32_t					in_cuda_stride;

	//uint32_t pitch;

	int		lock_count;
}nvenc_surface;

typedef struct _nv_reg_frame
{
	CUdeviceptr		ptr;
	NV_ENC_REGISTERED_PTR	reg_ptr;
	int mapped;
}nv_reg_frame;

class CCudaAutoLock
{
private:
    CUcontext m_pCtx;
public:
    CCudaAutoLock(CUcontext pCtx) :m_pCtx(pCtx) { cuCtxPushCurrent(m_pCtx); };
    ~CCudaAutoLock()  { CUcontext cuLast = NULL; cuCtxPopCurrent(&cuLast); };
};


typedef struct _nvenc_ctx
{
	int								is_init;

	HINSTANCE						cuda_lib;	// "nvcuda.dll"
	CUcontext						cuda_ctx;
	int								cuda_dev_count;

	HINSTANCE						nvenc_lib;	// "nvEncodeAPI64.dll" or "nvEncodeAPI.dll"
	void *							nv_encoder;


	NV_ENC_INITIALIZE_PARAMS		nvenc_param;
	NV_ENC_CONFIG					nvenc_config;

	NV_ENCODE_API_FUNCTION_LIST		*encode_apis;
#if 0	
	CUmodule						cuda_module;
#endif
	GUID							codec_guid;

	//
	NV_ENC_BUFFER_FORMAT			format;
	//int								reg_idx;
	uint32_t width;
	uint32_t height;
	//uint32_t pitch;
	int			                    codec_id;	// NV_ENC_H264 or NV_ENC_HEVC


	// for conver yv12 to  nv12 temp UV ptr
	CUdeviceptr                     uv_tmp_ptr[2];
	CUmodule                        cu_module;
	CUfunction                      cuInterleaveUVFunction;
	CUfunction                      cuScaleNV12Function;


	// io buffer
	int is_external_alloc;
	//int data_pix_fmt;	// NV_PIX_FMT
	nvenc_surface					*arr_surfaces;
	int								nb_surfaces;
#if 1	// do not use, can not alloc buffer in system memory
    nv_frame_buf                    *arr_yuv_buf;
    int                             nb_yuv_buf;
#endif
	nv_reg_frame					*arr_reg_frames;
	int								nb_reg_frames;

	std::list<nvenc_surface *>		*list_output;
	std::list<nvenc_surface *>		*list_output_ready;
	HANDLE							mutex_ouput_ready;

	int								async_depth;
	int								async_mode;

    int								sps_len;
	int								pps_len;
    unsigned char *spspps_buf;

}nvenc_ctx;


nvenc_ctx *nvenc_ctx_create();	//
int nvenc_encode_init(nv_enc_param *in_param, nvenc_ctx *ctx);	//
int nvenc_encode_deinit(nvenc_ctx *ctx);	//
int nvenc_encode_enc_frame(/*nv_out_bitstream *out_pkt, */const unsigned char *in_yuv_buf, const int yuv_len, int *got_packet, nvenc_ctx *ctx);	//



NVENCSTATUS nvenc_cuda_init(nvenc_ctx *ctx);
int nvenc_loading_libraries(nvenc_ctx *ctx);
int nvenc_encoder_close(nvenc_ctx *ctx);
int nvenc_param_init(nv_enc_param *in_param, nvenc_ctx *ctx);
NVENCSTATUS check_validate_encode_guid(GUID in_guid, nvenc_ctx *ctx);
GUID nvenc_get_preset_guid(int in_preset);
NVENCSTATUS nvenc_init(nvenc_ctx *ctx);

#if 0	// do not use
int nvenc_create_yuv_buffer(int nb_buf, nvenc_ctx *ctx);
int nvenc_release_yuv_buffer(nvenc_ctx *ctx);
#endif 

int nvenc_setup_surfaces(nvenc_ctx *ctx);
int nvenc_alloc_surfaces(int idx, nvenc_ctx *ctx);
nvenc_surface *nvenc_get_free_frame(nvenc_ctx *ctx);
void nvenc_codec_specific_pic_params(NV_ENC_PIC_PARAMS *params, nvenc_ctx *ctx);
//int nvenc_get_free_reg_frame(nvenc_ctx *ctx);
int nvenc_register_frame(int idx, nvenc_ctx *ctx);
int nvenc_get_register_frame_index(nvenc_surface *nv_frame, nvenc_ctx *ctx);
int nvenc_upload_frame(const unsigned char *in_yuv_buf, const int yuv_len, nvenc_surface *nv_frame, nvenc_ctx *ctx);
int nvenc_get_spspps(nvenc_ctx *ctx);
int nvenc_push_surface_to_list(nvenc_surface *in_surface, nvenc_ctx *ctx);
int nvenc_check_ready_bitstream(nvenc_ctx *ctx);
int nvenc_is_output_ready(nvenc_ctx *ctx);
nvenc_surface *nvenc_pop_list_output_ready_item(nvenc_ctx *ctx);
int nvenc_get_output_bitstream(unsigned char *out_buf, int *out_data_len, int *is_keyframe, nvenc_ctx *ctx);

int nvenc_create_reg_frames(int nb_frames, nvenc_ctx *ctx);
int nvenc_release_reg_frames(nvenc_ctx *ctx);



// debug info
int nvenc_debug_show_support_input_format(nvenc_ctx *ctx);
int nvenc_cuda_memory_alloc_host(void **buf, int buf_len,nvenc_ctx *ctx);
int nvenc_cuda_memory_release_host(void *buf, nvenc_ctx *ctx);







#endif  //_NV_ENCODER_H_
