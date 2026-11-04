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
for attempt in $(seq 1 180); do
    # exec-out may return success even when the device-side cat has no file yet.
    # Wait for a complete JSON result, rather than treating transport success
    # or a diagnostic string as completion of the editor test.
    adb exec-out run-as "$package" cat files/android-smoke.result.json > "$evidence/result.json" 2>/dev/null || true
    if python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "$evidence/result.json" >/dev/null 2>&1; then
        break
    fi
    if [ "$attempt" -gt 15 ] && ! adb shell pidof "$package" >/dev/null 2>&1; then
        printf '{"status":"failed","error":"App process exited before completing the editor check"}' > "$evidence/result.json"
        break
    fi
    sleep 2
done
adb logcat -d > "$evidence/logcat.txt"
adb exec-out run-as "$package" cat files/porymap.log > "$evidence/porymap.log" || true
adb exec-out screencap -p > "$evidence/device.png"
adb exec-out run-as "$package" cat files/android-smoke.png > "$evidence/editor.png" || true
adb exec-out run-as "$package" cat files/android-startup.png > "$evidence/startup.png" || true
test -s "$evidence/result.json"
python3 - "$evidence/result.json" <<'PY'
import json, sys
result = json.load(open(sys.argv[1]))
print(result)
assert result.get('status') == 'passed', result
PY
adb shell pidof "$package"
