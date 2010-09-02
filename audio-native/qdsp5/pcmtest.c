/* pcmtest.c - native PCM test application
 *
 * Based on native pcm test application platform/system/extras/sound/playwav.c
 *
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2009-2010, Code Aurora Forum. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/msm_audio.h>
#include <pthread.h>
#include <errno.h>
#include "audiotest_def.h"

const char     *dev_file_name;
static char *next, *org_next;
static unsigned avail, org_avail;
static int quit, repeat;

static int pcm_play(struct audtest_config *cfg, unsigned rate, 
					unsigned channels, int (*fill)(void *buf, 
												   unsigned sz, void *cookie), void *cookie)
{
	struct msm_audio_config config;
	// struct msm_audio_stats stats;
	unsigned n;
	int sz;
	char *buf; 
	int afd;
	int cntW=0;
	int ret = 0;
#ifdef AUDIOV2
	unsigned short dec_id;
	int control = 0;
#endif

	afd = open(dev_file_name, O_WRONLY);

	if (afd < 0) {
		perror("pcm_play: cannot open audio device");
		return -1;
	}

	cfg->private_data = (void*) afd;

#ifdef AUDIOV2
	if (ioctl(afd, AUDIO_GET_SESSION_ID, &dec_id)) {
		perror("could not get decoder session id\n");
		close(afd);
		return -1;
	}
#if defined(QC_PROP)
    if (devmgr_register_session(dec_id, DIR_RX) < 0) {
		ret = -1;
		goto exit;
    }
#endif
#endif

	if (ioctl(afd, AUDIO_GET_CONFIG, &config)) {
		perror("could not get config");
		ret = -1;
		goto err_state;
	}

	config.channel_count = channels;
	config.sample_rate = rate;
	if (ioctl(afd, AUDIO_SET_CONFIG, &config)) {
		perror("could not set config");
		ret = -1;
		goto err_state;
	}

	buf = (char*) malloc(sizeof(char) * config.buffer_size);
	if (buf == NULL) {
		perror("fail to allocate buffer\n");
		ret = -1;
		goto err_state;
	}

	printf("initiate_play: buffer_size=%d, buffer_count=%d\n", config.buffer_size,
		   config.buffer_count);

	fprintf(stderr,"prefill\n");
	for (n = 0; n < config.buffer_count; n++) {
		if ((sz = fill(buf, config.buffer_size, cookie)) < 0)
			break;
		if (write(afd, buf, sz) != sz)
			break;
	}
	cntW=cntW+config.buffer_count; 

	fprintf(stderr,"start playback\n");
	if (ioctl(afd, AUDIO_START, 0) >= 0) {
		for (;;) {
#if 0
		if (ioctl(afd, AUDIO_GET_STATS, &stats) == 0)
			fprintf(stderr,"%10d\n", stats.out_bytes);
#endif
			if (((sz = fill(buf, config.buffer_size, cookie)) < 0) || (quit == 1)) {
				if ((repeat == 0) || (quit == 1)) {
					printf(" fill return NON NULL, exit loop \n");
					break;
				} else {
					printf("\nRepeat playback\n");
					avail = org_avail;
					next  = org_next;
					cntW = 0;
					if(repeat > 0)
						repeat--;
					sleep(1);
					continue;
				}
			}
			if (write(afd, buf, sz) != sz) {
				printf(" write return not equal to sz, exit loop\n");
				break;
			} else {
				cntW++;
				printf(" pcm_play: repeat_count=%d cntW=%d\n", repeat, cntW);
			}
		}
		printf("end of pcm play\n");
		sleep(5); 
	} else {
		printf("pcm_play: Unable to start driver\n");
	}
	free(buf);
err_state:
#if defined(QC_PROP) && defined(AUDIOV2)
	if (devmgr_unregister_session(dec_id, DIR_RX) < 0)
			ret = -1;
exit:
#endif
	close(afd);
	return ret;
}

/* http://ccrma.stanford.edu/courses/422/projects/WaveFormat/ */

#define ID_RIFF 0x46464952
#define ID_WAVE 0x45564157
#define ID_FMT  0x20746d66
#define ID_DATA 0x61746164

#define FORMAT_PCM 1

struct wav_header {
	uint32_t riff_id;
	uint32_t riff_sz;
	uint32_t riff_fmt;
	uint32_t fmt_id;
	uint32_t fmt_sz;
	uint16_t audio_format;
	uint16_t num_channels;
	uint32_t sample_rate;
	uint32_t byte_rate;		  /* sample_rate * num_channels * bps / 8 */
	uint16_t block_align;	  /* num_channels * bps / 8 */
	uint16_t bits_per_sample;
	uint32_t data_id;
	uint32_t data_sz;
};

