// DVToolsDlg.cpp : main dialog - capture / record tabs, status, command line

#include "stdafx.h"
#include "WinDV.h"
#include "DVToolsDlg.h"

#include "CaptureCfg.h"
#include "CommandLine.h"
#include "RecordCfg.h"
#include "VideoDeviceSel.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace {

constexpr UINT_PTR kStatusTimer = 1;
constexpr UINT kStatusInterval = 200; // ms
constexpr UINT IDC_TAB_CHANGE = 0x100;

/////////////////////////////////////////////////////////////////////////////
// About box

class CAboutDlg : public CDialog {
public:
	CAboutDlg() : CDialog(IDD_ABOUTBOX) {}

protected:
	afx_msg void OnEmail() { OpenInShell(L"mailto:petr@mourek.cz?subject=WinDV"); }
	afx_msg void OnUrl() { OpenInShell(L"http://windv.mourek.cz/"); }
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	DECLARE_MESSAGE_MAP()

private:
	// ShellExecute can take a while and wants a single-threaded apartment, so
	// it runs on a short-lived thread of its own.
	static void OpenInShell(const wchar_t* target)
	{
		std::thread([url = std::wstring(target)] {
			const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
			ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
			if (SUCCEEDED(hr))
				CoUninitialize();
		}).detach();
	}
};

BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
	ON_COMMAND(IDC_EMAIL, OnEmail)
	ON_COMMAND(IDC_URL, OnUrl)
	ON_WM_CTLCOLOR()
	ON_WM_SETCURSOR()
END_MESSAGE_MAP()

HBRUSH CAboutDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	const HBRUSH hbr = CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
	switch (pWnd->GetDlgCtrlID()) {
	case IDC_EMAIL:
	case IDC_URL:
		pDC->SetTextColor(RGB(0, 0, 192));
		break;
	}
	return hbr;
}

BOOL CAboutDlg::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	switch (pWnd->GetDlgCtrlID()) {
	case IDOK:
	case IDC_EMAIL:
	case IDC_URL:
		SetCursor(LoadCursor(nullptr, IDC_HAND));
		return TRUE;
	}
	return CDialog::OnSetCursor(pWnd, nHitTest, message);
}

/////////////////////////////////////////////////////////////////////////////
// Layout table: how each control moves and stretches as the dialog resizes.
// dx/dw/dy/dh are the percentages of the size change applied to the left,
// right, top and bottom edges; tabMask says on which tabs it is visible.

constexpr int kAllTabs = -1;
constexpr int kCaptureTab = 1 << 0;
constexpr int kRecordTab = 1 << 1;
constexpr int XL = 25;
constexpr int XR = 75;

struct CtrlProperties {
	int id;
	int dx, dw, dy, dh;
	int tabMask;
};

constexpr std::array kCtrlProperties{
    CtrlProperties{IDC_VIDEO, 0, 100, 0, 100, kAllTabs},
    CtrlProperties{IDC_PICTURE, XR, XR, 100, 100, kAllTabs},
    CtrlProperties{IDC_TOOL_TAB, XL, XR, 100, 100, kAllTabs},
    CtrlProperties{IDC_VSRC_L, XL, XL, 100, 100, kCaptureTab},
    CtrlProperties{IDC_VSRC, XL, XR, 100, 100, kCaptureTab},
    CtrlProperties{IDC_VSRC_SEL, XR, XR, 100, 100, kCaptureTab},
    CtrlProperties{IDC_FSRC_L, XL, XL, 100, 100, kRecordTab},
    CtrlProperties{IDC_FSRC, XL, XR, 100, 100, kRecordTab},
    CtrlProperties{IDC_FSRC_SEL, XR, XR, 100, 100, kRecordTab},
    CtrlProperties{IDC_FDST_L, XL, XL, 100, 100, kCaptureTab},
    CtrlProperties{IDC_FDST, XL, XR, 100, 100, kCaptureTab},
    CtrlProperties{IDC_FDST_SEL, XR, XR, 100, 100, kCaptureTab},
    CtrlProperties{IDC_VDST_L, XL, XL, 100, 100, kRecordTab},
    CtrlProperties{IDC_VDST, XL, XR, 100, 100, kRecordTab},
    CtrlProperties{IDC_VDST_SEL, XR, XR, 100, 100, kRecordTab},
    CtrlProperties{IDC_CONFIG, XR, XR, 100, 100, kCaptureTab | kRecordTab},
    CtrlProperties{IDC_DVCTRL, XR, XR, 100, 100, kAllTabs},
    CtrlProperties{IDC_CAPTURE, XR, XR, 100, 100, kCaptureTab},
    CtrlProperties{IDC_RECORD, XR, XR, 100, 100, kRecordTab},
    CtrlProperties{IDCANCEL, XR, XR, 100, 100, kCaptureTab | kRecordTab},
    CtrlProperties{IDC_COUNTER, XR, XR, 100, 100, kAllTabs},
    CtrlProperties{IDC_STATUS, XL, XR, 100, 100, kAllTabs},
    CtrlProperties{IDC_STATUS2, XR, XR, 100, 100, kAllTabs},
    CtrlProperties{IDC_STATUS3, XR, XR, 100, 100, kAllTabs},
};

