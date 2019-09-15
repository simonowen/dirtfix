// DirtFix: https://github.com/simonowen/dirtfix
//
// Source code released under MIT License.

#include <WinSDKVer.h>
#define _WIN32_WINNT _WIN32_WINNT_WIN7
#include <SDKDDKVer.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>  
#include <commctrl.h>
#include <string>
#include <vector>
#include <map>
#include <thread>

#include "resource.h"


const auto APP_NAME{ TEXT("DirtFix") };
const auto SETTINGS_KEY{ TEXT(R"(Software\SimonOwen\DirtFix)") };

HWND g_hDlg{ NULL };

////////////////////////////////////////////////////////////////////////////////

_Success_(return)
bool GetProcessId(
	_In_ const std::wstring &strProcess,
	_Out_ DWORD & dwProcessId,
	_In_opt_ DWORD dwExcludeProcessId = 0)
{
	bool ret{ false };
	dwProcessId = 0;

	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnapshot != INVALID_HANDLE_VALUE)
	{
		PROCESSENTRY32 pe{ sizeof(pe) };

		auto f = Process32First(hSnapshot, &pe);
		while (f)
		{
			if (!lstrcmpi(pe.szExeFile, strProcess.c_str()) &&
				pe.th32ProcessID != dwExcludeProcessId)
			{
				dwProcessId = pe.th32ProcessID;
				ret = true;
				break;
			}

			f = Process32Next(hSnapshot, &pe);
		}

		CloseHandle(hSnapshot);
	}

	return ret;
}

_Success_(return)
bool InjectRemoteThread(
	_In_ DWORD dwProcessId,
	_In_ const std::wstring &strDLL)
{
	bool ret{ false };

	auto hProcess = OpenProcess(
		PROCESS_ALL_ACCESS,
		FALSE,
		dwProcessId);

	if (hProcess != NULL)
	{
		auto pfnLoadLibrary = GetProcAddress(
			GetModuleHandle(TEXT("kernel32.dll")),
			"LoadLibraryW");

		auto uStringSize = (strDLL.length() + 1) * sizeof(strDLL[0]);
		auto pszRemoteString = VirtualAllocEx(
			hProcess,
			NULL,
			uStringSize,
			MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

		if (pszRemoteString)
		{
			WriteProcessMemory(hProcess, pszRemoteString, strDLL.c_str(), uStringSize, NULL);

			auto hThread = CreateRemoteThread(
				hProcess, NULL, 0,
				reinterpret_cast<LPTHREAD_START_ROUTINE>(pfnLoadLibrary),
				pszRemoteString,
				0,
				NULL);

			if (hThread != NULL)
			{
				WaitForSingleObject(hThread, INFINITE);
				CloseHandle(hThread);
				ret = true;
			}

			VirtualFreeEx(hProcess, pszRemoteString, 0, MEM_RELEASE);
		}

		WaitForSingleObject(hProcess, INFINITE);
		CloseHandle(hProcess);
	}

	return ret;
}


void InjectModules(
	_In_ const std::vector<std::wstring> &mods)
{
	WCHAR szEXE[MAX_PATH];
	GetModuleFileName(NULL, szEXE, _countof(szEXE));

	// The injection DLL is in the same directory as the EXE.
	std::wstring strDLL{ szEXE };
	strDLL = strDLL.substr(0, strDLL.rfind('\\'));
	strDLL += TEXT("\\inject.dll");

	std::vector<std::thread> threads{};
	for (auto &mod : mods)
	{
		std::thread([=]
		{
			// Loop forever watching
			for (;;)
			{
				DWORD dwProcessId;

				// Poll for process start, with 3 seconds between checks.
				if (!GetProcessId(mod, dwProcessId))
				{
					Sleep(3000);
				}
				// Inject the patch DLL into the process and wait for it to
				// terminate. 1 second between injection attempts.
				else if (!InjectRemoteThread(dwProcessId, strDLL))
				{
					Sleep(1000);
				}
			}
		}).detach();
	}
}

void LoadSettings(
	_In_ HWND hDlg)
{
	HKEY hkey;
	if (RegCreateKey(
		HKEY_CURRENT_USER,
		SETTINGS_KEY,
		&hkey) == ERROR_SUCCESS)
	{
		DWORD dwValue = 0;
		DWORD cbValue = sizeof(dwValue);
		DWORD dwType = REG_DWORD;
		RegQueryValueEx(hkey, L"Delay", nullptr, &dwType, reinterpret_cast<LPBYTE>(&dwValue), &cbValue);
		if (dwType == REG_DWORD)
			SetDlgItemInt(hDlg, IDC_DELAY, dwValue, FALSE);

		RegCloseKey(hkey);
	}
}

void SaveSettings(
	_In_ HWND hDlg)
{
	HKEY hkey;
	if (RegCreateKey(
		HKEY_CURRENT_USER,
		SETTINGS_KEY,
		&hkey) == ERROR_SUCCESS)
	{
		DWORD dwValue = GetDlgItemInt(hDlg, IDC_DELAY, NULL, FALSE);
		RegSetValueEx(hkey, L"Delay", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&dwValue), sizeof(dwValue));

		RegCloseKey(hkey);
	}
}

