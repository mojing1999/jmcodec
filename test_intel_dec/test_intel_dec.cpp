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
#include <conio.h>
#include <Windows.h>

#include "jm_intel_dec.h"


#pragma warning(disable : 4996)

#pragma comment(lib,"intel_dec.lib")

int save_yuv_frame(unsigned char *out_buf, int out_len, void *user_data)
{
	FILE *ofile = (FILE*)user_data;
	fwrite(out_buf, 1, out_len, ofile);

	return 0;
}

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
	handle_inteldec dec_handle;

	FILE *ifile = NULL, *ofile = NULL;
	unsigned char *in_buf = NULL;
	unsigned char *out_buf = NULL;
	int in_len = 0, out_len = 0;

	in_len = (10 << 20);	// 2 MB
	out_len = (20 << 20);	// 20 MB
	in_buf = new unsigned char[in_len];
	out_buf = new unsigned char[out_len];

	char *in_file = argv[1];
	//ifile = fopen("f:\\test\\v-1920x960.h264", "rb");
	//ifile = fopen("f:\\ZCAM02.track_1.264", "rb");
	ifile = fopen(in_file, "rb");
	//ifile = fopen("F:\\qqyun\\arm_demo_day_4K_4mbps.track_1.264", "rb");
	//C:\Users\justin\Downloads\Temp
	///ofile = fopen("f://justin_zcam2.264.yuv", "wb");
	ofile = fopen("F:\\test.264.yuv", "wb");


	bool is_hw_support = jm_intel_is_hw_support();

	dec_handle = jm_intel_dec_create_handle();

	jm_intel_dec_init(0, 0, dec_handle);

	//jm_intel_dec_set_yuv_callback(ofile, save_yuv_frame, dec_handle);

	int ret = 0;
	int read_len = 0, write_len = 0;
	int remaining_len = 0;
	int yuv_len = 0;
	int is_eof = 0;

	while (!jm_intel_dec_is_exit(dec_handle) && !is_user_exit()) {
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

		yuv_len = out_len;
		ret = jm_intel_dec_output_frame(out_buf, &yuv_len, dec_handle);
		if (0 == ret/* && yuv_len > 0*/) {
			//write_len = fwrite(out_buf, 1, yuv_len, ofile);
		}


	}


	printf(jm_intel_dec_info(dec_handle));

	jm_intel_dec_deinit(dec_handle);

	if (in_buf) delete[] in_buf;
	if (out_buf) delete[] out_buf;

	if (ifile) fclose(ifile);
	if (ofile) fclose(ofile);

	return 0;
}
