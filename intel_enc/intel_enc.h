/*****************************************************************************
 *  Copyright (C) 2014 - 2017, Justin Mo.
 *  All rights reserverd.
 *
 *  FileName:  	intel_enc.h
 *  Author:     Justin Mo(mojing1999@gmail.com)
 *  Date:       2017-07-13
 *  Version:    V0.01
 *  Desc:       This file implement Intel Media SDK Encode
 *****************************************************************************/
#ifndef _INTEL_ENCODER_H_
#define _INTEL_ENCODER_H_
#include <Windows.h>
#include <stdint.h>
#include <queue>

#include "mfxvideo.h"
#include "mfxvideo++.h"
#include "mfxplugin.h"

#include "jm_intel_enc.h"


#ifdef _DEBUG
    #define LOG( ... )				printf( __VA_ARGS__ )
#else
    #define LOG( ... )              
#endif


#define MSDK_CHECK_RESULT(P, X, ERR)    {if ((X) > (P)) {return ERR;}}
#define MSDK_IGNORE_MFX_STS(P, X)       {if ((X) == (P)) {P = MFX_ERR_NONE;}}
#define MSDK_ALIGN32(X)                 (((mfxU32)((X)+31)) & (~ (mfxU32)31))
#define MSDK_ALIGN16(value)             (((value + 15) >> 4) << 4)
#define MSDK_SLEEP(X)                   { Sleep(X); }


#define INTEL_ENC_DEFAULT_WIDTH (1920)
#define INTEL_ENC_DEFAULT_HEIGHT (1080)
#define INTEL_ENC_DEFAULT_BITRATE (2000)
#define MAX_OUTPUT_BS_COUNT	(30)
#define DEFAULT_OUTPUT_BS_SIZE (3*1024*1024)
#define MUTEX_NAME_OUTPUT_BS	("mutex_intel_enc_out_bs")
#define MUTEX_NAME_INPUT_YUV	("mutex_intel_enc_in_yuv")
#define NUM_SURFACE_ADDITION	(30)

#define MAX_TIME_ENC_EYNCP	(60000)	// Synchronize. Wait until encoded frame is ready
#define INDEX_OF_RESERVED_IN_USE 0


#define MAX_LEN_ENC_INFO (1024)

#define ENC_SUPPORT_CODEC	(3)

typedef struct intel_enc_ctx
{
	mfxSession		session;
	mfxIMPL 		impl;

	intel_enc_param *in_param;

	mfxVideoParam	*enc_param;
	uint16_t		enc_width;
	uint16_t		enc_height;

	mfxFrameSurface1	**surfaces;
	uint16_t			num_surfaces;
	uint8_t				*yuv_big_buf;
	std::queue<mfxFrameSurface1 *> *in_surf_queue;
	HANDLE				mutex_yuv;


	mfxBitstream		**arr_bs;
	int					num_bs;
	std::queue<mfxBitstream *> *out_bs_queue;
	HANDLE				mutex_bs;


	uint32_t			len_sps;
	uint32_t			len_pps;
	uint8_t				*sps_pps_buffer;

	int					error_code;

	HANDLE				enc_thread;

	bool				thread_exit;
	bool				is_stop_input;	// 



	// info
	uint32_t			num_frames;
	uint32_t			elapsed_time;
	char				enc_info[MAX_LEN_ENC_INFO];

}intel_enc_ctx;

/**
 *   @desc:  create intel encode context
 *
 *   @return: context pointer - successful, NULL failed
 */
intel_enc_ctx *intel_enc_create();

/**
 *   @desc:  intel_enc_init before use
 *   @param: in_param:  user define encode param
 *   @param: ctx: encode context return by intel_enc_create()
 *
 *   @return: 0 - successful, else failed
 */
int intel_enc_init(intel_enc_param *in_param, intel_enc_ctx *ctx);

/**
 *	@desc:	intel_enc_deinit
 *  @param: ctx: encode context return by intel_enc_create()
 */
int intel_enc_deinit(intel_enc_ctx *ctx);