// Reduces a capture filename to its base: "D:\dv\tape.04-07-15.00.avi" gives
// "D:\dv\tape". Only the file name part is cut, so dots in folders are kept.
bool CaptureFilenameExtractBase(CString& file)
{
	const int separator = (std::max)({file.ReverseFind(L'\\'), file.ReverseFind(L'/'), file.ReverseFind(L':')});
	const int dot = file.Find(L'.', separator + 1);
	if (dot >= 0)
		file.Truncate(dot);
	return !file.IsEmpty();
}

CString LoadResourceString(UINT id)
{
	CString text;
	VERIFY(text.LoadString(id));
	return text;
}

} // namespace

/////////////////////////////////////////////////////////////////////////////
// CDVToolsDlg

CDVToolsDlg::CDVToolsDlg(CWnd* pParent)
    : CDialog(IDD, pParent), m_FSRC(L" | "), m_FDST(nullptr, CaptureFilenameExtractBase)
{
	m_hIcon = static_cast<HICON>(
	    LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(IDR_MAINFRAME), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
	m_hIconSmall = static_cast<HICON>(LoadImage(AfxGetResourceHandle(), MAKEINTRESOURCE(IDR_MAINFRAME), IMAGE_ICON,
	                                            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
}

CDVToolsDlg::~CDVToolsDlg()
{
	if (m_hIcon)
		DestroyIcon(m_hIcon);
	if (m_hIconSmall)
		DestroyIcon(m_hIconSmall);
}

void CDVToolsDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_TOOL_TAB, m_toolTab);
	DDX_Control(pDX, IDC_DVCTRL, m_DVCtrl);
	DDX_Control(pDX, IDC_COUNTER, m_counter);
	DDX_Control(pDX, IDC_STATUS3, m_status3);
	DDX_Control(pDX, IDC_STATUS2, m_status2);
	DDX_Control(pDX, IDC_VIDEO, m_video);
	DDX_Control(pDX, IDC_VDST, m_VDST);
	DDX_Control(pDX, IDC_VSRC, m_VSRC);
	DDX_Control(pDX, IDC_FSRC, m_FSRC);
	DDX_Control(pDX, IDC_FDST, m_FDST);
	DDX_Control(pDX, IDC_STATUS, m_status);
}

BEGIN_MESSAGE_MAP(CDVToolsDlg, CDialog)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_SIZE()
	ON_WM_GETMINMAXINFO()
	ON_NOTIFY(TCN_SELCHANGE, IDC_TOOL_TAB, OnSelchangeToolTab)
	ON_WM_CLOSE()
	ON_BN_CLICKED(IDC_VSRC_SEL, OnVsrcSel)
	ON_BN_CLICKED(IDC_VDST_SEL, OnVdstSel)
	ON_WM_CTLCOLOR()
	ON_WM_MOVE()
	ON_BN_CLICKED(IDC_FDST_SEL, OnFdstSel)
	ON_BN_CLICKED(IDC_FSRC_SEL, OnFsrcSel)
	ON_BN_CLICKED(IDC_CONFIG, OnConfig)
	ON_BN_CLICKED(IDC_CAPTURE, OnCapture)
	ON_BN_CLICKED(IDC_RECORD, OnRecord)
	ON_WM_TIMER()
	ON_BN_CLICKED(IDC_PICTURE, OnPicture)
	ON_BN_CLICKED(IDC_DVCTRL, OnDvctrl)
	ON_COMMAND_RANGE(IDC_TAB_CHANGE, IDC_TAB_CHANGE + TabCount - 1, OnCmdTabChange)
	ON_MESSAGE(WM_DV_TIMECHANGE, OnDVTimeChange)
	ON_MESSAGE(WM_DV_ERROR, OnDVError)
END_MESSAGE_MAP()

BOOL CDVToolsDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	// "About..." in the system menu. IDM_ABOUTBOX must be in the system command range.
	static_assert((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX && IDM_ABOUTBOX < 0xF000);
	if (CMenu* sysMenu = GetSystemMenu(FALSE)) {
		const CString aboutMenu = LoadResourceString(IDS_ABOUTBOX);
		if (!aboutMenu.IsEmpty()) {
			sysMenu->AppendMenu(MF_SEPARATOR);
			sysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, aboutMenu);
		}
	}

	SetIcon(m_hIcon, TRUE);
	SetIcon(m_hIconSmall, FALSE);

	GetClientRect(&m_originalRect);
	GetWindowRect(&m_lastRect);
	m_minWidth = m_lastRect.Width();
	m_minHeight = m_lastRect.Height();

	const UINT tabNames[TabCount] = {IDS_TAB_VIDEO_CAPTURE, IDS_TAB_VIDEO_RECORDING};
	for (int tab = 0; tab < TabCount; ++tab) {
		const CString name = LoadResourceString(tabNames[tab]);
		m_tabChangeBtns[tab].Create(name, WS_CHILD, CRect(0, 0, 0, 0), this, IDC_TAB_CHANGE + tab);
		m_toolTab.InsertItem(tab, name);
	}

	m_originalRects.clear();
	for (const CtrlProperties& ctrl : kCtrlProperties) {
		CRect rect;
		GetDlgItem(ctrl.id)->GetWindowRect(&rect);
		ScreenToClient(&rect);
		m_originalRects.push_back(rect);
	}
	SetToolTabItemSize();

	LoadSettings();

	if (!RunCommandLine())
		EndDialog(IDCANCEL);
	return TRUE;
}

