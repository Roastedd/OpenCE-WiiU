#!/usr/bin/env python3
"""Compare source-extracted BSP vector traversal against its frozen reference."""
import argparse,os,re,shlex,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def function(text,name):
    for match in re.finditer(r'(?:static\s+|__inline\s+)?(?:boolean|real|void\s*\*|long)\s+'+name+r'\s*\(',text):
        start=match.start();body=text.index('{',match.end());semi=text.find(';',match.end())
        if semi<body:continue
        level=1;end=body+1
        while level:level+=(text[end]=='{')-(text[end]=='}');end+=1
        return text[start:end]
    raise ValueError(name)
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--bench',type=int,default=0);parser.add_argument('--sanitize',action='store_true');parser.add_argument('--fault-controls',action='store_true');args=parser.parse_args()
    source=(ROOT/'source/physics/collision_bsp.c').read_text();header=(ROOT/'source/tag_files/tag_groups.h').read_text();math=(ROOT/'source/math/real_math.h').read_text();name='collision_bsp_test_vector_recursive'
    old=function((ROOT/'tools/harness/tests/collision_tail_reference.inc').read_text(),name);current=function(source,name)
    # The tail-return equivalence relies on the function's canonical leaves.
    # A future new return expression needs a new argument, not a silent pass.
    for text in [old,current]:
        text=re.sub(r'/\*.*?\*/|//[^\n]*','',text,flags=re.S)
        for expression in re.findall(r'\breturn\s+([^;]+);',text):
            expression=expression.strip()
            if expression not in ('TRUE','FALSE') and not expression.startswith(name+'('):
                raise ValueError('Canonical return contract needs review: '+expression)
    with tempfile.TemporaryDirectory() as temp:
        temp=Path(temp)
        for prefix,text in [('reference',old),('candidate',current)]:
            text=text.replace(name,prefix);body=text.index('{');text=text[:body+1]+'\n ENTRY(node_index,t0,t1,depth);'+text[body+1:];(temp/(prefix+'.inc')).write_text(text)
        start=header.index('static __inline__ void *tag_block_get_element_fast(');end=header.index('\n}',start)+2;(temp/'getter.inc').write_text(header[start:end])
        (temp/'helpers.inc').write_text('\n'.join(function(source,n) for n in ['collision_bsp3d_enter_node','collision_bsp_valid_leaf']))
        (temp/'math.inc').write_text('\n'.join(function(math,n).replace('__inline real','static inline real') for n in ['dot_product3d','plane3d_distance_to_point']))
        compiler=shlex.split(os.environ.get('CC','clang'))+shlex.split(os.environ.get('COLLISION_TARGET',os.environ.get('HALO_HARNESS_TARGET','')))
        flags=['-O2','-std=c11','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Werror','-ffp-contract=off','-fno-fast-math','-fno-strict-aliasing','-fwrapv','-I'+str(temp)]
        if args.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        target=temp/'test';fixture=ROOT/'tools/harness/tests/collision_tail.c'
        subprocess.run(compiler+flags+['-DTRACE',str(fixture),'-o',str(target)],check=True)
        runner=shlex.split(os.environ.get('COLLISION_RUNNER',os.environ.get('HALO_HARNESS_RUNNER','')));subprocess.run(runner+[str(target)],check=True)
        if args.bench:
            subprocess.run(compiler+flags+[str(fixture),'-o',str(target)],check=True);subprocess.run(runner+[str(target),str(args.bench)],check=True)
        if args.fault_controls:
            valid=(temp/'candidate.inc').read_text()
            for name,bad in [('wrong-child',valid.replace('node->children[reaches_front]','node->children[!reaches_front]')),('wrong-depth',valid.replace('depth + 1','depth'))]:
                if bad==valid:raise RuntimeError('Fault control not applied: '+name)
                (temp/'candidate.inc').write_text(bad)
                subprocess.run(compiler+flags+['-DTRACE',str(fixture),'-o',str(target)],check=True)
                result=subprocess.run(runner+[str(target)],capture_output=True,text=True)
                if result.returncode!=1 or 'differential failure' not in result.stderr:raise RuntimeError('Fault not detected: '+name)
                print('fault control detected:',name,flush=True)
            (temp/'candidate.inc').write_text(valid)
if __name__=='__main__':main()
