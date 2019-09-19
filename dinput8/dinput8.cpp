#include "pch.h"

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "detours.lib")		// from vcpkg

constexpr auto APP_NAME{ "DirtFix" };
constexpr auto MAX_ENUM_DEVICES_CALLS = 3;

decltype(&DirectInput8Create) g_pfnDirectInput8Create;
decltype(IDirectInput8::lpVtbl->EnumDevices) g_pfnEnumDevices;

std::mutex g_mutex;
std::map<DWORD, int> g_mEnumCalls;

///////////////////////////////////////////////////////////////////////////////

HRESULT __stdcall Hooked_EnumDevices(
	IDirectInput8* pThis,
	DWORD dwDevType,
	LPDIENUMDEVICESCALLBACK lpCallback,
	LPVOID pvRef,
	DWORD dwFlags)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (++g_mEnumCalls[GetCurrentThreadId()] > MAX_ENUM_DEVICES_CALLS)
	{
		// Fail the call, causing the game to skip any post-processing.
		return DIERR_GENERIC;
	}

	return g_pfnEnumDevices(pThis, dwDevType, lpCallback, pvRef, dwFlags);
}

///////////////////////////////////////////////////////////////////////////////

extern "C"
HRESULT WINAPI
DirectInput8Create(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter)
{
	HRESULT hr;

	if (!g_pfnDirectInput8Create)
	{
		char szSystem[MAX_PATH]{};
		GetSystemDirectory(szSystem, _countof(szSystem));	// System32 or SysWOW64

		fs::path dll_path(szSystem);
		dll_path /= "dinput8.dll";

		auto hmodDInput8 = LoadLibrary(dll_path.u8string().c_str());
		if (!hmodDInput8)
		{
			MessageBox(NULL, "Failed to bind to chained System32\\DInput8.dll", APP_NAME, MB_ICONSTOP);
			return E_FAIL;
		}

		g_pfnDirectInput8Create =
			(decltype(g_pfnDirectInput8Create))GetProcAddress(hmodDInput8, "DirectInput8Create");
	}

	if (!g_pfnDirectInput8Create)
		return E_FAIL;

	hr = g_pfnDirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);

	if (SUCCEEDED(hr) && (riidltf == IID_IDirectInput8A || (riidltf == IID_IDirectInput8W)))
	{
		auto pDI8 = reinterpret_cast<IDirectInput8*>(*ppvOut);
		g_pfnEnumDevices = pDI8->lpVtbl->EnumDevices;

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach(&reinterpret_cast<PVOID&>(g_pfnEnumDevices), Hooked_EnumDevices);
		DetourTransactionCommit();
	}

	return hr;
}

BOOL APIENTRY DllMain(
	_In_ HMODULE /*hinstDLL*/,
	_In_ DWORD  dwReason,
	_In_ LPVOID /*lpvReserved*/)
{
	if (DetourIsHelperProcess())
		return TRUE;

	if (dwReason == DLL_PROCESS_DETACH)
	{
		if (g_pfnEnumDevices)
		{
			DetourTransactionBegin();
			DetourUpdateThread(GetCurrentThread());
			DetourDetach(&reinterpret_cast<PVOID&>(g_pfnEnumDevices), Hooked_EnumDevices);
			DetourTransactionCommit();
		}
	}

	return TRUE;
}
