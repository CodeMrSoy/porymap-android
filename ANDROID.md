# Porymap Android prototype

The existing C++/Qt editor handles project parsing, tile rendering, painting,
collision/elevation, undo/redo and saving. Android-specific code adds a small
phone toolbar, full-width map/palette/list panels, Pan-mode touch gestures and
project archive import/export. Desktop builds retain their existing interface.

Version **0.2.0** adds project-folder access and a phone workspace. The header
contains Open, Save and More; bottom navigation switches Map, Maps, Tiles and
Tools. Desktop builds keep the desktop interface.

## Install and edit

1. Download the `porymap-android-arm64-v8a` artifact from a successful Android
   APK run, extract it, and install the APK on an ARM64 phone.
2. Put the complete game source repository on the phone. Tap **Open**, navigate
   into its root (the folder containing `data`, `graphics`, `include`, etc.),
   and tap Android's **Use this folder**, then **Allow**. A ROM alone is not a
   source project. Android may prevent choosing the storage root or Downloads
   itself; choose the actual project subfolder.
3. Choose a map under **Maps**, select a tile under **Tiles**, then return to
   **Map**. **Tools** provides Paint, Pan/pinch, Fill, Pick tile, collision,
   undo/redo and zoom with large touch targets.
4. Tap **Save** to save the working copy and write its changes back to the
   selected folder. `.git` and build caches are excluded. Git operations remain
   the responsibility of your Git client.

Android grants a folder URI rather than an ordinary C++ filesystem path.
Porymap therefore keeps a private working mirror, plus hashes of the source
files it opened. Save writes only locally changed files (including additions
and deletions), checking those source files for conflicts before any writes.
Unrelated changes made outside Porymap are preserved. If the selected folder
has conflicting changes, Save reports the file and retains your local edits.
Use **More → Export ZIP** to recover those edits before opening a fresh copy.
Providers do not offer a transaction across an entire project; a disconnected
provider may cause a partial save. A saved per-file checkpoint permits retry.

**Open** loads a fresh copy from the selected folder. **More → Recent working
copies** resumes an existing on-device session, without pulling external edits.
Backgrounding saves to the private working copy only; explicitly tap **Save**
to update the selected folder. Uninstalling removes private copies, so save or
export first. ZIP import/export remain under **More** for project transfer.
ZIP imports are independent private projects and require Export ZIP to transfer
their changes back. Development signing keys are cached for APK updates.

Archives exclude `.git`, `build`, `.gradle`, `.cxx`, `__pycache__`, and
`node_modules` directory components. Other project bytes, including PNGs,
palettes, map binaries, JSON, scripts and config, are preserved. Import limits
are 100,000 entries, 128 MiB per file and 1 GiB total expanded data.

Corvala's maintained generator can overwrite manual map edits. Backport phone
edits into the authoring source before running that generator again.

## Build

The Android APK workflow uses Qt **6.10.2**, JDK **21**, Android SDK **36**,
NDK **27.2.12479018** and minimum Android **9 / API 28**. Separate ARM64 and
x86_64 APKs are produced. Qt's libraries and the application are checked for
16 KiB ELF load-segment alignment, suitable for modern Android memory pages.

From a Linux Qt-for-Android environment with `ANDROID_SDK_ROOT`,
`ANDROID_NDK_ROOT` and `JAVA_HOME` configured:

```sh
mkdir -p build/android
cd build/android
qmake ../../porymap.pro CONFIG+=release ANDROID_ABIS=arm64-v8a
make -j2
make install INSTALL_ROOT="$PWD/android-build"
"$QT_HOST_PATH/bin/androiddeployqt" --input android-porymap-deployment-settings.json \
  --output android-build --android-platform android-36 \
  --jdk "$JAVA_HOME"
```

Qt's normal Android Gradle template is used. The custom manifest and Java
archive bridge are in `android/`. No broad external-storage permission is
needed: Android's system picker grants access to the selected folder or ZIP, and the
native editor works on an app-private project copy.

## Validation and prototype limits

The workflow compiles and verifies both signed APKs. Host Java tests cover
binary/map/event round trips, edited-file preservation, path traversal,
extraction limits, cleanup of failed imports and refusing to overwrite existing
projects. The x86_64 emulator uses Android **17 / API 37** with system dark mode
enabled. It launches with no project, checks palette zoom, screen bounds and
open-button contrast/enabled state. Android input taps **Open**, navigates into
an upstream `pret/pokefirered` source folder and confirms **Use this folder**.
After loading, another Android input tap paints a tile on the canvas. The check
then exercises undo/redo and collision editing,
saves and reopens it, then exports/imports the project through the Android JNI
bridge and checks that the edited block survives. It also compares the map bytes
in the originally selected folder to the saved working copy, checks the three
workspace panels, rotates the device, and checks the Tools dialog in portrait
and landscape. Host tests cover folder conflicts, retries, unchanged files,
create/delete and Git-directory preservation. Results, screenshots and
device logs are uploaded as `android-emulator-evidence`.
Window bounds are checked again after loading and reimporting a project.

Version **0.1.3** APKs were produced in
[build 41910124945](https://github.com/CodeMrSoy/porymap-android/actions/runs/41910124945).
The complete Android 17 touch/picker/import/edit test passed in
[retest 41912009147](https://github.com/CodeMrSoy/porymap-android/actions/runs/41912009147).
Screenshots of the readable toolbar, system picker and loaded map were inspected.
Emulator tests run separately from compiler jobs with 4 GiB RAM/data storage;
the workflow pins current SDK tools and checks the Android 17 AVD target before boot.

Emulator tests do not prove finger/stylus accuracy on a physical Pixel.
Real-device multitouch gestures still need a phone trial. Advanced desktop dialogs,
event editing and tileset/region-map editing retain their desktop layouts and
may need more phone-specific work. Git synchronization, ROM compilation and
an integrated emulator are outside this first APK. Game assets are supplied by
the user's imported project, not bundled in the APK.

The upstream LGPL license and third-party notices are retained in this fork.

