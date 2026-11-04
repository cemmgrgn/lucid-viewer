#include "StdAfx.h"
#include "DarkUI.h"
#include "SettingsProvider.h"
#include <uxtheme.h>
#include <gdiplus.h>
#include <map>
#include <vector>

#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "comctl32.lib")

namespace DarkUI {

static int s_nForcedTheme = -1;
static std::map<COLORREF, HBRUSH> s_brushes;
static const TCHAR* BACKGROUND_PROPERTY = _T("JPEGView.DarkUI.Background");

static const CPalette s_dark = {
	RGB(32, 32, 32), RGB(39, 39, 39), RGB(55, 55, 55), RGB(66, 66, 66), RGB(235, 235, 235), RGB(160, 160, 160),
	RGB(78, 78, 78), RGB(76, 194, 255), RGB(0, 0, 0), RGB(58, 58, 58)
};
static const CPalette s_light = {
	RGB(243, 243, 243), RGB(255, 255, 255), RGB(253, 253, 253), RGB(245, 245, 245), RGB(26, 26, 26), RGB(96, 96, 96),
	RGB(204, 204, 204), RGB(0, 95, 184), RGB(255, 255, 255), RGB(229, 239, 250)
};

bool IsDark() {
	if (s_nForcedTheme >= 0) return s_nForcedTheme == 1;
	return CSettingsProvider::This().ChromeDarkTheme();
}

void ForceTheme(int nDark) {
	s_nForcedTheme = nDark;
}

const CPalette& Colors() {
	return IsDark() ? s_dark : s_light;
}

HBRUSH Brush(COLORREF color) {
	auto it = s_brushes.find(color);
	if (it != s_brushes.end()) return it->second;
	HBRUSH hBrush = ::CreateSolidBrush(color);
	s_brushes[color] = hBrush;
	return hBrush;
}

static int Scale(HWND hWnd, int n) {
	HDC hDC = ::GetDC(hWnd);
	int nDpi = ::GetDeviceCaps(hDC, LOGPIXELSX);
	::ReleaseDC(hWnd, hDC);
	return ::MulDiv(n, nDpi, 96);
}

///////////////////////////////////////////////////////////////////////////////////
// Themes
///////////////////////////////////////////////////////////////////////////////////

typedef BOOL (WINAPI *AllowDarkModeForWindowFn)(HWND, BOOL);
typedef int (WINAPI *SetPreferredAppModeFn)(int);

static AllowDarkModeForWindowFn GetAllowDarkModeForWindow() {
	static AllowDarkModeForWindowFn pFunction = NULL;
	static bool bLoaded = false;
	if (!bLoaded) {
		bLoaded = true;
		OSVERSIONINFOEXW info = { sizeof(info) };
		typedef LONG (WINAPI *RtlGetVersionFn)(OSVERSIONINFOEXW*);
		RtlGetVersionFn rtlGetVersion = (RtlGetVersionFn)::GetProcAddress(::GetModuleHandle(_T("ntdll.dll")), "RtlGetVersion");
		if (rtlGetVersion != NULL) rtlGetVersion(&info);
		if (info.dwBuildNumber >= 18362) {
			HMODULE hUxTheme = ::LoadLibrary(_T("uxtheme.dll"));
			if (hUxTheme != NULL) {
				pFunction = (AllowDarkModeForWindowFn)::GetProcAddress(hUxTheme, MAKEINTRESOURCEA(133));
				SetPreferredAppModeFn setPreferredAppMode = (SetPreferredAppModeFn)::GetProcAddress(hUxTheme, MAKEINTRESOURCEA(135));
				// the main window sets the mode too, here for windows shown without it (UI test)
				if (setPreferredAppMode != NULL && IsDark()) setPreferredAppMode(2);
			}
		}
	}
	return pFunction;
}

static void SetDarkTitleBar(HWND hWnd, bool bDark) {
	typedef HRESULT (WINAPI *DwmSetWindowAttributeFn)(HWND, DWORD, LPCVOID, DWORD);
	static DwmSetWindowAttributeFn dwmSetWindowAttribute = NULL;
	static bool bLoaded = false;
	if (!bLoaded) {
		bLoaded = true;
		HMODULE hDwm = ::LoadLibrary(_T("dwmapi.dll"));
		if (hDwm != NULL) dwmSetWindowAttribute = (DwmSetWindowAttributeFn)::GetProcAddress(hDwm, "DwmSetWindowAttribute");
	}
	if (dwmSetWindowAttribute == NULL) return;
	BOOL bValue = bDark ? TRUE : FALSE;
	if (FAILED(dwmSetWindowAttribute(hWnd, 20, &bValue, sizeof(bValue)))) {
		dwmSetWindowAttribute(hWnd, 19, &bValue, sizeof(bValue));
	}
}

// The header of a list view sends its custom draw notification to the list view: light text on the dark header
static LRESULT CALLBACK ListViewSubclassProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
	if (nMsg == WM_NOTIFY && IsDark()) {
		NMHDR* pHeader = (NMHDR*)lParam;
		if (pHeader->code == NM_CUSTOMDRAW && pHeader->hwndFrom == ListView_GetHeader(hWnd)) {
			NMCUSTOMDRAW* pDraw = (NMCUSTOMDRAW*)lParam;
			if (pDraw->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
			if (pDraw->dwDrawStage == CDDS_ITEMPREPAINT) {
				::SetTextColor(pDraw->hdc, Colors().Text);
				return CDRF_DODEFAULT;
			}
		}
	}
	return ::DefSubclassProc(hWnd, nMsg, wParam, lParam);
}

void ApplyToControl(HWND hControl) {
	bool bDark = IsDark();
	AllowDarkModeForWindowFn allowDarkModeForWindow = GetAllowDarkModeForWindow();
	if (allowDarkModeForWindow != NULL) allowDarkModeForWindow(hControl, bDark ? TRUE : FALSE);
	TCHAR sClass[64] = { 0 };
	::GetClassName(hControl, sClass, 64);
	LPCWSTR sTheme = NULL;
	if (_tcsicmp(sClass, _T("Edit")) == 0 || _tcsicmp(sClass, _T("ComboBox")) == 0) {
		sTheme = bDark ? L"DarkMode_CFD" : L"Explorer";
	} else if (_tcsicmp(sClass, WC_LISTVIEW) == 0 || _tcsicmp(sClass, WC_TREEVIEW) == 0) {
		sTheme = bDark ? L"DarkMode_Explorer" : L"Explorer";
		if (_tcsicmp(sClass, WC_LISTVIEW) == 0) {
			const CPalette& colors = Colors();
			ListView_SetBkColor(hControl, colors.Surface);
			ListView_SetTextBkColor(hControl, colors.Surface);
			ListView_SetTextColor(hControl, colors.Text);
			::SetWindowSubclass(hControl, ListViewSubclassProc, 1, 0);
			HWND hHeader = ListView_GetHeader(hControl);
			if (hHeader != NULL) {
				if (allowDarkModeForWindow != NULL) allowDarkModeForWindow(hHeader, bDark ? TRUE : FALSE);
				::SetWindowTheme(hHeader, bDark ? L"DarkMode_ItemsView" : L"ItemsView", NULL);
			}
		}
	} else if (_tcsicmp(sClass, WC_HEADER) == 0) {
		// the list view sets it, the header is also enumerated as child window
		sTheme = bDark ? L"DarkMode_ItemsView" : L"ItemsView";
	} else if (_tcsicmp(sClass, _T("Button")) == 0 || _tcsicmp(sClass, _T("ListBox")) == 0 || _tcsicmp(sClass, _T("ScrollBar")) == 0 ||
		_tcsicmp(sClass, _T("msctls_updown32")) == 0) {
		sTheme = bDark ? L"DarkMode_Explorer" : L"Explorer";
	} else if (_tcsicmp(sClass, _T("Static")) != 0) {
		// own window classes: their scroll bars
		sTheme = bDark ? L"DarkMode_Explorer" : L"Explorer";
	}
	if (sTheme != NULL) {
		::SetWindowTheme(hControl, sTheme, NULL);
		::SendMessage(hControl, WM_THEMECHANGED, 0, 0);
	}
}

static BOOL CALLBACK ApplyToChild(HWND hWnd, LPARAM) {
	ApplyToControl(hWnd);
	return TRUE;
}

void ApplyToWindow(HWND hWnd) {
	bool bDark = IsDark();
	AllowDarkModeForWindowFn allowDarkModeForWindow = GetAllowDarkModeForWindow();
	if (allowDarkModeForWindow != NULL) allowDarkModeForWindow(hWnd, bDark ? TRUE : FALSE);
	SetDarkTitleBar(hWnd, bDark);
	::EnumChildWindows(hWnd, ApplyToChild, 0);
}

void SetBackground(HWND hParent, COLORREF color) {
	::SetProp(hParent, BACKGROUND_PROPERTY, (HANDLE)(INT_PTR)(color + 1));
}

static COLORREF GetBackground(HWND hParent) {
	INT_PTR nValue = (INT_PTR)::GetProp(hParent, BACKGROUND_PROPERTY);
	return nValue != 0 ? (COLORREF)(nValue - 1) : Colors().Window;
}

HBRUSH CtlColor(UINT nMsg, WPARAM wParam, LPARAM lParam, COLORREF background, bool bSubText) {
	const CPalette& colors = Colors();
	HDC hDC = (HDC)wParam;
	switch (nMsg) {
		case WM_CTLCOLORDLG:
			return Brush(background);
		case WM_CTLCOLORSTATIC: {
			// read only edit fields are statics too
			TCHAR sClass[16] = { 0 };
			::GetClassName((HWND)lParam, sClass, 16);
			bool bEdit = _tcsicmp(sClass, _T("Edit")) == 0;
			COLORREF back = bEdit ? colors.Control : background;
			::SetTextColor(hDC, ::IsWindowEnabled((HWND)lParam) ? (bSubText ? colors.SubText : colors.Text) : colors.SubText);
			::SetBkColor(hDC, back);
			return Brush(back);
		}
		case WM_CTLCOLOREDIT:
		case WM_CTLCOLORLISTBOX:
			::SetTextColor(hDC, colors.Text);
			::SetBkColor(hDC, colors.Control);
			return Brush(colors.Control);
		case WM_CTLCOLORBTN:
			::SetTextColor(hDC, colors.Text);
			::SetBkColor(hDC, background);
			return Brush(background);
	}
	return NULL;
}

///////////////////////////////////////////////////////////////////////////////////
// Buttons
///////////////////////////////////////////////////////////////////////////////////

static Gdiplus::Color ToColor(COLORREF color) {
	return Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color));
}

