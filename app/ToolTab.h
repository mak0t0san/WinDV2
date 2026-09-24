// ToolTab.h : owner-drawn tab control that blends with the dialog background
#pragma once

class CToolTab : public CTabCtrl {
protected:
	void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct) override;
};
