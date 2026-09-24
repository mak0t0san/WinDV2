// WinDV.h : main header file for the WinDV application
#pragma once

#ifndef __AFXWIN_H__
#error include 'stdafx.h' before including this file for PCH
#endif

#include "resource.h" // main symbols

// Shows an AVI file dialog and puts the chosen path(s) into ctrl. With open,
// several files may be picked; they are joined with " | ".
void SelectFile(bool open, CWnd* ctrl);

class CWinDVApp : public CWinApp {
public:
	BOOL InitInstance() override;

	DECLARE_MESSAGE_MAP()
};
