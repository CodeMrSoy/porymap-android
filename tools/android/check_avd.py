#!/usr/bin/env python3
"""Reject old avdmanager's android-0 metadata before it crashes Android 17."""
import os
from pathlib import Path
import sys

root = Path(os.environ.get("ANDROID_AVD_HOME", str(Path.home() / ".android/avd")))
text = (root / "test.ini").read_text()
print(text)
values = dict(line.split("=", 1) for line in text.splitlines() if "=" in line)
assert values.get("target") == f"android-{sys.argv[1]}", "AVD was created with an incorrect Android platform target"
