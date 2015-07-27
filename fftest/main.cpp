#include <stdio.h>
#include <string.h>

#include <SDL.h>
//#undef main

//#define MYDEBUG

extern "C" { 
//#include "libavcodec\avcodec.h"		
#include "libavformat\avformat.h"
#include "libswscale\swscale.h"
	
}

#include "utils.h"

AVFormatContext *fmt_ctx = NULL;
//AVFormatContext **fmt_ctx;
AVDictionary *option_dict;

SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
SDL_Surface *sdlframe = NULL;

class VideoState {

public:
	VideoState();
	~VideoState();

	bool OpenVideoInput(const char *uri);
	bool GetVideoStream(AVFormatContext *fmtCtx);
	bool PrepareVideoFrame();
	int DecodeVideoFrame();
	void Render();

	int m_VidWidth;
	int m_VidHeight;

private:		
	AVFormatContext *m_FormatCtx;

	AVCodecContext *m_VideoCodecCtx;
	
	AVCodec *m_VideoCodec;

	AVFrame *m_VideoFrame;
	AVFrame *m_VideoFrameDisplay;

	AVPacket *m_VideoPacket;

	SwsContext *m_ScalerCtx;

	SDL_Texture *m_VideoTexture;
	SDL_Rect m_SrcRect;

	int m_FirstVideoStream;

	char *m_SourceUri;
};

VideoState::VideoState()
{
	m_FormatCtx = NULL;

	m_VideoCodecCtx = NULL;
	
	m_VideoCodec = NULL;

	m_VideoFrame = NULL;
	m_VideoFrameDisplay = NULL;

	m_VideoPacket = (AVPacket *)av_malloc(sizeof(AVPacket));

	m_ScalerCtx = NULL;

	m_VideoTexture = NULL;

	m_FirstVideoStream = 0;

	m_VidWidth = m_VidHeight = 0;
};

VideoState::~VideoState()
{ 
	av_free(m_FormatCtx);
	m_FormatCtx = NULL;

	av_free(m_VideoCodecCtx);
	m_VideoCodecCtx = NULL;

	av_free(m_VideoCodec);
	m_VideoCodec = NULL;

	av_free(m_VideoFrame);
	m_VideoFrame = NULL;

	av_free(m_VideoFrameDisplay);
	m_VideoFrameDisplay = NULL;
		
	av_free(m_VideoPacket);	
	m_VideoPacket = NULL;

	av_free(m_ScalerCtx);
	m_ScalerCtx = NULL;
		
	SDL_DestroyTexture(m_VideoTexture);
	m_VideoTexture = NULL;
};

bool VideoState::GetVideoStream(AVFormatContext *fmtCtx)
{
	unsigned int streamIndex = 0;
	AVCodecContext *tmpVideoCodecCtx = NULL;

	if (&fmtCtx == NULL) {
		printf("Invalid AVFormatContext!\n");
		return false;
	}

	// list info about streams
	for (streamIndex = 0; streamIndex < fmtCtx->nb_streams; streamIndex++) {
		//printf("Stream : %d - %s\nbitrate %.0f Kbps\n", streamIndex, fmtCtx->streams[streamIndex]->codec->codec_descriptor->long_name, btokbs(fmtCtx->streams[streamIndex]->codec->bit_rate));

		// find the first video stream in the input format
		if (fmtCtx->streams[streamIndex]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			m_FirstVideoStream = streamIndex;
			break;
		}
	}
	if (m_FirstVideoStream == -1) {
		av_log(NULL, AV_LOG_ERROR, "No video streams found in file!\n");
		return false;
	}

	// allocate context for video codec
	tmpVideoCodecCtx = avcodec_alloc_context3(m_VideoCodec);
	// get pointer to the video codec for the first video stream
	tmpVideoCodecCtx = fmtCtx->streams[m_FirstVideoStream]->codec;

	// find suitable video codec to associate with codec context
	m_VideoCodec = avcodec_find_decoder(tmpVideoCodecCtx->codec_id);
	if (m_VideoCodec == NULL) {
		av_log(NULL, AV_LOG_ERROR, "Video codec not supported by FFMPEG!\n");
		return false;
	}
	printf("Found codec to decode video stream: %s\n", m_VideoCodec->long_name);

	// copy context, as we must not use the AVCodecContext from the video stream directly!
	// http://dranger.com/ffmpeg/tutorial01.html
	m_VideoCodecCtx = avcodec_alloc_context3(m_VideoCodec);

	if (avcodec_copy_context(m_VideoCodecCtx, tmpVideoCodecCtx) != 0) {
		av_log(NULL, AV_LOG_ERROR, "avcodec_copy_context failed!\n");
		return false;
	}

	// open video codec for the video stream's codec context
	if (avcodec_open2(m_VideoCodecCtx, m_VideoCodec, NULL) < 0) {
		av_log(NULL, AV_LOG_ERROR, "Could not open video codec!\n");
		return false;
	}

	// displays info about the streams contained in the video
	//av_dump_format(m_FormatCtx, 0, m_SourceUri, 0);
	//printf("\nOverall bitrate: %0.f Kbps\n", btokbs(m_FormatCtx->bit_rate));

	// get dimensions of video
	m_VidWidth = m_VideoCodecCtx->width;
	m_VidHeight = m_VideoCodecCtx->height;

	m_FormatCtx = fmtCtx;

	av_free(tmpVideoCodecCtx);

	return true;
};

