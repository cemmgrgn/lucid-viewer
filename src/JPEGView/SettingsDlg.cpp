#include "StdAfx.h"
#include "resource.h"
#include "SettingsDlg.h"
#include "MainDlg.h"
#include "WindowChrome.h"
#include "SettingsProvider.h"
#include "Converter.h"
#include "NLS.h"
#include "HelpersGUI.h"

static const LPCTSTR SETTINGS_CLASS = _T("JPEGViewSettings");
static const int ID_OK = IDOK, ID_CANCEL = IDCANCEL, ID_EDIT_INI = 200;

// The languages offered, the value is the 'Language' setting
static const LPCTSTR s_languages[][2] = { { _T("Automatic"), _T("auto") }, { _T("English"), _T("en") }, { _T("T\x00FCrk\x00E7" "e"), _T("tr") } };

CSettingsDlg::CSettingsDlg(CMainDlg* pMainDlg) {
	m_pMainDlg = pMainDlg;
	m_hWnd = NULL;
	m_hFont = NULL;
	m_bDone = m_bOK = m_bEditINI = false;
}

bool CSettingsDlg::DoModal() {
	HINSTANCE hInstance = ::GetModuleHandle(NULL);
	WNDCLASS wndClass{ 0 };
	wndClass.lpfnWndProc = WndProc;
	wndClass.hInstance = hInstance;
	wndClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	wndClass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wndClass.lpszClassName = SETTINGS_CLASS;
	::RegisterClass(&wndClass); // fails harmlessly when already registered

	HWND hParent = m_pMainDlg->GetHWND();
	int nW = HelpersGUI::ScaleToScreen(520), nH = HelpersGUI::ScaleToScreen(470);
	CRect parentRect;
	::GetWindowRect(hParent, &parentRect);
	m_hWnd = ::CreateWindowEx(WS_EX_DLGMODALFRAME, SETTINGS_CLASS, CNLS::GetString(_T("Settings")), WS_POPUP | WS_CAPTION | WS_SYSMENU,
		parentRect.CenterPoint().x - nW / 2, parentRect.CenterPoint().y - nH / 2, nW, nH, hParent, NULL, hInstance, this);
	if (m_hWnd == NULL) {
		return false;
	}
	CreateControls();
	::EnableWindow(hParent, FALSE);
	::ShowWindow(m_hWnd, SW_SHOWNORMAL);
	::SetFocus(m_hToolbar);
	MSG msg;
	while (!m_bDone && ::GetMessage(&msg, NULL, 0, 0) > 0) {
		if (!::IsDialogMessage(m_hWnd, &msg)) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
	}
	if (m_bOK) {
		Apply();
	}
	::EnableWindow(hParent, TRUE);
	::DestroyWindow(m_hWnd);
	::SetActiveWindow(hParent);
	::DeleteObject(m_hFont);
	if (m_bEditINI) {
		m_pMainDlg->ExecuteCommand(IDM_EDIT_USER_CONFIG);
	}
	return m_bOK;
}

HWND CSettingsDlg::AddControl(LPCTSTR sClass, LPCTSTR sText, DWORD nStyle, int nX, int nY, int nWidth, int nHeight, int nId) {
	HWND hControl = ::CreateWindowEx((_tcscmp(sClass, _T("EDIT")) == 0) ? WS_EX_CLIENTEDGE : 0, sClass, sText, WS_CHILD | WS_VISIBLE | nStyle,
		HelpersGUI::ScaleToScreen(nX), HelpersGUI::ScaleToScreen(nY), HelpersGUI::ScaleToScreen(nWidth), HelpersGUI::ScaleToScreen(nHeight),
		m_hWnd, (HMENU)(INT_PTR)nId, ::GetModuleHandle(NULL), NULL);
	::SendMessage(hControl, WM_SETFONT, (WPARAM)m_hFont, FALSE);
	return hControl;
}

