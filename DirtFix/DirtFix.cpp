// DirtFix: https://github.com/simonowen/dirtfix
//
// Source code released under MIT License.

#include "pch.h"
#include "resource.h"

constexpr auto APP_NAME{ "DirtFix" };
constexpr auto APP_VER{ "v1.5" };
constexpr auto APP_URL{ "https://github.com/simonowen/dirtfix" };
constexpr auto SETTINGS_KEY{ R"(Software\SimonOwen\DirtFix)" };
constexpr auto STEAM_KEY{ R"(Software\Valve\Steam)" };
constexpr auto OCULUS_KEY{ R"(Software\Oculus VR, LLC\Oculus\Libraries)" };

const std::vector<std::string> game_dirs
{
	"DiRT Rally",									// Steam
	"DiRT Rally 2.0",								// Steam and Microsoft
	"DiRT 4",										// Steam
	"codemasters-dirt-rally/World/application",		// Oculus
	"codemasters-dirt-rally-2-0",					// Oculus
	"GRID Autosport",								// Steam
};

struct FILE_CHANGES
{
	std::vector<std::pair<std::string, std::string>> copies;
	std::vector<std::string> deletes;
};

////////////////////////////////////////////////////////////////////////////////

// Helper class to disable WOW file redirection in the scope of the object,
// allowing us to selectively access 64-bit only paths from our 32-bit process.
class DisableFsRedirection
{
public:
	DisableFsRedirection() { Wow64DisableWow64FsRedirection(&m_last_redir); }
	~DisableFsRedirection() { Wow64RevertWow64FsRedirection(m_last_redir); }
private:
	PVOID m_last_redir{};
};

////////////////////////////////////////////////////////////////////////////////

bool MatchingFiles(const fs::path& src_path, const fs::path& dst_path)
{
	try
	{
		if (fs::exists(src_path) && fs::exists(dst_path) &&
			fs::file_size(src_path) == fs::file_size(dst_path))
		{
			std::ifstream src_file(src_path.string(), std::ifstream::in | std::ifstream::binary);
			std::ifstream dst_file(dst_path.string(), std::ifstream::in | std::ifstream::binary);
			if (src_file.is_open() && dst_file.is_open())
			{
				std::vector<char> sbuf(static_cast<size_t>(fs::file_size(src_path)));
				std::vector<char> dbuf(static_cast<size_t>(fs::file_size(dst_path)));

				src_file.read(sbuf.data(), sbuf.size());
				dst_file.read(dbuf.data(), dbuf.size());

				return sbuf == dbuf;
			}
		}
	}
	catch (...) {}

	return false;
}

bool IsX64Binary(const fs::path &path)
{
	// GetBinaryType appears to fail when the path contains unreadable directories,
	// even when a full path is given. Below does all we need directly.
	try
	{
		std::ifstream f(path.string(), std::ifstream::in | std::ifstream::binary);

		IMAGE_DOS_HEADER dos_header{};
		f.read(reinterpret_cast<char*>(&dos_header), sizeof(dos_header));

		if (dos_header.e_magic != IMAGE_DOS_SIGNATURE)
			return false;

		f.seekg(dos_header.e_lfanew);

		IMAGE_NT_HEADERS nt_headers{};
		f.read(reinterpret_cast<char*>(&nt_headers), sizeof(nt_headers));

		return nt_headers.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64;
	}
	catch (...) {}

	return false;
}

bool IsGameExe(const fs::path &path)
{
	DWORD dwHandle;
	auto path_str = path.string();
	auto dwSize = GetFileVersionInfoSize(path_str.c_str(), &dwHandle);

	std::vector<BYTE> data(dwSize);
	if (!GetFileVersionInfo(path_str.c_str(), NULL, dwSize, data.data()))
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

	auto strProduct = std::string(pszProductName, cchProductName);
	return strProduct.find("DiRT") != std::string::npos ||
		   strProduct.find("GRID") != std::string::npos;
}

