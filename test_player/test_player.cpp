/***********************************************
*     SDL player
*     Author:     Justin Mo
*     Email:      mojing1999@gmail.com
*     Date:       2017.07.10
*     Version:   V0.01
***********************************************
*     test_player, test intel and nv decode
***********************************************
*/
#include <Windows.h>
#include <stdio.h>

#include "jm_intel_dec.h"
#include "jm_nv_dec.h"




#pragma comment(lib,"nv_dec.lib")
#pragma comment(lib,"intel_dec.lib")


extern "C" {
#include "libavformat\avformat.h"
#include "libavcodec\avcodec.h"
#include "libavutil\avutil.h"
#include "libavutil\time.h"
#include "libswscale\swscale.h"
#include "libavutil\imgutils.h"

#include "SDL2\SDL.h"


//#pragma comment(lib, "pthreadVC2.lib")
#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "swscale.lib")

#pragma comment(lib, "SDL2.lib")
#pragma comment(lib, "SDL2main.lib")

}


//#pragma comment(linker,"/subsystem:\"Windows\" /entry:\"mainCRTStartup\"")

//Refresh Event  
#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1)  

#define SFM_BREAK_EVENT  (SDL_USEREVENT + 2)  

int thread_exit = 0;
int thread_pause = 0;

int sfp_refresh_thread(void *opaque) {
	thread_exit = 0;
	thread_pause = 0;

	while (!thread_exit) {
		if (!thread_pause) {
			SDL_Event event;
			event.type = SFM_REFRESH_EVENT;
			SDL_PushEvent(&event);
		}
		SDL_Delay(25);
	}
	thread_exit = 0;
	thread_pause = 0;
	//Break  
	SDL_Event event;
	event.type = SFM_BREAK_EVENT;
	SDL_PushEvent(&event);

	return 0;
}