void CSettingsDlg::CreateControls() {
	NONCLIENTMETRICS metrics{ 0 };
	metrics.cbSize = sizeof(metrics);
	::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
	m_hFont = ::CreateFontIndirect(&metrics.lfMessageFont);
	CSettingsProvider& sp = CSettingsProvider::This();
	CWindowChrome* pChrome = m_pMainDlg->GetWindowChrome();
	const DWORD CHECK = BS_AUTOCHECKBOX | WS_TABSTOP;
	const DWORD EDIT = ES_AUTOHSCROLL | WS_TABSTOP;
	const int LABEL_X = 16, FIELD_X = 200, FIELD_W = 290;

	AddControl(_T("BUTTON"), CNLS::GetString(_T("Window")), BS_GROUPBOX, 8, 6, 488, 132, 0);
	m_hToolbar = AddControl(_T("BUTTON"), CNLS::GetString(_T("Menu bar and toolbar")), CHECK, LABEL_X, 26, 230, 20, 0);
	m_hStatusBar = AddControl(_T("BUTTON"), CNLS::GetString(_T("Status bar")), CHECK, LABEL_X, 48, 230, 20, 0);
	m_hSidePanel = AddControl(_T("BUTTON"), CNLS::GetString(_T("Information panel")), CHECK, LABEL_X, 70, 230, 20, 0);
	m_hDarkTheme = AddControl(_T("BUTTON"), CNLS::GetString(_T("Dark theme")), CHECK, 260, 26, 230, 20, 0);
	AddControl(_T("STATIC"), CNLS::GetString(_T("Side panel width (pixels):")), SS_LEFT, LABEL_X, 104, 180, 20, 0);
	CString sWidth;
	sWidth.Format(_T("%d"), sp.SidePanelWidth());
	m_hSidePanelWidth = AddControl(_T("EDIT"), sWidth, EDIT | ES_NUMBER, FIELD_X, 100, 80, 24, 0);
	::SendMessage(m_hToolbar, BM_SETCHECK, pChrome->GetShowToolbar() ? BST_CHECKED : BST_UNCHECKED, 0);
	::SendMessage(m_hStatusBar, BM_SETCHECK, pChrome->GetShowStatusBar() ? BST_CHECKED : BST_UNCHECKED, 0);
	::SendMessage(m_hSidePanel, BM_SETCHECK, pChrome->GetShowSidePanel() ? BST_CHECKED : BST_UNCHECKED, 0);
	::SendMessage(m_hDarkTheme, BM_SETCHECK, sp.ChromeDarkTheme() ? BST_CHECKED : BST_UNCHECKED, 0);

	AddControl(_T("BUTTON"), CNLS::GetString(_T("General")), BS_GROUPBOX, 8, 144, 488, 88, 0);
	AddControl(_T("STATIC"), CNLS::GetString(_T("Language:")), SS_LEFT, LABEL_X, 168, 180, 20, 0);
	m_hLanguage = AddControl(_T("COMBOBOX"), _T(""), CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, FIELD_X, 164, 160, 120, 0);
	int nSelected = 0;
	for (int i = 0; i < sizeof(s_languages) / sizeof(s_languages[0]); i++) {
		::SendMessage(m_hLanguage, CB_ADDSTRING, 0, (LPARAM)(i == 0 ? CNLS::GetString(s_languages[i][0]) : s_languages[i][0]));
		if (_tcsicmp(sp.Language(), s_languages[i][1]) == 0) nSelected = i;
	}
	::SendMessage(m_hLanguage, CB_SETCURSEL, nSelected, 0);
	AddControl(_T("STATIC"), CNLS::GetString(_T("JPEG quality (0 - 100):")), SS_LEFT, LABEL_X, 200, 180, 20, 0);
	CString sQuality;
	sQuality.Format(_T("%d"), sp.JPEGSaveQuality());
	m_hJPEGQuality = AddControl(_T("EDIT"), sQuality, EDIT | ES_NUMBER, FIELD_X, 196, 80, 24, 0);

	AddControl(_T("BUTTON"), CNLS::GetString(_T("Convert to")), BS_GROUPBOX, 8, 238, 488, 140, 0);
	AddControl(_T("STATIC"), CNLS::GetString(_T("Additional conversions:")), SS_LEFT, LABEL_X, 262, 180, 20, 0);
	m_hCustomPresets = AddControl(_T("EDIT"), sp.ConvertCustomPresets(), EDIT, FIELD_X, 258, FIELD_W, 24, 0);
	AddControl(_T("STATIC"), CNLS::GetString(_T("Example: fit2560:webp;scale30:jpg;rot180:png")), SS_LEFT, FIELD_X, 286, FIELD_W, 20, 0);
	AddControl(_T("STATIC"), CNLS::GetString(_T("Output folder:")), SS_LEFT, LABEL_X, 316, 180, 20, 0);
	m_hOutputFolder = AddControl(_T("EDIT"), sp.ConvertOutputFolder(), EDIT, FIELD_X, 312, FIELD_W, 24, 0);
	AddControl(_T("STATIC"), CNLS::GetString(_T("Empty: next to the source file. A name: sub folder.")), SS_LEFT, FIELD_X, 340, FIELD_W, 20, 0);

	AddControl(_T("BUTTON"), CNLS::GetString(_T("Edit INI file...")), BS_PUSHBUTTON | WS_TABSTOP, 8, 392, 130, 28, ID_EDIT_INI);
	AddControl(_T("BUTTON"), CNLS::GetString(_T("OK")), BS_DEFPUSHBUTTON | WS_TABSTOP, 300, 392, 94, 28, ID_OK);
	AddControl(_T("BUTTON"), CNLS::GetString(_T("Cancel")), BS_PUSHBUTTON | WS_TABSTOP, 402, 392, 94, 28, ID_CANCEL);
}