static int fill_buffer(void *buf, unsigned sz, void *cookie)
{
	unsigned cpy_size = (sz < avail?sz:avail);   

	if (avail == 0)
		return -1;

	memcpy(buf, next, cpy_size);
	next += cpy_size; 
	avail -= cpy_size;

	return cpy_size;
}

static int play_file(struct audtest_config *config, 
					 unsigned rate, unsigned channels,
					 int fd, unsigned count)
{
	next = (char*)malloc(count);
	org_next = next;
	printf(" play_file: count=%d,next=%s\n", count,next);
	if (!next) {
		fprintf(stderr,"could not allocate %d bytes\n", count);
		return -1;
	}
	if (read(fd, next, count) != count) {
		fprintf(stderr,"could not read %d bytes\n", count);
		return -1;
	}
	avail = count;
	org_avail = avail;
	return pcm_play(config, rate, channels, fill_buffer, 0);
}

int wav_play(struct audtest_config *config)
{

	struct wav_header hdr;
	//  unsigned rate, channels;
	int fd;

	if (config == NULL) {
		return -1;
	}

	fd = open(config->file_name, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "playwav: cannot open '%s'\n", config->file_name);
		return -1;
	}
	if (read(fd, &hdr, sizeof(hdr)) != sizeof(hdr)) {
		fprintf(stderr, "playwav: cannot read header\n");
		return -1;
	}
	fprintf(stderr,"playwav: %d ch, %d hz, %d bit, %s\n",
			hdr.num_channels, hdr.sample_rate, hdr.bits_per_sample,
			hdr.audio_format == FORMAT_PCM ? "PCM" : "unknown");

	if ((hdr.riff_id != ID_RIFF) ||
		(hdr.riff_fmt != ID_WAVE) ||
		(hdr.fmt_id != ID_FMT)) {
		fprintf(stderr, "playwav: '%s' is not a riff/wave file\n", 
				config->file_name);
		return -1;
	}
	if ((hdr.audio_format != FORMAT_PCM) ||
		(hdr.fmt_sz != 16)) {
		fprintf(stderr, "playwav: '%s' is not pcm format\n", config->file_name);
		return -1;
	}
	if (hdr.bits_per_sample != 16) {
		fprintf(stderr, "playwav: '%s' is not 16bit per sample\n", config->file_name);
		return -1;
	}

	return play_file(config, hdr.sample_rate, hdr.num_channels,
					 fd, hdr.data_sz);
}

int rec_stop = 1;

