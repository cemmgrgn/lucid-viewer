#include "StdAfx.h"
#include "resource.h"
#include "ScreenCapture.h"
#include "Annotations.h"
#include "Ocr.h"
#include "NLS.h"
#include "SettingsProvider.h"
#include "HelpersGUI.h"
#include <gdiplus.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <vector>
#include <string>

#pragma comment(lib, "dwmapi.lib")

namespace ScreenCapture {

///////////////////////////////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////////////////////////////

// The process is system DPI aware (manifest), the capture threads are per monitor aware to get the physical pixels of all monitors
typedef HANDLE(WINAPI* SetThreadDpiAwarenessContextFn)(HANDLE);
static const HANDLE DPI_CONTEXT_PER_MONITOR_V2 = (HANDLE)-4;

class CPerMonitorDpiScope {
public:
	CPerMonitorDpiScope() {
		m_pSet = (SetThreadDpiAwarenessContextFn)::GetProcAddress(::GetModuleHandle(_T("user32.dll")), "SetThreadDpiAwarenessContext");
		m_hOld = (m_pSet != NULL) ? m_pSet(DPI_CONTEXT_PER_MONITOR_V2) : NULL;
	}
	~CPerMonitorDpiScope() {
		if (m_pSet != NULL && m_hOld != NULL) m_pSet(m_hOld);
	}
private:
	SetThreadDpiAwarenessContextFn m_pSet;
	HANDLE m_hOld;
};

static UINT GetMonitorDpi(HMONITOR hMonitor) {
	typedef HRESULT(WINAPI* GetDpiForMonitorFn)(HMONITOR, int, UINT*, UINT*);
	static GetDpiForMonitorFn pGetDpi = NULL;
	static bool bLoaded = false;
	if (!bLoaded) {
		bLoaded = true;
		HMODULE hShcore = ::LoadLibrary(_T("shcore.dll"));
		if (hShcore != NULL) pGetDpi = (GetDpiForMonitorFn)::GetProcAddress(hShcore, "GetDpiForMonitor");
	}
	UINT nDpiX = 96, nDpiY = 96;
	if (pGetDpi == NULL || FAILED(pGetDpi(hMonitor, 0, &nDpiX, &nDpiY))) {
		HDC hDC = ::GetDC(NULL);
		nDpiX = ::GetDeviceCaps(hDC, LOGPIXELSX);
		::ReleaseDC(NULL, hDC);
	}
	return nDpiX;
}

static CString GetExeName() {
	TCHAR sExe[MAX_PATH];
	::GetModuleFileName(NULL, sExe, MAX_PATH);
	return sExe;
}

CString GetScreenshotFolder() {
	CString sFolder = CSettingsProvider::This().ScreenshotFolder();
	if (sFolder.IsEmpty()) {
		// Pictures\Screenshots (FOLDERID_Screenshots, Windows 8 and later), else Pictures
		static const GUID folderScreenshots = { 0xb7bede81, 0xdf94, 0x4682, { 0xa7, 0xd8, 0x57, 0xa5, 0x26, 0x20, 0xb8, 0x6f } };
		PWSTR pPath = NULL;
		if (SUCCEEDED(::SHGetKnownFolderPath(folderScreenshots, KF_FLAG_CREATE, NULL, &pPath)) ||
			SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Pictures, 0, NULL, &pPath))) {
			sFolder = pPath;
		}
		::CoTaskMemFree(pPath);
	} else {
		::SHCreateDirectoryEx(NULL, sFolder, NULL);
	}
	sFolder.TrimRight(_T('\\'));
	return sFolder;
}

static CString GetNewScreenshotName(LPCTSTR sExtension) {
	SYSTEMTIME now;
	::GetLocalTime(&now);
	CString sName;
	sName.Format(_T("%s %04d-%02d-%02d %02d%02d%02d.%s"), CNLS::GetString(_T("Screenshot")), now.wYear, now.wMonth, now.wDay,
		now.wHour, now.wMinute, now.wSecond, sExtension);
	return sName;
}

static bool GetEncoder(LPCWSTR sMimeType, CLSID& clsid) {
	UINT nEncoders = 0, nSize = 0;
	Gdiplus::GetImageEncodersSize(&nEncoders, &nSize);
	if (nSize == 0) return false;
	std::vector<BYTE> buffer(nSize);
	Gdiplus::ImageCodecInfo* pEncoders = (Gdiplus::ImageCodecInfo*)&buffer[0];
	Gdiplus::GetImageEncoders(nEncoders, nSize, pEncoders);
	for (UINT i = 0; i < nEncoders; i++) {
		if (wcscmp(pEncoders[i].MimeType, sMimeType) == 0) {
			clsid = pEncoders[i].Clsid;
			return true;
		}
	}
	return false;
}

// Saves 32 bpp BGR pixels as PNG or JPEG (by the file ending)
static bool SavePixels(LPCTSTR sFileName, std::vector<DWORD>& pixels, int nWidth, int nHeight) {
	CString sLower(sFileName);
	sLower.MakeLower();
	bool bJPEG = sLower.Right(4) == _T(".jpg") || sLower.Right(5) == _T(".jpeg");
	CLSID clsid;
	if (!GetEncoder(bJPEG ? L"image/jpeg" : L"image/png", clsid)) return false;
	Gdiplus::Bitmap bitmap(nWidth, nHeight, nWidth * 4, PixelFormat32bppRGB, (BYTE*)&pixels[0]);
	Gdiplus::EncoderParameters parameters;
	ULONG nQuality = 92;
	parameters.Count = 1;
	parameters.Parameter[0].Guid = Gdiplus::EncoderQuality;
	parameters.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
	parameters.Parameter[0].NumberOfValues = 1;
	parameters.Parameter[0].Value = &nQuality;
	return bitmap.Save(sFileName, &clsid, bJPEG ? &parameters : NULL) == Gdiplus::Ok;
}

static bool CopyTextToClipboard(HWND hWnd, const CString& sText) {
	if (!::OpenClipboard(hWnd)) return false;
	::EmptyClipboard();
	size_t nBytes = (sText.GetLength() + 1) * sizeof(TCHAR);
	HGLOBAL hText = ::GlobalAlloc(GMEM_MOVEABLE, nBytes);
	if (hText != NULL) {
		memcpy(::GlobalLock(hText), (LPCTSTR)sText, nBytes);
		::GlobalUnlock(hText);
		::SetClipboardData(CF_UNICODETEXT, hText);
	}
	::CloseClipboard();
	return hText != NULL;
}

// Copies 32 bpp pixels (top down) as 24 bpp DIB, understood by all applications
static bool CopyPixelsToClipboard(HWND hWnd, const std::vector<DWORD>& pixels, int nWidth, int nHeight) {
	int nStride = (nWidth * 3 + 3) & ~3;
	HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + (size_t)nStride * nHeight);
	if (hMem == NULL) return false;
	BYTE* pData = (BYTE*)::GlobalLock(hMem);
	BITMAPINFOHEADER* pHeader = (BITMAPINFOHEADER*)pData;
	memset(pHeader, 0, sizeof(BITMAPINFOHEADER));
	pHeader->biSize = sizeof(BITMAPINFOHEADER);
	pHeader->biWidth = nWidth;
	pHeader->biHeight = nHeight; // bottom up
	pHeader->biPlanes = 1;
	pHeader->biBitCount = 24;
	pHeader->biCompression = BI_RGB;
	pHeader->biSizeImage = nStride * nHeight;
	BYTE* pTarget = pData + sizeof(BITMAPINFOHEADER);
	for (int y = 0; y < nHeight; y++) {
		const DWORD* pSource = &pixels[(size_t)(nHeight - 1 - y) * nWidth];
		BYTE* pRow = pTarget + (size_t)y * nStride;
		for (int x = 0; x < nWidth; x++) {
			DWORD c = pSource[x];
			pRow[x * 3] = (BYTE)c;
			pRow[x * 3 + 1] = (BYTE)(c >> 8);
			pRow[x * 3 + 2] = (BYTE)(c >> 16);
		}
	}
	::GlobalUnlock(hMem);
	if (!::OpenClipboard(hWnd)) {
		::GlobalFree(hMem);
		return false;
	}
	::EmptyClipboard();
	::SetClipboardData(CF_DIB, hMem);
	::CloseClipboard();
	return true;
}

// Small text is recognized much better when enlarged, the OCR engine wants text lines of 20 pixels and more
static bool RecognizeText(const std::vector<DWORD>& pixels, int nWidth, int nHeight, CString& sText, CString& sError) {
	int nFactor = (nHeight < 300) ? 3 : (nHeight < 900) ? 2 : 1;
	while (nFactor > 1 && (nWidth * nFactor > 6000 || nHeight * nFactor > 6000)) nFactor--;
	std::vector<DWORD> scaled;
	const DWORD* pSource = &pixels[0];
	int nScaledWidth = nWidth * nFactor, nScaledHeight = nHeight * nFactor;
	if (nFactor > 1) {
		scaled.resize((size_t)nScaledWidth * nScaledHeight);
		Gdiplus::Bitmap source(nWidth, nHeight, nWidth * 4, PixelFormat32bppRGB, (BYTE*)&pixels[0]);
		Gdiplus::Bitmap target(nScaledWidth, nScaledHeight, nScaledWidth * 4, PixelFormat32bppRGB, (BYTE*)&scaled[0]);
		Gdiplus::Graphics graphics(&target);
		graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
		graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
		graphics.DrawImage(&source, Gdiplus::Rect(0, 0, nScaledWidth, nScaledHeight), 0, 0, nWidth, nHeight, Gdiplus::UnitPixel);
		pSource = &scaled[0];
	}
	std::wstring sResult, sResultError;
	bool bOK = Ocr::RecognizeText(pSource, nScaledWidth, nScaledHeight, 4, sResult, sResultError);
	sText = sResult.c_str();
	sText.Replace(_T("\r\n"), _T("\n"));
	sText.Replace(_T("\n"), _T("\r\n"));
	sText.TrimRight();
	sError = sResultError.c_str();
	return bOK;
}

// Words with their rectangles in the given pixels
static bool RecognizeWords(const std::vector<DWORD>& pixels, int nWidth, int nHeight, std::vector<Ocr::CWord>& words, CString& sError) {
	int nFactor = (nHeight < 300) ? 3 : (nHeight < 900) ? 2 : 1;
	while (nFactor > 1 && (nWidth * nFactor > 6000 || nHeight * nFactor > 6000)) nFactor--;
	std::vector<DWORD> scaled;
	const DWORD* pSource = &pixels[0];
	int nScaledWidth = nWidth * nFactor, nScaledHeight = nHeight * nFactor;
	if (nFactor > 1) {
		scaled.resize((size_t)nScaledWidth * nScaledHeight);
		Gdiplus::Bitmap source(nWidth, nHeight, nWidth * 4, PixelFormat32bppRGB, (BYTE*)&pixels[0]);
		Gdiplus::Bitmap target(nScaledWidth, nScaledHeight, nScaledWidth * 4, PixelFormat32bppRGB, (BYTE*)&scaled[0]);
		Gdiplus::Graphics graphics(&target);
		graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
		graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
		graphics.DrawImage(&source, Gdiplus::Rect(0, 0, nScaledWidth, nScaledHeight), 0, 0, nWidth, nHeight, Gdiplus::UnitPixel);
		pSource = &scaled[0];
	}
	std::wstring sResultError;
	bool bOK = Ocr::RecognizeWords(pSource, nScaledWidth, nScaledHeight, 4, words, sResultError);
	for (Ocr::CWord& word : words) {
		word.Left /= nFactor;
		word.Top /= nFactor;
		word.Width /= nFactor;
		word.Height /= nFactor;
	}
	sError = sResultError.c_str();
	return bOK;
}

static HFONT CreateFontForDpi(LPCTSTR sFace, int nPoints96, int nWeight, UINT nDpi) {
	return ::CreateFont(-::MulDiv(nPoints96, nDpi, 96), 0, 0, 0, nWeight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, sFace);
}

static bool FontExists(LPCTSTR sFace) {
	HDC hDC = ::GetDC(NULL);
	LOGFONT logFont = { 0 };
	logFont.lfCharSet = DEFAULT_CHARSET;
	_tcsncpy_s(logFont.lfFaceName, sFace, LF_FACESIZE - 1);
	bool bFound = false;
	::EnumFontFamiliesEx(hDC, &logFont, [](const LOGFONT*, const TEXTMETRIC*, DWORD, LPARAM lParam) -> int {
		*(bool*)lParam = true;
		return 0;
	}, (LPARAM)&bFound, 0);
	::ReleaseDC(NULL, hDC);
	return bFound;
}

static void FillRound(HDC hDC, const CRect& rect, COLORREF color, int nRadius, COLORREF border = CLR_INVALID) {
	HBRUSH hBrush = ::CreateSolidBrush(color);
	HPEN hPen = ::CreatePen(PS_SOLID, 1, border == CLR_INVALID ? color : border);
	HGDIOBJ hOldBrush = ::SelectObject(hDC, hBrush);
	HGDIOBJ hOldPen = ::SelectObject(hDC, hPen);
	::RoundRect(hDC, rect.left, rect.top, rect.right, rect.bottom, nRadius, nRadius);
	::SelectObject(hDC, hOldBrush);
	::SelectObject(hDC, hOldPen);
	::DeleteObject(hBrush);
	::DeleteObject(hPen);
}

static void FrameRect(HDC hDC, const CRect& rect, COLORREF color, int nWidth) {
	HBRUSH hBrush = ::CreateSolidBrush(color);
	for (int i = 0; i < nWidth; i++) {
		CRect r(rect.left - i, rect.top - i, rect.right + i, rect.bottom + i);
		::FrameRect(hDC, &r, hBrush);
	}
	::DeleteObject(hBrush);
}

