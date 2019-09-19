// pch.h: This is a precompiled header file for dinput8.dll.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.

#pragma once

#include <WinSDKVer.h>
#define _WIN32_WINNT _WIN32_WINNT_WIN7
#include <SDKDDKVer.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "detours.h"

#include <mutex>
#include <map>
#include <filesystem>
namespace fs = std::filesystem;

#define INIT_GUID
#include <initguid.h>

#define DIRECTINPUT_VERSION		0x0800
#define CINTERFACE	// for vtable access
#include <dinput.h>
#undef CINTERFACE
