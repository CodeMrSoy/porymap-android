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
    adb("shell", "uiautomator", "dump", "/sdcard/porymap-picker.xml")
    raw = adb("exec-out", "cat", "/sdcard/porymap-picker.xml")
    (evidence / "picker.xml").write_bytes(raw)
    return list(ET.fromstring(raw).iter("node"))


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
        raise RuntimeError("Fixture ZIP is not visible in Android Downloads")
    # Wait for the source project to finish importing/loading, then paint via
    # the same Android input path as a finger, rather than a Qt mouse test.
    paint = None
    for _ in range(180):
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
    (evidence / "picker-result.json").write_text(json.dumps({
        "status": "passed", "checks": "Android touch opened SAF picker, selected project ZIP and tapped the paint canvas"
    }))
except Exception as error:
    screenshot("picker-failure.png")
    (evidence / "result.json").write_text(json.dumps({"status": "failed", "error": str(error)}))
    raise
