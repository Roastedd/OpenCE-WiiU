"""Exercise production remapping while the tick-owned table belongs to another weapon."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "source/interface/first_person_weapons.c").read_text(encoding="latin1")

def function(name):
    start = source.rindex("static boolean " + name + "(")
    body = source.index("{", start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

start = source.index("static struct\n{\n\tboolean initialized, valid;")
end = source.index("first_person_draw_mappings[8];", start) + len("first_person_draw_mappings[8];")
fixture = r"""
#include <assert.h>
#include <string.h>
typedef unsigned char boolean;
#define TRUE 1
#define FALSE 0
#define NONE -1
#define MAXIMUM_NODES_PER_ANIMATION 4
#define MIN(a,b) ((a)<(b)?(a):(b))
#define match_assert(file,line,expr) assert(expr)
#define csstrcmp strcmp
struct block { int count; void *address; };
struct model { struct block nodes; };
struct animation_graph { struct block nodes; };
struct model_node { const char *name; };
struct animation_graph_node { const char *name; };
struct real_matrix4x3 { int value; };
static struct model models[2];
static struct animation_graph graphs[2];
static struct model *model_definition_get(long index) { return &models[index]; }
static struct animation_graph *animation_graph_definition_get(long index) { return &graphs[index]; }
#define TAG_BLOCK_GET_ELEMENT(block,index,type) (&((type *)(block)->address)[index])
"""
main = r"""
int main(void) {
 struct model_node model_nodes[2]={{"root"},{"barrel"}};
 struct animation_graph_node graph_a[2]={{"barrel"},{"root"}};
 struct animation_graph_node graph_b[2]={{"root"},{"barrel"}};
 struct real_matrix4x3 input[4]={{10},{20},{30},{40}}, output[4]={{0}};
 short tick_table[4]={99,-1,99,-1};
 models[0].nodes=(struct block){2,model_nodes};
 graphs[0].nodes=(struct block){2,graph_a};
 graphs[1].nodes=(struct block){2,graph_b};
 assert(model_remap_node_matrices_to_match_animation_graph(0,output,0,input,tick_table));
 assert(output[0].value==20 && output[1].value==10);
 tick_table[0]=-999;
 assert(model_remap_node_matrices_to_match_animation_graph(0,output,0,input,tick_table));
 assert(output[0].value==20);
 assert(model_remap_node_matrices_to_match_animation_graph(0,output,1,input,tick_table));
 assert(output[0].value==10 && output[1].value==20);
 /* New map reuses tag identities but changes the node order. */
 memset(first_person_draw_mappings,0,sizeof(first_person_draw_mappings));
 graphs[1].nodes.address=graph_a;
 assert(model_remap_node_matrices_to_match_animation_graph(0,output,1,input,tick_table));
 assert(output[0].value==20);
 memset(first_person_draw_mappings,0,sizeof(first_person_draw_mappings));
 graph_a[1].name="unmatched";
 output[0].value=123;
 assert(!model_remap_node_matrices_to_match_animation_graph(0,output,1,input,tick_table));
 assert(output[0].value==123);
 memset(first_person_draw_mappings,0,sizeof(first_person_draw_mappings));
 models[0].nodes.count=5;
 assert(!model_remap_node_matrices_to_match_animation_graph(0,output,0,input,tick_table));
 return 0;
}
"""
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    test = fixture + source[start:end] + function("model_build_remapping_table_for_animation_graph")
    test += function("model_remap_node_matrices_to_match_animation_graph") + main
    (path / "test.c").write_text(test)
    subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-DHALO_WIIU", str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
    stale = test.replace("node_remapping_table = first_person_draw_mappings[slot].nodes;", "/* simulate reading the tick-owned table */")
    (path / "stale.c").write_text(stale)
    subprocess.run(["cc", "-DHALO_WIIU", str(path / "stale.c"), "-o", str(path / "stale")], check=True)
    failure = subprocess.run([str(path / "stale")], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    assert failure.returncode != 0, "The regression check must reject a stale tick-owned mapping"
print("First-person draw mapping checks passed; stale-table negative control failed as expected")
