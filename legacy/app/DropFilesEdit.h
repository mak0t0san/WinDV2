// DropFilesEdit.h : edit control that accepts files dropped from Explorer
#pragma once

class CDropFilesEdit : public CEdit {
public:
	// Adjusts a dropped path in place; returning false rejects it.
	using Filter = bool (*)(CString& path);

	// With a separator, several dropped files are joined with it; without one
	// only the first file is used.
	explicit CDropFilesEdit(LPCWSTR multidropSeparator = nullptr, Filter filter = nullptr);

protected:
	afx_msg void OnDropFiles(HDROP hDropInfo);
	DECLARE_MESSAGE_MAP()

private:
	CString m_separator;
	Filter m_filter;
};
