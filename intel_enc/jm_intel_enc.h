/*****************************************************************************
 *  Copyright (C) 2014 - 2017, Justin Mo.
 *  All rights reserverd.
 *
 *  FileName:  jm_nv_enc.h
 *  Author:     Justin Mo(mojing1999@gmail.com)
 *  Date:        2017-07-13
 *  Version:    V0.01
 *  Desc:       This file implement This file implement Intel Media SDK Encode
 *****************************************************************************/
#ifndef _JM_INTEL_ENCODER_H_
#define _JM_INTEL_ENCODER_H_

#ifndef JMDLL_FUNC
#define JMDLL_FUNC		_declspec(dllexport)
#define JMDLL_API		__stdcall
#endif


typedef void * handle_intelenc;

typedef struct intel_enc_param
{
	/*
	 *	Intel Media SDK encode support codec:
	 *	0	- MFX_CODEC_AVC
	 *	1	- MFX_CODEC_HEVC
	 *	2	- MFX_CODEC_MPEG2
	 */
	int codec_id;
	/*
	 *	from 1 - 7 inclusive
	 *	1 - for good quality
	 *	7 - hight speed
	 */
	int target_usage;

	/*
	 *	YUV frame width and height
	 */
	int src_width;
	int src_height;

	int framerate_D;
	int framerate_N;

	int bitrate_kb;

	int is_hw;

}intel_enc_param;



JMDLL_FUNC handle_intelenc jm_intel_enc_create_handle();
JMDLL_FUNC intel_enc_param *jm_intel_enc_default_param(handle_intelenc handle);

JMDLL_FUNC int jm_intel_enc_init(intel_enc_param *in_param, handle_intelenc handle);
JMDLL_FUNC int jm_intel_enc_deinit(handle_intelenc handle);

JMDLL_FUNC int jm_intel_enc_encode_yuv_frame(unsigned char *yuv, int len, handle_intelenc handle);
JMDLL_FUNC int jm_intel_enc_encode_yuv_yuv420(unsigned char *yuv, int len, handle_intelenc handle);

JMDLL_FUNC int jm_intel_enc_output_bitstream(unsigned char *out_buf, int *out_len, int *is_keyframe, handle_intelenc handle);

JMDLL_FUNC int jm_intel_enc_set_eof(handle_intelenc handle);
JMDLL_FUNC bool jm_intel_enc_is_exit(handle_intelenc handle);

JMDLL_FUNC bool jm_intel_enc_more_data(handle_intelenc handle);

JMDLL_FUNC char *jm_intel_enc_info(handle_intelenc handle);

JMDLL_FUNC  char * jm_intel_enc_get_spspps(int *sps_len, int *pps_len, handle_intelenc handle);


#endif	//_JM_INTEL_ENCODER_H_
