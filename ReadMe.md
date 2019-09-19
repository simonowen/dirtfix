# DirtFix (DiRT Input Glitch Fixer) v1.1

## Introduction

Recent Codemasters "DiRT" series games check for game controller changes every
2 seconds, which can cause a spike in CPU activity, leading to gameplay glitches.
DirtFix suspends this background polling to eliminate the glitches it causes.

Tested with DiRT Rally, DiRT Rally 2.0, and DiRT 4.

## Install

- Download and run the [installer for the latest version](https://github.com/simonowen/dirtfix/releases/latest).
- Select the games to fix (or Add any DiRT game locations that are missing).
- Click OK to apply the settings.
- Launch the DiRT game as normal.

To change settings for a game, re-launch DirtFix from the Start Menu shortcut.
To completely deactivate, uninstall from "Add or Remove Programs".

## Upgrade

To upgrade an earlier version simply over-install with the latest version.

## Results

Before installing, the SteamVR profiler shows CPU activity spikes at ~2 second
intervals that often exceed the 11ms frame time budget for 90fps in VR:

![Performance Before Installing](images/before.png)

After installing, the CPU spikes are absent, for smoother performance:

![Performance After Installing](images/after.png)

## Internals

DirtFix uses a shim version of a DirectInput module (dinput8.dll), which is
copied into the game directory. This allows allows it to sit between the game
and DirectInput API, and change its behaviour.

DirtFix passes through the first 3 calls to IDirectInput8::EnumDevices on each
thread, before failing the call. The failure code causes the game to skip any
post-processing of the results, so no further controller changes are seen. The
normal CPU-intensive functionality is skipped, which prevents the glitches.

Source code is available from the [DirtFix project page](https://github.com/simonowen/dirtfix) on GitHub.
Includes VS2019 solution, but requires detours.lib from vcpkg.

## Changelog

### v1.1
- added support for DiRT Rally 2.0 and DiRT 4.
- complete rewritten to avoid using thread injection.

### v1.0
- first public test release.

---

Simon Owen  
https://github.com/simonowen/dirtfix
