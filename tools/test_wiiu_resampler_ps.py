"""Audio PS readiness policy on the host; exact object contract with devkitPPC."""
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import wiiu_resampler_ps_audit as audit

SOURCE = ROOT / "port/wiiu/src/wiiu_resampler_ps.c"
ASSEMBLY = ROOT / "port/wiiu/src/wiiu_resampler_ps.S"
CHECKER = ROOT / "tools/wiiu_check_objects.py"
CONTAINER = shutil.which("powerpc-eabi-gcc") and shutil.which("powerpc-eabi-objdump")
needs_container = pytest.mark.skipif(not CONTAINER, reason="needs the existing opence-wiiu toolchain")


@pytest.fixture(scope="module")
def policy_program(tmp_path_factory):
    folder = tmp_path_factory.mktemp("ps-policy")
    fixture, executable = folder / "policy.c", folder / "policy"
    fixture.write_text(r'''
#define WIIU_RESAMPLER_PS_TEST
#include "BACKEND"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned core, fpscr = 4, gqr, reads, kernels, bad;
unsigned wiiu_resampler_ps_test_core(void) { reads++; return core; }
unsigned wiiu_resampler_ps_test_fpscr(void) { reads++; return fpscr; }
unsigned wiiu_resampler_ps_test_gqr0(void) { reads++; return gqr; }
static void corrupt(unsigned kind, float *out) {
    if (bad == kind) out[0] += 1.0f;
    if (bad == 5 && kind == 4) out[2] = 0;
    kernels++;
}
void wiiu_resampler_ps_dot48(const float *a, const float *w, float *o) {
    o[0] = reference(a,w,0,0); corrupt(1,o);
}
void wiiu_resampler_ps_dot48_stereo(const float *a, const float *b, const float *w, float *o) {
    o[0] = reference(a,w,0,0); o[1] = reference(b,w,0,0); corrupt(2,o);
}
void wiiu_resampler_ps_blended48(const float *a, const float *w, const float *d, const float *f, float *o) {
    o[0] = reference(a,w,d,*f); corrupt(3,o);
}
void wiiu_resampler_ps_blended48_stereo(const float *a, const float *b, const float *w, const float *d, const float *f, float *o) {
    o[0] = reference(a,w,d,*f); o[1] = reference(b,w,d,*f); corrupt(4,o);
}
int main(int argc, char **argv) {
    struct wiiu_resampler_ps_status status;
    unsigned reason = WIIU_RESAMPLER_PS_READY;
    assert(argc == 2 && !wiiu_resampler_ps_ready() && !wiiu_resampler_ps_check_chunk());
    if (!strcmp(argv[1],"disabled")) {
        assert(!wiiu_resampler_ps_worker_begin(0));
        assert(!reads && !kernels && !wiiu_resampler_ps_worker_begin(1));
        assert(wiiu_resampler_ps_take_report(&status) && status.result == WIIU_RESAMPLER_PS_DISABLED);
        assert(!wiiu_resampler_ps_take_report(&status)); return 0;
    }
    if (!strcmp(argv[1],"core")) { core=1; reason=WIIU_RESAMPLER_PS_WRONG_CORE; }
    if (!strcmp(argv[1],"mode")) { fpscr=0; reason=WIIU_RESAMPLER_PS_FPSCR_MODE; }
    if (!strcmp(argv[1],"gqrload")) { gqr=0x10000; reason=WIIU_RESAMPLER_PS_GQR_TYPE; }
    if (!strcmp(argv[1],"gqrstore")) { gqr=1; reason=WIIU_RESAMPLER_PS_GQR_TYPE; }
    if (!strncmp(argv[1],"bad",3)) { bad=(unsigned)atoi(argv[1]+3); reason=WIIU_RESAMPLER_PS_SELFTEST; }
    if (reason != WIIU_RESAMPLER_PS_READY) {
        assert(!wiiu_resampler_ps_worker_begin(1));
        assert(wiiu_resampler_ps_take_report(&status) && status.result==reason);
        if (reason != WIIU_RESAMPLER_PS_SELFTEST) assert(!kernels);
        else assert(status.failed_case && status.selftest_cases);
        core=0; fpscr=4; gqr=bad=0;
        assert(!wiiu_resampler_ps_worker_begin(1) && !wiiu_resampler_ps_check_chunk());
        assert(!wiiu_resampler_ps_take_report(&status)); return 0;
    }
    assert(wiiu_resampler_ps_worker_begin(1) && wiiu_resampler_ps_ready());
    assert(kernels==256);
    assert(wiiu_resampler_ps_take_report(&status) && status.result==WIIU_RESAMPLER_PS_READY);
    assert(status.selftest_cases==64 && !status.failed_case && !wiiu_resampler_ps_take_report(&status));
    fpscr=0x82028004; gqr=0x3f003f00; /* sticky flags and float scale fields are allowed */
    assert(wiiu_resampler_ps_check_chunk());
    if (!strcmp(argv[1],"changedmode")) { fpscr=5; reason=WIIU_RESAMPLER_PS_FPSCR_MODE; }
    if (!strcmp(argv[1],"changedgqr")) { gqr=0x10000; reason=WIIU_RESAMPLER_PS_GQR_TYPE; }
    if (!strcmp(argv[1],"changedcore")) { core=2; reason=WIIU_RESAMPLER_PS_WRONG_CORE; }
    if (reason != WIIU_RESAMPLER_PS_READY) {
        assert(!wiiu_resampler_ps_check_chunk() && !wiiu_resampler_ps_ready());
        assert(wiiu_resampler_ps_take_report(&status) && status.result==reason);
        core=0; fpscr=4; gqr=0;
        assert(!wiiu_resampler_ps_worker_begin(1) && !wiiu_resampler_ps_check_chunk());
        assert(!wiiu_resampler_ps_take_report(&status));
    }
    assert(kernels==256);
    assert(strcmp(wiiu_resampler_ps_reason(reason),"unknown"));
    return 0;
}
'''.replace("BACKEND", SOURCE.as_posix()))
    subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-O2", "-ffp-contract=off", "-Wall", "-Wextra", "-Werror",
                    str(fixture), "-o", str(executable)], check=True, capture_output=True, text=True)
    return executable


