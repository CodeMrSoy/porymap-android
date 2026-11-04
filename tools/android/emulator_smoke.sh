#!/usr/bin/env bash
set -euo pipefail
package=org.codemrsoy.porymap
evidence=build/emulator-evidence
mkdir -p "$evidence"
adb shell getprop ro.build.version.release > "$evidence/android-version.txt"
adb shell getprop ro.build.version.sdk >> "$evidence/android-version.txt"
cat "$evidence/android-version.txt"
adb install -r artifacts/porymap-android-x86_64-debug.apk
adb shell run-as "$package" mkdir -p files
python3 -m zipfile -c build/porymap-ui-fixture.zip build/fixture/pokefirered
adb shell mkdir -p /sdcard/Download
adb push build/porymap-ui-fixture.zip /sdcard/Download/porymap-ui-fixture.zip
adb shell am broadcast -a android.intent.action.MEDIA_SCANNER_SCAN_FILE -d file:///sdcard/Download/porymap-ui-fixture.zip
printf 'picker' | adb shell -T run-as "$package" sh -c '"cat > files/android-smoke.request"'
# Reproduce the system-dark-mode palette from the reported Pixel screenshot.
adb shell cmd uimode night yes
adb logcat -c
adb shell am start -W -n "$package/org.qtproject.qt.android.bindings.QtActivity"
picker_status=0
python3 tools/android/picker_smoke.py "$evidence" || picker_status=$?
for attempt in $(seq 1 180); do
    if [ "$picker_status" -ne 0 ]; then break; fi
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
