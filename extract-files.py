#!/usr/bin/env -S PYTHONPATH=../../../tools/extract-utils python3
# Copyright (C) 2026 apkreader
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
import re
import shutil
import tempfile

from extract_utils.fixups_blob import blob_fixup
from extract_utils.fixups_lib import lib_fixup_remove, lib_fixups
from extract_utils.main import ExtractUtils, ExtractUtilsModule

DEVICE_DIR = Path(__file__).resolve().parent
VENDOR_DIR = DEVICE_DIR.parents[2] / 'vendor' / 'huawei' / 'marie'
MARIE612_CIL = DEVICE_DIR / 'sepolicy' / 'vendor_sepolicy_marie612.cil'
MARIE612_MARKER = '; Marie Linux 6.12 / Panfrost / HWC3 / SELinux Enforcing'
MARIE612_FC_MARKER = '# Marie Linux 6.12 DRM/Panfrost/Battery labels'
MARIE612_FC_EXTRA = (
    '/dev/dri\t\tu:object_r:gpu_device:s0\n'
    '/dev/dri/card[0-9]+\t\tu:object_r:gpu_device:s0\n'
    '/dev/dri/renderD[0-9]+\t\tu:object_r:gpu_device:s0\n'
    '/sys/devices/platform/spmi-0/0-00/power_supply(/.*)?\t\tu:object_r:sysfs_batteryinfo:s0\n'
    '/sys/devices/platform/e82c0000\\.mali/uevent\t\tu:object_r:sysfs_gpu:s0\n'
)


def append_marie612_fc(ctx, file, file_path, *args, **kwargs):
    path = Path(file_path)
    text = path.read_text(encoding='utf-8')
    if MARIE612_FC_MARKER not in text and '/dev/dri/card[0-9]+' not in text:
        if not text.endswith('\n'):
            text += '\n'
        text += f'\n{MARIE612_FC_MARKER}\n{MARIE612_FC_EXTRA}'
        path.write_text(text, encoding='utf-8')



# Non-extracted assets in vendor/huawei/marie preserved across extract-utils cleanup.
PRESERVED_VENDOR_ENTRIES = ('mesa', 'README.md', 'LICENSE')


def append_marie612_cil(ctx, file, file_path, *args, **kwargs):
    path = Path(file_path)
    text = path.read_text(encoding='utf-8')
    if MARIE612_MARKER not in text:
        extra = MARIE612_CIL.read_text(encoding='utf-8')
        if not text.endswith('\n'):
            text += '\n'
        text += f'\n{MARIE612_MARKER}\n{extra}'
        path.write_text(text, encoding='utf-8')


def stock_name(lib, partition):
    return f'{lib}_marie' if partition == 'vendor' else lib


# The 28.0 mapping translates the old vfat label. The platform labels fuseblk.
perf_event_permission = r'(\((?:capability2|cap2_userns)\s+\([^)]*)\bperf_event\b'
blob_fixups = {
    'vendor/etc/Default_sched_config_cust.xml': blob_fixup()
    .regex_replace(r'^<\?xml version="3\.0"', '<?xml version="1.0"'),
    'vendor/etc/init/android.hardware.bluetooth@1.0-service.rc': blob_fixup()
    .regex_replace(r'(?m)^([ \t]*)//', r'\g<1>#'),
    'vendor/etc/init/connectivity/init.connectivity.hi1102.rc': blob_fixup()
    .regex_replace(
        r'(?m)^service (?:wpa_supplicant|hostapd) /[\s\S]*?(?=\n(?:service |on |#|$))',
        '',
    ),
    'vendor/etc/selinux/vendor_file_contexts': blob_fixup()
    .call(append_marie612_fc),
    'vendor/etc/selinux/plat_pub_versioned.cil': blob_fixup()
    .regex_replace(perf_event_permission, r'\g<1>perfmon'),
    'vendor/etc/vintf/manifest.xml': blob_fixup()
    .regex_replace(
        r'    <hal format="hidl">\n'
        r'        <name>(?:android\.hardware\.graphics\.(?:allocator|composer|mapper)'
        r'|(?:android|vendor\.huawei)\.hardware\.wifi(?:\.hostapd|\.supplicant)?|android\.hardware\.usb)</name>'
        r'[\s\S]*?    </hal>\n',
        '',
    ),
    'vendor/etc/selinux/vendor_sepolicy.cil': blob_fixup()
    .regex_replace(perf_event_permission, r'\g<1>perfmon')
    .regex_replace(r'(?m)^\(genfscon fuseblk / \(u object_r vfat \(.*\)\)\)\n', '')
    .call(append_marie612_cil),
}

