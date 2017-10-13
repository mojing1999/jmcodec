/******************************************************************************
*           PISOFTTECH CORPORATION PROPRIETARY INFORMATION
*  This software is supplied under the terms of a license agreement or
*  nondisclosure agreement with Pisofttech Corporation and may not be copied
*  or disclosed except in accordance with the terms of that agreement.
*    Copyright (c) 2010-2016 Pisofttech Corporation. All Rights Reserved.
*
*
*   This file is part of pimedia .
*
*	Author:				Justin Mo
*	Email:				justin.mo@pisofttech.com
*	Data:				2016-12-07
*	Version:			V3.14
*
******************************************************************************/
#ifndef _PISOFT_NV_ENC_H_
#define _PISOFT_NV_ENC_H_


#ifndef PISOFTDLL_FUNC
#define PISOFTDLL_FUNC		_declspec(dllexport)
#define PISOFTDLL_API		__stdcall
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

PISOFTDLL_FUNC handle_nvenc pisoft_nvenc_create_handle();	//
PISOFTDLL_FUNC int pisoft_nvenc_init(nv_enc_param *in_param, handle_nvenc handle);	//
PISOFTDLL_FUNC int pisoft_nvenc_deinit(handle_nvenc handle);	//
PISOFTDLL_FUNC int pisoft_nvenc_enc_frame(const unsigned char *in_yuv_buf, const int yuv_len, int *got_packet, handle_nvenc handle);	//
PISOFTDLL_FUNC int pisoft_nvenc_get_bitstream(unsigned char *out_buf, int *out_data_len, int *is_keyframe, handle_nvenc handle);

PISOFTDLL_FUNC int pisoft_nvenc_get_spspps_len(int *sps_len, int *pps_len,handle_nvenc handle);	//
PISOFTDLL_FUNC int pisoft_nvenc_get_spspps(unsigned char *out_buf, handle_nvenc handle);	//



PISOFTDLL_FUNC int pisoft_nvenc_memory_alloc_host(void **buf, int buf_len, handle_nvenc handle);
PISOFTDLL_FUNC int pisoft_nvenc_memory_release_host(void *buf, handle_nvenc handle);








#endif  //_PISOFT_NV_ENC_H_
