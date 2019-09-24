// DirtFix: https://github.com/simonowen/dirtfix
//
// Source code released under MIT License.

#include "pch.h"
#include <sstream>
#include "resource.h"

constexpr auto APP_NAME{ "DirtFix" };
constexpr auto APP_VER{ "v1.2" };
constexpr auto APP_URL{ "https://github.com/simonowen/dirtfix" };
constexpr auto SETTINGS_KEY{ R"(Software\SimonOwen\DirtFix)" };
constexpr auto STEAM_KEY{ R"(Software\Valve\Steam)" };

const std::vector<std::string> game_dirs
{
	"Dirt Rally",
	"Dirt Rally 2.0",
	"DiRT 4",
};

////////////////////////////////////////////////////////////////////////////////

bool IsDirtExe(fs::path path, bool &is_x64)
{
	DWORD dwHandle;
	auto dwSize = GetFileVersionInfoSize(path.u8string().c_str(), &dwHandle);

	std::vector<BYTE> data(dwSize);
	if (!GetFileVersionInfo(path.u8string().c_str(), NULL, dwSize, data.data()))
		return false;

    UINT ncbSz;
	struct LANGANDCODEPAGE { WORD wLanguage; WORD wCodePage; } *pLcp;
	if (!VerQueryValue(data.data(), "\\VarFileInfo\\Translation", (LPVOID *)&pLcp, &ncbSz))
		return false;

	std::stringstream ss;
	ss << "\\StringFileInfo\\" << std::hex << std::setfill('0');
	ss << std::setw(4) << pLcp->wLanguage;
	ss << std::setw(4) << pLcp->wCodePage;
	ss << "\\ProductName";

	char *pszProductName{};
	unsigned int cchProductName{};
	if (!VerQueryValue(data.data(), ss.str().c_str(), (LPVOID *)&pszProductName, &cchProductName))
		return false;

	DWORD dwBinaryType{};
	if (GetBinaryType(path.u8string().c_str(), &dwBinaryType))
		is_x64 = (dwBinaryType == SCS_64BIT_BINARY);

	auto strProduct = std::string(pszProductName, cchProductName);
	return strProduct.find("DiRT") != std::string::npos;
}

bool IsDirtDirectory(fs::path dir, bool &is_x64)
{
	if (!fs::is_directory(dir))
		return false;

	for (auto& p : fs::directory_iterator(dir))
	{
		if (p.path().extension() == ".exe" && IsDirtExe(p.path(), is_x64))
			return true;
	}

	return false;
}

bool IsShimInstalled(fs::path path)
{
	auto dll_path = path / "dinput8.dll";
	return fs::exists(dll_path) && fs::is_regular_file(dll_path);
}

bool UpdateShim(HWND hwndParent, fs::path path, bool install)
{
	char szEXE[MAX_PATH]{};
	GetModuleFileName(NULL, szEXE, _countof(szEXE));

	bool is_x64{};
	if (!IsDirtDirectory(path, is_x64))
		return false;

	auto dst_path = path / "dinput8.dll";

	if (install)
	{
		auto src_path = fs::path(szEXE).remove_filename();
		src_path /= (is_x64 ? "dinput8_64.dll" : "dinput8_32.dll");

		if (!CopyFileW(src_path.c_str(), dst_path.c_str(), FALSE))
		{
			auto dwError = GetLastError();
			std::stringstream ss;
			ss << "Failed to install shim DLL (error " << dwError << ")\n\n" << dst_path.string();
			MessageBox(hwndParent, ss.str().c_str(), APP_NAME, MB_ICONEXCLAMATION);
			return false;
		}
	}
	else if (fs::exists(dst_path) && !DeleteFileW(dst_path.c_str()))
	{
		auto dwError = GetLastError();
		if (dwError != ERROR_FILE_NOT_FOUND)
		{
			std::stringstream ss;
			ss << "Failed to remove shim DLL (error " << dwError << ")\n\n" << dst_path.string();
			MessageBox(hwndParent, ss.str().c_str(), APP_NAME, MB_ICONEXCLAMATION);
			return false;
		}
	}

	return true;
}

