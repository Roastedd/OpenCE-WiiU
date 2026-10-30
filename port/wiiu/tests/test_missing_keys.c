/* Exercise the production queue/state machine with real files and controlled syscall faults. */
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
static _Thread_local uint32_t thread_token = 1;
static int io_calls, max_write, fail_call, short_write, interrupt_write, io_allowed = 1, reports;
static int last_op, fault;
enum { OP_OPEN=1, OP_WRITE, OP_SYNC, OP_CLOSE, OP_STAT, OP_RENAME, OP_OPENDIR, OP_READDIR, OP_CLOSEDIR };
static pthread_mutex_t hold_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t hold_condition = PTHREAD_COND_INITIALIZER;
static int hold_kind, held, released;
static void hold(int kind)
{
    pthread_mutex_lock(&hold_lock);
    if (hold_kind == kind) {
        held = 1; pthread_cond_broadcast(&hold_condition);
        while (!released) pthread_cond_wait(&hold_condition,&hold_lock);
    }
    pthread_mutex_unlock(&hold_lock);
}
static int operation(int op)
{
    io_calls++; last_op = op;
    if (op == OP_WRITE) hold(3);
    if (fault == op && fail_call && --fail_call == 0) { errno = EIO; return 1; }
    return 0;
}
static int mock_open(const char *path,int flags,int mode) { return operation(OP_OPEN) ? -1 : open(path,flags,mode); }
static ssize_t mock_write(int fd,const void *data,size_t bytes)
{
    if ((int)bytes > max_write) max_write = (int)bytes;
    if (operation(OP_WRITE)) return -1;
    if (interrupt_write) { interrupt_write = 0; errno = EINTR; return -1; }
    if (short_write && bytes > (size_t)short_write) bytes = (size_t)short_write;
    return write(fd,data,bytes);
}
static int mock_sync(int fd) { return operation(OP_SYNC) ? -1 : fsync(fd); }
static int mock_close(int fd) { int bad = operation(OP_CLOSE), result = close(fd); return bad ? -1 : result; }
static int mock_stat(const char *path,struct stat *info) { return operation(OP_STAT) ? -1 : stat(path,info); }
static int mock_rename(const char *a,const char *b) { return operation(OP_RENAME) ? -1 : rename(a,b); }
static DIR *mock_opendir(const char *path) { return operation(OP_OPENDIR) ? NULL : opendir(path); }
static struct dirent *mock_readdir(DIR *dir) { return operation(OP_READDIR) ? NULL : readdir(dir); }
static int mock_closedir(DIR *dir) { int bad=operation(OP_CLOSEDIR),result=closedir(dir); return bad ? -1 : result; }
#define open mock_open
#define write mock_write
#define fsync mock_sync
#define close mock_close
#define stat(...) mock_stat(__VA_ARGS__)
#define rename mock_rename
#define opendir mock_opendir
#define readdir mock_readdir
#define closedir mock_closedir
#define WIIU_MISSING_PUBLISH_HOOK() hold(1)
#define WIIU_MISSING_OBSERVE_HOOK() hold(4)
#define WIIU_MISSING_CLAIM_HOOK() hold(2)
#include "../src/wiiu_missing_keys.c"
#undef open
#undef write
#undef fsync
#undef close
#undef stat
#undef rename
#undef opendir
#undef readdir
#undef closedir

#ifdef WIIU_MISSING_CLOSE_FIXTURE
static uint32_t foreground = 1, close_waited;
static void sleep_ms(uint32_t ms) { close_waited += ms; }
#include WIIU_MISSING_CLOSE_FIXTURE
#endif

#ifdef WIIU_MISSING_PRODUCER_FIXTURE
#include WIIU_MISSING_PRODUCER_FIXTURE
#endif

#ifdef WIIU_MISSING_ACTIVITY_FIXTURE
#include WIIU_MISSING_ACTIVITY_FIXTURE
#endif

