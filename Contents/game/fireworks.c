#include "fireworks.h"
#include "dyes.h"
#include "fire.h"
#include "sound.h"
#include "../world/space.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const FireworkSpec blank={1,0,{{0}}}, red_star={0,1,{{1u<<4,0,FW_SMALL,0}}};
static FireworkSpec variants[FW_VARIANTS];
static unsigned used;
static FireworkFlight bank[DIMENSION_COUNT][FW_FLIGHTS];
static unsigned version,serial;
static const float duration=2.85f;
static const float tau=6.28318530718f;
static const unsigned colors[16]={
    0x777488,0xa2a5ab,0xc5c8d3,0xfff7e8,0xff3d35,0x6fe053,0xdf9a6d,0x527aff,
    0xaf5eff,0x26e0e6,0xff9cd7,0x9dff41,0xffdf38,0x6ecaff,0xec63ff,0xff9c2b
};

void fireworks_reset(void) {
    used=serial=0;
    memset(bank,0,sizeof bank);
    ++version;
}
unsigned firework_variant_count(void) { return used; }
const FireworkSpec *firework_spec(ItemStack item) {
    if (!firework_item(item.type)) return NULL;
    if (!item.damage) return item.type==FIREWORK_STAR ? &red_star : &blank;
    if (item.damage>used) return NULL;
    const FireworkSpec *p=&variants[item.damage-1];
    return (p->flight==0)==(item.type==FIREWORK_STAR) ? p : NULL;
}
static unsigned star_key(FireworkStar s) { return ((unsigned)s.shape<<2)|s.flags; }
static int star_after(FireworkStar a,FireworkStar b) {
    return a.colors!=b.colors ? a.colors>b.colors : a.fade!=b.fade ? a.fade>b.fade : star_key(a)>star_key(b);
}
static int same(FireworkSpec a,FireworkSpec b) {
    if (a.flight!=b.flight || a.count!=b.count) return 0;
    for (int i=0;i<a.count;++i)
        if (a.star[i].colors!=b.star[i].colors || a.star[i].fade!=b.star[i].fade ||
            star_key(a.star[i])!=star_key(b.star[i])) return 0;
    return 1;
}
int firework_intern(ItemStack *item,FireworkSpec spec,int *created) {
    if (created) *created=0;
    if (!item || !firework_item(item->type) || spec.count>FW_MAX_STARS ||
        (item->type==FIREWORK_STAR ? spec.flight!=0 || spec.count!=1 : spec.flight<1 || spec.flight>3)) return 0;
    for (unsigned i=0;i<spec.count;++i) {
        FireworkStar s=spec.star[i];
        if (!s.colors || s.shape>=FW_SHAPES || s.flags>(FW_TRAIL|FW_TWINKLE)) return 0;
        unsigned j=i;
        while (j && star_after(spec.star[j-1],s)) { spec.star[j]=spec.star[j-1]; --j; }
        spec.star[j]=s;
    }
    if (same(spec,item->type==FIREWORK_STAR ? red_star : blank)) { item->damage=0; return 1; }
    for (unsigned i=0;i<used;++i) if (same(spec,variants[i])) { item->damage=(uint16_t)(i+1); return 1; }
    if (used==FW_VARIANTS) return 0;
    variants[used++]=spec;
    item->damage=(uint16_t)used;
    if (created) *created=(int)used;
    return 1;
}
void firework_rollback(int created) {
    if (created>0 && (unsigned)created==used) --used;
}

