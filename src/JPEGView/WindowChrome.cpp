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
#include "KeyMap.h"
#include "HelpersGUI.h"
#include "Helpers.h"
#include "NLS.h"
#include "Converter.h"
#include "TimerEventIDs.h"
#include "Annotations.h"
#include "Share.h"
#include <shellapi.h>
#include <math.h>
#include <map>
#include <process.h>

// Sizes in pixels at 96 DPI
static const int MENUBAR_HEIGHT = 28; // first row: menu titles
static const int TOOLBAR_HEIGHT = 36; // second row: buttons
static const int STATUSBAR_HEIGHT = 24;
static const int ITEM_MARGIN = 4; // vertical margin of toolbar items
static const int BUTTON_WIDTH = 34;
static const int MENU_TITLE_PADDING = 10;
static const int SEPARATOR_WIDTH = 11;
static const int SIDE_PADDING = 14;
static const int SIDE_HEADER_HEIGHT = 38;
static const int SIDE_COLLAPSED_WIDTH = 32;
static const int SIDE_BUTTON_HEIGHT = 26;
static const int HISTOGRAM_HEIGHT = 96;
static const int STATUS_MESSAGE_TIMEOUT = 6000; // ms
static const int TOOLTIP_DELAY = 450; // ms
static const int OVERFLOW_COMMAND = -1; // toolbar button showing the buttons that do not fit into the toolbar

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

// Text that is only wrapped at the given separator: the GPS location 'lat / lon' wraps between the coordinates, not inside them
static CString KeepTogether(LPCTSTR sText, LPCTSTR sBreakAt) {
	CString s(sText);
	s.Replace(sBreakAt, _T("\n"));
	s.Replace(_T(' '), (TCHAR)0x00A0);
	return s;
}

static int S(int nValue) { return HelpersGUI::ScaleToScreen(nValue); }

static const int FILMSTRIP_HEIGHT = 76;
static const int THUMBNAIL_SIZE = 96; // requested from the thumbnail cache, scaled down for painting

// Thumbnails of the Windows thumbnail cache, loaded in a background thread. A loaded thumbnail repaints the window frame.
class CThumbnailCache {
public:
	CThumbnailCache(HWND hWnd) {
		m_hWnd = hWnd;
		m_bStop = false;
		::InitializeCriticalSection(&m_lock);
		m_hEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
		m_hThread = (HANDLE)_beginthreadex(NULL, 0, ThreadProc, this, 0, NULL);
	}
	~CThumbnailCache() {
		m_bStop = true;
		::SetEvent(m_hEvent);
		::WaitForSingleObject(m_hThread, 5000);
		::CloseHandle(m_hThread);
		::CloseHandle(m_hEvent);
		for (auto& entry : m_cache) {
			if (entry.second != NULL) ::DeleteObject(entry.second);
		}
		::DeleteCriticalSection(&m_lock);
	}
	// Returns the thumbnail if loaded, NULL otherwise. Loading is requested for the files given in the order of the list.
	HBITMAP Get(const CString& sFile, bool& bLoaded) {
		::EnterCriticalSection(&m_lock);
		auto iter = m_cache.find(sFile);
		bLoaded = iter != m_cache.end();
		HBITMAP hBitmap = bLoaded ? iter->second : NULL;
		::LeaveCriticalSection(&m_lock);
		return hBitmap;
	}
	void Request(const std::vector<CString>& files) {
		::EnterCriticalSection(&m_lock);
		m_queue = files; // only the currently visible files are loaded
		::LeaveCriticalSection(&m_lock);
		::SetEvent(m_hEvent);
	}
private:
	HWND m_hWnd;
	volatile bool m_bStop;
	CRITICAL_SECTION m_lock;
	HANDLE m_hEvent, m_hThread;
	std::map<CString, HBITMAP> m_cache;
	std::vector<CString> m_queue;

	static unsigned __stdcall ThreadProc(void* pParam) {
		CThumbnailCache* pThis = (CThumbnailCache*)pParam;
		::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
		while (!pThis->m_bStop) {
			::WaitForSingleObject(pThis->m_hEvent, INFINITE);
			for (;;) {
				CString sFile;
				::EnterCriticalSection(&pThis->m_lock);
				while (!pThis->m_queue.empty() && sFile.IsEmpty()) {
					CString sCandidate = pThis->m_queue.front();
					pThis->m_queue.erase(pThis->m_queue.begin());
					if (pThis->m_cache.find(sCandidate) == pThis->m_cache.end()) sFile = sCandidate;
				}
				::LeaveCriticalSection(&pThis->m_lock);
				if (sFile.IsEmpty() || pThis->m_bStop) break;
				HBITMAP hBitmap = Share::GetThumbnail(sFile, S(THUMBNAIL_SIZE));
				::EnterCriticalSection(&pThis->m_lock);
				if (pThis->m_cache.size() > 400) {
					for (auto& entry : pThis->m_cache) {
						if (entry.second != NULL) ::DeleteObject(entry.second);
					}
					pThis->m_cache.clear();
				}
				pThis->m_cache[sFile] = hBitmap;
				::LeaveCriticalSection(&pThis->m_lock);
				::RedrawWindow(pThis->m_hWnd, NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_NOERASE | RDW_NOINTERNALPAINT);
			}
		}
		::CoUninitialize();
		return 0;
	}
};

//////////////////////////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////////////////////////

CWindowChrome::CWindowChrome(CMainDlg* pMainDlg) {
	m_pMainDlg = pMainDlg;
	CSettingsProvider& sp = CSettingsProvider::This();
	m_bShowToolbar = sp.ShowToolbar();
	m_bShowStatusBar = sp.ShowStatusBar();
	m_bShowSidePanel = sp.ShowSidePanel();
	m_bSideCollapsed = sp.SidePanelCollapsed();
	m_eSideTab = TAB_INFO;
	m_nHoverItem = m_nPressedItem = m_nHoverSide = -1;
	m_bTrackingMouse = false;
	m_nSideScroll = 0;
	m_nSideContentHeight = 0;
	m_nDragSlider = -1;
	m_hitOrigin = CPoint(0, 0);
	m_bHoverFileDrag = false;
	m_bShowFilmstrip = sp.ShowFilmstrip();
	m_pThumbnails = new CThumbnailCache(pMainDlg->GetHWND());

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

	m_hFont = CreateUIFont(_T("Segoe UI"), S(12), FW_NORMAL);
	m_hBoldFont = CreateUIFont(_T("Segoe UI"), S(13), FW_SEMIBOLD);
	m_bHasIconFont = true;
	LPCTSTR sIconFont = FontExists(_T("Segoe Fluent Icons")) ? _T("Segoe Fluent Icons") : FontExists(_T("Segoe MDL2 Assets")) ? _T("Segoe MDL2 Assets") : NULL;
	if (sIconFont != NULL) {
		m_hIconFont = CreateUIFont(sIconFont, S(16), FW_NORMAL);
		m_hSmallIconFont = CreateUIFont(sIconFont, S(12), FW_NORMAL);
	} else {
		m_hIconFont = m_hSmallIconFont = NULL;
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
		{ 0xE72D, _T("Share"), IDM_SHARE, _T("Share..."), false, true },
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
		{ 0xE70F, _T("Edit"), IDM_TOGGLE_EDITPANEL, _T("Edit image (brightness, contrast, colors)"), false, true },
		{ 0xE76D, _T("Draw"), IDM_TOGGLE_DRAWPANEL, _T("Draw and write on the image"), false, true },
		{ 0, _T("A|B"), IDM_COMPARE_BEFORE_AFTER, _T("Compare before and after editing"), false, true },
		{ 0, _T(""), 0, NULL, false, false },
		{ 0xE74D, _T("Del"), IDM_MOVE_TO_RECYCLE_BIN_CONFIRM, _T("Move to recycle bin"), false, true },
		{ 0, _T(""), 0, NULL, false, false },
		{ 0xE946, _T("Info"), IDM_TOGGLE_SIDEPANEL, _T("Information panel"), false, false },
		{ 0xE740, _T("Full"), IDM_FULL_SCREEN_MODE, _T("Full screen mode"), false, false },
	};
	for (const ButtonDef& def : buttons) {
		EItemType eType = (def.Command == 0) ? IT_Separator : IT_Button;
		CToolbarItem item = { eType, def.Text, def.Glyph, def.Command, def.Tooltip, def.Mirror, def.NeedsImage, CRect() };
		m_items.push_back(item);
	}
	CToolbarItem overflowItem = { IT_Button, _T("..."), 0xE712, OVERFLOW_COMMAND, _T("More"), false, false, CRect() };
	m_items.push_back(overflowItem);

	// Sliders of the edit tab, same ranges as the image processing panel
	const CSideSlider sliders[] = {
		{ _T("Exposure"), &CImageProcessingParams::Exposure, -2.0, 2.0, 0.0, false, false, 0 },
		{ _T("Brightness"), &CImageProcessingParams::Gamma, 0.5, 2.0, 1.0, true, true, 0 },
		{ _T("Contrast"), &CImageProcessingParams::Contrast, -0.5, 0.5, 0.0, false, false, 0 },
		{ _T("Saturation"), &CImageProcessingParams::Saturation, 0.0, 2.0, 1.0, false, false, 0 },
		{ _T("Sharpen"), &CImageProcessingParams::Sharpen, 0.0, 0.5, 0.0, false, false, 0 },
		{ _T("Vignette"), &CImageProcessingParams::Vignette, -1.0, 1.0, 0.0, false, false, 0 },
		{ _T("Cyan-Red"), &CImageProcessingParams::CyanRed, -1.0, 1.0, 0.0, false, false, 0 },
		// as in other editors: temperature to the right is warmer (yellow), tint to the right is magenta
		{ _T("Temperature"), &CImageProcessingParams::YellowBlue, -1.0, 1.0, 0.0, false, true, 0 },
		{ _T("Tint"), &CImageProcessingParams::MagentaGreen, -1.0, 1.0, 0.0, false, true, 0 },
		{ _T("Lighten Shadows"), &CImageProcessingParams::LightenShadows, 0.0, 1.0, 0.0, false, false, 1 },
		{ _T("Darken Highlights"), &CImageProcessingParams::DarkenHighlights, 0.0, 1.0, 0.0, false, false, 1 },
		{ _T("Deep Shadows"), &CImageProcessingParams::LightenShadowSteepness, 0.0, 1.0, 0.0, false, false, 1 },
		{ _T("Color Correction"), &CImageProcessingParams::ColorCorrectionFactor, -0.5, 0.5, 0.0, false, false, 2 },
		{ _T("Contrast Correction"), &CImageProcessingParams::ContrastCorrectionFactor, 0.0, 1.0, 0.0, false, false, 2 },
	};
	for (const CSideSlider& slider : sliders) {
		m_sliders.push_back(slider);
	}

	// Tooltip shown in tracking mode for toolbar buttons and side panel elements
	m_hTooltip = ::CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, pMainDlg->GetHWND(), NULL, _Module.GetModuleInstance(), NULL);
	if (m_hTooltip != NULL) {
		TOOLINFO toolInfo{ 0 };
		toolInfo.cbSize = sizeof(toolInfo);
		toolInfo.uFlags = TTF_TRACK | TTF_ABSOLUTE;
		toolInfo.hwnd = pMainDlg->GetHWND();
		toolInfo.uId = 1;
		toolInfo.lpszText = (LPTSTR)_T("");
		::SendMessage(m_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&toolInfo);
		::SendMessage(m_hTooltip, TTM_SETMAXTIPWIDTH, 0, S(400));
	}

	Helpers::SetAdditionalBorderSize(IsEnabled() ? GetExtraSize() : CSize(0, 0));
}