@pytest.mark.parametrize("case", ["disabled", "core", "mode", "gqrload", "gqrstore", "ready",
                                  "changedmode", "changedgqr", "changedcore", "bad1", "bad2", "bad3", "bad4", "bad5"])
def test_readiness_latches_and_reports_once(policy_program, case):
    subprocess.run([str(policy_program), case], check=True, capture_output=True, text=True)


def test_readiness_does_not_install_handlers_or_write_control_registers():
    source = SOURCE.read_text()
    assert "OSSetException" not in source
    assert not any(instruction in source for instruction in ("mtfs", "mtspr", "mtmsr", "mthid"))


@pytest.fixture
def assemble(tmp_path):
    def build(source=ASSEMBLY.read_text(), name="kernel"):
        path, obj = tmp_path / (name + ".S"), tmp_path / (name + ".o")
        path.write_text(source)
        subprocess.run(["powerpc-eabi-gcc", "-c", "-g", "-mcpu=750", "-meabi", "-mhard-float", str(path), "-o", str(obj)],
                       check=True, capture_output=True, text=True)
        return obj
    return build


@needs_container
def test_only_the_exact_requested_object_has_the_strict_exception(assemble, tmp_path):
    obj = assemble()
    assert audit.audit(obj) == {"psq_l": 288, "ps_mul": 192, "ps_add": 192, "ps_sum0": 12}
    normal = subprocess.run([sys.executable, str(CHECKER), "--quiet", str(obj)], capture_output=True, text=True)
    assert normal.returncode == 1 and "psq_l" in normal.stdout
    accepted = subprocess.run([sys.executable, str(CHECKER), "--quiet", "--audio-ps-object", str(obj), str(obj)],
                              capture_output=True, text=True)
    assert accepted.returncode == 0, accepted.stdout + accepted.stderr
    other = tmp_path / "other" / obj.name
    other.parent.mkdir(); other.write_bytes(obj.read_bytes())
    duplicate = subprocess.run([sys.executable, str(CHECKER), "--quiet", "--audio-ps-object", str(obj), str(obj), str(other)],
                               capture_output=True, text=True)
    assert duplicate.returncode == 1 and str(other) in duplicate.stdout


@needs_container
@pytest.mark.parametrize("before,after", [
    ("ps_mul 6,4,2", "fmadds 6,4,2,0"),
    ("ps_mul 6,4,2", "ps_madd 6,4,2,0"),
    ("ps_mul 6,4,2", "ps_mul. 6,4,2"),
    ("ps_mul 6,4,2", "ps_mul 14,4,2"),
    ("ps_mul 6,4,2", "mtspr 896,3"),
    ("ps_mul 6,4,2", '.machine "power7"\n\tfsqrt 6,4\n\t.machine "broadway"'),
    ("psq_l 2,\\base(4),0,0", "psq_l 2,\\base(4),0,1"),
    ("psq_l 2,\\base(4),0,0", "psq_st 2,\\base(4),0,0"),
    ("psq_l 2,\\base(4),0,0", "psq_lu 2,\\base(4),0,0"),
    (".irp base,0,16,32,48,64,80,96,112,128,144,160,176", ".irp base,0,16,32,48,64,80,96,112,128,144,160,160"),
    ("lfs 1,wiiu_resampler_ps_zero@l(12)", "lfs 1,wiiu_resampler_ps_zero@l(12)\naddi 13,3,16"),
    ("stfs 0,0(5)", "stfs 0,8(5)"),
    ("wiiu_resampler_ps_zero@ha", "unapproved_external@ha"),
    (".long 0", ".long 1"),
    (".long 0", ".long unapproved_external"),
    ("ps_sum0 0,0,0,0", "ps_sum0 0,1,1,1"),
    (".section .rodata.wiiu_resampler_ps", ".long 0\n\t.section .rodata.wiiu_resampler_ps"),
])
def test_audio_audit_rejects_opcode_operand_relocation_and_extent_changes(assemble, before, after):
    source = ASSEMBLY.read_text()
    assert before in source
    obj = assemble(source.replace(before, after, 1))
    with pytest.raises(ValueError):
        audit.audit(obj)


@needs_container
def test_audio_audit_rejects_an_extra_function(assemble):
    extra = '\n.section .text.wiiu_resampler_ps,"ax",@progbits\n.global extra\n.type extra,@function\nextra: blr\n.size extra,.-extra\n'
    with pytest.raises(ValueError):
        audit.audit(assemble(ASSEMBLY.read_text() + extra))


def test_audio_audit_fails_closed_on_malformed_elf(tmp_path):
    obj = tmp_path / "bad.o"
    obj.write_bytes(b"invalid ELF")
    with pytest.raises(ValueError):
        audit.audit(obj)
