# Porymap

## Android prototype

This fork adds a native Qt/C++ Android build, targeting ARM64 phones such as the
Pixel 10 Pro. The original desktop editor remains available. Android setup,
project-folder access, ZIP transfer, touch controls and test status are documented in
[ANDROID.md](ANDROID.md).

APK builds are available as artifacts in this repository's
[Android APK workflow](https://github.com/CodeMrSoy/porymap-android/actions/workflows/android.yml).
Choose `porymap-android-arm64-v8a` for your phone. The x86_64 artifact is for the
emulator test. These are development builds, not a Play Store release.

[![Actions Status](https://github.com/huderlem/porymap/workflows/Build%20Porymap/badge.svg)](https://github.com/huderlem/porymap/actions)

A map editor for the Pokémon generation 3 decompilation projects ([pokeruby][pokeruby], [pokeemerald][pokeemerald], and [pokefirered][pokefirered]).

To get started, view the full online guide here: https://huderlem.github.io/porymap/

View the [Changelog][changelog] to see what's new.

## Download

Windows and macOS users can download Porymap below to start using it immediately. Older versions of Porymap may be downloaded from the [Releases][releases] page.

 - [Download Porymap for Windows](https://github.com/huderlem/porymap/releases/latest/download/porymap-windows.zip).
 - [Download Porymap for macOS latest (arm)](https://github.com/huderlem/porymap/releases/latest/download/porymap-macos-latest.zip).
 - [Download Porymap for macOS 15 (intel)](https://github.com/huderlem/porymap/releases/latest/download/porymap-macos-15-intel.zip).

Linux users must compile Porymap from source.

<details>
    <summary><i>Pre-compiled builds for Linux...</i></summary>

>   If you are a Linux user and you do not want to compile Porymap from source, you may find Porymap on an external package repository like Flathub or AUR.
>   Builds installed through an external package manager are not explicitly maintained by Porymap and may be out of date.
</details>

Read [INSTALL.md](INSTALL.md) for instructions on how to compile Porymap from source.

![Porymap Preview](docsrc/manual/images/introduction/porymap-loaded-project.png)

[pokeruby]: https://github.com/pret/pokeruby
[pokeemerald]: https://github.com/pret/pokeemerald
[pokefirered]: https://github.com/pret/pokefirered
[changelog]: https://github.com/huderlem/porymap/blob/master/CHANGELOG.md
[releases]: https://github.com/huderlem/porymap/releases
