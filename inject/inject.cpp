// DirtFix: https://github.com/simonowen/dirtfix
//
// Source code released under MIT License.

#include <WinSDKVer.h>
#define _WIN32_WINNT _WIN32_WINNT_WIN7
#include <SDKDDKVer.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <detours.h>

#include <string>
#include <vector>
#include <map>
#include <thread>
#include <mutex>

constexpr auto APP_NAME{ TEXT("DirtFix") };
constexpr auto SETTINGS_KEY{ TEXT(R"(Software\SimonOwen\DirtFix)") };

using PFNSLEEP = decltype(&Sleep);
PFNSLEEP g_pfnSleep = Sleep;

HKEY g_hkey;
HANDLE g_hEvent;

ULONGLONG g_ullStartTime;
DWORD g_dwDelayMilliseconds;

// Thread to watch for registry changes and flush the settings cache.
static void RegistryWatchThread()
{
	for (;;)
	{
		auto status{WaitForSingleObject(g_hEvent, INFINITE)};
		if (status != WAIT_OBJECT_0)
			break;

		DWORD dwValue = 0;
		DWORD cbValue = sizeof(dwValue);
		DWORD dwType = REG_DWORD;

		// Read any updated delay setting.
		status = RegQueryValueEx(
			g_hkey,
			L"Delay",
			nullptr,
			&dwType,
			reinterpret_cast<LPBYTE>(&dwValue),
			&cbValue);

		if (status == ERROR_SUCCESS && dwType == REG_DWORD)
			g_dwDelayMilliseconds = dwValue * 1000;

		// Start a new watch for registry changes.
		RegNotifyChangeKeyValue(
			g_hkey,
			FALSE,
			REG_NOTIFY_CHANGE_LAST_SET,
			g_hEvent,
			TRUE);
	}
}

void WINAPI Hooked_Sleep (DWORD dwMilliseconds)
{
	static std::once_flag once;

	// Create the registry watching thread on the first call. We can't do this
	// during DllMain as it depends on the loader lock that is already held.
	std::call_once(once, [] {
		std::thread(RegistryWatchThread).detach();
	});

	// 2 second delay, as found in the input polling thread?
	if (dwMilliseconds == 2000)
	{
		// If we're outside the delay, make the delay as long as possible.
		if ((GetTickCount64() - g_ullStartTime) >= g_dwDelayMilliseconds)
			dwMilliseconds = MAXDWORD32;
	}

	g_pfnSleep(dwMilliseconds);
}

BOOL APIENTRY DllMain(
	_In_ HMODULE /*hinstDLL*/,
	_In_ DWORD  dwReason,
	_In_ LPVOID /*lpvReserved*/)
{
	if (DetourIsHelperProcess())
		return TRUE;

	if (dwReason == DLL_PROCESS_ATTACH)
	{
#ifdef _DEBUG
		// Convenient delay point to allow us to attach the debugger.
		MessageBox(NULL, TEXT("In DllMain for inject.dll"), APP_NAME, MB_OK | MB_ICONINFORMATION);
#endif
		g_ullStartTime = GetTickCount64();

		// Event used for registry change monitoring, signalled by default.
		g_hEvent = CreateEvent(nullptr, FALSE, TRUE, nullptr);

		// Registry containing settings, written to by the GUI.
		RegCreateKey(HKEY_CURRENT_USER, SETTINGS_KEY, &g_hkey);

		DetourRestoreAfterWith();

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach(&reinterpret_cast<PVOID&>(g_pfnSleep), Hooked_Sleep);
		DetourTransactionCommit();
	}
	else if (dwReason == DLL_PROCESS_DETACH)
	{
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourDetach(&reinterpret_cast<PVOID&>(g_pfnSleep), Hooked_Sleep);
		DetourTransactionCommit();
	}

	return TRUE;
}
