"""Exercise the actual composite refresh and context policy without game assets.

COMPOSITE_MIP_TARGET='--target=powerpc-linux-gnu -fuse-ld=lld -no-pie'
COMPOSITE_MIP_RUNNER=qemu-ppc runs the same fixture big-endian.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
from harness import function, structure, constant

SOURCE = ROOT / 'port/wiiu/src/wiiu_gx2.c'

PRELUDE = r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DK_COMPOSITE_LEVELS @DK_COMPOSITE_LEVELS@
typedef int GX2InvalidateMode;
enum { GX2_INVALIDATE_MODE_COLOR_BUFFER = 1, GX2_INVALIDATE_MODE_TEXTURE = 2 };
typedef struct { unsigned width, height, imageSize; void *image; } GX2Surface;
typedef struct { GX2Surface surface; } GX2Texture;
struct target { struct { GX2Surface surface; } color; unsigned last_used; };
struct dk_stage_texture { uint32_t id, composite, target; };
static struct {
    unsigned features_off, mip_reuses, mip_failed;
    unsigned long long mip_failed_pixels;
    struct dk_stage_texture stages[4];
} gx;
'''

MOCKS = r'''
static struct composite subject;
static struct target targets[DK_COMPOSITE_LEVELS];
static unsigned present[DK_COMPOSITE_LEVELS];
static unsigned copies[DK_COMPOSITE_LEVELS], derives[DK_COMPOSITE_LEVELS];
static unsigned content[DK_COMPOSITE_LEVELS], fail_mask;
static unsigned invalidations, resumes, failures;

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr,"composite policy FAIL line %d: %s\n",__LINE__,#condition); failures++; \
} } while (0)

static struct target *target_latest(uint32_t data)
{
    unsigned i;
    for(i=0;i<DK_COMPOSITE_LEVELS;i++)
        if(present[i] && data==100u+i) return &targets[i];
    return NULL;
}

static int composite_derive(struct composite *composite,unsigned level)
{
    CHECK(composite==&subject);
    CHECK(level>0 && level<composite->levels);
    derives[level]++;
    if(fail_mask & (1u<<level)) return 0;
    content[level]=content[level-1]+1000u;
    return 1;
}

static void GX2Invalidate(GX2InvalidateMode mode,void *memory,unsigned bytes)
{
    (void)memory;
    CHECK(mode==(GX2_INVALIDATE_MODE_COLOR_BUFFER|GX2_INVALIDATE_MODE_TEXTURE));
    CHECK(bytes>0);
    invalidations++;
}

static void GX2CopySurface(const GX2Surface *source,unsigned source_level,unsigned source_slice,
    GX2Surface *destination,unsigned destination_level,unsigned destination_slice)
{
    CHECK(source_level==0 && source_slice==0 && destination_slice==0);
    CHECK(destination==&subject.texture.surface);
    CHECK(destination_level<subject.levels);
    CHECK(source==&targets[destination_level].color.surface);
    copies[destination_level]++;
    content[destination_level]=(unsigned)(uintptr_t)source->image;
}

static struct composite *composite_find(uint32_t data,uint32_t levels)
{
    return data==subject.data && levels==subject.levels ? &subject : NULL;
}

static void context_resume(void) { resumes++; }
'''

FIXTURE = r'''
static void setup(unsigned levels)
{
    unsigned i;
    memset(&gx,0,sizeof(gx));
    memset(&subject,0,sizeof(subject));
    memset(targets,0,sizeof(targets));
    memset(present,0,sizeof(present));
    memset(copies,0,sizeof(copies));
    memset(derives,0,sizeof(derives));
    memset(content,0,sizeof(content));
    fail_mask=invalidations=resumes=0;
    subject.used=1;subject.data=42;subject.width=subject.height=8;subject.levels=levels;
    for(i=0;i<levels;i++) {
        subject.level_data[i]=100+i;
        targets[i].color.surface.width=targets[i].color.surface.height=8u>>i;
        targets[i].color.surface.imageSize=(8u>>i)*(8u>>i)*4;
        targets[i].color.surface.image=(void *)(uintptr_t)(20000u+i);
    }
    present[0]=1;targets[0].last_used=10;
}

static void unchanged(void)
{
    setup(4);
    CHECK(composite_refresh(&subject,1)==1);
    CHECK(copies[0]==1 && derives[1]==1 && derives[2]==1 && derives[3]==1);
    CHECK(composite_refresh(&subject,1)==0);
    CHECK(copies[0]==1 && derives[1]==1 && derives[2]==1 && derives[3]==1);
    CHECK(gx.mip_reuses==3);
    CHECK(content[3]==23000);
}

static void updates(void)
{
    setup(4);
    CHECK(composite_refresh(&subject,1)==1);
    targets[0].last_used=20;targets[0].color.surface.image=(void *)(uintptr_t)21000;
    CHECK(composite_refresh(&subject,1)==1);
    CHECK(copies[0]==2 && derives[1]==2 && derives[2]==2 && derives[3]==2);
    CHECK(content[3]==24000);
    CHECK(composite_refresh(&subject,1)==0);
}

static void explicit_intermediate(void)
{
    setup(4);
    present[1]=1;targets[1].last_used=17;
    CHECK(composite_refresh(&subject,1)==1);
    CHECK(copies[0]==1 && copies[1]==1 && derives[1]==0 && derives[2]==1 && derives[3]==1);
    CHECK(content[1]==20001 && content[2]==21001 && content[3]==22001);
    targets[0].last_used=30;
    CHECK(composite_refresh(&subject,1)==1);
    CHECK(copies[0]==2 && copies[1]==1 && derives[2]==1 && derives[3]==1);
    targets[1].last_used=31;targets[1].color.surface.image=(void *)(uintptr_t)31001;
    CHECK(composite_refresh(&subject,1)==1);
    CHECK(copies[1]==2 && derives[2]==2 && derives[3]==2 && content[3]==33001);
}

static void failure(void)
{
    setup(3);
    present[0]=0;subject.level_written[0]=10;content[0]=20000;
    fail_mask=1u<<1;
    CHECK(composite_refresh(&subject,1)==0);
    CHECK(derives[1]==1 && derives[2]==0);
    CHECK(subject.level_written[1]==0 && subject.level_derived[1]==0);
    CHECK(gx.mip_failed==1 && gx.mip_failed_pixels==16);
    fail_mask=0;
    CHECK(composite_refresh(&subject,1)==1);
    CHECK(derives[1]==2 && derives[2]==1 && content[2]==22000);
    CHECK(composite_refresh(&subject,1)==0);
}

static void disabled(void)
{
    setup(3);
    CHECK(composite_refresh(&subject,0)==1);
    CHECK(copies[0]==1 && derives[1]==0 && derives[2]==0);
    CHECK(composite_refresh(&subject,0)==0);
    gx.features_off=64;
    CHECK(composite_refresh(&subject,1)==0 && derives[1]==0);
    present[1]=1;targets[1].last_used=17;
    CHECK(composite_refresh(&subject,1)==1);
    CHECK(copies[1]==1 && derives[2]==0);
    gx.features_off=0;
    CHECK(composite_refresh(&subject,1)==1 && derives[2]==1);
}

static void wrong_size(void)
{
    setup(3);
    present[1]=1;targets[1].last_used=17;targets[1].color.surface.width=3;
    CHECK(composite_refresh(&subject,1)==1);
    CHECK(copies[1]==0 && derives[1]==1 && derives[2]==1);
    CHECK(content[1]==21000);
}

static void prepare(void)
{
    setup(3);
    gx.stages[0].target=subject.data;gx.stages[0].composite=subject.levels;
    gx.stages[1]=gx.stages[0];
    composites_prepare();
    CHECK(resumes==1 && copies[0]==1 && derives[1]==1 && derives[2]==1);
    composites_prepare();
    CHECK(resumes==1 && copies[0]==1 && derives[1]==1 && derives[2]==1);
    targets[0].last_used=20;
    composites_prepare();
    CHECK(resumes==2 && copies[0]==2 && derives[1]==2 && derives[2]==2);
    memset(gx.stages,0,sizeof(gx.stages));
    gx.stages[0].id=7;gx.stages[0].target=subject.data;gx.stages[0].composite=subject.levels;
    composites_prepare();CHECK(resumes==2);
    setup(3);present[0]=0;subject.level_written[0]=10;fail_mask=1u<<1;
    gx.stages[0].target=subject.data;gx.stages[0].composite=subject.levels;
    composites_prepare();CHECK(resumes==0 && subject.level_written[1]==0);
}

static void explicit_stamp_collision(void)
{
    setup(2);
    CHECK(composite_refresh(&subject,1)==1);
    /* A real target's next write increments gx.use_serial from 10 to 11.
       The synthetic derived stamp is also 11, but its bytes are different. */
    CHECK(subject.level_written[1]==11 && content[1]==21000);
    present[1]=1;targets[1].last_used=11;
    CHECK(composite_refresh(&subject,1)==1);
    CHECK(copies[1]==1 && content[1]==20001);
    CHECK(composite_refresh(&subject,1)==0 && copies[1]==1);
}

static void derived_provenance_cleared(void)
{
    setup(2);
    CHECK(composite_refresh(&subject,1)==1);
    present[1]=1;targets[1].last_used=30;
    CHECK(composite_refresh(&subject,1)==1 && content[1]==20001);
    present[1]=0;
    CHECK(composite_refresh(&subject,1)==1);
    CHECK(derives[1]==2 && content[1]==21000);
}

static void descendant_after_explicit_collision(void)
{
    setup(3);
    CHECK(composite_refresh(&subject,1)==1);
    CHECK(subject.level_written[1]==11 && subject.level_derived[2]==11 && content[2]==22000);
    present[1]=1;targets[1].last_used=11;
    CHECK(composite_refresh(&subject,1)==1);
    CHECK(copies[1]==1 && content[1]==20001);
    CHECK(derives[2]==2 && content[2]==21001);
    CHECK(composite_refresh(&subject,1)==0 && derives[2]==2);
}

static void descendant_after_derived_collision(void)
{
    setup(3);present[1]=1;targets[1].last_used=11;
    CHECK(composite_refresh(&subject,1)==1);
    CHECK(subject.level_written[1]==11 && subject.level_derived[2]==11 && content[2]==21001);
    present[1]=0;
    CHECK(composite_refresh(&subject,1)==1);
    CHECK(derives[1]==1 && content[1]==21000);
    CHECK(derives[2]==2 && content[2]==22000);
    CHECK(composite_refresh(&subject,1)==0 && derives[2]==2);
}

static void parent_update_explicit_appearance(void)
{
    setup(3);
    CHECK(composite_refresh(&subject,1)==1);
    targets[0].last_used=30;targets[0].color.surface.image=(void *)(uintptr_t)30000;
    present[1]=1;targets[1].last_used=11;
    CHECK(composite_refresh(&subject,1)==1);
    CHECK(copies[1]==1 && content[1]==20001);
    CHECK(derives[2]==2 && content[2]==21001);
}

static void derived_invalidation_explicit_boundary(void)
{
    setup(4);present[2]=1;targets[2].last_used=20;
    CHECK(composite_refresh(&subject,1)==1);
    CHECK(derives[1]==1 && copies[2]==1 && derives[3]==1 && content[3]==21002);
    targets[0].last_used=30;targets[0].color.surface.image=(void *)(uintptr_t)30000;
    CHECK(composite_refresh(&subject,1)==1);
    CHECK(derives[1]==2 && content[1]==31000);
    CHECK(copies[2]==1 && derives[3]==1 && content[3]==21002);
}

int main(int argc,char **argv)
{
    if(argc!=2) return 2;
    failures=0;
    if(!strcmp(argv[1],"unchanged")) unchanged();
    else if(!strcmp(argv[1],"updates")) updates();
    else if(!strcmp(argv[1],"explicit")) explicit_intermediate();
    else if(!strcmp(argv[1],"failure")) failure();
    else if(!strcmp(argv[1],"disabled")) disabled();
    else if(!strcmp(argv[1],"wrong-size")) wrong_size();
    else if(!strcmp(argv[1],"prepare")) prepare();
    else if(!strcmp(argv[1],"collision")) explicit_stamp_collision();
    else if(!strcmp(argv[1],"provenance")) derived_provenance_cleared();
    else if(!strcmp(argv[1],"descendant-explicit")) descendant_after_explicit_collision();
    else if(!strcmp(argv[1],"descendant-derived")) descendant_after_derived_collision();
    else if(!strcmp(argv[1],"parent-and-explicit")) parent_update_explicit_appearance();
    else if(!strcmp(argv[1],"explicit-boundary")) derived_invalidation_explicit_boundary();
    else return 2;
    if(failures) return 1;
    printf("composite policy: %s passed\n",argv[1]);
    return 0;
}
'''


def source_code():
    text = SOURCE.read_text()
    header = (ROOT / 'port/wiiu/src/dk_commands.h').read_text()
    prelude = PRELUDE.replace('@DK_COMPOSITE_LEVELS@', str(constant(header, 'DK_COMPOSITE_LEVELS')))
    refresh = function(text, 'composite_refresh')
    helpers = []
    for name in re.findall(r'\b(composite_[a-z_]+)\s*\(', refresh):
        if name not in ['composite_refresh', 'composite_derive'] and name not in helpers:
            helpers.append(name)
    return (prelude + structure(text, 'composite') + MOCKS +
            function(text, 'level_size') + ''.join(function(text, name) for name in helpers) + refresh +
            function(text, 'composites_prepare') + FIXTURE)


class CompositeMipPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='opence-composite-mip-')
        cls.folder = Path(cls.directory.name)
        cls.code = source_code()
        cls.compile(cls.code, cls.folder / 'check')

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    @classmethod
    def compile(cls, code, output, sanitizer=False):
        path = output.with_suffix('.c')
        path.write_text(code)
        command = [os.environ.get('CC', 'cc'), *shlex.split(os.environ.get('COMPOSITE_MIP_TARGET', '')),
                   '-std=gnu11', '-O2', '-g', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                   '-fno-strict-aliasing', str(path), '-o', str(output)]
        if sanitizer:
            command[1:1] = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
        result = subprocess.run(command, capture_output=True, text=True, timeout=45)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def run_case(self, name, binary=None, passed=True):
        command = [*shlex.split(os.environ.get('COMPOSITE_MIP_RUNNER', '')), str(binary or self.folder / 'check'), name]
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        if passed:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(name + ' passed', result.stdout)
        else:
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('composite policy FAIL', result.stderr)

    def test_unchanged_source_is_reused(self):
        self.run_case('unchanged')

    def test_source_update_rederives_descendants(self):
        self.run_case('updates')

    def test_rendered_intermediate_has_precedence(self):
        self.run_case('explicit')

    def test_failed_derive_does_not_publish_or_propagate(self):
        self.run_case('failure')

    def test_feature_flag_and_no_derive_preserve_explicit_copies(self):
        self.run_case('disabled')

    def test_wrong_size_target_is_not_copied(self):
        self.run_case('wrong-size')

    def test_real_prepare_restores_context_once_only_after_changes(self):
        self.run_case('prepare')

    def test_new_explicit_target_is_not_hidden_by_equal_derived_stamp(self):
        self.run_case('collision')

    def test_explicit_copy_replaces_derived_provenance(self):
        self.run_case('provenance')

    def test_explicit_transition_rebuilds_derived_descendants_with_equal_stamp(self):
        self.run_case('descendant-explicit')

    def test_derived_transition_rebuilds_derived_descendants_with_equal_stamp(self):
        self.run_case('descendant-derived')

    def test_parent_update_and_new_explicit_target_cannot_reuse_synthetic_content(self):
        self.run_case('parent-and-explicit')

    def test_derived_invalidation_stops_at_explicit_intermediate(self):
        self.run_case('explicit-boundary')

    @unittest.skipIf(bool(os.environ.get('COMPOSITE_MIP_TARGET')), 'sanitizer uses native host')
    def test_native_sanitizers(self):
        binary = self.folder / 'sanitized'
        self.compile(self.code, binary, sanitizer=True)
        for name in ['unchanged', 'updates', 'explicit', 'failure', 'disabled', 'wrong-size', 'prepare', 'collision', 'provenance',
                     'descendant-explicit', 'descendant-derived', 'parent-and-explicit', 'explicit-boundary']:
            self.run_case(name, binary)

    def test_wrong_context_changed_result_is_detected(self):
        before = 'return changed;'
        self.assertEqual(self.code.count(before), 1)
        binary = self.folder / 'wrong-result'
        self.compile(self.code.replace(before, 'return changed * 0;', 1), binary)
        self.run_case('prepare', binary, passed=False)

    def test_publishing_after_failed_derive_is_detected(self):
        before = 'else if (composite_derive(composite, level))'
        self.assertEqual(self.code.count(before), 1)
        binary = self.folder / 'wrong-failure'
        self.compile(self.code.replace(before, 'else if ((composite_derive(composite, level), 1))', 1), binary)
        self.run_case('failure', binary, passed=False)

    def test_derived_stamp_guard_omission_is_detected(self):
        before = '!composite->level_derived[level] && target->last_used'
        self.assertEqual(self.code.count(before), 1)
        binary = self.folder / 'wrong-provenance-guard'
        self.compile(self.code.replace(before, 'target->last_used', 1), binary)
        self.run_case('collision', binary, passed=False)

    def test_retained_derived_provenance_is_detected(self):
        before = ('composite->level_written[level] = target->last_used ? target->last_used : 1;\n'
                  '\t\tcomposite->level_derived[level] = 0;')
        self.assertEqual(self.code.count(before), 1)
        binary = self.folder / 'wrong-provenance-clear'
        self.compile(self.code.replace(before, before.replace('composite->level_derived[level] = 0;', '(void)0;'), 1), binary)
        self.run_case('provenance', binary, passed=False)

    def test_invalidated_derived_written_stamp_is_also_cleared(self):
        before = 'composite->level_written[level] = 0;'
        self.assertEqual(self.code.count(before), 1)
        binary = self.folder / 'wrong-invalidation-written-clear'
        self.compile(self.code.replace(before, '(void)0;', 1), binary)
        self.run_case('parent-and-explicit', binary, passed=False)


if __name__ == '__main__':
    unittest.main()
