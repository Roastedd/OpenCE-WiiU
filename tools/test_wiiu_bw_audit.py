"""The Broadway kernel objects' allowlist (tools/wiiu_check_objects.py --bw-kernel-object): what passes, and what fails."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import wiiu_check_objects as checker  # noqa: E402


def listing(*lines):
    return "x.o:     file format elf32-powerpc\n\nDisassembly of section .text:\n\n00000000 <f>:\n" + "".join(
        f"   {index * 4:x}:\t{line}\n" for index, line in enumerate(lines))


def test_the_exact_instructions_pass():
    text = listing("lfs     f0,0(r3)", "psq_l   f2,4(r3),0,0", "ps_muls0 f10,f2,f8", "ps_add  f10,f10,f12", "ps_sum0 f4,f4,f4,f4",
                   "fcmpu   cr0,f4,f0", "cror    eq,gt,eq", "beq-    ac <f+0xac>", "bge-    e8 <f+0xe8>", "bso-    cr1,e8 <f+0xe8>",
                   "mfspr   r5,901", "mfspr   r0,896", "mtspr   901,r3", "psq_st  f10,40(r5),0,0", "blr")
    assert checker.bw_violations(text) == {}


def test_fused_and_estimating_instructions_fail():
    for name in ("fmadds  f1,f2,f3,f4", "fmsubs  f1,f2,f3,f4", "fnmadds f1,f2,f3,f4", "fmadd   f1,f2,f3,f4", "ps_madd f1,f2,f3,f4",
                 "ps_madds0 f1,f2,f3,f4", "ps_msub  f1,f2,f3,f4", "ps_nmadd f1,f2,f3,f4", "ps_nmsub f1,f2,f3,f4", "ps_res   f1,f2",
                 "ps_rsqrte f1,f2", "frsqrte f1,f2", "fres    f1,f2", "fdivs   f1,f2,f3", "fsqrt   f1,f2", "ps_div  f1,f2,f3",
                 "ps_sel   f1,f2,f3,f4", "fmul    f1,f2,f3", "frsp    f1,f2"):
        assert checker.bw_violations(listing(name)), name


def test_control_registers_and_unlisted_special_registers_fail():
    for name in ("mtspr   917,r3", "mfspr   r5,917", "mfspr   r5,895", "mfspr   r5,904", "mtfsf   255,f1", "mtfsb1  29", "mtmsr   r3",
                 "mffs    f0", "mtspr   896,r3", "mtspr   900,r3", "mtspr   8,r3", "mtspr   920,r3", "bl      0 <f>", "bctr", "sc"):
        assert checker.bw_violations(listing(name)), name


def test_a_prediction_hint_is_not_part_of_the_name():
    assert checker.bw_violations(listing("blt+    10 <f+0x10>", "bge-    10 <f+0x10>")) == {}


def test_the_host_model_build_is_asked_for_by_the_environment(monkeypatch):
    from tools import linux_build

    monkeypatch.delenv("HALO_BW_MODEL", raising=False)
    assert linux_build.bw_model_defines() == []
    monkeypatch.setenv("HALO_BW_MODEL", "1")
    assert linux_build.bw_model_defines() == ["-DHALO_BW_KERNELS", "-DHALO_BW_MODEL"]
