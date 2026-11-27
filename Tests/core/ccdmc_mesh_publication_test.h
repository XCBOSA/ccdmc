/* Included by renderer.c only in native and staged Engine test builds,
   so the test can exercise the real snapshot code without exposing internals. */
static int mesh_publication_progress_selftest(void) {
    const int y=192;
    if (!world_begin_options(1712,GENERATOR_CURRENT,WORLD_SUPERFLAT,1)) return 1;
    renderer_reset_world();
    meshX=meshZ=0;
    float old_x=px,old_z=pz;
    px=pz=.5f;
    Mesh shown={.x=0,.z=0,.valid=1};
    int failed=!set_block(0,y-1,0,WATER);
    mesh_revisions(0,0,shown.revision);
    unsigned long long before=world_edit_version();
    failed|=!set_block(0,y,0,LILY_PAD);
    renderer_prioritize_edits_since(before);
    if (!failed) {
        failed=prepare_mesh_with_priority(&mesh_job,&shown,0,0,2)!=MESH_QUEUED ||
            mesh_cell(&mesh_job,0,y,0)!=LILY_PAD;
    }
    if (!failed) {
        /* A neighboring edit and two updates in the same chunk overtake
           distinct snapshots. Neither may freeze the visible terrain. */
        const BlockEdit flow[]={{-1,y-1,3,FLOW_BASE+2},{5,y-1,7,FLOW_BASE+2}};
        failed|=!world_set_blocks(flow,2);
        failed|=mesh_job_current(&mesh_job) || !mesh_job_publishable(&mesh_job,&shown);
        before=world_edit_version();
        failed|=!set_block(0,y,0,AIR);
        renderer_prioritize_edits_since(before);
        failed|=mesh_job_publishable(&mesh_job,&shown);
        failed|=prepare_mesh_with_priority(&mesh_job,&shown,0,0,2)!=MESH_QUEUED ||
            mesh_cell(&mesh_job,0,y,0)!=AIR;
    }
    if (!failed) {
        const BlockEdit flow={5,y-1,7,FLOW_BASE+3};
        failed|=!world_set_blocks(&flow,1);
        failed|=mesh_job_current(&mesh_job) || !mesh_job_publishable(&mesh_job,&shown);
        mesh_revisions(0,0,shown.revision);
        failed|=mesh_job_publishable(&mesh_job,&shown);
        renderer_reset_world();
        shown.valid=0;
        failed|=mesh_job_publishable(&mesh_job,&shown) || player_edit_floor(0,0)!=0;
    }
    renderer_reset_world();
    px=old_x; pz=old_z;
    printf("MESH TEST progress under flowing water, edit order and world reset failures=%d\n",failed!=0);
    return failed;
}

