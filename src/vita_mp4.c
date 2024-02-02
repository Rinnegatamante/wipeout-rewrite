#include <vitasdk.h>
#include <vitaGL.h>
#include <stdio.h>
#include <malloc.h>
#include "vita_mp4.h"

#define FB_ALIGNMENT 0x40000
#define ALIGN_MEM(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

int audio_new;
int audio_port = -1;
int audio_len;
int audio_freq;
int audio_mode;

SceAvPlayerHandle movie_player;
SceUID vid_handle, audio_thid;
int player_state = PLAYER_INACTIVE;

GLuint movie_frame[5] = {};
uint8_t movie_frame_idx = 0;
SceGxmTexture *movie_tex[5];
uint8_t first_frame = 1;

void video_audio_init(void) {
	// Check if we have an already available audio port
	audio_port = -1;
	for (int i = 0; i < 8; i++) {
		if (sceAudioOutGetConfig(i, SCE_AUDIO_OUT_CONFIG_TYPE_LEN) >= 0) {
			audio_port = i;
			break;
		}
	}

	// Configure the audio port (either new or old)
	if (audio_port == -1) {
		audio_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, 1024, 48000, SCE_AUDIO_OUT_MODE_STEREO);
		audio_new = 1;
	} else {
		audio_len = sceAudioOutGetConfig(audio_port, SCE_AUDIO_OUT_CONFIG_TYPE_LEN);
		audio_freq = sceAudioOutGetConfig(audio_port, SCE_AUDIO_OUT_CONFIG_TYPE_FREQ);
		audio_mode = sceAudioOutGetConfig(audio_port, SCE_AUDIO_OUT_CONFIG_TYPE_MODE);
		audio_new = 0;
	}
}

int video_audio_thread(SceSize args, void *argp) {
	SceAvPlayerFrameInfo frame;
	sceClibMemset(&frame, 0, sizeof(SceAvPlayerFrameInfo));

	while (player_state != PLAYER_INACTIVE && sceAvPlayerIsActive(movie_player)) {
		if (sceAvPlayerGetAudioData(movie_player, &frame)) {
			sceAudioOutSetConfig(audio_port, 1024, frame.details.audio.sampleRate, frame.details.audio.channelCount == 1 ? SCE_AUDIO_OUT_MODE_MONO : SCE_AUDIO_OUT_MODE_STEREO);
			sceAudioOutOutput(audio_port, frame.pData);
		} else {
			sceKernelDelayThread(1000);
		}
	}

	return sceKernelExitDeleteThread(0);
}

void *mem_alloc(void *p, uint32_t align, uint32_t size) {
	return memalign(align, size);
}

void mem_free(void *p, void *ptr) {
	free(ptr);
}

void *gpu_alloc(void *p, uint32_t align, uint32_t size) {
	if (align < 0x40000)
		align = 0x40000;
	size = ALIGN_MEM(size, align);
	SceKernelAllocMemBlockOpt opt;
	memset(&opt, 0, sizeof(opt));
	opt.size = sizeof(SceKernelAllocMemBlockOpt);
	opt.attr = 0x00000004U;
	opt.alignment = align;
	SceUID blk = sceKernelAllocMemBlock("cdram blk", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, size, &opt);
	void *res;
	sceKernelGetMemBlockBase(blk, &res);
	sceGxmMapMemory(res, size, SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE);
	return res;
}

void gpu_free(void *p, void *addr) {
	glFinish();
	SceUID blk = sceKernelFindMemBlockByAddr(addr, 0);
	sceGxmUnmapMemory(addr);
	sceKernelFreeMemBlock(blk);
}

void video_close() {
	if (player_state == PLAYER_ACTIVE) {
		sceAvPlayerStop(movie_player);
		sceAvPlayerClose(movie_player);
		player_state = PLAYER_INACTIVE;
	}
}

void video_open(const char *path, int looping) {
	if (player_state == PLAYER_ACTIVE) {
		video_close();
	}
	
	if (audio_port == -1)
		video_audio_init();
	
	first_frame = 1;
	if (movie_frame[0] == 0) {
		glGenTextures(5, movie_frame);
		for (int i = 0; i < 5; i++) {
			glBindTexture(GL_TEXTURE_2D, movie_frame[i]);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
			movie_tex[i] = vglGetGxmTexture(GL_TEXTURE_2D);
			vglFree(vglGetTexDataPointer(GL_TEXTURE_2D));
		}
	}
	
	SceAvPlayerInitData playerInit;
	memset(&playerInit, 0, sizeof(SceAvPlayerInitData));

	playerInit.memoryReplacement.allocate = mem_alloc;
	playerInit.memoryReplacement.deallocate = mem_free;
	playerInit.memoryReplacement.allocateTexture = gpu_alloc;
	playerInit.memoryReplacement.deallocateTexture = gpu_free;

	playerInit.basePriority = 0xA0;
	playerInit.numOutputVideoFrameBuffers = 5;
	playerInit.autoStart = 1;
#if DEBUG
	playerInit.debugLevel = 3;
#endif

	movie_player = sceAvPlayerInit(&playerInit);
	sceAvPlayerAddSource(movie_player, path);
	sceAvPlayerSetLooping(movie_player, looping);
	
	audio_thid = sceKernelCreateThread("video_audio_thread", video_audio_thread, 0x10000100 - 10, 0x4000, 0, 0, NULL);
	sceKernelStartThread(audio_thid, 0, NULL);
	
	player_state = PLAYER_ACTIVE;
}

