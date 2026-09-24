// DVToolsDlg.h : main dialog - capture / record tabs, status, command line
#pragma once

#include "DShow.h"
#include "DropFilesEdit.h"
#include "ToolTab.h"

class CDVToolsDlg : public CDialog {
public:
	explicit CDVToolsDlg(CWnd* pParent = nullptr);
	~CDVToolsDlg() override;

	enum { IDD = IDD_DVTOOLS_DIALOG };

protected:
	void DoDataExchange(CDataExchange* pDX) override;
	BOOL OnInitDialog() override;
	void OnOK() override;
	void OnCancel() override;

	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO* lpMMI);
	afx_msg void OnSelchangeToolTab(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnClose();
	afx_msg void OnVsrcSel();
	afx_msg void OnVdstSel();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg void OnMove(int x, int y);
	afx_msg void OnFdstSel();
	afx_msg void OnFsrcSel();
	afx_msg void OnConfig();
	afx_msg void OnCapture();
	afx_msg void OnRecord();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnPicture();
	afx_msg void OnDvctrl();
	afx_msg void OnCmdTabChange(UINT nID);
	afx_msg LRESULT OnDVTimeChange(WPARAM, LPARAM);
	afx_msg LRESULT OnDVError(WPARAM, LPARAM);
	DECLARE_MESSAGE_MAP()

private:
	enum Tab { TabCapture = 0, TabRecord = 1, TabCount };

	int CurrentTab() const;
	void SelectTab(int tab);
	void ShowTabControls();
	void SetToolTabItemSize();
	void InitVideo();
	void StartStatusTimer();
	void ShowError(const CString& message);
	// Runs a pipeline action; on failure resets the pipeline and shows the error.
	template <typename Action>
	void Guarded(Action&& action);
	bool RunCommandLine();
	void LoadSettings();
	void SaveSettings();
	CString RecordFileList(const CString& files) const;
	bool SelectDevice(CString& deviceName, CStatic& label);

	CToolTab m_toolTab;
	CButton m_DVCtrl;
	CStatic m_counter;
	CStatic m_status3;
	CStatic m_status2;
	CDV m_video;
	CStatic m_VDST;
	CStatic m_VSRC;
	CDropFilesEdit m_FSRC;
	CDropFilesEdit m_FDST;
	CStatic m_status;

	HICON m_hIcon = nullptr;
	HICON m_hIconSmall = nullptr;

	// Layout: control rectangles at the dialog's design size, scaled in OnSize.
	std::vector<CRect> m_originalRects;
	CRect m_originalRect{0, 0, 0, 0};
	CRect m_lastRect{0, 0, 0, 0};
	int m_minWidth = 1, m_minHeight = 1;
	// Hidden buttons carrying the tab captions, so their &-mnemonics switch tabs.
	std::array<CButton, TabCount> m_tabChangeBtns;

	CString m_VSRCname;
	CString m_VDSTname;
	CString m_AVIPrefix, m_AVISuffix;
	CString m_DTFormat, m_DTFormatHistory;
	int m_nSuffixDigits = 2;

	bool m_exitOnFinish = false;
};
