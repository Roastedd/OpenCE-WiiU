from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];s=(root/'source/rasterizer/xbox/rasterizer_xbox_decals.c').read_text()
def function(signature):
 a=s.index(signature+'\n{');start=s.index('{',a);depth=1;i=start+1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
code=r'''
#include <assert.h>
#include <stddef.h>
#define NONE -1
#define FALSE 0
#define TRUE 1
#define TEST_FLAG(v,b) ((v)&(1u<<(b)))
#define match_assert(file,line,expr) assert(expr)
typedef int boolean;
enum {_decal_locked_bit=0,_decal_permanent_bit=1};
struct decal_datum {unsigned flags;};
struct data_array {int valid;};
static struct data_array data={1};static struct data_array *global_decal_data=&data;
static struct decal_datum live;
static long live_handle=0x12340011,last_decal_index_queried_by_lruv_cache;
static unsigned repair_requests;
void decals_request_cache_repair(void){repair_requests++;}
static int locked_decal_reported,permanent_decal_reported,local_vertex_cache,deleted;
static int lruv_has_locked_proc(int cache){(void)cache;return 1;}
static void *datum_try_and_get(struct data_array*d,long index){return d->valid && index==live_handle ? &live : NULL;}
static void error(int level,const char*fmt,...){(void)level;(void)fmt;}
static void decal_delete(long index){assert(index==live_handle);deleted++;}
'''+function('static void rasterizer_decal_vertices_purge_proc(\n\tlong decal_index)')+'\n'+function('static boolean rasterizer_decal_vertices_locked_proc(\n\tlong decal_index)')+r'''
int main(void){
 long stale=0x12330011;
 rasterizer_decal_vertices_purge_proc(stale);assert(deleted==0 && repair_requests==1);
 assert(!rasterizer_decal_vertices_locked_proc(stale));
 live.flags=1;assert(rasterizer_decal_vertices_locked_proc(live_handle));
 live.flags=2;assert(rasterizer_decal_vertices_locked_proc(live_handle));
 live.flags=0;assert(!rasterizer_decal_vertices_locked_proc(live_handle));
 rasterizer_decal_vertices_purge_proc(live_handle);assert(deleted==1);
 data.valid=0;rasterizer_decal_vertices_purge_proc(live_handle);assert(deleted==1);
 assert(!rasterizer_decal_vertices_locked_proc(live_handle));
 global_decal_data=NULL;rasterizer_decal_vertices_purge_proc(live_handle);assert(deleted==1);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.c').write_text(code)
 subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Stale decal generations, replacement preservation and valid eviction passed')
