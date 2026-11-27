#include "seed.h"
#include <stddef.h>

int64_t world_seed_signed(uint64_t seed) {
    return seed<=INT64_MAX ? (int64_t)seed : -1-(int64_t)(UINT64_MAX-seed);
}

/* Java's String.hashCode() uses UTF-16 code units, not the UTF-8 bytes or
   Unicode scalars. Encode each supplementary scalar as two units here. */
static int hash_text(const unsigned char *p,const unsigned char *end,uint32_t *hash) {
    uint32_t h=0;
    while (p<end) {
        uint32_t c=*p++;
        if (c>0x7fu) {
            unsigned extra;
            uint32_t minimum;
            if (c>=0xc2u && c<=0xdfu) { extra=1; minimum=0x80u; c&=0x1fu; }
            else if (c>=0xe0u && c<=0xefu) { extra=2; minimum=0x800u; c&=0x0fu; }
            else if (c>=0xf0u && c<=0xf4u) { extra=3; minimum=0x10000u; c&=0x07u; }
            else return 0;
            if ((size_t)(end-p)<extra) return 0;
            while (extra--) {
                unsigned byte=*p++;
                if ((byte&0xc0u)!=0x80u) return 0;
                c=(c<<6)|(byte&0x3fu);
            }
            if (c<minimum || c>0x10ffffu || (c>=0xd800u && c<=0xdfffu)) return 0;
        }
        if (c<=0xffffu) h=31u*h+c;
        else {
            c-=0x10000u;
            h=31u*h+(0xd800u+(c>>10));
            h=31u*h+(0xdc00u+(c&0x3ffu));
        }
    }
    *hash=h;
    return 1;
}

WorldSeedInput world_seed_parse(const char *text,uint64_t *out) {
    if (!out) return SEED_INVALID;
    if (!text || !*text) return SEED_AUTOMATIC;
    size_t length=0;
    while (length<=WORLD_SEED_TEXT_MAX && text[length]) ++length;
    if (length>WORLD_SEED_TEXT_MAX) return SEED_INVALID;
    const unsigned char *start=(const unsigned char *)text,*end=start+length,*p=start;
    int negative=*p=='-';
    if (negative || *p=='+') ++p;
    if (p!=end) {
        uint64_t limit=(uint64_t)INT64_MAX+(unsigned)negative,magnitude=0;
        while (p<end && *p>='0' && *p<='9') {
            unsigned digit=*p-'0';
            if (magnitude>(limit-digit)/10u) break;
            magnitude=magnitude*10u+digit;
            ++p;
        }
        if (p==end) {
            *out=negative ? UINT64_C(0)-magnitude : magnitude;
            return SEED_EXPLICIT;
        }
    }
    uint32_t hash=0;
    if (!hash_text(start,end,&hash)) return SEED_INVALID;
    *out=hash<=INT32_MAX ? (uint64_t)hash : UINT64_C(0xffffffff00000000)|hash;
    return SEED_EXPLICIT;
}
