#!/usr/bin/env bash
set -euo pipefail
package=org.codemrsoy.porymap
evidence=build/emulator-evidence
mkdir -p "$evidence"
adb install -r artifacts/porymap-android-x86_64-debug.apk
adb shell run-as "$package" mkdir -p files/projects
tar -C build/fixture -cf build/fixture.tar pokefirered
adb shell -T run-as "$package" tar -xf - -C files/projects < build/fixture.tar
printf 'projects/pokefirered' | adb shell -T run-as "$package" sh -c '"cat > files/android-smoke.request"'
adb logcat -c
adb shell am start -W -n "$package/org.qtproject.qt.android.bindings.QtActivity"
for _ in $(seq 1 180); do
    if adb exec-out run-as "$package" cat files/android-smoke.result.json > "$evidence/result.json" 2>/dev/null; then
        break
    fi
    sleep 2
done
adb logcat -d > "$evidence/logcat.txt"
adb exec-out screencap -p > "$evidence/device.png"
adb exec-out run-as "$package" cat files/android-smoke.png > "$evidence/editor.png" || true
test -s "$evidence/result.json"
python3 - "$evidence/result.json" <<'PY'
import json, sys
result = json.load(open(sys.argv[1]))
print(result)
assert result.get('status') == 'passed', result
PY
adb shell pidof "$package"
