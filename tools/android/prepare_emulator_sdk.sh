#!/usr/bin/env bash
set -euo pipefail
tools_root="${ANDROID_HOME:?}/cmdline-tools"
current="$tools_root/15859902"
test -x "$current/bin/avdmanager"
# emulator-runner prepends cmdline-tools/latest even when setup-android has
# installed a newer revision elsewhere. Retain the runner's old tools as backup.
if [ -d "$tools_root/latest" ] && [ ! -L "$tools_root/latest" ]; then
    mv "$tools_root/latest" "$tools_root/runner-original"
fi
ln -sfn "$current" "$tools_root/latest"
cat "$tools_root/latest/source.properties"