///////////////////////////////////////////////////////////////////////////////////
// Toast and text result windows
///////////////////////////////////////////////////////////////////////////////////

static LRESULT CALLBACK ToastWndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
	switch (nMsg) {
		case WM_TIMER:
			::DestroyWindow(hWnd);
			return 0;
		case WM_LBUTTONDOWN:
			::DestroyWindow(hWnd);
			return 0;
		case WM_PAINT: {
			PAINTSTRUCT ps;
			HDC hDC = ::BeginPaint(hWnd, &ps);
			CRect rect;
			::GetClientRect(hWnd, &rect);
			HBRUSH hBrush = ::CreateSolidBrush(RGB(36, 36, 36));
			::FillRect(hDC, &rect, hBrush);
			::DeleteObject(hBrush);
			HFONT hFont = (HFONT)::GetWindowLongPtr(hWnd, GWLP_USERDATA);
			HGDIOBJ hOldFont = ::SelectObject(hDC, hFont);
			::SetBkMode(hDC, TRANSPARENT);
			::SetTextColor(hDC, RGB(240, 240, 240));
			TCHAR sText[512];
			::GetWindowText(hWnd, sText, 512);
			::DrawText(hDC, sText, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
			::SelectObject(hDC, hOldFont);
			::EndPaint(hWnd, &ps);
			return 0;
		}
		case WM_DESTROY: {
			HFONT hFont = (HFONT)::GetWindowLongPtr(hWnd, GWLP_USERDATA);
			if (hFont != NULL) ::DeleteObject(hFont);
			return 0;
		}
	}
	return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

// Short message near the bottom of the monitor with the mouse, disappears after a while.
// bWait: runs a message loop until it is gone (process without other windows).
static void ShowToast(LPCTSTR sText, bool bWait) {
	CPerMonitorDpiScope dpiScope;
	WNDCLASS wndClass = { 0 };
	wndClass.lpfnWndProc = ToastWndProc;
	wndClass.hInstance = ::GetModuleHandle(NULL);
	wndClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	wndClass.lpszClassName = _T("LucidViewerToast");
	::RegisterClass(&wndClass);
	POINT cursor;
	::GetCursorPos(&cursor);
	HMONITOR hMonitor = ::MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
	MONITORINFO info = { sizeof(MONITORINFO) };
	::GetMonitorInfo(hMonitor, &info);
	UINT nDpi = GetMonitorDpi(hMonitor);
	HFONT hFont = CreateFontForDpi(_T("Segoe UI"), 15, FW_NORMAL, nDpi);
	HDC hDC = ::GetDC(NULL);
	HGDIOBJ hOldFont = ::SelectObject(hDC, hFont);
	CRect textRect(0, 0, 0, 0);
	::DrawText(hDC, sText, -1, &textRect, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
	::SelectObject(hDC, hOldFont);
	::ReleaseDC(NULL, hDC);
	int nWidth = min(textRect.Width() + ::MulDiv(40, nDpi, 96), (int)(info.rcWork.right - info.rcWork.left) - 20);
	int nHeight = ::MulDiv(44, nDpi, 96);
	int nX = (info.rcWork.left + info.rcWork.right - nWidth) / 2;
	int nY = info.rcWork.bottom - nHeight - ::MulDiv(40, nDpi, 96);
	HWND hWnd = ::CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED, _T("LucidViewerToast"), sText,
		WS_POPUP, nX, nY, nWidth, nHeight, NULL, NULL, wndClass.hInstance, NULL);
	if (hWnd == NULL) {
		::DeleteObject(hFont);
		return;
	}
	::SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)hFont);
	::SetLayeredWindowAttributes(hWnd, 0, 235, LWA_ALPHA);
	// rounded corners on Windows 11
	int nCorner = 2; // DWMWCP_ROUND
	::DwmSetWindowAttribute(hWnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &nCorner, sizeof(nCorner));
	::ShowWindow(hWnd, SW_SHOWNOACTIVATE);
	::SetTimer(hWnd, 1, 2500, NULL);
	if (bWait) {
		MSG msg;
		while (::IsWindow(hWnd) && ::GetMessage(&msg, NULL, 0, 0) > 0) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
	}
}

struct CTextWindow {
	HWND Edit;
	bool Done;
};

static LRESULT CALLBACK TextWndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
	CTextWindow* pWindow = (CTextWindow*)::GetWindowLongPtr(hWnd, GWLP_USERDATA);
	switch (nMsg) {
		case WM_CREATE:
			::SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCT*)lParam)->lpCreateParams);
			return 0;
		case WM_COMMAND:
			if (pWindow != NULL && LOWORD(wParam) == IDOK) {
				// copies the selection or all text
				DWORD nStart = 0, nEnd = 0;
				::SendMessage(pWindow->Edit, EM_GETSEL, (WPARAM)&nStart, (LPARAM)&nEnd);
				int nLength = ::GetWindowTextLength(pWindow->Edit);
				CString sText;
				::GetWindowText(pWindow->Edit, sText.GetBuffer(nLength + 1), nLength + 1);
				sText.ReleaseBuffer();
				if (nEnd > nStart) sText = sText.Mid(nStart, nEnd - nStart);
				CopyTextToClipboard(hWnd, sText);
				return 0;
			}
			if (pWindow != NULL && LOWORD(wParam) == IDCANCEL) {
				pWindow->Done = true;
				::PostMessage(hWnd, WM_NULL, 0, 0);
				return 0;
			}
			break;
		case WM_SIZE:
			if (pWindow != NULL) {
				int nDpi = GetMonitorDpi(::MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST));
				auto S = [nDpi](int n) { return ::MulDiv(n, nDpi, 96); };
				int nW = LOWORD(lParam), nH = HIWORD(lParam);
				::MoveWindow(pWindow->Edit, S(10), S(10), nW - S(20), nH - S(62), TRUE);
				::MoveWindow(::GetDlgItem(hWnd, IDOK), nW - S(10) - 2 * S(110) - S(8), nH - S(42), S(110), S(30), TRUE);
				::MoveWindow(::GetDlgItem(hWnd, IDCANCEL), nW - S(10) - S(110), nH - S(42), S(110), S(30), TRUE);
			}
			return 0;
		case WM_CLOSE:
			if (pWindow != NULL) {
				pWindow->Done = true;
				::PostMessage(hWnd, WM_NULL, 0, 0);
			}
			return 0;
	}
	return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

// The recognized text, can be edited and partly copied
static void ShowTextWindow(const CString& sText) {
	CPerMonitorDpiScope dpiScope;
	HINSTANCE hInstance = ::GetModuleHandle(NULL);
	WNDCLASS wndClass = { 0 };
	wndClass.lpfnWndProc = TextWndProc;
	wndClass.hInstance = hInstance;
	wndClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	wndClass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wndClass.hIcon = ::LoadIcon(hInstance, MAKEINTRESOURCE(IDR_MAINFRAME));
	wndClass.lpszClassName = _T("LucidViewerOcrText");
	::RegisterClass(&wndClass);
	POINT cursor;
	::GetCursorPos(&cursor);
	HMONITOR hMonitor = ::MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
	MONITORINFO info = { sizeof(MONITORINFO) };
	::GetMonitorInfo(hMonitor, &info);
	UINT nDpi = GetMonitorDpi(hMonitor);
	int nWidth = ::MulDiv(560, nDpi, 96), nHeight = ::MulDiv(420, nDpi, 96);
	CTextWindow window = { NULL, false };
	HWND hWnd = ::CreateWindowEx(WS_EX_TOPMOST, _T("LucidViewerOcrText"), CNLS::GetString(_T("Recognized text")), WS_OVERLAPPEDWINDOW,
		(info.rcWork.left + info.rcWork.right - nWidth) / 2, (info.rcWork.top + info.rcWork.bottom - nHeight) / 2, nWidth, nHeight,
		NULL, NULL, hInstance, &window);
	if (hWnd == NULL) return;
	HFONT hFont = CreateFontForDpi(_T("Segoe UI"), 15, FW_NORMAL, nDpi);
	window.Edit = ::CreateWindowEx(WS_EX_CLIENTEDGE, _T("EDIT"), sText, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
		0, 0, 10, 10, hWnd, NULL, hInstance, NULL);
	HWND hCopy = ::CreateWindow(_T("BUTTON"), CNLS::GetString(_T("Copy")), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 10, 10, hWnd, (HMENU)IDOK, hInstance, NULL);
	HWND hClose = ::CreateWindow(_T("BUTTON"), CNLS::GetString(_T("Close")), WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10, hWnd, (HMENU)IDCANCEL, hInstance, NULL);
	for (HWND hControl : { window.Edit, hCopy, hClose }) ::SendMessage(hControl, WM_SETFONT, (WPARAM)hFont, FALSE);
	CRect clientRect;
	::GetClientRect(hWnd, &clientRect);
	::SendMessage(hWnd, WM_SIZE, 0, MAKELPARAM(clientRect.Width(), clientRect.Height()));
	::ShowWindow(hWnd, SW_SHOWNORMAL);
	::SetForegroundWindow(hWnd);
	::SetFocus(window.Edit);
	::SendMessage(window.Edit, EM_SETSEL, 0, 0);
	MSG msg;
	while (!window.Done && ::GetMessage(&msg, NULL, 0, 0) > 0) {
		if (!::IsDialogMessage(hWnd, &msg)) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
	}
	::DestroyWindow(hWnd);
	::DeleteObject(hFont);
}

///////////////////////////////////////////////////////////////////////////////////
// Captured screen
///////////////////////////////////////////////////////////////////////////////////

struct CScreen {
	int Left, Top, Width, Height; // virtual screen in physical pixels
	HBITMAP Bitmap, DarkBitmap;
	DWORD* Pixels;
	DWORD* DarkPixels;
	std::vector<CRect> Windows;  // visible top level windows, top first, in screen bitmap coordinates
	std::vector<CRect> Monitors; // in screen bitmap coordinates
	std::vector<CRect> WorkAreas;

	CScreen() : Left(0), Top(0), Width(0), Height(0), Bitmap(NULL), DarkBitmap(NULL), Pixels(NULL), DarkPixels(NULL) {}
	~CScreen() {
		if (Bitmap != NULL) ::DeleteObject(Bitmap);
		if (DarkBitmap != NULL) ::DeleteObject(DarkBitmap);
	}
};

static BOOL CALLBACK EnumMonitorProc(HMONITOR hMonitor, HDC, LPRECT, LPARAM lParam) {
	CScreen* pScreen = (CScreen*)lParam;
	MONITORINFO info = { sizeof(MONITORINFO) };
	if (::GetMonitorInfo(hMonitor, &info)) {
		CRect monitor(info.rcMonitor), work(info.rcWork);
		monitor.OffsetRect(-pScreen->Left, -pScreen->Top);
		work.OffsetRect(-pScreen->Left, -pScreen->Top);
		pScreen->Monitors.push_back(monitor);
		pScreen->WorkAreas.push_back(work);
	}
	return TRUE;
}

static BOOL CALLBACK EnumWindowProc(HWND hWnd, LPARAM lParam) {
	CScreen* pScreen = (CScreen*)lParam;
	if (!::IsWindowVisible(hWnd) || ::IsIconic(hWnd)) return TRUE;
	int nCloaked = 0;
	if (SUCCEEDED(::DwmGetWindowAttribute(hWnd, 14 /* DWMWA_CLOAKED */, &nCloaked, sizeof(nCloaked))) && nCloaked != 0) return TRUE;
	TCHAR sClass[64];
	::GetClassName(hWnd, sClass, 64);
	if (_tcscmp(sClass, _T("Progman")) == 0 || _tcscmp(sClass, _T("WorkerW")) == 0) return TRUE; // desktop, the monitor is used instead
	if ((::GetWindowLong(hWnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT) != 0) return TRUE;
	RECT rect;
	if (FAILED(::DwmGetWindowAttribute(hWnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect)))) {
		::GetWindowRect(hWnd, &rect);
	}
	CRect windowRect(rect);
	windowRect.OffsetRect(-pScreen->Left, -pScreen->Top);
	windowRect.IntersectRect(&windowRect, CRect(0, 0, pScreen->Width, pScreen->Height));
	if (windowRect.Width() >= 20 && windowRect.Height() >= 20) {
		pScreen->Windows.push_back(windowRect);
	}
	return TRUE;
}

static bool CaptureScreen(CScreen& screen) {
	screen.Left = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
	screen.Top = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
	screen.Width = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
	screen.Height = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
	if (screen.Width <= 0 || screen.Height <= 0) return false;
	BITMAPINFO bmi = { 0 };
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = screen.Width;
	bmi.bmiHeader.biHeight = -screen.Height; // top down
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;
	HDC hScreenDC = ::GetDC(NULL);
	screen.Bitmap = ::CreateDIBSection(hScreenDC, &bmi, DIB_RGB_COLORS, (void**)&screen.Pixels, NULL, 0);
	screen.DarkBitmap = ::CreateDIBSection(hScreenDC, &bmi, DIB_RGB_COLORS, (void**)&screen.DarkPixels, NULL, 0);
	bool bOK = false;
	if (screen.Bitmap != NULL && screen.DarkBitmap != NULL) {
		HDC hMemDC = ::CreateCompatibleDC(hScreenDC);
		HGDIOBJ hOld = ::SelectObject(hMemDC, screen.Bitmap);
		bOK = ::BitBlt(hMemDC, 0, 0, screen.Width, screen.Height, hScreenDC, screen.Left, screen.Top, SRCCOPY | CAPTUREBLT) != FALSE;
		::SelectObject(hMemDC, hOld);
		::DeleteDC(hMemDC);
		::GdiFlush();
		size_t nPixels = (size_t)screen.Width * screen.Height;
		for (size_t i = 0; i < nPixels; i++) {
			DWORD c = screen.Pixels[i] | 0xFF000000;
			screen.Pixels[i] = c;
			screen.DarkPixels[i] = ((c >> 1) & 0x007F7F7F) - ((c >> 3) & 0x001F1F1F) | 0xFF000000; // 37.5 % brightness
		}
	}
	::ReleaseDC(NULL, hScreenDC);
	::EnumDisplayMonitors(NULL, NULL, EnumMonitorProc, (LPARAM)&screen);
	::EnumWindows(EnumWindowProc, (LPARAM)&screen);
	return bOK;
}