uint32_t video_get_current_time() {
	if (player_state == PLAYER_ACTIVE && sceAvPlayerIsActive(movie_player))
		return sceAvPlayerCurrentTime(movie_player);
	return 0;
}

GLuint video_get_frame(int *width, int *height) {
	if (player_state == PLAYER_ACTIVE) {
		if (sceAvPlayerIsActive(movie_player)) {
			SceAvPlayerFrameInfo frame;
			if (sceAvPlayerGetVideoData(movie_player, &frame)) {
				movie_frame_idx = (movie_frame_idx + 1) % 5;
				sceGxmTextureInitLinear(
					movie_tex[movie_frame_idx],
					frame.pData,
					SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1,
					frame.details.video.width,
					frame.details.video.height, 0);
				*width = frame.details.video.width;
				*height = frame.details.video.height;
				sceGxmTextureSetMinFilter(movie_tex[movie_frame_idx], SCE_GXM_TEXTURE_FILTER_LINEAR);
				sceGxmTextureSetMagFilter(movie_tex[movie_frame_idx], SCE_GXM_TEXTURE_FILTER_LINEAR);
				first_frame = 0;
			}
			return first_frame ? 0xDEADBEEF : movie_frame[movie_frame_idx];
		} else if (!first_frame)
			video_close();
	} else if (player_state == PLAYER_PAUSED) {
		return movie_frame[movie_frame_idx];
	}
	
	return 0xDEADBEEF;
}

void video_pause() {
	sceAvPlayerPause(movie_player);
	player_state = PLAYER_PAUSED;
}

void video_resume() {
	sceAvPlayerResume(movie_player);
	player_state = PLAYER_ACTIVE;
}

void video_set_volume(float vol) {
	int vols[2] = {(int)(vol * 32767.0f), (int)(vol * 32767.0f)};
	sceAudioOutSetVolume(audio_port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vols);
}

void video_jump_to_time(uint64_t time) {
	if (player_state == PLAYER_ACTIVE) { 
		uint64_t old_time = sceAvPlayerCurrentTime(movie_player);
		sceAvPlayerJumpToTime(movie_player, time);
		while (sceAvPlayerCurrentTime(movie_player) == old_time) { // Wait for sceAvPlayer FileStream thread response after jump finished
			sceKernelDelayThread(1000);
		}
	}
}

float *draw_attributes = NULL;
int draw_video_frame() {
	glUseProgram(0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (!draw_attributes)
		draw_attributes = (float*)malloc(sizeof(float) * 22);

	int vid_w, vid_h;
	GLuint vid_frame = video_get_frame(&vid_w, &vid_h);
	if (vid_frame == 0xDEADBEEF)
		return 0;
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, vid_frame);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_CULL_FACE);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	draw_attributes[0] = 0.0f;
	draw_attributes[1] = 0.0f;
	draw_attributes[2] = 0.0f;
	draw_attributes[3] = 960.0f;
	draw_attributes[4] = 0.0f;
	draw_attributes[5] = 0.0f;
	draw_attributes[6] = 0.0f;
	draw_attributes[7] = 544.0f;
	draw_attributes[8] = 0.0f;
	draw_attributes[9] = 960.0f;
	draw_attributes[10] = 544.0f;
	draw_attributes[11] = 0.0f;
	vglVertexPointerMapped(3, draw_attributes);
	
	draw_attributes[12] = 0.0f;
	draw_attributes[13] = 0.0f;
	draw_attributes[14] = 1.0f;
	draw_attributes[15] = 0.0f;
	draw_attributes[16] = 0.0f;
	draw_attributes[17] = 1.0f;
	draw_attributes[18] = 1.0f;
	draw_attributes[19] = 1.0f;
	vglTexCoordPointerMapped(&draw_attributes[12]);
	
	uint16_t *bg_indices = (uint16_t*)&draw_attributes[20];
	bg_indices[0] = 0;
	bg_indices[1] = 1;
	bg_indices[2] = 2;
	bg_indices[3] = 3;
	vglIndexPointerMapped(bg_indices);

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	//glEnableClientState(GL_COLOR_ARRAY);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrthof(0, 960, 544, 0, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	vglDrawObjects(GL_TRIANGLE_STRIP, 4, GL_TRUE);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glDisableClientState(GL_COLOR_ARRAY);
	return 1;
}
