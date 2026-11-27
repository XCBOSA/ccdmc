#include "item_custom.h"
#include "navigation.h"
#include <stdlib.h>
#include <string.h>

enum { NAME_BUCKETS=257, NAME_LIMIT=4096 };
typedef struct { char *text; uint16_t next; } Name;
static Name names[NAME_LIMIT+1];
static uint16_t heads[NAME_BUCKETS],used;
void item_names_reset(void) {
    for (unsigned i=1;i<=used;++i) { free(names[i].text); names[i]=(Name){NULL,0}; }
    memset(heads,0,sizeof heads); used=0;
}
int item_name_normalize(const char *src,char out[ITEM_NAME_BYTES+1]) {
    if (!src || !out) return 0;
    size_t bytes=0;
    while (bytes<=ITEM_NAME_BYTES && src[bytes]) ++bytes;
    if (bytes>ITEM_NAME_BYTES) return 0;
    size_t start=0;
    while (start<bytes && src[start]==' ') ++start;
    while (bytes>start && src[bytes-1]==' ') --bytes;
    unsigned units=0;
    for (size_t i=start;i<bytes;) {
        unsigned char first=(unsigned char)src[i++];
        uint32_t scalar=first;
        unsigned extra=0;
        uint32_t minimum=0;
        if (first>=0xc2 && first<=0xdf) { extra=1; scalar=first&31u; minimum=0x80; }
        else if (first>=0xe0 && first<=0xef) { extra=2; scalar=first&15u; minimum=0x800; }
        else if (first>=0xf0 && first<=0xf4) { extra=3; scalar=first&7u; minimum=0x10000; }
        else if (first>=0x80) return 0;
        if (i+extra>bytes) return 0;
        while (extra--) {
            unsigned char tail=(unsigned char)src[i++];
            if ((tail&0xc0)!=0x80) return 0;
            scalar=(scalar<<6)|(tail&63u);
        }
        if (scalar<minimum || scalar<0x20 || (scalar>=0x7f && scalar<=0x9f) ||
            (scalar>=0xd800 && scalar<=0xdfff) || scalar>0x10ffff ||
            scalar==0x2028 || scalar==0x2029) return 0;
        units+=scalar>0xffff ? 2 : 1;
        if (units>ITEM_NAME_UNITS) return 0;
    }
    memmove(out,src+start,bytes-start);
    out[bytes-start]=0;
    return 1;
}
static unsigned intern(const char *text) {
    if (!*text) return 0;
    uint32_t key=2166136261u;
    for (const unsigned char *p=(const unsigned char *)text;*p;++p) key=(key^*p)*16777619u;
    key%=NAME_BUCKETS;
    for (unsigned id=heads[key];id;id=names[id].next) if (!strcmp(names[id].text,text)) return id;
    if (used>=NAME_LIMIT) return 0;
    size_t n=strlen(text)+1;
    char *copy=(char *)malloc(n);
    if (!copy) return 0;
    memcpy(copy,text,n);
    unsigned id=++used;
    names[id]=(Name){copy,heads[key]};
    heads[key]=(uint16_t)id;
    return id;
}
int item_name_intern(const char *input,uint16_t *out) {
    char normalized[ITEM_NAME_BYTES+1];
    if (!out || !item_name_normalize(input,normalized)) return 0;
    unsigned id=intern(normalized);
    if (*normalized && !id) return 0;
    *out=(uint16_t)id;
    return 1;
}
const char *item_name_for_id(uint16_t id) {
    return id && id<=used ? names[id].text : "";
}
const char *item_override(ItemStack item) { return item_name_for_id((uint16_t)(item.custom>>16)); }
const char *item_display_name(ItemStack item) {
    const char *name=item_override(item);
    if (*name) return name;
    if (item.type==ANVIL && item.damage) return item.damage==1 ? "微损铁砧" : "重损铁砧";
    return item_name(item.type);
}
unsigned item_prior_work(ItemStack item) {
    return item.type==FILLED_MAP ? (item.custom>>8)&255u : item.custom&65535u;
}
int item_custom_valid(ItemStack item) {
    return (!item.custom || (item.type && item.count)) && (item.custom>>16)<=used &&
        item_prior_work(item)<=ITEM_PRIOR_MAX && (item.type!=FILLED_MAP || map_view(map_item_id(item),NULL));
}
int item_set_custom(ItemStack *item,unsigned work,const char *name) {
    if (!item || !item->type || !item->count || work>ITEM_PRIOR_MAX) return 0;
    unsigned details=item->type==FILLED_MAP ? (work<<8)|map_item_id(*item) : work;
    if (!name) {
        item->custom=(item->custom&0xffff0000u)|details;
        return 1;
    }
    uint16_t id;
    if (!item_name_intern(name,&id)) return 0;
    item->custom=((uint32_t)id<<16)|details;
    return 1;
}