int main(int argc, char **argv)
{

	AVFormatContext *ic = NULL;
	AVCodecContext *vcc = NULL, *acc = NULL;
	AVStream *vs = NULL, *as = NULL;
	AVCodec *vc = NULL, *ac = NULL;
	AVFrame *frame = NULL, *frame_yuv = NULL;
	unsigned char *out_buf;
	int out_buf_len = 0;

	AVPacket packet, *pkt = &packet;
	struct SwsContext *sws = NULL;

	AVBitStreamFilterContext *bsfc = NULL;

	int vi = -1, ai = -1;
	int ret = 0;

	int got_frame = 0;

	// SDL
	int screen_w = 0, screen_h = 0;
	SDL_Window *screen = NULL;
	SDL_Renderer *sdl_renderer = NULL;
	SDL_Texture *sdl_texture = NULL;
	SDL_Rect sdl_rect;
	SDL_Thread *video_tid;
	SDL_Event event;


	FILE *fp_yuv = NULL;
	char *in_file = argv[1];//"f:\\test\\star_war_eve_3840x2160.264";
	FILE *ifile = NULL;

	// ========================================
	av_register_all();
	avformat_network_init();
	
	ic = avformat_alloc_context();

	ret = avformat_open_input(&ic, in_file, NULL, NULL);

	ret = avformat_find_stream_info(ic, NULL);

	for (int i = 0; i < ic->nb_streams; i++) {
		if (AVMEDIA_TYPE_VIDEO == ic->streams[i]->codec->codec_type) {
			vi = i;
			vs = ic->streams[i];
			break;
		}
#if 0
		else if (AVMEDIA_TYPE_AUDIO == ic->streams[i]->codec->codec_type) {
			ai = i;
			as = ic->streams[i];
		}
#endif
	}

	if (-1 == vi) {
		return -1;
	}


	vcc = vs->codec;
	vc = avcodec_find_decoder(vcc->codec_id);

	if (NULL == vc) {
		return -2;
	}

	avcodec_open2(vcc, vc, NULL);

	// =============================================
	frame = av_frame_alloc();
	frame_yuv = av_frame_alloc();
	out_buf_len = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, vcc->width, vcc->height, 1);
	out_buf = (unsigned char *)av_malloc(out_buf_len);
	av_image_fill_arrays(frame_yuv->data, frame_yuv->linesize, out_buf, AV_PIX_FMT_YUV420P,
		vcc->width, vcc->height, 1);

	//packet = (AVPacket *)av_malloc(sizeof(AVPacket));

	av_dump_format(ic, 0, in_file, 0);

	sws = sws_getContext(vcc->width, vcc->height, vcc->pix_fmt,
		vcc->width, vcc->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

	//
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		return -2;
	}

	screen_w = 800;// vcc->width;
	screen_h = 600;// vcc->height;
	screen = SDL_CreateWindow("Justin simple player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		screen_w, screen_h, SDL_WINDOW_RESIZABLE| SDL_WINDOW_MINIMIZED| SDL_WINDOW_MAXIMIZED|SDL_WINDOW_OPENGL);


	if (!screen) {
		return -3;
	}


	sdl_renderer = SDL_CreateRenderer(screen, -1, 0);

	sdl_texture = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_IYUV, 
		SDL_TEXTUREACCESS_STREAMING, vcc->width, vcc->height);
	

	sdl_rect.x = 0;
	sdl_rect.y = 0;
	sdl_rect.w = screen_w;
	sdl_rect.h = screen_h;


	int dec_type = 1;
	int codec_id = 0;
	int yuv_len = out_buf_len;

	void *dec_handle = NULL;

	codec_id = (vs->codec->codec_id == AV_CODEC_ID_H264) ? 0 : 1;

	if (1 == dec_type) {
		// nvidia decode
		dec_handle = jm_nvdec_create_handle();
		jm_nvdec_init(codec_id, 1, NULL, 0, dec_handle);
		dec_type = 1;
	}
	else if (2 == dec_type) {
		// intel decode
		dec_handle = jm_intel_dec_create_handle();
		jm_intel_dec_init(codec_id, 1, dec_handle);
		dec_type = 2;
	}
	else {
		dec_type = 3;	// FFmpeg decode
	}


	if (vcc->codec_id == AV_CODEC_ID_H264 || vcc->codec_id == AV_CODEC_ID_HEVC) {
		if (vcc->codec_id == AV_CODEC_ID_H264)
			bsfc = av_bitstream_filter_init("h264_mp4toannexb");
		else
			bsfc = av_bitstream_filter_init("hevc_mp4toannexb");
	}


	video_tid = SDL_CreateThread(sfp_refresh_thread, NULL, NULL);

	// ==============================================================

	for (;;) {
		// Wait
		SDL_WaitEvent(&event);
		if (event.type == SFM_REFRESH_EVENT) {
			while (1)
			{
				if (av_read_frame(ic, pkt) < 0)
					thread_exit = 1;

				if (pkt->stream_index == vi)
					break;
			}

			if (bsfc) {
				av_bitstream_filter_filter(bsfc, vcc, NULL, &pkt->data, &pkt->size, pkt->data, pkt->size, 0);
			}


			if (1 == dec_type) {
				// nv
				jm_nvdec_decode_frame(pkt->data, pkt->size, &got_frame, dec_handle);
				if (1 == got_frame) {
					yuv_len = out_buf_len;
					jm_nvdec_output_frame(out_buf, &yuv_len, dec_handle);
				}
			}
			else if (2 == dec_type) {
				// intel
				jm_intel_dec_input_data(pkt->data, pkt->size, dec_handle);
				ret = jm_intel_dec_output_frame(out_buf, &yuv_len, dec_handle);
				if (0 == ret) {
					got_frame = 1;
				}

			}
			else {
				// FFmpeg
				ret = avcodec_decode_video2(vcc, frame, &got_frame, pkt);
				if (ret < 0) {
					return -4;
				}
			}

			if (got_frame) {
				if (3 == dec_type) {
					sws_scale(sws, (const unsigned char * const *)frame->data, frame->linesize,
						0, vcc->height, frame_yuv->data, frame_yuv->linesize);
				}

				// SDL
				SDL_UpdateTexture(sdl_texture, NULL, frame_yuv->data[0], frame_yuv->linesize[0]);

				SDL_RenderClear(sdl_renderer);
				SDL_RenderCopy(sdl_renderer, sdl_texture, NULL, NULL);

				SDL_RenderPresent(sdl_renderer);

				//
			}

			av_free_packet(pkt);
		}
		else if (SDL_KEYDOWN == event.type) {
			// PAUSE
			if (SDLK_SPACE == event.key.keysym.sym )
				thread_pause = !thread_pause;
			else if (SDLK_ESCAPE == event.key.keysym.sym) {
				thread_exit = 1;
			}
		}
		else if (event.type == SDL_QUIT) {
			thread_exit = 1;
		}
		else if (event.type == SFM_BREAK_EVENT) {
			break;
		}
	}

	if (bsfc) {
		av_bitstream_filter_close(bsfc);
	}

	//sws_freeContext(sws);

	SDL_Quit();

	//
	if (1 == dec_type) {
		// nv
		printf(jm_nvdec_show_dec_info(dec_handle));
		jm_nvdec_deinit(dec_handle);

		//if (out_buf) delete[] out_buf;

	}
	else if (2 == dec_type) {
		printf(jm_intel_dec_info(dec_handle));
		jm_intel_dec_deinit(dec_handle);
		//if (out_buf) delete[] out_buf;
	}
	else {
		// FFmpeg 
		sws_freeContext(sws);
		avcodec_close(vcc);
	}

	av_free(frame);
	av_free(frame_yuv);
	//if (out_buf) delete[] out_buf;

	//avcodec_close(vcc);

	avformat_close_input(&ic);
	avformat_network_deinit();


	return 0;
}