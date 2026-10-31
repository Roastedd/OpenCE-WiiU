#!/usr/bin/env python3
"""Exercise production mip-helper failure and queued-storage contracts.

The GPU calls are recorded, not emulated. Pixel equivalence belongs to the
native gpu_test.txt 2 qualification. Deliberate faults prove that allocation
ordering and alignment checks are observable here.
"""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'port/wiiu/src/wiiu_gx2_mip.c'

MOCK = r'''
#ifndef MIP_MOCK_H
#define MIP_MOCK_H
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Fault controls exit normally rather than creating expected-abort cores. */
#undef assert
#define assert(condition) do { if(!(condition)) { \
 fprintf(stderr,"contract failed: %s:%d: %s\n",__FILE__,__LINE__,#condition);exit(99); \
 } } while(0)
typedef int GX2SurfaceUse, GX2InvalidateMode;
typedef struct { unsigned dim,width,height,depth,mipLevels,format,aa,use,tileMode;
 uint32_t imageSize,mipmapSize,alignment;void *image,*mipmaps; } GX2Surface;
typedef struct { GX2Surface surface;unsigned viewNumMips,viewNumSlices,compMap; } GX2Texture;
typedef struct { GX2Surface surface;unsigned viewNumSlices; } GX2ColorBuffer;
typedef struct { int unused; } GX2ContextState;
typedef int GX2Sampler;
typedef struct { const char *name;unsigned location; } MockSampler;
typedef struct { unsigned samplerVarCount;MockSampler *samplerVars; } MockPixel;
typedef struct { int fetchShader;MockPixel *pixelShader;void *vertexShader; } WHBGfxShaderGroup;
#define TRUE 1
#define FALSE 0
#define GX2_SURFACE_DIM_TEXTURE_2D 1
#define GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8 1
#define GX2_AA_MODE1X 1
#define GX2_SURFACE_USE_TEXTURE 1
#define GX2_SURFACE_USE_COLOR_BUFFER 2
#define GX2_TILE_MODE_DEFAULT 0
#define GX2_ATTRIB_FORMAT_FLOAT_32_32 1
#define GX2_TEX_CLAMP_MODE_CLAMP 1
#define GX2_TEX_XY_FILTER_MODE_POINT 1
#define GX2_TEX_Z_FILTER_MODE_POINT 1
#define GX2_TEX_MIP_FILTER_MODE_NONE 0
#define GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER 4
#define GX2_INVALIDATE_MODE_COLOR_BUFFER 1
#define GX2_INVALIDATE_MODE_TEXTURE 2
#define GX2_COMP_MAP(...) 0
#define GX2_SQ_SEL_R 0
#define GX2_SQ_SEL_G 1
#define GX2_SQ_SEL_B 2
#define GX2_SQ_SEL_A 3
#define GX2_RENDER_TARGET_0 0
#define GX2_COMPARE_FUNC_ALWAYS 0
#define GX2_FRONT_FACE_CCW 0
#define GX2_LOGIC_OP_COPY 0
#define GX2_CHANNEL_MASK_RGBA 15
#define GX2_SHADER_MODE_UNIFORM_REGISTER 0
#define GX2_PRIMITIVE_MODE_TRIANGLES 0
static int shader_ok=1,shader_calls,alloc_calls,fail_alloc,gpu_mutations,copy_calls,draw_calls;
static unsigned required_alignment=64;
static int allocated_after_mutation;
static void *allocated[32],*retired[32];
static unsigned allocated_count,retired_count;
static const void *last_attributes;
static unsigned last_attribute_size;
static void mock_state(void) { gpu_mutations++; }
static void *MEMAllocFromDefaultHeapEx(uint32_t size,int32_t alignment) {
 void *p=NULL;alloc_calls++;
 if(gpu_mutations)allocated_after_mutation++;
 if(alloc_calls==fail_alloc)return NULL;
 assert(allocated_count<32);
 assert(posix_memalign(&p,(size_t)alignment,size)==0);
 allocated[allocated_count++]=p;return p;
}
static int WHBGfxLoadGFDShaderGroup(WHBGfxShaderGroup *g,unsigned index,const void *bytes) {
 static MockSampler sampler={"tex0",0};static MockPixel pixel={1,&sampler};
 (void)index;(void)bytes;shader_calls++;g->pixelShader=&pixel;return shader_ok;
}
static int WHBGfxInitShaderAttribute(WHBGfxShaderGroup *g,const char *n,unsigned b,unsigned o,int f) {
 (void)g;(void)n;(void)b;(void)o;(void)f;return 1;
}
static int WHBGfxInitFetchShader(WHBGfxShaderGroup *g) { (void)g;return 1; }
static void GX2CalcSurfaceSizeAndAlignment(GX2Surface *s) {
 s->imageSize=(s->width*s->height*4+255u)&~255u;s->alignment=required_alignment;
}
static void GX2InitColorBufferRegs(GX2ColorBuffer *b) { (void)b; }
static void GX2InitTextureRegs(GX2Texture *t) { (void)t; }
static void GX2Invalidate(int mode,void *p,uint32_t size) { (void)p;(void)size;if(mode&3)mock_state(); }
static void GX2CopySurface(const GX2Surface *s,unsigned sl,unsigned ss,GX2Surface *d,unsigned dl,unsigned ds) {
 (void)s;(void)sl;(void)ss;(void)d;(void)dl;(void)ds;copy_calls++;mock_state();
}
static void GX2SetAttribBuffer(unsigned index,unsigned size,unsigned stride,const void *data) {
 (void)index;(void)stride;last_attributes=data;last_attribute_size=size;mock_state();
}
static void GX2SetPixelTexture(const GX2Texture *t,unsigned slot) {
 (void)slot;assert(t->viewNumMips==1&&t->surface.mipLevels==1);mock_state();
}
#define GX2InitSampler(...) ((void)0)
#define GX2InitSamplerZMFilter(...) ((void)0)
#define GX2InitSamplerLOD(...) ((void)0)
#define GX2SetupContextStateEx(...) mock_state()
#define GX2SetContextState(...) mock_state()
#define GX2SetColorBuffer(...) mock_state()
#define GX2SetViewport(...) mock_state()
#define GX2SetScissor(...) mock_state()
#define GX2SetRasterizerClipControlEx(...) mock_state()
#define GX2SetDepthOnlyControl(...) mock_state()
#define GX2SetCullOnlyControl(...) mock_state()
#define GX2SetColorControl(...) mock_state()
#define GX2SetTargetChannelMasks(...) mock_state()
#define GX2SetShaderMode(...) mock_state()
#define GX2SetFetchShader(...) mock_state()
#define GX2SetVertexShader(...) mock_state()
#define GX2SetPixelShader(...) mock_state()
#define GX2SetPixelSampler(...) mock_state()
#define GX2DrawEx(...) do { draw_calls++;mock_state(); } while(0)
#endif
'''

