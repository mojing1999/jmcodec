/*****************************************************************************
*  Copyright (C) 2014 - 2017, Justin Mo.
*  All rights reserverd.
*
*  FileName:  	test_intel_dec.cpp
*  Author:    	Justin Mo(mojing1999@gmail.com)
*  Date:        2017-06-23
*  Version:     V0.01
*  Desc:        This sample code test intel_dec library
*****************************************************************************/
#include <stdio.h>
#include <Windows.h>
#include <time.h>
#include "jm_nv_dec.h"

#pragma warning(disable : 4996)

#pragma comment(lib,"nv_dec.lib")

int save_yuv_frame(unsigned char *out_buf, int out_len, void *user_data)
{
	FILE *ofile = (FILE*)user_data;
	fwrite(out_buf, 1, out_len, ofile);

	return 0;
}

// return offset
int find_nalu_prefix(unsigned char *buf_start, int buf_size, int *prefix_len)
{
	int offset = 0;

	unsigned char *buf = buf_start;
	*prefix_len = 0;

	while (buf_size >= offset + 3)
	{
		buf = buf_start + offset;
		if ((0x00 == buf[0]) &&
			(0x00 == buf[1]) &&
			(0x01 == buf[2]))
		{
			*prefix_len = 3;
			return offset;
		}
		else if ((0x00 == buf[0]) &&
			(0x00 == buf[1]) &&
			(0x00 == buf[2]) &&
			(0x01 == buf[3]))
		{
			*prefix_len = 4;
			return offset;
		}
		offset += 1;
	}


	return -1;

}

unsigned char *find_nalu(unsigned char *buf, int size, int *nalu_len)
{
	unsigned char *p = buf;
	int offset1 = 0, offset2 = 0;

	int prefix1 = 0;
	int prefix2 = 0;
	offset1 = find_nalu_prefix(p, size, &prefix1);

	// offset1 should be 0
	offset2 = find_nalu_prefix(p + offset1 + prefix1, size - prefix1 - offset1, &prefix2);

	if (-1 == offset2) {
		*nalu_len = 0;
		p = NULL;
	}
	else {
		*nalu_len = offset2 + prefix1;
		p = p + offset1;

	}

	return p;
}

int main(int argc, char **argv)
{
	handle_nvdec dec_handle;

	FILE *ifile = NULL, *ofile = NULL;
	unsigned char *in_buf = NULL;
	unsigned char *out_buf = NULL;
	int in_len = 0, out_len = 0;

	in_len = (10 << 20);	// 2 MB
	out_len = (20 << 20);	// 20 MB
	in_buf = new unsigned char[in_len];
	out_buf = new unsigned char[out_len];


	//ifile = fopen("f:\\justin_zcam2.264", "rb");
	ifile = fopen("f:\\4.track_2.264", "rb");
	ofile = fopen("f:\\4.track_2.264.yuv", "wb");
	//ofile = fopen("f:\\justin_zcam2.264.yuv", "wb");

	int ret = 0;
	int read_len = 0, write_len = 0;

	
	int offset = 0;
	int prefix = 0;
	unsigned char *buf = NULL;
	unsigned char *nalu = NULL;
	int nalu_len = 0;

	unsigned char spspps_buf[100] = { 0 };
	unsigned char *sps = NULL, pps = NULL;
	int spspps_len = 0;

	// find sps pps
	read_len = fread(in_buf, 1, in_len, ifile);

	// find the first prefix
	offset = find_nalu_prefix(in_buf, read_len, &prefix);



#if 0
	buf = find_nalu(in_buf + offset, read_len - offset, &nalu_len);
	if (buf[prefix] == 0x07) {
		// sps+
		memcpy(spspps_buf, buf, nalu_len);
		spspps_len += nalu_len;
	}

	// pps
	offset += nalu_len;
	buf = find_nalu(in_buf + offset, read_len - offset, &nalu_len);
	if (buf[prefix] == 0x08) {
		// sps+
		memcpy(spspps_buf+ spspps_len, buf, nalu_len);
		spspps_len += nalu_len;
	}

#endif

	dec_handle = jm_nvdec_create_handle();
	 
	jm_nvdec_init(0, 0, NULL, 0, dec_handle);

	//jm_intel_dec_set_yuv_callback(ofile, save_yuv_frame, dec_handle);

	int got_frame = 0;
	int buf_len = read_len - offset;
	int yuv_len = 0;

	int is_eof = 0;

	buf = in_buf + offset;

	long elapsed_time = clock();
	unsigned long frame_count = 0;

	long nalu_count = 0;

	while (!jm_nvdec_is_exit(dec_handle)) {
		if (0 == is_eof) {
			nalu = find_nalu(buf, buf_len, &nalu_len);

			if (!nalu) {
				// need more data
				//if (0 == is_eof) {
				memmove(in_buf, buf, buf_len);
				read_len = fread(in_buf + buf_len, 1, in_len - buf_len, ifile);

				buf_len += read_len;
				buf = in_buf;

				if (0 == read_len) {
					is_eof = 1;
					nalu = buf;
					nalu_len = buf_len;
					//jm_nvdec_set_eof(1, dec_handle);
				}
				//}
				continue;
			}

			buf += nalu_len;
			buf_len -= nalu_len;

		}

		if ((nalu)/* ||(1 == is_eof)*/) {
			// decode nalu
			nalu_count += 1;
			jm_nvdec_decode_frame(nalu, nalu_len, &got_frame, dec_handle);
			if (1 == got_frame) {
				yuv_len = out_len;
				jm_nvdec_output_frame(out_buf, &yuv_len, dec_handle);
				if (yuv_len > 0) {
					frame_count += 1;
					//write_len = fwrite(out_buf, 1, yuv_len, ofile);
				}
			}

			if (1 == is_eof) {
				// last nalu
				nalu = 0;
				nalu_len = 0;
				//jm_nvdec_set_eof(1, dec_handle);
			}
			//}

		}
		else if (1 == is_eof) {
			// decode cached frame
			nalu_count += 1;
			jm_nvdec_decode_frame(NULL, 0, &got_frame, dec_handle);
			if (1 == got_frame) {
				yuv_len = out_len;
				jm_nvdec_output_frame(out_buf, &yuv_len, dec_handle);
				if (yuv_len > 0) {
					frame_count += 1;
					//write_len = fwrite(out_buf, 1, yuv_len, ofile);
				}
			}

		}


	}

	printf(jm_nvdec_show_dec_info(dec_handle));

	printf("------nalu count = %d\n", nalu_count);
	

	//printf(jm_intel_dec_stream_info(dec_handle));

	jm_nvdec_deinit(dec_handle);

	if (in_buf) delete[] in_buf;
	if (out_buf) delete[] out_buf;

	if (ifile) fclose(ifile);
	if (ofile) fclose(ofile);

	return 0;
}
