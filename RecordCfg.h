// RecordCfg.h : "Record" settings page
#pragma once

#include "DropFilesEdit.h"

class CRecordCfg : public CPropertyPage {
public:
	CRecordCfg();

	enum { IDD = IDD_RECORD_CONFIG };

	BOOL m_recordPreview = FALSE;
	CString m_aviPrefix; // files recorded before / after the selection
	CString m_aviSuffix;

protected:
	void DoDataExchange(CDataExchange* pDX) override;
	afx_msg void OnPrefixSel();
	afx_msg void OnSuffixSel();
	DECLARE_MESSAGE_MAP()

private:
	CDropFilesEdit m_aviSuffixCtl;
	CDropFilesEdit m_aviPrefixCtl;
};
