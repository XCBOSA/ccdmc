#ifndef VOXEL_STREAM_PRIORITY_H
#define VOXEL_STREAM_PRIORITY_H

#include <stdlib.h>

enum { FLIGHT_PREFETCH_DEPTH=7, FLIGHT_REST_TIER=6 };
static inline int stream_ring(int x,int z) {
    int ax=abs(x),az=abs(z);
    return ax>az ? ax : az;
}
/* Protect the immediate ring in every direction. Beyond it, the narrow
   forward corridor completes first; the rest still has a finite order. */
static inline int flight_stream_tier(int x,int z,int hx,int hz,int halo) {
    int ring=stream_ring(x,z);
    if (ring<=1) return ring;
    int scale=abs(hx)+abs(hz),along=x*hx+z*hz;
    if (scale && along>0 && along<=(FLIGHT_PREFETCH_DEPTH+halo)*scale) {
        int depth=(along+scale-1)/scale;
        if (abs(x*hz-z*hx)<=scale*(1+depth/3+halo))
            return 2+(depth-1)/2;
    }
    return FLIGHT_REST_TIER+ring;
}

#endif