static int mesh_preemption_selftest(void) {
    const int y=SUPERFLAT_SURFACE+1;
    if (mesh_running || !world_begin_options(7871,GENERATOR_CURRENT,WORLD_SUPERFLAT,1)) return 1;
    renderer_reset_world();
    const float old_x=px,old_z=pz;
    px=pz=.5f;
    meshX=meshZ=0;
    TerrainMeshJob *near=mesh_workers[0].job,*far=mesh_workers[1].job;
    Mesh *shown=&terrain_mesh[ring_index(0)][ring_index(0)];
    *shown=(Mesh){.x=0,.z=0,.valid=1};
    mesh_revisions(0,0,shown->revision);
    memcpy(near->revision,shown->revision,sizeof near->revision);
    near->cx=near->cz=0; near->epoch=mesh_epoch; near->urgent=0;
    far->cx=0; far->cz=-7; far->epoch=mesh_epoch; far->urgent=0;
    const unsigned long long before=world_edit_version();
    int failed=!set_block(0,y,0,STONE);
    renderer_prioritize_edits_since(before);
    /* Model the two slots while their workers are deliberately not
       started, so synchronization and victim selection are reproducible. */
    mesh_running=2;
    int states[MESH_WORKERS]={MESH_BAKING,MESH_PENDING};
    for (int i=0;i<MESH_WORKERS;++i) {
        mesh_workers[i].state=states[i];
        __atomic_store_n(&mesh_workers[i].cancel,0,__ATOMIC_RELAXED);
    }
    failed|=edit_in_flight(states,0,0,shown)!=0;
    yield_to_queued_edit(states);
    failed|=__atomic_load_n(&mesh_workers[0].cancel,__ATOMIC_RELAXED)!=0 || mesh_workers[1].state!=MESH_PENDING;
    /* Don't throw away useful work until the edited chunk's halo exists. */
    for (int x=-1;x<=1;++x) for (int z=-1;z<=1;++z) failed|=!chunk_view(x,z).cells;
    yield_to_queued_edit(states);
    failed|=!__atomic_load_n(&mesh_workers[0].cancel,__ATOMIC_RELAXED) || mesh_workers[1].state!=MESH_PENDING;
    failed|=preempt_mesh_for_edit(0,0)!=0;
    near->count=42;
    failed|=bake_chunk_mesh_interruptible(near,BAKE_WATER_MASK,0,&mesh_workers[0].cancel)!=0 || near->count!=42;
    __atomic_store_n(&mesh_workers[0].cancel,0,__ATOMIC_RELAXED);
    near->urgent=1;
    failed|=!preempt_mesh_for_edit(0,0) || mesh_workers[0].state!=MESH_BAKING || mesh_workers[1].state!=MESH_IDLE;
    failed|=prepare_mesh_with_priority(near,shown,0,0,1)!=MESH_QUEUED;
    failed|=!edit_in_flight(states,0,0,shown);
    for (int i=0;i<MESH_WORKERS;++i) mesh_workers[i].state=MESH_IDLE;
    mesh_running=0;
    px=old_x; pz=old_z;
    renderer_reset_world();
    printf("MESH TEST preempt only resident edits, preserve urgent work and discard partial bake failures=%d\n",failed!=0);
    return failed!=0;
}

static int mesh_relight_queue_selftest(void) {
    if (mesh_running || !world_begin_options(6073,GENERATOR_CURRENT,WORLD_SUPERFLAT,1)) return 1;
    renderer_reset_world();
    meshX=meshZ=0;
    const MeshWorker *const slot=&mesh_workers[0];
    mesh_workers[0].state=MESH_IDLE;
    Mesh *center=&terrain_mesh[ring_index(0)][ring_index(0)];
    *center=(Mesh){.x=0,.z=0,.valid=1,.relight=1};
    mesh_revisions(0,0,center->revision);
    for (int x=-1;x<=1;++x) for (int z=-1;z<=1;++z) (void)chunk_view(x,z);
    const int states[MESH_WORKERS]={MESH_IDLE,MESH_IDLE};
    int waiting=0;
    prefer_initial_mesh=0;
    int failed=!queue_terrain_mesh(&mesh_workers[0],states,&waiting) || slot->job->work!=MESH_FIRST ||
        geometry_since_light!=1;
    mesh_workers[0].state=MESH_IDLE;
    geometry_since_light=LIGHT_TURN_AFTER;
    failed|=!queue_terrain_mesh(&mesh_workers[0],states,&waiting) || slot->job->work!=MESH_ONLY_LIGHT ||
        slot->job->cx!=0 || slot->job->cz!=0 || slot->job->relight || geometry_since_light!=0;
    /* The last provisional mesh must finish even when there will never
       be six more geometry publications to consume the budget. */
    for (int x=-RENDER_CHUNKS;x<=RENDER_CHUNKS;++x) for (int z=-RENDER_CHUNKS;z<=RENDER_CHUNKS;++z) {
        if (!x && !z) continue;
        Mesh *m=&terrain_mesh[ring_index(x)][ring_index(z)];
        *m=(Mesh){.x=x,.z=z,.valid=1};
        mesh_revisions(x,z,m->revision);
    }
    mesh_workers[0].state=MESH_IDLE;
    failed|=!queue_terrain_mesh(&mesh_workers[0],states,&waiting) ||
        slot->job->work!=MESH_ONLY_LIGHT || slot->job->relight;
    mesh_workers[0].state=MESH_IDLE;
    const float old_x=px,old_z=pz;
    px=pz=.5f;
    const unsigned long long before=world_edit_version();
    failed|=!set_block(0,200,0,STONE);
    renderer_prioritize_edits_since(before);
    geometry_since_light=LIGHT_TURN_AFTER;
    failed|=!queue_terrain_mesh(&mesh_workers[0],states,&waiting) || !slot->job->urgent ||
        slot->job->work!=MESH_GEOMETRY || slot->job->cx || slot->job->cz;
    mesh_workers[0].state=MESH_IDLE;
    px=old_x; pz=old_z;
    renderer_reset_world();
    printf("MESH TEST first draw / fair exact light / idle drain / immediate player edit failures=%d\n",failed!=0);
    return failed!=0;
}

