#include "StdAfx.h"
#include "resource.h"
#include "WindowChrome.h"
#include "MainDlg.h"
#include "JPEGImage.h"
#include "FileList.h"
#include "EXIFDisplayCtl.h"
#include "EXIFDisplay.h"
#include "ImageProcPanelCtl.h"
#include "HistogramCorr.h"
#include "SettingsProvider.h"
#include "HelpersGUI.h"
#include "Helpers.h"
#include "NLS.h"
#include "Converter.h"
#include "TimerEventIDs.h"
#include <shellapi.h>

// Sizes in pixels at 96 DPI
static const int TOOLBAR_HEIGHT = 36;
static const int STATUSBAR_HEIGHT = 24;
static const int ITEM_MARGIN = 4; // vertical margin of toolbar items
static const int BUTTON_WIDTH = 34;
static const int MENU_TITLE_PADDING = 10;
static const int SEPARATOR_WIDTH = 11;
static const int SIDE_PADDING = 14;
static const int HISTOGRAM_HEIGHT = 96;
static const int STATUS_MESSAGE_TIMEOUT = 6000; // ms

//////////////////////////////////////////////////////////////////////////////////////////////
// Helpers
//////////////////////////////////////////////////////////////////////////////////////////////

static int CALLBACK FontExistsProc(const LOGFONT*, const TEXTMETRIC*, DWORD, LPARAM lParam) {
	*(bool*)lParam = true;
	return 0;
}

static bool FontExists(LPCTSTR sFaceName) {
	LOGFONT lf{ 0 };
	lf.lfCharSet = DEFAULT_CHARSET;
	_tcscpy_s(lf.lfFaceName, LF_FACESIZE, sFaceName);
	bool bFound = false;
	HDC hDC = ::GetDC(NULL);
	::EnumFontFamiliesEx(hDC, &lf, (FONTENUMPROC)FontExistsProc, (LPARAM)&bFound, 0);
	::ReleaseDC(NULL, hDC);
	return bFound;
}

static HFONT CreateUIFont(LPCTSTR sFaceName, int nPixelHeight, int nWeight) {
	LOGFONT lf{ 0 };
	lf.lfHeight = -nPixelHeight;
	lf.lfWeight = nWeight;
	lf.lfCharSet = DEFAULT_CHARSET;
	lf.lfQuality = CLEARTYPE_QUALITY;
	_tcscpy_s(lf.lfFaceName, LF_FACESIZE, sFaceName);
	return ::CreateFontIndirect(&lf);
}

static DWORD GetWindowsBuildNumber() {
	typedef LONG (WINAPI *RtlGetVersionFn)(OSVERSIONINFOW*);
	RtlGetVersionFn rtlGetVersion = (RtlGetVersionFn)::GetProcAddress(::GetModuleHandle(_T("ntdll.dll")), "RtlGetVersion");
	OSVERSIONINFOW info{ 0 };
	info.dwOSVersionInfoSize = sizeof(info);
	if (rtlGetVersion == NULL || rtlGetVersion(&info) != 0) {
		return 0;
	}
	return info.dwBuildNumber;
}

// Dark title bar and dark popup menus. The menu part uses undocumented uxtheme functions that exist since Windows 10 1903.
static void ApplyDarkMode(HWND hWnd) {
	typedef HRESULT (WINAPI *DwmSetWindowAttributeFn)(HWND, DWORD, LPCVOID, DWORD);
	HMODULE hDwm = ::LoadLibrary(_T("dwmapi.dll"));
	if (hDwm != NULL) {
		DwmSetWindowAttributeFn dwmSetWindowAttribute = (DwmSetWindowAttributeFn)::GetProcAddress(hDwm, "DwmSetWindowAttribute");
		if (dwmSetWindowAttribute != NULL) {
			BOOL bDark = TRUE;
			const DWORD DWMWA_USE_IMMERSIVE_DARK_MODE = 20, DWMWA_USE_IMMERSIVE_DARK_MODE_OLD = 19;
			if (FAILED(dwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &bDark, sizeof(bDark)))) {
				dwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE_OLD, &bDark, sizeof(bDark));
			}
		}
	}

	if (GetWindowsBuildNumber() < 18362) {
		return;
	}
	typedef int (WINAPI *SetPreferredAppModeFn)(int);
	typedef BOOL (WINAPI *AllowDarkModeForWindowFn)(HWND, BOOL);
	typedef void (WINAPI *FlushMenuThemesFn)();
	HMODULE hUxTheme = ::LoadLibrary(_T("uxtheme.dll"));
	if (hUxTheme != NULL) {
		SetPreferredAppModeFn setPreferredAppMode = (SetPreferredAppModeFn)::GetProcAddress(hUxTheme, MAKEINTRESOURCEA(135));
		AllowDarkModeForWindowFn allowDarkModeForWindow = (AllowDarkModeForWindowFn)::GetProcAddress(hUxTheme, MAKEINTRESOURCEA(133));
		FlushMenuThemesFn flushMenuThemes = (FlushMenuThemesFn)::GetProcAddress(hUxTheme, MAKEINTRESOURCEA(136));
		if (setPreferredAppMode != NULL && allowDarkModeForWindow != NULL && flushMenuThemes != NULL) {
			const int FORCE_DARK = 2;
			setPreferredAppMode(FORCE_DARK);
			allowDarkModeForWindow(hWnd, TRUE);
			flushMenuThemes();
		}
	}
}