///////////////////////////////////////////////////////////////////////////////////
// Overlay window with selection and tools
///////////////////////////////////////////////////////////////////////////////////

class COverlay {
public:
	enum EResult { RESULT_NONE, RESULT_OCR };

	COverlay(CScreen& screen, EMode eMode);
	~COverlay();

	// Shows the overlay until the user closes it, returns RESULT_OCR with the cropped pixels if text shall be recognized
	EResult Run(std::vector<DWORD>& cropPixels, int& nCropWidth, int& nCropHeight);

private:
	enum EButton { BTN_PEN = 1, BTN_HIGHLIGHTER, BTN_RECTANGLE, BTN_ARROW, BTN_TEXT, BTN_PIXELATE, BTN_COLOR, BTN_WIDTH, BTN_UNDO,
		BTN_OCR, BTN_OPEN, BTN_SAVE, BTN_COPY, BTN_CLOSE, BTN_COPY_TEXT, BTN_SWATCH };
	enum EDrag { DRAG_NONE, DRAG_NEW, DRAG_MOVE, DRAG_RESIZE, DRAG_DRAW, DRAG_TEXT };
	struct CTextWord {
		CRect Rect; // screen bitmap coordinates
		CString Text;
		int Line;
	};
	struct CButton {
		EButton Id;
		wchar_t Glyph;
		LPCTSTR Text;    // shown if there is no icon font or no glyph
		LPCTSTR Tooltip; // English, translated with CNLS
		CRect Rect;
		COLORREF Swatch;
	};

	CScreen& m_screen;
	EMode m_eMode;
	HWND m_hWnd;
	HDC m_hMemDC, m_hBrightDC, m_hDarkDC;
	HBITMAP m_hMemBitmap;
	HGDIOBJ m_hOldMem, m_hOldBright, m_hOldDark;
	HFONT m_hFont, m_hSmallFont, m_hIconFont;
	bool m_bHasIconFont;
	UINT m_nDpi;
	bool m_bDone;
	EResult m_eResult;

	bool m_bHasSelection;
	CRect m_selection;
	CRect m_hoverWindow;
	EDrag m_eDrag;
	int m_nResizeEdges; // 1 left, 2 top, 4 right, 8 bottom
	CPoint m_dragStart;
	CRect m_selectionStart;
	CAnnotations m_annotations;
	std::vector<CButton> m_buttons;
	int m_nHoverButton;
	bool m_bColorPopup;
	CRect m_lastUIBounds;
	CPoint m_mouse;
	// text layer: the recognized words over the selection, they can be selected and copied
	bool m_bTextLayer;
	std::vector<CTextWord> m_words;
	int m_nTextStart, m_nTextEnd; // selected words, -1 if none
	CString m_sTextHint;
	void ShowTextLayer(bool bCopyAll);
	void HideTextLayer();
	int WordAt(const CPoint& pt, bool bNearest);
	CString GetSelectedText(bool bAllIfNone);
	void CopySelectedText();
	CRect GetTextHintRect();

	int S(int n) { return ::MulDiv(n, m_nDpi, 96); }
	static LRESULT CALLBACK WndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam);
	LRESULT HandleMessage(UINT nMsg, WPARAM wParam, LPARAM lParam);

	int FindMonitor(const CPoint& pt);
	void UpdateDpi();
	void Layout();
	CRect GetUIBounds();
	void UpdateWindow();
	void Paint(HDC hDC, const CRect& paintRect);
	void PaintButton(const CButton& button, bool bHover, bool bChecked);
	CRect GetHintRect();
	// Magnifier at the mouse while selecting: enlarged pixels, position and color
	bool IsLoupeShown() { return !m_bHasSelection || m_eDrag == DRAG_NEW; }
	CRect GetLoupeRect();
	void PaintLoupe(HDC hDC);
	CString GetHintText();
	int HitTestHandle(const CPoint& pt);
	int HitTestButton(const CPoint& pt);
	void SetCursorFor(const CPoint& pt);
	void OnButton(EButton eButton, int nIndex);
	void OnLButtonDown(CPoint pt, bool bDoubleClick);
	void OnMouseMove(CPoint pt);
	void OnLButtonUp(CPoint pt);
	void OnKeyDown(UINT nKey);
	void SelectionFinished();
	void GetCrop(std::vector<DWORD>& pixels, int& nWidth, int& nHeight);
	void Copy();
	void Save();
	void OpenInViewer();
	void Close(EResult eResult);
};

static const COLORREF COLOR_BAR = RGB(32, 32, 32);
static const COLORREF COLOR_BAR_BORDER = RGB(70, 70, 70);
static const COLORREF COLOR_HOVER = RGB(64, 64, 64);
static const COLORREF COLOR_ACCENT = RGB(0, 120, 215);
static const COLORREF COLOR_TEXT = RGB(240, 240, 240);
static const COLORREF s_palette[] = { RGB(230, 40, 40), RGB(255, 140, 0), RGB(250, 210, 0), RGB(40, 180, 70),
	RGB(30, 120, 230), RGB(150, 70, 200), RGB(0, 0, 0), RGB(255, 255, 255) };

COverlay::COverlay(CScreen& screen, EMode eMode) : m_screen(screen), m_eMode(eMode) {
	m_hWnd = NULL;
	m_hMemDC = m_hBrightDC = m_hDarkDC = NULL;
	m_hMemBitmap = NULL;
	m_hOldMem = m_hOldBright = m_hOldDark = NULL;
	m_hFont = m_hSmallFont = m_hIconFont = NULL;
	m_bHasIconFont = false;
	m_nDpi = 0;
	m_bDone = false;
	m_eResult = RESULT_NONE;
	m_bHasSelection = false;
	m_eDrag = DRAG_NONE;
	m_nResizeEdges = 0;
	m_nHoverButton = -1;
	m_bColorPopup = false;
	m_bTextLayer = false;
	m_nTextStart = m_nTextEnd = -1;
	m_annotations.SetTool(CAnnotations::TOOL_NONE);
	m_annotations.SetWidth(3);
}

COverlay::~COverlay() {
	if (m_hMemDC != NULL) {
		::SelectObject(m_hMemDC, m_hOldMem);
		::DeleteDC(m_hMemDC);
		::DeleteObject(m_hMemBitmap);
	}
	if (m_hBrightDC != NULL) {
		::SelectObject(m_hBrightDC, m_hOldBright);
		::DeleteDC(m_hBrightDC);
	}
	if (m_hDarkDC != NULL) {
		::SelectObject(m_hDarkDC, m_hOldDark);
		::DeleteDC(m_hDarkDC);
	}
	for (HFONT hFont : { m_hFont, m_hSmallFont, m_hIconFont }) {
		if (hFont != NULL) ::DeleteObject(hFont);
	}
}

COverlay::EResult COverlay::Run(std::vector<DWORD>& cropPixels, int& nCropWidth, int& nCropHeight) {
	HINSTANCE hInstance = ::GetModuleHandle(NULL);
	WNDCLASS wndClass = { 0 };
	wndClass.style = CS_DBLCLKS;
	wndClass.lpfnWndProc = WndProc;
	wndClass.hInstance = hInstance;
	wndClass.hCursor = NULL; // set in WM_SETCURSOR
	wndClass.lpszClassName = _T("LucidViewerScreenCapture");
	::RegisterClass(&wndClass);

	HDC hScreenDC = ::GetDC(NULL);
	m_hMemDC = ::CreateCompatibleDC(hScreenDC);
	m_hMemBitmap = ::CreateCompatibleBitmap(hScreenDC, m_screen.Width, m_screen.Height);
	m_hOldMem = ::SelectObject(m_hMemDC, m_hMemBitmap);
	m_hBrightDC = ::CreateCompatibleDC(hScreenDC);
	m_hOldBright = ::SelectObject(m_hBrightDC, m_screen.Bitmap);
	m_hDarkDC = ::CreateCompatibleDC(hScreenDC);
	m_hOldDark = ::SelectObject(m_hDarkDC, m_screen.DarkBitmap);
	::ReleaseDC(NULL, hScreenDC);
	m_bHasIconFont = FontExists(_T("Segoe Fluent Icons")) || FontExists(_T("Segoe MDL2 Assets"));
	::GetCursorPos(&m_mouse);
	m_mouse.Offset(-m_screen.Left, -m_screen.Top);
	UpdateDpi();

	m_hWnd = ::CreateWindowEx(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, _T("LucidViewerScreenCapture"), CNLS::GetString(_T("Screenshot")), WS_POPUP,
		m_screen.Left, m_screen.Top, m_screen.Width, m_screen.Height, NULL, NULL, hInstance, this);
	if (m_hWnd == NULL) {
		return RESULT_NONE;
	}
	OnMouseMove(m_mouse);
	::ShowWindow(m_hWnd, SW_SHOW);
	::SetForegroundWindow(m_hWnd);
	::SetFocus(m_hWnd);
	MSG msg;
	while (!m_bDone && ::GetMessage(&msg, NULL, 0, 0) > 0) {
		::TranslateMessage(&msg);
		::DispatchMessage(&msg);
	}
	if (m_eResult == RESULT_OCR) {
		GetCrop(cropPixels, nCropWidth, nCropHeight);
	}
	::DestroyWindow(m_hWnd);
	m_hWnd = NULL;
	return m_eResult;
}

LRESULT CALLBACK COverlay::WndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
	COverlay* pThis = (COverlay*)::GetWindowLongPtr(hWnd, GWLP_USERDATA);
	if (nMsg == WM_NCCREATE) {
		pThis = (COverlay*)((CREATESTRUCT*)lParam)->lpCreateParams;
		::SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pThis);
		pThis->m_hWnd = hWnd;
	}
	if (pThis != NULL) {
		return pThis->HandleMessage(nMsg, wParam, lParam);
	}
	return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

LRESULT COverlay::HandleMessage(UINT nMsg, WPARAM wParam, LPARAM lParam) {
	switch (nMsg) {
		case WM_ERASEBKGND:
			return 1;
		case WM_PAINT: {
			PAINTSTRUCT ps;
			HDC hDC = ::BeginPaint(m_hWnd, &ps);
			Paint(hDC, CRect(ps.rcPaint));
			::EndPaint(m_hWnd, &ps);
			return 0;
		}
		case WM_SETCURSOR:
			if (LOWORD(lParam) == HTCLIENT) {
				SetCursorFor(m_mouse);
				return TRUE;
			}
			break;
		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
			OnLButtonDown(CPoint((short)LOWORD(lParam), (short)HIWORD(lParam)), nMsg == WM_LBUTTONDBLCLK);
			return 0;
		case WM_MOUSEMOVE:
			OnMouseMove(CPoint((short)LOWORD(lParam), (short)HIWORD(lParam)));
			return 0;
		case WM_LBUTTONUP:
			OnLButtonUp(CPoint((short)LOWORD(lParam), (short)HIWORD(lParam)));
			return 0;
		case WM_RBUTTONDOWN:
			// right click: removes the selection, closes if there is none
			if (m_bHasSelection && m_annotations.IsEmpty() && m_eDrag == DRAG_NONE) {
				m_bHasSelection = false;
				m_bColorPopup = false;
				OnMouseMove(m_mouse);
				UpdateWindow();
			} else if (!m_bHasSelection) {
				Close(RESULT_NONE);
			}
			return 0;
		case WM_KEYDOWN:
			OnKeyDown((UINT)wParam);
			return 0;
		case WM_CLOSE:
			Close(RESULT_NONE);
			return 0;
	}
	return ::DefWindowProc(m_hWnd, nMsg, wParam, lParam);
}

int COverlay::FindMonitor(const CPoint& pt) {
	for (int i = 0; i < (int)m_screen.Monitors.size(); i++) {
		if (m_screen.Monitors[i].PtInRect(pt)) return i;
	}
	return 0;
}

void COverlay::UpdateDpi() {
	// DPI of the monitor with the selection (or the mouse), the tool bars are sized for it
	CPoint pt = m_bHasSelection ? m_selection.TopLeft() : m_mouse;
	POINT screenPt = { pt.x + m_screen.Left, pt.y + m_screen.Top };
	UINT nDpi = GetMonitorDpi(::MonitorFromPoint(screenPt, MONITOR_DEFAULTTONEAREST));
	if (nDpi == m_nDpi) return;
	m_nDpi = nDpi;
	for (HFONT hFont : { m_hFont, m_hSmallFont, m_hIconFont }) {
		if (hFont != NULL) ::DeleteObject(hFont);
	}
	m_hFont = CreateFontForDpi(_T("Segoe UI"), 14, FW_NORMAL, m_nDpi);
	m_hSmallFont = CreateFontForDpi(_T("Segoe UI"), 12, FW_SEMIBOLD, m_nDpi);
	m_hIconFont = CreateFontForDpi(FontExists(_T("Segoe Fluent Icons")) ? _T("Segoe Fluent Icons") : _T("Segoe MDL2 Assets"), 16, FW_NORMAL, m_nDpi);
}