static CString GetText(HWND hControl) {
	int nLength = ::GetWindowTextLength(hControl);
	CString sText;
	::GetWindowText(hControl, sText.GetBuffer(nLength + 1), nLength + 1);
	sText.ReleaseBuffer();
	return sText.Trim();
}

static bool IsChecked(HWND hControl) {
	return ::SendMessage(hControl, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void CSettingsDlg::Apply() {
	CSettingsProvider& sp = CSettingsProvider::This();
	CWindowChrome* pChrome = m_pMainDlg->GetWindowChrome();
	CString sOldPresets = sp.ConvertCustomPresets();
	bool bOldDark = sp.ChromeDarkTheme();
	CString sOldLanguage = sp.Language();

	int nLanguage = (int)::SendMessage(m_hLanguage, CB_GETCURSEL, 0, 0);
	LPCTSTR sLanguage = s_languages[max(0, min(nLanguage, (int)(sizeof(s_languages) / sizeof(s_languages[0])) - 1))][1];
	int nWidth = max(180, min(800, _ttoi(GetText(m_hSidePanelWidth))));
	int nQuality = max(0, min(100, _ttoi(GetText(m_hJPEGQuality))));
	sp.SaveGeneralSettings(IsChecked(m_hDarkTheme), nWidth, sLanguage, nQuality, GetText(m_hCustomPresets), GetText(m_hOutputFolder));

	// the window parts are changed immediately, this also saves them
	if (pChrome->GetShowToolbar() != IsChecked(m_hToolbar)) pChrome->SetShowToolbar(IsChecked(m_hToolbar));
	if (pChrome->GetShowStatusBar() != IsChecked(m_hStatusBar)) pChrome->SetShowStatusBar(IsChecked(m_hStatusBar));
	if (pChrome->GetShowSidePanel() != IsChecked(m_hSidePanel)) pChrome->SetShowSidePanel(IsChecked(m_hSidePanel));
	pChrome->UpdateFrame();

	if (sOldPresets != sp.ConvertCustomPresets()) {
		Converter::ReloadPresets();
		if (Converter::IsExplorerMenuRegistered()) {
			Converter::RegisterExplorerMenu();
		}
	}
	if (bOldDark != sp.ChromeDarkTheme() || sOldLanguage.CompareNoCase(sLanguage) != 0) {
		::MessageBox(m_pMainDlg->GetHWND(), CNLS::GetString(_T("The theme and the language are changed when JPEGView is started the next time.")),
			CNLS::GetString(_T("Settings")), MB_OK | MB_ICONINFORMATION);
	}
}

LRESULT CALLBACK CSettingsDlg::WndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
	CSettingsDlg* pThis = (CSettingsDlg*)::GetWindowLongPtr(hWnd, GWLP_USERDATA);
	switch (nMsg) {
		case WM_NCCREATE:
			::SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCT*)lParam)->lpCreateParams);
			break;
		case WM_COMMAND:
			if (pThis != NULL && HIWORD(wParam) == BN_CLICKED) {
				if (LOWORD(wParam) == ID_OK || LOWORD(wParam) == ID_CANCEL) {
					pThis->m_bOK = LOWORD(wParam) == ID_OK;
					pThis->m_bDone = true;
					::PostMessage(hWnd, WM_NULL, 0, 0); // ends the message loop
					return 0;
				}
				if (LOWORD(wParam) == ID_EDIT_INI) {
					pThis->m_bEditINI = pThis->m_bDone = true;
					::PostMessage(hWnd, WM_NULL, 0, 0);
					return 0;
				}
			}
			break;
		case WM_CLOSE:
			if (pThis != NULL) pThis->m_bDone = true;
			::PostMessage(hWnd, WM_NULL, 0, 0);
			return 0;
	}
	return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}
