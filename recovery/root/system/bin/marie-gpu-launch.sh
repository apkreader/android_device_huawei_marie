#!/system/bin/sh
export PATH=/system/bin:/sbin:/vendor/bin
mkdir -p /gpu
mkdir -p /gpu/stock-system /gpu/stock-vendor
mount_stock() {
    grep -qs " $2 " /proc/mounts || mount -o ro "$1" "$2"
}
if mount_stock /dev/block/by-name/system /gpu/stock-system &&
   mount_stock /dev/block/by-name/vendor /gpu/stock-vendor; then
    sh /system/bin/marie-gpu-libraries.sh
    resetprop ro.aera.gpu.mali_driver /gpu/stock-vendor/lib64/egl/libGLES_mali.so
    resetprop ro.aera.gpu.mali_libraries /gpu/lib64
    resetprop ro.aera.gpu.gralloc /gpu/stock-vendor/lib64/hw/gralloc.kirin710.so
    resetprop ro.aera.gpu.hwcomposer /gpu/stock-vendor/lib64/hw/hwcomposer.kirin710.so
    resetprop ro.aera.gpu.mali_permitted_paths /gpu:/vendor
    resetprop ro.aera.gpu.hwc_acquire_fence_ownership composer
    resetprop ro.aera.gpu.native_fence true
    resetprop ro.aera.gpu.mali_buffers 3
fi
exec /system/bin/recovery