int wav_rec(struct audtest_config *config)
{

	struct wav_header hdr;
	unsigned char buf[8192];
	struct msm_audio_config cfg;
	unsigned sz; //n;
	int fd, afd;
	unsigned total = 0;
	unsigned char tmp;
#ifdef AUDIOV2
	unsigned short enc_id;
	int device_id;
	int control = 0;
	const char *device = "handset_tx";
#endif

	if ((config->channel_mode != 2) && (config->channel_mode != 1)) {
		perror("invalid channel mode \n");
		return -1;
	} else {
		switch (config->sample_rate) {
		case 48000:    
		case 44100:  
		case 32000:  
		case 24000:  
		case 22050:  
		case 16000: 
		case 12000: 
		case 11025: 
		case 8000:  
			break;
		default:    
			perror("invalid sample rate \n");
			return -1;
			break;
		}
	}

	hdr.riff_id = ID_RIFF;
	hdr.riff_sz = 0;
	hdr.riff_fmt = ID_WAVE;
	hdr.fmt_id = ID_FMT;
	hdr.fmt_sz = 16;
	hdr.audio_format = FORMAT_PCM;
	hdr.num_channels = config->channel_mode;
	hdr.sample_rate = config->sample_rate;
	hdr.byte_rate = hdr.sample_rate * hdr.num_channels * 2;
	hdr.block_align = hdr.num_channels * 2;
	hdr.bits_per_sample = 16;
	hdr.data_id = ID_DATA;
	hdr.data_sz = 0;

	fd = open(config->file_name, O_CREAT | O_RDWR, 0666);
	if (fd < 0) {
		perror("cannot open output file");
		return -1;
	}
	write(fd, &hdr, sizeof(hdr));

#ifdef AUDIOV2
	afd = open("/dev/msm_pcm_in", O_RDONLY);
	if (afd < 0) {
		perror("cannot open msm_pcm_in");
		close(fd);
		return -1;
	}
#else
        afd = open("/dev/msm_pcm_in", O_RDWR);
        if (afd < 0) {
                perror("cannot open msm_pcm_in");
                close(fd);
                return -1;
        }
#endif

#ifdef AUDIOV2
	if (ioctl(afd, AUDIO_GET_SESSION_ID, &enc_id)) {
		perror("could not get encoder session id\n");
		close(fd);
		close(afd);
		return -1;
	}
	if (devmgr_register_session(enc_id, DIR_TX) < 0) {
		close(fd);
		close(afd);
		return -1;
	}
#endif
	config->private_data = (void*) afd;

	if (ioctl(afd, AUDIO_GET_CONFIG, &cfg)) {
		perror("cannot read audio config");
		goto fail;
	}

	cfg.channel_count = hdr.num_channels;
	cfg.sample_rate = hdr.sample_rate;
	if (ioctl(afd, AUDIO_SET_CONFIG, &cfg)) {
		perror("cannot write audio config");
		goto fail;
	}

	if (ioctl(afd, AUDIO_GET_CONFIG, &cfg)) {
		perror("cannot read audio config");
		goto fail;
	}

	sz = cfg.buffer_size;
	fprintf(stderr, "buffer size %d\n", sz);
	if (sz > sizeof(buf)) {
		fprintf(stderr, "buffer size %d too large\n", sz);
		goto fail;
	}

	if (ioctl(afd, AUDIO_START, 0) < 0) {
		perror("cannot start audio");
		goto fail;
	}
	rec_stop = 0;
	fprintf(stderr,"\n*** RECORDING IN PROGRESS ***\n");

	while(!rec_stop) {
		if (read(afd, buf, sz) != sz) {
			perror("cannot read buffer");
			goto fail;
		}
		if (write(fd, buf, sz) != sz) {
			perror("cannot write buffer");
			goto fail;
		}
		total += sz;
	}
	done:
	close(afd);

	/* update lengths in header */
	hdr.data_sz = total;
	hdr.riff_sz = total + 8 + 16 + 8;
	lseek(fd, 0, SEEK_SET);
	write(fd, &hdr, sizeof(hdr));
	close(fd);
#ifdef AUDIOV2
	if (devmgr_unregister_session(enc_id, DIR_TX) < 0){
		perror("could not unregister encode session\n");
	}
#endif
	return 0;

	fail:
	close(afd);
	close(fd);
#ifdef AUDIOV2
	if (devmgr_unregister_session(enc_id, DIR_TX) < 0){
		perror("could not unregister encode session\n");
	}
#endif
	unlink(config->file_name);
	return -1;
}

void* playpcm_thread(void* arg) {
	struct audiotest_thread_context *context = 
	(struct audiotest_thread_context*) arg;
	int ret_val;

	ret_val = wav_play(&context->config);
	free_context(context);
	pthread_exit((void*) ret_val);

    return NULL;
}

int pcmplay_read_params(void) {
	struct audiotest_thread_context *context; 
	char *token;
	int ret_val = 0;

	if ((context = get_free_context()) == NULL) {
		ret_val = -1;
	} else {
		context->config.file_name = "/data/data.wav";
		dev_file_name = "/dev/msm_pcm_out";
		repeat = 0;
		quit = 0;

		token = strtok(NULL, " ");

		while (token != NULL) {
			if (!memcmp(token,"-id=", (sizeof("-id=")-1))) {
				context->cxt_id = atoi(&token[sizeof("-id=") - 1]);
			} else if (!memcmp(token, "-dev=",
					(sizeof("-dev=") - 1))) {
				dev_file_name = token + (sizeof("-dev=")-1);
			} else if (!memcmp(token, "-repeat=",
					(sizeof("-repeat=") - 1))) {
				repeat = atoi(&token[sizeof("-repeat=") - 1]);
				if (repeat == 0)
					repeat = -1;
				else
					repeat--;
			} else {
				context->config.file_name = token;
			} 
			token = strtok(NULL, " ");
		}
		context->type = AUDIOTEST_TEST_MOD_PCM_DEC;
		pthread_create( &context->thread, NULL, 
						playpcm_thread, (void*) context);
	}

	return ret_val;

}

void* recpcm_thread(void* arg) {
	struct audiotest_thread_context *context = 
	(struct audiotest_thread_context*) arg;
	int ret_val;

	ret_val = wav_rec(&context->config);
	free_context(context);
	pthread_exit((void*) ret_val);

    return NULL;
}