ItemStack firework_recipe(const ItemStack *grid,int slots,FireworkSpec *spec) {
    ItemStack none={0};
    if (!grid || !spec || (slots!=4 && slots!=9)) return none;
    FireworkSpec built={0};
    FireworkStar color={0};
    int powder=0,paper=0,shapes=0,heads=0,modifiers=0,fuel=0,unknown=0;
    for (int i=0;i<slots;++i) if (grid[i].type) {
        int type=grid[i].type,glass=glass_recipe_item(type),shape=-1;
        if (glass!=AIR) color.colors|=(uint16_t)(1u<<(glass-BLACK_GLASS_ITEM));
        else if (type==GUNPOWDER) ++powder;
        else if (type==PAPER) ++paper;
        else if (type==FIREWORK_STAR) {
            const FireworkSpec *star=firework_spec(grid[i]);
            if (!star || built.count>=FW_MAX_STARS) return none;
            built.star[built.count++]=star->star[0];
        } else if (type==FIRE_CHARGE) shape=FW_LARGE;
        else if (type==GOLD_NUGGET) shape=FW_STAR;
        else if (type==SHELL_FLAKE) shape=FW_SPIRAL;
        else if (type==FEATHER) shape=FW_BURST;
        else if (type==DIAMOND || type==GLOWSTONE_DUST) {
            int flag=type==DIAMOND ? FW_TRAIL : FW_TWINKLE;
            if (color.flags&flag) return none;
            color.flags|=(uint8_t)flag;
            ++modifiers;
        } else if (type==REED_GRIT) ++heads;
        else if (type==COAL || type==CHARCOAL) ++fuel;
        else ++unknown;
        if (shape>=0) { ++shapes; color.shape=(uint8_t)shape; }
    }
    if (unknown) return none;
    if (powder==1 && heads==1 && fuel==1 && !paper && !shapes && !modifiers && !built.count && !color.colors)
        return (ItemStack){FIRE_CHARGE,3,0,0,0};
    if (heads || fuel) return none;
    if (paper==1 && powder>=1 && powder<=3 && !shapes && !modifiers && !color.colors) {
        built.flight=(uint8_t)powder;
        *spec=built;
        return (ItemStack){FIREWORK_ROCKET,3,0,0,0};
    }
    if (paper || !color.colors) return none;
    if (powder==1 && !built.count && shapes<=1) {
        built.count=1;
        built.star[0]=color;
    } else if (!powder && built.count==1 && !shapes && !modifiers) built.star[0].fade=color.colors;
    else return none;
    *spec=built;
    return (ItemStack){FIREWORK_STAR,1,0,0,0};
}
void firework_describe(const FireworkSpec *spec,char *text,size_t capacity) {
    if (!text || !capacity) return;
    *text=0;
    if (!spec) return;
    if (spec->flight) {
        snprintf(text,capacity,"飞行时间 %u · %u 次爆破",(unsigned)spec->flight,(unsigned)spec->count);
        return;
    }
    static const char *const names[]={"小球","大球","星形","双臂旋涡","爆裂"};
    const FireworkStar *star=&spec->star[0];
    unsigned dyes=0;
    for (unsigned mask=star->colors;mask;mask&=mask-1u) ++dyes;
    snprintf(text,capacity,"%s · %u 色%s%s%s",names[star->shape<FW_SHAPES ? star->shape : FW_SMALL],dyes,
        star->fade ? " · 渐变" : "",star->flags&FW_TRAIL ? " · 尾迹" : "",star->flags&FW_TWINKLE ? " · 闪烁" : "");
}