# These dependencies are supplied by the VNDK 28 APEX at runtime.
vndk_28_libraries = (
    'android.hardware.bluetooth.a2dp@1.0',
    'android.hardware.configstore@1.0',
    'android.hardware.configstore@1.1',
    'android.hardware.ir@1.0',
    'android.hardware.light@2.0',
    'android.hardware.memtrack@1.0',
    'android.hardware.vibrator@1.0',
)

# Keep the stock filenames when a platform module uses the same name.
suffixed_modules = (
    'android.hardware.audio.effect@4.0-impl',
    'android.hardware.audio@4.0-impl',
    'android.hardware.bluetooth@1.0-impl',
    'android.hardware.bluetooth@1.0-service',
    'android.hardware.camera.provider@2.4-impl',
    'android.hardware.cas@1.0-service',
    'android.hardware.drm@1.0-impl',
    'android.hardware.drm@1.0-service',
    'android.hardware.gatekeeper@1.0-impl',
    'android.hardware.gatekeeper@1.0-service',
    'android.hardware.keymaster@3.0-impl',
    'android.hardware.keymaster@3.0-service',
    'android.hardware.media.omx@1.0-service',
    'android.hardware.renderscript@1.0-impl',
    'android.hardware.secure_element@1.0-service',
    'android.hardware.soundtrigger@2.0-impl',
    'android.hardware.thermal@1.0-impl',
    'android.hardware.thermal@1.0-service',
    'audio.r_submix.default',
    'audio.usb.default',
    'awk',
    'bcc',
    'camera.device@1.0-impl',
    'camera.device@3.2-impl',
    'camera.device@3.3-impl',
    'camera.device@3.4-external-impl',
    'camera.device@3.4-impl',
    'cplay',
    'gralloc.default',
    'libLLVM_android',
    'libOpenCL',
    'libalsautils',
    'libaudiopreprocessing',
    'libavservices_minijail_vendor',
    'libbcc',
    'libbt-vendor',
    'libbundlewrapper',
    'libclearkeycasplugin',
    'libdownmix',
    'libdrmclearkeyplugin',
    'libdynproc',
    'libeffectproxy',
    'libeffects',
    'libeffectsconfig',
    'libhwc2on1adapter',
    'libhwc2onfbadapter',
    'libhwminijail',
    'libkeymaster3device',
    'libldnhncr',
    'libmockcasplugin',
    'libnbaio_mono',
    'libreference-ril',
    'libril',
    'librilutils',
    'libstagefrighthw',
    'libstdc++',
    'libtinycompress',
    'libvisualizer',
    'libwifi-hal-bcm',
    'libwpa_client',
    'local_time.default',
    'power.default',
    'rild',
    'sh',
    'toolbox',
    'toybox_vendor',
    'vibrator.default',
    'vndservice',
    'vndservicemanager',
    'wpa_cli',
)