static void AddRoundRect(Gdiplus::GraphicsPath& path, float x, float y, float w, float h, float r) {
	float d = 2 * r;
	path.AddArc(x, y, d, d, 180, 90);
	path.AddArc(x + w - d, y, d, d, 270, 90);
	path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
	path.AddArc(x, y + h - d, d, d, 90, 90);
	path.CloseFigure();
}

static void DrawButtonText(HDC hDC, HWND hButton, RECT rect, COLORREF color, UINT nFormat) {
	TCHAR sText[256] = { 0 };
	::GetWindowText(hButton, sText, 256);
	HFONT hFont = (HFONT)::SendMessage(hButton, WM_GETFONT, 0, 0);
	HGDIOBJ hOldFont = hFont != NULL ? ::SelectObject(hDC, hFont) : NULL;
	::SetBkMode(hDC, TRANSPARENT);
	::SetTextColor(hDC, color);
	::DrawText(hDC, sText, -1, &rect, nFormat | DT_SINGLELINE | DT_VCENTER | DT_HIDEPREFIX | DT_END_ELLIPSIS);
	if (hOldFont != NULL) ::SelectObject(hDC, hOldFont);
}

bool CustomDrawButton(LPARAM lParam, LRESULT& result) {
	NMHDR* pHeader = (NMHDR*)lParam;
	if (pHeader->code != NM_CUSTOMDRAW) return false;
	TCHAR sClass[16] = { 0 };
	::GetClassName(pHeader->hwndFrom, sClass, 16);
	if (_tcsicmp(sClass, _T("Button")) != 0) return false;
	NMCUSTOMDRAW* pDraw = (NMCUSTOMDRAW*)lParam;
	if (pDraw->dwDrawStage != CDDS_PREPAINT) {
		result = CDRF_DODEFAULT;
		return true;
	}
	HWND hButton = pHeader->hwndFrom;
	LONG nStyle = ::GetWindowLong(hButton, GWL_STYLE);
	LONG nType = nStyle & BS_TYPEMASK;
	bool bCheckBox = nType == BS_CHECKBOX || nType == BS_AUTOCHECKBOX;
	bool bRadio = nType == BS_RADIOBUTTON || nType == BS_AUTORADIOBUTTON;
	bool bPushLike = (nStyle & BS_PUSHLIKE) != 0;
	bool bPush = nType == BS_PUSHBUTTON || nType == BS_DEFPUSHBUTTON || bPushLike;
	if (nType == BS_GROUPBOX || nType == BS_OWNERDRAW || (!bCheckBox && !bRadio && !bPush)) return false;
	// push buttons keep the look of Windows in light mode
	if (bPush && !bPushLike && !IsDark()) return false;

	const CPalette& colors = Colors();
	HDC hDC = pDraw->hdc;
	RECT rect = pDraw->rc;
	bool bEnabled = ::IsWindowEnabled(hButton) != FALSE;
	bool bChecked = ::SendMessage(hButton, BM_GETCHECK, 0, 0) == BST_CHECKED;
	bool bHot = (pDraw->uItemState & CDIS_HOT) != 0;
	bool bPressed = (pDraw->uItemState & CDIS_SELECTED) != 0;
	bool bFocus = (pDraw->uItemState & CDIS_FOCUS) != 0;
	COLORREF background = GetBackground(::GetParent(hButton));
	::FillRect(hDC, &rect, Brush(background));

	Gdiplus::Graphics graphics(hDC);
	graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
	float fScale = Scale(hButton, 100) / 100.0f;

	if (bPush) {
		bool bDefault = nType == BS_DEFPUSHBUTTON || (bPushLike && bChecked);
		COLORREF fill = bDefault ? colors.Accent : (bPressed ? colors.Border : (bHot ? colors.ControlHot : colors.Control));
		if (bDefault && (bHot || bPressed)) {
			fill = RGB(GetRValue(fill) * 9 / 10, GetGValue(fill) * 9 / 10, GetBValue(fill) * 9 / 10);
		}
		Gdiplus::GraphicsPath path;
		AddRoundRect(path, rect.left + 0.5f, rect.top + 0.5f, rect.right - rect.left - 1.0f, rect.bottom - rect.top - 1.0f, 4 * fScale);
		Gdiplus::SolidBrush brush(ToColor(fill));
		graphics.FillPath(&brush, &path);
		Gdiplus::Pen pen(ToColor(bFocus && !bDefault ? colors.Accent : (bDefault ? fill : colors.Border)), 1.0f);
		graphics.DrawPath(&pen, &path);
		COLORREF text = !bEnabled ? colors.SubText : (bDefault ? colors.AccentText : colors.Text);
		DrawButtonText(hDC, hButton, rect, text, DT_CENTER);
		result = CDRF_SKIPDEFAULT;
		return true;
	}

	float fHeight = (float)(rect.bottom - rect.top);
	float fTextX;
	if (bCheckBox) {
		// switch like in the settings of Windows 11
		float fW = 36 * fScale, fH = 18 * fScale;
		float fX = rect.left + 1.0f, fY = rect.top + (fHeight - fH) / 2;
		Gdiplus::GraphicsPath path;
		AddRoundRect(path, fX, fY, fW, fH, fH / 2);
		if (bChecked) {
			Gdiplus::SolidBrush brush(ToColor(bEnabled ? colors.Accent : colors.Border));
			graphics.FillPath(&brush, &path);
		} else {
			Gdiplus::SolidBrush brush(ToColor(bHot ? colors.ControlHot : background));
			graphics.FillPath(&brush, &path);
			Gdiplus::Pen pen(ToColor(colors.SubText), 1.2f * fScale);
			graphics.DrawPath(&pen, &path);
		}
		float fKnob = (bHot || bPressed ? 12 : 10) * fScale;
		float fKnobX = bChecked ? fX + fW - fH / 2 - fKnob / 2 : fX + fH / 2 - fKnob / 2;
		Gdiplus::SolidBrush knob(ToColor(bChecked ? colors.AccentText : colors.SubText));
		graphics.FillEllipse(&knob, fKnobX, fY + (fH - fKnob) / 2, fKnob, fKnob);
		fTextX = fX + fW + 8 * fScale;
	} else {
		float fD = 16 * fScale;
		float fX = rect.left + 1.0f, fY = rect.top + (fHeight - fD) / 2;
		if (bChecked) {
			Gdiplus::SolidBrush brush(ToColor(bEnabled ? colors.Accent : colors.Border));
			graphics.FillEllipse(&brush, fX, fY, fD, fD);
			float fDot = (bHot ? 8 : 7) * fScale;
			Gdiplus::SolidBrush dot(ToColor(colors.AccentText));
			graphics.FillEllipse(&dot, fX + (fD - fDot) / 2, fY + (fD - fDot) / 2, fDot, fDot);
		} else {
			Gdiplus::SolidBrush brush(ToColor(bHot ? colors.ControlHot : colors.Control));
			graphics.FillEllipse(&brush, fX, fY, fD, fD);
			Gdiplus::Pen pen(ToColor(colors.SubText), 1.2f * fScale);
			graphics.DrawEllipse(&pen, fX, fY, fD, fD);
		}
		fTextX = fX + fD + 8 * fScale;
	}
	RECT textRect = rect;
	textRect.left = (LONG)(fTextX + 0.5f);
	DrawButtonText(hDC, hButton, textRect, bEnabled ? colors.Text : colors.SubText, DT_LEFT);
	if (bFocus && (::SendMessage(hButton, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS) == 0) {
		RECT focusRect = textRect;
		focusRect.left -= 2;
		::DrawFocusRect(hDC, &focusRect);
	}
	result = CDRF_SKIPDEFAULT;
	return true;
}

///////////////////////////////////////////////////////////////////////////////////
// Snapshots for the UI test
///////////////////////////////////////////////////////////////////////////////////

static CString s_sSnapshotFile;
static HWND s_hSnapshotWindow = NULL;
static int s_nCloseCommand = 0;
static const UINT_PTR SNAPSHOT_TIMER = 0x5A47;

void SetSnapshotFile(LPCTSTR sFileName) {
	s_sSnapshotFile = sFileName;
}

bool IsSnapshotMode() {
	return !s_sSnapshotFile.IsEmpty();
}

static bool SaveWindowImage(HWND hWnd, LPCTSTR sFileName) {
	RECT rect;
	::GetWindowRect(hWnd, &rect);
	int nWidth = rect.right - rect.left, nHeight = rect.bottom - rect.top;
	HDC hScreen = ::GetDC(NULL);
	HDC hMemDC = ::CreateCompatibleDC(hScreen);
	HBITMAP hBitmap = ::CreateCompatibleBitmap(hScreen, nWidth, nHeight);
	HGDIOBJ hOld = ::SelectObject(hMemDC, hBitmap);
	const UINT PW_RENDERFULLCONTENT_FLAG = 2;
	bool bOK = ::PrintWindow(hWnd, hMemDC, PW_RENDERFULLCONTENT_FLAG) != FALSE;
	::SelectObject(hMemDC, hOld);
	if (bOK) {
		Gdiplus::Bitmap bitmap(hBitmap, NULL);
		UINT nEncoders = 0, nSize = 0;
		Gdiplus::GetImageEncodersSize(&nEncoders, &nSize);
		std::vector<BYTE> buffer(nSize);
		Gdiplus::ImageCodecInfo* pEncoders = (Gdiplus::ImageCodecInfo*)&buffer[0];
		Gdiplus::GetImageEncoders(nEncoders, nSize, pEncoders);
		bOK = false;
		for (UINT i = 0; i < nEncoders; i++) {
			if (wcscmp(pEncoders[i].MimeType, L"image/png") == 0) {
				bOK = bitmap.Save(sFileName, &pEncoders[i].Clsid, NULL) == Gdiplus::Ok;
			}
		}
	}
	::DeleteObject(hBitmap);
	::DeleteDC(hMemDC);
	::ReleaseDC(NULL, hScreen);
	return bOK;
}

static void CALLBACK SnapshotTimerProc(HWND, UINT, UINT_PTR nTimer, DWORD) {
	::KillTimer(NULL, nTimer);
	if (s_hSnapshotWindow == NULL || !::IsWindow(s_hSnapshotWindow)) return;
	::RedrawWindow(s_hSnapshotWindow, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
	SaveWindowImage(s_hSnapshotWindow, s_sSnapshotFile);
	HWND hWnd = s_hSnapshotWindow;
	s_hSnapshotWindow = NULL;
	::PostMessage(hWnd, WM_COMMAND, s_nCloseCommand, 0);
}

void ShowSnapshot(HWND hWnd, int nCloseCommand, int nDelayMs) {
	s_hSnapshotWindow = hWnd;
	s_nCloseCommand = nCloseCommand;
	// out of the visible screen, not activated: the user can keep working
	::SetWindowPos(hWnd, HWND_BOTTOM, -20000, -20000, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
	::ShowWindow(hWnd, SW_SHOWNOACTIVATE);
	::SetTimer(NULL, 0, nDelayMs, SnapshotTimerProc);
}

}
