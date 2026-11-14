#!/system/bin/sh
set -e
mkdir -p /gpu/lib64
for directory in \
    /gpu/stock-system/system/lib64 \
    /gpu/stock-system/system/lib64/vndk-28 \
    /gpu/stock-system/system/lib64/vndk-sp-28 \
    /gpu/stock-vendor/lib64; do
    for library in "$directory"/*.so; do
        [ -f "$library" ] || continue
        name=${library##*/}
        case "$name" in libc.so|libdl.so|libm.so|libdl_android.so) continue ;; esac
        ln -sf "$library" "/gpu/lib64/$name"
    done
done

for library in libGLESv1_CM.so libGLESv2.so libEGL.so; do
    ln -sf /gpu/stock-vendor/lib64/egl/libGLES_mali.so /gpu/lib64/$library
done
mkdir -p /vendor/lib64/hw
ln -sf /gpu/stock-vendor/lib64/hw/gralloc.kirin710.so /vendor/lib64/hw/gralloc.kirin710.so