static uint32_t randomize(uint32_t x) {
    x^=x>>16; x*=UINT32_C(0x7feb352d); x^=x>>15; x*=UINT32_C(0x846ca68b); return x^(x>>16);
}
static float rand01(uint32_t key) { return (randomize(key)&65535u)*(1.f/65535.f); }
int firework_launch(ItemStack item,Vec3 point,float angle) {
    const FireworkSpec *spec=firework_spec(item);
    if (item.type!=FIREWORK_ROCKET || !item.count || !spec ||
        !(point.y>=0.f && point.y<WY && fabsf(point.x)<WORLD_SAVE_LIMIT && fabsf(point.z)<WORLD_SAVE_LIMIT) ||
        !world_chunk_ready((int)floorf(point.x),(int)floorf(point.z)) ||
        world_point_blocked(point.x,point.y,point.z,0)) return 0;
    FireworkFlight *f=bank[world_dimension()];
    for (int i=0;i<FW_FLIGHTS;++i) if (!f[i].fuse) {
        uint32_t seed=randomize(++serial^hash2((int)floorf(point.x),(int)floorf(point.z)));
        float fuse=.56f+.43f*(spec->flight-1)+.12f*rand01(seed);
        f[i]=(FireworkFlight){point,*spec,0,fuse,0,angle,seed};
        ++version;
        sound_play(SOUND_ROCKET,.78f,seed,point);
        printf("FIREWORK LAUNCH flight=%u stars=%u\n",(unsigned)spec->flight,(unsigned)spec->count);
        fflush(stdout);
        return 1;
    }
    return 0;
}
const char *firework_use(Hit hit) {
    if ((unsigned)selected_slot>=HOTBAR_SIZE) return NULL;
    ItemStack *held=&inventory[selected_slot];
    if (!held->count || (held->type!=FIREWORK_ROCKET && held->type!=FIRE_CHARGE)) return NULL;
    if (!hit.hit) return held->type==FIRE_CHARGE ? "对准方块点火" : "对准方块发射烟花";
    if (held->type==FIRE_CHARGE) {
        int tnt=world_cell(hit.x,hit.y,hit.z)==TNT_BLOCK;
        if (!(tnt ? fire_use_igniter(hit.x,hit.y,hit.z) : fire_use_igniter(hit.ax,hit.ay,hit.az)))
            return "这里不能点火";
    } else {
        Vec3 p={hit.ax+.5f,hit.ay+(hit.ay>hit.y ? .055f : .5f),hit.az+.5f};
        if (!firework_launch(*held,p,yaw)) return "发射受阻，烟花未消耗";
    }
    if (player_rules.mode==SURVIVAL && !--held->count) *held=(ItemStack){0};
    return "已点燃";
}
void fireworks_step(float dt) {
    if (!(dt>0.f && dt<=.1f)) return;
    FireworkFlight *list=bank[world_dimension()];
    int moving=0;
    for (int i=0;i<FW_FLIGHTS;++i) if (list[i].fuse) {
        FireworkFlight *f=&list[i];
        moving=1;
        f->age+=dt;
        if (f->burst>0.f) {
            f->burst+=dt;
            if (f->burst>=duration) f->fuse=0.f;
            continue;
        }
        float next=f->point.y+(4.8f+7.f*f->age)*dt;
        int blocked=next<WY && world_point_blocked(f->point.x,next,f->point.z,0);
        if (!blocked) f->point.y=next;
        if (f->age>=f->fuse || blocked) {
            f->burst=.001f;
            if (!f->spec.count) f->fuse=0.f;
            else {
                sound_play(SOUND_FIREWORK,1.14f,f->seed,f->point);
                printf("FIREWORK BURST stars=%u first=%04x shape=%u\n",(unsigned)f->spec.count,
                    (unsigned)f->spec.star[0].colors,(unsigned)f->spec.star[0].shape);
                fflush(stdout);
            }
        }
    }
    if (moving) ++version;
}
const FireworkFlight *fireworks_in(Dimension d) { return (unsigned)d<DIMENSION_COUNT ? bank[d] : NULL; }
unsigned fireworks_revision(void) { return version; }

