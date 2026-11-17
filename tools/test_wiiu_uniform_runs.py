#!/usr/bin/env python3
"""Compare cached GX2 register runs with the previous scan for every subrange."""
import pathlib, subprocess, tempfile
root = pathlib.Path(__file__).resolve().parents[1]
code = r'''
#include <assert.h>
#include <stdio.h>
#include "wiiu_uniform_runs.h"
static unsigned seed=13;
static unsigned random_value(void) { seed=seed*1664525u+1013904223u; return seed; }
int main(void) {
 int16_t elements[192]; uint8_t runs[192]; unsigned trial,i,first,end,a,b,n,m,calls=0;
 for(trial=0;trial<160;trial++) {
  for(i=0;i<192;i++) elements[i]= trial==0 ? -1 : trial==1 ? (int16_t)(i*4) :
   (random_value()%4==0 ? -1 : (i && random_value()%3 ? elements[i-1]+4 : (int16_t)(random_value()%200*4)));
  wiiu_uniform_runs_build(elements,runs,192);
  for(first=0;first<192;first++) for(end=first+1;end<=192;end++) {
   a=b=first;
   while(a<end || b<end) {
    while(a<end && elements[a]<0) a++;
    while(b<end && elements[b]<0) b+=wiiu_uniform_run(elements,runs,b,end);
    assert(a==b);
    if(a==end) break;
    n=1; while(a+n<end && elements[a+n]==elements[a]+(int)n*4) n++;
    m=wiiu_uniform_run(elements,runs,b,end);
    assert(n==m); assert(m==wiiu_uniform_run(elements,0,b,end));
    a+=n;b+=m;calls++;
   }
  }
 }
 printf("Identical register writes across 2,964,480 ranges (%u emitted spans)\n",calls);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as temp:
 src=pathlib.Path(temp)/'test.c';exe=pathlib.Path(temp)/'test'
 src.write_text(code)
 subprocess.run(['cc','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'port/wiiu/src'),str(src),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
