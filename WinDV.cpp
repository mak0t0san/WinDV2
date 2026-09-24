// WinDV.cpp : application entry point

#include "stdafx.h"
#include "WinDV.h"
#include "DVToolsDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

BEGIN_MESSAGE_MAP(CWinDVApp, CWinApp)
END_MESSAGE_MAP()

CWinDVApp theApp;

BOOL CWinDVApp::InitInstance()
{
	CWinApp::InitInstance();
	AfxEnableControlContainer();

	// Capture must keep up with the camcorder in real time.
	SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

	// DirectShow objects are created and used from several threads, so the UI
	// thread joins the multithreaded apartment too.
	const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

	// Keep the original author's key so existing settings carry over.
	SetRegistryKey(L"Petr Mourek");

	{
		CDVToolsDlg dlg;
		m_pMainWnd = &dlg;
		dlg.DoModal();
		m_pMainWnd = nullptr;
	}

	if (SUCCEEDED(hrCom))
		CoUninitialize();
	// The dialog has closed; return FALSE to exit instead of starting a message pump.
	return FALSE;
}

void SelectFile(bool open, CWnd* ctrl)
{
	CFileDialog dlg(open, L"avi", nullptr, OFN_HIDEREADONLY | (open ? OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST : 0),
	                L"AVI files (*.avi)|*.avi|All files (*.*)|*.*||", ctrl);

	// Room for a generous multi-selection; the dialog fails rather than
	// truncating if it is exceeded.
	std::vector<wchar_t> buffer(open ? 64 * 1024 : MAX_PATH * 4, L'\0');
	dlg.m_ofn.lpstrFile = buffer.data();
	dlg.m_ofn.nMaxFile = static_cast<DWORD>(buffer.size());

	// Start in, and afterwards move, the working directory; relative paths in
	// the file fields and the saved "WorkingDirectory" setting depend on it.
	// The Vista-style dialog no longer changes it by itself.
	std::vector<wchar_t> initialDir(GetCurrentDirectory(0, nullptr) + 1, L'\0');
	GetCurrentDirectory(static_cast<DWORD>(initialDir.size()), initialDir.data());
	dlg.m_ofn.lpstrInitialDir = initialDir.data();

	if (dlg.DoModal() != IDOK)
		return;
	SetCurrentDirectory(dlg.GetFolderPath());

	CString text;
	if (open) {
		POSITION pos = dlg.GetStartPosition();
		while (pos) {
			text += dlg.GetNextPathName(pos);
			if (pos)
				text += L" | ";
		}
	} else {
		text = dlg.GetPathName();
	}
	ctrl->SetWindowText(text);
}