int pcmrec_read_params(void) {
	struct audiotest_thread_context *context;
	char *token;
	int ret_val = 0;

	if ((context = get_free_context()) == NULL) {
		ret_val = -1;
	} else {
		context->config.sample_rate = 8000;
		context->config.file_name = "/data/record.wav";
		context->config.channel_mode = 1;  
		context->type = AUDIOTEST_TEST_MOD_PCM_ENC;
		token = strtok(NULL, " ");

		while (token != NULL) {
			printf("%s \n", token);
			if (!memcmp(token,"-rate=", (sizeof("-rate=") - 1))) {
				context->config.sample_rate = 
				atoi(&token[sizeof("-rate=") - 1]);
			} else if (!memcmp(token,"-cmode=", (sizeof("-cmode=") - 1))) {
				context->config.channel_mode = 
				atoi(&token[sizeof("-cmode=") - 1]);
			} else if (!memcmp(token,"-id=", (sizeof("-id=") - 1))) {
				context->cxt_id = atoi(&token[sizeof("-id=") - 1]);
			} else {
				context->config.file_name = token;
			}
			token = strtok(NULL, " ");  
		}
		printf("%s : sample_rate=%d channel_mode=%d\n", __FUNCTION__, 
			   context->config.sample_rate, context->config.channel_mode);
		pthread_create( &context->thread, NULL, recpcm_thread, (void*) context);  
	}

	return ret_val;
}

int pcm_play_control_handler(void* private_data) {
	int  drvfd , ret_val = 0;
	char *token;
#if defined(QC_PROP) && defined(AUDIOV2)
	int volume;
#endif

	token = strtok(NULL, " ");
	if ((private_data != NULL) && 
		(token != NULL)) {
		drvfd = (int) private_data;
		if(!memcmp(token,"-cmd=", (sizeof("-cmd=") -1))) {
#if defined(QC_PROP) && defined(AUDIOV2)
                       token = &token[sizeof("-cmd=") - 1];
                       printf("%s: cmd %s\n", __FUNCTION__, token);
                       if (!strcmp(token, "volume")) {
                               int rc;
                               unsigned short dec_id;
                               token = strtok(NULL, " ");
                               if (!memcmp(token, "-value=",
                                       (sizeof("-value=") - 1))) {
                                       volume = atoi(&token[sizeof("-value=") - 1]);
                                       if (ioctl(drvfd, AUDIO_GET_SESSION_ID, &dec_id)) {
                                               perror("could not get decoder session id\n");
                                       } else {
                                               printf("session %d - volume %d \n", dec_id, volume);
                                               rc = msm_set_volume(dec_id, volume);
                                               printf("session volume result %d\n", rc);
                                       }
                               }
                       } else if (!strcmp(token, "stop")) {
			       quit = 1;
			       printf("quit session\n");
		       }
#else
			token = &token[sizeof("-id=") - 1];
			printf("%s: cmd %s\n", __FUNCTION__, token);
#endif
		}
	} else {
		ret_val = -1;
	}

	return ret_val;
}

int pcm_rec_control_handler(void* private_data) {
	int /* drvfd ,*/ ret_val = 0;
	char *token;

	token = strtok(NULL, " ");
	if ((private_data != NULL) && 
		(token != NULL)) {
		/* drvfd = (int) private_data */
		if(!memcmp(token,"-cmd=", (sizeof("-cmd=") -1))) {
			token = &token[sizeof("-cmd=") - 1];
			printf("%s: cmd %s\n", __FUNCTION__, token);
			if (!strcmp(token, "stop")) {
				rec_stop = 1;
			}
		}
	} else {
		ret_val = -1;
	}

	return ret_val;
}

const char *pcmplay_help_txt = 
	"Play PCM file: type \n\
echo \"playpcm path_of_file -id=xxx -repeat=x -dev=/dev/msm_pcm_dec or msm_pcm_out\" > tmp/audio_test \n\
Repeat 'x' no. of times, repeat infinitely if repeat = 0\n\
Sample rate of PCM file <= 48000 \n\
Bits per sample = 16 bits \n\
Supported control command: pause, flush, volume, stop\n ";

void pcmplay_help_menu(void) {
	printf("%s\n", pcmplay_help_txt);
}

const char *pcmrec_help_txt = 
"Record pcm file: type \n\
echo \"recpcm path_of_file -rate=xxx -cmode=x -id=xxx\" > tmp/audio_test \n\
sample rate: 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000 \n\
channel mode: 1 or 2 \n\
Supported control command: stop\n ";

void pcmrec_help_menu(void) {
	printf("%s\n", pcmrec_help_txt);
}