void COverlay::Layout() {
	m_buttons.clear();
	if (!m_bHasSelection || (m_eMode == MODE_TEXT && !m_bTextLayer)) {
		return;
	}
	UpdateDpi();
	int nButton = S(34), nGap = S(4), nPad = S(4);
	int nMonitor = FindMonitor(m_selection.CenterPoint());
	CRect work = m_screen.Monitors.empty() ? CRect(0, 0, m_screen.Width, m_screen.Height) : m_screen.Monitors[nMonitor];

	// drawing tools: vertical bar right of the selection, else left of it, else inside
	struct { EButton Id; wchar_t Glyph; LPCTSTR Text; LPCTSTR Tooltip; } tools[] = {
		{ BTN_PEN, 0xE76D, _T("P"), _T("Pen") },
		{ BTN_HIGHLIGHTER, 0xE7E6, _T("H"), _T("Highlighter") },
		{ BTN_RECTANGLE, 0xE739, _T("R"), _T("Rectangle") },
		{ BTN_ARROW, 0xE72A, _T("A"), _T("Arrow") },
		{ BTN_TEXT, 0xE8D2, _T("T"), _T("Text") },
		{ BTN_PIXELATE, 0, _T("\x25A6"), _T("Pixelate (hide an area)") },
		{ BTN_COLOR, 0, _T(""), _T("Color") },
		{ BTN_WIDTH, 0, _T(""), _T("Line width") },
		{ BTN_UNDO, 0xE7A7, _T("U"), _T("Undo (Ctrl+Z)") },
	};
	int nToolCount = (m_bTextLayer || m_eMode == MODE_TEXT) ? 0 : sizeof(tools) / sizeof(tools[0]);
	int nBarHeight = nToolCount * nButton + (nToolCount - 1) * nGap + 2 * nPad;
	int nBarWidth = nButton + 2 * nPad;
	int nX = m_selection.right + S(8);
	if (nX + nBarWidth > work.right) nX = m_selection.left - S(8) - nBarWidth;
	if (nX < work.left) nX = m_selection.right - nBarWidth - S(8);
	int nY = max((int)work.top + S(8), min((int)m_selection.top, (int)work.bottom - nBarHeight - S(8)));
	for (int i = 0; i < nToolCount; i++) {
		CButton button = { tools[i].Id, tools[i].Glyph, tools[i].Text, tools[i].Tooltip,
			CRect(nX + nPad, nY + nPad + i * (nButton + nGap), nX + nPad + nButton, nY + nPad + i * (nButton + nGap) + nButton), CLR_INVALID };
		m_buttons.push_back(button);
	}

	// actions: horizontal bar below the selection (right aligned), else above, else inside
	struct CActionDef { EButton Id; wchar_t Glyph; LPCTSTR Text; LPCTSTR Tooltip; };
	std::vector<CActionDef> actions;
	if (m_eMode == MODE_CAPTURE) {
		actions.push_back({ BTN_OCR, 0, _T("OCR"), _T("Show the text in the selection, it can be selected and copied (Ctrl+T)") });
	}
	if (m_bTextLayer) {
		actions.push_back({ BTN_COPY_TEXT, 0, _T("Copy all text"), _T("Copies all recognized text") });
	}
	if (m_eMode == MODE_CAPTURE) {
		actions.push_back({ BTN_OPEN, 0xE8E5, _T("O"), _T("Open in Lucid Viewer") });
		actions.push_back({ BTN_SAVE, 0xE74E, _T("S"), _T("Save (Ctrl+S)") });
		actions.push_back({ BTN_COPY, 0xE8C8, _T("C"), _T("Copy (Ctrl+C)") });
	}
	actions.push_back({ BTN_CLOSE, 0xE711, _T("X"), _T("Close (Esc)") });
	int nActionCount = (int)actions.size();
	int nOcrWidth = S(48);
	// text buttons are as wide as their text
	auto ButtonWidth = [&](const CActionDef& action) {
		if (action.Id == BTN_OCR) return nOcrWidth;
		if (action.Glyph != 0) return nButton;
		HDC hDC = ::GetDC(NULL);
		HGDIOBJ hOld = ::SelectObject(hDC, m_hSmallFont);
		CRect textRect(0, 0, 0, 0);
		::DrawText(hDC, CNLS::GetString(action.Text), -1, &textRect, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
		::SelectObject(hDC, hOld);
		::ReleaseDC(NULL, hDC);
		return (int)textRect.Width() + S(20);
	};
	int nActionWidth = (nActionCount - 1) * nGap + 2 * nPad;
	for (const CActionDef& action : actions) nActionWidth += ButtonWidth(action);
	int nActionHeight = nButton + 2 * nPad;
	nX = max((int)work.left + S(8), min((int)m_selection.right - nActionWidth, (int)work.right - nActionWidth - S(8)));
	nY = m_selection.bottom + S(8);
	if (nY + nActionHeight > work.bottom) nY = m_selection.top - S(8) - nActionHeight;
	if (nY < work.top) nY = m_selection.bottom - nActionHeight - S(8);
	int nButtonX = nX + nPad;
	for (int i = 0; i < nActionCount; i++) {
		int nWidth = ButtonWidth(actions[i]);
		CButton button = { actions[i].Id, actions[i].Glyph, actions[i].Text, actions[i].Tooltip,
			CRect(nButtonX, nY + nPad, nButtonX + nWidth, nY + nPad + nButton), CLR_INVALID };
		m_buttons.push_back(button);
		nButtonX += nWidth + nGap;
	}

	// color swatches left (or right) of the color button
	if (m_bColorPopup) {
		CRect colorRect;
		for (const CButton& button : m_buttons) if (button.Id == BTN_COLOR) colorRect = button.Rect;
		int nSwatch = S(26);
		int nCount = sizeof(s_palette) / sizeof(s_palette[0]);
		int nWidth = nCount * nSwatch + (nCount - 1) * nGap;
		int nStartX = colorRect.left - S(12) - nWidth;
		if (nStartX < work.left) nStartX = colorRect.right + S(12);
		for (int i = 0; i < nCount; i++) {
			int nSwatchX = nStartX + i * (nSwatch + nGap);
			int nSwatchY = colorRect.top + (colorRect.Height() - nSwatch) / 2;
			CButton button = { BTN_SWATCH, 0, _T(""), NULL, CRect(nSwatchX, nSwatchY, nSwatchX + nSwatch, nSwatchY + nSwatch), s_palette[i] };
			m_buttons.push_back(button);
		}
	}
}

CRect COverlay::GetHintRect() {
	int nMonitor = FindMonitor(m_mouse);
	CRect monitor = m_screen.Monitors.empty() ? CRect(0, 0, m_screen.Width, m_screen.Height) : m_screen.Monitors[nMonitor];
	int nWidth = S(560), nHeight = S(34);
	int nX = (monitor.left + monitor.right - nWidth) / 2;
	return CRect(nX, monitor.top + S(24), nX + nWidth, monitor.top + S(24) + nHeight);
}

CString COverlay::GetHintText() {
	return CNLS::GetString(m_eMode == MODE_TEXT ? _T("Select the area with the text, click for a window, Ctrl+C for the whole screen. Esc cancels.") :
		_T("Drag to select an area, click for a window, Ctrl+C or Ctrl+S for the whole screen. Esc cancels."));
}

CRect COverlay::GetUIBounds() {
	CRect bounds(0, 0, 0, 0);
	CRect bright = m_bHasSelection ? m_selection : m_hoverWindow;
	if (!bright.IsRectEmpty()) {
		bounds = bright;
		bounds.InflateRect(S(6), S(34), S(6), S(6)); // handles, border and size label above
	}
	for (const CButton& button : m_buttons) {
		CRect buttonRect = button.Rect;
		buttonRect.InflateRect(S(8), S(8));
		bounds.UnionRect(&bounds, &buttonRect);
	}
	if (m_nHoverButton >= 0) {
		CRect tooltipRect = m_buttons[m_nHoverButton].Rect;
		tooltipRect.InflateRect(S(320), S(48));
		bounds.UnionRect(&bounds, &tooltipRect);
	}
	if (!m_bHasSelection || m_eDrag == DRAG_NEW) {
		CRect hintRect = GetHintRect();
		hintRect.InflateRect(S(4), S(4));
		bounds.UnionRect(&bounds, &hintRect);
		CRect loupeRect = GetLoupeRect();
		loupeRect.InflateRect(S(4), S(4));
		bounds.UnionRect(&bounds, &loupeRect);
	}
	if (m_bTextLayer) {
		CRect textHint = GetTextHintRect();
		textHint.InflateRect(S(4), S(4));
		bounds.UnionRect(&bounds, &textHint);
	}
	return bounds;
}

void COverlay::UpdateWindow() {
	Layout();
	CRect bounds = GetUIBounds();
	CRect dirty;
	dirty.UnionRect(&bounds, &m_lastUIBounds);
	m_lastUIBounds = bounds;
	if (!dirty.IsRectEmpty()) {
		::InvalidateRect(m_hWnd, &dirty, FALSE);
	}
}

void COverlay::PaintButton(const CButton& button, bool bHover, bool bChecked) {
	HDC hDC = m_hMemDC;
	if (button.Id == BTN_SWATCH) {
		CRect rect = button.Rect;
		if (button.Swatch == m_annotations.GetColor()) {
			CRect outer = rect;
			outer.InflateRect(S(3), S(3));
			FillRound(hDC, outer, COLOR_ACCENT, S(8));
		}
		FillRound(hDC, rect, button.Swatch, S(6), RGB(90, 90, 90));
		return;
	}
	if (bChecked || bHover) {
		FillRound(hDC, button.Rect, bChecked ? COLOR_ACCENT : COLOR_HOVER, S(6));
	}
	CRect rect = button.Rect;
	if (button.Id == BTN_COLOR) {
		CRect swatch = rect;
		swatch.DeflateRect(S(8), S(8));
		FillRound(hDC, swatch, m_annotations.GetColor(), S(6), RGB(200, 200, 200));
		return;
	}
	if (button.Id == BTN_WIDTH) {
		// a line as thick as the drawings
		int nLine = max(1, S(m_annotations.GetWidth()));
		CRect line(rect.left + S(8), rect.CenterPoint().y - nLine / 2, rect.right - S(8), rect.CenterPoint().y - nLine / 2 + nLine);
		HBRUSH hBrush = ::CreateSolidBrush(COLOR_TEXT);
		::FillRect(hDC, &line, hBrush);
		::DeleteObject(hBrush);
		return;
	}
	::SetBkMode(hDC, TRANSPARENT);
	::SetTextColor(hDC, COLOR_TEXT);
	bool bGlyph = m_bHasIconFont && button.Glyph != 0;
	HGDIOBJ hOldFont = ::SelectObject(hDC, bGlyph ? m_hIconFont : m_hSmallFont);
	wchar_t glyph[2] = { button.Glyph, 0 };
	::DrawText(hDC, bGlyph ? glyph : (button.Glyph == 0 ? CNLS::GetString(button.Text) : button.Text), -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	::SelectObject(hDC, hOldFont);
}

void COverlay::Paint(HDC hDC, const CRect& paintRect) {
	HDC hMem = m_hMemDC;
	int nSaved = ::SaveDC(hMem);
	::IntersectClipRect(hMem, paintRect.left, paintRect.top, paintRect.right, paintRect.bottom);
	::BitBlt(hMem, paintRect.left, paintRect.top, paintRect.Width(), paintRect.Height(), m_hDarkDC, paintRect.left, paintRect.top, SRCCOPY);

	CRect bright = m_bHasSelection ? m_selection : m_hoverWindow;
	if (!bright.IsRectEmpty()) {
		::BitBlt(hMem, bright.left, bright.top, bright.Width(), bright.Height(), m_hBrightDC, bright.left, bright.top, SRCCOPY);
	}
	if (m_bHasSelection) {
		if (m_annotations.HasPixelate()) {
			// pixelated copy of the selection (blocks aligned as in the result)
			std::vector<DWORD> pixels((size_t)m_selection.Width() * m_selection.Height());
			for (int y = 0; y < m_selection.Height(); y++) {
				memcpy(&pixels[(size_t)y * m_selection.Width()], &m_screen.Pixels[(size_t)(m_selection.top + y) * m_screen.Width + m_selection.left], m_selection.Width() * sizeof(DWORD));
			}
			m_annotations.PixelateInto(&pixels[0], m_selection.Width(), m_selection.Height(), m_selection.left, m_selection.top);
			BITMAPINFO bmi = { 0 };
			bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bmi.bmiHeader.biWidth = m_selection.Width();
			bmi.bmiHeader.biHeight = -m_selection.Height();
			bmi.bmiHeader.biPlanes = 1;
			bmi.bmiHeader.biBitCount = 32;
			bmi.bmiHeader.biCompression = BI_RGB;
			::SetDIBitsToDevice(hMem, m_selection.left, m_selection.top, m_selection.Width(), m_selection.Height(), 0, 0, 0, m_selection.Height(), &pixels[0], &bmi, DIB_RGB_COLORS);
		}
		if (!m_annotations.IsEmpty()) {
			Gdiplus::Graphics graphics(hMem);
			CRect clip;
			clip.IntersectRect(&m_selection, &paintRect);
			graphics.SetClip(Gdiplus::Rect(clip.left, clip.top, clip.Width(), clip.Height()));
			m_annotations.Render(graphics);
		}
		if (m_bTextLayer) {
			// the image is dimmed, the words are marked, the selected words in the accent color
			Gdiplus::Graphics graphics(hMem);
			CRect clip;
			clip.IntersectRect(&m_selection, &paintRect);
			graphics.SetClip(Gdiplus::Rect(clip.left, clip.top, clip.Width(), clip.Height()));
			Gdiplus::SolidBrush dim(Gdiplus::Color(110, 0, 0, 0));
			graphics.FillRectangle(&dim, m_selection.left, m_selection.top, m_selection.Width(), m_selection.Height());
			int nFrom = min(m_nTextStart, m_nTextEnd), nTo = max(m_nTextStart, m_nTextEnd);
			Gdiplus::SolidBrush wordBrush(Gdiplus::Color(55, 255, 255, 255));
			Gdiplus::SolidBrush selectedBrush(Gdiplus::Color(120, 0, 120, 215));
			Gdiplus::Pen wordPen(Gdiplus::Color(150, 255, 255, 255), 1.0f);
			for (int i = 0; i < (int)m_words.size(); i++) {
				CRect wordRect = m_words[i].Rect;
				wordRect.InflateRect(S(2), S(2));
				// the words are shown undimmed
				::BitBlt(hMem, m_words[i].Rect.left, m_words[i].Rect.top, m_words[i].Rect.Width(), m_words[i].Rect.Height(),
					m_hBrightDC, m_words[i].Rect.left, m_words[i].Rect.top, SRCCOPY);
				bool bSelected = nFrom >= 0 && i >= nFrom && i <= nTo;
				graphics.FillRectangle(bSelected ? &selectedBrush : &wordBrush, wordRect.left, wordRect.top, wordRect.Width(), wordRect.Height());
				if (!bSelected) graphics.DrawRectangle(&wordPen, wordRect.left, wordRect.top, wordRect.Width() - 1, wordRect.Height() - 1);
			}
		}
		CRect border = m_selection;
		border.InflateRect(1, 1);
		FrameRect(hMem, border, COLOR_ACCENT, 1);
		if (m_eMode == MODE_CAPTURE && m_eDrag != DRAG_DRAW) {
			// resize handles
			int nHandle = S(4);
			CPoint points[8] = { m_selection.TopLeft(), CPoint(m_selection.CenterPoint().x, m_selection.top), CPoint(m_selection.right, m_selection.top),
				CPoint(m_selection.right, m_selection.CenterPoint().y), m_selection.BottomRight(), CPoint(m_selection.CenterPoint().x, m_selection.bottom),
				CPoint(m_selection.left, m_selection.bottom), CPoint(m_selection.left, m_selection.CenterPoint().y) };
			for (const CPoint& pt : points) {
				CRect handle(pt.x - nHandle, pt.y - nHandle, pt.x + nHandle + 1, pt.y + nHandle + 1);
				FillRound(hMem, handle, RGB(255, 255, 255), S(2), COLOR_ACCENT);
			}
		}
		// size label
		CString sSize;
		sSize.Format(_T("%d \x00D7 %d"), m_selection.Width(), m_selection.Height());
		HGDIOBJ hOldFont = ::SelectObject(hMem, m_hSmallFont);
		CRect labelRect(0, 0, 0, 0);
		::DrawText(hMem, sSize, -1, &labelRect, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
		labelRect.InflateRect(S(6), S(3));
		labelRect.OffsetRect(m_selection.left - labelRect.left, m_selection.top - S(8) - labelRect.bottom);
		if (labelRect.top < 0) labelRect.OffsetRect(S(6), m_selection.top + S(6) - labelRect.top);
		FillRound(hMem, labelRect, COLOR_BAR, S(6));
		::SetBkMode(hMem, TRANSPARENT);
		::SetTextColor(hMem, COLOR_TEXT);
		::DrawText(hMem, sSize, -1, &labelRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		::SelectObject(hMem, hOldFont);
	} else if (!m_hoverWindow.IsRectEmpty()) {
		FrameRect(hMem, m_hoverWindow, COLOR_ACCENT, S(2));
	}

	// tool bars
	if (!m_buttons.empty()) {
		CRect toolBar(0, 0, 0, 0), actionBar(0, 0, 0, 0), swatchBar(0, 0, 0, 0);
		for (const CButton& button : m_buttons) {
			CRect* pBar = (button.Id <= BTN_UNDO) ? &toolBar : (button.Id == BTN_SWATCH) ? &swatchBar : &actionBar;
			if (pBar->IsRectEmpty()) *pBar = button.Rect; else pBar->UnionRect(pBar, &button.Rect);
		}
		for (CRect* pBar : { &toolBar, &actionBar, &swatchBar }) {
			if (pBar->IsRectEmpty()) continue;
			CRect bar = *pBar;
			bar.InflateRect(S(4), S(4));
			FillRound(hMem, bar, COLOR_BAR, S(10), COLOR_BAR_BORDER);
		}
		CAnnotations::ETool eTool = m_annotations.GetTool();
		for (int i = 0; i < (int)m_buttons.size(); i++) {
			const CButton& button = m_buttons[i];
			bool bChecked = (button.Id == BTN_PEN && eTool == CAnnotations::TOOL_PEN) || (button.Id == BTN_HIGHLIGHTER && eTool == CAnnotations::TOOL_HIGHLIGHTER) ||
				(button.Id == BTN_RECTANGLE && eTool == CAnnotations::TOOL_RECTANGLE) || (button.Id == BTN_ARROW && eTool == CAnnotations::TOOL_ARROW) ||
				(button.Id == BTN_TEXT && eTool == CAnnotations::TOOL_TEXT) || (button.Id == BTN_PIXELATE && eTool == CAnnotations::TOOL_PIXELATE) ||
				(button.Id == BTN_COLOR && m_bColorPopup);
			PaintButton(button, i == m_nHoverButton, bChecked);
		}
		// tooltip beside the hovered button, away from the selection
		if (m_nHoverButton >= 0 && m_buttons[m_nHoverButton].Tooltip != NULL) {
			const CButton& button = m_buttons[m_nHoverButton];
			LPCTSTR sTip = CNLS::GetString(button.Tooltip);
			HGDIOBJ hOldFont = ::SelectObject(hMem, m_hFont);
			CRect tipRect(0, 0, 0, 0);
			::DrawText(hMem, sTip, -1, &tipRect, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
			tipRect.InflateRect(S(8), S(4));
			if (button.Id <= BTN_UNDO) {
				bool bRight = button.Rect.left >= m_selection.right;
				int nX = bRight ? button.Rect.right + S(12) : button.Rect.left - S(12) - tipRect.Width();
				tipRect.OffsetRect(nX - tipRect.left, button.Rect.CenterPoint().y - tipRect.Height() / 2 - tipRect.top);
			} else {
				bool bBelow = button.Rect.top >= m_selection.bottom;
				int nY = bBelow ? button.Rect.bottom + S(12) : button.Rect.top - S(12) - tipRect.Height();
				tipRect.OffsetRect(button.Rect.CenterPoint().x - tipRect.Width() / 2 - tipRect.left, nY - tipRect.top);
			}
			FillRound(hMem, tipRect, COLOR_BAR, S(6), COLOR_BAR_BORDER);
			::SetBkMode(hMem, TRANSPARENT);
			::SetTextColor(hMem, COLOR_TEXT);
			::DrawText(hMem, sTip, -1, &tipRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			::SelectObject(hMem, hOldFont);
		}
	}

	// hint of the text layer below the selection
	if (m_bTextLayer && !m_sTextHint.IsEmpty()) {
		CRect hintRect = GetTextHintRect();
		FillRound(hMem, hintRect, COLOR_BAR, S(8), COLOR_BAR_BORDER);
		HGDIOBJ hOldFont = ::SelectObject(hMem, m_hFont);
		::SetBkMode(hMem, TRANSPARENT);
		::SetTextColor(hMem, COLOR_TEXT);
		::DrawText(hMem, m_sTextHint, -1, &hintRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
		::SelectObject(hMem, hOldFont);
	}

	// hint at the top of the monitor
	if (!m_bHasSelection || m_eDrag == DRAG_NEW) {
		CRect hintRect = GetHintRect();
		FillRound(hMem, hintRect, COLOR_BAR, S(10), COLOR_BAR_BORDER);
		HGDIOBJ hOldFont = ::SelectObject(hMem, m_hFont);
		::SetBkMode(hMem, TRANSPARENT);
		::SetTextColor(hMem, COLOR_TEXT);
		::DrawText(hMem, GetHintText(), -1, &hintRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
		::SelectObject(hMem, hOldFont);
	}

	if (IsLoupeShown()) {
		PaintLoupe(hMem);
	}

	::RestoreDC(hMem, nSaved);
	::BitBlt(hDC, paintRect.left, paintRect.top, paintRect.Width(), paintRect.Height(), hMem, paintRect.left, paintRect.top, SRCCOPY);
}

static const int LOUPE_PIXELS = 15, LOUPE_ZOOM = 8;

CRect COverlay::GetLoupeRect() {
	int nSize = LOUPE_PIXELS * LOUPE_ZOOM;
	int nHeight = nSize + S(24);
	CRect monitor = m_screen.Monitors.empty() ? CRect(0, 0, m_screen.Width, m_screen.Height) : m_screen.Monitors[FindMonitor(m_mouse)];
	int nX = m_mouse.x + S(24), nY = m_mouse.y + S(24);
	if (nX + nSize > monitor.right) nX = m_mouse.x - S(24) - nSize;
	if (nY + nHeight > monitor.bottom) nY = m_mouse.y - S(24) - nHeight;
	return CRect(nX, nY, nX + nSize, nY + nHeight);
}

void COverlay::PaintLoupe(HDC hDC) {
	CRect rect = GetLoupeRect();
	int nSize = LOUPE_PIXELS * LOUPE_ZOOM;
	CRect zoomRect(rect.left, rect.top, rect.left + nSize, rect.top + nSize);
	int nOldMode = ::SetStretchBltMode(hDC, COLORONCOLOR);
	::StretchBlt(hDC, zoomRect.left, zoomRect.top, nSize, nSize, m_hBrightDC, m_mouse.x - LOUPE_PIXELS / 2, m_mouse.y - LOUPE_PIXELS / 2,
		LOUPE_PIXELS, LOUPE_PIXELS, SRCCOPY);
	::SetStretchBltMode(hDC, nOldMode);
	// the pixel under the mouse
	CRect center(zoomRect.left + (LOUPE_PIXELS / 2) * LOUPE_ZOOM, zoomRect.top + (LOUPE_PIXELS / 2) * LOUPE_ZOOM, 0, 0);
	center.right = center.left + LOUPE_ZOOM;
	center.bottom = center.top + LOUPE_ZOOM;
	FrameRect(hDC, center, RGB(255, 255, 255), 1);
	FrameRect(hDC, zoomRect, COLOR_BAR_BORDER, 1);
	// position and color
	DWORD c = (m_mouse.x >= 0 && m_mouse.y >= 0 && m_mouse.x < m_screen.Width && m_mouse.y < m_screen.Height) ?
		m_screen.Pixels[(size_t)m_mouse.y * m_screen.Width + m_mouse.x] : 0;
	CString sText;
	sText.Format(_T("%d, %d   #%02X%02X%02X"), m_mouse.x + m_screen.Left, m_mouse.y + m_screen.Top, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
	CRect textRect(rect.left, zoomRect.bottom, rect.right, rect.bottom);
	HBRUSH hBrush = ::CreateSolidBrush(COLOR_BAR);
	::FillRect(hDC, &textRect, hBrush);
	::DeleteObject(hBrush);
	HGDIOBJ hOldFont = ::SelectObject(hDC, m_hSmallFont);
	::SetBkMode(hDC, TRANSPARENT);
	::SetTextColor(hDC, COLOR_TEXT);
	::DrawText(hDC, sText, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
	::SelectObject(hDC, hOldFont);
}

int COverlay::HitTestHandle(const CPoint& pt) {
	if (!m_bHasSelection || m_eMode != MODE_CAPTURE) return 0;
	int nTolerance = S(6);
	int nEdges = 0;
	bool bInY = pt.y >= m_selection.top - nTolerance && pt.y <= m_selection.bottom + nTolerance;
	bool bInX = pt.x >= m_selection.left - nTolerance && pt.x <= m_selection.right + nTolerance;
	if (bInY && abs(pt.x - m_selection.left) <= nTolerance) nEdges |= 1;
	if (bInX && abs(pt.y - m_selection.top) <= nTolerance) nEdges |= 2;
	if (bInY && abs(pt.x - m_selection.right) <= nTolerance) nEdges |= 4;
	if (bInX && abs(pt.y - m_selection.bottom) <= nTolerance) nEdges |= 8;
	return nEdges;
}

int COverlay::HitTestButton(const CPoint& pt) {
	for (int i = 0; i < (int)m_buttons.size(); i++) {
		if (m_buttons[i].Rect.PtInRect(pt)) return i;
	}
	return -1;
}

void COverlay::SetCursorFor(const CPoint& pt) {
	LPCTSTR sCursor = IDC_CROSS;
	if (HitTestButton(pt) >= 0) {
		sCursor = IDC_HAND;
	} else if (m_bTextLayer && m_selection.PtInRect(pt) && (m_eDrag == DRAG_TEXT || WordAt(pt, false) >= 0)) {
		sCursor = IDC_IBEAM;
	} else if (m_bTextLayer && m_selection.PtInRect(pt) && HitTestHandle(pt) == 0) {
		sCursor = IDC_ARROW;
	} else if (m_eDrag == DRAG_MOVE) {
		sCursor = IDC_SIZEALL;
	} else if (m_bHasSelection && m_eDrag == DRAG_NONE) {
		int nEdges = HitTestHandle(pt);
		bool bDrawing = m_annotations.GetTool() != CAnnotations::TOOL_NONE && m_selection.PtInRect(pt);
		if (nEdges != 0 && !bDrawing) {
			sCursor = (nEdges == 3 || nEdges == 12) ? IDC_SIZENWSE : (nEdges == 6 || nEdges == 9) ? IDC_SIZENESW :
				(nEdges == 1 || nEdges == 4) ? IDC_SIZEWE : IDC_SIZENS;
		} else if (m_selection.PtInRect(pt)) {
			CAnnotations::ETool eTool = m_annotations.GetTool();
			sCursor = (eTool == CAnnotations::TOOL_NONE) ? (m_eMode == MODE_CAPTURE ? IDC_SIZEALL : IDC_ARROW) : (eTool == CAnnotations::TOOL_TEXT) ? IDC_IBEAM : IDC_CROSS;
		}
	}
	::SetCursor(::LoadCursor(NULL, sCursor));
}

void COverlay::OnButton(EButton eButton, int nIndex) {
	CAnnotations::ETool eTool = CAnnotations::TOOL_NONE;
	switch (eButton) {
		case BTN_PEN: eTool = CAnnotations::TOOL_PEN; break;
		case BTN_HIGHLIGHTER: eTool = CAnnotations::TOOL_HIGHLIGHTER; break;
		case BTN_RECTANGLE: eTool = CAnnotations::TOOL_RECTANGLE; break;
		case BTN_ARROW: eTool = CAnnotations::TOOL_ARROW; break;
		case BTN_TEXT: eTool = CAnnotations::TOOL_TEXT; break;
		case BTN_PIXELATE: eTool = CAnnotations::TOOL_PIXELATE; break;
		case BTN_COLOR:
			m_bColorPopup = !m_bColorPopup;
			UpdateWindow();
			return;
		case BTN_SWATCH:
			m_annotations.SetColor(m_buttons[nIndex].Swatch);
			m_bColorPopup = false;
			if (m_annotations.GetTool() == CAnnotations::TOOL_NONE) m_annotations.SetTool(CAnnotations::TOOL_PEN);
			UpdateWindow();
			::InvalidateRect(m_hWnd, &m_selection, FALSE);
			return;
		case BTN_WIDTH: {
			static const int widths[] = { 2, 3, 5, 8 };
			int nNext = widths[0];
			for (int i = 0; i < 4; i++) if (widths[i] > m_annotations.GetWidth()) { nNext = widths[i]; break; }
			m_annotations.SetWidth(nNext);
			UpdateWindow();
			return;
		}
		case BTN_UNDO:
			m_annotations.Undo();
			::InvalidateRect(m_hWnd, &m_selection, FALSE);
			UpdateWindow();
			return;
		case BTN_OCR:
			if (m_bTextLayer) HideTextLayer(); else ShowTextLayer(false);
			return;
		case BTN_COPY_TEXT:
			m_nTextStart = m_nTextEnd = -1;
			CopySelectedText();
			return;
		case BTN_OPEN: OpenInViewer(); return;
		case BTN_SAVE: Save(); return;
		case BTN_COPY: Copy(); return;
		case BTN_CLOSE: Close(RESULT_NONE); return;
	}
	// clicking the active tool again switches back to moving the selection
	HideTextLayer();
	m_annotations.SetTool(m_annotations.GetTool() == eTool ? CAnnotations::TOOL_NONE : eTool);
	m_bColorPopup = false;
	UpdateWindow();
}

void COverlay::OnLButtonDown(CPoint pt, bool bDoubleClick) {
	m_mouse = pt;
	int nButton = HitTestButton(pt);
	if (nButton >= 0) {
		OnButton(m_buttons[nButton].Id, nButton);
		return;
	}
	if (m_bColorPopup) {
		m_bColorPopup = false;
		UpdateWindow();
	}
	if (m_bTextLayer && m_selection.PtInRect(pt) && HitTestHandle(pt) == 0) {
		int nWord = WordAt(pt, false);
		if (bDoubleClick && nWord >= 0) {
			// double click selects the line
			int nFirst = nWord, nLast = nWord;
			while (nFirst > 0 && m_words[nFirst - 1].Line == m_words[nWord].Line) nFirst--;
			while (nLast + 1 < (int)m_words.size() && m_words[nLast + 1].Line == m_words[nWord].Line) nLast++;
			m_nTextStart = nFirst;
			m_nTextEnd = nLast;
		} else {
			m_nTextStart = m_nTextEnd = nWord;
			if (nWord >= 0) {
				m_eDrag = DRAG_TEXT;
				::SetCapture(m_hWnd);
			}
		}
		::InvalidateRect(m_hWnd, &m_selection, FALSE);
		return;
	}
	if (m_bTextLayer && (m_eMode == MODE_TEXT || m_annotations.IsEmpty())) {
		HideTextLayer(); // the words belong to the old selection
	}
	if (m_bHasSelection && m_eMode == MODE_CAPTURE) {
		CAnnotations::ETool eTool = m_annotations.GetTool();
		bool bInside = m_selection.PtInRect(pt) != FALSE;
		if (bDoubleClick && bInside && eTool == CAnnotations::TOOL_NONE) {
			Copy();
			return;
		}
		if (bInside && eTool == CAnnotations::TOOL_TEXT) {
			CString sText;
			if (CAnnotations::AskForText(m_hWnd, CNLS::GetString(_T("Text")), CNLS::GetString(_T("OK")), CNLS::GetString(_T("Cancel")), sText)) {
				m_annotations.AddText((float)pt.x, (float)pt.y, 1.0, sText);
				::InvalidateRect(m_hWnd, &m_selection, FALSE);
			}
			::SetForegroundWindow(m_hWnd);
			return;
		}
		if (bInside && eTool != CAnnotations::TOOL_NONE) {
			m_annotations.BeginShape((float)pt.x, (float)pt.y, 1.0);
			m_eDrag = DRAG_DRAW;
			::SetCapture(m_hWnd);
			return;
		}
		int nEdges = HitTestHandle(pt);
		if (nEdges != 0) {
			m_eDrag = DRAG_RESIZE;
			m_nResizeEdges = nEdges;
		} else if (bInside) {
			m_eDrag = DRAG_MOVE;
		} else if (m_annotations.IsEmpty()) {
			m_eDrag = DRAG_NEW; // a new selection, the drawings would not fit to it
			m_bHasSelection = false;
		} else {
			return;
		}
		m_dragStart = pt;
		m_selectionStart = m_selection;
		::SetCapture(m_hWnd);
		UpdateWindow();
		return;
	}
	m_eDrag = DRAG_NEW;
	m_dragStart = pt;
	m_bHasSelection = false;
	::SetCapture(m_hWnd);
}

void COverlay::OnMouseMove(CPoint pt) {
	m_mouse = pt;
	CRect screenRect(0, 0, m_screen.Width, m_screen.Height);
	switch (m_eDrag) {
		case DRAG_NEW: {
			CRect selection(m_dragStart, pt);
			selection.NormalizeRect();
			selection.IntersectRect(&selection, &screenRect);
			if (selection.Width() > 2 || selection.Height() > 2 || m_bHasSelection) {
				m_selection = selection;
				m_bHasSelection = true;
				m_hoverWindow.SetRectEmpty();
			}
			break;
		}
		case DRAG_MOVE: {
			CRect selection = m_selectionStart;
			selection.OffsetRect(pt - m_dragStart);
			if (selection.left < 0) selection.OffsetRect(-selection.left, 0);
			if (selection.top < 0) selection.OffsetRect(0, -selection.top);
			if (selection.right > m_screen.Width) selection.OffsetRect(m_screen.Width - selection.right, 0);
			if (selection.bottom > m_screen.Height) selection.OffsetRect(0, m_screen.Height - selection.bottom);
			m_selection = selection;
			break;
		}
		case DRAG_RESIZE: {
			CRect selection = m_selectionStart;
			CPoint delta = pt - m_dragStart;
			if (m_nResizeEdges & 1) selection.left += delta.x;
			if (m_nResizeEdges & 2) selection.top += delta.y;
			if (m_nResizeEdges & 4) selection.right += delta.x;
			if (m_nResizeEdges & 8) selection.bottom += delta.y;
			selection.NormalizeRect();
			selection.IntersectRect(&selection, &screenRect);
			if (selection.Width() >= 1 && selection.Height() >= 1) m_selection = selection;
			break;
		}
		case DRAG_DRAW:
			m_annotations.ContinueShape((float)pt.x, (float)pt.y);
			::InvalidateRect(m_hWnd, &m_selection, FALSE);
			return;
		case DRAG_TEXT: {
			int nWord = WordAt(pt, true);
			if (nWord >= 0 && nWord != m_nTextEnd) {
				m_nTextEnd = nWord;
				::InvalidateRect(m_hWnd, &m_selection, FALSE);
			}
			return;
		}
		case DRAG_NONE: {
			int nHover = HitTestButton(pt);
			if (nHover != m_nHoverButton) {
				m_nHoverButton = nHover;
				UpdateWindow();
			}
			if (!m_bHasSelection) {
				// the window under the mouse is selected by a click
				CRect hover(0, 0, 0, 0);
				for (const CRect& window : m_screen.Windows) {
					if (window.PtInRect(pt)) {
						hover = window;
						break;
					}
				}
				if (hover.IsRectEmpty() && !m_screen.Monitors.empty()) {
					hover = m_screen.Monitors[FindMonitor(pt)];
				}
				if (hover != m_hoverWindow) {
					m_hoverWindow = hover;
					UpdateDpi();
					UpdateWindow();
				}
				UpdateWindow(); // hint and magnifier follow the mouse
			}
			return;
		}
	}
	m_nHoverButton = -1;
	UpdateWindow();
}

void COverlay::OnLButtonUp(CPoint pt) {
	m_mouse = pt;
	EDrag eDrag = m_eDrag;
	m_eDrag = DRAG_NONE;
	::ReleaseCapture();
	if (eDrag == DRAG_DRAW) {
		m_annotations.EndShape();
		UpdateWindow();
		return;
	}
	if (eDrag == DRAG_TEXT) {
		return;
	}
	if (eDrag == DRAG_MOVE || eDrag == DRAG_RESIZE) {
		HideTextLayer();
	}
	if (eDrag == DRAG_NEW) {
		if (!m_bHasSelection || (m_selection.Width() < 3 && m_selection.Height() < 3)) {
			// a click: the window (or monitor) under the mouse
			m_bHasSelection = !m_hoverWindow.IsRectEmpty();
			m_selection = m_hoverWindow;
		}
		m_hoverWindow.SetRectEmpty();
		if (m_bHasSelection) {
			SelectionFinished();
			return;
		}
	}
	UpdateWindow();
}

void COverlay::SelectionFinished() {
	if (m_eMode == MODE_TEXT) {
		// as the Snipping Tool: the text is shown over the image and all of it is copied
		ShowTextLayer(true);
		return;
	}
	UpdateDpi();
	UpdateWindow();
}

void COverlay::OnKeyDown(UINT nKey) {
	bool bCtrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
	bool bShift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
	if (nKey == VK_ESCAPE) {
		if (m_bColorPopup) {
			m_bColorPopup = false;
			UpdateWindow();
		} else {
			Close(RESULT_NONE);
		}
		return;
	}
	if (!m_bHasSelection && !bCtrl && nKey == 'C' && m_mouse.x >= 0 && m_mouse.y >= 0 && m_mouse.x < m_screen.Width && m_mouse.y < m_screen.Height) {
		// C copies the color under the mouse
		DWORD c = m_screen.Pixels[(size_t)m_mouse.y * m_screen.Width + m_mouse.x];
		CString sColor;
		sColor.Format(_T("#%02X%02X%02X"), (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
		CopyTextToClipboard(m_hWnd, sColor);
		Close(RESULT_NONE);
		ShowToast(CString(CNLS::GetString(_T("Color copied:"))) + _T(" ") + sColor, false);
		return;
	}
	if (!m_bHasSelection) {
		// without selection the keys take the whole monitor with the mouse (Shift: all monitors)
		bool bAction = nKey == VK_RETURN || (bCtrl && (nKey == 'C' || nKey == 'S' || nKey == 'T'));
		if (!bAction) return;
		m_selection = (bShift || m_screen.Monitors.empty()) ? CRect(0, 0, m_screen.Width, m_screen.Height) : m_screen.Monitors[FindMonitor(m_mouse)];
		m_bHasSelection = true;
		m_hoverWindow.SetRectEmpty();
		UpdateDpi();
	}
	if (m_bTextLayer && bCtrl && nKey == 'A') {
		m_nTextStart = 0;
		m_nTextEnd = (int)m_words.size() - 1;
		::InvalidateRect(m_hWnd, &m_selection, FALSE);
		return;
	}
	if (nKey == VK_RETURN) {
		if (m_eMode == MODE_TEXT) {
			if (!m_bTextLayer) ShowTextLayer(true);
			else if (m_nTextStart >= 0) { CopySelectedText(); Close(RESULT_NONE); }
			else Close(RESULT_NONE);
		} else {
			Copy();
		}
	} else if (bCtrl && nKey == 'C') {
		if (m_bTextLayer || m_eMode == MODE_TEXT) {
			if (!m_bTextLayer) ShowTextLayer(true); else CopySelectedText();
		} else {
			Copy();
		}
	} else if (bCtrl && nKey == 'S') {
		Save();
	} else if (bCtrl && nKey == 'Z') {
		m_annotations.Undo();
		::InvalidateRect(m_hWnd, &m_selection, FALSE);
	} else if (bCtrl && nKey == 'T') {
		if (m_bTextLayer && m_eMode == MODE_CAPTURE) HideTextLayer(); else ShowTextLayer(m_eMode == MODE_TEXT);
	} else if (nKey == VK_LEFT || nKey == VK_RIGHT || nKey == VK_UP || nKey == VK_DOWN) {
		// arrows move the selection by a pixel, with Shift they change the size
		int nDX = (nKey == VK_LEFT) ? -1 : (nKey == VK_RIGHT) ? 1 : 0;
		int nDY = (nKey == VK_UP) ? -1 : (nKey == VK_DOWN) ? 1 : 0;
		CRect selection = m_selection;
		if (bShift) {
			selection.right = max(selection.left + 1, (int)selection.right + nDX);
			selection.bottom = max(selection.top + 1, (int)selection.bottom + nDY);
		} else if (m_annotations.IsEmpty()) {
			selection.OffsetRect(nDX, nDY);
		}
		CRect screenRect(0, 0, m_screen.Width, m_screen.Height);
		CRect clipped;
		clipped.IntersectRect(&selection, &screenRect);
		if (clipped == selection) {
			m_selection = selection;
			UpdateWindow();
		}
	}
}

CRect COverlay::GetTextHintRect() {
	HDC hDC = ::GetDC(NULL);
	HGDIOBJ hOld = ::SelectObject(hDC, m_hFont);
	CRect textRect(0, 0, 0, 0);
	::DrawText(hDC, m_sTextHint.IsEmpty() ? _T(" ") : (LPCTSTR)m_sTextHint, -1, &textRect, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
	::SelectObject(hDC, hOld);
	::ReleaseDC(NULL, hDC);
	textRect.InflateRect(S(10), S(5));
	int nX = m_selection.left;
	int nY = m_selection.top - S(8) - textRect.Height() - S(26); // above the size label
	if (nY < 0) nY = m_selection.top + S(8);
	textRect.OffsetRect(nX - textRect.left, nY - textRect.top);
	return textRect;
}

void COverlay::ShowTextLayer(bool bCopyAll) {
	if (!m_bHasSelection) return;
	std::vector<DWORD> pixels;
	int nWidth, nHeight;
	GetCrop(pixels, nWidth, nHeight);
	if (nWidth <= 0 || nHeight <= 0) return;
	HCURSOR hOldCursor = ::SetCursor(::LoadCursor(NULL, IDC_WAIT));
	std::vector<Ocr::CWord> words;
	CString sError;
	bool bOK = RecognizeWords(pixels, nWidth, nHeight, words, sError);
	::SetCursor(hOldCursor);
	m_words.clear();
	for (const Ocr::CWord& word : words) {
		CTextWord textWord;
		textWord.Rect = CRect((int)word.Left, (int)word.Top, (int)(word.Left + word.Width + 0.999f), (int)(word.Top + word.Height + 0.999f));
		textWord.Rect.OffsetRect(m_selection.TopLeft());
		textWord.Rect.IntersectRect(&textWord.Rect, &m_selection);
		textWord.Text = word.Text.c_str();
		textWord.Line = word.Line;
		m_words.push_back(textWord);
	}
	m_bTextLayer = true;
	m_annotations.SetTool(CAnnotations::TOOL_NONE);
	m_bColorPopup = false;
	m_nTextStart = m_nTextEnd = -1;
	if (!bOK) {
		m_sTextHint = sError.IsEmpty() ? CNLS::GetString(_T("The text could not be recognized.")) : (LPCTSTR)sError;
	} else if (m_words.empty()) {
		m_sTextHint = CNLS::GetString(_T("No text was found in the image."));
	} else if (bCopyAll) {
		CopySelectedText();
	} else {
		m_sTextHint = CNLS::GetString(_T("Select words with the mouse, Ctrl+C copies them, Ctrl+A selects all."));
	}
	UpdateWindow();
	::InvalidateRect(m_hWnd, &m_selection, FALSE);
}

void COverlay::HideTextLayer() {
	if (!m_bTextLayer) return;
	m_bTextLayer = false;
	m_words.clear();
	m_nTextStart = m_nTextEnd = -1;
	m_sTextHint.Empty();
	UpdateWindow();
	::InvalidateRect(m_hWnd, &m_selection, FALSE);
}

int COverlay::WordAt(const CPoint& pt, bool bNearest) {
	int nBest = -1;
	double dBest = 1e30;
	for (int i = 0; i < (int)m_words.size(); i++) {
		CRect rect = m_words[i].Rect;
		rect.InflateRect(S(3), S(3));
		if (rect.PtInRect(pt)) return i;
		if (bNearest) {
			// distance to the rectangle, the vertical distance counts more (stays on the line)
			double dX = max(0, max((int)rect.left - pt.x, pt.x - (int)rect.right));
			double dY = max(0, max((int)rect.top - pt.y, pt.y - (int)rect.bottom));
			double dDistance = dX * dX + 4 * dY * dY;
			if (dDistance < dBest) {
				dBest = dDistance;
				nBest = i;
			}
		}
	}
	return nBest;
}

CString COverlay::GetSelectedText(bool bAllIfNone) {
	int nFrom = min(m_nTextStart, m_nTextEnd), nTo = max(m_nTextStart, m_nTextEnd);
	if (nFrom < 0) {
		if (!bAllIfNone || m_words.empty()) return CString();
		nFrom = 0;
		nTo = (int)m_words.size() - 1;
	}
	CString sText;
	for (int i = nFrom; i <= nTo && i < (int)m_words.size(); i++) {
		if (i > nFrom) sText += (m_words[i].Line != m_words[i - 1].Line) ? _T("\r\n") : _T(" ");
		sText += m_words[i].Text;
	}
	return sText;
}

void COverlay::CopySelectedText() {
	CString sText = GetSelectedText(true);
	if (sText.IsEmpty()) return;
	CopyTextToClipboard(m_hWnd, sText);
	int nLines = 1;
	for (int i = 0; i < sText.GetLength(); i++) if (sText[i] == _T('\n')) nLines++;
	CString sMessage;
	if (m_nTextStart < 0) {
		sMessage.Format(CNLS::GetString(_T("All text was copied (%d lines). Select words to copy a part.")), nLines);
	} else {
		sMessage.Format(CNLS::GetString(_T("%d characters were copied to the clipboard.")), sText.GetLength());
	}
	m_sTextHint = sMessage;
	UpdateWindow();
}

void COverlay::GetCrop(std::vector<DWORD>& pixels, int& nWidth, int& nHeight) {
	nWidth = m_selection.Width();
	nHeight = m_selection.Height();
	pixels.resize((size_t)nWidth * nHeight);
	for (int y = 0; y < nHeight; y++) {
		memcpy(&pixels[(size_t)y * nWidth], &m_screen.Pixels[(size_t)(m_selection.top + y) * m_screen.Width + m_selection.left], nWidth * sizeof(DWORD));
	}
	if (!m_annotations.IsEmpty()) {
		m_annotations.PixelateInto(&pixels[0], nWidth, nHeight, m_selection.left, m_selection.top);
		Gdiplus::Bitmap bitmap(nWidth, nHeight, nWidth * 4, PixelFormat32bppRGB, (BYTE*)&pixels[0]);
		Gdiplus::Graphics graphics(&bitmap);
		graphics.TranslateTransform((Gdiplus::REAL)-m_selection.left, (Gdiplus::REAL)-m_selection.top);
		m_annotations.Render(graphics);
	}
}

void COverlay::Copy() {
	std::vector<DWORD> pixels;
	int nWidth, nHeight;
	GetCrop(pixels, nWidth, nHeight);
	if (nWidth > 0 && nHeight > 0 && CopyPixelsToClipboard(m_hWnd, pixels, nWidth, nHeight)) {
		// screenshot history: also the copied ones are kept in the folder (shown on the start screen of the viewer)
		if (CSettingsProvider::This().ScreenshotHistory()) {
			SavePixels(GetScreenshotFolder() + _T("\\") + GetNewScreenshotName(_T("png")), pixels, nWidth, nHeight);
		}
		Close(RESULT_NONE);
		ShowToast(CNLS::GetString(_T("The screenshot was copied to the clipboard.")), false);
	}
}

void COverlay::Save() {
	std::vector<DWORD> pixels;
	int nWidth, nHeight;
	GetCrop(pixels, nWidth, nHeight);
	if (nWidth <= 0 || nHeight <= 0) return;
	CString sFileName = GetScreenshotFolder() + _T("\\") + GetNewScreenshotName(_T("png"));
	int nFilterIndex = 1;
	CString sFilter = _T("PNG (*.png)|*.png|JPEG (*.jpg)|*.jpg|");
	if (!HelpersGUI::ShowFileDialog(m_hWnd, true, sFilter, _T("png"), sFileName, CNLS::GetString(_T("Save screenshot")), nFilterIndex, sFileName)) {
		::SetForegroundWindow(m_hWnd);
		return;
	}
	if (SavePixels(sFileName, pixels, nWidth, nHeight)) {
		Close(RESULT_NONE);
		ShowToast(CNLS::GetString(_T("The screenshot was saved.")), false);
	} else {
		::MessageBox(m_hWnd, CNLS::GetString(_T("Error writing file to disk!")), CNLS::GetString(_T("Screenshot")), MB_OK | MB_ICONERROR);
	}
}

void COverlay::OpenInViewer() {
	std::vector<DWORD> pixels;
	int nWidth, nHeight;
	GetCrop(pixels, nWidth, nHeight);
	if (nWidth <= 0 || nHeight <= 0) return;
	// saved in the screenshot folder, so the file is there to be edited, shared or deleted
	CString sFileName = GetScreenshotFolder() + _T("\\") + GetNewScreenshotName(_T("png"));
	if (!SavePixels(sFileName, pixels, nWidth, nHeight)) {
		::MessageBox(m_hWnd, CNLS::GetString(_T("Error writing file to disk!")), CNLS::GetString(_T("Screenshot")), MB_OK | MB_ICONERROR);
		return;
	}
	Close(RESULT_NONE);
	::ShellExecute(NULL, _T("open"), GetExeName(), _T("\"") + sFileName + _T("\""), NULL, SW_SHOWNORMAL);
}

void COverlay::Close(EResult eResult) {
	m_eResult = eResult;
	m_bDone = true;
	::ShowWindow(m_hWnd, SW_HIDE);
	::PostMessage(m_hWnd, WM_NULL, 0, 0);
}

///////////////////////////////////////////////////////////////////////////////////
// Capture run (once or from the tray)
///////////////////////////////////////////////////////////////////////////////////

static void RunCapture(EMode eMode, bool bWaitForToast) {
	static bool s_bRunning = false;
	if (s_bRunning) return;
	s_bRunning = true;
	{
		CPerMonitorDpiScope dpiScope;
		CScreen screen;
		if (CaptureScreen(screen)) {
			COverlay overlay(screen, eMode);
			std::vector<DWORD> pixels;
			int nWidth = 0, nHeight = 0;
			if (overlay.Run(pixels, nWidth, nHeight) == COverlay::RESULT_OCR && nWidth > 0 && nHeight > 0) {
				HCURSOR hOldCursor = ::SetCursor(::LoadCursor(NULL, IDC_WAIT));
				CString sText, sError;
				bool bOK = RecognizeText(pixels, nWidth, nHeight, sText, sError);
				::SetCursor(hOldCursor);
				if (!bOK) {
					ShowToast(sError.IsEmpty() ? CNLS::GetString(_T("The text could not be recognized.")) : (LPCTSTR)sError, bWaitForToast);
				} else if (sText.IsEmpty()) {
					ShowToast(CNLS::GetString(_T("No text was found in the image.")), bWaitForToast);
				} else {
					CopyTextToClipboard(NULL, sText);
					if (eMode == MODE_TEXT) {
						int nLines = 1;
						for (int i = 0; i < sText.GetLength(); i++) if (sText[i] == _T('\n')) nLines++;
						CString sMessage;
						sMessage.Format(CNLS::GetString(_T("%d lines of text were copied to the clipboard.")), nLines);
						ShowToast(sMessage, bWaitForToast);
					} else {
						ShowTextWindow(sText);
					}
				}
			} else if (bWaitForToast) {
				// a toast of copy or save is still shown
				HWND hToast = ::FindWindow(_T("LucidViewerToast"), NULL);
				MSG msg;
				while (hToast != NULL && ::IsWindow(hToast) && ::GetMessage(&msg, NULL, 0, 0) > 0) {
					::TranslateMessage(&msg);
					::DispatchMessage(&msg);
				}
			}
		}
	}
	s_bRunning = false;
}

///////////////////////////////////////////////////////////////////////////////////
// Tray
///////////////////////////////////////////////////////////////////////////////////

static const UINT WM_TRAY_NOTIFY = WM_APP + 1;
static const UINT WM_TRAY_CAPTURE = WM_APP + 2;
static const int HOTKEY_CAPTURE = 1, HOTKEY_TEXT = 2;
static const int TRAY_CAPTURE = 1, TRAY_TEXT = 2, TRAY_FOLDER = 3, TRAY_AUTOSTART = 4, TRAY_KEYBOARD_SETTINGS = 5, TRAY_EXIT = 6;
static const LPCTSTR RUN_KEY = _T("Software\\Microsoft\\Windows\\CurrentVersion\\Run");
static const LPCTSTR RUN_VALUE = _T("Lucid Viewer Screenshot");
static UINT s_nTaskbarCreated = 0;
static CString s_sHotkeyText[3];

// "Shift+PrintScreen", "Ctrl+Alt+S", "F12"
static bool ParseHotkey(LPCTSTR sHotkey, UINT& nModifiers, UINT& nKey) {
	nModifiers = MOD_NOREPEAT;
	nKey = 0;
	CString sText(sHotkey);
	int nStart = 0;
	CString sPart = sText.Tokenize(_T("+"), nStart);
	while (nStart >= 0) {
		sPart.Trim();
		if (sPart.CompareNoCase(_T("Ctrl")) == 0) nModifiers |= MOD_CONTROL;
		else if (sPart.CompareNoCase(_T("Alt")) == 0) nModifiers |= MOD_ALT;
		else if (sPart.CompareNoCase(_T("Shift")) == 0) nModifiers |= MOD_SHIFT;
		else if (sPart.CompareNoCase(_T("Win")) == 0) nModifiers |= MOD_WIN;
		else if (sPart.CompareNoCase(_T("PrintScreen")) == 0 || sPart.CompareNoCase(_T("PrtScn")) == 0) nKey = VK_SNAPSHOT;
		else if (sPart.GetLength() == 1 && _istalnum(sPart[0])) nKey = _totupper(sPart[0]);
		else if ((sPart[0] == _T('F') || sPart[0] == _T('f')) && _ttoi(sPart.Mid(1)) >= 1 && _ttoi(sPart.Mid(1)) <= 24) nKey = VK_F1 + _ttoi(sPart.Mid(1)) - 1;
		else return false;
		sPart = sText.Tokenize(_T("+"), nStart);
	}
	return nKey != 0;
}

static bool IsAutostartEnabled() {
	HKEY hKey;
	bool bEnabled = false;
	if (::RegOpenKeyEx(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
		bEnabled = ::RegQueryValueEx(hKey, RUN_VALUE, NULL, NULL, NULL, NULL) == ERROR_SUCCESS;
		::RegCloseKey(hKey);
	}
	return bEnabled;
}

static void SetAutostart(bool bEnable) {
	HKEY hKey;
	if (::RegOpenKeyEx(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) return;
	if (bEnable) {
		CString sCommand = _T("\"") + GetExeName() + _T("\" /tray");
		::RegSetValueEx(hKey, RUN_VALUE, 0, REG_SZ, (const BYTE*)(LPCTSTR)sCommand, (sCommand.GetLength() + 1) * sizeof(TCHAR));
	} else {
		::RegDeleteValue(hKey, RUN_VALUE);
	}
	::RegCloseKey(hKey);
}

// Versions before the program was renamed to Lucid Viewer started their tray tool with this value and their own
// window class. The old tool holds the Print Screen key, and the old value would start it again with Windows.
static const LPCTSTR LEGACY_RUN_VALUE = _T("JPEGView Screenshot");
static const LPCTSTR LEGACY_TRAY_CLASS = _T("JPEGViewScreenshotTray");

static void RemoveLegacyTray() {
	bool bLegacyAutostart = false;
	HKEY hKey;
	if (::RegOpenKeyEx(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
		const int BUFFER_LEN = 1024;
		TCHAR sCommand[BUFFER_LEN];
		DWORD nType = 0, nSize = sizeof(sCommand);
		if (::RegQueryValueEx(hKey, LEGACY_RUN_VALUE, NULL, &nType, (LPBYTE)sCommand, &nSize) == ERROR_SUCCESS &&
			(nType == REG_SZ || nType == REG_EXPAND_SZ)) {
			sCommand[min((int)(nSize / sizeof(TCHAR)), BUFFER_LEN - 1)] = 0;
			// only the value of the tray tool, the original JPEGView has no tray tool
			if (Helpers::stristr(sCommand, _T("/tray")) != NULL) {
				::RegDeleteValue(hKey, LEGACY_RUN_VALUE);
				bLegacyAutostart = true;
			}
		}
		::RegCloseKey(hKey);
	}
	if (bLegacyAutostart && !IsAutostartEnabled()) {
		SetAutostart(true); // the user had the old tool start with Windows
	}
	HWND hLegacyTray = ::FindWindow(LEGACY_TRAY_CLASS, NULL);
	if (hLegacyTray != NULL) {
		DWORD_PTR nResult = 0;
		::SendMessageTimeout(hLegacyTray, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG, 2000, &nResult);
		for (int i = 0; i < 40 && ::IsWindow(hLegacyTray); i++) {
			::Sleep(50); // its hotkeys are free once the window is gone
		}
	}
}

// Windows 11 can give the Print Screen key to the Snipping Tool, then it does not reach other applications
static bool IsPrintScreenTakenBySnippingTool() {
	DWORD nValue = 0, nSize = sizeof(nValue);
	return ::RegGetValue(HKEY_CURRENT_USER, _T("Control Panel\\Keyboard"), _T("PrintScreenKeyForSnippingEnabled"), RRF_RT_REG_DWORD, NULL, &nValue, &nSize) == ERROR_SUCCESS && nValue != 0;
}

static void AddTrayIcon(HWND hWnd, bool bAdd) {
	NOTIFYICONDATA data = { 0 };
	data.cbSize = sizeof(data);
	data.hWnd = hWnd;
	data.uID = 1;
	data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	data.uCallbackMessage = WM_TRAY_NOTIFY;
	data.hIcon = (HICON)::LoadImage(::GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MAINFRAME), IMAGE_ICON,
		::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), 0);
	CString sTip = CString(CNLS::GetString(_T("Lucid Viewer screenshot"))) + _T(" (") + s_sHotkeyText[HOTKEY_CAPTURE] + _T(")");
	_tcsncpy_s(data.szTip, sTip, _TRUNCATE);
	::Shell_NotifyIcon(bAdd ? NIM_ADD : NIM_DELETE, &data);
	if (data.hIcon != NULL) ::DestroyIcon(data.hIcon);
}

static void ShowBalloon(HWND hWnd, LPCTSTR sTitle, LPCTSTR sText) {
	NOTIFYICONDATA data = { 0 };
	data.cbSize = sizeof(data);
	data.hWnd = hWnd;
	data.uID = 1;
	data.uFlags = NIF_INFO;
	data.dwInfoFlags = NIIF_INFO;
	_tcsncpy_s(data.szInfoTitle, sTitle, _TRUNCATE);
	_tcsncpy_s(data.szInfo, sText, _TRUNCATE);
	::Shell_NotifyIcon(NIM_MODIFY, &data);
}

static void ShowTrayMenu(HWND hWnd) {
	HMENU hMenu = ::CreatePopupMenu();
	CString sCapture = CString(CNLS::GetString(_T("Take screenshot"))) + _T("\t") + s_sHotkeyText[HOTKEY_CAPTURE];
	CString sText = CString(CNLS::GetString(_T("Copy text from screen"))) + _T("\t") + s_sHotkeyText[HOTKEY_TEXT];
	::AppendMenu(hMenu, MF_STRING, TRAY_CAPTURE, sCapture);
	::AppendMenu(hMenu, MF_STRING, TRAY_TEXT, sText);
	::AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
	::AppendMenu(hMenu, MF_STRING, TRAY_FOLDER, CNLS::GetString(_T("Open screenshot folder")));
	::AppendMenu(hMenu, MF_STRING | (IsAutostartEnabled() ? MF_CHECKED : 0), TRAY_AUTOSTART, CNLS::GetString(_T("Start with Windows")));
	if (IsPrintScreenTakenBySnippingTool()) {
		::AppendMenu(hMenu, MF_STRING, TRAY_KEYBOARD_SETTINGS, CNLS::GetString(_T("Print Screen key settings of Windows...")));
	}
	::AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
	::AppendMenu(hMenu, MF_STRING, TRAY_EXIT, CNLS::GetString(_T("Exit")));
	::SetMenuDefaultItem(hMenu, TRAY_CAPTURE, FALSE);
	POINT pt;
	::GetCursorPos(&pt);
	::SetForegroundWindow(hWnd);
	int nCommand = ::TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
	::PostMessage(hWnd, WM_NULL, 0, 0);
	::DestroyMenu(hMenu);
	switch (nCommand) {
		case TRAY_CAPTURE: ::PostMessage(hWnd, WM_TRAY_CAPTURE, MODE_CAPTURE, 0); break;
		case TRAY_TEXT: ::PostMessage(hWnd, WM_TRAY_CAPTURE, MODE_TEXT, 0); break;
		case TRAY_FOLDER: ::ShellExecute(NULL, _T("open"), GetScreenshotFolder(), NULL, NULL, SW_SHOWNORMAL); break;
		case TRAY_AUTOSTART: SetAutostart(!IsAutostartEnabled()); break;
		case TRAY_KEYBOARD_SETTINGS: ::ShellExecute(NULL, _T("open"), _T("ms-settings:easeofaccess-keyboard"), NULL, NULL, SW_SHOWNORMAL); break;
		case TRAY_EXIT: ::DestroyWindow(hWnd); break;
	}
}

static LRESULT CALLBACK TrayWndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
	if (nMsg == s_nTaskbarCreated && s_nTaskbarCreated != 0) {
		AddTrayIcon(hWnd, true); // Explorer was restarted
		return 0;
	}
	switch (nMsg) {
		case WM_HOTKEY:
			// the capture runs after this message, the hotkey message must return first
			::PostMessage(hWnd, WM_TRAY_CAPTURE, wParam == HOTKEY_TEXT ? MODE_TEXT : MODE_CAPTURE, 0);
			return 0;
		case WM_TRAY_CAPTURE:
			RunCapture((EMode)wParam, false);
			return 0;
		case WM_TRAY_NOTIFY:
			if (lParam == WM_LBUTTONUP) {
				::PostMessage(hWnd, WM_TRAY_CAPTURE, MODE_CAPTURE, 0);
			} else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
				ShowTrayMenu(hWnd);
			}
			return 0;
		case WM_DESTROY:
			AddTrayIcon(hWnd, false);
			::UnregisterHotKey(hWnd, HOTKEY_CAPTURE);
			::UnregisterHotKey(hWnd, HOTKEY_TEXT);
			::PostQuitMessage(0);
			return 0;
	}
	return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

static int RunTray() {
	HANDLE hMutex = ::CreateMutex(NULL, FALSE, _T("LucidViewerScreenshotTray"));
	if (::GetLastError() == ERROR_ALREADY_EXISTS) {
		// already running: a second start takes a screenshot
		HWND hTray = ::FindWindow(_T("LucidViewerScreenshotTray"), NULL);
		if (hTray != NULL) ::PostMessage(hTray, WM_TRAY_CAPTURE, MODE_CAPTURE, 0);
		if (hMutex != NULL) ::CloseHandle(hMutex);
		return 0;
	}
	HINSTANCE hInstance = ::GetModuleHandle(NULL);
	WNDCLASS wndClass = { 0 };
	wndClass.lpfnWndProc = TrayWndProc;
	wndClass.hInstance = hInstance;
	wndClass.lpszClassName = _T("LucidViewerScreenshotTray");
	::RegisterClass(&wndClass);
	s_nTaskbarCreated = ::RegisterWindowMessage(_T("TaskbarCreated"));
	HWND hWnd = ::CreateWindow(_T("LucidViewerScreenshotTray"), _T("Lucid Viewer screenshot"), WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);
	if (hWnd == NULL) return 1;
	RemoveLegacyTray();

	CString sHotkeys[3] = { _T(""), CSettingsProvider::This().ScreenshotHotkey(), CSettingsProvider::This().ScreenshotTextHotkey() };
	CString sFailed;
	for (int nHotkey = HOTKEY_CAPTURE; nHotkey <= HOTKEY_TEXT; nHotkey++) {
		s_sHotkeyText[nHotkey] = sHotkeys[nHotkey];
		UINT nModifiers, nKey;
		if (sHotkeys[nHotkey].IsEmpty()) continue;
		if (!ParseHotkey(sHotkeys[nHotkey], nModifiers, nKey) || !::RegisterHotKey(hWnd, nHotkey, nModifiers, nKey)) {
			sFailed += (sFailed.IsEmpty() ? _T("") : _T(", ")) + sHotkeys[nHotkey];
			s_sHotkeyText[nHotkey].Empty();
		}
	}
	AddTrayIcon(hWnd, true);
	if (!sFailed.IsEmpty()) {
		CString sMessage;
		sMessage.Format(CNLS::GetString(_T("The shortcut %s is used by another program. It can be changed in the INI file (ScreenshotHotkey).")), (LPCTSTR)sFailed);
		ShowBalloon(hWnd, CNLS::GetString(_T("Lucid Viewer screenshot")), sMessage);
	} else if (IsPrintScreenTakenBySnippingTool() && (sHotkeys[HOTKEY_CAPTURE].Find(_T("PrintScreen")) >= 0 || sHotkeys[HOTKEY_TEXT].Find(_T("PrintScreen")) >= 0)) {
		ShowBalloon(hWnd, CNLS::GetString(_T("Lucid Viewer screenshot")),
			CNLS::GetString(_T("Windows opens the Snipping Tool with the Print Screen key. Switch this off in Settings > Accessibility > Keyboard (right click on this icon).")));
	}

	MSG msg;
	while (::GetMessage(&msg, NULL, 0, 0) > 0) {
		::TranslateMessage(&msg);
		::DispatchMessage(&msg);
	}
	::CloseHandle(hMutex);
	return 0;
}

///////////////////////////////////////////////////////////////////////////////////
// Public functions
///////////////////////////////////////////////////////////////////////////////////

static bool HasArgument(LPCTSTR sCommandLine, LPCTSTR sArgument) {
	if (sCommandLine == NULL) return false;
	CString sLower(sCommandLine);
	sLower.MakeLower();
	int nPos = sLower.Find(sArgument);
	if (nPos < 0) return false;
	TCHAR cEnd = sLower[nPos + (int)_tcslen(sArgument)];
	return cEnd == 0 || cEnd == _T(' ') || cEnd == _T('"');
}

bool IsCommandLine(LPCTSTR sCommandLine) {
	return HasArgument(sCommandLine, _T("/tray")) || HasArgument(sCommandLine, _T("/capture")) || HasArgument(sCommandLine, _T("/capturetext"));
}

int RunCommandLine(LPCTSTR sCommandLine) {
	CNLS::ReadStringTable(CNLS::GetStringTableFileName(CSettingsProvider::This().Language()));
	Ocr::SetPreferredLanguage(CSettingsProvider::This().OcrLanguage());
	if (HasArgument(sCommandLine, _T("/tray"))) {
		return RunTray();
	}
	RunCapture(HasArgument(sCommandLine, _T("/capturetext")) ? MODE_TEXT : MODE_CAPTURE, true);
	return 0;
}

void Start(EMode eMode) {
	::ShellExecute(NULL, _T("open"), GetExeName(), eMode == MODE_TEXT ? _T("/capturetext") : _T("/capture"), NULL, SW_SHOWNORMAL);
}

void StartTray() {
	::ShellExecute(NULL, _T("open"), GetExeName(), _T("/tray"), NULL, SW_SHOWNORMAL);
}

}
