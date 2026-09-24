// DropFilesEdit.cpp : edit control that accepts files dropped from Explorer

#include "stdafx.h"
#include "DropFilesEdit.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

CDropFilesEdit::CDropFilesEdit(LPCWSTR multidropSeparator, Filter filter)
    : m_separator(multidropSeparator), m_filter(filter)
{}

BEGIN_MESSAGE_MAP(CDropFilesEdit, CEdit)
	ON_WM_DROPFILES()
END_MESSAGE_MAP()

void CDropFilesEdit::OnDropFiles(HDROP hDropInfo)
{
	const UINT count = m_separator.IsEmpty() ? 1 : DragQueryFile(hDropInfo, 0xFFFFFFFF, nullptr, 0);

	CString files;
	for (UINT i = 0; i < count; ++i) {
		const UINT length = DragQueryFile(hDropInfo, i, nullptr, 0);
		if (length == 0)
			continue;
		CString file;
		DragQueryFile(hDropInfo, i, file.GetBuffer(static_cast<int>(length) + 1), length + 1);
		file.ReleaseBuffer();

		if (m_filter && !m_filter(file))
			continue;
		if (!files.IsEmpty())
			files += m_separator;
		files += file;
	}
	DragFinish(hDropInfo);

	if (!files.IsEmpty())
		SetWindowText(files);
}
