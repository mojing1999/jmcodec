/*****************************************************************************
 *  Copyright (C) 2014 - 2017, Justin Mo.
 *  All rights reserverd.
 *
 *  FileName:  jm_nv_dec.h
 *  Author:     Justin Mo(mojing1999@gmail.com)
 *  Date:        2017-05-08
 *  Version:    V0.01
 *  Desc:       This file implement NVIDIA VIDEO DECODER(NVDEC) INTERFACE
 *****************************************************************************/
#ifndef _JM_NV_DECODER_H_
#define _JM_NV_DECODER_H_

#ifndef PISOFTDLL_FUNC
#define JMTDLL_FUNC		_declspec(dllexport)
#define JMDLL_API		__stdcall
#endif


typedef void * handle_nvdec;

/** 
 *  @desc:   create decode handle
 *   
 *  @return: handle for use
 */
JMTDLL_FUNC handle_nvdec jm_nvdec_create_handle();	//

/** 
 *   @desc:   Init decode before use
 *   @param: codec_type:  0 - H.264,  1 - H.265
 *   @param: extra_data: sps or pps buffer, = NULL is OK
 *   @param: len: extra_data length
 *   @param: handle: decode handle return by jm_nvdec_create_handle()
 *
 *   @return: 0 - successful, else failed
 */
JMTDLL_FUNC int jm_nvdec_init(int codec_type, char *extra_data, int len, handle_nvdec handle);	//

/** 
 *   @desc:   destroy decode handle
 *   @param: handle: decode handle return by jm_nvdec_create_handle()
 *
 *   @return: 0 - successful, else failed
 */
JMTDLL_FUNC int jm_nvdec_deinit(handle_nvdec handle);	//

/** 
 *   @desc:   decode video frame
 *   @param: in_buf[in]: video frame data
 *   @param: in_data_len[in]: data length
 *   @param: got_frame[out]: 1 - if decode output YUV frame, else 0.
 *   @param: handle: decode handle fater init by jm_nvdec_init()
 *
 *   @return: 0 - successful, else failed
 */
JMTDLL_FUNC int jm_nvdec_decode_frame(unsigned char *in_buf, int in_data_len, int *got_frame, handle_nvdec handle);

/** 
 *   @desc:   if got_frame get 1 from jm_nvdec_decode_frame(), call this function to get YUV data
 *   @param: out_fmt[in]: YUV format type, 0 - NV12, 1 - YUV420(YV12)
 *   @param: out_buf[out]: output YUV data buffer
 *   @param: out_len[in][out]: [in] out_buf buffer size, if < YUV420 frame size, will return error(-1), [out]YUV420 frame size.
 *   @param: handle: decode handle fater init by jm_nvdec_init()
 *
 *   @return: 0 - successful, else failed
 */
JMTDLL_FUNC int jm_nvdec_output_frame(int out_fmt, unsigned char *out_buf, int *out_len, handle_nvdec handle);


/** 
 *   @desc:   get YUV frame resolution (width x height), this function will called after successfully decode first frame.
 *   @param: disp_width[out]: width
 *   @param: disp_height[out]: height
 *   @param: handle: decode handle fater init by jm_nvdec_init()
 *
 *   @return: 0 - successful, else failed
 */
JMTDLL_FUNC int jm_nvdec_stream_info(int *disp_width, int *disp_height, handle_nvdec handle);


#endif	//_JM_NV_DECODER_H_
