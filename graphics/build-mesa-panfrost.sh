#!/usr/bin/env bash
# Copyright (C) 2026 apkreader
# SPDX-License-Identifier: Apache-2.0
#
# Reproducibly cross-compiles Mesa 24.3.4 Gallium Panfrost (Mali-G51) for
# Huawei P30 lite (marie, arm64 + arm32) using LineageOS 24's Clang + NDK
# sysroot + built libdrm, and installs the stripped outputs into
# vendor/huawei/marie/mesa/lib{,64}/.

set -euo pipefail

MESA_VERSION="${MESA_VERSION:-24.3.4}"
MESA_URL="https://archive.mesa3d.org/mesa-${MESA_VERSION}.tar.xz"
TOP="$(cd "$(dirname "$0")/../../../.." && pwd)"
WORK="${WORK_DIR:-${TOP}/out/mesa-panfrost-build}"
OUT_MESA="${TOP}/vendor/huawei/marie/mesa"

CLANG_DIR="$(ls -d "${TOP}"/prebuilts/clang/host/linux-x86/clang-* | sort -V | tail -n1)"
SYSROOT="${TOP}/out/soong/ndk/sysroot"
LIBDRM_INC="${TOP}/external/libdrm"
PRODUCT_VENDOR="${TOP}/out/target/product/marie/vendor"

if [ ! -d "${SYSROOT}" ] || [ ! -f "${PRODUCT_VENDOR}/lib64/libdrm.so" ]; then
    echo "Run 'breakfast marie && mka ndk_sysroot libdrm' first." >&2
    exit 1
fi

mkdir -p "${WORK}"
cd "${WORK}"

if [ ! -f "mesa-${MESA_VERSION}.tar.xz" ]; then
    curl -fsSL -o "mesa-${MESA_VERSION}.tar.xz" "${MESA_URL}"
fi
if [ ! -d "mesa-${MESA_VERSION}" ]; then
    tar -xf "mesa-${MESA_VERSION}.tar.xz"
fi

build_arch() {
    local arch="$1"
    local triple="$2"
    local cpu_family="$3"
    local cpu="$4"
    local libdir="$5"
    local ndk_lib_triple="$6"

    local pc_dir="${WORK}/pc-${arch}"
    local bdir="${WORK}/build-${arch}"
    local stage="${WORK}/stage-${arch}"
    mkdir -p "${pc_dir}"

    cat > "${pc_dir}/libdrm.pc" <<PC
prefix=/vendor
libdir=${PRODUCT_VENDOR}/${libdir}
includedir=${LIBDRM_INC}
Name: libdrm
Description: Userspace interface to kernel DRM services
Version: 2.4.122
Libs: -L\${libdir} -ldrm
Cflags: -I\${includedir} -I\${includedir}/include/drm
PC

    cat > "${WORK}/${arch}-clang" <<WRAP
#!/usr/bin/env bash
exec "${CLANG_DIR}/bin/clang" \
  --target=${triple} \
  --sysroot="${SYSROOT}" \
  --start-no-unused-arguments \
  -B"${SYSROOT}/usr/lib/${ndk_lib_triple}/31" \
  -L"${SYSROOT}/usr/lib/${ndk_lib_triple}/31" \
  -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables \
  -fuse-ld=lld \
  --end-no-unused-arguments \
  "\$@"
WRAP
    cat > "${WORK}/${arch}-clang++" <<WRAP
#!/usr/bin/env bash
exec "${CLANG_DIR}/bin/clang++" \
  --target=${triple} \
  --sysroot="${SYSROOT}" \
  --start-no-unused-arguments \
  -B"${SYSROOT}/usr/lib/${ndk_lib_triple}/31" \
  -L"${SYSROOT}/usr/lib/${ndk_lib_triple}/31" \
  -I"${SYSROOT}/usr/include/c++/v1" \
  -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables \
  -static-libstdc++ \
  -fuse-ld=lld \
  --end-no-unused-arguments \
  "\$@"
WRAP
    chmod +x "${WORK}/${arch}-clang" "${WORK}/${arch}-clang++"

    cat > "${WORK}/cross-${arch}.ini" <<CROSS
[binaries]
ar = '${CLANG_DIR}/bin/llvm-ar'
c = '${WORK}/${arch}-clang'
cpp = '${WORK}/${arch}-clang++'
strip = '${CLANG_DIR}/bin/llvm-strip'
pkg-config = 'pkg-config'

[host_machine]
system = 'android'
cpu_family = '${cpu_family}'
cpu = '${cpu}'
endian = 'little'

[properties]
needs_exe_wrapper = true
pkg_config_libdir = '${pc_dir}'
CROSS

    rm -rf "${bdir}" "${stage}"
    meson setup "${bdir}" "${WORK}/mesa-${MESA_VERSION}" \
        --cross-file "${WORK}/cross-${arch}.ini" \
        --buildtype=release \
        --prefix=/vendor \
        --libdir="${libdir}" \
        -Dplatforms=android \
        -Dandroid-libbacktrace=disabled \
        -Dgallium-drivers=panfrost \
        -Dvulkan-drivers= \
        -Degl=enabled \
        -Dgles1=enabled \
        -Dgles2=enabled \
        -Dglx=disabled \
        -Dglvnd=disabled \
        -Dshared-glapi=enabled \
        -Dllvm=disabled \
        -Dlibunwind=disabled \
        -Dzstd=disabled \
        -Dcpp_rtti=true

    ninja -C "${bdir}"
    DESTDIR="${stage}" ninja -C "${bdir}" install

    mkdir -p "${OUT_MESA}/${libdir}/egl"
    "${CLANG_DIR}/bin/llvm-strip" --strip-unneeded \
        -o "${OUT_MESA}/${libdir}/libgallium_dri.so" "${stage}/vendor/${libdir}/libgallium_dri.so"
    "${CLANG_DIR}/bin/llvm-strip" --strip-unneeded \
        -o "${OUT_MESA}/${libdir}/libglapi.so" "${stage}/vendor/${libdir}/libglapi.so"
    "${CLANG_DIR}/bin/llvm-strip" --strip-unneeded \
        -o "${OUT_MESA}/${libdir}/egl/libEGL_mesa.so" "${stage}/vendor/${libdir}/libEGL.so"
    "${CLANG_DIR}/bin/llvm-strip" --strip-unneeded \
        -o "${OUT_MESA}/${libdir}/egl/libGLESv1_CM_mesa.so" "${stage}/vendor/${libdir}/libGLESv1_CM.so"
    "${CLANG_DIR}/bin/llvm-strip" --strip-unneeded \
        -o "${OUT_MESA}/${libdir}/egl/libGLESv2_mesa.so" "${stage}/vendor/${libdir}/libGLESv2.so"
}

build_arch arm64 aarch64-linux-android31 aarch64 armv8 lib64 aarch64-linux-android
build_arch arm32 armv7a-linux-androideabi31 arm armv7a lib arm-linux-androideabi

echo "Installed stripped Mesa ${MESA_VERSION} Panfrost libraries into ${OUT_MESA}"