void CDVToolsDlg::LoadSettings()
{
	CWinApp* app = AfxGetApp();

	const int wx = app->GetProfileInt(L"MainWindow", L"X", 0);
	const int wy = app->GetProfileInt(L"MainWindow", L"Y", 0);
	const int ww = app->GetProfileInt(L"MainWindow", L"W", 0);
	const int wh = app->GetProfileInt(L"MainWindow", L"H", 0);
	if (ww > 0 && wh > 0)
		SetWindowPos(nullptr, wx, wy, ww, wh, SWP_NOZORDER);
	else
		PostMessage(WM_SYSCOMMAND, IDM_ABOUTBOX, 0); // first run

	m_video.m_DVctrl = app->GetProfileInt(L"MainWindow", L"DVControlEnabled", m_video.m_DVctrl ? 1 : 0) > 0;
	m_DVCtrl.SetCheck(m_video.m_DVctrl ? BST_CHECKED : BST_UNCHECKED);

	SetCurrentDirectory(app->GetProfileString(L"MainWindow", L"WorkingDirectory", L"."));

	m_VSRCname = app->GetProfileString(L"Capture", L"DVDevice", L"Microsoft DV Camera and VCR");
	m_VDSTname = app->GetProfileString(L"Record", L"DVDevice", L"Microsoft DV Camera and VCR");
	m_VSRC.SetWindowText(m_VSRCname);
	m_VDST.SetWindowText(m_VDSTname);

	m_FSRC.SetWindowText(app->GetProfileString(L"Record", L"File", L""));
	m_FDST.SetWindowText(app->GetProfileString(L"Capture", L"File", L""));

	const int tab = app->GetProfileInt(L"MainWindow", L"SelectedTool", TabCapture);
	m_toolTab.SetCurSel(tab >= 0 && tab < TabCount ? tab : TabCapture);

	m_AVIPrefix = app->GetProfileString(L"Record", L"AVIPrefix", L"");
	m_AVISuffix = app->GetProfileString(L"Record", L"AVISuffix", L"");
	m_video.m_recordPreview = app->GetProfileInt(L"Record", L"Preview", m_video.m_recordPreview ? 1 : 0) > 0;

	m_video.m_type2AVI = app->GetProfileInt(L"Capture", L"Type2AVI", m_video.m_type2AVI ? 1 : 0) > 0;
	// "Treshold" is misspelt in the registry since WinDV 1.0; kept for compatibility.
	m_video.m_discontinuityThreshold =
	    (std::max)(0, static_cast<int>(
	                      app->GetProfileInt(L"Capture", L"DiscontinuityTreshold", m_video.m_discontinuityThreshold)));
	m_video.m_maxAVIFrames =
	    (std::max)(10, static_cast<int>(app->GetProfileInt(L"Capture", L"MaxAVIFrames", m_video.m_maxAVIFrames)));
	m_video.m_everyNth =
	    (std::max)(1, static_cast<int>(app->GetProfileInt(L"Capture", L"EveryNth", m_video.m_everyNth)));

	m_DTFormat = app->GetProfileString(L"Capture", L"DateTimeFormat", L"%y-%m-%d_%H-%M");
	m_DTFormatHistory =
	    app->GetProfileString(L"Capture", L"DateTimeFormatHistory",
	                          L"%y-%m-%d_%H-%M-%S\n%Y-%m-%d_%H-%M\n%Y-%m-%d_%H-%M-%S\n%Y%m%d-%H%M%S\n%a_%H-%M-%S");
	m_nSuffixDigits = std::clamp(static_cast<int>(app->GetProfileInt(L"Capture", L"SuffixDigits", 2)), 0, 4);
}