bool AddListEntry(HWND hDlg, fs::path path, bool enabled)
{
	HWND hListView = GetDlgItem(hDlg, IDL_DIRS);
	auto strPath = fs::canonical(fs::path(path)).u8string();

	LVFINDINFO lfi{};
	lfi.flags = LVFI_STRING;
	lfi.psz = strPath.c_str();
	auto index = ListView_FindItem(hListView, 0, &lfi);

	if (index >= 0)
		return false;

	LVITEM lvi{};
	lvi.mask = LVIF_TEXT;
	lvi.pszText = const_cast<char*>(lfi.psz);
	index = ListView_InsertItem(hListView, &lvi);

	ListView_SetCheckState(hListView, index, enabled);
	return index >= 0;
}

auto LoadRegistrySettings()
{
	HKEY hkey;
	bool is_x64{};
	std::map<std::string, bool> settings;

	// If Steam is installed, check if the supported games are installed.
	if (RegCreateKey(HKEY_CURRENT_USER, STEAM_KEY, &hkey) == ERROR_SUCCESS)
	{
		char szPath[MAX_PATH]{};
		DWORD cbPath = sizeof(szPath);
		DWORD dwType = REG_SZ;

		RegQueryValueEx(hkey, "SteamPath", NULL, &dwType, reinterpret_cast<LPBYTE>(szPath), &cbPath);
		RegCloseKey(hkey);

		if (szPath[0])
		{
			auto steam_path = fs::path(szPath) / "SteamApps/common";

			for (auto& subdir : game_dirs)
			{
				auto dir_path = fs::absolute(steam_path / subdir);
				if (IsDirtDirectory(dir_path, is_x64))
				{
					settings[fs::canonical(dir_path).u8string()] = IsShimInstalled(dir_path);
				}
			}
		}
	}

	// Pull previously saved paths from the registry, which includes manual entries.
	if (RegCreateKey(HKEY_CURRENT_USER, SETTINGS_KEY, &hkey) == ERROR_SUCCESS)
	{
		// Populate from settings stored in the registry.
		for (DWORD idx = 0; ; ++idx)
		{
			char szValue[256];
			DWORD dwType{ REG_DWORD };
			DWORD cchValue{ _countof(szValue) };
			DWORD dwData{ 0 }, cbData{ sizeof(dwData) };

			if (RegEnumValue(hkey, idx, szValue, &cchValue, NULL, &dwType,
					reinterpret_cast<BYTE*>(&dwData), &cbData) != ERROR_SUCCESS)
			{
				break;
			}

			auto dir_path = fs::path(szValue);
			if (IsDirtDirectory(dir_path, is_x64))
			{
				settings[fs::canonical(dir_path).u8string()] = IsShimInstalled(dir_path);
			}
			else
			{
				RegDeleteValue(hkey, szValue);
				idx--;
			}
		}

		RegCloseKey(hkey);
	}

	return settings;
}

void SaveRegistrySettings(
	_In_ HWND hDlg,
	_In_ const std::map<std::string, bool> &settings)
{
	HKEY hkey;
	if (RegCreateKey(HKEY_CURRENT_USER, SETTINGS_KEY, &hkey) == ERROR_SUCCESS)
	{
		for (auto &entry : settings)
		{
			auto [dir, enabled] = entry;
			UpdateShim(hDlg, dir, enabled);

			DWORD dwData = enabled ? 1 : 0;	// not used
			RegSetValueExA(hkey, dir.c_str(), 0, REG_DWORD, reinterpret_cast<LPBYTE>(&dwData), sizeof(dwData));
		}

		RegCloseKey(hkey);
	}

}

void AddSetting(HWND hDlg)
{
	BROWSEINFO bi{};
	bi.hwndOwner = hDlg;
	bi.lpszTitle = "Select DiRT Game Installation Directory:";
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI | BIF_NONEWFOLDERBUTTON;

	auto pidl = SHBrowseForFolder(&bi);
	if (pidl == nullptr)
		return;

	char szPath[MAX_PATH]{};
	if (!SHGetPathFromIDList(pidl, szPath))
		return;

	auto path = fs::path(szPath);
	CoTaskMemFree(pidl);

	bool is_x64{};
	if (!IsDirtDirectory(path, is_x64))
	{
		MessageBox(hDlg, "Selected location does not contain a DiRT game.", APP_NAME, MB_ICONEXCLAMATION);
		return;
	}

	AddListEntry(hDlg, path, IsShimInstalled(path));
}

