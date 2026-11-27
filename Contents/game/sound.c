#include "sound.h"
#include "actors.h"
#include <math.h>
#include <string.h>

enum { VOICES=12,WAVE=1024 };
typedef struct {
    float seconds,hz0,hz1,noise,grit,volume;
} Instrument;
static const Instrument instrument[SOUND_TYPES]={
    {.135f,145,57,.82f,.21f,.55f}, {.19f,228,74,.81f,.40f,.73f}, {.13f,286,95,.58f,.34f,.62f},
    {.43f,390,123,.91f,.55f,.64f}, {.24f,194,78,.23f,.48f,.56f},
    {.62f,276,680,.65f,.24f,.60f}, {.77f,117,44,.87f,.32f,.88f},
    {.66f,482,346,.08f,.28f,.61f}, {1.62f,118,109,.46f,.12f,.55f}, {.24f,692,567,.29f,.40f,.65f},
    {.31f,223,134,.42f,.43f,.52f}, {.72f,113,69,.19f,.38f,.52f}, {.48f,225,171,.18f,.35f,.56f},
    {.27f,462,243,.31f,.14f,.55f}, {.31f,193,113,.35f,.59f,.56f},
    {.36f,172,141,.09f,.52f,.50f}, {.69f,90,52,.39f,.74f,.52f}, {.30f,247,92,.88f,.73f,.60f},
    {.54f,685,422,.15f,.32f,.51f}, {.24f,359,158,.91f,.50f,.40f},
    {.29f,131,42,.38f,.49f,.60f}, {.51f,268,93,.32f,.68f,.48f}, {.72f,310,132,.60f,.58f,.55f},
    {.35f,245,420,.62f,.17f,.35f}, {.46f,145,83,.42f,.63f,.48f},
    {.21f,648,255,.83f,.68f,.41f}, {.44f,147,59,.81f,.39f,.55f}, {.31f,159,64,.89f,.32f,.40f},
    {.64f,254,91,.38f,.48f,.55f},
    {.92f,160,66,.74f,.68f,.67f},
    {.82f,108,42,.43f,.78f,.71f},
    {.28f,584,282,.61f,.44f,.46f}
};
typedef struct {
    float time,phase,noise,low,left,right,pitch;
    uint32_t random;
    unsigned kind,texture;
} Voice;
static Voice voices[VOICES];
static float wave[WAVE];
static int wave_ready;
static unsigned event_serial;