void CDVToolsDlg::SaveSettings()
{
	CWinApp* app = AfxGetApp();

	app->WriteProfileInt(L"MainWindow", L"X", m_lastRect.left);
	app->WriteProfileInt(L"MainWindow", L"Y", m_lastRect.top);
	app->WriteProfileInt(L"MainWindow", L"W", m_lastRect.Width());
	app->WriteProfileInt(L"MainWindow", L"H", m_lastRect.Height());
	app->WriteProfileInt(L"MainWindow", L"DVControlEnabled", m_video.m_DVctrl ? 1 : 0);
	app->WriteProfileInt(L"MainWindow", L"SelectedTool", CurrentTab());

	std::vector<wchar_t> workingDir(GetCurrentDirectory(0, nullptr) + 1, L'\0');
	GetCurrentDirectory(static_cast<DWORD>(workingDir.size()), workingDir.data());
	app->WriteProfileString(L"MainWindow", L"WorkingDirectory", workingDir.data());

	app->WriteProfileString(L"Capture", L"DVDevice", m_VSRCname);
	app->WriteProfileString(L"Record", L"DVDevice", m_VDSTname);
	CString text;
	m_FSRC.GetWindowText(text);
	app->WriteProfileString(L"Record", L"File", text);
	m_FDST.GetWindowText(text);
	app->WriteProfileString(L"Capture", L"File", text);

	app->WriteProfileInt(L"Capture", L"Type2AVI", m_video.m_type2AVI ? 1 : 0);
	app->WriteProfileInt(L"Capture", L"DiscontinuityTreshold", m_video.m_discontinuityThreshold);
	app->WriteProfileInt(L"Capture", L"MaxAVIFrames", m_video.m_maxAVIFrames);
	app->WriteProfileInt(L"Capture", L"EveryNth", m_video.m_everyNth);
	app->WriteProfileString(L"Capture", L"DateTimeFormat", m_DTFormat);
	app->WriteProfileString(L"Capture", L"DateTimeFormatHistory", m_DTFormatHistory);
	app->WriteProfileInt(L"Capture", L"SuffixDigits", m_nSuffixDigits);

	app->WriteProfileString(L"Record", L"AVIPrefix", m_AVIPrefix);
	app->WriteProfileString(L"Record", L"AVISuffix", m_AVISuffix);
	app->WriteProfileInt(L"Record", L"Preview", m_video.m_recordPreview ? 1 : 0);
}