bool IsGameDirectory(const fs::path &dir, bool &is_x64)
{
	if (!fs::is_directory(dir))
		return false;

	for (auto& p : fs::directory_iterator(dir))
	{
		if (p.path().extension() == ".exe" && IsGameExe(p.path()))
		{
			is_x64 = IsX64Binary(p.path());
			return true;
		}
	}

	return false;
}

bool IsShimInstalled(const fs::path &path)
{
	auto dll_path = path / "dinput8.dll";
	return fs::exists(dll_path) && fs::is_regular_file(dll_path);
}

bool GetShimFileChanges(fs::path path, bool install, FILE_CHANGES &file_changes)
{
	DisableFsRedirection fs_disable;

	char szEXE[MAX_PATH]{};
	GetModuleFileName(NULL, szEXE, _countof(szEXE));

	bool is_x64{};
	if (!IsGameDirectory(path, is_x64))
		return false;

	auto dst_path = path / "dinput8.dll";

	if (install)
	{
		auto src_path = fs::path(szEXE).remove_filename();
		src_path /= (is_x64 ? "dinput8_64.dll" : "dinput8_32.dll");

		if (!MatchingFiles(src_path, dst_path))
		{
			file_changes.copies.emplace_back(std::make_pair(src_path.string(), dst_path.string()));
		}
	}
	else if (fs::exists(dst_path))
	{
		file_changes.deletes.emplace_back(dst_path.string());
	}

	return true;
}

bool ApplyFileChanges(HWND hwndParent, const FILE_CHANGES &file_changes)
{
	auto& [file_copies, file_deletes] = file_changes;

	if (!file_copies.empty())
	{
		std::string src_files;
		std::string dst_files;

		for (auto [src_path, dst_path] : file_copies)
		{
			src_files += src_path + '\0';
			dst_files += dst_path + '\0';
		}

		src_files += '\0';
		dst_files += '\0';

		SHFILEOPSTRUCT fo{};
		fo.hwnd = hwndParent;
		fo.wFunc = FO_COPY;
		fo.fFlags = FOF_MULTIDESTFILES | FOF_FILESONLY | FOF_NOCONFIRMATION;
		fo.pFrom = src_files.c_str();
		fo.pTo = dst_files.c_str();

		if (SHFileOperation(&fo) || fo.fAnyOperationsAborted)
			return false;
	}

	if (!file_deletes.empty())
	{
		std::string files;

		for (auto& file : file_deletes)
			files += file + '\0';

		files += '\0';

		SHFILEOPSTRUCT fo{};
		fo.hwnd = hwndParent;
		fo.wFunc = FO_DELETE;
		fo.fFlags = FOF_FILESONLY | FOF_NOCONFIRMATION;
		fo.pFrom = files.c_str();

		if (SHFileOperation(&fo) || fo.fAnyOperationsAborted)
			return false;
	}

	return true;
}