static int mesh_build_shapes_selftest(void) {
    int failed=0;
    const Vec3 place={-16.f,61.f,-1.f};
    const unsigned alongside[]={0,15u,0,0,0,0};
    for (int top=0;top<2;++top) {
        const unsigned mask=top ? 240u : 15u;
        vertexCount=meshOOM=0;
        shape_mesh(place,1.f,BRICK_CELL,mask,NULL);
        failed|=vertexCount!=36 || meshOOM;
        for (unsigned i=0;i<vertexCount;++i) {
            const Vertex *v=&vertices[i];
            const int tile=(int)(v->uv[0]*ATLAS_UV_WIDTH/TILE_EDGE);
            failed|=v->p[1]<61.f+top*.5f || v->p[1]>61.5f+top*.5f || tile!=BUILD_BRICK_TILE;
        }
    }
    vertexCount=meshOOM=0;
    shape_mesh(place,1.f,STONE,15,alongside);
    failed|=vertexCount!=30 || meshOOM;
    const int a=build_stair_raw(BUILD_OAK,EAST,0),b=build_stair_raw(BUILD_BRICK,SOUTH,0),neighbor[]={AIR,b,AIR,AIR};
    const unsigned corner=build_mask(a,neighbor);
    vertexCount=meshOOM=0;
    shape_mesh(place,1.f,PLANKS,corner,NULL);
    failed|=!vertexCount || meshOOM || corner!=143u;
    for (unsigned k=0;k<vertexCount;++k) {
        const Vertex *v=&vertices[k];
        failed|=v->p[1]>61.5f && (v->p[0]<-15.5f || v->p[2]<-.5f);
    }
    free(vertices); vertices=NULL; vertexCount=vertexCapacity=0;
    printf("MESH TEST slabs, culled seams and stair corner occupancy failures=%d\n",failed!=0);
    return failed!=0;
}

static int mesh_wall_attachment_selftest(void) {
    int failed=0;
    const int neighbor[4]={AIR,AIR,AIR,AIR};
    for (int mode=0;mode<4;++mode) for (int facing=0;facing<4;++facing) {
        int raw=mode==0 ? LADDER+facing : WOOD_TRAPDOOR+facing+
            (mode==1 ? 0 : TRAPDOOR_TOP|(mode==3 ? TRAPDOOR_OPEN : 0));
        vertexCount=meshOOM=0;
        draw_barrier(0,0,0,raw,neighbor);
        failed|=vertexCount!=216 || meshOOM;
        for (unsigned i=0;i<vertexCount;++i) {
            const float *v=vertices[i].p;
            float normal=facing==NORTH ? v[2] : facing==SOUTH ? 1-v[2] : facing==WEST ? v[0] : 1-v[0];
            if (mode==0 || mode==3) failed|=normal<-.0001f || normal>(mode==0 ? .1271f : .1876f);
            else failed|=mode==1 ? v[1]>.1876f : v[1]<.8124f;
        }
        if (mode==0 && vertexCount==216)
            for (int k=0;k<4;++k) {
                float top=0,low=1;
                for (int i=(k+2)*36;i<(k+3)*36;++i) {
                    top=fmaxf(top,vertices[i].p[1]); low=fminf(low,vertices[i].p[1]);
                }
                failed|=fabsf(low-(.095f+k*.2375f))>.0001f || fabsf(top-low-.098f)>.0001f;
            }
    }
    free(vertices); vertices=NULL; vertexCount=vertexCapacity=0;
    printf("MESH TEST trapdoor halves, all hinges and four ladder rungs failures=%d\n",failed!=0);
    return failed!=0;
}

