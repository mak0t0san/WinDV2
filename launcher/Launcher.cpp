// WinDV.exe at the top of the portable zip. The WinUI app has to sit next to
// its runtime (hundreds of DLLs and language folders), so the zip keeps all of
// that in an "app" subfolder and this stub starts app\WinDV.exe from there.
#include <windows.h>
#include <shlwapi.h>

#include <string>

namespace {

std::wstring ModuleDirectory()
{
	std::wstring path(MAX_PATH, L'\0');
	for (;;) {
		DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
		if (length == 0) {
			return {};
		}
		if (length < path.size()) {
			path.resize(length);
			break;
		}
		path.resize(path.size() * 2);
	}
	return path.substr(0, path.find_last_of(L'\\') + 1);
}

void ShowError(const std::wstring& message)
{
	MessageBoxW(nullptr, message.c_str(), L"WinDV", MB_OK | MB_ICONERROR);
}

} // namespace

int WINAPI wWinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
{
	const std::wstring appDir = ModuleDirectory() + L"app\\";
	const std::wstring app = appDir + L"WinDV.exe";
	if (GetFileAttributesW(app.c_str()) == INVALID_FILE_ATTRIBUTES) {
		ShowError(L"The \"app\" folder next to WinDV.exe is missing or incomplete.\n\n"
		          L"Extract the whole zip file (right-click it, then \"Extract All...\") "
		          L"and run WinDV.exe from the extracted folder.");
		return 1;
	}

	// Pass our own arguments through unchanged.
	std::wstring commandLine = L"\"" + app + L"\"";
	const PCWSTR args = PathGetArgsW(GetCommandLineW());
	if (args != nullptr && *args != L'\0') {
		commandLine += L" " + std::wstring(args);
	}

	// Forward the show state too (a shortcut set to "Run: Minimized", say).
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	GetStartupInfoW(&startup);
	PROCESS_INFORMATION process{};
	if (!CreateProcessW(app.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup,
	                    &process)) {
		ShowError(L"Could not start " + app + L" (error " + std::to_wstring(GetLastError()) + L").");
		return 1;
	}
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	return 0;
}