bool AddListEntry(HWND hDlg, fs::path path, bool enabled)
{
	HWND hListView = GetDlgItem(hDlg, IDL_DIRS);
	auto strPath = fs::canonical(fs::path(path)).string();

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
	if (RegOpenKeyEx(HKEY_CURRENT_USER, STEAM_KEY, 0, KEY_QUERY_VALUE, &hkey) == ERROR_SUCCESS)
	{
		char szPath[MAX_PATH]{};
		DWORD cbPath = sizeof(szPath);
		DWORD dwType = REG_SZ;

		RegQueryValueEx(hkey, "SteamPath", NULL, &dwType, reinterpret_cast<LPBYTE>(szPath), &cbPath);
		RegCloseKey(hkey);

		auto steam_path = fs::path(szPath) / "SteamApps/common";

		for (auto& subdir : game_dirs)
		{
			auto dir_path = fs::absolute(steam_path / subdir);
			if (IsGameDirectory(dir_path, is_x64))
			{
				settings[fs::canonical(dir_path).string()] = IsShimInstalled(dir_path);
			}
		}
	}

	// Check for supported games in Microsoft Store storage locations.
	char szPF[MAX_PATH]{};
	if (ExpandEnvironmentStrings("%ProgramW6432%", szPF, _countof(szPF)) ||
		ExpandEnvironmentStrings("%ProgramFiles%", szPF, _countof(szPF)))
	{
		DisableFsRedirection fs_disable;

		// Until we know how to find the storage locations, scan all fixed drives.
		std::vector<char> drives(GetLogicalDriveStrings(0, nullptr));
		GetLogicalDriveStrings(static_cast<DWORD>(drives.size()), drives.data());

		for (auto pszRoot = drives.data(); *pszRoot; pszRoot += strlen(pszRoot) + 1)
		{
			if (GetDriveType(pszRoot) != DRIVE_FIXED)
				continue;

			szPF[0] = pszRoot[0];
			auto msstore_path = fs::path(szPF) / "ModifiableWindowsApps";

			if (fs::exists(msstore_path))
			{
				for (auto& subdir : game_dirs)
				{
					auto dir_path = fs::absolute(msstore_path / subdir);
					if (IsGameDirectory(dir_path, is_x64))
					{
						settings[fs::canonical(dir_path).string()] = IsShimInstalled(dir_path);
					}
				}
			}
		}
	}

	// If the Oculus software is installed, check for supported games there.
	DWORD dwOptions = KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS;
	if (RegOpenKeyEx(HKEY_CURRENT_USER, OCULUS_KEY, 0, dwOptions, &hkey) == ERROR_SUCCESS)
	{
		DisableFsRedirection fs_disable;
		DWORD dwIndex = 0;

		for (;;)
		{
			char szSubKey[MAX_PATH]{};
			DWORD cbSubKey = sizeof(szSubKey);

			if (RegEnumKeyEx(hkey, dwIndex++, szSubKey, &cbSubKey, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
				break;

			HKEY hkeyLib{};
			if (RegOpenKeyEx(hkey, szSubKey, 0, KEY_QUERY_VALUE, &hkeyLib) == ERROR_SUCCESS)
			{
				char szPath[MAX_PATH]{};
				DWORD cbPath = sizeof(szPath);

				RegQueryValueEx(hkeyLib, "Path", NULL, NULL, reinterpret_cast<LPBYTE>(szPath), &cbPath);
				RegCloseKey(hkeyLib);

				DWORD cchRet{};
				char szVolume[MAX_PATH]{};
				auto vol_str = std::string(szPath, 49);
				GetVolumePathNamesForVolumeName(vol_str.c_str(), szVolume, _countof(szVolume), &cchRet);

				auto oculus_path = fs::path(std::string(szVolume)) / std::string(szPath + 49);
				oculus_path /= "Software";

				for (auto& subdir : game_dirs)
				{
					auto dir_path = fs::absolute(oculus_path / subdir);
					if (IsGameDirectory(dir_path, is_x64))
					{
						settings[fs::canonical(dir_path).string()] = IsShimInstalled(dir_path);
					}
				}
			}
		}

		RegCloseKey(hkey);
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
			if (IsGameDirectory(dir_path, is_x64))
			{
				settings[fs::canonical(dir_path).string()] = IsShimInstalled(dir_path);
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
		FILE_CHANGES file_changes;

		for (auto &entry : settings)
		{
			auto [dir, enabled] = entry;
			GetShimFileChanges(dir, enabled, file_changes);

			DWORD dwData = enabled ? 1 : 0;	// not used
			RegSetValueExA(hkey, dir.c_str(), 0, REG_DWORD, reinterpret_cast<LPBYTE>(&dwData), sizeof(dwData));
		}

		RegCloseKey(hkey);
		ApplyFileChanges(hDlg, file_changes);
	}
}

void AddSetting(HWND hDlg)
{
	BROWSEINFO bi{};
	bi.hwndOwner = hDlg;
	bi.lpszTitle = "Select Game Installation Directory:";
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
	if (!IsGameDirectory(path, is_x64))
	{
		MessageBox(hDlg, "Selected location does not contain a supported game.", APP_NAME, MB_ICONEXCLAMATION);
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
		FILE_CHANGES file_changes;

		auto settings = LoadRegistrySettings();
		for (auto &entry : settings)
		{
			auto [dir, enabled] = entry;
			GetShimFileChanges(dir, false, file_changes);
		}

		ApplyFileChanges(NULL, file_changes);
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