static int mesh_redstone_horizon_selftest(void) {
    const int y=SUPERFLAT_SURFACE+1;
    const int wires[][2]={{5,2},{6,2},{7,2},{8,2},{9,2},{10,4},{10,5},{10,6},{10,7},{10,8},{10,9},
        {9,9},{8,9},{5,9},{4,9},{3,9},{3,8},{3,7},{3,6},{3,5},{3,4},{3,3}};
    if (!world_begin_options(1712,GENERATOR_CURRENT,WORLD_SUPERFLAT,1) ||
        !set_block(3,y,2,STONE) || !set_block(10,y,2,STONE) || !set_block(7,y,9,STONE) ||
        !redstone_place_torch((CircuitPos){4,y,2},(CircuitPos){3,y,2}) ||
        !redstone_place_torch((CircuitPos){10,y,3},(CircuitPos){10,y,2}) ||
        !redstone_place_torch((CircuitPos){6,y,9},(CircuitPos){7,y,9})) return 1;
    for (unsigned i=0;i<sizeof wires/sizeof *wires;++i)
        if (!redstone_place_wire(wires[i][0],y,wires[i][1])) return 1;
    renderer_reset_world();
    meshX=meshZ=0;
    float old_yaw=yaw;
    yaw=0.f;
    /* Start where the player's nearby terrain is already visible. Requests
       for the missing outer terrain still go through the real stream worker. */
    for (int x=-2;x<=2;++x) for (int z=-2;z<=2;++z) {
        (void)chunk_view(x,z);
        Mesh *m=&terrain_mesh[ring_index(x)][ring_index(z)];
        *m=(Mesh){.x=x,.z=z,.valid=1};
        mesh_revisions(x,z,m->revision);
    }
    int failed=!world_stream_start() || !renderer_start_meshing();
    if (!failed) {
        /* This first draw has emitters, but the light worker has yet to
           finish its first solve. The geometry must be eligible anyway. */
        const Mesh unseen={0};
        failed=prepare_mesh(&mesh_job,&unseen,0,0)!=MESH_QUEUED || !mesh_job.relight;
    }
    world_set_interactive(1);
    const struct timespec delay={0,2000000};
    int prev=0,edges=0,center=0,horizon=0,provisional=0;
    unsigned long long initial_revision=chunk_revision(0,0);
    for (int tick=0;tick<1900 && !failed && !(horizon && center>=3 && edges>=4);++tick) {
        world_stream_pump(6,8);
        redstone_step(6,8);
        int power=redstone_wire_power(8,y,2)>0;
        if (tick>0) edges+=power!=prev;
        prev=power;
        int states[MESH_WORKERS]={0};
        MeshWorker *idle=NULL;
        pthread_mutex_lock(&mesh_lock);
        for (int i=0;i<mesh_running;++i) states[i]=mesh_workers[i].state;
        pthread_mutex_unlock(&mesh_lock);
        for (int i=0;i<mesh_running;++i) {
            MeshWorker *w=&mesh_workers[i];
            if (states[i]==MESH_READY && claim_mesh(w)==MESH_READY) {
                states[i]=MESH_IDLE;
                TerrainMeshJob *job=w->job;
                Mesh *m=&terrain_mesh[ring_index(job->cx)][ring_index(job->cz)];
                if (job->failed) { failed=1; break; }
                if (mesh_job_publishable(job,m)) {
                    if (job->cx==0 && job->cz==0 && job->revision[0]>m->revision[0]) ++center;
                    if (job->cz<=-4 && abs(job->cx)<=2 && job->count) ++horizon;
                    if (!m->valid && job->relight) ++provisional;
                    m->x=job->cx; m->z=job->cz; m->valid=1; m->relight=job->relight;
                    memcpy(m->revision,job->revision,sizeof m->revision);
                }
            }
            if (states[i]==MESH_IDLE && !idle) idle=w;
        }
        int pending=0;
        if (idle && !failed) (void)queue_terrain_mesh(idle,states,&pending);
        nanosleep(&delay,NULL);
    }
    failed|=!horizon || center<3 || edges<4 || chunk_revision(0,0)<=initial_revision;
    printf("MESH TEST live three-torch clock edges=%d center updates=%d outer meshes=%d provisional=%d failures=%d\n",
        edges,center,horizon,provisional,failed!=0);
    renderer_stop_meshing();
    world_stream_stop();
    renderer_reset_world();
    yaw=old_yaw;
    return failed;
}
