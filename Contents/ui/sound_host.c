#include "sound_host.h"
#include "../game/sound.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static SDL_AudioStream *stream;
static int audio_initialized,was_playing,reported;
static const char *driver;
enum { FRAME_CHUNK=480, RING_FRAMES=4096, TARGET_FRAMES=4*FRAME_CHUNK,
       FRAME_BYTES=SOUND_CHANNELS*sizeof(float) };
static float fifo[RING_FRAMES][SOUND_CHANNELS];
/* Only the game thread advances written and skip; only SDL advances read.
   Monotonic unsigned counters also work across their natural wraparound. */
static uint32_t written,read_frames,skip_before,consumed;

static void SDLCALL pull_audio(void *unused,SDL_AudioStream *output,int additional,int total) {
    (void)unused; (void)total;
    unsigned remaining=(unsigned)(additional>0 ? additional : 0)/FRAME_BYTES;
    for (int block=0;block<4 && remaining;++block) {
        const unsigned need=remaining<FRAME_CHUNK ? remaining : FRAME_CHUNK;
        const uint32_t w=__atomic_load_n(&written,__ATOMIC_ACQUIRE);
        uint32_t r=__atomic_load_n(&read_frames,__ATOMIC_RELAXED);
        uint32_t skip=__atomic_load_n(&skip_before,__ATOMIC_ACQUIRE);
        if (skip-r<=w-r) r=skip;
        unsigned count=w-r<need ? w-r : need;
        float samples[FRAME_CHUNK][SOUND_CHANNELS];
        for (unsigned n=0;n<count;++n)
            memcpy(samples[n],fifo[(r+n)&(RING_FRAMES-1)],FRAME_BYTES);
        memset(samples+count,0,(need-count)*FRAME_BYTES);
        __atomic_store_n(&read_frames,r+count,__ATOMIC_RELEASE);
        if (SDL_PutAudioStreamData(output,samples,(int)(need*FRAME_BYTES)) && count)
            __atomic_store_n(&consumed,1,__ATOMIC_RELEASE);
        remaining-=need;
    }
}

void sound_host_open(void) {
    if (audio_initialized) return;
    /* Opening an SDL device starts its guest mixer thread. Do this on the
       program entry thread, not inside a CCDUI timer callback. */
    audio_initialized=SDL_InitSubSystem(SDL_INIT_AUDIO);
    if (audio_initialized) {
        SDL_AudioSpec format={SDL_AUDIO_F32LE,SOUND_CHANNELS,SOUND_RATE};
        driver=SDL_GetCurrentAudioDriver();
        stream=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&format,pull_audio,NULL);
        if (stream && !SDL_ResumeAudioStreamDevice(stream)) {
            fprintf(stderr,"CCDMC audio unavailable: %s\n",SDL_GetError());
            SDL_DestroyAudioStream(stream);
            stream=NULL;
            return;
        }
    }
    if (!stream) {
        fprintf(stderr,"CCDMC audio unavailable: %s\n",SDL_GetError());
        fflush(stderr);
    }
}
void sound_host_close(void) {
    if (stream) { SDL_DestroyAudioStream(stream); stream=NULL; }
    if (audio_initialized) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    audio_initialized=was_playing=reported=0;
    written=read_frames=skip_before=consumed=0;
    driver=NULL;
    sound_clear();
}
void sound_host_tick(int session_playing) {
    if (!session_playing) {
        if (was_playing) {
            sound_clear();
            const uint32_t end=__atomic_load_n(&written,__ATOMIC_RELAXED);
            __atomic_store_n(&skip_before,end,__ATOMIC_RELEASE);
        }
        was_playing=0;
        return;
    }
    was_playing=1;
    if (!reported && __atomic_load_n(&consumed,__ATOMIC_ACQUIRE)) {
        reported=1;
        printf("CCDMC AUDIO: SDL stereo PCM started (%s)\n",driver ? driver : "unknown");
        fflush(stdout);
    }
    if (!sound_active()) return;
    if (!stream) { sound_clear(); return; }
    uint32_t w=__atomic_load_n(&written,__ATOMIC_RELAXED);
    uint32_t r=__atomic_load_n(&read_frames,__ATOMIC_ACQUIRE);
    /* Never call a locking SDL audio API from the CCDUI timer. Otherwise
       a guest mixer blocked in a host call can also strand UI callbacks. */
    for (int i=0;i<4 && w-r+FRAME_CHUNK<=TARGET_FRAMES && sound_active();++i) {
        float pcm[FRAME_CHUNK*SOUND_CHANNELS];
        sound_mix(pcm,FRAME_CHUNK);
        for (unsigned n=0;n<FRAME_CHUNK;++n)
            memcpy(fifo[(w+n)&(RING_FRAMES-1)],pcm+n*SOUND_CHANNELS,FRAME_BYTES);
        w+=FRAME_CHUNK;
        __atomic_store_n(&written,w,__ATOMIC_RELEASE);
    }
}