static uint32_t random_bits(uint32_t *seed) {
    uint32_t r=*seed; r^=r<<13; r^=r>>17; r^=r<<5; return *seed=r;
}
static float sine(float angle) {
    /* All phase arguments are bounded by the lifetime of a voice. */
    float at=angle*WAVE;
    int base=(int)floorf(at);
    float v=wave[(unsigned)base&(WAVE-1)],w=wave[((unsigned)base+1u)&(WAVE-1)];
    return v+(w-v)*(at-base);
}
static float clip(float x) { return fmaxf(0.f,fminf(1.f,x)); }
static float smooth(float x) { x=clip(x); return x*x*(3.f-2.f*x); }
void sound_clear(void) { memset(voices,0,sizeof voices); }
int sound_active(void) {
    for (int i=0;i<VOICES;++i) if (voices[i].left+voices[i].right>0.f) return 1;
    return 0;
}
void sound_play(SoundKind kind,float gain,unsigned variation,Vec3 point) {
    if ((unsigned)kind>=SOUND_TYPES || !(gain>0.f && gain<=2.f)) return;
    float dx=point.x-px,dz=point.z-pz,dy=point.y-py-1.25f;
    float distance2=dx*dx+dy*dy+dz*dz;
    if (!(distance2<32.f*32.f)) return;
    float distance=sqrtf(distance2);
    float pan=(dx*cosf(yaw)+dz*sinf(yaw))/(distance+.35f);
    float energy=gain*instrument[kind].volume/(1.f+.025f*distance2);
    int best=0;
    float quiet=1.e6f;
    for (int i=0;i<VOICES;++i) {
        float priority=(voices[i].left+voices[i].right)*(1.f-voices[i].time/instrument[voices[i].kind].seconds);
        if (priority<quiet) { quiet=priority; best=i; }
    }
    if (quiet>energy*1.45f) return;
    uint32_t seed=(0x8ba694f3u^(++event_serial*0x9e3779b9u)^variation)|1u;
    float pitch=.91f+(random_bits(&seed)&255u)*(.18f/255.f);
    voices[best]=(Voice){0,0,0,0,energy*(1.f-pan*.54f),energy*(1.f+pan*.54f),pitch,seed,(unsigned)kind,variation};
}
void sound_material(SoundKind kind,int raw,Vec3 point) {
    int block=cell_type(raw);
    unsigned material=(is_log(block)||block==PLANKS||build_full(raw)||ladder_cell(raw)||trapdoor_cell(raw)) ? 1u :
        block==GRASS||block==DIRT||block==PODZOL||block==SAND||block==RED_SAND||block==SNOW ? 2u :
        block==GLASS||block==GLASS_PANE||block==ICE ? 3u : 0u;
    float gain=kind==SOUND_STEP ? .49f : kind==SOUND_PLACE ? .71f : .87f;
    sound_play(kind,gain,material,point);
}
void sound_creature(int type,int hurt,int calm,Vec3 point,unsigned variation) {
    SoundKind kind;
    switch (type) {
    case ACTOR_OCELOT: kind=hurt ? SOUND_CAT_HURT : calm ? SOUND_CAT_PURR : SOUND_CAT_MEOW; break;
    case ACTOR_PIG: kind=SOUND_PIG; break;
    case ACTOR_COW: kind=SOUND_COW; break;
    case ACTOR_SHEEP: kind=SOUND_SHEEP; break;
    case ACTOR_CHICKEN: kind=SOUND_CHICKEN; break;
    case ACTOR_WOLF: kind=SOUND_WOLF; break;
    case ACTOR_VILLAGER: case ACTOR_STRANDED_GUIDE: kind=SOUND_VILLAGER; break;
    case ACTOR_ZOMBIE: kind=SOUND_ZOMBIE; break;
    case ACTOR_SKELETON: kind=SOUND_SKELETON; break;
    case ACTOR_FAULTBACK: kind=SOUND_FAULTBACK; break;
    case ACTOR_SHELLBACK: kind=SOUND_SHELLBACK; break;
    case ACTOR_SPIDER: case ACTOR_CAVE_SPIDER: kind=SOUND_SPIDER; break;
    case ACTOR_SILTBACK: kind=SOUND_SILTBACK; break;
    case ACTOR_TIDERAY: kind=SOUND_TIDERAY; break;
    case ACTOR_REEDLIZARD: kind=SOUND_REEDLIZARD; break;
    case ACTOR_LANTERN_MOTH: kind=SOUND_LANTERN_MOTH; break;
    case ACTOR_KILN_MOLE: kind=SOUND_KILN_MOLE; break;
    case ACTOR_SEEP_BEETLE: kind=SOUND_SEEP_BEETLE; break;
    case ACTOR_TIDE_CRAB: kind=SOUND_TIDE_CRAB; break;
    case ACTOR_IRON_GOLEM: case ACTOR_SNOW_GOLEM: kind=SOUND_GOLEM; break;
    case ACTOR_SQUID: kind=SOUND_SQUID; break;
    default: return;
    }
    /* The first two cue IDs for this creature encode actual game events,
       not idle variants: random ambient calls must not imitate a charge. */
    if (kind==SOUND_FAULTBACK) variation=hurt ? 2u : 0u;
    if (kind==SOUND_REEDLIZARD) variation=0u;
    if (kind==SOUND_LANTERN_MOTH) variation=0u;
    sound_play(kind,hurt ? .82f : calm && type==ACTOR_OCELOT ? .64f : .70f,variation|((unsigned)(hurt!=0)<<7),point);
}
static float sample(Voice *v) {
    const Instrument *p=&instrument[v->kind];
    float t=v->time,x=t/p->seconds;
    if (x>=1.f) { v->left=v->right=0.f; return 0.f; }
    float white=((float)(random_bits(&v->random)&65535u)-32767.5f)*(1.f/32767.5f);
    float edge=white-v->noise;
    v->noise+=.16f*edge;
    v->low+=.029f*(v->noise-v->low);
    float attack=.016f,hz=p->hz0+(p->hz1-p->hz0)*smooth(x);
    float mod=1.f,vibrato=0.f,texture=p->noise;
    if (v->kind==SOUND_CAT_MEOW || v->kind==SOUND_CAT_HURT) {
        /* Lift at the front of the call, then slide down through the vowel,
           with a brief nasal onset and a quieter breath on release. */
        hz+=234.f*smooth(x/.28f)*(1.f-smooth((x-.36f)/.57f));
        vibrato=8.3f*sine(t*7.7f)+3.4f*sine(t*16.3f);
        mod=smooth((x+.03f)*10.f)*(1.f-.34f*smooth((x-.64f)*3.2f));
        attack=.031f;
    } else if (v->kind==SOUND_CAT_PURR) {
        float pulse=.63f+.29f*sine(t*26.5f)+.07f*sine(t*53.f);
        mod=pulse*(.77f+.23f*sine(t*1.55f+.27f));
        attack=.12f;
    } else if (v->kind<=SOUND_PLACE) {
        static const float tuning[]={.93f,.64f,.52f,1.85f};
        hz*=tuning[v->texture&3u]; attack=.0035f;
        mod=1.f-x*.74f;
    } else if (v->kind==SOUND_TIDERAY) {
        mod=.45f+.38f*sine(t*10.2f+.1f)+.10f*sine(t*21.4f);
        hz=278.f+132.f*smooth(x*.9f);
        texture*=.34f;
        attack=.048f;
    } else if (v->kind==SOUND_SILTBACK) {
        float stroke=.42f+.36f*sine(t*23.f)+.12f*sine(t*49.f);
        texture*=.43f;
        hz=94.f+38.f*smooth(x)+8.f*sine(t*12.f);
        mod=stroke;
        attack=.022f;
    } else if (v->kind==SOUND_SHELLBACK) {
        /* Two dry mineral taps followed by a short ceramic overtone. */
        float first=1.f-smooth(x*4.f),second=smooth((x-.24f)*30.f)*(1.f-smooth((x-.29f)*1.4f));
        mod=.64f*first+.76f*second;
        hz+=104.f*first;
        attack=.005f;
    } else if (v->kind==SOUND_AIR_PUMP) {
        mod=.61f+.32f*sine(t*19.2f);
        hz=74.f+103.f*smooth(x);
        attack=.019f;
    } else if (v->kind==SOUND_SEEP_BEETLE) {
        float pulse=1.f-smooth(x*3.f);
        hz=510.f+93.f*pulse+56.f*sine(t*17.f);
        texture*=.12f;
        mod=.63f+.22f*sine(t*13.f);
        attack=.025f;
    } else if (v->kind==SOUND_TIDE_CRAB) {
        float first=1.f-smooth(x*7.f),second=smooth((x-.33f)*60.f)*(1.f-smooth((x-.35f)*4.f));
        mod=.78f*first+.82f*second;
        hz+=160.f*first;
        attack=.004f;
    } else if (v->kind==SOUND_KILN_MOLE) {
        float tap=1.f-smooth(x*3.4f),ring=smooth((x-.19f)*19.f)*(1.f-smooth((x-.27f)*1.4f));
        hz=185.f+110.f*tap+48.f*ring+(v->texture&3u)*24.f;
        mod=.54f*tap+.76f*ring;
        texture*=.53f;
        attack=.014f;
    } else if (v->kind==SOUND_FAULTBACK) {
        /* A slow double scrape, a rising fracture buzz, or a landing thud. */
        int action=v->texture&7u;
        float pulse=.67f+.27f*sine(t*(action==1 ? 22.f : 9.f));
        hz=action==1 ? 57.f+127.f*smooth(x) : 44.f+(action==2 ? 101.f : 49.f)*(1.f-smooth(x));
        texture*=action==2 ? 1.29f : .83f;
        mod=pulse*(action==2 ? 1.f-.64f*smooth(x*2.7f) : 1.f);
        attack=action==2 ? .004f : .054f;
    } else if (v->kind==SOUND_REEDLIZARD) {
        int cue=v->texture&7u;
        /* Dry reed tones open into an airy pressure release. Cooling is
           a short ceramic-clink pair, clearly distinct from the warning. */
        hz=cue==1 ? 145.f+280.f*smooth(x) : cue==3 ? 470.f-285.f*smooth(x) : 193.f-67.f*smooth(x);
        texture=cue==2 ? .97f : cue==3 ? .11f : .19f+.18f*smooth(x);
        mod=cue==3 ? fmaxf(1.f-smooth(x*7.f),smooth((x-.2f)*18.f)*(1.f-smooth((x-.28f)*3.3f))) :
            cue==2 ? .79f+.15f*sine(t*9.f) : .61f+.35f*sine(t*(cue==1 ? 23.f : 8.f));
        attack=cue>=2 ? .009f : .04f;
    } else if (v->kind==SOUND_LANTERN_MOTH) {
        const int pollinating=v->texture&1u;
        hz=(pollinating ? 228.f : 168.f)+(pollinating ? 185.f : 76.f)*smooth(x);
        mod=.52f+.44f*sine(t*(pollinating ? 29.f : 42.f));
        texture=pollinating ? .35f : .75f;
        attack=.019f;
    } else if (v->kind==SOUND_FIREWORK || v->kind==SOUND_SKELETON) {
        mod=.30f+.70f*(sine(t*(v->kind==SOUND_FIREWORK ? 21.f : 16.f))>.04f);
        attack=.004f;
    } else if (v->kind==SOUND_CHICKEN || v->kind==SOUND_WOLF || v->kind==SOUND_PIG) {
        mod=.57f+.36f*sine(x*(v->kind==SOUND_WOLF ? 1.4f : 2.3f)+.08f);
        hz+=34.f*sine(t*6.2f);
    } else hz+=hz*.055f*sine(t*4.6f);
    v->phase+=(hz+vibrato)*v->pitch/SOUND_RATE;
    if (v->phase>=1.f) v->phase-=1.f;
    v->time+=1.f/SOUND_RATE;
    float a=v->phase;
    float tone=.49f*sine(a)+(.18f+.12f*p->grit)*sine(2.f*a)+.14f*sine(3.f*a)+.09f*p->grit*sine(5.f*a);
    if (v->kind==SOUND_CAT_MEOW || v->kind==SOUND_CAT_HURT) {
        float vowel=smooth((x-.16f)*1.9f),nose=1.f-smooth(x*4.7f);
        tone=.34f*sine(a)+(.16f+.20f*vowel)*sine(2.f*a)+(.35f-.28f*vowel)*sine(3.f*a)
             +(.15f-.11f*vowel)*sine(4.f*a)+.12f*nose*sine(5.f*a);
        texture+=.20f*smooth((x-.77f)*4.3f);
    } else if (v->kind==SOUND_CAT_PURR) tone=.36f*sine(a)+.33f*sine(2.f*a)+.25f*v->low;
    float breath=.62f*v->noise+.19f*edge+.44f*v->low;
    if (v->kind==SOUND_SPLASH || v->kind==SOUND_FIREWORK) breath+=.21f*white*(1.f-x);
    float envelope=smooth(t/attack)*(1.f-smooth((x-.34f)/.66f))*mod;
    return envelope*((1.f-texture)*tone+texture*breath);
}
void sound_mix(float *stereo,size_t frames) {
    if (!stereo) return;
    if (!wave_ready) {
        for (int i=0;i<WAVE;++i) wave[i]=sinf(i*(6.283185307f/WAVE));
        wave_ready=1;
    }
    for (size_t i=0;i<frames;++i) {
        float left=0.f,right=0.f;
        for (int j=0;j<VOICES;++j) if (voices[j].left+voices[j].right>0.f) {
            Voice *v=&voices[j];
            float s=sample(v);
            left+=v->left*s; right+=v->right*s;
        }
        stereo[i*2]=left/(1.f+.65f*fabsf(left));
        stereo[i*2+1]=right/(1.f+.65f*fabsf(right));
    }
}
