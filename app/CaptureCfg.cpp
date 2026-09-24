// CaptureCfg.cpp : "Capture" settings page

#include "stdafx.h"
#include "WinDV.h"
#include "CaptureCfg.h"

#include "TimeFormat.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace {

constexpr UINT_PTR kExampleTimer = 1;
constexpr int kMaxHistory = 10;
constexpr int kMaxSuffixDigits = 4;

} // namespace

CCaptureCfg::CCaptureCfg() : CPropertyPage(IDD)
{}

void CCaptureCfg::DoDataExchange(CDataExchange* pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_FEXAMPLE, m_fexample);
	DDX_Control(pDX, IDC_DTFORMAT, m_dtformatctl);
	DDX_Control(pDX, IDC_NDIGITS, m_ndigitsctl);
	DDX_Text(pDX, IDC_DISCONTINUITY_TRESHOLD, m_discontinuityThreshold);
	DDV_MinMaxUInt(pDX, m_discontinuityThreshold, 0, 1000000);
	DDX_Text(pDX, IDC_EVERY_NTH, m_everyNth);
	DDV_MinMaxUInt(pDX, m_everyNth, 1, 1000000);
	DDX_Text(pDX, IDC_MAX_FRAMES, m_maxAVIFrames);
	DDV_MinMaxUInt(pDX, m_maxAVIFrames, 10, 1000000);
	DDX_Radio(pDX, IDC_TYPE_1, m_type12);
	DDX_CBString(pDX, IDC_DTFORMAT, m_dtformat);
	if (pDX->m_bSaveAndValidate && !windv::IsValidTimeFormat(m_dtformat.GetString())) {
		AfxMessageBox(L"The date/time format contains an invalid % code.", MB_ICONEXCLAMATION);
		pDX->Fail();
	}
	DDX_CBIndex(pDX, IDC_NDIGITS, m_ndigits);
}

BEGIN_MESSAGE_MAP(CCaptureCfg, CPropertyPage)
	ON_WM_TIMER()
END_MESSAGE_MAP()

BOOL CCaptureCfg::OnInitDialog()
{
	CPropertyPage::OnInitDialog();

	for (int i = 0; i <= kMaxSuffixDigits; ++i) {
		CString digits;
		digits.Format(L"%d", i);
		m_ndigitsctl.AddString(digits);
	}

	m_dtformatctl.AddString(m_dtformat);
	int pos = 0;
	for (CString format = m_dtformathistory.Tokenize(L"\n", pos); pos >= 0;
	     format = m_dtformathistory.Tokenize(L"\n", pos)) {
		if (format != m_dtformat) {
			m_dtformatctl.AddString(format);
		}
	}
	if (!m_dtformat.IsEmpty()) {
		m_dtformatctl.AddString(L""); // offer "no date in the name"
	}

	m_ndigitsctl.SetCurSel(m_ndigits);
	UpdateExample();
	SetTimer(kExampleTimer, 500, nullptr);
	return TRUE;
}

void CCaptureCfg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == kExampleTimer) {
		UpdateExample();
	} else {
		CPropertyPage::OnTimer(nIDEvent);
	}
}

// Shows what a capture filename would look like with the current settings.
void CCaptureCfg::UpdateExample()
{
	CString format;
	m_dtformatctl.GetWindowText(format);

	CString example = L"...example";
	if (!windv::IsValidTimeFormat(format.GetString())) {
		m_fexample.SetWindowText(L"(invalid % code)");
		return;
	}
	const CString date = windv::FormatTime(format.GetString(), std::time(nullptr)).c_str();
	if (!date.IsEmpty()) {
		example += L"." + date;
	}

	const int digits = m_ndigitsctl.GetCurSel();
	if (digits > 0) {
		CString number;
		number.Format(L".%0*d", digits, 0);
		example += number;
	}
	m_fexample.SetWindowText(example + L".avi");
}

void CCaptureCfg::OnOK()
{
	CPropertyPage::OnOK();

	// Rebuild the history: the chosen format goes to the top (it is stored
	// separately), followed by the rest, most recent first.
	const int current = m_dtformatctl.FindStringExact(-1, m_dtformat);
	if (current >= 0) {
		m_dtformatctl.DeleteString(current);
	}

	m_dtformathistory.Empty();
	const int count = (std::min)(m_dtformatctl.GetCount(), kMaxHistory);
	for (int i = 0; i < count; ++i) {
		CString format;
		m_dtformatctl.GetLBText(i, format);
		if (format.IsEmpty()) {
			continue;
		}
		if (!m_dtformathistory.IsEmpty()) {
			m_dtformathistory += L"\n";
		}
		m_dtformathistory += format;
	}
}
