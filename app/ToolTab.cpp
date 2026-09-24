// ToolTab.cpp : owner-drawn tab control that blends with the dialog background

#include "stdafx.h"
#include "ToolTab.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

void CToolTab::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	const HDC hdc = lpDrawItemStruct->hDC;
	const int saved = SaveDC(hdc);
	lpDrawItemStruct->rcItem.top += ::GetSystemMetrics(SM_CYEDGE) + 1;

	wchar_t text[256] = L"";
	TCITEM item{};
	item.mask = TCIF_TEXT;
	item.cchTextMax = static_cast<int>(std::size(text));
	item.pszText = text;
	GetItem(static_cast<int>(lpDrawItemStruct->itemID), &item);

	const auto hbr = reinterpret_cast<HBRUSH>(
	    GetParent()->SendMessage(WM_CTLCOLORDLG, reinterpret_cast<WPARAM>(hdc), reinterpret_cast<LPARAM>(m_hWnd)));
	FillRect(hdc, &lpDrawItemStruct->rcItem, hbr);
	DrawText(hdc, text, -1, &lpDrawItemStruct->rcItem, DT_CENTER);
	RestoreDC(hdc, saved);
}
