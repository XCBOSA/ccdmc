#include "map_ui.h"
#include "../game/navigation.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* CCD's guest GPU supports one world transform per frame. Reuse CCDUI's
   transparent canvas for the paper, behind the existing touch controls.
   Equal-color runs merge vertically; all of a color is one draw path. */
enum { MAX_RUNS=1400, MAP_REPAINT_MS=290, MOVING_MAP_REPAINT_MS=700, FULL_DETAIL_IDLE_MS=480 };
typedef struct { int x,z,w,h,next; uint8_t color; } Run;
typedef struct { char *str; size_t length,cap; } Writer;
typedef struct { int x,z,size; } Board;
static Run runs[MAX_RUNS];
static int palette_heads[256],rows[MAP_SIDE];
static uint64_t displayed,last_paint,last_motion;
static int screen_w,screen_h,screen_stick,shown,paint_detail;
static MapMarker last_marker;

static void clear_canvas(AutoTree root) {
    ccd_ui_id(root,"navigationPaper").sub=tree_newarr();
    ccd_ui_id(root,"navigationMarker").sub=tree_newarr();
}

void map_ui_reset(AutoTree root) {
    displayed=last_paint=last_motion=0; shown=screen_w=screen_h=screen_stick=paint_detail=0;
    last_marker=(MapMarker){0};
    clear_canvas(root);
}
static int push(Writer *w,const char *format,...) {
    if (w->length>=w->cap) return 0;
    va_list args;
    va_start(args,format);
    int n=vsnprintf(w->str+w->length,w->cap-w->length,format,args);
    va_end(args);
    if (n<0 || (size_t)n>=w->cap-w->length) return 0;
    w->length+=(size_t)n;
    return 1;
}
static Board bounds(int w,int h,int stick) {
    int edge=(int)fminf(328.f,fminf(w*.54f,h*.46f));
    int left=stick+10,right=w-78; /* sizing of the two CCDUI controls */
    if (right-left>=32 && edge>right-left) edge=right-left;
    if (edge<32) edge=32;
    int x=(w-edge)/2;
    if (right-left>=edge) {
        if (x<left) x=left;
        if (x+edge>right) x=right-edge;
    }
    int bottom=(int)fminf(h*.27f,fmaxf(83.f,h*.12f));
    return (Board){x,h-bottom-edge,edge};
}
static int path_runs(MapView map,int step) {
    for (int i=0;i<256;++i) palette_heads[i]=-1;
    for (int i=0;i<MAP_SIDE;++i) rows[i]=-1;
    int n=0;
    for (int z=0;z<MAP_SIDE;z+=step) for (int x=0;x<MAP_SIDE;) {
        uint8_t c=map.colors[z*MAP_SIDE+x];
        int end=x+step;
        while (end<MAP_SIDE && map.colors[z*MAP_SIDE+end]==c) end+=step;
        if (c) {
            int w=end-x,last=rows[x];
            if (last>=0 && runs[last].color==c && runs[last].w==w && runs[last].z+runs[last].h==z)
                runs[last].h+=step;
            else {
                if (n==MAX_RUNS) return -1;
                runs[n]=(Run){x,z,w,step,palette_heads[c],c};
                rows[x]=palette_heads[c]=n++;
            }
        }
        x=end;
    }
    return n;
}
static int point(int origin,int side,int cell) { return origin+(cell*side+MAP_SIDE/2)/MAP_SIDE; }
static int draw_paper(AutoTree canvas,MapView map,Board b,int detail) {
    int count=path_runs(map,detail);
    if (count<0 && detail==1) count=path_runs(map,2);
    if (count<0) count=path_runs(map,4);  /* Full-resolution map data is still saved. */
    if (count<0) return 0;
    int bx=b.x+6,bz=b.z+6,edge=b.size-12;
    Writer w={.cap=(size_t)count*170+16000};
    w.str=(char *)malloc(w.cap);
    if (!w.str) return 0;
    int ok=push(&w,"[{\"type\":\"drawRect\",\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,"
        "\"radius\":3,\"fill\":\"#C29564\",\"stroke\":\"#39281D\",\"lineWidth\":3},"
        "{\"type\":\"drawRect\",\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,\"fill\":\"#E5CB97\"}",
        b.x,b.z,b.size,b.size,bx,bz,edge,edge);
    if (detail>1) for (int i=0;i<count && ok;++i) {
        Run r=runs[i];
        int x=point(bx,edge,r.x),z=point(bz,edge,r.z),x1=point(bx,edge,r.x+r.w),z1=point(bz,edge,r.z+r.h);
        if (x<x1 && z<z1)
            ok=push(&w,",{\"type\":\"drawRect\",\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,\"fill\":\"#%06X\"}",
                x,z,x1-x,z1-z,(unsigned)map_rgb(r.color));
    }
    for (int c=1;detail==1 && c<256 && ok;++c) if (palette_heads[c]>=0) {
        ok=push(&w,",{\"type\":\"drawPath\",\"fill\":\"#%06X\",\"sub\":[",(unsigned)map_rgb((uint8_t)c));
        int first=1;
        for (int i=palette_heads[c];i>=0 && ok;i=runs[i].next) {
            Run r=runs[i];
            int x=point(bx,edge,r.x),z=point(bz,edge,r.z),x1=point(bx,edge,r.x+r.w),z1=point(bz,edge,r.z+r.h);
            if (x>=x1 || z>=z1) continue;
            ok=push(&w,"%s{\"type\":\"move\",\"x\":%d,\"y\":%d},"
                "{\"type\":\"lineTo\",\"x\":%d,\"y\":%d},"
                "{\"type\":\"lineTo\",\"x\":%d,\"y\":%d},"
                "{\"type\":\"lineTo\",\"x\":%d,\"y\":%d},{\"type\":\"close\"}",
                first ? "" : ",",x,z,x1,z,x1,z1,x,z1);
            first=0;
        }
        if (ok) ok=push(&w,"]}");
    }
    if (ok) ok=push(&w,"]");
    if (ok) canvas.sub=json_tree(w.str);
    free(w.str);
    return ok;
}
static void draw_marker(AutoTree canvas,MapMarker m,Board b) {
    AutoTree content=tree_newarr();
    if (m.visible) {
        int edge=b.size-12;
        float x=(float)point(b.x+6,edge,m.x),z=(float)point(b.z+6,edge,m.z);
        if (m.visible==2) {
            AutoTree dot=mc_ellipse(x-3.f,z-3.f,6,6);
            dot.fill="#FFFAED"; dot.stroke="#3B2B22"; dot.lineWidth=1.5;
            tree_array_append(content,dot);
        } else {
            float a=m.heading*(6.283185307f/16.f),dx=sinf(a),dz=-cosf(a);
            x=fmaxf(b.x+9.f,fminf(b.x+b.size-9.f,x)); z=fmaxf(b.z+9.f,fminf(b.z+b.size-9.f,z));
            AutoTree path=tree_newarr();
            tree_array_append(path,mc_move(x+dx*7.f,z+dz*7.f));
            tree_array_append(path,mc_line_to(x-dx*5.f-dz*4.7f,z-dz*5.f+dx*4.7f));
            tree_array_append(path,mc_line_to(x-dx*2.5f,z-dz*2.5f));
            tree_array_append(path,mc_line_to(x-dx*5.f+dz*4.7f,z-dz*5.f-dx*4.7f));
            tree_array_append(path,mc_close());
            AutoTree arrow=mc_path(path);
            arrow.fill="#FFFAED"; arrow.stroke="#3B2B22"; arrow.lineWidth=1.4;
            tree_array_append(content,arrow);
        }
    }
    canvas.sub=content;
}
void map_ui_update(AutoTree root,int w,int h,int stick,int moving,uint64_t now) {
    unsigned id=(unsigned)selected_slot<HOTBAR_SIZE ? map_item_id(inventory[selected_slot]) : 0;
    MapView map;
    if (w<64 || h<80 || !map_view(id,&map)) {
        if (shown) {
            clear_canvas(root);
            shown=0; displayed=0; last_paint=last_motion=0;
        }
        return;
    }
    Board b=bounds(w,h,stick);
    MapMarker marker=map_marker(map);
    int changed=!shown || screen_w!=w || screen_h!=h || screen_stick!=stick;
    if (moving) last_motion=now;
    int detail=last_motion && now-last_motion<FULL_DETAIL_IDLE_MS ? 2 : 1;
    unsigned interval=detail==2 ? MOVING_MAP_REPAINT_MS : MAP_REPAINT_MS;
    /* Don't replace a detailed, otherwise unchanged map when walking starts.
       Repaint the new terrain at lower density, then refine once stationary.
       The game layout owns the canvases' frames, including while hidden. */
    if (changed || ((displayed!=map.revision || (detail==1 && paint_detail==2)) && now-last_paint>=interval)) {
        AutoTree p=ccd_ui_id(root,"navigationPaper");
        if (draw_paper(p,map,b,detail)) { displayed=map.revision; paint_detail=detail; }
        last_paint=now;
    }
    if (changed || marker.x!=last_marker.x || marker.z!=last_marker.z ||
        marker.heading!=last_marker.heading || marker.visible!=last_marker.visible) {
        AutoTree p=ccd_ui_id(root,"navigationMarker");
        draw_marker(p,marker,b);
        last_marker=marker;
    }
    screen_w=w; screen_h=h; screen_stick=stick; shown=1;
}