# Files kept in EMUI 9 OEM format via raw copy-one-file (avoids Soong init-rc
# parser rejections or platform module install path collisions).
LEGACY_VENDOR_FILES = (
    'etc/init/android.hardware.drm@1.1-service.clearkey.rc',
    'etc/init/android.hardware.drm@1.1-service.widevine.rc',
    'etc/init/aptouch_daemon.rc',
    'etc/init/charger/init.charger.rc',
    'etc/init/connectivity/init.connectivity.gps.rc',
    'etc/init/connectivity/init.connectivity.hi1102.rc',
    'etc/init/hinetmanager.rc',
    'etc/init/hw/init.kirin710.rc',
    'etc/init/init.manufacture.rc',
    'etc/init/init.platform.rc',
    'etc/init/modemchr_service.rc',
    'etc/init/modemlog_service.rc',
    'etc/init/recovery/init.recovery.huawei.rc',
    'etc/init/vendor.huawei.hardware.audio@4.0-service.rc',
    'etc/init/vendor.huawei.hardware.biometrics.hwfacerecognize@1.1-service.rc',
    'etc/init/vendor.huawei.hardware.dolby.dms@1.0-service.rc',
    'etc/init/vendor.huawei.hardware.fm@1.0-service.rc',
    'etc/init/vendor.huawei.hardware.hwupdate@1.0-service.rc',
    'etc/init/vendor.huawei.hardware.kds@1.0-service.rc',
    'etc/init/vendor.huawei.hardware.tp@1.0-service.rc',
    'etc/initi/connectivity/init.connectivity.gps.rc',
    'etc/modemConfig/observe_config_encryt.xml',
    'etc/native_packages.xml',
    'bin/hw/android.hardware.media.omx@1.0-service',
    'default.prop',
    'etc/vintf/compatibility_matrix.xml',
    'bin/awk',
    'bin/grep',
    'bin/sh',
    'bin/toolbox',
    'bin/toybox_vendor',
    'bin/vndservice',
    'bin/vndservicemanager',
)

# Platform vendor modules overridden by the EMUI 9 OEM modules installed below.
MANIFEST_OVERRIDES = (
    'android.hardware.audio.effect@4.0-impl',
    'android.hardware.audio@4.0-impl',
    'android.hardware.bluetooth@1.0-impl',
    'android.hardware.bluetooth@1.0-service',
    'android.hardware.camera.provider@2.4-impl',
    'android.hardware.cas@1.0-service',
    'android.hardware.cas@1.2-service',
    'android.hardware.drm@1.0-impl',
    'android.hardware.drm@1.0-service',
    'android.hardware.gatekeeper@1.0-impl',
    'android.hardware.gatekeeper@1.0-service',
    'android.hardware.keymaster@3.0-impl',
    'android.hardware.keymaster@3.0-service',
    'android.hardware.media.omx@1.0-service',
    'android.hardware.renderscript@1.0-impl',
    'android.hardware.secure_element@1.0-service',
    'android.hardware.thermal@1.0-impl',
    'android.hardware.thermal@1.0-service',
    'audio.r_submix.default',
    'audio.usb.default',
    'boringssl_self_test_vendor',
    'camera.device@1.0-impl',
    'camera.device@3.2-impl',
    'camera.device@3.3-impl',
    'camera.device@3.4-external-impl',
    'camera.device@3.4-impl',
    'cplay',
    'gralloc.default',
    'libLLVM_android.vendor',
    'libalsautils.vendor',
    'libavservices_minijail_vendor',
    'libbcc.vendor',
    'libclearkeycasplugin',
    'libdrmclearkeyplugin',
    'libdynproc',
    'libeffectproxy',
    'libeffects',
    'libeffectsconfig.vendor',
    'libhwc2on1adapter',
    'libhwc2onfbadapter',
    'libhwminijail',
    'libkeymaster3device.vendor',
    'libldnhncr',
    'libmockcasplugin',
    'libnbaio_mono',
    'libreference-ril',
    'libril',
    'librilutils',
    'libtinycompress',
    'libvisualizer',
    'libwpa_client',
    'local_time.default',
    'power.default',
    'rild',
    'vibrator.default',
    'wpa_cli',
)

vendor_lib_fixups = {
    **lib_fixups,
    vndk_28_libraries: lib_fixup_remove,
    suffixed_modules: stock_name,
}


