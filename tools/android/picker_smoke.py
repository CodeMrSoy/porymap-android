#!/usr/bin/env python3
"""Exercise real Android touch -> Qt import button -> DocumentsUI ZIP selection."""
import json
from pathlib import Path
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

evidence = Path(sys.argv[1])
package = "org.codemrsoy.porymap"


def adb(*args, check=True):
    return subprocess.run(["adb", *args], check=check, capture_output=True).stdout


def screenshot(name):
    (evidence / name).write_bytes(adb("exec-out", "screencap", "-p"))


def snapshot():
    # DocumentsUI's first launch can still be laying out while the tap returns.
    # uiautomator sometimes exits zero without writing a dump in that state.
    for _ in range(12):
        message = adb("shell", "uiautomator", "dump", "--compressed",
                      "/data/local/tmp/porymap-picker.xml", check=False)
        raw = adb("exec-out", "cat", "/data/local/tmp/porymap-picker.xml", check=False)
        (evidence / "picker.xml").write_bytes(raw)
        try:
            return list(ET.fromstring(raw).iter("node"))
        except ET.ParseError:
            print("Waiting for picker hierarchy:", message.decode(errors="replace").strip(), flush=True)
            time.sleep(1)
    raise RuntimeError("Android document picker did not expose a complete UI hierarchy")


def tap(node):
    values = list(map(int, re.findall(r"\d+", node.attrib["bounds"])))
    x1, y1, x2, y2 = values
    adb("shell", "input", "tap", str((x1 + x2) // 2), str((y1 + y2) // 2))


def named(nodes, name):
    return next((n for n in nodes if n.get("text", "") == name
                 or n.get("content-desc", "") == name), None)


try:
    targets = None
    for _ in range(60):
        try:
            targets = json.loads(adb("exec-out", "run-as", package, "cat",
                                     "files/android-ui-targets.json", check=False))
            break
        except ValueError:
            time.sleep(1)
    if targets is None:
        raise RuntimeError("App did not expose an enabled Import ZIP button")
    screenshot("before-import.png")
    adb("shell", "input", "tap", str(targets["import_x"]), str(targets["import_y"]))
    nodes = []
    for _ in range(8):
        nodes = snapshot()
        if any("documentsui" in n.get("package", "") for n in nodes):
            break
        time.sleep(1)
    else:
        raise RuntimeError("Tapping Import ZIP did not open Android's document picker")
    screenshot("system-picker.png")
    selected = False
    for _ in range(8):
        entry = next((n for n in nodes if n.get("text", "").startswith("porymap-ui-fixture")), None)
        if entry is not None:
            tap(entry)
            selected = True
            break
        downloads = named(nodes, "Downloads")
        if downloads is None:
            downloads = named(nodes, "Download")
        roots = named(nodes, "Show roots")
        if downloads is not None:
            tap(downloads)
        elif roots is not None:
            tap(roots)
        else:
            raise RuntimeError("Cannot locate Downloads in Android document picker")
        time.sleep(1)
        nodes = snapshot()
    if not selected:
        raise RuntimeError("Fixture project folder is not visible in Android Downloads")
    # Opening a directory navigates into it; Android requires an explicit tree grant.
    for _ in range(8):
        nodes = snapshot()
        use = named(nodes, "USE THIS FOLDER")
        if use is None:
            use = named(nodes, "Use this folder")
        if use is not None:
            tap(use)
            time.sleep(1)
            nodes = snapshot()
            allow = named(nodes, "ALLOW")
            if allow is None:
                allow = named(nodes, "Allow")
            if allow is not None:
                tap(allow)
            break
        time.sleep(1)
    else:
        raise RuntimeError("Android did not offer Use this folder")
    # Wait for the source project to finish importing/loading, then paint via
    # the same Android input path as a finger, rather than a Qt mouse test.
    paint = None
    for _ in range(600):
        try:
            paint = json.loads(adb("exec-out", "run-as", package, "cat",
                                   "files/android-paint-target.json", check=False))
            break
        except ValueError:
            time.sleep(1)
    if paint is None:
        raise RuntimeError("Imported project did not reach the canvas touch check")
    adb("shell", "input", "tap", str(paint["x"]), str(paint["y"]))
    adb("shell", "run-as", package, "touch", "files/android-touch.done")
    for _ in range(180):
        request = adb("exec-out", "run-as", package, "ls", "files/android-rotate.request", check=False)
        if b"android-rotate.request" in request and b"No such" not in request:
            adb("shell", "settings", "put", "system", "accelerometer_rotation", "0")
            adb("shell", "settings", "put", "system", "user_rotation", "1")
            break
        result = adb("exec-out", "run-as", package, "cat", "files/android-smoke.result.json", check=False)
        if b'"failed"' in result:
            raise RuntimeError(result.decode(errors="replace"))
        time.sleep(1)
    else:
        raise RuntimeError("App did not reach the landscape layout check")
    (evidence / "picker-result.json").write_text(json.dumps({
        "status": "passed", "checks": "Android touch opened SAF tree picker, selected project folder and tapped the paint canvas"
    }))
except Exception as error:
    screenshot("picker-failure.png")
    (evidence / "result.json").write_text(json.dumps({"status": "failed", "error": str(error)}))
    raise