CWindowChrome::~CWindowChrome() {
	delete m_pThumbnails;
	if (m_hTooltip != NULL) {
		::DestroyWindow(m_hTooltip);
	}
	::DeleteObject(m_hFont);
	::DeleteObject(m_hBoldFont);
	if (m_hIconFont != NULL) {
		::DeleteObject(m_hIconFont);
		::DeleteObject(m_hSmallIconFont);
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

void CWindowChrome::SetSidePanelCollapsed(bool bCollapsed) {
	m_bSideCollapsed = bCollapsed;
	m_nSideScroll = 0;
	HideTooltip();
	SaveSettings();
	UpdateFrame();
}

void CWindowChrome::ShowSideTab(ESideTab eTab) {
	m_eSideTab = eTab;
	m_nSideScroll = 0;
	if (!m_bShowSidePanel || m_bSideCollapsed) {
		m_bShowSidePanel = true;
		m_bSideCollapsed = false;
		SaveSettings();
		UpdateFrame();
	} else {
		Paint();
	}
}

CSize CWindowChrome::GetExtraSize() {
	return CSize(m_bShowSidePanel ? SidePanelWidth() : 0,
		(m_bShowToolbar ? ToolbarHeight() : 0) + (m_bShowStatusBar ? StatusBarHeight() : 0));
}

void CWindowChrome::SyncBorderSize() {
	Helpers::SetAdditionalBorderSize(IsEnabled() ? GetExtraSize() : CSize(0, 0));
	// the mouse leaves the toolbar without notification when the window mode changes
	m_nHoverItem = m_nPressedItem = m_nHoverSide = -1;
	m_bTrackingMouse = false;
	HideTooltip();
}

void CWindowChrome::UpdateFrame() {
	SyncBorderSize();
	::SetWindowPos(m_pMainDlg->GetHWND(), NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	::InvalidateRect(m_pMainDlg->GetHWND(), NULL, FALSE);
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
	int nSidePanel = IsSidePanelShown() ? min(SidePanelWidth(), max(0, (int)(pClientRect->right - pClientRect->left) - S(120))) : 0;
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
		} else if (m_bSideCollapsed) {
			PaintCollapsedSidePanel(memDC);
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
		return (IsInFileDragArea(pt) || GetFilmstripRect().PtInRect(pt)) ? HT_CHROME : HTCAPTION;
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
	int nSide = (nItem < 0 && m_sidePanelRect.PtInRect(pt)) ? FindSideHit(pt) : -1;
	bool bFileDrag = IsInFileDragArea(pt);
	if (bFileDrag != m_bHoverFileDrag) {
		m_bHoverFileDrag = bFileDrag;
		if (bFileDrag) {
			CRect dragRect(m_statusBarRect.left, m_statusBarRect.top - S(30), m_statusBarRect.left + m_statusBarRect.Width() * 2 / 5, m_statusBarRect.top - S(30));
			ScheduleTooltip(CNLS::GetString(_T("Drag the file name to copy the file to other applications")), dragRect);
		} else {
			HideTooltip();
		}
	}
	if (nItem != m_nHoverItem || nSide != m_nHoverSide) {
		m_nHoverItem = nItem;
		m_nHoverSide = nSide;
		if (nItem >= 0 && m_items[nItem].Tooltip != NULL) {
			ScheduleTooltip(GetTooltipText(m_items[nItem].Tooltip, m_items[nItem].Type == IT_Button ? m_items[nItem].Command : 0), m_items[nItem].Rect);
		} else if (nSide >= 0 && m_sideHits[nSide].Tooltip != NULL) {
			ScheduleTooltip(CNLS::GetString(m_sideHits[nSide].Tooltip), m_sideHits[nSide].Rect);
		} else {
			HideTooltip();
		}
		Paint();
	}
}

bool CWindowChrome::IsInFileDragArea(CPoint ptWindow) {
	CJPEGImage* pImage = m_pMainDlg->GetCurrentImage();
	return m_statusBarRect.PtInRect(ptWindow) && !GetFilmstripRect().PtInRect(ptWindow) && ptWindow.x < m_statusBarRect.left + m_statusBarRect.Width() * 2 / 5 &&
		pImage != NULL && !pImage->IsClipboardImage() && m_pMainDlg->GetFileList() != NULL && m_pMainDlg->GetFileList()->Current() != NULL;
}

void CWindowChrome::OnNCMouseLeave() {
	m_bTrackingMouse = false;
	m_bHoverFileDrag = false;
	HideTooltip();
	if (m_nHoverItem >= 0 || m_nPressedItem >= 0 || m_nHoverSide >= 0) {
		m_nHoverItem = m_nPressedItem = m_nHoverSide = -1;
		Paint();
	}
}

void CWindowChrome::OnNCLButtonDown(CPoint ptScreen, bool bDoubleClick) {
	HideTooltip();
	UpdateLayout();
	CPoint pt = ScreenToWindow(ptScreen);
	if (m_sidePanelRect.PtInRect(pt)) {
		int nHit = FindSideHit(pt);
		if (nHit >= 0) {
			CSideHit hit = m_sideHits[nHit]; // copy, painting rebuilds the list
			ExecuteSideHit(hit, pt, bDoubleClick);
		}
		return;
	}
	if (GetFilmstripRect().PtInRect(pt)) {
		for (const auto& cell : m_filmstripCells) {
			if (cell.first.PtInRect(pt)) {
				CString sFile = cell.second; // the cells are rebuilt when the new image is painted
				m_pMainDlg->ShowFile(sFile);
				break;
			}
		}
		return;
	}
	if (IsInFileDragArea(pt)) {
		m_bHoverFileDrag = false;
		Share::DragFile(m_pMainDlg->GetHWND(), m_pMainDlg->GetFileList()->Current());
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
	if (!IsSidePanelExpanded()) {
		return false;
	}
	UpdateLayout();
	if (!m_sidePanelRect.PtInRect(ScreenToWindow(ptScreen))) {
		return false;
	}
	int nVisibleHeight = m_sidePanelRect.Height() - S(SIDE_HEADER_HEIGHT);
	int nMaxScroll = max(0, m_nSideContentHeight - nVisibleHeight);
	m_nSideScroll = max(0, min(nMaxScroll, m_nSideScroll - nDelta * S(48) / WHEEL_DELTA));
	HideTooltip();
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
	if (nTimerId == TOOLTIP_TIMER_EVENT_ID) {
		::KillTimer(m_pMainDlg->GetHWND(), TOOLTIP_TIMER_EVENT_ID);
		if (m_hTooltip != NULL && !m_sTooltipText.IsEmpty()) {
			TOOLINFO toolInfo{ 0 };
			toolInfo.cbSize = sizeof(toolInfo);
			toolInfo.hwnd = m_pMainDlg->GetHWND();
			toolInfo.uId = 1;
			toolInfo.lpszText = (LPTSTR)(LPCTSTR)m_sTooltipText;
			::SendMessage(m_hTooltip, TTM_UPDATETIPTEXT, 0, (LPARAM)&toolInfo);
			::SendMessage(m_hTooltip, TTM_TRACKPOSITION, 0, MAKELPARAM(m_tooltipPos.x, m_tooltipPos.y));
			::SendMessage(m_hTooltip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&toolInfo);
			// keep the tooltip on the monitor, it is shown at the left edge of the element and can leave the screen at the right
			CRect tooltipRect;
			MONITORINFO monitorInfo{ 0 };
			monitorInfo.cbSize = sizeof(monitorInfo);
			if (::GetWindowRect(m_hTooltip, &tooltipRect) && ::GetMonitorInfo(::MonitorFromPoint(m_tooltipPos, MONITOR_DEFAULTTONEAREST), &monitorInfo) &&
				tooltipRect.right > monitorInfo.rcWork.right) {
				int nX = max((int)monitorInfo.rcWork.left, (int)monitorInfo.rcWork.right - tooltipRect.Width());
				::SendMessage(m_hTooltip, TTM_TRACKPOSITION, 0, MAKELPARAM(nX, m_tooltipPos.y));
			}
		}
		return true;
	}
	return false;
}

void CWindowChrome::OnDragMove(CPoint ptScreen) {
	if (m_nDragSlider >= 0) {
		SetSliderFromPosition(m_nDragSlider, ScreenToWindow(ptScreen).x);
	}
}

void CWindowChrome::EndDrag() {
	m_nDragSlider = -1;
	Paint();
}

void CWindowChrome::ShowMessage(LPCTSTR sMessage) {
	m_sMessage = sMessage;
	::SetTimer(m_pMainDlg->GetHWND(), STATUSBAR_MESSAGE_TIMER_EVENT_ID, STATUS_MESSAGE_TIMEOUT, NULL);
	Paint();
}

int CWindowChrome::TrackConvertMenu() {
	HideTooltip();
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

int CWindowChrome::TrackOverflowMenu(const CRect& rectButton) {
	HMENU hMenu = ::CreatePopupMenu();
	bool bLastWasSeparator = true;
	for (const CToolbarItem& item : m_items) {
		if (!item.Rect.IsRectEmpty() || item.Command == OVERFLOW_COMMAND || item.Type == IT_Menu) {
			continue;
		}
		if (item.Type == IT_Separator) {
			if (!bLastWasSeparator) ::AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
			bLastWasSeparator = true;
			continue;
		}
		CString sText = CNLS::GetString(item.Tooltip);
		CString sKey = (m_pMainDlg->GetKeyMap() != NULL) ? m_pMainDlg->GetKeyMap()->GetKeyStringForCommand(item.Command) : CString();
		if (!sKey.IsEmpty()) sText += _T("\t") + sKey;
		::AppendMenu(hMenu, MF_STRING | (IsItemEnabled(item) ? 0 : MF_GRAYED) | (IsItemChecked(item) ? MF_CHECKED : 0), item.Command, sText);
		bLastWasSeparator = false;
	}
	int nCommand = m_pMainDlg->TrackPopupMenu(CPoint(m_windowRect.left + rectButton.left, m_windowRect.top + rectButton.bottom), hMenu);
	::DestroyMenu(hMenu);
	if (nCommand == IDM_CONVERT_MENU) {
		nCommand = TrackConvertMenu();
	}
	return nCommand;
}

int CWindowChrome::ToolbarHeight() { return S(MENUBAR_HEIGHT) + S(TOOLBAR_HEIGHT); }
int CWindowChrome::StatusBarHeight() { return S(STATUSBAR_HEIGHT) + (m_bShowFilmstrip ? S(FILMSTRIP_HEIGHT) : 0); }

CRect CWindowChrome::GetFilmstripRect() {
	if (!m_bShowFilmstrip || m_statusBarRect.IsRectEmpty()) {
		return CRect(0, 0, 0, 0);
	}
	return CRect(m_statusBarRect.left, m_statusBarRect.top, m_statusBarRect.right, m_statusBarRect.bottom - S(STATUSBAR_HEIGHT));
}

void CWindowChrome::SetShowFilmstrip(bool bShow) {
	m_bShowFilmstrip = bShow;
	CSettingsProvider::This().SaveShowFilmstrip(bShow);
	UpdateFrame();
}
int CWindowChrome::SidePanelWidth() { return m_bSideCollapsed ? S(SIDE_COLLAPSED_WIDTH) : S(CSettingsProvider::This().SidePanelWidth()); }

CPoint CWindowChrome::ScreenToWindow(CPoint ptScreen) {
	return CPoint(ptScreen.x - m_windowRect.left, ptScreen.y - m_windowRect.top);
}

void CWindowChrome::SaveSettings() {
	CSettingsProvider::This().SaveWindowChromeSettings(m_bShowToolbar, m_bShowStatusBar, m_bShowSidePanel, m_bSideCollapsed);
}

CString CWindowChrome::GetTooltipText(LPCTSTR sTooltip, int nCommand) {
	CString sText = CNLS::GetString(sTooltip);
	if (nCommand != 0 && m_pMainDlg->GetKeyMap() != NULL) {
		CString sKey = m_pMainDlg->GetKeyMap()->GetKeyStringForCommand(nCommand);
		if (!sKey.IsEmpty()) {
			sText += _T("  (") + sKey + _T(")");
		}
	}
	return sText;
}

void CWindowChrome::ScheduleTooltip(LPCTSTR sText, CRect rectWindow) {
	HideTooltip();
	m_sTooltipText = sText;
	m_tooltipPos = CPoint(m_windowRect.left + rectWindow.left, m_windowRect.top + rectWindow.bottom + S(4));
	::SetTimer(m_pMainDlg->GetHWND(), TOOLTIP_TIMER_EVENT_ID, TOOLTIP_DELAY, NULL);
}

void CWindowChrome::HideTooltip() {
	::KillTimer(m_pMainDlg->GetHWND(), TOOLTIP_TIMER_EVENT_ID);
	m_sTooltipText.Empty();
	if (m_hTooltip != NULL) {
		TOOLINFO toolInfo{ 0 };
		toolInfo.cbSize = sizeof(toolInfo);
		toolInfo.hwnd = m_pMainDlg->GetHWND();
		toolInfo.uId = 1;
		::SendMessage(m_hTooltip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&toolInfo);
	}
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
	// the menu titles are in the first row, the buttons in the second row below
	int nMenuTop = m_toolbarRect.top + S(2);
	int nMenuBottom = m_toolbarRect.top + S(MENUBAR_HEIGHT) - S(2);
	int nTop = m_toolbarRect.top + S(MENUBAR_HEIGHT) + S(ITEM_MARGIN);
	int nBottom = m_toolbarRect.bottom - S(ITEM_MARGIN);
	int nLast = (int)m_items.size() - 1; // the overflow button
	// first without the overflow button, if not all items fit the overflow button gets the space at the right
	for (int nPass = 0; nPass < 2; nPass++) {
		int nRight = m_toolbarRect.right - S(4) - (nPass == 1 ? S(BUTTON_WIDTH) : 0);
		bool bAllFit = true;
		int nMenuX = m_toolbarRect.left + S(6);
		int nX = m_toolbarRect.left + S(6);
		bool bRowStart = true;
		for (int i = 0; i < nLast; i++) {
			CToolbarItem& item = m_items[i];
			if (item.Type == IT_Menu) {
				SIZE textSize;
				::GetTextExtentPoint32(hDC, item.Text, item.Text.GetLength(), &textSize);
				int nMenuWidth = (int)textSize.cx + 2 * S(MENU_TITLE_PADDING);
				item.Rect = (m_toolbarRect.IsRectEmpty() || nMenuX + nMenuWidth > m_toolbarRect.right - S(4)) ? CRect(0, 0, 0, 0) :
					CRect(nMenuX, nMenuTop, nMenuX + nMenuWidth, nMenuBottom);
				nMenuX += nMenuWidth;
				continue;
			}
			if (item.Type == IT_Separator && bRowStart) {
				item.Rect = CRect(0, 0, 0, 0); // no separator at the start of the button row
				continue;
			}
			bRowStart = false;
			int nWidth;
			if (item.Type == IT_Separator) {
				nWidth = S(SEPARATOR_WIDTH);
			} else if (item.Type == IT_Menu || item.Glyph == 0 || !m_bHasIconFont) {
				SIZE textSize;
				::GetTextExtentPoint32(hDC, item.Text, item.Text.GetLength(), &textSize);
				nWidth = max(S(BUTTON_WIDTH), (int)textSize.cx + 2 * S(MENU_TITLE_PADDING));
			} else {
				nWidth = S(BUTTON_WIDTH);
			}
			if (m_toolbarRect.IsRectEmpty() || nX + nWidth > nRight) {
				item.Rect = CRect(0, 0, 0, 0);
				bAllFit &= item.Type == IT_Separator;
			} else {
				item.Rect = CRect(nX, nTop, nX + nWidth, nBottom);
			}
			nX += nWidth;
		}
		m_items[nLast].Rect = CRect(0, 0, 0, 0);
		if (bAllFit || m_toolbarRect.IsRectEmpty()) {
			break;
		}
		if (nPass == 1) {
			m_items[nLast].Rect = CRect(nRight, nTop, nRight + S(BUTTON_WIDTH), nBottom);
		}
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

int CWindowChrome::FindSideHit(CPoint ptWindow) {
	for (int i = 0; i < (int)m_sideHits.size(); i++) {
		if (m_sideHits[i].Rect.PtInRect(ptWindow)) {
			return i;
		}
	}
	return -1;
}

void CWindowChrome::AddSideHit(ESideHitType eType, int nValue, const CRect& rectPanel, LPCTSTR sTooltip) {
	CSideHit hit = { eType, nValue, rectPanel, sTooltip };
	hit.Rect.OffsetRect(m_hitOrigin);
	// elements scrolled out of the content area of the panel can not be clicked
	CRect visibleRect = m_sidePanelRect;
	if (eType != SH_Collapse && eType != SH_Expand && eType != SH_Tab && !m_bSideCollapsed) {
		visibleRect.top += S(SIDE_HEADER_HEIGHT);
	}
	hit.Rect.IntersectRect(&hit.Rect, &visibleRect);
	if (!hit.Rect.IsRectEmpty()) {
		m_sideHits.push_back(hit);
	}
}

bool CWindowChrome::IsItemEnabled(const CToolbarItem& item) {
	if (!item.NeedsImage) {
		return true;
	}
	if (m_pMainDlg->GetCurrentImage() == NULL) {
		return false;
	}
	if (item.Command == IDM_CONVERT_MENU || item.Command == IDM_SHARE || item.Command == IDM_MOVE_TO_RECYCLE_BIN_CONFIRM) {
		return !m_pMainDlg->GetCurrentImage()->IsClipboardImage();
	}
	return true;
}

bool CWindowChrome::IsItemChecked(const CToolbarItem& item) {
	switch (item.Command) {
		case IDM_TOGGLE_SIDEPANEL: return m_bShowSidePanel;
		case IDM_TOGGLE_EDITPANEL: return IsEditTabShown();
		case IDM_TOGGLE_DRAWPANEL: return IsDrawTabShown();
		case IDM_COMPARE_BEFORE_AFTER: return m_pMainDlg->IsCompareMode();
	}
	return false;
}

double CWindowChrome::GetSliderDefault(const CSideSlider& slider) {
	// the default values of the INI file, e.g. Sharpen=0.3, not the neutral value of the parameter
	CImageProcessingParams defaults = m_pMainDlg->GetDefaultImageProcessingParams();
	return defaults.*slider.Value;
}

bool CWindowChrome::IsSliderGroupEnabled(int nGroup) {
	if (nGroup == 1) return m_pMainDlg->IsLDC();
	if (nGroup == 2) return m_pMainDlg->IsAutoContrast();
	return true;
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
	} else if (item.Command == OVERFLOW_COMMAND) {
		nCommand = TrackOverflowMenu(item.Rect);
	} else {
		nCommand = item.Command;
	}
	if (nCommand != 0) {
		m_pMainDlg->ExecuteCommand(nCommand);
	}
}

void CWindowChrome::ExecuteSideHit(const CSideHit& hit, CPoint ptWindow, bool bDoubleClick) {
	// the expand button of the collapsed panel is where the collapse button was, a double click must not toggle twice
	if (bDoubleClick && (hit.Type == SH_Collapse || hit.Type == SH_Expand)) {
		return;
	}
	switch (hit.Type) {
		case SH_Collapse:
			SetSidePanelCollapsed(true);
			break;
		case SH_Expand:
			SetSidePanelCollapsed(false);
			break;
		case SH_Tab:
			ShowSideTab((ESideTab)hit.Value);
			break;
		case SH_Location:
			if (!m_sLocationURL.IsEmpty()) {
				::ShellExecute(m_pMainDlg->GetHWND(), _T("open"), m_sLocationURL, NULL, NULL, SW_SHOWNORMAL);
			}
			break;
		case SH_Slider:
			if (m_pMainDlg->GetCurrentImage() == NULL || !IsSliderGroupEnabled(m_sliders[hit.Value].Group)) {
				break;
			}
			if (bDoubleClick) {
				// double click resets the value
				const CSideSlider& slider = m_sliders[hit.Value];
				m_pMainDlg->GetImageProcessingParams()->*slider.Value = GetSliderDefault(slider);
				m_pMainDlg->OnImageProcessingChanged();
				Paint();
			} else {
				m_nDragSlider = hit.Value;
				::SetCapture(m_pMainDlg->GetHWND());
				SetSliderFromPosition(hit.Value, ptWindow.x);
			}
			break;
		case SH_GroupCheck:
			if (m_pMainDlg->GetCurrentImage() != NULL) {
				m_pMainDlg->ExecuteCommand(hit.Value == 1 ? IDM_LDC : IDM_AUTO_CORRECTION);
				Paint();
			}
			break;
		case SH_Button:
			m_pMainDlg->ExecuteCommand(hit.Value);
			Paint();
			break;
		case SH_Rating:
			// clicking the current rating removes it
			m_pMainDlg->SetRating(m_pMainDlg->GetRating() == hit.Value ? 0 : hit.Value);
			break;
		case SH_Tool:
			m_pMainDlg->GetAnnotations()->SetTool((CAnnotations::ETool)hit.Value);
			Paint();
			break;
		case SH_Color:
			m_pMainDlg->GetAnnotations()->SetColor((COLORREF)hit.Value);
			Paint();
			break;
		case SH_Width:
			m_pMainDlg->GetAnnotations()->SetWidth(hit.Value);
			Paint();
			break;
	}
}

double CWindowChrome::SliderToPosition(const CSideSlider& slider, double dValue) {
	double dMin = slider.Logarithmic ? log10(slider.Min) : slider.Min;
	double dMax = slider.Logarithmic ? log10(slider.Max) : slider.Max;
	double dX = (slider.Logarithmic ? log10(max(1e-6, dValue)) : dValue) * (slider.Inverted ? -1 : 1);
	return max(0.0, min(1.0, (dX - dMin) / (dMax - dMin)));
}

double CWindowChrome::PositionToSlider(const CSideSlider& slider, double dPosition) {
	double dMin = slider.Logarithmic ? log10(slider.Min) : slider.Min;
	double dMax = slider.Logarithmic ? log10(slider.Max) : slider.Max;
	double dX = (dMin + dPosition * (dMax - dMin)) * (slider.Inverted ? -1 : 1);
	return slider.Logarithmic ? pow(10.0, dX) : dX;
}

void CWindowChrome::SetSliderFromPosition(int nSlider, int nXWindow) {
	for (const CSideHit& hit : m_sideHits) {
		if (hit.Type == SH_Slider && hit.Value == nSlider) {
			// the hit rectangle has the knob radius as margin on both sides
			int nMargin = S(7);
			double dPosition = (double)(nXWindow - hit.Rect.left - nMargin) / max(1, hit.Rect.Width() - 2 * nMargin);
			dPosition = max(0.0, min(1.0, dPosition));
			const CSideSlider& slider = m_sliders[nSlider];
			double dValue = PositionToSlider(slider, dPosition);
			// snap to the default value close to it
			if (fabs(SliderToPosition(slider, GetSliderDefault(slider)) - dPosition) < 0.015) {
				dValue = GetSliderDefault(slider);
			}
			m_pMainDlg->GetImageProcessingParams()->*slider.Value = dValue;
			m_pMainDlg->OnImageProcessingChanged();
			Paint();
			return;
		}
	}
}

void CWindowChrome::PaintItem(CDC& dc, const CToolbarItem& item, int nIndex) {
	if (item.Rect.IsRectEmpty()) {
		return;
	}
	if (item.Type == IT_Separator) {
		int nX = (item.Rect.left + item.Rect.right) / 2;
		dc.FillSolidRect(nX, item.Rect.top + S(5), max(1, S(1)), item.Rect.Height() - S(10), m_colors.Border);
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
		FillRoundRect(dc, item.Rect, background, S(8));
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
		int nInset = S(4);
		dc.StretchBlt(textRect.right - 1 - nInset, textRect.top + nInset, -(nW - 2 * nInset), nH - 2 * nInset, glyphDC, nInset, nInset, nW - 2 * nInset, nH - 2 * nInset, SRCCOPY);
		glyphDC.SelectBitmap(hOldBitmap);
	}
}

void CWindowChrome::PaintToolbar(CDC& dc) {
	dc.FillSolidRect(&m_toolbarRect, m_colors.Background);
	DrawHorizontalLine(dc, m_toolbarRect.left, m_toolbarRect.right, m_toolbarRect.top + S(MENUBAR_HEIGHT) - max(1, S(1)) / 2, m_colors.Hover);
	DrawHorizontalLine(dc, m_toolbarRect.left, m_toolbarRect.right, m_toolbarRect.bottom - max(1, S(1)), m_colors.Border);
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

void CWindowChrome::PaintFilmstrip(CDC& dc, const CRect& rect) {
	m_filmstripCells.clear();
	dc.FillSolidRect(&rect, m_colors.PanelBackground);
	DrawHorizontalLine(dc, rect.left, rect.right, rect.top, m_colors.Border);
	CFileList* pFileList = m_pMainDlg->GetFileList();
	if (pFileList == NULL || pFileList->Size() == 0 || pFileList->IsSlideShowList()) {
		return;
	}
	std::list<CFileDesc>& files = pFileList->GetFileList();
	int nCurrent = pFileList->CurrentIndex();
	int nCell = rect.Height() - S(8);
	int nGap = S(4);
	int nCount = (int)files.size();
	int nVisible = max(1, (rect.Width() - nGap) / (nCell + nGap));
	// the current image in the middle
	int nFirst = max(0, min(nCurrent - nVisible / 2, nCount - nVisible));
	int nLast = min(nCount - 1, nFirst + nVisible - 1);
	int nX = rect.left + (rect.Width() - (nLast - nFirst + 1) * (nCell + nGap) + nGap) / 2;
	std::vector<CString> missing;
	int nIndex = 0;
	for (const CFileDesc& file : files) {
		if (nIndex > nLast) break;
		if (nIndex >= nFirst) {
			CRect cellRect(nX, rect.top + S(4), nX + nCell, rect.top + S(4) + nCell);
			m_filmstripCells.push_back(std::make_pair(cellRect, file.GetName()));
			bool bLoaded;
			HBITMAP hThumbnail = m_pThumbnails->Get(file.GetName(), bLoaded);
			if (!bLoaded) missing.push_back(file.GetName());
			dc.FillSolidRect(&cellRect, m_colors.Background);
			if (hThumbnail != NULL) {
				BITMAP bitmapInfo;
				::GetObject(hThumbnail, sizeof(bitmapInfo), &bitmapInfo);
				double dScale = min((double)nCell / max(1, bitmapInfo.bmWidth), (double)nCell / max(1, abs(bitmapInfo.bmHeight)));
				int nW = (int)(bitmapInfo.bmWidth * dScale), nH = (int)(abs(bitmapInfo.bmHeight) * dScale);
				CDC memDC;
				memDC.CreateCompatibleDC(dc);
				HBITMAP hOld = memDC.SelectBitmap(hThumbnail);
				dc.SetStretchBltMode(HALFTONE);
				dc.StretchBlt(cellRect.left + (nCell - nW) / 2, cellRect.top + (nCell - nH) / 2, nW, nH, memDC, 0, 0, bitmapInfo.bmWidth, abs(bitmapInfo.bmHeight), SRCCOPY);
				memDC.SelectBitmap(hOld);
			}
			if (nIndex == nCurrent) {
				// frame around the current image
				int nFrame = max(2, S(2));
				dc.FillSolidRect(cellRect.left - nFrame, cellRect.top - nFrame, cellRect.Width() + 2 * nFrame, nFrame, m_colors.Accent);
				dc.FillSolidRect(cellRect.left - nFrame, cellRect.bottom, cellRect.Width() + 2 * nFrame, nFrame, m_colors.Accent);
				dc.FillSolidRect(cellRect.left - nFrame, cellRect.top, nFrame, cellRect.Height(), m_colors.Accent);
				dc.FillSolidRect(cellRect.right, cellRect.top, nFrame, cellRect.Height(), m_colors.Accent);
			}
			nX += nCell + nGap;
		}
		nIndex++;
	}
	if (!missing.empty()) {
		// the current image first, then the others
		m_pThumbnails->Request(missing);
	}
}

void CWindowChrome::PaintStatusBar(CDC& dc) {
	CRect filmstripRect = GetFilmstripRect();
	if (!filmstripRect.IsRectEmpty()) {
		PaintFilmstrip(dc, filmstripRect);
	}
	CRect barRect(m_statusBarRect.left, m_statusBarRect.bottom - S(STATUSBAR_HEIGHT), m_statusBarRect.right, m_statusBarRect.bottom);
	dc.FillSolidRect(&barRect, m_colors.Background);
	DrawHorizontalLine(dc, barRect.left, barRect.right, barRect.top, m_colors.Border);
	HFONT hOldFont = dc.SelectFont(m_hFont);
	int nPadding = S(10);
	CRect textRect(barRect.left + nPadding, barRect.top, barRect.right - nPadding, barRect.bottom);

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
	if (m_pMainDlg->IsCompareMode()) {
		sStatus += CString(_T("     ")) + CNLS::GetString(_T("Comparing: left before, right after editing"));
	}
	dc.DrawText(sStatus, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
	dc.SelectFont(hOldFont);
}

void CWindowChrome::PaintSideButton(CDC& dc, const CRect& rect, LPCTSTR sText, bool bChecked, bool bHover, bool bEnabled) {
	COLORREF background = bChecked ? m_colors.Checked : bHover && bEnabled ? m_colors.Hover : m_colors.Background;
	FillRoundRect(dc, rect, background, S(6));
	dc.SetTextColor(bEnabled ? m_colors.Text : m_colors.DimText);
	CRect textRect = rect;
	dc.DrawText(sText, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
}

void CWindowChrome::PaintCheckBox(CDC& dc, const CRect& rect, bool bChecked) {
	FillRoundRect(dc, rect, bChecked ? m_colors.Accent : m_colors.Hover, S(4));
	if (bChecked && m_hSmallIconFont != NULL) {
		HFONT hOldFont = dc.SelectFont(m_hSmallIconFont);
		dc.SetTextColor(RGB(255, 255, 255));
		wchar_t check[2] = { 0xE73E, 0 }; // check mark glyph
		CRect checkRect = rect;
		dc.DrawText(check, 1, &checkRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		dc.SelectFont(hOldFont);
	}
}

void CWindowChrome::PaintCollapsedSidePanel(CDC& dc) {
	const CRect& rect = m_sidePanelRect;
	m_sideHits.clear();
	m_hitOrigin = rect.TopLeft();
	dc.FillSolidRect(&rect, m_colors.PanelBackground);
	dc.FillSolidRect(rect.left, rect.top, max(1, S(1)), rect.Height(), m_colors.Border);

	struct { wchar_t Glyph; LPCTSTR Text; ESideHitType Type; int Value; LPCTSTR Tooltip; } buttons[] = {
		{ 0xE76B, _T("<"), SH_Expand, 0, _T("Expand the side panel") },
		{ 0xE946, _T("i"), SH_Tab, TAB_INFO, _T("Information") },
		{ 0xE70F, _T("E"), SH_Tab, TAB_EDIT, _T("Edit image (brightness, contrast, colors)") },
		{ 0xE76D, _T("D"), SH_Tab, TAB_DRAW, _T("Draw and write on the image") },
	};
	int nSize = rect.Width() - S(6);
	int nY = S(6);
	for (int i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		CRect buttonRect(S(3), nY, S(3) + nSize, nY + nSize);
		AddSideHit(buttons[i].Type, buttons[i].Value, buttonRect, buttons[i].Tooltip);
		buttonRect.OffsetRect(rect.TopLeft());
		bool bHover = m_nHoverSide == (int)m_sideHits.size() - 1;
		if (bHover) {
			FillRoundRect(dc, buttonRect, m_colors.Hover, S(6));
		}
		dc.SetTextColor(m_colors.Text);
		HFONT hOldFont = dc.SelectFont(m_bHasIconFont ? m_hIconFont : m_hFont);
		wchar_t glyph[2] = { buttons[i].Glyph, 0 };
		dc.DrawText(m_bHasIconFont ? glyph : buttons[i].Text, -1, &buttonRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		dc.SelectFont(hOldFont);
		nY += nSize + S(4);
	}
}

void CWindowChrome::PaintSidePanel(CDC& dc) {
	const CRect& rect = m_sidePanelRect;
	m_sideHits.clear();
	dc.FillSolidRect(&rect, m_colors.PanelBackground);
	dc.FillSolidRect(rect.left, rect.top, max(1, S(1)), rect.Height(), m_colors.Border);

	// header with the tabs and the collapse button, it does not scroll
	int nHeader = S(SIDE_HEADER_HEIGHT);
	m_hitOrigin = rect.TopLeft();
	HFONT hOldFont = dc.SelectFont(m_hBoldFont);
	int nX = S(SIDE_PADDING) - S(6);
	LPCTSTR tabs[3] = { _T("Information"), _T("Edit"), _T("Draw") };
	for (int nTab = 0; nTab < 3; nTab++) {
		CRect measureRect(0, 0, 0, 0);
		LPCTSTR sTab = CNLS::GetString(tabs[nTab]);
		dc.DrawText(sTab, -1, &measureRect, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
		CRect tabRect(nX, S(6), nX + measureRect.Width() + S(16), nHeader - S(6));
		AddSideHit(SH_Tab, nTab, tabRect, NULL);
		bool bHover = m_nHoverSide == (int)m_sideHits.size() - 1;
		tabRect.OffsetRect(rect.TopLeft());
		if (bHover) {
			FillRoundRect(dc, tabRect, m_colors.Hover, S(6));
		}
		dc.SetTextColor(nTab == m_eSideTab ? m_colors.Text : m_colors.DimText);
		dc.DrawText(sTab, -1, &tabRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		if (nTab == m_eSideTab) {
			dc.FillSolidRect(tabRect.left + S(8), tabRect.bottom - S(2), tabRect.Width() - S(16), S(2), m_colors.Accent);
		}
		nX += tabRect.Width() + S(2);
	}
	int nButton = nHeader - S(12);
	CRect collapseRect(rect.Width() - nButton - S(6), S(6), rect.Width() - S(6), S(6) + nButton);
	AddSideHit(SH_Collapse, 0, collapseRect, _T("Collapse the side panel"));
	bool bCollapseHover = m_nHoverSide == (int)m_sideHits.size() - 1;
	collapseRect.OffsetRect(rect.TopLeft());
	if (bCollapseHover) {
		FillRoundRect(dc, collapseRect, m_colors.Hover, S(6));
	}
	dc.SetTextColor(m_colors.Text);
	dc.SelectFont(m_bHasIconFont ? m_hSmallIconFont : m_hFont);
	wchar_t collapseGlyph[2] = { 0xE76C, 0 };
	dc.DrawText(m_bHasIconFont ? collapseGlyph : _T(">"), -1, &collapseRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	dc.SelectFont(hOldFont);
	DrawHorizontalLine(dc, rect.left + S(1), rect.right, rect.top + nHeader - 1, m_colors.Border);

	// content below the header, shifted by the scroll position
	int nVisibleHeight = rect.Height() - nHeader;
	int nMaxScroll = max(0, m_nSideContentHeight - nVisibleHeight);
	m_nSideScroll = min(m_nSideScroll, nMaxScroll);

	CPoint oldOrigin;
	dc.GetViewportOrg(&oldOrigin);
	dc.SetViewportOrg(oldOrigin.x + rect.left, oldOrigin.y + rect.top + nHeader - m_nSideScroll);
	m_hitOrigin = CPoint(rect.left, rect.top + nHeader - m_nSideScroll);
	HRGN hClip = ::CreateRectRgn(0, 0, rect.Width(), nVisibleHeight);
	::OffsetRgn(hClip, oldOrigin.x + rect.left, oldOrigin.y + rect.top + nHeader);
	::SelectClipRgn(dc, hClip);
	m_nSideContentHeight = (m_eSideTab == TAB_INFO) ? PaintInfoTab(dc, rect.Width(), S(10)) :
		(m_eSideTab == TAB_EDIT) ? PaintEditTab(dc, rect.Width(), S(10)) : PaintDrawTab(dc, rect.Width(), S(10));
	::SelectClipRgn(dc, NULL);
	::DeleteObject(hClip);
	dc.SetViewportOrg(oldOrigin);

	// scroll indicator
	if (m_nSideContentHeight > nVisibleHeight) {
		int nBarHeight = max(S(24), nVisibleHeight * nVisibleHeight / m_nSideContentHeight);
		int nBarTop = rect.top + nHeader + (nVisibleHeight - nBarHeight) * m_nSideScroll / max(1, m_nSideContentHeight - nVisibleHeight);
		int nBarWidth = S(4);
		FillRoundRect(dc, CRect(rect.right - nBarWidth - S(3), nBarTop, rect.right - S(3), nBarTop + nBarHeight), m_colors.Border, nBarWidth);
	}
}

int CWindowChrome::PaintInfoTab(CDC& dc, int nWidth, int nY) {
	int nPad = S(SIDE_PADDING);
	int nGap = S(6);
	int nInnerWidth = nWidth - 2 * nPad;
	m_sLocationURL.Empty();

	HFONT hOldFont = dc.SelectFont(m_hFont);
	CJPEGImage* pImage = m_pMainDlg->GetCurrentImage();
	if (pImage == NULL || m_pMainDlg->GetEXIFDisplayCtl() == NULL) {
		dc.SetTextColor(m_colors.DimText);
		CRect textRect(nPad, nY, nPad + nInnerWidth, nY + S(40));
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
		dc.SelectFont(m_hBoldFont);
		dc.SetTextColor(m_colors.Text);
		CRect titleRect(nPad, nY, nPad + nInnerWidth, nY);
		dc.DrawText(sTitle, -1, &titleRect, DT_CALCRECT | DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
		titleRect.right = nPad + nInnerWidth;
		dc.DrawText(sTitle, -1, &titleRect, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
		nY = titleRect.bottom + S(2);
		dc.SelectFont(m_hFont);
	}
	if (pEXIF->GetPrefix() != NULL && pEXIF->GetPrefix()[0] != 0 && sTitle != pEXIF->GetPrefix()) {
		dc.SetTextColor(m_colors.DimText);
		CRect prefixRect(nPad, nY, nPad + nInnerWidth, nY + S(18));
		dc.DrawText(pEXIF->GetPrefix(), -1, &prefixRect, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
		nY = prefixRect.bottom;
	}
	// star rating, the stars can be clicked
	int nRating = m_pMainDlg->GetRating();
	if (nRating >= 0) {
		nY += S(4);
		int nStar = S(20);
		HFONT hStarFont = m_bHasIconFont ? m_hIconFont : m_hFont;
		HFONT hPrevFont = dc.SelectFont(hStarFont);
		for (int i = 1; i <= 5; i++) {
			CRect starRect(nPad + (i - 1) * (nStar + S(2)), nY, nPad + (i - 1) * (nStar + S(2)) + nStar, nY + nStar);
			AddSideHit(SH_Rating, i, starRect, _T("Rating (keys 1 to 5, 0 removes it)"));
			bool bHover = m_nHoverSide == (int)m_sideHits.size() - 1;
			bool bFilled = i <= nRating;
			dc.SetTextColor(bFilled ? RGB(255, 196, 0) : bHover ? m_colors.Text : m_colors.DimText);
			wchar_t star[2] = { (wchar_t)(m_bHasIconFont ? (bFilled ? 0xE735 : 0xE734) : (bFilled ? 0x2605 : 0x2606)), 0 };
			dc.DrawText(star, 1, &starRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		}
		dc.SelectFont(hPrevFont);
		nY += nStar;
	}
	nY += nGap;
	DrawHorizontalLine(dc, nPad, nPad + nInnerWidth, nY, m_colors.Border);
	nY += nGap + S(2);

	// description / value lines
	int nDescWidth = nInnerWidth * 42 / 100;
	int nValueX = nPad + nDescWidth + nGap;
	int nValueWidth = nPad + nInnerWidth - nValueX;
	CString sLocationURL = pEXIF->GetLocationURL();
	for (const CEXIFDisplay::TextLine& line : pEXIF->GetLines()) {
		bool bIsLink = line.ValueIsURL && !sLocationURL.IsEmpty();
		// the GPS location only wraps between latitude and longitude
		CString sValue = line.ValueIsURL ? KeepTogether(line.Value, _T(" / ")) : CString(line.Value);
		CRect descRect(nPad, nY, nPad + nDescWidth, nY);
		dc.DrawText(line.Desc, -1, &descRect, DT_CALCRECT | DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
		descRect.right = nPad + nDescWidth;
		CRect valueRect(nValueX, nY, nValueX + nValueWidth, nY);
		dc.DrawText(sValue, -1, &valueRect, DT_CALCRECT | DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
		valueRect.right = nValueX + nValueWidth;

		dc.SetTextColor(m_colors.DimText);
		dc.DrawText(line.Desc, -1, &descRect, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
		dc.SetTextColor(bIsLink ? m_colors.Accent : m_colors.Text);
		dc.DrawText(sValue, -1, &valueRect, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
		if (bIsLink) {
			m_sLocationURL = sLocationURL;
			AddSideHit(SH_Location, 0, valueRect, _T("Show the location on the map"));
		}
		nY = max(descRect.bottom, valueRect.bottom) + S(4);
	}

	// histogram
	const CHistogram* pHistogram = pImage->GetProcessedHistogram();
	if (pHistogram != NULL) {
		nY += nGap;
		dc.SelectFont(m_hBoldFont);
		dc.SetTextColor(m_colors.DimText);
		CRect histHeaderRect(nPad, nY, nPad + nInnerWidth, nY + S(20));
		dc.DrawText(CNLS::GetString(_T("Histogram")), -1, &histHeaderRect, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
		dc.SelectFont(m_hFont);
		nY = histHeaderRect.bottom + nGap;
		CRect histRect(nPad, nY, nPad + nInnerWidth, nY + S(HISTOGRAM_HEIGHT));
		PaintHistogram(dc, histRect);
		nY = histRect.bottom + nGap;
	}

	// comment
	if (pEXIF->GetComment() != NULL) {
		nY += nGap;
		dc.SelectFont(m_hBoldFont);
		dc.SetTextColor(m_colors.DimText);
		CRect commentHeaderRect(nPad, nY, nPad + nInnerWidth, nY + S(20));
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

int CWindowChrome::PaintEditTab(CDC& dc, int nWidth, int nY) {
	int nPad = S(SIDE_PADDING);
	int nInnerWidth = nWidth - 2 * nPad;
	CJPEGImage* pImage = m_pMainDlg->GetCurrentImage();
	CImageProcessingParams* pParams = m_pMainDlg->GetImageProcessingParams();
	bool bHasImage = pImage != NULL && pParams != NULL;

	HFONT hOldFont = dc.SelectFont(m_hFont);
	if (!bHasImage) {
		dc.SetTextColor(m_colors.DimText);
		CRect textRect(nPad, nY, nPad + nInnerWidth, nY + S(40));
		dc.DrawText(CNLS::GetString(_T("No image loaded!")), -1, &textRect, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
		dc.SelectFont(hOldFont);
		return textRect.bottom + nPad;
	}

	// buttons at the top: compare and reset
	int nButtonHeight = S(SIDE_BUTTON_HEIGHT);
	int nHalf = (nInnerWidth - 2 * S(6)) / 3;
	struct { LPCTSTR Text; int Command; LPCTSTR Tooltip; } topButtons[] = {
		{ _T("Auto"), IDM_AUTO_ENHANCE, _T("Automatic color, contrast and shadow correction") },
		{ _T("Compare"), IDM_COMPARE_BEFORE_AFTER, _T("Compare before and after editing") },
		{ _T("Reset"), IDM_RESET_PROCESSING, _T("Reset all values to the defaults") },
	};
	for (int i = 0; i < 3; i++) {
		CRect buttonRect(nPad + i * (nHalf + S(6)), nY, nPad + i * (nHalf + S(6)) + nHalf, nY + nButtonHeight);
		AddSideHit(SH_Button, topButtons[i].Command, buttonRect, topButtons[i].Tooltip);
		bool bHover = m_nHoverSide == (int)m_sideHits.size() - 1;
		bool bChecked = (topButtons[i].Command == IDM_COMPARE_BEFORE_AFTER && m_pMainDlg->IsCompareMode()) ||
			(topButtons[i].Command == IDM_AUTO_ENHANCE && m_pMainDlg->IsAutoContrast() && m_pMainDlg->IsLDC());
		PaintSideButton(dc, buttonRect, CNLS::GetString(topButtons[i].Text), bChecked, bHover, true);
	}
	nY += nButtonHeight + S(10);

	int nGroup = -1;
	LPCTSTR groupTitles[] = { _T("Tone and color"), _T("Local density correction"), _T("Automatic correction") };
	int nTrackHeight = S(14);
	for (int nSlider = 0; nSlider < (int)m_sliders.size(); nSlider++) {
		const CSideSlider& slider = m_sliders[nSlider];
		if (slider.Group != nGroup) {
			// group header, the groups 1 and 2 have a check box to switch the correction on and off
			nGroup = slider.Group;
			nY += S(4);
			int nCheck = S(14);
			int nTextX = nPad;
			if (nGroup > 0) {
				CRect checkRect(nPad, nY + S(2), nPad + nCheck, nY + S(2) + nCheck);
				PaintCheckBox(dc, checkRect, IsSliderGroupEnabled(nGroup));
				nTextX += nCheck + S(8);
			}
			dc.SelectFont(m_hBoldFont);
			dc.SetTextColor(m_colors.DimText);
			CRect titleRect(nTextX, nY, nPad + nInnerWidth, nY + S(18));
			dc.DrawText(CNLS::GetString(groupTitles[nGroup]), -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			dc.SelectFont(m_hFont);
			if (nGroup > 0) {
				AddSideHit(SH_GroupCheck, nGroup, CRect(nPad, nY, nPad + nInnerWidth, nY + S(18)), _T("Switch this correction on or off"));
			}
			nY += S(24);
		}

		bool bEnabled = IsSliderGroupEnabled(slider.Group);
		double dValue = pParams->*slider.Value;
		// label and value
		dc.SetTextColor(bEnabled ? m_colors.Text : m_colors.DimText);
		CRect labelRect(nPad, nY, nPad + nInnerWidth, nY + S(16));
		dc.DrawText(CNLS::GetString(slider.Name), -1, &labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		TCHAR buff[16];
		// sliders with inverted direction show the value as seen on the slider (temperature: positive is warmer)
		double dShown = (slider.Inverted && !slider.Logarithmic) ? -dValue : dValue;
		_stprintf_s(buff, 16, _T("%.2f"), fabs(dShown) < 0.005 ? 0.0 : dShown); // no "-0.00"
		dc.SetTextColor(fabs(dValue - GetSliderDefault(slider)) > 0.005 ? m_colors.Accent : m_colors.DimText);
		dc.DrawText(buff, -1, &labelRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		nY += S(18);

		// track with the default position mark and the knob
		CRect hitRect(nPad, nY, nPad + nInnerWidth, nY + nTrackHeight);
		AddSideHit(SH_Slider, nSlider, hitRect, _T("Drag to change, double click to reset"));
		int nMargin = S(7);
		int nTrackLeft = hitRect.left + nMargin, nTrackRight = hitRect.right - nMargin;
		int nTrackY = hitRect.top + nTrackHeight / 2;
		dc.FillSolidRect(nTrackLeft, nTrackY - S(1), nTrackRight - nTrackLeft, S(2), m_colors.Border);
		int nDefaultX = nTrackLeft + (int)(SliderToPosition(slider, GetSliderDefault(slider)) * (nTrackRight - nTrackLeft) + 0.5);
		dc.FillSolidRect(nDefaultX, nTrackY - S(4), max(1, S(1)), S(8), m_colors.DimText);
		int nKnobX = nTrackLeft + (int)(SliderToPosition(slider, dValue) * (nTrackRight - nTrackLeft) + 0.5);
		if (bEnabled) {
			dc.FillSolidRect(min(nDefaultX, nKnobX), nTrackY - S(1), abs(nKnobX - nDefaultX), S(2), m_colors.Accent);
		}
		int nKnob = S(6);
		CRect knobRect(nKnobX - nKnob, nTrackY - nKnob, nKnobX + nKnob, nTrackY + nKnob);
		COLORREF knobColor = !bEnabled ? m_colors.Border : (m_nDragSlider == nSlider) ? m_colors.Text : m_colors.Accent;
		CBrush knobBrush;
		knobBrush.CreateSolidBrush(knobColor);
		HBRUSH hOldBrush = dc.SelectBrush(knobBrush);
		HPEN hOldPen = dc.SelectPen((HPEN)::GetStockObject(NULL_PEN));
		dc.Ellipse(&knobRect);
		dc.SelectPen(hOldPen);
		dc.SelectBrush(hOldBrush);
		nY += nTrackHeight + S(8);
	}

	// crop and straighten
	nY += S(4);
	dc.SelectFont(m_hBoldFont);
	dc.SetTextColor(m_colors.DimText);
	CRect cropTitleRect(nPad, nY, nPad + nInnerWidth, nY + S(18));
	dc.DrawText(CNLS::GetString(_T("Crop and straighten")), -1, &cropTitleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	dc.SelectFont(m_hFont);
	nY += S(24);
	{
		CRect toolRect(nPad, nY, nPad + nInnerWidth, nY + nButtonHeight);
		AddSideHit(SH_Button, IDM_TOGGLE_CROP_TOOL, toolRect, _T("While on, dragging on the image selects the crop area (otherwise Ctrl + drag)"));
		bool bHover = m_nHoverSide == (int)m_sideHits.size() - 1;
		PaintSideButton(dc, toolRect, CNLS::GetString(_T("Crop tool")), m_pMainDlg->IsCropTool(), bHover, true);
		nY += nButtonHeight + S(6);
		struct { LPCTSTR Text; int Command; } ratios[] = {
			{ _T("Free"), IDM_CROPMODE_FREE }, { _T("1:1"), IDM_CROPMODE_1_1 }, { _T("4:3"), IDM_CROPMODE_4_3 },
			{ _T("3:2"), IDM_CROPMODE_3_2 }, { _T("16:9"), IDM_CROPMODE_16_9 }, { _T("Image"), IDM_CROPMODE_IMAGE },
		};
		int nRatioWidth = (nInnerWidth - 2 * S(6)) / 3;
		for (int i = 0; i < 6; i++) {
			int nX = nPad + (i % 3) * (nRatioWidth + S(6));
			CRect ratioRect(nX, nY, nX + nRatioWidth, nY + nButtonHeight);
			AddSideHit(SH_Button, ratios[i].Command, ratioRect, _T("Aspect ratio of the crop area"));
			bool bRatioHover = m_nHoverSide == (int)m_sideHits.size() - 1;
			PaintSideButton(dc, ratioRect, CNLS::GetString(ratios[i].Text), m_pMainDlg->GetCropModeCommand() == ratios[i].Command, bRatioHover, true);
			if (i % 3 == 2) nY += nButtonHeight + S(6);
		}
		int nHalfWidth = (nInnerWidth - S(6)) / 2;
		CRect rotateRect(nPad, nY, nPad + nHalfWidth, nY + nButtonHeight);
		AddSideHit(SH_Button, IDM_ROTATE, rotateRect, _T("Rotate by any angle to straighten the horizon"));
		bool bRotateHover = m_nHoverSide == (int)m_sideHits.size() - 1;
		PaintSideButton(dc, rotateRect, CNLS::GetString(_T("Straighten...")), false, bRotateHover, true);
		CRect perspectiveRect(nPad + nHalfWidth + S(6), nY, nPad + nInnerWidth, nY + nButtonHeight);
		AddSideHit(SH_Button, IDM_PERSPECTIVE, perspectiveRect, _T("Correct converging lines of buildings"));
		bool bPerspectiveHover = m_nHoverSide == (int)m_sideHits.size() - 1;
		PaintSideButton(dc, perspectiveRect, CNLS::GetString(_T("Perspective...")), false, bPerspectiveHover, true);
		nY += nButtonHeight + S(10);
	}

	// further actions
	nY += S(6);
	DrawHorizontalLine(dc, nPad, nPad + nInnerWidth, nY, m_colors.Border);
	nY += S(10);
	bool bCanUseParamDB = !pImage->IsClipboardImage() && !m_pMainDlg->IsKeepParams() && !m_pMainDlg->IsInMovieMode();
	struct { LPCTSTR Text; int Command; bool Enabled; LPCTSTR Tooltip; } buttons[] = {
		{ _T("Resize image..."), IDM_CHANGESIZE, true, _T("Change the size in pixels or percent, save afterwards") },
		{ _T("Copy text from image"), IDM_OCR_COPY_TEXT, true, _T("Recognizes the text in the image and copies it to the clipboard") },
		{ _T("Unsharp mask..."), IDM_UNSHARP_MASK, true, _T("Sharpen with radius, amount and threshold") },
		{ _T("Save these values for this image"), IDM_SAVE_PARAM_DB, bCanUseParamDB, _T("JPEGView shows this image with these values the next time (parameter DB)") },
		{ _T("Forget the values of this image"), IDM_CLEAR_PARAM_DB, bCanUseParamDB && pImage->IsInParamDB(), _T("Removes this image from the parameter DB") },
		{ _T("Save processed image..."), IDM_SAVE, true, NULL },
	};
	for (int i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		CRect buttonRect(nPad, nY, nPad + nInnerWidth, nY + nButtonHeight);
		if (buttons[i].Enabled) {
			AddSideHit(SH_Button, buttons[i].Command, buttonRect, buttons[i].Tooltip);
		}
		bool bHover = buttons[i].Enabled && m_nHoverSide == (int)m_sideHits.size() - 1;
		PaintSideButton(dc, buttonRect, CNLS::GetString(buttons[i].Text), false, bHover, buttons[i].Enabled);
		nY += nButtonHeight + S(6);
	}

	dc.SelectFont(hOldFont);
	return nY + nPad;
}

int CWindowChrome::PaintDrawTab(CDC& dc, int nWidth, int nY) {
	int nPad = S(SIDE_PADDING);
	int nInnerWidth = nWidth - 2 * nPad;
	int nButtonHeight = S(SIDE_BUTTON_HEIGHT);
	CAnnotations* pAnnotations = m_pMainDlg->GetAnnotations();
	bool bHasImage = m_pMainDlg->GetCurrentImage() != NULL;
	HFONT hOldFont = dc.SelectFont(m_hFont);

	auto SectionTitle = [&](LPCTSTR sTitle) {
		dc.SelectFont(m_hBoldFont);
		dc.SetTextColor(m_colors.DimText);
		CRect titleRect(nPad, nY, nPad + nInnerWidth, nY + S(18));
		dc.DrawText(CNLS::GetString(sTitle), -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		dc.SelectFont(m_hFont);
		nY += S(24);
	};

	// tools, three per row
	SectionTitle(_T("Tool"));
	struct { LPCTSTR Text; CAnnotations::ETool Tool; LPCTSTR Tooltip; } tools[] = {
		{ _T("Pen"), CAnnotations::TOOL_PEN, _T("Draw freehand") },
		{ _T("Highlighter"), CAnnotations::TOOL_HIGHLIGHTER, _T("Mark with a transparent wide pen") },
		{ _T("Rectangle"), CAnnotations::TOOL_RECTANGLE, _T("Draw a rectangle") },
		{ _T("Arrow"), CAnnotations::TOOL_ARROW, _T("Draw an arrow") },
		{ _T("Text"), CAnnotations::TOOL_TEXT, _T("Click on the image to write a text") },
		{ _T("Remove color"), CAnnotations::TOOL_REMOVE_COLOR, _T("Click on a color to make the connected area transparent, the width sets the tolerance") },
		{ _T("Off"), CAnnotations::TOOL_NONE, _T("No tool, the mouse pans and selects again") },
	};
	int nColumnWidth = (nInnerWidth - 2 * S(6)) / 3;
	for (int i = 0; i < sizeof(tools) / sizeof(tools[0]); i++) {
		int nX = nPad + (i % 3) * (nColumnWidth + S(6));
		CRect buttonRect(nX, nY, nX + nColumnWidth, nY + nButtonHeight);
		AddSideHit(SH_Tool, tools[i].Tool, buttonRect, tools[i].Tooltip);
		bool bHover = m_nHoverSide == (int)m_sideHits.size() - 1;
		PaintSideButton(dc, buttonRect, CNLS::GetString(tools[i].Text), pAnnotations->GetTool() == tools[i].Tool, bHover, true);
		if (i % 3 == 2) nY += nButtonHeight + S(6);
	}
	if ((sizeof(tools) / sizeof(tools[0])) % 3 != 0) nY += nButtonHeight + S(6);
	nY += S(6);

	// colors
	SectionTitle(_T("Color"));
	const COLORREF colors[] = { RGB(230, 40, 40), RGB(255, 140, 0), RGB(255, 214, 0), RGB(40, 180, 70),
		RGB(30, 120, 230), RGB(150, 70, 200), RGB(0, 0, 0), RGB(255, 255, 255) };
	int nSwatch = (nInnerWidth - 7 * S(6)) / 8;
	for (int i = 0; i < 8; i++) {
		int nX = nPad + i * (nSwatch + S(6));
		CRect swatchRect(nX, nY, nX + nSwatch, nY + nSwatch);
		AddSideHit(SH_Color, (int)colors[i], swatchRect, NULL);
		if (pAnnotations->GetColor() == colors[i]) {
			FillRoundRect(dc, CRect(swatchRect.left - S(3), swatchRect.top - S(3), swatchRect.right + S(3), swatchRect.bottom + S(3)), m_colors.Accent, S(8));
		}
		FillRoundRect(dc, swatchRect, colors[i], S(6));
	}
	nY += nSwatch + S(14);

	// line width
	SectionTitle(_T("Width"));
	const int widths[] = { 2, 4, 8, 16 };
	int nWidthButton = (nInnerWidth - 3 * S(6)) / 4;
	for (int i = 0; i < 4; i++) {
		int nX = nPad + i * (nWidthButton + S(6));
		CRect buttonRect(nX, nY, nX + nWidthButton, nY + nButtonHeight);
		AddSideHit(SH_Width, widths[i], buttonRect, NULL);
		bool bHover = m_nHoverSide == (int)m_sideHits.size() - 1;
		PaintSideButton(dc, buttonRect, _T(""), pAnnotations->GetWidth() == widths[i], bHover, true);
		int nLine = max(1, S(widths[i]) / 2);
		dc.FillSolidRect(buttonRect.left + S(10), buttonRect.CenterPoint().y - nLine / 2, buttonRect.Width() - S(20), nLine, m_colors.Text);
	}
	nY += nButtonHeight + S(14);

	// actions
	bool bHasDrawing = !pAnnotations->IsEmpty();
	struct { LPCTSTR Text; int Command; bool Enabled; LPCTSTR Tooltip; } buttons[] = {
		{ _T("Undo"), IDM_ANNOTATION_UNDO, bHasDrawing, _T("Remove the last drawing") },
		{ _T("Remove all drawings"), IDM_ANNOTATION_CLEAR, bHasDrawing, NULL },
		{ _T("Save pen strokes as signature"), IDM_SIGNATURE_SAVE, bHasDrawing, _T("Write your signature with the pen first") },
		{ _T("Insert signature"), IDM_SIGNATURE_INSERT, bHasImage && pAnnotations->HasSignature(), NULL },
		{ _T("Draw into the image"), IDM_ANNOTATION_APPLY, bHasDrawing, _T("Saving the image does this too") },
		{ _T("Remove background"), IDM_REMOVE_BACKGROUND, bHasImage, _T("Makes the background transparent with an AI model (176 MB download on first use)") },
		{ _T("Save as transparent PNG..."), IDM_SAVE_TRANSPARENT, m_pMainDlg->HasTransparency(), _T("Saves the image with the removed colors as transparent PNG") },
		{ _T("Restore removed colors"), IDM_UNDO_TRANSPARENCY, m_pMainDlg->HasTransparency(), NULL },
		{ _T("Save processed image..."), IDM_SAVE, bHasImage, NULL },
	};
	for (int i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
		CRect buttonRect(nPad, nY, nPad + nInnerWidth, nY + nButtonHeight);
		if (buttons[i].Enabled) {
			AddSideHit(SH_Button, buttons[i].Command, buttonRect, buttons[i].Tooltip);
		}
		bool bHover = buttons[i].Enabled && m_nHoverSide == (int)m_sideHits.size() - 1;
		PaintSideButton(dc, buttonRect, CNLS::GetString(buttons[i].Text), false, bHover, buttons[i].Enabled);
		nY += nButtonHeight + S(6);
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
		pen.CreatePen(PS_SOLID, max(1, S(1)), channelColors[c - 1]);
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
