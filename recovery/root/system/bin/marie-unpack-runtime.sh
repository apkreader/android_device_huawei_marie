#!/system/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
set -o pipefail

failed() {
    echo '<4>marie-runtime: unpack failed' >/dev/kmsg || :
    # exec_start must keep init paused if the runtime is incomplete.
    while :; do /system/bin/sleep 60 || :; done
}
trap failed EXIT
cd /
echo '<4>marie-runtime: unpack starting' >/dev/kmsg
/system/bin/7za x -so /runtime.tar.xz | /system/bin/7za x -si -ttar -y -o/ >/dev/marie-runtime-unpack.log
/system/bin/sha256sum -c /runtime.sha256 >/dev/marie-runtime-hashes.log
/system/bin/restorecon -R /system/lib64 /system/bin /crypto
echo '<4>marie-runtime: unpack verified' >/dev/kmsg
trap - EXIT
