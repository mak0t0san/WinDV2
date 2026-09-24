// RecordCfg.cpp : "Record" settings page

#include "stdafx.h"
#include "WinDV.h"
#include "RecordCfg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CRecordCfg::CRecordCfg() : CPropertyPage(IDD), m_aviSuffixCtl(L" | "), m_aviPrefixCtl(L" | ") {}

void CRecordCfg::DoDataExchange(CDataExchange* pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_AVI_SUFFIX, m_aviSuffixCtl);
	DDX_Control(pDX, IDC_AVI_PREFIX, m_aviPrefixCtl);
	DDX_Check(pDX, IDC_RECORDPREVIEW, m_recordPreview);
	DDX_Text(pDX, IDC_AVI_PREFIX, m_aviPrefix);
	DDX_Text(pDX, IDC_AVI_SUFFIX, m_aviSuffix);
}

BEGIN_MESSAGE_MAP(CRecordCfg, CPropertyPage)
	ON_BN_CLICKED(IDC_PREFIX_SEL, OnPrefixSel)
	ON_BN_CLICKED(IDC_SUFFIX_SEL, OnSuffixSel)
END_MESSAGE_MAP()

void CRecordCfg::OnPrefixSel()
{
	SelectFile(true, &m_aviPrefixCtl);
}

void CRecordCfg::OnSuffixSel()
{
	SelectFile(true, &m_aviSuffixCtl);
}