// Handles "capture ..." / "record ..." arguments. Returns false if the
// arguments are invalid and the program should exit.
bool CDVToolsDlg::RunCommandLine()
{
	std::vector<std::wstring> args;
	int argc = 0;
	if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
		for (int i = 1; i < argc; ++i)
			args.emplace_back(argv[i]);
		LocalFree(argv);
	}

	const auto commandLine = windv::ParseCommandLine(args);
	if (!commandLine) {
		MessageBox(LoadResourceString(IDS_USAGE), nullptr, MB_OK | MB_ICONEXCLAMATION);
		return false;
	}

	switch (commandLine->mode) {
	case windv::CommandLine::Mode::Interactive:
		ShowTabControls();
		InitVideo();
		break;

	case windv::CommandLine::Mode::Capture: {
		m_toolTab.SetCurSel(TabCapture);
		ShowTabControls();
		m_exitOnFinish = commandLine->exitOnFinish;
		const CString file = commandLine->captureFile.c_str();
		Guarded([&] {
			m_FDST.SetWindowText(file);
			m_video.BuildCapturing(m_VSRCname);
			m_video.StartCapturing(file, m_DTFormat, m_nSuffixDigits, commandLine->duration);
			StartStatusTimer();
		});
		break;
	}

	case windv::CommandLine::Mode::Record: {
		m_toolTab.SetCurSel(TabRecord);
		ShowTabControls();
		m_exitOnFinish = commandLine->exitOnFinish;
		CString files;
		for (const std::wstring& file : commandLine->recordFiles) {
			if (!files.IsEmpty())
				files += L" | ";
			files += file.c_str();
		}
		Guarded([&] {
			m_FSRC.SetWindowText(files);
			m_video.BuildRecording(RecordFileList(files), m_VDSTname);
			m_video.StartRecording();
			StartStatusTimer();
		});
		break;
	}
	}
	return true;
}

template <typename Action>
void CDVToolsDlg::Guarded(Action&& action)
{
	CString error;
	try {
		action();
		return;
	} catch (const DShowError& e) {
		error = e.Message();
	} catch (const std::exception& e) {
		error = e.what();
	} catch (CException* e) {
		e->GetErrorMessage(error.GetBuffer(512), 512);
		error.ReleaseBuffer();
		e->Delete();
	}
	InitVideo();
	ShowError(error);
}

void CDVToolsDlg::ShowError(const CString& message)
{
	m_status.SetWindowText(L"Error: " + message);
}

CString CDVToolsDlg::RecordFileList(const CString& files) const
{
	return m_AVIPrefix + L'|' + files + L'|' + m_AVISuffix;
}

void CDVToolsDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX) {
		CAboutDlg about;
		about.DoModal();
	} else {
		CDialog::OnSysCommand(nID, lParam);
	}
}

void CDVToolsDlg::OnPaint()
{
	if (!IsIconic()) {
		CDialog::OnPaint();
		return;
	}

	CPaintDC dc(this);
	SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);
	CRect rect;
	GetClientRect(&rect);
	const int x = (rect.Width() - GetSystemMetrics(SM_CXICON) + 1) / 2;
	const int y = (rect.Height() - GetSystemMetrics(SM_CYICON) + 1) / 2;
	dc.DrawIcon(x, y, m_hIcon);
}

HCURSOR CDVToolsDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

void CDVToolsDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	CDialog::OnGetMinMaxInfo(lpMMI);
	lpMMI->ptMinTrackSize.x = m_minWidth;
	lpMMI->ptMinTrackSize.y = m_minHeight;
}

