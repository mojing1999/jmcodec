/*****************************************************************************
*  Copyright (C) 2014 - 2017, Justin Mo.
*  All rights reserverd.
*
*  FileName:  	test_intel_dec.cpp
*  Author:    	Justin Mo(mojing1999@gmail.com)
*  Date:       2017-05-08
*  Version:    V0.01
*  Desc:       This sample code test intel_dec library
*****************************************************************************/
#include <stdio.h>
#include "jm_intel_dec.h"
#pragma warning(disable : 4996)

#pragma comment(lib,"intel_dec.lib")

int save_yuv_frame(unsigned char *out_buf, int out_len, void *user_data)
{
	FILE *ofile = (FILE*)user_data;
	fwrite(out_buf, 1, out_len, ofile);

	return 0;
}

int main(int argc, char **argv)
{
	handle_inteldec dec_handle;

	FILE *ifile = NULL, *ofile = NULL;
	unsigned char *in_buf = NULL;
	unsigned char *out_buf = NULL;
	int in_len = 0, out_len = 0;

	in_len = (10 << 20);	// 2 MB
	out_len = (10 << 20);	// 10 MB
	in_buf = new unsigned char[in_len];
	out_buf = new unsigned char[out_len];


	ifile = fopen("f:\\justin_zcam2.264", "rb");
	//C:\Users\justin\Downloads\Temp
	///ofile = fopen("f://justin_zcam2.264.yuv", "wb");
	ofile = fopen("f:\\justin_zcam2.264.yuv", "wb");



	dec_handle = jm_intel_dec_create_handle();

	jm_intel_dec_init(0, 0, dec_handle);

	//jm_intel_dec_set_yuv_callback(ofile, save_yuv_frame, dec_handle);

	int ret = 0;
	int read_len = 0, write_len = 0;
	int remaining_len = 0;
	int yuv_len = 0;
	int is_eof = 0;

	while (!jm_intel_dec_is_exit(dec_handle)) {
		if (jm_intel_dec_need_more_data(dec_handle) && 0 == is_eof) {
			remaining_len = jm_intel_dec_free_buf_len(dec_handle);
			read_len = fread(in_buf, 1, remaining_len, ifile);
			if (0 == read_len) {
				// no more data
				is_eof = 1;
				jm_intel_dec_set_eof(1, dec_handle);
			}
			else {
				jm_intel_dec_input_data(in_buf, read_len, dec_handle);
			}
		}

		ret = jm_intel_dec_output_frame(out_buf, &yuv_len, dec_handle);
		if (0 == ret/* && yuv_len > 0*/) {
			write_len = fwrite(out_buf, 1, yuv_len, ofile);
		}


	}


	printf(jm_intel_dec_stream_info(dec_handle));

	jm_intel_dec_deinit(dec_handle);

	if (in_buf) delete[] in_buf;
	if (out_buf) delete[] out_buf;

	if (ifile) fclose(ifile);
	if (ofile) fclose(ofile);

	return 0;
}