bool VideoState::PrepareVideoFrame()
{
	int frameSizeInBytes = 0;
	int decodedFrameCount = 0;
	int isFrameFinished = 0;

	unsigned char *frameBuffer = NULL;

	// allocate memory for video frames
	m_VideoFrame = av_frame_alloc();
	m_VideoFrameDisplay = av_frame_alloc();

	// get size of video frame in bytes
	frameSizeInBytes = avpicture_get_size(PIX_FMT_RGBA, m_VidWidth, m_VidHeight);
	
	// allocate memory for frame buffer
	frameBuffer = (unsigned char *)av_malloc(frameSizeInBytes * sizeof(unsigned char));
	
	// prepare the frame buffer for video data
	avpicture_fill((AVPicture *)m_VideoFrameDisplay, frameBuffer, PIX_FMT_RGBA, m_VidWidth, m_VidHeight);
	// todo: NULL checking??
		
	m_VideoPacket = (AVPacket *)av_malloc(sizeof(AVPacket));

	// allocate and return an SwsContext to perform scaling operations with sws_scale()
	// src width, src height, src pixel format, dest width, dest height, dest pixel format, flags, src scaling filter, dest scaling filter, param)
	m_ScalerCtx = sws_getContext(m_VidWidth, m_VidHeight, m_VideoCodecCtx->pix_fmt, m_VidWidth, m_VidHeight, PIX_FMT_RGBA, SWS_LANCZOS, NULL, NULL, NULL);
	if (m_ScalerCtx == NULL) {
		av_log(NULL, AV_LOG_ERROR, "Error creating sws context!\n");
		return false;
	}

	if (renderer == NULL) {
		printf("Cannot prepare video texture when renderer is invalid!\n");
		return false;
	}

	m_VideoTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, m_VidWidth, m_VidHeight);
	m_SrcRect = { 0, 0, m_VidWidth, m_VidHeight };

	return true;
};

int VideoState::DecodeVideoFrame()
{
	int frameFinished = 0;

	if (m_VideoPacket->buf != NULL) {
		if (av_read_frame(m_FormatCtx, m_VideoPacket) == 0) {
			// ffmpeg decode video frame from packet
			if (m_VideoPacket->stream_index == m_FirstVideoStream) {
				avcodec_decode_video2(m_VideoCodecCtx, m_VideoFrame, &frameFinished, m_VideoPacket);
			}
		}
	}
	else {
		frameFinished = -1;
	}

	return frameFinished;
};

void VideoState::Render() {
	// perform format conversion
	sws_scale(m_ScalerCtx, (const unsigned char * const *)m_VideoFrame->data, m_VideoFrame->linesize, 0, m_VideoCodecCtx->height, m_VideoFrameDisplay->data, m_VideoFrameDisplay->linesize);
	
	// update the texture for this video state
	SDL_UpdateTexture(m_VideoTexture, NULL, m_VideoFrameDisplay->data[0], m_VideoFrameDisplay->linesize[0]);	
	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, m_VideoTexture, NULL, &m_SrcRect);
	SDL_RenderPresent(renderer);
};

// move this to utils.h?
AVFormatContext *OpenMediaInput(const char *uri)
{	
	AVFormatContext *inputFormat = avformat_alloc_context();

	// open video source
	if (avformat_open_input(&inputFormat, uri, NULL, &option_dict) != 0) {
		av_log(NULL, AV_LOG_ERROR, "Cannot open video input!\n");
		return false;
	}

	// find stream info from the video file
	if (avformat_find_stream_info(inputFormat, &option_dict) < 0) {
		av_log(NULL, AV_LOG_ERROR, "Could not read stream info\n");
		return false;
	}

	return inputFormat;
};

int main( int argc, char **argv )
{
	VideoState *vState = new VideoState();

	int decoded_frame_count = 0;
	int frame_finished = 0;
	int first_time, frame_time = 0;
	int texok = 0;
	bool done = false;

	// SDL stuff
	SDL_Init( SDL_INIT_EVERYTHING );
	
	// FFMPEG stuff
	av_register_all();
	av_log_set_level( AV_LOG_VERBOSE );

	fmt_ctx = OpenMediaInput("video.mp4");
	vState->GetVideoStream(fmt_ctx);

	// todo: error checking
	window = SDL_CreateWindow("fftest", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, vState->m_VidWidth, vState->m_VidHeight, SDL_WINDOW_SHOWN);
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	printf("Created SDL Window @ %dx%d\n", vState->m_VidWidth, vState->m_VidHeight);

	vState->PrepareVideoFrame();	

	while (!done) {
		if (vState->DecodeVideoFrame()) {
			first_time = SDL_GetTicks();
			vState->Render();
		}
	}

	SDL_DestroyRenderer(renderer);
	renderer = NULL;
	SDL_DestroyWindow(window);
	window = NULL;

	delete vState;

	SDL_Quit();
	return 0;
}