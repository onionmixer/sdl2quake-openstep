/*
 * snd_sdl.c -- sound layer
 *
 * From sdlquake (https://github.com/mckayemu/sdlquake), which is
 * id Software's Quake under the GNU General Public License, version 2
 * or later.  MODIFIED for OPENSTEP 4.2 and SDL2 on 2026-08-30:
 *
 *   - SDL_byteorder.h -> SDL_endian.h (SDL2's name for the same header)
 *   - added SNDDMA_Submit(), which snd_dma.c calls unconditionally and this
 *     file never defined -- the Linux build linked snd_linux.c alongside,
 *     which does
 *   - divided samplepos by shm->channels rather than a hardcoded 2; the two
 *     agree today, see the comment at the site
 *
 * The unmodified original is kept beside this tree in upstream/sdlquake.
 */

#include <stdio.h>
#include "SDL_audio.h"
/* OPENSTEP/SDL2: SDL_byteorder.h is SDL 1.2's name.  SDL2 calls the same
 * header SDL_endian.h and keeps SDL_BYTEORDER / SDL_LIL_ENDIAN /
 * SDL_BIG_ENDIAN spelled exactly as they were, so nothing below changes. */
#include "SDL_endian.h"
#include "quakedef.h"

static dma_t the_shm;
static int snd_inited;

extern int desired_speed;
extern int desired_bits;

static void paint_audio(void *unused, Uint8 *stream, int len)
{
	if ( shm ) {
		shm->buffer = stream;
		/*
		 * Divided by the channel count rather than by a hardcoded 2.
		 *
		 * NOT A BUG FIX -- the two agree today, and it is worth saying why
		 * rather than leaving a reader to wonder.  This backend asks for
		 * stereo; if the device cannot give it, SNDDMA_Init falls into its
		 * `default:` arm, closes, and reopens with obtained == NULL, which
		 * in SDL2 means allowed_changes == 0 and a conversion stream that
		 * delivers the requested format exactly.  It then copies `desired`
		 * over `obtained`, so shm->channels is 2 on every path that gets
		 * here.
		 *
		 * On this machine the device really is 22050 mono S16MSB, so that
		 * reopen is the path taken, every time.  Writing the division the
		 * way the rest of the struct describes it costs nothing and stops
		 * being right by coincidence.
		 */
		shm->samplepos += len/(shm->samplebits/8)/shm->channels;
		// Check for samplepos overflow?
		S_PaintChannels (shm->samplepos);
	}
}

qboolean SNDDMA_Init(void)
{
	SDL_AudioSpec desired, obtained;

	snd_inited = 0;

	/* Set up the desired format */
	desired.freq = desired_speed;
	switch (desired_bits) {
		case 8:
			desired.format = AUDIO_U8;
			break;
		case 16:
			if ( SDL_BYTEORDER == SDL_BIG_ENDIAN )
				desired.format = AUDIO_S16MSB;
			else
				desired.format = AUDIO_S16LSB;
			break;
		default:
        		Con_Printf("Unknown number of audio bits: %d\n",
								desired_bits);
			return 0;
	}
	desired.channels = 2;
	desired.samples = 512;
	desired.callback = paint_audio;

	/* Open the audio device */
	if ( SDL_OpenAudio(&desired, &obtained) < 0 ) {
        	Con_Printf("Couldn't open SDL audio: %s\n", SDL_GetError());
		return 0;
	}

	/* Make sure we can support the audio format */
	switch (obtained.format) {
		case AUDIO_U8:
			/* Supported */
			break;
		case AUDIO_S16LSB:
		case AUDIO_S16MSB:
			if ( ((obtained.format == AUDIO_S16LSB) &&
			     (SDL_BYTEORDER == SDL_LIL_ENDIAN)) ||
			     ((obtained.format == AUDIO_S16MSB) &&
			     (SDL_BYTEORDER == SDL_BIG_ENDIAN)) ) {
				/* Supported */
				break;
			}
			/* Unsupported, fall through */;
		default:
			/* Not supported -- force SDL to do our bidding */
			SDL_CloseAudio();
			if ( SDL_OpenAudio(&desired, NULL) < 0 ) {
        			Con_Printf("Couldn't open SDL audio: %s\n",
							SDL_GetError());
				return 0;
			}
			memcpy(&obtained, &desired, sizeof(desired));
			break;
	}
	SDL_PauseAudio(0);

	/* Fill the audio DMA information block */
	shm = &the_shm;
	shm->splitbuffer = 0;
	shm->samplebits = (obtained.format & 0xFF);
	shm->speed = obtained.freq;
	shm->channels = obtained.channels;
	shm->samples = obtained.samples*shm->channels;
	shm->samplepos = 0;
	shm->submission_chunk = 1;
	shm->buffer = NULL;

	snd_inited = 1;
	return 1;
}

/*
================
SNDDMA_Submit

Empty, and upstream's own snd_linux.c is empty for the same reason: this
backend does not push.  SDL pulls -- paint_audio() above is called by the
audio thread when it wants more -- so by the time snd_dma.c asks for a
submission there is nothing left to do.

It exists because snd_dma.c calls it unconditionally, and sdlquake's
snd_sdl.c simply never defined it: the Linux Makefile links snd_linux.c
alongside, which does.  Building the SDL backend on its own leaves the
symbol undefined at link time, which is exactly what happened here.
================
*/
void SNDDMA_Submit(void)
{
}

int SNDDMA_GetDMAPos(void)
{
	return shm->samplepos;
}

void SNDDMA_Shutdown(void)
{
	if (snd_inited)
	{
		SDL_CloseAudio();
		snd_inited = 0;
	}
}

