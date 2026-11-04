# Porymap Android prototype

The existing C++/Qt editor handles project parsing, tile rendering, painting,
collision/elevation, undo/redo and saving. Android-specific code adds a small
phone toolbar, full-width map/palette/list panels, Pan-mode touch gestures and
project archive import/export. Desktop builds retain their existing interface.

## Install and edit

1. Open the repository's **Actions → Android APK** workflow and its latest
   successful run. Download the **porymap-android-arm64-v8a** artifact, extract
   the ZIP, and install the contained `.apk` on the phone. Android may ask you
   to allow installation from the browser/file manager you chose.
2. On your PC, ZIP a complete Porymap-compatible game source project. Include
   `data`, `graphics`, `include`, `src`, `constants`, project config files and
   other source folders. A `.gba` ROM alone cannot be opened. GitHub's
   **Download ZIP** for the desired game branch is another option.
3. Copy the ZIP to your phone. In Porymap Android choose **Project → Import
   project ZIP**. If prompted, select the project's base game, e.g. `pokefirered`.
4. Tap **Maps**, then a map name. **Palette** opens/closes the full-width tile
   selector. Pick a tile and return to the canvas with **Palette**.
5. Use **Paint**, **Fill**, or **Pick** with one finger. Switch to **Pan** for
   dragging and two-finger pinch zoom. The zoom buttons are also available.
6. **Collision** switches to collision/elevation editing and opens its selector.
   Tap **Palette** to return to the canvas. Undo/redo use the native editor's
   history. Toolbar actions that do not fit are in its overflow menu.
7. **Save All** saves to the phone's imported copy. **Project → Export project
   ZIP** saves and exports that copy. Extract it on your PC and review its changes
   on the intended Git branch before using it in desktop Porymap or building a ROM.

Imports always create a new local project; importing the same ZIP again never
overwrites an earlier phone session. Use **Open imported project** to resume it.
The app saves pending project edits when Android backgrounds it. Export a ZIP
before uninstalling the app: app-private projects are removed by uninstall.
Development signing keys are cached in Actions to allow updates, but a cache
reset or moving builds to another branch can require reinstalling a test build.

Archives exclude `.git`, `build`, `.gradle`, `.cxx`, `__pycache__`, and
`node_modules` directory components. Other project bytes, including PNGs,
palettes, map binaries, JSON, scripts and config, are preserved. Import limits
are 100,000 entries, 128 MiB per file and 1 GiB total expanded data.

Corvala's maintained generator can overwrite manual map edits. Backport phone
edits into the authoring source before running that generator again.

## Build

The Android APK workflow uses Qt **6.10.2**, JDK **17**, Android SDK **36**,
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
needed: Android's system picker grants access to the selected ZIP, and the
native editor works on an app-private project copy.

## Validation and prototype limits

The workflow compiles and verifies both signed APKs. Host Java tests cover
binary/map/event round trips, edited-file preservation, path traversal,
extraction limits, cleanup of failed imports and refusing to overwrite existing
projects. The x86_64 emulator check opens an upstream `pret/pokefirered` fixture,
paints a tile through the canvas, exercises undo/redo and collision editing,
saves and reopens it, then exports/imports the project through the Android JNI
bridge and checks that the edited block survives. Results, screenshots and
device logs are uploaded as `android-emulator-evidence`.

Emulator tests do not prove finger/stylus accuracy on a Pixel. The Android file
picker and real-device gestures need a phone trial. Advanced desktop dialogs,
event editing and tileset/region-map editing retain their desktop layouts and
may need more phone-specific work. Git synchronization, ROM compilation and
an integrated emulator are outside this first APK. Game assets are supplied by
the user's imported project, not bundled in the APK.

The upstream LGPL license and third-party notices are retained in this fork.