static LPCTSTR GetImageFormatName(EImageFormat eFormat) {
	switch (eFormat) {
		case IF_JPEG: return _T("JPEG");
		case IF_WindowsBMP: return _T("BMP");
		case IF_PNG: return _T("PNG");
		case IF_GIF: return _T("GIF");
		case IF_TIFF: return _T("TIFF");
		case IF_WEBP: return _T("WEBP");
		case IF_JXL: return _T("JPEG XL");
		case IF_HEIF: return _T("HEIF");
		case IF_AVIF: return _T("AVIF");
		case IF_QOI: return _T("QOI");
		case IF_PSD: return _T("PSD");
		case IF_TGA: return _T("TGA");
		case IF_CameraRAW:
		case IF_JPEG_Embedded: return _T("RAW");
		case IF_WIC: return _T("WIC");
	}
	return _T("");
}

static CString FormatFileSize(__int64 nBytes) {
	CString s;
	if (nBytes >= 1024 * 1024) {
		s.Format(_T("%.2f MB"), nBytes / (1024.0 * 1024.0));
	} else {
		s.Format(_T("%.0f KB"), max(1.0, nBytes / 1024.0));
	}
	return s;
}

static void FillRoundRect(CDC& dc, const CRect& rect, COLORREF color, int nRadius) {
	CBrush brush;
	brush.CreateSolidBrush(color);
	HBRUSH hOldBrush = dc.SelectBrush(brush);
	HPEN hOldPen = dc.SelectPen((HPEN)::GetStockObject(NULL_PEN));
	dc.RoundRect(rect.left, rect.top, rect.right + 1, rect.bottom + 1, nRadius, nRadius);
	dc.SelectPen(hOldPen);
	dc.SelectBrush(hOldBrush);
}

