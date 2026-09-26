// VideoDeviceSel.cpp : dialog for picking a DV device

#include "stdafx.h"
#include "WinDV.h"
#include "VideoDeviceSel.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CVideoDeviceSel::CVideoDeviceSel(const std::vector<CString>& devices, const CString& selected, CWnd* pParent)
    : CDialog(IDD, pParent), m_devices(devices), m_selName(selected)
{}

void CVideoDeviceSel::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_DEVLIST, m_listbox);
}

BEGIN_MESSAGE_MAP(CVideoDeviceSel, CDialog)
	ON_LBN_DBLCLK(IDC_DEVLIST, OnDblclkDevlist)
END_MESSAGE_MAP()

BOOL CVideoDeviceSel::OnInitDialog()
{
	CDialog::OnInitDialog();

	// The list box is not sorted, so its indices match m_devices.
	for (std::size_t i = 0; i < m_devices.size(); ++i) {
		m_listbox.AddString(m_devices[i]);
		if (m_devices[i] == m_selName) {
			m_selected = static_cast<int>(i);
		}
	}
	m_listbox.SetCurSel(m_selected);
	return TRUE;
}

void CVideoDeviceSel::OnDblclkDevlist()
{
	OnOK();
}

void CVideoDeviceSel::OnOK()
{
	m_selected = m_listbox.GetCurSel();
	if (m_selected >= 0) {
		CDialog::OnOK();
	}
}
