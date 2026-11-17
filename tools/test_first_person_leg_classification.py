"""Compare optimized bone classification with the original production logic."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
current=(root/'port/linux/game/first_person_legs.c').read_text()
original=subprocess.check_output(['git','-C',str(root),'show','HEAD:port/linux/game/first_person_legs.c'],text=True)
a=original.index('static boolean first_person_legs_name_has(');b=original.index('/* the middle of the waist',a)
old=original[a:b].replace('first_person_legs_', 'old_')
a=current.index('static unsigned char first_person_legs_classify(');b=current.index('/* the middle of the waist',a)
new=current[a:b]
fixture=r"""
#include <assert.h>
#include <string.h>
#include <stdint.h>
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define TAG_STRING_LENGTH 32
#define NUMBEROF(a) (sizeof(a)/sizeof((a)[0]))
"""
main=r"""
static void check(const char *name){
 unsigned char c=first_person_legs_classify(name);
 assert(first_person_legs_node(name)==old_node(name));
 assert(!!(c&1)==old_name_has(name,"pelvis"));
 assert(!!(c&2)==old_name_has(name,"thigh"));
}
int main(void){
 const char *names[]={"Bip01 Pelvis","bip01 l thigh","RIGHT_FOOT","lower LEG","calf","toe","root","head","legitimate",""};
 for(unsigned i=0;i<NUMBEROF(names);i++)check(names[i]);
 uint32_t seed=1234;
 for(unsigned n=0;n<100000;n++){
  char name[33];
  for(int i=0;i<32;i++){seed=seed*1664525u+1013904223u;name[i]=(char)('A'+(seed>>24)%58);}
  name[32]=0;check(name);
 }
 return 0;
}
"""
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.c').write_text(fixture+old+new+main)
 subprocess.run(['cc','-std=c99','-O2','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('100,000 bone-name comparisons match the original behavior')
