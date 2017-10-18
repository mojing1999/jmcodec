/*****************************************************************************
*  Copyright (C) 2014 - 2017, Justin Mo.
*  All rights reserverd.
*
*  FileName:  jmnv_enc.h
*  Author:     Justin Mo(mojing1999@gmail.com)
*  Date:        2017-10-13
*  Version:    V0.01
*  Desc:       This file implement NVIDIA VIDEO ENCODE(NVENC) INTERFACE
*****************************************************************************/
#ifndef _JMNV_ENC_H_
#define _JMNV_ENC_H_


#ifndef JMDLL_FUNC
#define JMDLL_FUNC		_declspec(dllexport)
#define JMDLL_API		__stdcall
#endif

typedef void * handle_nvenc;


typedef struct _nv_enc_param
{
	int			codec_id;	// NV_ENC_H264 or NV_ENC_HEVC
							//NV_ENC_BUFFER_FORMAT	in_fmt;
	int			in_fmt;
	/*
	NV_ENC_PRESET_DEFAULT_GUID
	NV_ENC_PRESET_HP_GUID
	NV_ENC_PRESET_HQ_GUID
	NV_ENC_PRESET_LOW_LATENCY_HQ_GUID
	NV_ENC_PRESET_LOW_LATENCY_HP_GUID
	*/
	//GUID        preset_GUID;
	int			preset;

	int			src_width;
	int			src_height;//

	int			dst_width;
	int			dst_height;

	int			fps;
	int			bitrate_kb;
	int			gop_len;
	int			num_bframe;

	int         is_external_alloc;

	int			qp;

}nv_enc_param;

JMDLL_FUNC handle_nvenc jm_nvenc_create_handle();	//
JMDLL_FUNC int jm_nvenc_init(nv_enc_param *in_param, handle_nvenc handle);	//
JMDLL_FUNC int jm_nvenc_deinit(handle_nvenc handle);	//
JMDLL_FUNC int jm_nvenc_enc_frame(const unsigned char *in_yuv_buf, const int yuv_len, int *got_packet, handle_nvenc handle);	//
JMDLL_FUNC int jm_nvenc_get_bitstream(unsigned char *out_buf, int *out_data_len, int *is_keyframe, handle_nvenc handle);

JMDLL_FUNC int jm_nvenc_get_spspps_len(int *sps_len, int *pps_len,handle_nvenc handle);	//
JMDLL_FUNC int jm_nvenc_get_spspps(unsigned char *out_buf, handle_nvenc handle);	//



JMDLL_FUNC int jm_nvenc_memory_alloc_host(void **buf, int buf_len, handle_nvenc handle);
JMDLL_FUNC int jm_nvenc_memory_release_host(void *buf, handle_nvenc handle);








#endif  //_JMNV_ENC_H_
