#include "StdAfx.h"
#include "UITest.h"
#include "DarkUI.h"
#include "SettingsEditor.h"
#include "PrintDlg.h"
#include "PrintParameters.h"
#include "JPEGImage.h"
#include "ProcessParams.h"
#include <shellapi.h>
#include "StartScreen.h"

namespace UITest {

bool IsCommandLine(LPCTSTR sCommandLine) {
	return sCommandLine != NULL && _tcsstr(sCommandLine, _T("/uitest")) != NULL;
}

// Colorful test picture for the print preview
static CJPEGImage* CreateTestImage() {
	const int nWidth = 1200, nHeight = 800;
	uint8* pPixels = new uint8[nWidth * nHeight * 4];
	for (int y = 0; y < nHeight; y++) {
		for (int x = 0; x < nWidth; x++) {
			uint8* p = pPixels + (y * nWidth + x) * 4;
			p[0] = (uint8)(255 * x / nWidth);
			p[1] = (uint8)(80 + 120 * y / nHeight);
			p[2] = (uint8)(255 - 255 * x / nWidth);
			p[3] = 255;
		}
	}
	return new CJPEGImage(nWidth, nHeight, pPixels, NULL, 4, 0, IF_Unknown, false, 0, 1, 0);
}

static int ShowPrint() {
	CPrintParameters parameters(1.0, -15.0);
	// PrintDlgEx needs an owner window
	HWND hOwner = ::CreateWindow(_T("STATIC"), _T(""), WS_POPUP, -20000, -20000, 10, 10, NULL, NULL, ::GetModuleHandle(NULL), NULL);
	CPrintDialogEx printerDialog(PD_ALLPAGES | PD_USEDEVMODECOPIES | PD_NOPAGENUMS | PD_NOSELECTION | PD_NOCURRENTPAGE, hOwner);
	printerDialog.GetDefaults();
	printerDialog.m_pdex.Flags &= ~PD_RETURNDEFAULT;
	CJPEGImage* pImage = CreateTestImage();
	CImageProcessingParams procParams(0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0);
	CPrintDlg dialog(&parameters, &printerDialog, printerDialog.GetDevMode(), printerDialog.GetDeviceName(), printerDialog.GetPortName(),
		pImage, procParams, PFLAG_None);
	dialog.SetFileName(_T("C:\\Photos\\IMG_2024_Holiday.jpg"));
	HWND hDialog = dialog.Create(NULL);
	if (hDialog != NULL) {
		DarkUI::ShowSnapshot(hDialog, IDCANCEL);
		MSG msg;
		while (::IsWindow(hDialog) && ::IsWindowVisible(hDialog) && ::GetMessage(&msg, NULL, 0, 0) > 0) {
			if (!::IsDialogMessage(hDialog, &msg)) {
				::TranslateMessage(&msg);
				::DispatchMessage(&msg);
			}
		}
		if (::IsWindow(hDialog)) ::DestroyWindow(hDialog);
	}
	if (dialog.GetPrinterDC() != NULL) ::DeleteDC(dialog.GetPrinterDC());
	delete pImage;
	::DestroyWindow(hOwner);
	return hDialog != NULL ? 0 : 1;
}

static CStartScreen* s_pGallery = NULL;

static LRESULT CALLBACK GalleryWndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
	switch (nMsg) {
		case WM_PAINT:
			if (s_pGallery != NULL) {
				PAINTSTRUCT paint;
				HDC hDC = ::BeginPaint(hWnd, &paint);
				CRect rect;
				::GetClientRect(hWnd, &rect);
				s_pGallery->Paint(hDC, rect);
				::EndPaint(hWnd, &paint);
				return 0;
			}
			break;
		case WM_ERASEBKGND:
			return 1;
		case WM_COMMAND:
			if (LOWORD(wParam) == IDCANCEL) ::DestroyWindow(hWnd);
			return 0;
	}
	return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

// Start screen with the wallpapers of Windows as photos (page 0) or empty (page 1), page 2: with the mouse over a pinned photo
static int ShowGallery(int nPage) {
	std::vector<CString> files;
	LPCTSTR patterns[] = { _T("C:\\Windows\\Web\\Wallpaper\\ThemeA\\*.jpg"), _T("C:\\Windows\\Web\\Wallpaper\\ThemeB\\*.jpg"),
		_T("C:\\Windows\\Web\\Screen\\*.jpg"), _T("C:\\Windows\\Web\\Wallpaper\\Spotlight\\*.jpg"), _T("C:\\Windows\\Web\\Wallpaper\\Windows\\*.jpg") };
	for (LPCTSTR sPattern : patterns) {
		CString sFolder(sPattern);
		sFolder = sFolder.Left(sFolder.ReverseFind(_T('\\')) + 1);
		WIN32_FIND_DATA findData;
		HANDLE hFind = ::FindFirstFile(sPattern, &findData);
		if (hFind == INVALID_HANDLE_VALUE) continue;
		do {
			files.push_back(sFolder + findData.cFileName);
		} while (::FindNextFile(hFind, &findData));
		::FindClose(hFind);
	}
	std::vector<CString> recent, pinned, screenshots;
	if (nPage != 1) {
		FILETIME now;
		::GetSystemTimeAsFileTime(&now);
		__int64 nNow = ((__int64)now.dwHighDateTime << 32) | now.dwLowDateTime;
		for (size_t i = 0; i < files.size() && i < 9; i++) {
			CString sLine;
			sLine.Format(_T("%I64d\t%s"), nNow - (__int64)i * 7 * 3600 * 10000000LL, (LPCTSTR)files[i]);
			recent.push_back(sLine);
		}
		for (size_t i = 0; i < files.size(); i++) pinned.push_back(files[files.size() - 1 - i]);
		for (size_t i = 3; i < files.size() && i < 12; i++) screenshots.push_back(files[i]);
	}
	CStartScreen::SetTestLists(recent, pinned, screenshots);

	HINSTANCE hInstance = ::GetModuleHandle(NULL);
	WNDCLASS wndClass = { 0 };
	wndClass.lpfnWndProc = GalleryWndProc;
	wndClass.hInstance = hInstance;
	wndClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	wndClass.lpszClassName = _T("LucidViewerGalleryTest");
	::RegisterClass(&wndClass);
	HWND hWnd = ::CreateWindowEx(0, _T("LucidViewerGalleryTest"), _T("Lucid Viewer"), WS_OVERLAPPEDWINDOW, 0, 0, 1440, 900, NULL, NULL, hInstance, NULL);
	if (hWnd == NULL) return 1;
	CStartScreen screen(hWnd);
	s_pGallery = &screen;
	screen.Reload();
	DarkUI::ApplyToWindow(hWnd);
	DarkUI::ShowSnapshot(hWnd, IDCANCEL, 3500);
	if (nPage == 2) {
		// paint once for the layout, then the mouse over the third pinned photo
		CRect rect;
		::GetClientRect(hWnd, &rect);
		HDC hDC = ::GetDC(hWnd);
		screen.Paint(hDC, rect);
		::ReleaseDC(hWnd, hDC);
		for (int y = rect.bottom - 10; y > rect.top; y -= 20) {
			for (int x = rect.left + 10; x < rect.right; x += 20) {
				if (screen.IsOverItem(x, y) && y > rect.Height() / 2 && x > rect.Width() / 2) {
					screen.OnMouseMove(x, y);
					y = -1;
					break;
				}
			}
		}
		::InvalidateRect(hWnd, NULL, FALSE);
	}
	MSG msg;
	while (::IsWindow(hWnd) && ::GetMessage(&msg, NULL, 0, 0) > 0) {
		::TranslateMessage(&msg);
		::DispatchMessage(&msg);
	}
	s_pGallery = NULL;
	return 0;
}

int Run(LPCTSTR sCommandLine) {
	int nArgs = 0;
	LPWSTR* pArgs = ::CommandLineToArgvW(sCommandLine, &nArgs);
	if (pArgs == NULL) return 1;
	int nIndex = -1;
	for (int i = 0; i < nArgs; i++) {
		if (_tcsicmp(pArgs[i], _T("/uitest")) == 0) nIndex = i;
	}
	if (nIndex < 0 || nIndex + 3 >= nArgs) {
		::LocalFree(pArgs);
		return 1;
	}
	CString sKind = pArgs[nIndex + 1], sTheme = pArgs[nIndex + 2], sOutput = pArgs[nIndex + 3];
	int nPage = nIndex + 4 < nArgs ? _ttoi(pArgs[nIndex + 4]) : 0;
	::LocalFree(pArgs);

	DarkUI::ForceTheme(sTheme.CompareNoCase(_T("dark")) == 0 ? 1 : 0);
	DarkUI::SetSnapshotFile(sOutput);
	int nResult = 1;
	if (sKind.CompareNoCase(_T("settings")) == 0) {
		SettingsEditor::SetSnapshotCategory(nPage);
		SettingsEditor::CResult result;
		SettingsEditor::Show(NULL, result);
		nResult = 0;
	} else if (sKind.CompareNoCase(_T("shortcuts")) == 0) {
		SettingsEditor::EditShortcuts(NULL);
		nResult = 0;
	} else if (sKind.CompareNoCase(_T("print")) == 0) {
		nResult = ShowPrint();
	} else if (sKind.CompareNoCase(_T("gallery")) == 0) {
		nResult = ShowGallery(nPage);
	}
	return ::GetFileAttributes(sOutput) != INVALID_FILE_ATTRIBUTES ? nResult : 2;
}

}
