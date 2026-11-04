#!/usr/bin/env python3
"""Resolve Android 17's currently published SDK image name, including previews."""
import os
import re
import sys

packages = []
for line in sys.stdin:
    package = line.split("|", 1)[0].strip()
    parts = package.split(";")
    if len(parts) == 4 and parts[0] == "system-images" and parts[3] == "x86_64":
        packages.append(parts)

targets = ["google_apis", "google_apis_playstore", "google_apis_ps16k", "google_apis_playstore_16k"]
eligible = [p for p in packages if p[2] in targets and
            (re.fullmatch(r"android-37(?:[.\-][A-Za-z0-9.\-]+)?", p[1]) or "cinnamon" in p[1].lower())]
if not eligible:
    print("No published Android 17 x86_64 image found. Available image packages:")
    for p in packages:
        print(";".join(p))
    raise SystemExit(1)
selected = sorted(eligible, key=lambda p: (targets.index(p[2]), p[1]))[0]
api = selected[1].removeprefix("android-")
print("Selected Android 17 system image:", ";".join(selected))
with open(os.environ["GITHUB_OUTPUT"], "a") as output:
    output.write(f"api={api}\ntarget={selected[2]}\n")