void intel_enc_show_info(intel_enc_ctx *ctx);

/**
 *	@desc:	return default encode param to user setting.
 *  @param: ctx: encode context return by intel_enc_create()
 *  @return: intel_enc_param pointer, user custom the param,
 *			then use with input param in API intel_enc_init()
 */
intel_enc_param *intel_enc_get_param(intel_enc_ctx *ctx);
int enc_set_param(intel_enc_param *in_param, intel_enc_ctx *ctx);
int intel_enc_default_param(intel_enc_ctx *ctx);

/**
 *	@desc:	check whether need input yuv frame to encode
 *	
 *	@return: true - user can input yuv frame to encode, else cannot input yuv frame
 */
bool intel_enc_need_more_data(intel_enc_ctx *ctx);
/**
 *	@desc:	input yuv frame for encode.
 *	@param:	yuv : YUV frame data
 *	@param:	len : YUV frame data len
 */
int intel_enc_input_yuv_frame(uint8_t *yuv, int len, intel_enc_ctx *ctx);
int intel_enc_input_yuv_yuv420(uint8_t *yuv, int len, intel_enc_ctx *ctx);

/**
 *	@desc:	user can get bitstream from encode
 *	@param:	out_buf[in] : output buffer
 *	@param:	out_len[in][out] : input out_buf size, output bitstream length
 *
 *	@return:	0 - successful, < 0 else failed.
 */
int intel_enc_output_bitstream(uint8_t *out_buf, int *out_len, int *is_keyframe, intel_enc_ctx *ctx);

/**
 *	@desc:	user call this api if no more yuv frame input. after handle finish encode buffer yuv data
 *			encode thread will exit.
 */
int intel_enc_stop_yuv_input(intel_enc_ctx *ctx);

/**
 *	@desc:	check encode thread is running
 */
int intel_enc_is_exit(intel_enc_ctx *ctx);


mfxStatus enc_session_init(intel_enc_ctx *ctx);
uint32_t enc_get_mfx_codec_id(int in_codec, intel_enc_ctx *ctx);

mfxStatus enc_param_init(intel_enc_ctx *ctx);

mfxStatus enc_get_spspps(intel_enc_ctx *ctx);

/**************************************************************
 *	encode surfaces
 *************************************************************/
mfxStatus enc_surfaces_init(intel_enc_ctx *ctx);
void enc_surfaces_deinit(intel_enc_ctx *ctx);
int enc_get_free_surface_index(intel_enc_ctx *ctx);
void enc_surface_enquue_mark(mfxFrameSurface1 *surface);
void enc_surface_dequeue_mark(mfxFrameSurface1 *surface);

int enc_push_yuv_surface(mfxFrameSurface1 *surf, intel_enc_ctx *ctx);
mfxFrameSurface1 *enc_pop_yuv_surface(intel_enc_ctx *ctx);


/**************************************************************
*	output bitstream
*************************************************************/
int enc_output_bs_init(intel_enc_ctx *ctx);
int enc_output_bs_deinit(intel_enc_ctx *ctx);
int enc_init_bitstream(int buf_size, mfxBitstream *pbs);
int enc_extend_bitstream(int new_size, mfxBitstream *pbs);
int enc_get_free_bitstream_index(intel_enc_ctx *ctx);
int enc_release_bitstream(mfxBitstream *pbs);
int enc_push_bitstream(mfxBitstream *bs, intel_enc_ctx *ctx);
mfxBitstream *enc_pop_bitstream(intel_enc_ctx *ctx);

/**************************************************************
*	encode function
*************************************************************/
mfxStatus enc_encode_frame(intel_enc_ctx *ctx);

/**************************************************************
*	encode thread
*************************************************************/
int enc_create_thread(intel_enc_ctx *ctx);
int enc_thread_exit(intel_enc_ctx *ctx);

char *enc_get_codec_id_string(intel_enc_ctx *ctx);

int enc_plugin_load(intel_enc_ctx *ctx);
int enc_plugin_unload(intel_enc_ctx *ctx);

#endif	// _INTEL_ENCODER_H_