FIXTURE = r'''
#include "production.c"
static void retire(void *p) { assert(retired_count<32);retired[retired_count++]=p; }
static GX2Surface surface(unsigned width,unsigned height) {
 GX2Surface s={0};static unsigned char identity;
 s.dim=GX2_SURFACE_DIM_TEXTURE_2D;s.width=width;s.height=height;s.depth=1;
 s.mipLevels=1;s.format=GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;s.aa=GX2_AA_MODE1X;
 s.image=&identity;s.imageSize=width*height*4;return s;
}
int main(int argc,char **argv) {
 GX2Surface source=surface(8,8),destination=surface(4,4);
 unsigned i;unsigned char saved[256];const void *first_attributes;
 assert(argc==2);
 if(!strcmp(argv[1],"shader-failure")) {
  shader_ok=0;
  assert(!wiiu_gx2_mip_derive(&source,0,&destination,0,retire));
  assert(!wiiu_gx2_mip_derive(&source,0,&destination,0,retire));
  assert(shader_calls==1&&alloc_calls==0&&gpu_mutations==0);
 } else if(!strncmp(argv[1],"allocation-failure-",19)) {
  fail_alloc=atoi(argv[1]+19);assert(fail_alloc==1||fail_alloc==2);
  assert(!wiiu_gx2_mip_derive(&source,0,&destination,0,retire));
  assert(gpu_mutations==0&&copy_calls==0&&draw_calls==0);
  assert(!wiiu_gx2_mip_derive(&source,0,&destination,0,retire));
  assert(alloc_calls==fail_alloc&&gpu_mutations==0);
 } else if(!strcmp(argv[1],"invalid-arguments")) {
  source.width=0;
  assert(!wiiu_gx2_mip_derive(&source,0,&destination,0,retire));
  source.width=8;source.height=0;
  assert(!wiiu_gx2_mip_derive(&source,0,&destination,0,retire));
  source.height=8;destination.width=0;
  assert(!wiiu_gx2_mip_derive(&source,0,&destination,0,retire));
  destination.width=4;destination.height=0;
  assert(!wiiu_gx2_mip_derive(&source,0,&destination,0,retire));
  destination.height=4;
  source.format=99;
  assert(!wiiu_gx2_mip_derive(&source,0,&destination,0,retire));
  source.format=GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
  assert(!wiiu_gx2_mip_derive(&source,1,&destination,0,retire));
  assert(!wiiu_gx2_mip_derive(&source,0,&destination,0,NULL));
  destination.width=3;
  assert(!wiiu_gx2_mip_derive(&source,0,&destination,0,retire));
  assert(shader_calls==0&&alloc_calls==0&&gpu_mutations==0);
 } else {
  assert(wiiu_gx2_mip_derive(&source,0,&destination,0,retire));
  assert(alloc_calls==2&&copy_calls==2&&draw_calls==1&&!allocated_after_mutation);
  assert(last_attribute_size<=sizeof(saved));memcpy(saved,last_attributes,last_attribute_size);first_attributes=last_attributes;
  if(!strcmp(argv[1],"reuse-and-alignment")) {
   gpu_mutations=0;required_alignment=512;
   assert(wiiu_gx2_mip_derive(&source,0,&destination,0,retire));
   assert(alloc_calls==4&&retired_count==2&&!allocated_after_mutation);
   assert(retired[0]==allocated[0]&&retired[1]==allocated[1]);
   assert((uintptr_t)allocated[2]%512==0&&(uintptr_t)allocated[3]%512==0);
   gpu_mutations=0;source=surface(32,32);destination=surface(16,16);required_alignment=64;
   assert(wiiu_gx2_mip_derive(&source,0,&destination,0,retire));
   assert(alloc_calls==6&&retired_count==4&&!allocated_after_mutation);
   gpu_mutations=0;required_alignment=512;
   assert(wiiu_gx2_mip_derive(&source,0,&destination,0,retire));
   assert(alloc_calls==6&&retired_count==4&&!allocated_after_mutation);
   assert(last_attributes==first_attributes&&memcmp(saved,last_attributes,last_attribute_size)==0);
  } else assert(!strcmp(argv[1],"success"));
 }
 for(i=0;i<allocated_count;i++)free(allocated[i]);
 return 0;
}
'''


class MipContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory(prefix='wiiu-mip-contract-')
        cls.directory = Path(cls.work.name)
        cls.compiler = shutil.which('clang') or shutil.which('cc')
        if not cls.compiler:
            raise unittest.SkipTest('No host C compiler')
        (cls.directory / 'mock.h').write_text(MOCK)
        for name in ['gx2/surface.h', 'coreinit/memdefaultheap.h', 'gx2/context.h',
                     'gx2/draw.h', 'gx2/enum.h', 'gx2/mem.h', 'gx2/registers.h',
                     'gx2/sampler.h', 'gx2/shaders.h', 'gx2/state.h', 'gx2/texture.h',
                     'gx2/utils.h', 'whb/gfx.h']:
            path = cls.directory / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "mock.h"\n')
        cls.environment = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def build(self, name, source):
        (self.directory / 'production.c').write_text(source)
        (self.directory / 'fixture.c').write_text(FIXTURE)
        executable = self.directory / name
        subprocess.run([self.compiler, '-std=gnu11', '-D_POSIX_C_SOURCE=200112L',
                        '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-ffp-contract=off', '-I' + str(self.directory),
                        '-I' + str(SOURCE.parent), str(self.directory / 'fixture.c'),
                        '-o', str(executable)], check=True, capture_output=True, text=True)
        return executable

    def run_case(self, executable, case):
        return subprocess.run([str(executable), case], env=self.environment, capture_output=True, text=True)

    def test_production_failure_and_lifetime_contracts(self):
        executable = self.build('production', SOURCE.read_text())
        for case in ['shader-failure', 'allocation-failure-1', 'allocation-failure-2',
                     'invalid-arguments', 'success', 'reuse-and-alignment']:
            with self.subTest(case=case):
                result = self.run_case(executable, case)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_deliberate_faults_are_detected(self):
        source = SOURCE.read_text()
        ordering = source.replace('if (!mip_initialize())',
                                  'GX2SetContextState(&mip_context);\n\tif (!mip_initialize())', 1)
        self.assertNotEqual(ordering, source)
        executable = self.build('bad-order', ordering)
        self.assertNotEqual(self.run_case(executable, 'allocation-failure-2').returncode, 0)
        alignment = source.replace(' || alignment > mip.scratch[slot].alignment', '', 1)
        self.assertNotEqual(alignment, source)
        executable = self.build('bad-alignment', alignment)
        self.assertNotEqual(self.run_case(executable, 'reuse-and-alignment').returncode, 0)


if __name__ == '__main__':
    unittest.main()
