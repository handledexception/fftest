#include <stdio.h>
#include <string.h>

#include <SDL.h>
#undef main
extern "C" { 
#include "libavcodec\avcodec.h"		
#include "libavformat\avformat.h"
#include "libswscale\swscale.h"
	
}

#include "utils.h"

AVFormatContext *fmt_ctx = NULL;
AVDictionary *opt_dict;

SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
SDL_Surface *sdlframe = NULL;
SDL_Texture *texture = NULL;
//FILE *f;

int main(int argc, char **argv)
{
	// init FFMPEG and SDL
	int num_frames;

	// SDL stuff
	SDL_Init(SDL_INIT_EVERYTHING);
	
	// FFMPEG stuff
	av_register_all();
	av_log_set_level(AV_LOG_VERBOSE);

	// open video file
	if (avformat_open_input(&fmt_ctx, argv[1], NULL, &opt_dict) != 0) {
		av_log(NULL, AV_LOG_ERROR, "Cannot open input file\n");
		return -1;
	}

	// find stream info from the video file
	if (avformat_find_stream_info(fmt_ctx, &opt_dict) < 0) {
		av_log(NULL, AV_LOG_ERROR, "Could not read stream info\n");
		return -1;
	}

	uint8_t i = 0;
	int video_stream = -1;
	AVCodecContext *codec_ctx_orig = NULL;
	AVCodecContext *codec_ctx = NULL;
	AVCodec *vcodec = NULL;
	AVFrame *vframe = NULL;
	AVFrame *vframe_display = NULL;
	uint8_t *buffer = NULL;
	int num_bytes = 0;

	// displays info about the streams contained in the video
	av_dump_format(fmt_ctx, 0, argv[1], 0);
		
	printf("\nOverall bitrate: %0.f Kbps\n", btokbs(fmt_ctx->bit_rate));
		
	// list info about streams
	for (i = 0; i < fmt_ctx->nb_streams; i++) {
		//printf("Stream : %d - %s\nbitrate %.0f Kbps\n", i, fmt_ctx->streams[i]->codec->codec_descriptor->long_name, btokbs(fmt_ctx->streams[i]->codec->bit_rate));
		if (fmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			video_stream = i;
			break;
		}
	}
	if (video_stream == -1) {
		av_log(NULL, AV_LOG_ERROR, "No video streams found in file!\n");
	}

	// get pointer to the codec context for the video stream
	codec_ctx_orig = avcodec_alloc_context3(vcodec);
	codec_ctx_orig = fmt_ctx->streams[video_stream]->codec;
	// find suitable decoder for this stream
	vcodec = avcodec_find_decoder(codec_ctx_orig->codec_id); 
	if (vcodec == NULL) {
		av_log(NULL, AV_LOG_ERROR, "Unsupported codec!\n");
		return -1;
	}
	printf("Found codec to decode video stream: %s\n", vcodec->long_name);

	// copy context, as we must not use the AVCodecContext from the video stream directly!
	// http://dranger.com/ffmpeg/tutorial01.html
	codec_ctx = avcodec_alloc_context3(vcodec);		
	if (avcodec_copy_context(codec_ctx, codec_ctx_orig) != 0) {
		av_log(NULL, AV_LOG_ERROR, "Failed to copy codec context!\n");
		return -1;
	}
	if (avcodec_open2(codec_ctx, vcodec, NULL) < 0) {
		av_log(NULL, AV_LOG_ERROR, "Could not open codec!\n");
		return -1;
	}

	vframe = av_frame_alloc();
	vframe_display = av_frame_alloc();
	num_bytes = avpicture_get_size(PIX_FMT_RGBA, codec_ctx->width, codec_ctx->height);
	buffer = (uint8_t *)av_malloc(num_bytes * sizeof(uint8_t));
	avpicture_fill((AVPicture *)vframe_display, buffer, PIX_FMT_RGBA, codec_ctx->width, codec_ctx->height);

	int decoded_frame_count = 0;
	int frame_finished = 0;
	AVPacket *pkt = NULL;
	SDL_Rect src_rect;
	SwsContext *sws_ctx = NULL;
	pkt = (AVPacket *)av_malloc(sizeof(AVPacket));

	sws_ctx = sws_getContext(codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt, codec_ctx->width, codec_ctx->height, PIX_FMT_RGBA, SWS_LANCZOS, NULL, NULL, NULL);

	// todo: SDL error checking
	window = SDL_CreateWindow("fftest", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, codec_ctx->width, codec_ctx->height, SDL_WINDOW_SHOWN);
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888 , SDL_TEXTUREACCESS_STREAMING, codec_ctx->width, codec_ctx->height);
	src_rect = { 0, 0, codec_ctx->width, codec_ctx->height };
	printf("Created SDL Window @ %dx%d\n", codec_ctx->width, codec_ctx->height);

	int first_time, frame_time = 0;

	while (av_read_frame(fmt_ctx, pkt) >= 0) {
		first_time = SDL_GetTicks();

		if (pkt->stream_index == video_stream) {
			avcodec_decode_video2(codec_ctx, vframe, &frame_finished, pkt);
		}

		if (frame_finished) {			
			//printf("pict_type: %d\n", vframe->pict_type);

			sws_scale(sws_ctx, (const uint8_t * const *)vframe->data, vframe->linesize, 0, codec_ctx->height, vframe_display->data, vframe_display->linesize);		
			SDL_UpdateTexture(texture, NULL, vframe_display->data[0], vframe_display->linesize[0]);

			//sdlframe = SDL_CreateRGBSurfaceFrom(vframe_display->data, vframe->width, vframe->height, 32, (uint8_t)vframe_display->linesize, 0x00, 0x00, 0x00, 0x00);
			//texture = SDL_CreateTextureFromSurface(renderer, sdlframe);
			//SDL_FreeSurface(sdlframe);
			
			SDL_RenderClear(renderer);
			SDL_RenderCopy(renderer, texture, NULL, &src_rect);
			SDL_RenderPresent(renderer);
			frame_time = SDL_GetTicks() - first_time;
			//printf("%d\n", frame_time);
			SDL_Delay(32 - frame_time);
		}
	}

	printf("Decoded %d of %d frames\n", decoded_frame_count, fmt_ctx->streams[video_stream]->nb_frames);

	SDL_Quit();	
	return 0;
}