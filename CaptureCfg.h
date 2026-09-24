// CaptureCfg.h : "Capture" settings page
#pragma once

class CCaptureCfg : public CPropertyPage {
public:
	CCaptureCfg();

	enum { IDD = IDD_CAPTURE_CONFIG };

	UINT m_discontinuityThreshold = 0;
	UINT m_everyNth = 0;
	UINT m_maxAVIFrames = 0;
	int m_type12 = -1; // radio index: 0 = type-1 AVI, 1 = type-2 AVI
	CString m_dtformat;
	int m_ndigits = -1;
	CString m_dtformathistory; // '\n'-separated, most recent first

protected:
	void DoDataExchange(CDataExchange* pDX) override;
	BOOL OnInitDialog() override;
	void OnOK() override;
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	DECLARE_MESSAGE_MAP()

private:
	void UpdateExample();

	CStatic m_fexample;
	CComboBox m_dtformatctl;
	CComboBox m_ndigitsctl;
};
