# DirtFix (Dirt Rally Glitch Fixer) v1.0

## Introduction

Dirt Rally checks for game controller changes every 2 seconds, leading to
a spike in CPU activity that can drop video frames (particularly in VR).

DirtFix suspends this background polling to eliminate the glitches it causes.

Tested with Dirt Rally, but hopefully also works with Dirt Rally 2.0.

## Install

- Download and run the [installer for the latest version](https://github.com/simonowen/dirtfix/releases/latest).
- Optionally change the delay time (the default of zero is recommended).
- Click OK to activate.

DirtFix will continue running in the background, and start with Windows.
Launch Dirt Rally as normal, and DirtFix will activate automatically.

To change the delay time, re-launch DirtFix from the Start Menu shortcut.
To deactivate it, uninstall from "Add or Remove Programs".

## Upgrade

To upgrade an earlier version simply over-install with the latest version.

## Results

Before installing, the SteamVR profiler shows CPU activity spikes at ~2 second
intervals that often exceed the 11ms frame time budget for 90fps:

![Performance Before Installing](images/before.png)

After installing, the CPU spikes are absent, for smoother performance:

![Performance After Installing](images/after.png)

## Internals

DirtFix injects a DLL into drt.exe and dirtrally2.exe, hooking calls to the
Windows Sleep() function. This intercepts a 2 second sleep used by the input
controller and battery status polling threads. After a user-configured delay
has expired, the sleep duration is set to a very large value to prevent
further polling interfering with gameplay.

This process injection technique could upset some runtime virus scanners,
but no known issues are known at this time.

Source code is available from the [DirtFix project page](https://github.com/simonowen/dirtfix) on GitHub.

## Changelog

### v1.0
- first public test release

---

Simon Owen  
https://github.com/simonowen/dirtfix
