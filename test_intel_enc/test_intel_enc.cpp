/*****************************************************************************
*  Copyright (C) 2014 - 2017, Justin Mo.
*  All rights reserverd.
*
*  FileName:  	test_intel_enc.cpp
*  Author:    	Justin Mo(mojing1999@gmail.com)
*  Date:       2017-07-18
*  Version:    V0.01
*  Desc:       This sample code test intel_enc library
*****************************************************************************/
#include <stdio.h>
#include <conio.h>
#include <Windows.h>

#include "jm_intel_enc.h"


#pragma warning(disable : 4996)

#pragma comment(lib,"intel_enc.lib")


bool is_user_exit()
{
	if (_kbhit()) {
		if ('q' == getch())
			return true;
	}

	return false;
}

int main(int argc, char **argv)
{
	handle_intelenc enc_handle;

	FILE *ifile = NULL, *ofile = NULL;
	unsigned char *in_buf = NULL;
	unsigned char *out_buf = NULL;
	int in_len = 0, out_len = 0;

	in_len = (10 << 20);	// 2 MB
	out_len = (10 << 20);	// 20 MB
	in_buf = new unsigned char[in_len];
	out_buf = new unsigned char[out_len];

	char *in_file = argv[1];
	ifile = fopen("f:\\test.264.yuv", "rb");
	//ifile = fopen(in_file, "rb");
	//ifile = fopen("F:\\qqyun\\arm_demo_day_4K_4mbps.track_1.264", "rb");
	//C:\Users\justin\Downloads\Temp
	///ofile = fopen("f://justin_zcam2.264.yuv", "wb");
	ofile = fopen("F:\\test_intel_enc.h264", "wb");

	int width = 1920;
	int height = 1080;

	//bool is_hw_support = jm_intel_is_hw_support();

	enc_handle = jm_intel_enc_create_handle();

	intel_enc_param *in_param = jm_intel_enc_default_param(enc_handle);
	in_param->codec_id =  0;// 0;
	in_param->src_width = width;
	in_param->src_height = height;
	in_param->target_usage = 4;
	in_param->framerate_D = 1;
	in_param->framerate_N = 30;
	in_param->bitrate_kb = 2000;
	in_param->is_hw = 1;

	jm_intel_enc_init(in_param, enc_handle);


	int ret = 0;
	int read_len = 0, write_len = 0;

	int yuv_len = width * height * 3 / 2;
	int bs_len = 0;
	int is_eof = 0;
	int is_keyframe = 0;

	while (!jm_intel_enc_is_exit(enc_handle)) {
		if (0 == is_eof) {
			if (jm_intel_enc_more_data(enc_handle)) {
				read_len = fread(in_buf, 1, yuv_len, ifile);

				if (0 == read_len) {
					// no more data
					is_eof = 1;
					jm_intel_enc_set_eof(enc_handle);
				}
				else {
					jm_intel_enc_encode_yuv_frame(in_buf, read_len, enc_handle);
				}
			}
		}

		bs_len = out_len;
		ret = jm_intel_enc_output_bitstream(out_buf, &bs_len, &is_keyframe, enc_handle);
		if (0 == ret/* && yuv_len > 0*/) {
			write_len = fwrite(out_buf, 1, bs_len, ofile);
		}

		if (is_user_exit()) {
			is_eof = 1;
			jm_intel_enc_set_eof(enc_handle);

		}
	}

	printf(jm_intel_enc_info(enc_handle));

	jm_intel_enc_deinit(enc_handle);

	if (in_buf) delete[] in_buf;
	if (out_buf) delete[] out_buf;

	if (ifile) fclose(ifile);
	if (ofile) fclose(ofile);

	return 0;
}

