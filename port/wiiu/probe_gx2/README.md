# Wii U GX2 probe

The GPU half of the plan's phase 0 (`WIIU_PORT_PLAN.md`, section 6.4): a small Wii U application that answers, on a console,
the questions the renderer's design waits for. The first probe (`../probe`) measures memory, the FPU and speed; this one draws.
Each test is drawn on the TV and the GamePad and read back by the CPU; the numbers go to a text report on the SD card.

**Nothing here has run on a console.** It builds with devkitPPC's GCC and with clang (`-Wall -Wextra -Werror`), is clean under
clang's analyzer, its portable logic is tested on three machines (ARM64, x86, big-endian PowerPC), and its stages are run in a
simulation with GPUs that behave as eleven different scenarios say (below). What none of that can say is how a Latte behaves; that
is what the run is for.

## What it answers

| Question | Answer in the report (`[answers]`) | Decides |
| --- | --- | --- |
| Does CafeGLSL run on the console, and compile what OpenCE's generator writes? | `cafeglsl.library`, `cafeglsl.compiles`, `cafeglsl.compile_ms` | Plan 6.4: compile on the console (like the Switch port's UAM), or only ahead of time on a PC. Milliseconds a shader takes decide whether a cache on the SD card and a background compile are essential. If the library does not run or refuses the dialect, the translator must emit GX2 microcode itself (plan section 8, risk 3). |
| Does a shader compiled on the console draw right, and is it the Mac compiler's output? | `cafeglsl.draw_runtime`, `cafeglsl.draw_offline`, `cafeglsl.same_as_mac` | Whether shaders can be compiled on a desktop and shipped as GSH files (the same bytes), and whether the generator's vertex program computes the clip position as intended. |
| In what byte order does the GPU read BC1, BC2 and BC3 (DXT1, 3 and 5) blocks? | `texture.bc1`, `texture.bc2`, `texture.bc3` | The texture loader: which rearrangement of each block's bytes the Xbox's data needs on its way to GX2 (or none). |
| Do little-endian vertex streams work through `GX2AttribStream.endianSwap`? | `vertex.float32x4`, `float32x2`, `d3dcolor`, `snorm16x4`, `short2`, `normpacked3` | The vertex declarations of the GX2 backend: the swap and component mask per format, or a conversion at load. Also what `GX2_ENDIAN_SWAP_DEFAULT` does. |
| Do `GX2_INDEX_TYPE_U16_LE` and `U32_LE` draw right? | `index.u16_le`, `index.u32_le` (and `index.u16`, `index.u32` for big-endian data) | Whether index buffers need converting at all. `index.control` says that a mismatch fails, so that a pass means something. |
| Can the CPU read what the GPU drew? | `readback`, `texture.rgba8` | Whether the numbers above can be trusted. They are judged by reading a 64 x 64 linear RGBA8 color buffer; the control stage checks that first. If it fails, only the pictures on the screen remain. |

## Running it

Build (in the `opence-wiiu` container, from the repository around OpenCE; the container downloads nothing, so CafeGLSL is fetched
once, outside it):

    tools/wiiu/fetch_cafeglsl.sh                    # CafeGLSL v1.0.0-rc1 (Wii U library 2.86 MB, macOS compiler 3.28 MB), SHA-256 checked
    tools/wiiu/build_probe_gx2.sh gcc dist          # builds clang, gcc, and clang without the compiler

Copy the folder `port/wiiu/probe_gx2/dist/wiiu` into the SD card's root (it merges into `wiiu/apps/opence-probe-gx2/`). Start
`opence-probe-gx2-clang.wuhb` from the Aroma menu (clang is what the port is built with, so its answers are the ones that count).
Nothing else needs to be on the card. Start it from a fresh start of the console, with nothing else open and nothing being saved.

What you see: a screen that says what is running, then, when the stages are done, the pages. **A** (or the d-pad right, **R**,
**ZR**) is the next page, **B** (left, **L**, **ZL**) the previous, **HOME** leaves. The first page lists every answer; the others
draw the tests:

- `controls`: the clear color and the BC patterns decoded by the CPU, the pictures the tests are compared with.
- `index buffers`: eight tiles (four types, two data orders). Three triangles = read as written.
- `little-endian vertex streams`: six formats by five columns. The first tile of a row is the value the game means; a green
  frame is a setting that delivers it.
