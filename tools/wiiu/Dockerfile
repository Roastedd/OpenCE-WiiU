# OpenCE for the Wii U: clang 19 (the compiler the Linux ports and the big-endian
# qemu-ppc loop are validated with) compiling for powerpc-eabi against devkitPPC's
# newlib and wut, and devkitPPC's linker, elf2rpl and wuhbtool making the RPX.
#
# Built from the two local images (no download): opence-ppc-be (tools/ppc-be) for clang
# and the python tooling, devkitpro/devkitppc for the Wii U toolchain.
#   docker build -t opence-wiiu tools/wiiu
FROM devkitpro/devkitppc:latest AS devkitpro
FROM opence-ppc-be:latest
COPY --from=devkitpro /opt/devkitpro /opt/devkitpro
ENV DEVKITPRO=/opt/devkitpro \
    DEVKITPPC=/opt/devkitpro/devkitPPC \
    WUT_ROOT=/opt/devkitpro/wut \
    PATH=/opt/devkitpro/devkitPPC/bin:/opt/devkitpro/tools/bin:$PATH
COPY wiiu-clang /usr/local/bin/wiiu-clang