static void DrawHorizontalLine(CDC& dc, int nX1, int nX2, int nY, COLORREF color) {
	dc.FillSolidRect(nX1, nY, nX2 - nX1, max(1, HelpersGUI::ScaleToScreen(1)), color);
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////////////////////////

CWindowChrome::CWindowChrome(CMainDlg* pMainDlg) {
	m_pMainDlg = pMainDlg;
	CSettingsProvider& sp = CSettingsProvider::This();
	m_bShowToolbar = sp.ShowToolbar();
	m_bShowStatusBar = sp.ShowStatusBar();
	m_bShowSidePanel = sp.ShowSidePanel();
	m_nHoverItem = m_nPressedItem = -1;
	m_bTrackingMouse = false;
	m_nSideScroll = 0;
	m_nSideContentHeight = 0;

	if (sp.ChromeDarkTheme()) {
		CColors dark = { RGB(32, 32, 32), RGB(26, 26, 26), RGB(58, 58, 58), RGB(232, 232, 232), RGB(150, 150, 150),
			RGB(62, 62, 62), RGB(82, 82, 82), RGB(42, 66, 96), RGB(96, 165, 250) };
		m_colors = dark;
		ApplyDarkMode(pMainDlg->GetHWND());
	} else {
		CColors light = { RGB(243, 243, 243), RGB(250, 250, 250), RGB(218, 218, 218), RGB(25, 25, 25), RGB(100, 100, 100),
			RGB(225, 225, 225), RGB(205, 205, 205), RGB(204, 228, 247), RGB(0, 103, 192) };
		m_colors = light;
	}

	m_hFont = CreateUIFont(_T("Segoe UI"), HelpersGUI::ScaleToScreen(12), FW_NORMAL);
	m_hBoldFont = CreateUIFont(_T("Segoe UI"), HelpersGUI::ScaleToScreen(13), FW_SEMIBOLD);
	m_bHasIconFont = true;
	if (FontExists(_T("Segoe Fluent Icons"))) {
		m_hIconFont = CreateUIFont(_T("Segoe Fluent Icons"), HelpersGUI::ScaleToScreen(16), FW_NORMAL);
	} else if (FontExists(_T("Segoe MDL2 Assets"))) {
		m_hIconFont = CreateUIFont(_T("Segoe MDL2 Assets"), HelpersGUI::ScaleToScreen(16), FW_NORMAL);
	} else {
		m_hIconFont = NULL;
		m_bHasIconFont = false;
	}

	// Menu titles, the command is the index of the menu (see CMainDlg::CreateMenuBarMenu())
	LPCTSTR menuTitles[] = { _T("File"), _T("Edit"), _T("View"), _T("Navigate"), _T("Tools"), _T("Help") };
	for (int i = 0; i < sizeof(menuTitles) / sizeof(LPCTSTR); i++) {
		CToolbarItem item = { IT_Menu, CNLS::GetString(menuTitles[i]), 0, i, NULL, false, false, CRect() };
		m_items.push_back(item);
	}

	struct ButtonDef { wchar_t Glyph; LPCTSTR Text; int Command; LPCTSTR Tooltip; bool Mirror; bool NeedsImage; };
	const ButtonDef buttons[] = {
		{ 0, _T(""), 0, NULL, false, false },
		{ 0xE8E5, _T("Open"), IDM_OPEN, _T("Open image..."), false, false },
		{ 0xE74E, _T("Save"), IDM_SAVE, _T("Save processed image..."), false, true },
		{ 0xE8AB, _T("Convert"), IDM_CONVERT_MENU, _T("Convert to"), false, true },
		{ 0, _T(""), 0, NULL, false, false },
		{ 0xE76B, _T("<"), IDM_PREV, _T("Previous image"), false, true },
		{ 0xE76C, _T(">"), IDM_NEXT, _T("Next image"), false, true },
		{ 0, _T(""), 0, NULL, false, false },
		{ 0xE8A3, _T("+"), IDM_ZOOM_INC, _T("Zoom in"), false, true },
		{ 0xE71F, _T("-"), IDM_ZOOM_DEC, _T("Zoom out"), false, true },
		{ 0xE9A6, _T("Fit"), IDM_FIT_TO_SCREEN, _T("Fit to screen"), false, true },
		{ 0, _T("1:1"), IDM_ZOOM_100, _T("100 %"), false, true },
		{ 0, _T(""), 0, NULL, false, false },
		{ 0xE7AD, _T("L"), IDM_ROTATE_270, _T("Rotate -90"), true, true },
		{ 0xE7AD, _T("R"), IDM_ROTATE_90, _T("Rotate +90"), false, true },
		{ 0xE70F, _T("Edit"), IDM_TOGGLE_EDITPANEL, _T("Image processing panel"), false, true },
		{ 0, _T(""), 0, NULL, false, false },
		{ 0xE74D, _T("Del"), IDM_MOVE_TO_RECYCLE_BIN_CONFIRM_PERMANENT_DELETE, _T("Move to recycle bin"), false, true },
		{ 0, _T(""), 0, NULL, false, false },
		{ 0xE946, _T("Info"), IDM_TOGGLE_SIDEPANEL, _T("Information panel"), false, false },
		{ 0xE740, _T("Full"), IDM_FULL_SCREEN_MODE, _T("Full screen mode"), false, false },
	};
	for (const ButtonDef& def : buttons) {
		CToolbarItem item = { (def.Command == 0) ? IT_Separator : IT_Button, def.Text, def.Glyph, def.Command, def.Tooltip, def.Mirror, def.NeedsImage, CRect() };
		m_items.push_back(item);
	}

	Helpers::SetAdditionalBorderSize(IsEnabled() ? GetExtraSize() : CSize(0, 0));
}

CWindowChrome::~CWindowChrome() {
	::DeleteObject(m_hFont);
	::DeleteObject(m_hBoldFont);
	if (m_hIconFont != NULL) {
		::DeleteObject(m_hIconFont);
	}
}

bool CWindowChrome::IsEnabled() {
	return !m_pMainDlg->IsFullScreenMode() && !m_pMainDlg->IsWindowBorderless();
}

void CWindowChrome::SetShowToolbar(bool bShow) {
	m_bShowToolbar = bShow;
	SaveSettings();
	UpdateFrame();
}

void CWindowChrome::SetShowStatusBar(bool bShow) {
	m_bShowStatusBar = bShow;
	SaveSettings();
	UpdateFrame();
}

void CWindowChrome::SetShowSidePanel(bool bShow) {
	m_bShowSidePanel = bShow;
	m_nSideScroll = 0;
	SaveSettings();
	UpdateFrame();
}

CSize CWindowChrome::GetExtraSize() {
	return CSize(m_bShowSidePanel ? SidePanelWidth() : 0,
		(m_bShowToolbar ? ToolbarHeight() : 0) + (m_bShowStatusBar ? StatusBarHeight() : 0));
}

void CWindowChrome::SyncBorderSize() {
	Helpers::SetAdditionalBorderSize(IsEnabled() ? GetExtraSize() : CSize(0, 0));
}

void CWindowChrome::UpdateFrame() {
	SyncBorderSize();
	::SetWindowPos(m_pMainDlg->GetHWND(), NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void CWindowChrome::OnNCCalcSize(RECT* pClientRect) {
	bool bEnabled = IsEnabled();
	Helpers::SetAdditionalBorderSize(bEnabled ? GetExtraSize() : CSize(0, 0));
	if (!bEnabled) {
		return;
	}
	int nHeight = pClientRect->bottom - pClientRect->top;
	int nToolbar = IsToolbarShown() ? min(ToolbarHeight(), nHeight) : 0;
	int nStatusBar = IsStatusBarShown() ? min(StatusBarHeight(), nHeight - nToolbar) : 0;
	int nSidePanel = IsSidePanelShown() ? min(SidePanelWidth(), max(0, (int)(pClientRect->right - pClientRect->left) - HelpersGUI::ScaleToScreen(120))) : 0;
	pClientRect->top += nToolbar;
	pClientRect->bottom -= nStatusBar;
	pClientRect->right -= nSidePanel;
}

void CWindowChrome::Paint() {
	HWND hWnd = m_pMainDlg->GetHWND();
	if (!IsEnabled() || ::IsIconic(hWnd)) {
		return;
	}
	UpdateLayout();
	HDC hWindowDC = ::GetWindowDC(hWnd);
	if (hWindowDC == NULL) {
		return;
	}
	CRect parts[3] = { m_toolbarRect, m_statusBarRect, m_sidePanelRect };
	for (int i = 0; i < 3; i++) {
		const CRect& rect = parts[i];
		if (rect.IsRectEmpty()) {
			continue;
		}
		CDC memDC;
		memDC.CreateCompatibleDC(hWindowDC);
		CBitmap bitmap;
		bitmap.CreateCompatibleBitmap(hWindowDC, rect.Width(), rect.Height());
		HBITMAP hOldBitmap = memDC.SelectBitmap(bitmap);
		memDC.SetViewportOrg(-rect.left, -rect.top);
		memDC.SetBkMode(TRANSPARENT);
		if (i == 0) {
			PaintToolbar(memDC);
		} else if (i == 1) {
			PaintStatusBar(memDC);
		} else {
			PaintSidePanel(memDC);
		}
		memDC.SetViewportOrg(0, 0);
		::BitBlt(hWindowDC, rect.left, rect.top, rect.Width(), rect.Height(), memDC, 0, 0, SRCCOPY);
		memDC.SelectBitmap(hOldBitmap);
	}
	::ReleaseDC(hWnd, hWindowDC);
}

LRESULT CWindowChrome::HitTest(CPoint ptScreen) {
	if (!IsEnabled()) {
		return 0;
	}
	UpdateLayout();
	CPoint pt = ScreenToWindow(ptScreen);
	if (m_toolbarRect.PtInRect(pt)) {
		return (FindItem(pt) >= 0) ? HT_CHROME : HTCAPTION; // empty toolbar area moves the window
	}
	if (m_statusBarRect.PtInRect(pt)) {
		return HTCAPTION;
	}
	if (m_sidePanelRect.PtInRect(pt)) {
		return HT_CHROME;
	}
	return 0;
}

void CWindowChrome::OnNCMouseMove(CPoint ptScreen) {
	if (!m_bTrackingMouse) {
		TRACKMOUSEEVENT tme{ 0 };
		tme.cbSize = sizeof(tme);
		tme.dwFlags = TME_LEAVE | TME_NONCLIENT;
		tme.hwndTrack = m_pMainDlg->GetHWND();
		m_bTrackingMouse = ::TrackMouseEvent(&tme) != FALSE;
	}
	UpdateLayout();
	CPoint pt = ScreenToWindow(ptScreen);
	int nItem = FindItem(pt);
	if (nItem >= 0 && !IsItemEnabled(m_items[nItem])) {
		nItem = -1;
	}
	if (nItem != m_nHoverItem) {
		m_nHoverItem = nItem;
		Paint();
	}
}

void CWindowChrome::OnNCMouseLeave() {
	m_bTrackingMouse = false;
	if (m_nHoverItem >= 0 || m_nPressedItem >= 0) {
		m_nHoverItem = m_nPressedItem = -1;
		Paint();
	}
}

void CWindowChrome::OnNCLButtonDown(CPoint ptScreen) {
	UpdateLayout();
	CPoint pt = ScreenToWindow(ptScreen);
	if (!m_sLocationURL.IsEmpty() && m_locationRect.PtInRect(pt)) {
		::ShellExecute(m_pMainDlg->GetHWND(), _T("open"), m_sLocationURL, NULL, NULL, SW_SHOWNORMAL);
		return;
	}
	int nItem = FindItem(pt);
	if (nItem < 0 || !IsItemEnabled(m_items[nItem])) {
		return;
	}
	if (m_items[nItem].Type == IT_Menu) {
		ExecuteItem(nItem); // menus open on mouse down like the standard Windows menu bar
	} else {
		m_nPressedItem = nItem;
		Paint();
	}
}

void CWindowChrome::OnNCLButtonUp(CPoint ptScreen) {
	UpdateLayout();
	int nItem = FindItem(ScreenToWindow(ptScreen));
	int nPressedItem = m_nPressedItem;
	m_nPressedItem = -1;
	Paint();
	if (nItem >= 0 && nItem == nPressedItem && IsItemEnabled(m_items[nItem])) {
		ExecuteItem(nItem);
	}
}

bool CWindowChrome::OnMouseWheel(CPoint ptScreen, int nDelta) {
	if (!IsSidePanelShown()) {
		return false;
	}
	UpdateLayout();
	if (!m_sidePanelRect.PtInRect(ScreenToWindow(ptScreen))) {
		return false;
	}
	int nMaxScroll = max(0, m_nSideContentHeight - m_sidePanelRect.Height());
	m_nSideScroll = max(0, min(nMaxScroll, m_nSideScroll - nDelta * HelpersGUI::ScaleToScreen(48) / WHEEL_DELTA));
	Paint();
	return true;
}

bool CWindowChrome::OnTimer(int nTimerId) {
	if (nTimerId == STATUSBAR_MESSAGE_TIMER_EVENT_ID) {
		::KillTimer(m_pMainDlg->GetHWND(), STATUSBAR_MESSAGE_TIMER_EVENT_ID);
		m_sMessage.Empty();
		Paint();
		return true;
	}
	return false;
}

void CWindowChrome::ShowMessage(LPCTSTR sMessage) {
	m_sMessage = sMessage;
	::SetTimer(m_pMainDlg->GetHWND(), STATUSBAR_MESSAGE_TIMER_EVENT_ID, STATUS_MESSAGE_TIMEOUT, NULL);
	Paint();
}

int CWindowChrome::TrackConvertMenu() {
	HMENU hMenu = Converter::CreatePresetMenu(IDM_FIRST_CONVERT_CMD);
	CPoint pos;
	::GetCursorPos(&pos);
	if (IsToolbarShown()) {
		UpdateLayout();
		for (const CToolbarItem& item : m_items) {
			if (item.Command == IDM_CONVERT_MENU && item.Type == IT_Button && !item.Rect.IsRectEmpty()) {
				pos = CPoint(m_windowRect.left + item.Rect.left, m_windowRect.top + item.Rect.bottom);
				break;
			}
		}
	}
	int nCommand = m_pMainDlg->TrackPopupMenu(pos, hMenu);
	::DestroyMenu(hMenu);
	return nCommand;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Private
//////////////////////////////////////////////////////////////////////////////////////////////

int CWindowChrome::ToolbarHeight() { return HelpersGUI::ScaleToScreen(TOOLBAR_HEIGHT); }
int CWindowChrome::StatusBarHeight() { return HelpersGUI::ScaleToScreen(STATUSBAR_HEIGHT); }
int CWindowChrome::SidePanelWidth() { return HelpersGUI::ScaleToScreen(CSettingsProvider::This().SidePanelWidth()); }

CPoint CWindowChrome::ScreenToWindow(CPoint ptScreen) {
	return CPoint(ptScreen.x - m_windowRect.left, ptScreen.y - m_windowRect.top);
}

void CWindowChrome::SaveSettings() {
	CSettingsProvider::This().SaveWindowChromeSettings(m_bShowToolbar, m_bShowStatusBar, m_bShowSidePanel);
}

void CWindowChrome::UpdateLayout() {
	HWND hWnd = m_pMainDlg->GetHWND();
	::GetWindowRect(hWnd, &m_windowRect);
	CRect clientRect;
	::GetClientRect(hWnd, &clientRect);
	CPoint clientOrigin(0, 0);
	::ClientToScreen(hWnd, &clientOrigin);
	CRect client(CPoint(clientOrigin.x - m_windowRect.left, clientOrigin.y - m_windowRect.top), clientRect.Size());

	int nSidePanel = IsSidePanelShown() ? SidePanelWidth() : 0;
	m_toolbarRect = IsToolbarShown() ? CRect(client.left, client.top - ToolbarHeight(), client.right + nSidePanel, client.top) : CRect(0, 0, 0, 0);
	m_statusBarRect = IsStatusBarShown() ? CRect(client.left, client.bottom, client.right + nSidePanel, client.bottom + StatusBarHeight()) : CRect(0, 0, 0, 0);
	m_sidePanelRect = (nSidePanel > 0) ? CRect(client.right, client.top, client.right + nSidePanel, client.bottom) : CRect(0, 0, 0, 0);

	// toolbar items from left to right, items not fitting are hidden
	HDC hDC = ::GetDC(NULL);
	HFONT hOldFont = (HFONT)::SelectObject(hDC, m_hFont);
	int nX = m_toolbarRect.left + HelpersGUI::ScaleToScreen(6);
	int nTop = m_toolbarRect.top + HelpersGUI::ScaleToScreen(ITEM_MARGIN);
	int nBottom = m_toolbarRect.bottom - HelpersGUI::ScaleToScreen(ITEM_MARGIN);
	for (CToolbarItem& item : m_items) {
		int nWidth;
		if (item.Type == IT_Separator) {
			nWidth = HelpersGUI::ScaleToScreen(SEPARATOR_WIDTH);
		} else if (item.Type == IT_Menu || item.Glyph == 0 || !m_bHasIconFont) {
			SIZE textSize;
			::GetTextExtentPoint32(hDC, item.Text, item.Text.GetLength(), &textSize);
			nWidth = max(HelpersGUI::ScaleToScreen(BUTTON_WIDTH), (int)textSize.cx + 2 * HelpersGUI::ScaleToScreen(MENU_TITLE_PADDING));
		} else {
			nWidth = HelpersGUI::ScaleToScreen(BUTTON_WIDTH);
		}
		if (m_toolbarRect.IsRectEmpty() || nX + nWidth > m_toolbarRect.right - HelpersGUI::ScaleToScreen(4)) {
			item.Rect = CRect(0, 0, 0, 0);
		} else {
			item.Rect = CRect(nX, nTop, nX + nWidth, nBottom);
		}
		nX += nWidth;
	}
	::SelectObject(hDC, hOldFont);
	::ReleaseDC(NULL, hDC);
}

int CWindowChrome::FindItem(CPoint ptWindow) {
	for (int i = 0; i < (int)m_items.size(); i++) {
		if (m_items[i].Type != IT_Separator && m_items[i].Rect.PtInRect(ptWindow)) {
			return i;
		}
	}
	return -1;
}

bool CWindowChrome::IsItemEnabled(const CToolbarItem& item) {
	if (!item.NeedsImage) {
		return true;
	}
	if (m_pMainDlg->GetCurrentImage() == NULL) {
		return false;
	}
	if (item.Command == IDM_CONVERT_MENU || item.Command == IDM_MOVE_TO_RECYCLE_BIN_CONFIRM_PERMANENT_DELETE) {
		return !m_pMainDlg->GetCurrentImage()->IsClipboardImage();
	}
	return true;
}

bool CWindowChrome::IsItemChecked(const CToolbarItem& item) {
	switch (item.Command) {
		case IDM_TOGGLE_SIDEPANEL: return m_bShowSidePanel;
		case IDM_TOGGLE_EDITPANEL: return m_pMainDlg->GetImageProcPanelCtl() != NULL && m_pMainDlg->GetImageProcPanelCtl()->IsPinned();
	}
	return false;
}

void CWindowChrome::ExecuteItem(int nIndex) {
	const CToolbarItem& item = m_items[nIndex];
	int nCommand = 0;
	if (item.Type == IT_Menu) {
		m_nPressedItem = nIndex;
		Paint();
		HMENU hMenu = m_pMainDlg->CreateMenuBarMenu(item.Command);
		if (hMenu != NULL) {
			nCommand = m_pMainDlg->TrackPopupMenu(CPoint(m_windowRect.left + item.Rect.left, m_windowRect.top + item.Rect.bottom), hMenu);
			::DestroyMenu(hMenu);
		}
		m_nPressedItem = m_nHoverItem = -1;
		Paint();
	} else if (item.Command == IDM_CONVERT_MENU) {
		nCommand = TrackConvertMenu();
	} else {
		nCommand = item.Command;
	}
	if (nCommand != 0) {
		m_pMainDlg->ExecuteCommand(nCommand);
	}
}

void CWindowChrome::PaintItem(CDC& dc, const CToolbarItem& item, int nIndex) {
	if (item.Rect.IsRectEmpty()) {
		return;
	}
	if (item.Type == IT_Separator) {
		int nX = (item.Rect.left + item.Rect.right) / 2;
		dc.FillSolidRect(nX, item.Rect.top + HelpersGUI::ScaleToScreen(5), max(1, HelpersGUI::ScaleToScreen(1)), item.Rect.Height() - HelpersGUI::ScaleToScreen(10), m_colors.Border);
		return;
	}

	bool bEnabled = IsItemEnabled(item);
	COLORREF background = m_colors.Background;
	if (bEnabled && nIndex == m_nPressedItem) {
		background = m_colors.Pressed;
	} else if (bEnabled && nIndex == m_nHoverItem) {
		background = m_colors.Hover;
	} else if (IsItemChecked(item)) {
		background = m_colors.Checked;
	}
	if (background != m_colors.Background) {
		FillRoundRect(dc, item.Rect, background, HelpersGUI::ScaleToScreen(8));
	}

	dc.SetTextColor(bEnabled ? m_colors.Text : m_colors.DimText);
	CRect textRect = item.Rect;
	if (item.Type == IT_Menu || item.Glyph == 0 || !m_bHasIconFont) {
		HFONT hOldFont = dc.SelectFont(m_hFont);
		dc.DrawText(item.Text, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		dc.SelectFont(hOldFont);
		return;
	}

	wchar_t glyph[2] = { item.Glyph, 0 };
	if (!item.Mirror) {
		HFONT hOldFont = dc.SelectFont(m_hIconFont);
		dc.DrawText(glyph, 1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		dc.SelectFont(hOldFont);
	} else {
		// paint into a helper bitmap and copy it mirrored
		int nW = textRect.Width(), nH = textRect.Height();
		CDC glyphDC;
		glyphDC.CreateCompatibleDC(dc);
		CBitmap glyphBitmap;
		glyphBitmap.CreateCompatibleBitmap(dc, nW, nH);
		HBITMAP hOldBitmap = glyphDC.SelectBitmap(glyphBitmap);
		glyphDC.FillSolidRect(0, 0, nW, nH, background);
		glyphDC.SetBkMode(TRANSPARENT);
		glyphDC.SetTextColor(bEnabled ? m_colors.Text : m_colors.DimText);
		HFONT hOldFont = glyphDC.SelectFont(m_hIconFont);
		CRect glyphRect(0, 0, nW, nH);
		glyphDC.DrawText(glyph, 1, &glyphRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		glyphDC.SelectFont(hOldFont);
		// keep the rounded corners of the hover background, only the inner part is copied
		int nInset = HelpersGUI::ScaleToScreen(4);
		dc.StretchBlt(textRect.right - 1 - nInset, textRect.top + nInset, -(nW - 2 * nInset), nH - 2 * nInset, glyphDC, nInset, nInset, nW - 2 * nInset, nH - 2 * nInset, SRCCOPY);
		glyphDC.SelectBitmap(hOldBitmap);
	}
}

void CWindowChrome::PaintToolbar(CDC& dc) {
	dc.FillSolidRect(&m_toolbarRect, m_colors.Background);
	DrawHorizontalLine(dc, m_toolbarRect.left, m_toolbarRect.right, m_toolbarRect.bottom - max(1, HelpersGUI::ScaleToScreen(1)), m_colors.Border);
	for (int i = 0; i < (int)m_items.size(); i++) {
		PaintItem(dc, m_items[i], i);
	}
}

CString CWindowChrome::GetStatusText() {
	CJPEGImage* pImage = m_pMainDlg->GetCurrentImage();
	CFileList* pFileList = m_pMainDlg->GetFileList();
	CString sText;
	const TCHAR* GAP = _T("     ");
	if (pImage != NULL && pImage->IsClipboardImage()) {
		sText = CNLS::GetString(_T("Clipboard image"));
	} else if (pFileList != NULL && pFileList->Current() != NULL) {
		CString sIndex;
		sIndex.Format(_T("[%d/%d]  "), pFileList->CurrentIndex() + 1, pFileList->Size());
		sText = sIndex + m_pMainDlg->CurrentFileName(true);
	}
	if (pImage == NULL) {
		return sText;
	}
	CString sPart;
	sPart.Format(_T("%d \x00D7 %d"), pImage->OrigWidth(), pImage->OrigHeight());
	sText += GAP + sPart;
	if (!pImage->IsClipboardImage() && pFileList != NULL && pFileList->Current() != NULL) {
		WIN32_FILE_ATTRIBUTE_DATA fileData;
		if (::GetFileAttributesEx(pFileList->Current(), GetFileExInfoStandard, &fileData)) {
			__int64 nSize = ((__int64)fileData.nFileSizeHigh << 32) | fileData.nFileSizeLow;
			sText += GAP + FormatFileSize(nSize);
		}
	}
	double dZoom = m_pMainDlg->GetZoom();
	if (dZoom > 0.0) {
		sPart.Format(_T("%d %%"), (int)(dZoom * 100 + 0.5));
		sText += GAP + sPart;
	}
	LPCTSTR sFormat = GetImageFormatName(pImage->GetImageFormat());
	if (sFormat[0] != 0) {
		sText += GAP;
		sText += sFormat;
	}
	return sText;
}

void CWindowChrome::PaintStatusBar(CDC& dc) {
	dc.FillSolidRect(&m_statusBarRect, m_colors.Background);
	DrawHorizontalLine(dc, m_statusBarRect.left, m_statusBarRect.right, m_statusBarRect.top, m_colors.Border);
	HFONT hOldFont = dc.SelectFont(m_hFont);
	int nPadding = HelpersGUI::ScaleToScreen(10);
	CRect textRect(m_statusBarRect.left + nPadding, m_statusBarRect.top, m_statusBarRect.right - nPadding, m_statusBarRect.bottom);

	// the right part shows the temporary message or the tooltip of the hovered toolbar button
	CString sRight = m_sMessage;
	if (sRight.IsEmpty() && m_nHoverItem >= 0 && m_items[m_nHoverItem].Tooltip != NULL) {
		sRight = CNLS::GetString(m_items[m_nHoverItem].Tooltip);
	}
	if (!sRight.IsEmpty()) {
		CRect measureRect(0, 0, 0, 0);
		dc.DrawText(sRight, -1, &measureRect, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
		int nRightWidth = min(measureRect.Width(), textRect.Width() / 2);
		CRect rightRect(textRect.right - nRightWidth, textRect.top, textRect.right, textRect.bottom);
		dc.SetTextColor(m_sMessage.IsEmpty() ? m_colors.DimText : m_colors.Accent);
		dc.DrawText(sRight, -1, &rightRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
		textRect.right -= nRightWidth + nPadding;
	}

	dc.SetTextColor(m_colors.Text);
	CString sStatus = GetStatusText();
	dc.DrawText(sStatus, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
	dc.SelectFont(hOldFont);
}

void CWindowChrome::PaintSidePanel(CDC& dc) {
	const CRect& rect = m_sidePanelRect;
	dc.FillSolidRect(&rect, m_colors.PanelBackground);
	dc.FillSolidRect(rect.left, rect.top, max(1, HelpersGUI::ScaleToScreen(1)), rect.Height(), m_colors.Border);

	int nMaxScroll = max(0, m_nSideContentHeight - rect.Height());
	m_nSideScroll = min(m_nSideScroll, nMaxScroll);

	// the content is painted relative to the top, left corner of the panel, shifted by the scroll position
	CPoint oldOrigin;
	dc.GetViewportOrg(&oldOrigin);
	dc.SetViewportOrg(oldOrigin.x + rect.left, oldOrigin.y + rect.top - m_nSideScroll);
	HRGN hClip = ::CreateRectRgn(0, 0, rect.Width(), rect.Height());
	::OffsetRgn(hClip, oldOrigin.x + rect.left, oldOrigin.y + rect.top);
	::SelectClipRgn(dc, hClip);
	m_nSideContentHeight = PaintSidePanelContent(dc, rect.Width());
	::SelectClipRgn(dc, NULL);
	::DeleteObject(hClip);
	dc.SetViewportOrg(oldOrigin);

	// scroll indicator
	if (m_nSideContentHeight > rect.Height()) {
		int nBarHeight = max(HelpersGUI::ScaleToScreen(24), rect.Height() * rect.Height() / m_nSideContentHeight);
		int nBarTop = rect.top + (rect.Height() - nBarHeight) * m_nSideScroll / max(1, m_nSideContentHeight - rect.Height());
		int nBarWidth = HelpersGUI::ScaleToScreen(4);
		FillRoundRect(dc, CRect(rect.right - nBarWidth - HelpersGUI::ScaleToScreen(3), nBarTop, rect.right - HelpersGUI::ScaleToScreen(3), nBarTop + nBarHeight),
			m_colors.Border, nBarWidth);
	}
}

int CWindowChrome::PaintSidePanelContent(CDC& dc, int nWidth) {
	int nPad = HelpersGUI::ScaleToScreen(SIDE_PADDING);
	int nGap = HelpersGUI::ScaleToScreen(6);
	int nInnerWidth = nWidth - 2 * nPad;
	int nY = nPad;
	m_sLocationURL.Empty();
	m_locationRect = CRect(0, 0, 0, 0);

	HFONT hOldFont = dc.SelectFont(m_hBoldFont);
	dc.SetTextColor(m_colors.DimText);
	CRect headerRect(nPad, nY, nPad + nInnerWidth, nY + HelpersGUI::ScaleToScreen(20));
	dc.DrawText(CNLS::GetString(_T("Information")), -1, &headerRect, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
	nY = headerRect.bottom + nGap;

	CJPEGImage* pImage = m_pMainDlg->GetCurrentImage();
	if (pImage == NULL || m_pMainDlg->GetEXIFDisplayCtl() == NULL) {
		dc.SelectFont(m_hFont);
		CRect textRect(nPad, nY, nPad + nInnerWidth, nY + HelpersGUI::ScaleToScreen(40));
		dc.DrawText(CNLS::GetString(_T("No image loaded!")), -1, &textRect, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
		dc.SelectFont(hOldFont);
		return textRect.bottom + nPad;
	}

	CEXIFDisplayCtl* pEXIFCtl = m_pMainDlg->GetEXIFDisplayCtl();
	pEXIFCtl->FillEXIFDataDisplay();
	CEXIFDisplay* pEXIF = pEXIFCtl->GetEXIFDisplay();

	// file name (word wrapped) and index in folder
	LPCTSTR sTitle = (pEXIF->GetTitle() != NULL && pEXIF->GetTitle()[0] != 0) ? pEXIF->GetTitle() : pEXIF->GetPrefix();
	if (sTitle != NULL) {
		dc.SetTextColor(m_colors.Text);
		CRect titleRect(nPad, nY, nPad + nInnerWidth, nY);
		dc.DrawText(sTitle, -1, &titleRect, DT_CALCRECT | DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
		titleRect.right = nPad + nInnerWidth;
		dc.DrawText(sTitle, -1, &titleRect, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
		nY = titleRect.bottom + HelpersGUI::ScaleToScreen(2);
	}
	dc.SelectFont(m_hFont);
	if (pEXIF->GetPrefix() != NULL && pEXIF->GetPrefix()[0] != 0 && sTitle != pEXIF->GetPrefix()) {
		dc.SetTextColor(m_colors.DimText);
		CRect prefixRect(nPad, nY, nPad + nInnerWidth, nY + HelpersGUI::ScaleToScreen(18));
		dc.DrawText(pEXIF->GetPrefix(), -1, &prefixRect, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
		nY = prefixRect.bottom;
	}
	nY += nGap;
	DrawHorizontalLine(dc, nPad, nPad + nInnerWidth, nY, m_colors.Border);
	nY += nGap + HelpersGUI::ScaleToScreen(2);

	// description / value lines
	int nDescWidth = nInnerWidth * 45 / 100;
	int nValueX = nPad + nDescWidth + nGap;
	int nValueWidth = nPad + nInnerWidth - nValueX;
	CString sLocationURL = pEXIF->GetLocationURL();
	for (const CEXIFDisplay::TextLine& line : pEXIF->GetLines()) {
		CRect descRect(nPad, nY, nPad + nDescWidth, nY);
		dc.DrawText(line.Desc, -1, &descRect, DT_CALCRECT | DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
		descRect.right = nPad + nDescWidth;
		CRect valueRect(nValueX, nY, nValueX + nValueWidth, nY);
		dc.DrawText(line.Value, -1, &valueRect, DT_CALCRECT | DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
		valueRect.right = nValueX + nValueWidth;

		dc.SetTextColor(m_colors.DimText);
		dc.DrawText(line.Desc, -1, &descRect, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
		bool bIsLink = line.ValueIsURL && !sLocationURL.IsEmpty();
		dc.SetTextColor(bIsLink ? m_colors.Accent : m_colors.Text);
		dc.DrawText(line.Value, -1, &valueRect, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
		if (bIsLink) {
			m_sLocationURL = sLocationURL;
			m_locationRect = valueRect;
			m_locationRect.OffsetRect(m_sidePanelRect.left, m_sidePanelRect.top - m_nSideScroll);
		}
		nY = max(descRect.bottom, valueRect.bottom) + HelpersGUI::ScaleToScreen(4);
	}

	// histogram
	const CHistogram* pHistogram = pImage->GetProcessedHistogram();
	if (pHistogram != NULL) {
		nY += nGap;
		dc.SelectFont(m_hBoldFont);
		dc.SetTextColor(m_colors.DimText);
		CRect histHeaderRect(nPad, nY, nPad + nInnerWidth, nY + HelpersGUI::ScaleToScreen(20));
		dc.DrawText(CNLS::GetString(_T("Histogram")), -1, &histHeaderRect, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
		dc.SelectFont(m_hFont);
		nY = histHeaderRect.bottom + nGap;
		CRect histRect(nPad, nY, nPad + nInnerWidth, nY + HelpersGUI::ScaleToScreen(HISTOGRAM_HEIGHT));
		PaintHistogram(dc, histRect);
		nY = histRect.bottom + nGap;
	}

	// comment
	if (pEXIF->GetComment() != NULL) {
		nY += nGap;
		dc.SelectFont(m_hBoldFont);
		dc.SetTextColor(m_colors.DimText);
		CRect commentHeaderRect(nPad, nY, nPad + nInnerWidth, nY + HelpersGUI::ScaleToScreen(20));
		dc.DrawText(CNLS::GetString(_T("Comment")), -1, &commentHeaderRect, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
		dc.SelectFont(m_hFont);
		nY = commentHeaderRect.bottom + nGap;
		dc.SetTextColor(m_colors.Text);
		CRect commentRect(nPad, nY, nPad + nInnerWidth, nY);
		dc.DrawText(pEXIF->GetComment(), -1, &commentRect, DT_CALCRECT | DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
		commentRect.right = nPad + nInnerWidth;
		dc.DrawText(pEXIF->GetComment(), -1, &commentRect, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
		nY = commentRect.bottom;
	}

	dc.SelectFont(hOldFont);
	return nY + nPad;
}

void CWindowChrome::PaintHistogram(CDC& dc, const CRect& rect) {
	const CHistogram* pHistogram = m_pMainDlg->GetCurrentImage()->GetProcessedHistogram();
	dc.FillSolidRect(&rect, m_colors.Background);
	if (pHistogram == NULL || rect.Width() < 2 || rect.Height() < 2) {
		return;
	}
	const int* channels[4] = { pHistogram->GetChannelGrey(), pHistogram->GetChannelR(), pHistogram->GetChannelG(), pHistogram->GetChannelB() };
	// scale to the highest value, the extreme bins 0 and 255 often contain clipped pixels and are not used for scaling
	int nMax = 1;
	for (int c = 0; c < 4; c++) {
		for (int i = 1; i < 255; i++) {
			nMax = max(nMax, channels[c][i]);
		}
	}
	int nW = rect.Width(), nH = rect.Height();

	// luminance as filled area
	COLORREF greyColor = CSettingsProvider::This().ChromeDarkTheme() ? RGB(110, 110, 110) : RGB(180, 180, 180);
	for (int x = 0; x < nW; x++) {
		int nBin = x * 256 / nW;
		int nValue = min(nH, (int)((__int64)channels[0][nBin] * nH / nMax));
		if (nValue > 0) {
			dc.FillSolidRect(rect.left + x, rect.bottom - nValue, 1, nValue, greyColor);
		}
	}

	// color channels as lines
	const COLORREF channelColors[3] = { RGB(235, 80, 80), RGB(80, 200, 100), RGB(90, 140, 255) };
	for (int c = 1; c < 4; c++) {
		CPen pen;
		pen.CreatePen(PS_SOLID, max(1, HelpersGUI::ScaleToScreen(1)), channelColors[c - 1]);
		HPEN hOldPen = dc.SelectPen(pen);
		for (int x = 0; x < nW; x++) {
			int nBin = x * 256 / nW;
			int nValue = min(nH - 1, (int)((__int64)channels[c][nBin] * (nH - 1) / nMax));
			if (x == 0) {
				dc.MoveTo(rect.left, rect.bottom - 1 - nValue);
			} else {
				dc.LineTo(rect.left + x, rect.bottom - 1 - nValue);
			}
		}
		dc.SelectPen(hOldPen);
	}
}