INT_PTR CALLBACK DialogProc(
	_In_ HWND hDlg,
	_In_ UINT uMsg,
	_In_ WPARAM wParam,
	_In_ LPARAM /*lParam*/)
{
	switch (uMsg)
	{
		case WM_INITDIALOG:
			LoadSettings(hDlg);
			return TRUE;

		case WM_DESTROY:
			g_hDlg = NULL;
			return TRUE;

		case WM_COMMAND:
		{
			switch (LOWORD(wParam))
			{
				case IDOK:
					SaveSettings(hDlg);
					DestroyWindow(hDlg);
					return TRUE;

				case IDCANCEL:
					DestroyWindow(hDlg);
					return TRUE;
			}
			break;
		}
	}

	return FALSE;
}

void ShowGUI(
	_In_ HINSTANCE hInstance,
	_In_ int nCmdShow)
{
	INITCOMMONCONTROLSEX icce{ sizeof(icce), ICC_LISTVIEW_CLASSES };
	InitCommonControlsEx(&icce);

	g_hDlg = CreateDialog(
		hInstance, MAKEINTRESOURCE(IDD_OPTIONS), NULL, DialogProc);

	ShowWindow(g_hDlg, nCmdShow);
}

////////////////////////////////////////////////////////////////////////////////

int CALLBACK WinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE /*hPrevInstance*/,
	_In_ LPSTR lpCmdLine,
	_In_ int nCmdShow)
{
	auto uRegMsg = RegisterWindowMessage(APP_NAME);

	// Check for a running instance.
	HANDLE hMutex = CreateMutex(NULL, TRUE, APP_NAME);
	if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS)
	{
		ReleaseMutex(hMutex);

		// Ask the existing instance to show the GUI.
		DWORD dwRecipients = BSM_APPLICATIONS;
		BroadcastSystemMessage(
			BSF_POSTMESSAGE,
			&dwRecipients,
			uRegMsg,
			0,
			0L);
	}
	else
	{
		// Inject the hook DLL into the known affected applications.
		InjectModules({ TEXT("drt.exe"), TEXT("dirtrally2.exe") });

		// Show the GUI if not launched with a parameter (startup mode).
		if (!lpCmdLine[0])
			ShowGUI(hInstance, nCmdShow);

		// Dummy window to listen for broadcast messages.
		CreateWindow(TEXT("static"), TEXT(""), 0, 0, 0, 0, 0, NULL, NULL, hInstance, 0L);

		MSG msg;
		while (GetMessage(&msg, NULL, 0, 0))
		{
			// Show the GUI if asked to by another instance, and not already active.
			if (msg.message == uRegMsg && !g_hDlg)
				ShowGUI(hInstance, SW_SHOWNORMAL);

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	return 0;
}
