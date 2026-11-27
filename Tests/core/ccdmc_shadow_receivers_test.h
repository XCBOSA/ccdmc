/* Actual view/caster geometry without a GPU or SDL window. The witnesses
   start at visible receivers, travel towards the sun, and also exercise
   the compare sampler's off-axis footprint. */
static int shadow_receivers_selftest(void) {
    static const int origins[]={0,-37000,910000};
    static const float aspects[]={.59f,1.f,2.1f};
    static const float elevations[]={.10f,.40f,.90f};
    static const float distances[]={0.f,.06f,1.5f,11.f,32.f,75.f,101.f,172.f,208.f};
    unsigned witnesses=0,offscreen=0,misses=0,removed=0,failed=0;
    for (int site=0;site<3;++site) for (int heading=0;heading<11;++heading)
        for (int altitude=0;altitude<5;++altitude) for (int sunward=0;sunward<7;++sunward) {
        const int cx=origins[site],cz=-cx/2;
        const Vec3 eye={cx*CHUNK_SIZE+.5f,71.25f,cz*CHUNK_SIZE+4.5f};
        const float yaw=heading*.5712f,pitch=(altitude-2)*.64f;
        const Vec3 dir={cosf(pitch)*sinf(yaw),sinf(pitch),-cosf(pitch)*cosf(yaw)};
        const Vec3 right=unit(cross(dir,(Vec3){0,1,0})),up=cross(right,dir);
        const float azimuth=1.17f*sunward,aspect=aspects[(heading+altitude)%3];
        const Vec3 sun=unit((Vec3){sinf(azimuth),elevations[sunward%3],cosf(azimuth)});
        const Vec3 light_x=unit(cross((Vec3){0,1,0},sun)),light_y=cross(sun,light_x);
        float view[16],light[16];
        camera_matrix(view,aspect,eye,dir,right,up,1.f);
        sun_matrix(light,eye,sun);
        const ShadowReceivers volume=shadow_receivers(view,sun,cx,cz);
        const CullFrustum camera=clip_frustum(view,cx,cz,SHADOW_CHUNKS);
        const CullFrustum cast=clip_frustum(light,cx,cz,SHADOW_CHUNKS);
        failed|=volume.count<6 || volume.count>sizeof volume.plane/sizeof volume.plane[0];
        for (int d=0;d<9;++d) for (int ix=-1;ix<=1;++ix) for (int iy=-1;iy<=1;++iy) {
            const float depth=2.5f+d*7.5f;
            const float tx=ix*.995f*aspect*depth/camera_scale(),ty=iy*.995f*depth/camera_scale();
            const float t=distances[d];
            const float f=2.67f*(2.f*shadow_radius/SHADOW_SIZE);
            Vec3 q={eye.x+depth*dir.x+right.x*tx+up.x*ty+sun.x*t+f*(ix*light_x.x+iy*light_y.x),
                eye.y+depth*dir.y+right.y*tx+up.y*ty+sun.y*t+f*(ix*light_x.y+iy*light_y.y),
                eye.z+depth*dir.z+right.z*tx+up.z*ty+sun.z*t+f*(ix*light_x.z+iy*light_y.z)};
            Mesh m={.x=(int)floorf(q.x/CHUNK_SIZE),.z=(int)floorf(q.z/CHUNK_SIZE),
                .opaque_bottom=q.y-.002f,.opaque_top=q.y+.002f};
            if (abs(m.x-cx)>SHADOW_CHUNKS || abs(m.z-cz)>SHADOW_CHUNKS || q.y<0.f || q.y>WY) continue;
            misses+=!has_shadow_receiver(&volume,&m);
            offscreen+=!visible_chunk(&camera,m.x,m.z,m.opaque_bottom,m.opaque_top);
            ++witnesses;
        }
        for (int x=cx-5;x<=cx+5;++x) for (int z=cz-5;z<=cz+5;++z) {
            const Mesh m={.x=x,.z=z,.opaque_bottom=57.f,.opaque_top=65.f};
            if (visible_chunk(&cast,x,z,m.opaque_bottom,m.opaque_top)) removed+=!has_shadow_receiver(&volume,&m);
        }
    }
    failed|=!witnesses || !offscreen || !removed || misses;
    printf("MESH TEST shadow sweep: %u receiver witnesses (%u offscreen casters), %u removed sun batches, %u missed, failures=%u\n",
        witnesses,offscreen,removed,misses,failed!=0);
    return failed!=0;
}
