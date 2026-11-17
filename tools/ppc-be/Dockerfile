# Big-endian 32-bit PowerPC Linux cross build of OpenCE, run under qemu-ppc.
# The stand-in for the Wii U's Espresso (big-endian, ILP32) while the
# endianness work is done: see WIIU_PORT_PLAN.md, section 6.1.
FROM debian:trixie

RUN apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
        clang lld llvm cmake ninja-build python3 python3-pytest make file \
        gcc-powerpc-linux-gnu libc6-dev-powerpc-cross binutils-powerpc-linux-gnu \
        gcc-i686-linux-gnu libc6-dev-i386-cross \
        qemu-user gdb-multiarch ca-certificates curl xz-utils python3-clang-19 libclang-19-dev \
    && rm -rf /var/lib/apt/lists/*

# SDL3 at the other ports' version (tools/linux_sysroot.py), with no video
# or audio drivers but SDL's dummy and offscreen ones: the null renderer and
# headless runs are all this build is for.
ARG SDL_VERSION=3.4.16
ARG SDL_SHA256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68
COPY toolchain-ppc.cmake /opt/toolchain-ppc.cmake
RUN curl -fsSL -o /tmp/sdl.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VERSION}/SDL3-${SDL_VERSION}.tar.gz \
    && echo "${SDL_SHA256}  /tmp/sdl.tar.gz" | sha256sum -c - \
    && mkdir /tmp/sdl && tar -xzf /tmp/sdl.tar.gz -C /tmp/sdl --strip-components=1 \
    && cmake -S /tmp/sdl -B /tmp/sdl-build -G Ninja -DCMAKE_TOOLCHAIN_FILE=/opt/toolchain-ppc.cmake \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/powerpc-linux-gnu \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_TEST_LIBRARY=OFF \
        -DSDL_UNIX_CONSOLE_BUILD=ON -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF -DSDL_OPENGL=ON -DSDL_OPENGLES=OFF -DSDL_VULKAN=OFF \
        -DSDL_ALSA=OFF -DSDL_PULSEAUDIO=OFF -DSDL_PIPEWIRE=OFF -DSDL_JACK=OFF -DSDL_SNDIO=OFF \
        -DSDL_HIDAPI=OFF -DSDL_IBUS=OFF -DSDL_LIBURING=OFF -DSDL_DBUS=OFF -DSDL_LIBUDEV=OFF \
    && ninja -C /tmp/sdl-build install >/dev/null \
    && rm -rf /tmp/sdl /tmp/sdl-build /tmp/sdl.tar.gz

# The same SDL for 32-bit x86, and the x86 build's compiler for this machine
# (clang-i686), to run the x86 build under qemu-i386 beside the big-endian one.
COPY toolchain-i686.cmake /opt/toolchain-i686.cmake
COPY clang-i686 /usr/local/bin/clang-i686
RUN curl -fsSL -o /tmp/sdl.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VERSION}/SDL3-${SDL_VERSION}.tar.gz \
    && echo "${SDL_SHA256}  /tmp/sdl.tar.gz" | sha256sum -c - \
    && mkdir /tmp/sdl && tar -xzf /tmp/sdl.tar.gz -C /tmp/sdl --strip-components=1 \
    && cmake -S /tmp/sdl -B /tmp/sdl-build -G Ninja -DCMAKE_TOOLCHAIN_FILE=/opt/toolchain-i686.cmake \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/i686-linux-gnu \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_TEST_LIBRARY=OFF \
        -DSDL_UNIX_CONSOLE_BUILD=ON -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF -DSDL_OPENGL=ON -DSDL_OPENGLES=OFF -DSDL_VULKAN=OFF \
        -DSDL_ALSA=OFF -DSDL_PULSEAUDIO=OFF -DSDL_PIPEWIRE=OFF -DSDL_JACK=OFF -DSDL_SNDIO=OFF \
        -DSDL_HIDAPI=OFF -DSDL_IBUS=OFF -DSDL_LIBURING=OFF -DSDL_DBUS=OFF -DSDL_LIBUDEV=OFF \
    && ninja -C /tmp/sdl-build install >/dev/null \
    && rm -rf /tmp/sdl /tmp/sdl-build /tmp/sdl.tar.gz

ENV QEMU_LD_PREFIX=/usr/powerpc-linux-gnu
WORKDIR /src