fs::path ExePath(
	_In_ std::string strModule)
{
	char szEXE[MAX_PATH]{};
	GetModuleFileName(NULL, szEXE, _countof(szEXE));

	auto exe_path = fs::path(szEXE).remove_filename();
	return exe_path;
}

INT_PTR CALLBACK DialogProc(
	_In_ HWND hDlg,
	_In_ UINT uMsg,
	_In_ WPARAM wParam,
	_In_ LPARAM /*lParam*/)
{
	constexpr auto IDM_ABOUT = 0x1234;

	switch (uMsg)
	{
	case WM_INITDIALOG:
	{
		auto strCaption = std::string(APP_NAME) + " " + APP_VER;
		SetWindowText(hDlg, strCaption.c_str());

		HWND hListView = GetDlgItem(hDlg, IDL_DIRS);
		ListView_SetExtendedListViewStyle(hListView, LVS_EX_CHECKBOXES);

		LVCOLUMN lvc{};
		lvc.cx = 1000;
		lvc.mask = LVCF_WIDTH;
		ListView_InsertColumn(hListView, 0, &lvc);

		auto settings = LoadRegistrySettings();
		for (auto &entry : settings)
		{
			auto [dir, enabled] = entry;
			AddListEntry(hDlg, dir, enabled);
		}

		auto hmenu = GetSystemMenu(hDlg, FALSE);
		AppendMenu(hmenu, MF_SEPARATOR, 0, nullptr);
		AppendMenu(hmenu, MF_STRING, IDM_ABOUT, "About DashFix...");

		return TRUE;
	}

	case WM_COMMAND:
	{
		switch (LOWORD(wParam))
		{
		case IDC_ADD:
			AddSetting(hDlg);
			return TRUE;

		case IDOK:
		{
			auto hListView = GetDlgItem(hDlg, IDL_DIRS);
			std::map<std::string, bool> dir_status;

			auto num_items = ListView_GetItemCount(hListView);
			for (int i = 0; i < num_items; ++i)
			{
				char szItem[MAX_PATH]{};

				LV_ITEM li{};
				li.mask = LVIF_TEXT;
				li.iItem = i;
				li.pszText = szItem;
				li.cchTextMax = _countof(szItem);

				if (!SendMessage(hListView, LVM_GETITEM, 0, reinterpret_cast<LPARAM>(&li)))
					break;

				dir_status[szItem] = ListView_GetCheckState(hListView, i);
			}

			SaveRegistrySettings(hDlg, dir_status);
			DestroyWindow(hDlg);
			return TRUE;
		}

		case IDCANCEL:
			DestroyWindow(hDlg);
			return TRUE;
		}
		break;
	}
	break;

	case WM_SYSCOMMAND:
		if (wParam == IDM_ABOUT)
			ShellExecute(nullptr, "open", APP_URL, nullptr, nullptr, SW_SHOWNORMAL);

		break;
	}

	return FALSE;
}

////////////////////////////////////////////////////////////////////////////////

int CALLBACK WinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE /*hPrevInstance*/,
	_In_ LPSTR lpCmdLine,
	_In_ int /*nCmdShow*/)
{
	if (!lstrcmpi(lpCmdLine, "/uninstall"))
	{
		auto settings = LoadRegistrySettings();
		for (auto &entry : settings)
		{
			auto [dir, enabled] = entry;
			UpdateShim(NULL, dir, false);
		}

		return 0;
	}

	auto strCaption = std::string(APP_NAME) + " " + APP_VER;
	auto hwnd = FindWindow(nullptr, strCaption.c_str());
	if (hwnd)
	{
		SetForegroundWindow(hwnd);
	}
	else
	{
		if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
		{
			INITCOMMONCONTROLSEX icce{ sizeof(icce), ICC_LISTVIEW_CLASSES };
			InitCommonControlsEx(&icce);

			DialogBox(hInstance, MAKEINTRESOURCE(IDD_OPTIONS), NULL, DialogProc);

			CoUninitialize();
		}
	}

	return 0;
}
