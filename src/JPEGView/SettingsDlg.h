#pragma once

class CMainDlg;

// Settings window (Tools > Settings...) with the most used settings of the INI file.
// The window is built at runtime, the values are written to the user INI file.
class CSettingsDlg
{
public:
	CSettingsDlg(CMainDlg* pMainDlg);

	// Shows the window modal to the main window, returns true if the settings were changed
	bool DoModal();

private:
	CMainDlg* m_pMainDlg;
	HWND m_hWnd;
	HFONT m_hFont;
	bool m_bDone, m_bOK, m_bEditINI;
	HWND m_hToolbar, m_hStatusBar, m_hSidePanel, m_hDarkTheme;
	HWND m_hSidePanelWidth, m_hLanguage, m_hJPEGQuality, m_hCustomPresets, m_hOutputFolder;

	void CreateControls();
	HWND AddControl(LPCTSTR sClass, LPCTSTR sText, DWORD nStyle, int nX, int nY, int nWidth, int nHeight, int nId);
	void Apply();
	static LRESULT CALLBACK WndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam);
};
