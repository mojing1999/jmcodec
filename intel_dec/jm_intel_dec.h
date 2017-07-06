/*****************************************************************************
 *  Copyright (C) 2014 - 2017, Justin Mo.
 *  All rights reserverd.
 *
 *  FileName:  jm_nv_dec.h
 *  Author:     Justin Mo(mojing1999@gmail.com)
 *  Date:        2017-05-08
 *  Version:    V0.01
 *  Desc:       This file implement This file implement Intel Media SDK Decode
 *****************************************************************************/
#ifndef _JM_INTEL_DECODER_H_
#define _JM_INTEL_DECODER_H_

#ifndef PISOFTDLL_FUNC
#define JMDLL_FUNC		_declspec(dllexport)
#define JMDLL_API		__stdcall
#endif


typedef void * handle_inteldec;
typedef int (*HANDLE_YUV_CALLBACK)(unsigned char *out_buf, int out_len, void *user_data);


/** 
 *  @desc:   create decode handle
 *   
 *  @return: handle for use
 */
JMDLL_FUNC handle_inteldec jm_intel_dec_create_handle();	//

/** 
 *   @desc:   Init decode before use
 *   @param: codec_type:  0 - H.264,  1 - H.265
 *   @param: out_fmt: output YUV frame format, 0 - NV12, 1 - YV12
 *   @param: handle: decode handle return by jm_intel_dec_create_handle()
 *
 *   @return: 0 - successful, else failed
 */
JMDLL_FUNC int jm_intel_dec_init(int codec_type, int out_fmt, handle_inteldec handle);	//

/** 
 *   @desc:  destroy decode handle
 *   @param: handle: decode handle return by jm_intel_dec_create_handle()
 *
 *   @return: 0 - successful, else failed
 */
JMDLL_FUNC int jm_intel_dec_deinit(handle_inteldec handle);	//

/**
 *   @desc:  set yuv output callback, if callback non null, yuv will output to callback, API jm_intel_dec_output_frame will  no yuv output.
 *	 @param: user_data: callback param.
 *	 @param: callback: callback function.
 *   @param: handle: decode handle return by jm_intel_dec_create_handle()
 *
 *   @return: 0 - successful, else failed
 */
JMDLL_FUNC int jm_intel_dec_set_yuv_callback(void *user_data, HANDLE_YUV_CALLBACK callback, handle_inteldec handle);

/** 
 *   @desc:   decode video frame
 *   @param: in_buf[in]: video frame data, 
 *   @param: in_data_len[in]: data length
 *   @param: handle: decode handle fater init by jm_intel_dec_init()
 *
 *   @return: 0 - successful, else failed
 */
JMDLL_FUNC int jm_intel_dec_input_data(unsigned char *in_buf, int in_data_len, handle_inteldec handle);

/** 
 *   @desc:  get yuv frame, if no data output, will return failed. If user has been set YUV callback function, this API wil no yuv output.
 *   @param: out_buf[out]: output YUV data buffer
 *   @param: out_len[out]: we cab set out_buf = NULL, output yuv frame size.
 *   @param: handle: decode handle fater init by jm_intel_dec_init()
 *
 *   @return: 0 - successful, else failed
 */
JMDLL_FUNC int jm_intel_dec_output_frame(unsigned char *out_buf, int *out_len, handle_inteldec handle);


/*
 *	@desc:	no more data input, set eof to decode, output decoder cached frame
 */
JMDLL_FUNC int jm_intel_dec_set_eof(int is_eof, handle_inteldec handle);


/** 
 *   @desc:  show decode informationt
 *   @param: handle: decode handle fater init by jm_intel_dec_init()
 *
 *   @return: return char * 
 */
JMDLL_FUNC char *jm_intel_dec_info(handle_inteldec handle);

JMDLL_FUNC int jm_intel_get_stream_info(int *width, int *height, float *frame_rate, handle_inteldec handle);

/**
 *   @desc:  check whether decode need more input data.
 *   @param: handle: decode handle fater init by jm_intel_dec_init()
 *
 *   @return: if need more input data, return true, else return false.
 */
JMDLL_FUNC bool jm_intel_dec_need_more_data(handle_inteldec handle);

/**
 *   @desc:  get decode input data buffer free length, app can not input data greater than return length
 *   @param: handle: decode handle fater init by jm_intel_dec_init()
 *
 *   @return: return free buffer length
 */
JMDLL_FUNC int jm_intel_dec_free_buf_len(handle_inteldec handle);

/**
 *   @desc:  after app set eof to decode, decode will output the cached frame, then exit
 *   @param: handle: decode handle fater init by jm_intel_dec_init()
 *
 *   @return: return true if decode exit, else return false.
 */
JMDLL_FUNC bool jm_intel_dec_is_exit(handle_inteldec handle);


JMDLL_FUNC bool jm_intel_is_hw_support();

#endif	//_JM_INTEL_DECODER_H_