static unsigned pick_color(unsigned mask,unsigned sample) {
    unsigned n=0;
    for (unsigned bit=0;bit<16;++bit) if (mask&(1u<<bit)) ++n;
    if (!n) return 0xffeee0;
    sample%=n;
    for (unsigned bit=0;bit<16;++bit) if (mask&(1u<<bit)) {
        if (!sample) return colors[bit];
        --sample;
    }
    return 0xffeee0;
}
static unsigned blend(unsigned a,unsigned b,float progress) {
    unsigned out=0;
    for (unsigned shift=0;shift<24;shift+=8) {
        float x=(a>>shift)&255u,y=(b>>shift)&255u;
        out|=(unsigned)(x+(y-x)*progress+.5f)<<shift;
    }
    return out;
}
static Vec3 particle_direction(unsigned shape,unsigned i,unsigned count,uint32_t seed) {
    float fraction=i/(float)count;
    float a=fraction*tau;
    if (shape==FW_STAR) {
        float t=fraction*10.f;
        int k=(int)t;
        float ra=k&1 ? .43f : 1.f,rb=k&1 ? 1.f : .43f;
        float aa=k*tau/10.f,ab=(k+1)*tau/10.f;
        return (Vec3){ra*sinf(aa)+(rb*sinf(ab)-ra*sinf(aa))*(t-k),
            ra*cosf(aa)+(rb*cosf(ab)-ra*cosf(aa))*(t-k),0};
    }
    if (shape==FW_SPIRAL) {
        float u=(i/2+.5f)/((count+1u)/2u),r=.07f+.93f*u;
        float angle=u*tau*1.42f+(i&1u ? .5f*tau : 0.f);
        return (Vec3){r*cosf(angle),r*sinf(angle),.29f*(u-.5f)};
    }
    if (shape==FW_BURST) {
        float r=.24f+.76f*rand01(seed);
        return (Vec3){sinf(a)*r,.32f+rand01(seed+11u)*.84f,cosf(a)*r};
    }
    float y=1.f-2.f*(i+.5f)/count,r=sqrtf(fmaxf(0.f,1.f-y*y));
    a=i*2.39996323f;
    return (Vec3){sinf(a)*r,y,cosf(a)*r};
}
unsigned firework_sparks(const FireworkFlight *f,FireworkSpark out[FW_SPARKS]) {
    if (!f || !f->fuse || !out) return 0;
    if (!f->burst) {
        out[0]=(FireworkSpark){f->point,.12f,0xffeee0};
        for (int i=1;i<=5;++i)
            out[i]=(FireworkSpark){{f->point.x+.045f*sinf(i*3.f+f->age*19.f),f->point.y-i*.12f,
                f->point.z+.04f*cosf(i*6.f+f->age*13.f)},.10f-i*.012f,i<3 ? 0xffbd6bu : 0xbdbad0u};
        return 6;
    }
    unsigned n=0;
    float s=sinf(f->angle),c=cosf(f->angle);
    for (unsigned j=0;j<f->spec.count;++j) {
        const FireworkStar *star=&f->spec.star[j];
        unsigned count=star->shape==FW_LARGE || star->shape==FW_SPIRAL ? 54u : 42u;
        if (f->spec.count>1) count=(count+32u)/f->spec.count+4u;
        float scale=(star->shape==FW_LARGE ? 3.6f : star->shape==FW_STAR || star->shape==FW_SPIRAL ? 3.3f : 2.65f)+.17f*j;
        for (unsigned i=0;i<count && n<FW_SPARKS;++i) {
            uint32_t seed=randomize(f->seed+117u*j+i);
            float t=f->burst-.14f*j;
            if (t<0.f) continue;
            Vec3 v=particle_direction(star->shape,i,count,seed);
            float x=v.x*c-v.z*s,z=v.x*s+v.z*c;
            float life=fmaxf(0.f,1.f-t/duration);
            unsigned tint=pick_color(star->colors,seed);
            if (star->fade) tint=blend(tint,pick_color(star->fade,seed>>16),fmaxf(0.f,fminf(1.f,(t-.65f)*.87f)));
            int twinkle=(star->flags&FW_TWINKLE) && t>.48f && (((unsigned)(t*12)+seed)%4u==0);
            for (int k=0;k<1+2*((star->flags&FW_TRAIL)!=0) && n<FW_SPARKS;++k) {
                float past=fmaxf(0.f,t-k*.115f);
                float spread=scale*(1.f-expf(-2.2f*past));
                float size=(.032f+.087f*life)*(k ? .62f-.14f*k : 1.f);
                if (twinkle) size*=1.4f;
                unsigned color=blend(tint,twinkle ? 0xffffff : 0x534062,
                    twinkle ? .49f : fminf(.82f,.72f*(1.f-life)+k*.18f));
                Vec3 point={f->point.x+x*spread,f->point.y+v.y*spread-.52f*past*past,f->point.z+z*spread};
                out[n++]=(FireworkSpark){point,size,color};
            }
        }
    }
    return n;
}