static char directory[200];
static uint32_t token(void *context) { (void)context; return thread_token; }
static int allowed(void *context) { (void)context; return io_allowed; }
static void report(void *context,const char *text) { (void)context; CHECK(strlen(text) < 240); reports++; }
static void reset(void)
{
    wiiu_missing_keys_config config = {0};
    char template[] = "/tmp/opence-missing-XXXXXX";
    memset(&capture,0,sizeof(capture));
    CHECK(mkdtemp(template)); snprintf(directory,sizeof(directory),"%s",template);
    config.directory = directory; config.run_high=0x12345678; config.run_low=0xabcdef01;
    config.thread_id=token; config.io_allowed=allowed; config.report=report;
    CHECK(!wiiu_missing_keys_start(&config)); CHECK(wiiu_missing_keys_start(&config)==EBUSY);
    CHECK(load(&capture.io_gate) == (ACCEPTING | GAMEPLAY));
    wiiu_missing_keys_gameplay(0);
    thread_token=1; io_calls=max_write=fail_call=short_write=interrupt_write=fault=reports=0; io_allowed=1;
    hold_kind=held=released=0;
}
static void cleanup(void)
{
    DIR *dir=opendir(directory); struct dirent *entry; char path[512];
    CHECK(dir);
    if (capture.descriptor>=0) { close(capture.descriptor); capture.descriptor=-1; }
    if (capture.scan) { closedir(capture.scan); capture.scan=NULL; }
    while ((entry=readdir(dir))) if (strcmp(entry->d_name,".") && strcmp(entry->d_name,"..")) {
        snprintf(path,sizeof(path),"%s/%s",directory,entry->d_name); CHECK(!unlink(path));
    }
    CHECK(!closedir(dir)); CHECK(!rmdir(directory));
}
static int step(void)
{
    int before=io_calls, work;
    thread_token=2; work=wiiu_missing_keys_service(NULL); CHECK(io_calls-before<=1); CHECK(max_write<=4096); return work;
}
static wiiu_missing_keys_counts counts(void) { wiiu_missing_keys_counts c; wiiu_missing_keys_counts_get(&c); return c; }
static void finish(void)
{
    unsigned turns;
    wiiu_missing_keys_stop();
    for (turns=0;turns<100000;turns++) {
        wiiu_missing_keys_counts c=counts();
        if (c.drained || (c.failed && !c.queue_pending && !c.batch_records && c.phase==FAILED)) break;
        step();
    }
    CHECK(turns<100000);
}
static void pixel(unsigned n)
{
    unsigned char wire[256]; unsigned i; thread_token=1;
    for (i=0;i<256;i++) wire[i]=(unsigned char)(i+n);
    CHECK(wiiu_missing_keys_pixel(0xfeed0000,n,wire,sizeof(wire)));
    memset(wire,0xcc,sizeof(wire)); /* queue must own the original wire */
}
static size_t read_batch(unsigned n,char *text,size_t room)
{
    char path[512]; FILE *file; size_t bytes;
    snprintf(path,sizeof(path),"%s/shader-misses-12345678abcdef01-%04u.txt",directory,n);
    file=fopen(path,"rb"); CHECK(file); bytes=fread(text,1,room-1,file); CHECK(!ferror(file)); CHECK(!fclose(file)); text[bytes]=0; return bytes;
}
static void test_format_and_wrap(void)
{
    char actual[20000],expected[20000]; uint32_t words[544],copy[544]; size_t used=0; unsigned i,round;
    reset(); pixel(3);
    for (i=0;i<544;i++) words[i]=copy[i]=0x80706050u+i;
    /* Lookup identity deliberately differs from the raw program identity printed in V records. */
    thread_token=1; CHECK(wiiu_missing_keys_vertex(0x0badcafe,0x12345678,0x11223344,0x55667788,0x12345678,136,words));
    memset(words,0,sizeof(words));
    expected[used++]='P'; expected[used++]=' ';
    for (i=0;i<256;i++) used+=(size_t)sprintf(expected+used,"%02x",(i+3)&255);
    expected[used++]='\n'; used+=(size_t)sprintf(expected+used,"V 1122334455667788 %u 136",0x12345678u);
    for (i=0;i<544;i++) used+=(size_t)sprintf(expected+used," %08x",copy[i]);
    expected[used++]='\n'; expected[used]=0;
    while (counts().written<2) { CHECK(!counts().failed); step(); }
    CHECK(read_batch(1,actual,sizeof(actual))==used); CHECK(!memcmp(actual,expected,used+1));
    for (round=0;round<4;round++) {
        for (i=0;i<40;i++) pixel(i+round);
        while (counts().queue_pending || counts().batch_records || counts().phase!=IDLE) { CHECK(!counts().failed); step(); }
    }
    finish(); CHECK(counts().complete && counts().written==162); CHECK(capture.read_sequence>128); CHECK(!counts().full);
    CHECK(max_write==4096 && reports==0); cleanup();
}
#ifdef WIIU_MISSING_PRODUCER_FIXTURE
static void test_actual_producer(void)
{
    uint32_t words[544]; struct dk_vertex_key vertex; struct nv2a_pixel_shader_key pixel_key;
    unsigned char wire[256]; char actual[20000],expected[20000]; uint64_t lookup; size_t used; unsigned i;
    reset(); program_count=seen_count=0;
    for(i=0;i<544;i++) words[i]=0x12345678u+i;
    vertex.program_hash=0x1122334455667788ull; vertex.packed_mask=0xffffffffu;
    dk_shader_program_note(vertex.program_hash,words,136); CHECK(program_count==1);
    dk_shader_program_note(vertex.program_hash,words,136); CHECK(program_count==1);
    dk_shader_program_note(1,words,137); CHECK(program_count==1);
    lookup=shader_vertex_key(vertex.program_hash,vertex.packed_mask); CHECK(lookup!=vertex.program_hash);
    memset(&pixel_key,0x42,sizeof(pixel_key)); shader_key_write(&pixel_key,wire);
    missing_note(DK_SHADER_STAGE_VERTEX,lookup,&vertex);
    /* This pair collided under old hash^stage identity; both records must now survive. */
    missing_note(DK_SHADER_STAGE_PIXEL,lookup^1,&pixel_key);
    missing_note(DK_SHADER_STAGE_VERTEX,lookup,&vertex); CHECK(counts().accepted==2 && seen_count==2);
    thread_token=3; missing_note(DK_SHADER_STAGE_PIXEL,7,&pixel_key); CHECK(seen_count==2);
    thread_token=1; vertex.program_hash++; missing_note(DK_SHADER_STAGE_VERTEX,8,&vertex);
    CHECK(counts().unavailable==1);
    used=(size_t)sprintf(expected,"V 1122334455667788 %u 136",0xffffffffu);
    for(i=0;i<544;i++) used+=(size_t)sprintf(expected+used," %08x",words[i]);
    expected[used++]='\n'; expected[used++]='P'; expected[used++]=' ';
    for(i=0;i<256;i++) used+=(size_t)sprintf(expected+used,"%02x",wire[i]);
    expected[used++]='\n'; expected[used]=0;
    memset(words,0,sizeof(words)); memset(&pixel_key,0,sizeof(pixel_key));
    finish(); CHECK(read_batch(1,actual,sizeof(actual))==used && !memcmp(actual,expected,used+1));
    CHECK(counts().drained && !counts().complete); free(programs[0].words); programs[0].words=NULL; cleanup();
}
#endif
static void test_sequence_rollover(void)
{
    unsigned i; reset(); capture.write_sequence=capture.read_sequence=UINT32_MAX-31u;
    for(i=0;i<64;i++) pixel(i);
    CHECK(counts().queue_pending==64 && capture.write_sequence==32u);
    finish(); CHECK(counts().complete && counts().written==64 && capture.read_sequence==32u); cleanup();
}
static void test_capacity_and_ownership(void)
{
    unsigned i; unsigned char wire[256]={0}; uint32_t words[4]={0}; wiiu_missing_keys_counts c;
    reset(); for (i=0;i<64;i++) pixel(i);
    thread_token=1; CHECK(!wiiu_missing_keys_pixel(1,2,wire,sizeof(wire))); CHECK(counts().full==1);
    thread_token=3; CHECK(!wiiu_missing_keys_pixel(1,2,wire,sizeof(wire))); CHECK(counts().rejected_producer==1);
    thread_token=1; CHECK(!wiiu_missing_keys_vertex(1,2,3,4,5,137,words));
    CHECK(!wiiu_missing_keys_vertex(1,2,3,4,5,0,words)); CHECK(!wiiu_missing_keys_pixel(1,2,wire,255));
    step(); thread_token=3; CHECK(!wiiu_missing_keys_service(NULL)); CHECK(counts().rejected_consumer==1);
    finish(); c=counts(); CHECK(c.drained && !c.complete && c.written==64 && c.unavailable==3);
    CHECK(!wiiu_missing_keys_pixel(1,2,wire,sizeof(wire))); CHECK(counts().unavailable==4 && !counts().complete); cleanup();
}
static void test_short_and_home(void)
{
    unsigned char wire[256]={0}; unsigned turns=0; int before;
    reset(); pixel(1); short_write=1; interrupt_write=1;
    while (counts().written!=1) {
        CHECK(++turns<2000 && !counts().failed); before=io_calls;
        wiiu_missing_keys_suspend(); io_allowed=0;
        CHECK(!step() && io_calls==before);
        io_allowed=1; CHECK(!step() && io_calls==before); /* resume gate is explicit */
        wiiu_missing_keys_resume(); step();
    }
    finish(); CHECK(counts().complete); wiiu_missing_keys_quiesce(); wiiu_missing_keys_resume();
    thread_token=1; CHECK(!wiiu_missing_keys_pixel(1,2,wire,sizeof(wire))); before=io_calls;
    CHECK(!step() && io_calls==before); CHECK(counts().io_gate==QUIESCED); cleanup();
}
static void test_failures(void)
{
    int op;
    for (op=OP_OPEN;op<=OP_CLOSEDIR;op++) {
        reset(); pixel(1); fault=op; fail_call=1;
        finish(); CHECK(counts().failed && !counts().complete && !counts().drained);
        CHECK(counts().io_errors==1 && counts().lost_io==1 && counts().last_error==EIO);
        if (op==OP_CLOSE) CHECK(counts().close_errors==1);
        CHECK(reports==0); /* Neither COMMIT nor FAILED may enqueue a summary behind main's Closed line. */
        wiiu_missing_keys_report("explicit-caller"); CHECK(reports==2); cleanup();
    }
    reset(); pixel(1); while (counts().phase!=WRITE) step();
    fault=OP_WRITE; fail_call=1; step(); CHECK(counts().phase==FAIL_CLOSE);
    fault=OP_CLOSE; fail_call=1; finish(); CHECK(counts().close_errors==1); cleanup();
}
static void test_limits_and_collision(void)
{
    char path[512]; FILE *file; unsigned i;
    reset(); pixel(1);
    snprintf(path,sizeof(path),"%s/shader-misses-12345678abcdef01-0001.txt",directory);
    file=fopen(path,"wb"); CHECK(file); fputs("original\n",file); fclose(file);
    finish(); CHECK(counts().failed && counts().last_error==EEXIST); file=fopen(path,"rb"); CHECK(fgetc(file)=='o'); fclose(file); cleanup();
    reset(); pixel(1);
    for(i=0;i<128;i++) { snprintf(path,sizeof(path),"%s/shader-misses-existing-%u.part",directory,i); file=fopen(path,"wb"); CHECK(file); fclose(file); }
    finish(); CHECK(counts().failed && counts().last_error==ENOSPC); cleanup();
    reset(); pixel(1); capture.scanned=SCAN_CAP; finish(); CHECK(counts().last_error==EFBIG); cleanup();
}
static void wait_held(void) { pthread_mutex_lock(&hold_lock); while(!held) pthread_cond_wait(&hold_condition,&hold_lock); pthread_mutex_unlock(&hold_lock); }
static void release_held(void) { pthread_mutex_lock(&hold_lock); released=1; pthread_cond_broadcast(&hold_condition); pthread_mutex_unlock(&hold_lock); }
static void *publish_thread(void *unused) { (void)unused; pixel(7); return NULL; }
static void *service_thread(void *unused) { (void)unused; step(); return NULL; }
#ifdef WIIU_MISSING_PRODUCER_FIXTURE
static void *observe_thread(void *unused)
{
    struct nv2a_pixel_shader_key pixel_key;
    (void)unused; thread_token=1; memset(&pixel_key,0x72,sizeof(pixel_key));
    missing_note(DK_SHADER_STAGE_PIXEL,0x66778899aabbccddull,&pixel_key); return NULL;
}
#endif
#ifdef WIIU_MISSING_ACTIVITY_FIXTURE
static void test_activity_states(void)
{
    unsigned i; int before;
    reset(); wiiu_missing_keys_gameplay(1); pixel(1); before=io_calls;
    for(i=0;i<100;i++) CHECK(!step()); CHECK(io_calls==before && counts().accepted==1 && !counts().written);
    game_time_initialize(); CHECK(!counts().gameplay);
    game_time_initialize_for_new_map(); CHECK(!counts().gameplay);
    main_menu_active(FALSE); game_time_start(); CHECK(counts().gameplay);
    wiiu_missing_keys_suspend(); wiiu_missing_keys_resume(); CHECK(counts().gameplay);
    for(i=0;i<100;i++) CHECK(!step()); CHECK(io_calls==before);
    /* A visible co-op pause that leaves the clock active must not admit optional file work. */
    CHECK(game_time_globals->active && !game_time_globals->paused);
    game_time_set_paused(TRUE); CHECK(!counts().gameplay && !game_time_globals->active);
    while(counts().phase!=WRITE) { CHECK(!counts().failed); step(); }
    game_time_set_paused(FALSE); CHECK(counts().gameplay && game_time_globals->active);
    before=io_calls; for(i=0;i<100;i++) CHECK(!step()); CHECK(io_calls==before);
    /* A late UI-menu flag after the clock starts must immediately become eligible. */
    main_menu_active(TRUE); CHECK(!counts().gameplay && game_time_globals->active);
    main_menu_active(FALSE); CHECK(counts().gameplay);
    game_time_end(); CHECK(!counts().gameplay);
    finish(); CHECK(counts().complete); cleanup();

    reset(); main_menu_active(TRUE); game_time_initialize(); game_time_initialize_for_new_map();
    game_time_start(); CHECK(!counts().gameplay); pixel(2);
    game_time_dispose_from_old_map(); CHECK(!counts().gameplay && !game_time_globals->initialized);
    finish(); CHECK(counts().complete); cleanup();

    reset(); game_time_initialize(); game_time_initialize_for_new_map(); main_menu_active(FALSE); game_time_start();
    pixel(3); wiiu_missing_keys_quiesce(); game_time_set_paused(TRUE); wiiu_missing_keys_resume();
    before=io_calls; CHECK(!counts().gameplay && (counts().io_gate&QUIESCED));
    for(i=0;i<100;i++) CHECK(!step()); CHECK(io_calls==before && counts().queue_pending==1); cleanup();
}
static void test_activity_admitted_turn(void)
{
    pthread_t thread; int before; unsigned i,kind;
    for(kind=2;kind<=3;kind++) {
        reset(); game_time_initialize(); game_time_initialize_for_new_map(); main_menu_active(FALSE); game_time_start();
        pixel(kind); game_time_set_paused(TRUE);
        while(counts().phase!=(kind==2 ? FORM : WRITE)) { CHECK(!counts().failed); step(); }
        hold_kind=(int)kind; CHECK(!pthread_create(&thread,NULL,service_thread,NULL)); wait_held();
        game_time_set_paused(FALSE); CHECK(counts().gameplay && (counts().io_gate&ACTIVE));
        release_held(); CHECK(!pthread_join(thread,NULL)); before=io_calls;
        /* One previously admitted claim/write may finish. Gameplay forbids every subsequent turn. */
        for(i=0;i<100;i++) CHECK(!step()); CHECK(io_calls==before && counts().batch_records==1 && !counts().written);
        game_time_set_paused(TRUE); finish(); CHECK(counts().complete); cleanup();
    }
}
#endif
static void test_stop_schedules(void)
{
    pthread_t thread; unsigned i; int before;
    reset(); hold_kind=1; CHECK(!pthread_create(&thread,NULL,publish_thread,NULL)); wait_held();
    CHECK(counts().gate==(ACCEPTING|ACTIVE)); before=io_calls; wiiu_missing_keys_stop();
    for(i=0;i<100;i++) { CHECK(!step()); CHECK(!counts().drained && counts().queue_pending==0 && counts().gate==ACTIVE); }
    CHECK(io_calls==before); release_held(); CHECK(!pthread_join(thread,NULL)); finish(); CHECK(counts().complete); cleanup();
    reset(); pixel(2); while(counts().phase!=FORM) step();
    hold_kind=2; CHECK(!pthread_create(&thread,NULL,service_thread,NULL)); wait_held();
    wiiu_missing_keys_stop();
#ifdef WIIU_MISSING_CLOSE_FIXTURE
    close_waited=0; extracted_shell_capture_close(); CHECK(close_waited==300);
#else
    wiiu_missing_keys_quiesce();
#endif
    CHECK(!counts().drained && (counts().io_gate&ACTIVE));
    release_held(); CHECK(!pthread_join(thread,NULL)); before=io_calls;
    for(i=0;i<100;i++) CHECK(!step()); CHECK(io_calls==before && counts().batch_records==1 && !counts().complete); cleanup();
    reset(); pixel(3); while(counts().phase!=WRITE) step();
    hold_kind=3; CHECK(!pthread_create(&thread,NULL,service_thread,NULL)); wait_held();
    wiiu_missing_keys_stop(); wiiu_missing_keys_quiesce(); CHECK(!counts().drained && (counts().io_gate&ACTIVE));
    release_held(); CHECK(!pthread_join(thread,NULL)); before=io_calls;
    for(i=0;i<100;i++) CHECK(!step()); CHECK(io_calls==before && !counts().drained); cleanup();
#ifdef WIIU_MISSING_CLOSE_FIXTURE
    reset(); hold_kind=1; CHECK(!pthread_create(&thread,NULL,publish_thread,NULL)); wait_held();
    wiiu_missing_keys_stop(); close_waited=0; extracted_shell_capture_close();
    CHECK(close_waited==300 && counts().gate==ACTIVE && !counts().complete);
    release_held(); CHECK(!pthread_join(thread,NULL)); before=io_calls;
    CHECK(!step() && io_calls==before && counts().queue_pending==1 && !counts().drained); cleanup();
#endif
#if defined(WIIU_MISSING_CLOSE_FIXTURE) && defined(WIIU_MISSING_PRODUCER_FIXTURE)
    reset(); seen_count=0; hold_kind=4; CHECK(!pthread_create(&thread,NULL,observe_thread,NULL)); wait_held();
    wiiu_missing_keys_stop(); close_waited=0; extracted_shell_capture_close();
    CHECK(close_waited==300 && counts().gate==ACTIVE && !counts().complete && !counts().queue_pending);
    release_held(); CHECK(!pthread_join(thread,NULL)); before=io_calls;
    CHECK(!step() && io_calls==before && counts().accepted==1 && counts().queue_pending==1 && !counts().drained); cleanup();
#endif
}
int main(void)
{
    test_format_and_wrap(); test_sequence_rollover();
#ifdef WIIU_MISSING_PRODUCER_FIXTURE
    test_actual_producer();
#endif
#ifdef WIIU_MISSING_ACTIVITY_FIXTURE
    test_activity_states(); test_activity_admitted_turn();
#endif
    test_capacity_and_ownership(); test_short_and_home(); test_failures(); test_limits_and_collision(); test_stop_schedules();
    puts("missing-key queue: exact V/P, ownership, wrap/full, fair bounded IO, faults, HOME and ACTIVE/IO-held close passed"); return 0;
}