void CDVToolsDlg::OnSize(UINT nType, int cx, int cy)
{
	CDialog::OnSize(nType, cx, cy);
	if (nType == SIZE_RESTORED)
		GetWindowRect(&m_lastRect);
	if (m_originalRects.size() != kCtrlProperties.size())
		return; // not initialized yet

	const int dx = cx - m_originalRect.right;
	const int dy = cy - m_originalRect.bottom;
	for (std::size_t i = 0; i < kCtrlProperties.size(); ++i) {
		const CtrlProperties& ctrl = kCtrlProperties[i];
		const CRect& rect = m_originalRects[i];
		GetDlgItem(ctrl.id)->MoveWindow(ctrl.dx * dx / 100 + rect.left, ctrl.dy * dy / 100 + rect.top,
		                                (ctrl.dw - ctrl.dx) * dx / 100 + rect.Width(),
		                                (ctrl.dh - ctrl.dy) * dy / 100 + rect.Height(), FALSE);
	}
	SetToolTabItemSize();
	InvalidateRect(nullptr);
	UpdateWindow();
}

void CDVToolsDlg::OnMove(int x, int y)
{
	CDialog::OnMove(x, y);
	if (!IsIconic() && !IsZoomed())
		GetWindowRect(&m_lastRect);
}

int CDVToolsDlg::CurrentTab() const
{
	const int tab = m_toolTab.GetCurSel();
	return tab >= 0 && tab < TabCount ? tab : TabCapture;
}

void CDVToolsDlg::ShowTabControls()
{
	const int tabBit = 1 << CurrentTab();
	for (const CtrlProperties& ctrl : kCtrlProperties)
		GetDlgItem(ctrl.id)->ShowWindow((ctrl.tabMask & tabBit) ? SW_SHOW : SW_HIDE);
	UpdateWindow();
}

void CDVToolsDlg::SelectTab(int tab)
{
	m_toolTab.SetCurSel(tab);
	ShowTabControls();
	InitVideo();
}

void CDVToolsDlg::OnSelchangeToolTab(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
	SelectTab(CurrentTab());
	*pResult = 0;
}

void CDVToolsDlg::OnCmdTabChange(UINT nID)
{
	const int newTab = static_cast<int>(nID - IDC_TAB_CHANGE);
	if (m_toolTab.IsWindowEnabled() && m_toolTab.GetCurSel() != newTab)
		SelectTab(newTab);
}

void CDVToolsDlg::SetToolTabItemSize()
{
	CRect item, tab;
	m_toolTab.GetItemRect(0, &item);
	m_toolTab.GetWindowRect(&tab);
	m_toolTab.SetItemSize(CSize(tab.Width() * 2 / (m_toolTab.GetItemCount() * 2 + 1), item.Height()));
}

HBRUSH CDVToolsDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (pWnd->GetDlgCtrlID() == IDC_VIDEO)
		return static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
	return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CDVToolsDlg::OnCancel()
{
	// Esc / the Stop button resets the pipeline instead of closing the dialog.
	InitVideo();
}

void CDVToolsDlg::OnOK()
{
	// Enter must not close the dialog.
}

void CDVToolsDlg::OnClose()
{
	KillTimer(kStatusTimer);
	m_video.Destroy();
	SaveSettings();
	CDialog::OnCancel();
}

bool CDVToolsDlg::SelectDevice(CString& deviceName, CStatic& label)
{
	std::vector<CString> devices;
	try {
		devices = GetVideoDeviceList();
	} catch (const DShowError& e) {
		ShowError(e.Message());
		return false;
	}
	if (devices.empty()) {
		MessageBox(L"No DV device found. Connect the camcorder and set it to VCR/VTR mode.", nullptr,
		           MB_OK | MB_ICONINFORMATION);
		return false;
	}

	CVideoDeviceSel devSel(devices, deviceName, this);
	if (devSel.DoModal() != IDOK || devSel.GetSelection() < 0)
		return false;
	deviceName = devices[static_cast<std::size_t>(devSel.GetSelection())];
	label.SetWindowText(deviceName);
	return true;
}