- `BC1, BC2 and BC3 byte order`: the pattern, then the upload orders. The green one is the order the GPU reads.
- `CafeGLSL on the console`: the compiles, and the triangle drawn with the generator's vertex shader.

Take `report-clang.txt` (and `crash-clang.txt` if there is one) from `sd:/wiiu/apps/opence-probe-gx2/`. Then, if you like,
`opence-probe-gx2-gcc.wuhb`, to see whether the two compilers agree. If you cannot copy a file, a photo of the first page and
words on the others ("tile 3 looks right") answer most of it.

**If the console stops**, hold the power button. The report is written after every stage and before each compile, so it ends at
the step that did not return; that is a finding, send it. The stages that only draw come first; CafeGLSL, which is 22 MB of
someone else's C and C++ run on a thread with a 4 MiB stack, is last. **If the program does not start at all** (a black screen, no
report), the library's constructors may be the cause: run `opence-probe-gx2-clang-nocompiler.wuhb`, which is the same program
without CafeGLSL and answers everything else.

It writes nothing but the two files; it touches no storage of the console's.

## Reading the answers

A few lines as they come, with what each decides:

    texture.bc1 = draws right when uploaded: swap32; the GPU reads a block as: swap32

The Xbox's block (two little-endian 16-bit colors, 32 bits of 2-bit indices) reaches the GPU right when every 32-bit word of it is
byte-reversed first. The loader applies that to each block (plus tiling, which is separate). `as it is` would mean nothing is
needed. If no named order draws right the line says `none of the orders tried draws right`, names what the GPU reads (found by
trying all 40320 rearrangements of the eight bytes against what it drew) and whether uploading that order drew right.

    vertex.d3dcolor = little-endian stream works with swap: none, 8_IN_16, 8_IN_32; DEFAULT behaves as 8_IN_32

D3DCOLOR is bytes B, G, R, A in memory; each swap needs its own component mask to deliver (r, g, b, a), and the probe draws each with
its mask. A swap not listed does not deliver the game's value (the report line has the pixels). `DEFAULT behaves as 8_IN_32` is
what GX2 does to a stream a big-endian CPU wrote: right for the CPU's own floats, wrong for the Xbox's.

    index.u16_le = reads the indices as written

The 32-bit types are drawn only with the data order they read: a mismatch would index 150 million vertices.

    cafeglsl.compiles = 8 of 8 shader pairs compiled (8 tried); the generator's ES dialect (#version 300 es) is accepted
    cafeglsl.compile_ms = 1203 ms in all, slowest 410 ms: attr 40, vs00+diffuse 120, ...

(Made-up numbers; the point is the form.) `cafeglsl.same_as_mac` compares the program bytes of the pair compiled on the console with
the same pair compiled by the Mac's `glslcompiler`: identical bytes mean shaders can be built ahead of time on a PC, and cached
on the SD card.

The report's other sections: `[system]` (GX2 buffers, the embedded GSH files), `[control]`, `[index]`, `[vertex]` (every draw's
pixels), `[textures]` (every variant's error count), `[glsl]` (each compile, its log, each shader's inputs, uniforms and samplers,
whether the vertex outputs and pixel inputs meet) and `[summary]`.

## What was established without a console

From sources, and checked here:

- **CafeGLSL is not part of the Wii U's system.** "By default, Wii U offers no way to compile shaders dynamically during runtime"
  (its README). It is Exzap's Mesa fork (github.com/Exzap/CafeGLSL). Versions up to v0.2.0 were `glslcompiler.rpl`, a file on the
  SD card (`wiiu/libs/glslcompiler.rpl`) loaded at run time; **v1.0.0-rc1 (2026-09-19, a pre-release) is a static library** that adds about
  1.75 MB to the program, with a different API (`InitGLSLCompiler`, `CompileShaderPair`, ...; the header reports its version as
  `v0.3.0`). The plan's assumption of a system-provided `glslcompiler.rpl` was wrong on both counts. wut 1.9.1 has no header, import
  library or sample for it.