def finalize_vendor_tree():
    """Adapt generated Android.bp / marie-vendor.mk / Android.mk for VNDK 28 EMUI 9."""
    bp_path = VENDOR_DIR / 'Android.bp'
    mk_path = VENDOR_DIR / 'marie-vendor.mk'
    amk_path = VENDOR_DIR / 'Android.mk'
    legacy_set = set(LEGACY_VENDOR_FILES)
    suffixed_copy_files = []
    removed_pkgs = set()

    # 1. Transform Android.bp blocks
    bp_text = bp_path.read_text(encoding='utf-8')
    # Split into top-level blocks separated by blank lines
    blocks = re.split(r'\n(?=[a-z0-9_]+ \{)', bp_text)
    new_blocks = []

    for blk in blocks:
        m_type = re.match(r'^([a-z0-9_]+) \{', blk)
        if not m_type:
            new_blocks.append(blk)
            continue
        mod_type = m_type.group(1)
        m_name = re.search(r'^\s*name:\s*"([^"]+)"', blk, re.M)
        mod_name = m_name.group(1) if m_name else ''

        # Rename vendor manifest.xml to avoid module name collisions
        if mod_type == 'prebuilt_etc_xml' and mod_name == 'manifest.xml':
            blk = blk.replace('name: "manifest.xml"', 'name: "marie-vendor-manifest"', 1)
            new_blocks.append(blk)
            continue

        # Drop prebuilt_etc_xml / prebuilt_etc modules that belong to LEGACY_VENDOR_FILES
        srcs = re.findall(r'"(proprietary/vendor/[^"]+)"', blk)
        src_rels = [s[len('proprietary/vendor/'):] for s in srcs]
        if src_rels and all(r in legacy_set for r in src_rels):
            if mod_name:
                removed_pkgs.add(mod_name)
            continue

        if mod_type in ('cc_prebuilt_library_shared', 'cc_prebuilt_binary'):
            # Strip shared_libs arrays and any existing check_elf_files/system_shared_libs
            blk = re.sub(r'\n[ \t]*shared_libs:\s*\[[^\]]*\],?', '', blk)
            blk = re.sub(r'\n[ \t]*check_elf_files:[^\n]*', '', blk)
            blk = re.sub(r'\n[ \t]*system_shared_libs:[^\n]*', '', blk)
            # Inject check_elf_files: false and system_shared_libs: [] right after name
            inject = '    check_elf_files: false,\n    system_shared_libs: [],\n'
            is_suffixed = mod_name.endswith('_marie')
            if is_suffixed:
                inject += '    installable: false,\n'
                for r in src_rels:
                    if r not in legacy_set:
                        suffixed_copy_files.append(r)
                removed_pkgs.add(mod_name)
            blk = re.sub(
                r'(^\s*name:\s*"[^"]+",\n)',
                r'\1' + inject,
                blk,
                count=1,
                flags=re.M,
            )

        new_blocks.append(blk)

    bp_path.write_text('\n'.join(new_blocks), encoding='utf-8')

    # 2. Transform marie-vendor.mk
    mk_lines = mk_path.read_text(encoding='utf-8').splitlines()
    out_lines = []
    for line in mk_lines:
        if 'proprietary/vendor/ueventd.rc:$(TARGET_COPY_OUT_VENDOR)/ueventd.rc' in line:
            line = line.replace(':$(TARGET_COPY_OUT_VENDOR)/ueventd.rc', ':$(TARGET_COPY_OUT_VENDOR)/etc/ueventd.rc')
        stripped = line.strip().rstrip('\\').strip()
        # Rename manifest.xml package reference
        if stripped == 'manifest.xml':
            out_lines.append(line.replace('manifest.xml', 'marie-vendor-manifest'))
            continue
        # Remove packages that are installed via copy-one-file or PRODUCT_COPY_FILES
        if stripped in removed_pkgs:
            continue
        # Remove PRODUCT_COPY_FILES lines handled by LEGACY_VENDOR_FILES
        if 'proprietary/vendor/' in stripped and ':' in stripped:
            rel = stripped.split('proprietary/vendor/', 1)[1].split(':', 1)[0]
            if rel in legacy_set:
                continue
        out_lines.append(line)

    # Fix any trailing backslash left before a blank line or EOF
    fixed_lines = []
    for i, line in enumerate(out_lines):
        if line.rstrip().endswith('\\'):
            nxt = out_lines[i + 1].strip() if i + 1 < len(out_lines) else ''
            if not nxt or nxt.startswith(('PRODUCT_', '#')):
                line = line.rstrip()[:-1].rstrip()
        fixed_lines.append(line)

    if suffixed_copy_files:
        fixed_lines.append('')
        fixed_lines.append('# Suffixed OEM binaries/libraries installed under their stock paths')
        fixed_lines.append('PRODUCT_COPY_FILES += \\')
        unique_suffixed = sorted(set(suffixed_copy_files))
        for idx, rel in enumerate(unique_suffixed):
            cont = ' \\' if idx < len(unique_suffixed) - 1 else ''
            fixed_lines.append(
                f'    vendor/huawei/marie/proprietary/vendor/{rel}:$(TARGET_COPY_OUT_VENDOR)/{rel}{cont}'
            )

    mk_out = '\n'.join(fixed_lines) + '\n'
    assert 'marie-vendor-manifest' in mk_out, 'marie-vendor-manifest missing from marie-vendor.mk!'
    assert '$(TARGET_COPY_OUT_VENDOR)/etc/ueventd.rc' in mk_out, 'etc/ueventd.rc missing from marie-vendor.mk!'
    mk_path.write_text(mk_out, encoding='utf-8')

    # 3. Generate vendor/huawei/marie/Android.mk
    amk_lines = [
        '#',
        '# Automatically generated file. DO NOT MODIFY',
        '#',
        '',
        'LOCAL_PATH := $(call my-dir)',
        '',
        '# The OEM modules are installed below or by marie-vendor.mk.',
        'ETC.marie-vendor-manifest.OVERRIDES += \\',
    ]
    for idx, ov in enumerate(MANIFEST_OVERRIDES):
        cont = ' \\' if idx < len(MANIFEST_OVERRIDES) - 1 else ''
        amk_lines.append(f'    {ov}{cont}')

    amk_lines += [
        '',
        'marie_legacy_vendor_files := \\',
    ]
    for idx, lf in enumerate(LEGACY_VENDOR_FILES):
        cont = ' \\' if idx < len(LEGACY_VENDOR_FILES) - 1 else ''
        amk_lines.append(f'    {lf}{cont}')

    amk_lines += [
        '',
        '$(foreach script,$(marie_legacy_vendor_files), \\',
        '    $(eval marie_legacy_src := $(LOCAL_PATH)/proprietary/vendor/$(script)) \\',
        '    $(eval marie_legacy_dst := $(TARGET_OUT_VENDOR)/$(script)) \\',
        '    $(eval $(call copy-one-file,$(marie_legacy_src),$(marie_legacy_dst))) \\',
        '    $(eval ALL_DEFAULT_INSTALLED_MODULES += $(marie_legacy_dst)))',
        '',
    ]
    amk_path.write_text('\n'.join(amk_lines), encoding='utf-8')
    print(
        f'Finalized vendor/huawei/marie: {len(set(suffixed_copy_files))} suffixed OEM copies, '
        f'{len(LEGACY_VENDOR_FILES)} legacy raw copies'
    )


if __name__ == '__main__':
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)
        saved = []
        for entry in PRESERVED_VENDOR_ENTRIES:
            src = VENDOR_DIR / entry
            if src.exists():
                shutil.move(str(src), str(tmp / entry))
                saved.append(entry)
        try:
            module = ExtractUtilsModule(
                'marie',
                'huawei',
                blob_fixups=blob_fixups,
                lib_fixups=vendor_lib_fixups,
            )
            ExtractUtils.device(module).run()
            finalize_vendor_tree()
        finally:
            for entry in saved:
                dst = VENDOR_DIR / entry
                if dst.exists():
                    if dst.is_dir():
                        shutil.rmtree(dst)
                    else:
                        dst.unlink()
                shutil.move(str(tmp / entry), str(dst))