void CDVToolsDlg::OnVsrcSel()
{
	if (SelectDevice(m_VSRCname, m_VSRC))
		InitVideo();
}

void CDVToolsDlg::OnVdstSel()
{
	if (SelectDevice(m_VDSTname, m_VDST))
		InitVideo();
}

void CDVToolsDlg::StartStatusTimer()
{
	SetTimer(kStatusTimer, kStatusInterval, nullptr);
}

// Rebuilds the pipeline for the current tab: capture shows the live picture
// straight away, record waits for files to be chosen.
void CDVToolsDlg::InitVideo()
{
	m_exitOnFinish = false;
	KillTimer(kStatusTimer);
	m_status.SetWindowText(L"Initializing...");
	m_status2.SetWindowText(L"");
	m_status3.SetWindowText(L"");
	m_counter.SetWindowText(L"");

	if (CurrentTab() == TabCapture) {
		try {
			m_video.BuildCapturing(m_VSRCname);
			StartStatusTimer();
		} catch (const DShowError& e) {
			ShowError(e.Message());
		} catch (const std::exception& e) {
			ShowError(CString(e.what()));
		}
	} else {
		m_video.Destroy();
		m_status.SetWindowText(L"Select file and press <Record>");
	}
}

void CDVToolsDlg::OnFsrcSel()
{
	SelectFile(true, &m_FSRC);
}

void CDVToolsDlg::OnFdstSel()
{
	SelectFile(false, &m_FDST);
	CString filename;
	m_FDST.GetWindowText(filename);
	CaptureFilenameExtractBase(filename);
	m_FDST.SetWindowText(filename);
}

void CDVToolsDlg::OnCapture()
{
	switch (m_video.GetState()) {
	case CDV::CapturePaused:
		Guarded([&] {
			CString filename;
			m_FDST.GetWindowText(filename);
			m_video.StartCapturing(filename, m_DTFormat, m_nSuffixDigits);
		});
		break;
	case CDV::Capturing:
		Guarded([&] { m_video.StopCapturing(); });
		break;
	default:
		InitVideo();
		break;
	}
}

void CDVToolsDlg::OnRecord()
{
	switch (m_video.GetState()) {
	case CDV::RecordPaused:
		Guarded([&] { m_video.StartRecording(); });
		break;
	case CDV::Recording:
		Guarded([&] { m_video.StopRecording(); });
		break;
	default:
		Guarded([&] {
			CString filename;
			m_FSRC.GetWindowText(filename);
			m_video.BuildRecording(RecordFileList(filename), m_VDSTname);
			StartStatusTimer();
		});
		break;
	}
}

void CDVToolsDlg::OnConfig()
{
	CCaptureCfg captureCfg;
	captureCfg.m_type12 = m_video.m_type2AVI ? 1 : 0;
	captureCfg.m_discontinuityThreshold = static_cast<UINT>(m_video.m_discontinuityThreshold.load());
	captureCfg.m_maxAVIFrames = static_cast<UINT>(m_video.m_maxAVIFrames.load());
	captureCfg.m_everyNth = static_cast<UINT>(m_video.m_everyNth.load());
	captureCfg.m_dtformat = m_DTFormat;
	captureCfg.m_dtformathistory = m_DTFormatHistory;
	captureCfg.m_ndigits = m_nSuffixDigits;

	CRecordCfg recordCfg;
	recordCfg.m_aviPrefix = m_AVIPrefix;
	recordCfg.m_aviSuffix = m_AVISuffix;
	recordCfg.m_recordPreview = m_video.m_recordPreview ? TRUE : FALSE;

	CPropertySheet cfgDlg(IDS_CONFIG_DLG, this);
	cfgDlg.m_psh.dwFlags |= PSH_NOAPPLYNOW;
	cfgDlg.AddPage(&captureCfg);
	cfgDlg.AddPage(&recordCfg);
	cfgDlg.SetActivePage(CurrentTab());

	if (cfgDlg.DoModal() != IDOK)
		return;

	m_video.m_type2AVI = captureCfg.m_type12 == 1;
	m_video.m_discontinuityThreshold = static_cast<int>(captureCfg.m_discontinuityThreshold);
	m_video.m_maxAVIFrames = static_cast<int>(captureCfg.m_maxAVIFrames);
	m_video.m_everyNth = static_cast<int>(captureCfg.m_everyNth);
	m_DTFormat = captureCfg.m_dtformat;
	m_DTFormatHistory = captureCfg.m_dtformathistory;
	m_nSuffixDigits = captureCfg.m_ndigits;

	m_AVIPrefix = recordCfg.m_aviPrefix;
	m_AVISuffix = recordCfg.m_aviSuffix;
	m_video.m_recordPreview = recordCfg.m_recordPreview != FALSE;
}

void CDVToolsDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent != kStatusTimer) {
		CDialog::OnTimer(nIDEvent);
		return;
	}

	const CDV::State state = m_video.GetState();
	// Keep the display on, and the machine awake while tape is running.
	const bool active = state == CDV::Capturing || state == CDV::Recording;
	SetThreadExecutionState(ES_DISPLAY_REQUIRED | (active ? ES_SYSTEM_REQUIRED : 0));

	if (state == CDV::Finished && m_exitOnFinish) {
		OnClose();
		return;
	}

	CString status, counter, queue;
	switch (state) {
	case CDV::Capturing:
		status.Format(L"Capturing...  Press <Capture> for pause. (%ld frames dropped)", m_video.GetDropped());
		break;
	case CDV::CapturePaused:
		status = L"Paused... Press <Capture> for Capturing.";
		break;
	case CDV::Recording:
		status = L"Recording...  Press <Record> for pause.";
		break;
	case CDV::RecordPaused:
		status = L"Paused... Press <Record> for recording.";
		break;
	case CDV::Finished:
		status = L"Finished.";
		break;
	default:
		break;
	}

	if (state != CDV::Idle) {
		REFERENCE_TIME t = m_video.GetTime();
		if (t >= 0) {
			t /= 1000000; // tenths of a second
			const int tenths = static_cast<int>(t % 10);
			t /= 10;
			const int seconds = static_cast<int>(t % 60);
			t /= 60;
			const int minutes = static_cast<int>(t % 60);
			t /= 60;
			counter.Format(L"%d:%02d:%02d.%01d", static_cast<int>(t), minutes, seconds, tenths);
		}
	}

	if (state == CDV::Capturing || state == CDV::Recording || state == CDV::RecordPaused)
		queue.Format(L" Q:%u", static_cast<unsigned>(m_video.GetQueueLoad()));

	// Only touch controls whose text changed, to avoid flicker.
	const auto update = [](CStatic& ctrl, const CString& text) {
		CString current;
		ctrl.GetWindowText(current);
		if (current != text)
			ctrl.SetWindowText(text);
	};
	update(m_status, status);
	update(m_counter, counter);
	update(m_status3, queue);
}

LRESULT CDVToolsDlg::OnDVTimeChange(WPARAM, LPARAM)
{
	CString text;
	const std::time_t recTime = m_video.GetDVTime();
	std::tm local{};
	if (recTime > 0 && localtime_s(&local, &recTime) == 0) {
		wchar_t buf[64] = L"";
		if (wcsftime(buf, std::size(buf), L"%d.%m.'%y %H:%M:%S", &local) > 0)
			text = buf;
	}
	m_status2.SetWindowText(text);
	return 0;
}

LRESULT CDVToolsDlg::OnDVError(WPARAM, LPARAM)
{
	const CString error = m_video.TakeError();
	if (!error.IsEmpty()) {
		InitVideo();
		ShowError(error);
	}
	return 0;
}

void CDVToolsDlg::OnPicture()
{
	OnSysCommand(IDM_ABOUTBOX, 0);
}

void CDVToolsDlg::OnDvctrl()
{
	m_video.m_DVctrl = m_DVCtrl.GetCheck() == BST_CHECKED;
}