- It also has a command line compiler for Linux, macOS and Windows, which writes `.gsh` files. **That compiler compiled all 143
  shaders OpenCE's generator writes** for the ES path (every one of the game's 67 vertex programs as written, with its normal as a
  packed integer, and in the five lit forms, and the text drawing's pixel shaders: `results/cafeglsl-host-survey.txt`,
  `tools/wiiu/gx2_probe_host_survey.sh`). That is the same compiler as the library's; it does not say how long the Espresso takes or
  whether the output draws right.
- The numbers CafeGLSL gives its varyings (alphabetical by name) are the same in both stages when both declare the same names,
  which the generator's shaders do; the probe checks it for every compiled pair.
- GSH files made by the Mac's compiler load with wut's own loader (`WHBGfxLoadGFDShaderGroup`), which the probe uses for its drawing
  shaders, so the texture, vertex and index tests do not depend on the library running.

Assumptions the probe checks as it runs (each shows in the report if it fails): a linear aligned color buffer can be drawn into and
read after `GX2DrawDone`; texture rows run top to bottom; a compressed surface's pitch counts texels (if it counts blocks, the size
says so); the loose uniforms' offsets count 32-bit words (`GX2SetVertexUniformReg(offset, count, ...)`, as the compiled shader's
`uniformVars` say).

## How it was tested

    tools/wiiu/probe_gx2_tests.sh      # about a minute

- The portable modules (`bc`, `readback`, `vertex_test`, `index_test`, `scene`, `font_mini`, `gsh_check`, `iface_check`, `answers`)
  on ARM64 under the undefined-behaviour sanitizer, on 32-bit x86 and on 32-bit big-endian PowerPC: BC1/BC2/BC3 decoding against hand-computed
  texels; every named byte order and a made-up one recovered by the search from a simulated GPU; the vertex and index models, with the
  proof that the test vectors tell the swaps apart; every pixel the tests look at lies inside the triangle it belongs to; the embedded
  GSH files are well formed GFD files; the embedded shaders are the sources' (`tools/embed_shaders.py --check`).
- The stages themselves (`tests/test_stages_mock.c`, `tests/mock/mock_gx2.c`), on big-endian PowerPC under qemu, against a stand-in for
  GX2, WHB, coreinit and CafeGLSL with a small GPU behind it that draws what the portable models say a GPU with the scenario's
  behaviour draws. Eleven scenarios: a well-behaved console; target bytes in the order B, G, R, A; a BC1 order none of the named ones
  is; `_LE` index types reading as swapped; the compiler refusing the ES dialect; textures upside down; a compressed pitch counting
  blocks; a target the CPU cannot read; blocks read as they are; `DEFAULT` not swapping; a runtime compile that differs from the
  Mac's. Each asserts what the answers and the report say, and that every page draws. This tests the probe's own glue (which pixels
  it looks at, how it compares, how it concludes, how it words it); it cannot say how a real GPU behaves.
- `make analyze` (clang's static analyzer), and both compilers with `-Werror`.

## Layout

- `src/` is portable C, the same on the console and on a desktop: `bc` (the pattern, the decoder, the byte orders, the search that
  explains a drawing), `readback`, `vertex_test`, `index_test`, `scene` (the triangles and the pixels looked at), `font_mini` (a 3 x 5
  font, so that the pages need nothing but a texture), `gsh_check`, `iface_check`, `answers`; `shader_glsl.c` and `shader_gsh.c` are
  written by `tools/embed_shaders.py` from `shaders/` (the probe's own GLSL) and `generated/` (what OpenCE's generator writes, from
  `tools/wiiu/gx2_probe_shaders.sh`). The report is the first probe's (`../probe/src/report.c`, not copied).
- `src/wiiu/` is the console's: `main.c` (the order of the stages, the pages), `gx` (a thin layer over GX2), `ui` and `pages` (quads and
  text), the stages `stage_system`, `stage_control`, `stage_index`, `stage_vertex`, `stage_textures`, `stage_glsl`, and `card`, `crash`
  (a fault handler that writes `crash-<compiler>.txt`) and `clock`, which are the first probe's with another folder.
- `tests/` the host tests and the mock console; `tools/` the embedding script and the generator harness.
- `results/` what was learned on a desktop: the compile survey.
