/* Included only for the native or explicitly staged Engine mesh suite. */
static int outside_entity_bounds(Vec3 origin,float radius) {
    if (meshOOM || !vertexCount) return 1;
    for (Uint32 i=0;i<vertexCount;++i) {
        Vec3 d={vertices[i].p[0]-origin.x,vertices[i].p[1]-origin.y,vertices[i].p[2]-origin.z};
        if (dot(d,d)>radius*radius) return 1;
    }
    return 0;
}
static int entity_view_selftest(void) {
    const Vec3 eye={0,0,0},north={0,0,-1},east={1,0,0};
    const EntityView v=entity_view(eye,north,1.f),turned=entity_view(eye,east,1.f);
    const EntityView wide=entity_view(eye,north,3.f),unknown=entity_view(eye,north,0.f);
    const float r=ACTOR_MESH_RADIUS;
    int failed= !entity_in_view(&v,(Vec3){0,0,-12},r) ||
        entity_in_view(&v,(Vec3){0,0,12},r) ||
        !entity_in_view(&v,(Vec3){0,0,1},r) ||
        !entity_in_view(&v,(Vec3){8,0,-12},r) ||
        entity_in_view(&v,(Vec3){18,0,-12},r) ||
        !entity_in_view(&wide,(Vec3){18,0,-12},r) ||
        !entity_in_view(&unknown,(Vec3){18,0,-12},r) ||
        entity_in_view(&v,(Vec3){0,18,-12},r) ||
        entity_in_view(&v,(Vec3){18,0,0},r) ||
        !entity_in_view(&turned,(Vec3){18,0,0},r);
    for (int type=1;type<ACTOR_TYPES;++type)
        for (int state=0;state<=FAULTBACK_CALM;++state)
            for (int phase=0;phase<=4;++phase) {
                ActorRecord a={{2,5,-3},phase*.67f,(uint8_t)type,16,
                    (uint8_t)((phase&3)*4+(state&3))};
                vertexCount=meshOOM=0;
                actor_model(&a,phase*.25f,state);
                draw_heart((Vec3){a.feet.x,a.feet.y+1.53f,a.feet.z});
                draw_flame(a.feet.x,a.feet.y+.50f,a.feet.z,.64f);
                failed|=outside_entity_bounds(a.feet,r);
            }
    for (int type=EQUINE_HORSE;type<EQUINE_TYPES;++type) for (int age=0;age<=1;++age) {
        HorseRecord horse={.feet={-2,5,3},.heading=.61f,.age=(uint16_t)age,
            .type=(uint8_t)type,.saddle=1,.armor={DIAMOND_HORSE_ARMOR,1,0,0,0}};
        vertexCount=meshOOM=0;
        horse_model(&horse);
        failed|=outside_entity_bounds(horse.feet,HORSE_MESH_RADIUS);
    }
    free(vertices); vertices=NULL; vertexCount=vertexCapacity=0;
    printf("MESH TEST camera turns, model bounds, and conservative viewport edges failures=%d\n",failed!=0);
    return failed;
}
