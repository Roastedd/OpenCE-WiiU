#!/usr/bin/env python3
"""Production-derived decal recovery checks, including deterministic late invalidation."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
data=(root/'source/memory/data.c').read_text()
decals=(root/'source/effects/decals.c').read_text()
raster=(root/'source/rasterizer/xbox/rasterizer_xbox_decals.c').read_text()
def function(source,name):
 m=re.search(r'^[\w *]+?\b'+re.escape(name)+r'\(\s*[^;{]*\)\s*\{',source,re.M)
 if not m:raise LookupError(name)
 i=source.index('{',m.start())+1;depth=1
 while depth:
  depth+=(source[i]=='{')-(source[i]=='}');i+=1
 return source[m.start():i]
def block(source,start):
 a=source.index('{',start);i=a+1;depth=1
 while depth:
  depth+=(source[i]=='{')-(source[i]=='}');i+=1
 return source[start:i]
strict=function(data,'datum_delete').replace('void datum_delete(', 'static void datum_delete_body(',1)
conditional=function(data,'datum_delete_if_alive')
reset=function(data,'data_delete_all').replace('void data_delete_all(', 'static void data_delete_all_body(',1)
strict_get=function(data,'datum_get')
cache_reset=function(raster,'rasterizer_decals_reset_cache')
try_link=function(decals,'decal_try_get_link')
request=function(decals,'decals_request_cache_repair')
pending=function(decals,'decals_cache_repair_pending')
repair=function(decals,'decals_repair_cache')
delete=function(decals,'decal_delete')
a=decals.index('static long decal_insert(\n\tlong cache_index')
insert=block(decals,decals.index('if (next_decal_index != NONE)',a))
fixture=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#define HALO_WIIU
#define TRUE 1
#define FALSE 0
#define NONE -1
#define MAXIMUM_CLUSTERS_PER_STRUCTURE 4
#define NUMBER_OF_DECAL_LAYERS 2
#define match_assert(file,line,expr) assert(expr)
#define match_vassert(file,line,expr,message) assert(expr)
#define _decal_locked_bit 0
#define _decal_permanent_bit 1
#define TEST_FLAG(v,b) ((v)&(1u<<(b)))
typedef int boolean;
typedef unsigned char byte;
struct datum_header {short identifier;};
struct decal_datum {short identifier;unsigned short flags;long previous_decal_index,next_decal_index;short cluster_index,layer;};
struct data_array {char name[32];int valid,identifier_zero_invalid;short maximum_count,count,actual_count,first_free_absolute_index,next_identifier;long size;void *data;};
struct decal_globals {long first_decal_indices[NUMBER_OF_DECAL_LAYERS][MAXIMUM_CLUSTERS_PER_STRUCTURE];long first_disconnected_decal_index;short locked_count,permanent_count;};
struct lruv_cache {struct data_array*blocks;long first_block_index,last_block_index,tick;void(*delete_block_proc)(long);long page_count,page_size_bits;};
static struct decal_datum owners[4];
static struct datum_header blocks[4],other_slots[4];
static struct data_array owner_pool,block_pool,other_pool;
static struct data_array *global_decal_data=&owner_pool;
static struct decal_globals globals,*decal_globals=&globals;
static struct {int decal_vertex_count,decal_surface_count;}decal_geometry;
static struct lruv_cache cache,*local_vertex_cache=&cache;
static int decal_batch_count,callback_calls,log_calls,lock_depth,locks,unlocks,replace_at_lock,in_callback;
void decals_repair_cache(void);
static unsigned char vertex_buffer[64];
static boolean decal_cache_repair_requested;
static unsigned long decal_cache_repairs;
static unsigned decal_late_delete_reports;
static long handle(int slot,unsigned short id){return ((long)id<<16)|slot;}
static long A,B,C;
static void data_verify(struct data_array *p){assert(p && p->data && p->size>0);}
static void allocator_lock(void){assert(lock_depth==0);lock_depth=1;locks++;if(replace_at_lock==1)owners[0].identifier=(short)0x5678;else if(replace_at_lock==2)owners[0].identifier=0;else if(replace_at_lock==3)owner_pool.count=0;replace_at_lock=0;}
static void allocator_unlock(void){assert(lock_depth==1);lock_depth=0;unlocks++;}
#define DATA_LOCK() allocator_lock()
#define DATA_UNLOCK() allocator_unlock()
static void csmemset(void*p,int v,size_t n){memset(p,v,n);}
static void platform_log(const char*fmt,...){(void)fmt;log_calls++;}
static void callback(long index){(void)index;callback_calls++;}
static void init_pool(struct data_array*p,const char*name,void*memory,long size){memset(p,0,sizeof(*p));snprintf(p->name,sizeof(p->name),"%s",name);p->valid=1;p->identifier_zero_invalid=1;p->maximum_count=4;p->count=p->actual_count=3;p->first_free_absolute_index=3;p->data=memory;p->size=size;}
'''
try_get=function(data,'datum_try_and_get')
get=r'''
#define DECAL_GET(index) ((struct decal_datum*)datum_get(global_decal_data,index))
static void decal_set_first_decal_index(short cluster,short layer,long index){assert(cluster>=0&&cluster<4&&layer>=0&&layer<2);decal_globals->first_decal_indices[layer][cluster]=index;}
'''
setup=r'''
static void setup(void){
 memset(owners,0,sizeof(owners));memset(blocks,0,sizeof(blocks));memset(other_slots,0,sizeof(other_slots));
 memset(&globals,0,sizeof(globals));memset(globals.first_decal_indices,0xff,sizeof(globals.first_decal_indices));globals.first_disconnected_decal_index=NONE;
 init_pool(&owner_pool,"decals",owners,sizeof(owners[0]));init_pool(&block_pool,"cache",blocks,sizeof(blocks[0]));init_pool(&other_pool,"objects",other_slots,sizeof(other_slots[0]));
 A=handle(0,0x1234);B=handle(1,0x2345);C=handle(2,0x3456);
 owners[0].identifier=(short)0x1234;owners[1].identifier=(short)0x2345;owners[2].identifier=(short)0x3456;
 for(int i=0;i<3;i++){owners[i].previous_decal_index=owners[i].next_decal_index=NONE;owners[i].cluster_index=0;owners[i].layer=0;blocks[i].identifier=(short)(10+i);other_slots[i].identifier=(short)(20+i);}
 cache=(struct lruv_cache){&block_pool,A,C,10,callback,128,9};
 globals.first_decal_indices[0][0]=A;
 callback_calls=log_calls=lock_depth=locks=unlocks=replace_at_lock=in_callback=0;
 decal_cache_repair_requested=0;decal_cache_repairs=0;decal_batch_count=3;
 decal_geometry.decal_vertex_count=33;decal_geometry.decal_surface_count=11;
 memset(vertex_buffer,0x7e,sizeof(vertex_buffer));
}
static void late_invalidation(void){
 setup();assert(datum_try_and_get(&owner_pool,A)==&owners[0]);replace_at_lock=1;
 assert(!datum_delete_if_alive(&owner_pool,A));assert(owners[0].identifier==(short)0x5678);
 assert(owner_pool.actual_count==3 && owner_pool.count==3 && locks==1 && unlocks==1 && !lock_depth);
 setup();assert(datum_try_and_get(&owner_pool,A));replace_at_lock=2;
 assert(!datum_delete_if_alive(&owner_pool,A)&&!owners[0].identifier&&owner_pool.actual_count==3);
 setup();assert(datum_try_and_get(&owner_pool,A));replace_at_lock=3;
 assert(!datum_delete_if_alive(&owner_pool,A)&&owners[0].identifier==(short)0x1234&&owner_pool.count==0);
}
static void valid_and_stale(void){
 setup();assert(datum_delete_if_alive(&owner_pool,B));assert(!owners[1].identifier&&owner_pool.actual_count==2&&owner_pool.first_free_absolute_index==1);
 assert(!datum_delete_if_alive(&owner_pool,B));assert(owner_pool.actual_count==2);
 owners[1].identifier=(short)0x6789;owner_pool.actual_count++;
 assert(!datum_delete_if_alive(&owner_pool,B));assert(owners[1].identifier==(short)0x6789&&owner_pool.actual_count==3);
 assert(!datum_delete_if_alive(NULL,A));assert(!datum_delete_if_alive(&owner_pool,NONE));assert(!datum_delete_if_alive(&owner_pool,0));
 owner_pool.valid=0;assert(!datum_delete_if_alive(&owner_pool,A));assert(!lock_depth&&locks==unlocks);
 /* The original ordinary deletion body still handles a valid datum. */
 setup();datum_delete_body(&owner_pool,C);assert(!owners[2].identifier&&owner_pool.actual_count==2&&owner_pool.count==2);
}
static void links(void){
 setup();owners[0].next_decal_index=B;owners[1].previous_decal_index=A;
 decal_delete(A);assert(!decals_cache_repair_pending());assert(!owners[0].identifier&&owners[1].previous_decal_index==NONE&&globals.first_decal_indices[0][0]==B);
 setup();owners[0].previous_decal_index=A;owners[0].next_decal_index=A;
 decal_delete(A);assert(decals_cache_repair_pending());assert(owners[0].identifier==(short)0x1234);
 setup();owners[0].previous_decal_index=B;owners[0].next_decal_index=B;owners[1].previous_decal_index=A;owners[1].next_decal_index=A;
 decal_delete(A);assert(decals_cache_repair_pending());assert(owners[0].identifier==(short)0x1234);
 setup();owners[0].previous_decal_index=B;owners[0].next_decal_index=C;owners[1].next_decal_index=A;owners[2].previous_decal_index=A;
 decal_delete(A);assert(decals_cache_repair_pending());assert(owners[0].identifier==(short)0x1234);
 setup();owners[0].next_decal_index=handle(1,0x7777);
 decal_delete(A);assert(decals_cache_repair_pending());assert(owners[1].identifier==(short)0x2345);
 setup();owners[0].next_decal_index=1;decal_delete(A);assert(decals_cache_repair_pending());assert(owners[1].identifier==(short)0x2345);
 setup();decal_delete(handle(0,0x7777));assert(decals_cache_repair_pending());assert(owners[0].identifier==(short)0x1234);
}
static void deferred(void){
 setup();owners[0].previous_decal_index=A;owners[0].next_decal_index=A;
 in_callback=1;decal_delete(A);assert(decals_cache_repair_pending());
 assert(owner_pool.actual_count==3 && block_pool.actual_count==3 && cache.first_block_index==A && !callback_calls);
 in_callback=0;decals_repair_cache();assert(!owner_pool.actual_count&&!block_pool.actual_count&&!callback_calls);
}
static void strict_rejects(void){setup();owners[0].identifier=(short)0x5678;(void)datum_get(&owner_pool,A);}
static void paired(void){
 setup();struct data_array other_before=other_pool;struct datum_header before[4];memcpy(before,other_slots,sizeof(before));
 globals.locked_count=3;globals.permanent_count=2;globals.first_disconnected_decal_index=B;
 decals_request_cache_repair();assert(decals_cache_repair_pending());assert(owner_pool.actual_count==3&&block_pool.actual_count==3&&!callback_calls);
 decals_repair_cache();assert(!decals_cache_repair_pending());assert(decal_cache_repairs==1&&log_calls==1);
 assert(owner_pool.count==0&&owner_pool.actual_count==0&&block_pool.count==0&&block_pool.actual_count==0);
 for(int i=0;i<4;i++){assert(!owners[i].identifier&&!blocks[i].identifier);assert(vertex_buffer[i]==0x7e);}
 for(int layer=0;layer<2;layer++)for(int cluster=0;cluster<4;cluster++)assert(globals.first_decal_indices[layer][cluster]==NONE);
 assert(globals.first_disconnected_decal_index==NONE&&!globals.locked_count&&!globals.permanent_count);
 assert(cache.first_block_index==NONE&&cache.last_block_index==NONE&&cache.tick==1&&!decal_batch_count&&!callback_calls);
 assert(cache.blocks==&block_pool&&cache.delete_block_proc==callback&&cache.page_count==128&&cache.page_size_bits==9);
 assert(!decal_geometry.decal_vertex_count&&!decal_geometry.decal_surface_count);
 assert(!memcmp(&other_pool,&other_before,sizeof(other_pool))&&!memcmp(other_slots,before,sizeof(before)));
 decals_repair_cache();assert(decal_cache_repairs==1&&log_calls==1);
}
'''
link_function='''static void insert_before(long decal_index,long next_decal_index,short cluster_index,short layer)
{
 struct decal_datum *new_decal=DECAL_GET(decal_index);
'''+insert+'\n}\n'
insert_test=r'''
static void insertion(void){
 setup();globals.first_decal_indices[0][0]=B;
 insert_before(A,B,0,0);assert(owners[0].previous_decal_index==NONE&&owners[0].next_decal_index==B&&owners[1].previous_decal_index==A&&globals.first_decal_indices[0][0]==A);
 insert_before(C,B,0,0);assert(owners[2].previous_decal_index==A&&owners[2].next_decal_index==B&&owners[0].next_decal_index==C&&owners[1].previous_decal_index==C);
}
int main(int argc,char**argv){
 if(argc==2){if(!strcmp(argv[1],"late"))late_invalidation();else if(!strcmp(argv[1],"paired"))paired();else if(!strcmp(argv[1],"insert"))insertion();else if(!strcmp(argv[1],"deferred"))deferred();else if(!strcmp(argv[1],"strict"))strict_rejects();else return 2;return 0;}
 late_invalidation();valid_and_stale();links();paired();insertion();deferred();
 puts("Decal late-invalidation, replacement, link repair, paired reset and insertion checks passed");return 0;
}
'''
def program(mode=None):
 helper=conditional;recover=repair;insert_fn=link_function;request_fn=request
 if mode=='peek-before-lock':
  helper=helper.replace('    boolean result=FALSE;','    boolean result=FALSE;\n    void *peek=datum_try_and_get(data,index);').replace('&& datum_try_and_get(data,index))','&& peek)')
 elif mode=='one-pool-reset':recover=recover.replace('    rasterizer_decals_reset_cache();','    /* negative: LRU pool left stale */')
 elif mode=='immediate-reset':request_fn=request_fn.replace('decal_cache_repair_requested=TRUE;', 'decal_cache_repair_requested=TRUE;decals_repair_cache();')
 elif mode=='self-predecessor':insert_fn=insert_fn.replace('new_decal->previous_decal_index = next->previous_decal_index;','new_decal->previous_decal_index = decal_index;')
 return fixture+try_get+strict_get+get+strict+helper+reset+'\nvoid data_delete_all(struct data_array*p){assert(!in_callback);data_delete_all_body(p); }\n'+cache_reset+try_link+request_fn+pending+recover+delete+setup+insert_fn+insert_test
with tempfile.TemporaryDirectory() as d:
 p=Path(d)
 for mode,case in [(None,None),('peek-before-lock','late'),('one-pool-reset','paired'),('self-predecessor','insert'),('immediate-reset','deferred')]:
  name=mode or 'test';source=p/(name+'.c');exe=p/name;source.write_text(program(mode))
  subprocess.run(['cc','-std=c99','-O1','-ffp-contract=off','-fwrapv','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined',str(source),'-o',str(exe)],check=True)
  result=subprocess.run([str(exe)]+([case] if case else []),text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
  if mode:assert result.returncode!=0,mode+' escaped its negative control'
  else:
   assert result.returncode==0,result.stdout+result.stderr
   print(result.stdout.strip())
   strict_result=subprocess.run([str(exe),'strict'],text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
   assert strict_result.returncode!=0,'ordinary strict datum_get became tolerant'
print('Negative controls reject pre-lock peeks, one-pool reset, self-predecessor insertion and callback reset')
