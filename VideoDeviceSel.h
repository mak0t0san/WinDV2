// VideoDeviceSel.h : dialog for picking a DV device
#pragma once

class CVideoDeviceSel : public CDialog {
public:
	CVideoDeviceSel(const std::vector<CString>& devices, const CString& selected, CWnd* pParent = nullptr);

	enum { IDD = IDD_VIDEODEVICESEL };

	// Index into the device list, or -1 if nothing was chosen.
	int GetSelection() const { return m_selected; }

protected:
	void DoDataExchange(CDataExchange* pDX) override;
	BOOL OnInitDialog() override;
	void OnOK() override;
	afx_msg void OnDblclkDevlist();
	DECLARE_MESSAGE_MAP()

private:
	CListBox m_listbox;
	const std::vector<CString>& m_devices;
	CString m_selName;
	int m_selected = -1;
};
