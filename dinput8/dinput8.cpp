#include "pch.h"

#pragma comment(lib, "detours.lib")		// from vcpkg

constexpr auto APP_NAME{ "DirtFix" };
constexpr auto MAX_ENUM_DEVICES_CALLS = 2;

decltype(&DirectInput8Create) g_pfnDirectInput8Create;
decltype(IDirectInput8::lpVtbl->EnumDevices) g_pfnEnumDevices;

std::mutex g_mutex;
std::map<DWORD, int> g_mEnumCalls;
HWND g_hwndNotify;

///////////////////////////////////////////////////////////////////////////////

HRESULT __stdcall Hooked_EnumDevices(
	IDirectInput8* pThis,
	DWORD dwDevType,
	LPDIENUMDEVICESCALLBACK lpCallback,
	LPVOID pvRef,
	DWORD dwFlags)
{
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (++g_mEnumCalls[GetCurrentThreadId()] > MAX_ENUM_DEVICES_CALLS)
		{
			// Fail the call, causing the game to skip any post-processing.
			return DIERR_GENERIC;
		}
	}

#ifdef _DEBUG
	// In debug, play the bell sound every time a call is passed through.
	MessageBeep(static_cast<UINT>(-1));
#endif

	return g_pfnEnumDevices(pThis, dwDevType, lpCallback, pvRef, dwFlags);
}

///////////////////////////////////////////////////////////////////////////////

LRESULT CALLBACK HidNotifySubclassProc(
	HWND hWnd,
	UINT uMsg,
	WPARAM wParam,
	LPARAM lParam,
	UINT_PTR /*uIdSubclass*/,
	DWORD_PTR /*dwRefData*/)
{
	auto p = reinterpret_cast<PDEV_BROADCAST_DEVICEINTERFACE>(lParam);

	if (uMsg == WM_DEVICECHANGE &&
		(wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE) &&
		p->dbcc_devicetype == DBT_DEVTYP_DEVICEINTERFACE &&
		p->dbcc_classguid == GUID_DEVINTERFACE_HID)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_mEnumCalls.clear();
	}

	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

extern "C"
HRESULT WINAPI
DirectInput8Create(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter)
{
	HRESULT hr;

	if (!g_pfnDirectInput8Create)
	{
#ifdef _DEBUG
		// In debug, show the path of the module we've been loaded into.
		char szEXE[MAX_PATH]{};
		GetModuleFileName(NULL, szEXE, sizeof(szEXE));
		MessageBox(NULL, szEXE, APP_NAME, MB_ICONINFORMATION);
#endif

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

	if (SUCCEEDED(hr) && (g_pfnEnumDevices == nullptr) &&
		(riidltf == IID_IDirectInput8A || (riidltf == IID_IDirectInput8W)))
	{
		auto pDI8 = reinterpret_cast<IDirectInput8*>(*ppvOut);
		g_pfnEnumDevices = pDI8->lpVtbl->EnumDevices;

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach(&reinterpret_cast<PVOID&>(g_pfnEnumDevices), Hooked_EnumDevices);
		DetourTransactionCommit();

		g_hwndNotify = CreateWindow("static", "", 0, 0, 0, 0, 0, NULL, NULL, GetModuleHandle(NULL), 0L);
		SetWindowSubclass(g_hwndNotify, HidNotifySubclassProc, 0, 0);

		DEV_BROADCAST_DEVICEINTERFACE dbdi{};
		dbdi.dbcc_size = sizeof(dbdi);
		dbdi.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
		dbdi.dbcc_classguid = GUID_DEVINTERFACE_HID;
		RegisterDeviceNotification(g_hwndNotify, &dbdi, DEVICE_NOTIFY_WINDOW_HANDLE);
	}

	return hr;
}

BOOL APIENTRY DllMain(
	_In_ HMODULE /*hinstDLL*/,
	_In_ DWORD  dwReason,
	_In_ LPVOID /*lpvReserved*/)
{
	if ((dwReason == DLL_PROCESS_DETACH) && (g_pfnEnumDevices != nullptr))
	{
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourDetach(&reinterpret_cast<PVOID&>(g_pfnEnumDevices), Hooked_EnumDevices);
		DetourTransactionCommit();
	}

	return TRUE;
}
