#include "StdAfx.h"
#include "resource.h"
#include "DuplicatesDlg.h"
#include "MainDlg.h"
#include "FileList.h"
#include "HelpersGUI.h"
#include "NLS.h"
#include "Share.h"
#include <process.h>
#include <intrin.h>

static const LPCTSTR DUPLICATES_CLASS = _T("LucidViewerDuplicates");
static const UINT WM_SCAN_PROGRESS = WM_APP + 1;
static const UINT WM_SCAN_DONE = WM_APP + 2;
static const int ID_SHOW = 201, ID_RECYCLE = 202, ID_LIST = 203;
static const int MAX_HASH_DISTANCE = 6; // of 64 bits, smaller is more similar

// dHash: 9 x 8 gray pixels of the thumbnail, one bit per pair of neighbor pixels
static bool ComputeHash(HBITMAP hThumbnail, unsigned __int64& nHash) {
	BITMAP info;
	if (::GetObject(hThumbnail, sizeof(info), &info) == 0 || info.bmWidth <= 0) {
		return false;
	}
	HDC hScreen = ::GetDC(NULL);
	HDC hSource = ::CreateCompatibleDC(hScreen);
	HDC hTarget = ::CreateCompatibleDC(hScreen);
	BITMAPINFO bmi{ 0 };
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = 9;
	bmi.bmiHeader.biHeight = -8;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	void* pBits = NULL;
	HBITMAP hSmall = ::CreateDIBSection(hScreen, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
	HGDIOBJ hOldSource = ::SelectObject(hSource, hThumbnail);
	HGDIOBJ hOldTarget = ::SelectObject(hTarget, hSmall);
	::SetStretchBltMode(hTarget, HALFTONE);
	::StretchBlt(hTarget, 0, 0, 9, 8, hSource, 0, 0, info.bmWidth, abs(info.bmHeight), SRCCOPY);
	::GdiFlush();
	const BYTE* p = (const BYTE*)pBits;
	nHash = 0;
	for (int y = 0; y < 8; y++) {
		for (int x = 0; x < 8; x++) {
			const BYTE* a = p + (y * 9 + x) * 4;
			const BYTE* b = a + 4;
			int nGrayA = a[0] + 2 * a[1] + a[2], nGrayB = b[0] + 2 * b[1] + b[2];
			nHash = (nHash << 1) | (nGrayA > nGrayB ? 1 : 0);
		}
	}
	::SelectObject(hSource, hOldSource);
	::SelectObject(hTarget, hOldTarget);
	::DeleteObject(hSmall);
	::DeleteDC(hSource);
	::DeleteDC(hTarget);
	::ReleaseDC(NULL, hScreen);
	return true;
}

CDuplicatesDlg::CDuplicatesDlg(CMainDlg* pMainDlg) {
	m_pMainDlg = pMainDlg;
	m_hWnd = m_hList = m_hStatus = NULL;
	m_hFont = NULL;
	m_bDone = false;
	m_bCancel = false;
	m_hThread = NULL;
	m_nScanned = 0;
}

void CDuplicatesDlg::DoModal() {
	CFileList* pFileList = m_pMainDlg->GetFileList();
	if (pFileList == NULL) {
		return;
	}
	for (const CFileDesc& file : pFileList->GetFileList()) {
		m_files.push_back(file.GetName());
	}
	m_hashes.assign(m_files.size(), 0);
	m_hashValid.assign(m_files.size(), false);

	HINSTANCE hInstance = ::GetModuleHandle(NULL);
	WNDCLASS wndClass{ 0 };
	wndClass.lpfnWndProc = WndProc;
	wndClass.hInstance = hInstance;
	wndClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	wndClass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wndClass.lpszClassName = DUPLICATES_CLASS;
	::RegisterClass(&wndClass);

	HWND hParent = m_pMainDlg->GetHWND();
	int nW = HelpersGUI::ScaleToScreen(560), nH = HelpersGUI::ScaleToScreen(460);
	CRect parentRect;
	::GetWindowRect(hParent, &parentRect);
	// not modal: the main window shows the images selected in the list
	m_hWnd = ::CreateWindowEx(WS_EX_TOOLWINDOW, DUPLICATES_CLASS, CNLS::GetString(_T("Find similar photos")), WS_POPUP | WS_CAPTION | WS_SYSMENU,
		parentRect.right - nW - HelpersGUI::ScaleToScreen(20), parentRect.top + HelpersGUI::ScaleToScreen(80), nW, nH, hParent, NULL, hInstance, this);
	if (m_hWnd == NULL) {
		return;
	}
	CreateControls();
	::ShowWindow(m_hWnd, SW_SHOWNORMAL);
	m_hThread = (HANDLE)_beginthreadex(NULL, 0, ScanThread, this, 0, NULL);
	MSG msg;
	while (!m_bDone && ::GetMessage(&msg, NULL, 0, 0) > 0) {
		if (!::IsDialogMessage(m_hWnd, &msg)) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
	}
	m_bCancel = true;
	if (m_hThread != NULL) {
		::WaitForSingleObject(m_hThread, INFINITE);
		::CloseHandle(m_hThread);
	}
	::DestroyWindow(m_hWnd);
	::DeleteObject(m_hFont);
	::SetActiveWindow(hParent);
}

void CDuplicatesDlg::CreateControls() {
	NONCLIENTMETRICS metrics{ 0 };
	metrics.cbSize = sizeof(metrics);
	::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
	m_hFont = ::CreateFontIndirect(&metrics.lfMessageFont);
	auto S = [](int n) { return HelpersGUI::ScaleToScreen(n); };
	HINSTANCE hInstance = ::GetModuleHandle(NULL);
	m_hStatus = ::CreateWindow(_T("STATIC"), _T(""), WS_CHILD | WS_VISIBLE | SS_LEFT, S(12), S(10), S(520), S(20), m_hWnd, NULL, hInstance, NULL);
	m_hList = ::CreateWindowEx(WS_EX_CLIENTEDGE, _T("LISTBOX"), _T(""), WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
		S(12), S(36), S(520), S(330), m_hWnd, (HMENU)(INT_PTR)ID_LIST, hInstance, NULL);
	HWND hShow = ::CreateWindow(_T("BUTTON"), CNLS::GetString(_T("Show")), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
		S(12), S(378), S(120), S(28), m_hWnd, (HMENU)(INT_PTR)ID_SHOW, hInstance, NULL);
	HWND hRecycle = ::CreateWindow(_T("BUTTON"), CNLS::GetString(_T("Move to recycle bin")), WS_CHILD | WS_VISIBLE | WS_TABSTOP,
		S(140), S(378), S(180), S(28), m_hWnd, (HMENU)(INT_PTR)ID_RECYCLE, hInstance, NULL);
	HWND hClose = ::CreateWindow(_T("BUTTON"), CNLS::GetString(_T("Close")), WS_CHILD | WS_VISIBLE | WS_TABSTOP,
		S(412), S(378), S(120), S(28), m_hWnd, (HMENU)IDCANCEL, hInstance, NULL);
	HWND controls[] = { m_hStatus, m_hList, hShow, hRecycle, hClose };
	for (HWND hControl : controls) {
		::SendMessage(hControl, WM_SETFONT, (WPARAM)m_hFont, FALSE);
	}
}

unsigned __stdcall CDuplicatesDlg::ScanThread(void* pParam) {
	CDuplicatesDlg* pThis = (CDuplicatesDlg*)pParam;
	::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	for (size_t i = 0; i < pThis->m_files.size() && !pThis->m_bCancel; i++) {
		HBITMAP hThumbnail = Share::GetThumbnail(pThis->m_files[i], 64);
		if (hThumbnail != NULL) {
			unsigned __int64 nHash;
			if (ComputeHash(hThumbnail, nHash)) {
				pThis->m_hashes[i] = nHash;
				pThis->m_hashValid[i] = true;
			}
			::DeleteObject(hThumbnail);
		}
		::InterlockedIncrement(&pThis->m_nScanned);
		::PostMessage(pThis->m_hWnd, WM_SCAN_PROGRESS, 0, 0);
	}
	::CoUninitialize();
	if (!pThis->m_bCancel) {
		::PostMessage(pThis->m_hWnd, WM_SCAN_DONE, 0, 0);
	}
	return 0;
}

void CDuplicatesDlg::ShowResult() {
	// groups of similar images (union find over all pairs)
	size_t n = m_files.size();
	std::vector<size_t> group(n);
	for (size_t i = 0; i < n; i++) group[i] = i;
	auto Find = [&group](size_t i) { while (group[i] != i) { group[i] = group[group[i]]; i = group[i]; } return i; };
	for (size_t i = 0; i < n; i++) {
		if (!m_hashValid[i]) continue;
		for (size_t j = i + 1; j < n; j++) {
			if (m_hashValid[j] && __popcnt64(m_hashes[i] ^ m_hashes[j]) <= MAX_HASH_DISTANCE) {
				group[Find(j)] = Find(i);
			}
		}
	}
	std::vector<std::vector<size_t>> groups(n);
	for (size_t i = 0; i < n; i++) {
		if (m_hashValid[i]) groups[Find(i)].push_back(i);
	}
	::SendMessage(m_hList, LB_RESETCONTENT, 0, 0);
	int nGroups = 0;
	for (const std::vector<size_t>& members : groups) {
		if (members.size() < 2) continue;
		nGroups++;
		CString sHeader;
		sHeader.Format(_T("%s %d (%d)"), CNLS::GetString(_T("Group")), nGroups, (int)members.size());
		int nItem = (int)::SendMessage(m_hList, LB_ADDSTRING, 0, (LPARAM)(LPCTSTR)sHeader);
		::SendMessage(m_hList, LB_SETITEMDATA, nItem, (LPARAM)-1);
		for (size_t nFile : members) {
			const CString& sFile = m_files[nFile];
			WIN32_FILE_ATTRIBUTE_DATA data;
			CString sSize;
			if (::GetFileAttributesEx(sFile, GetFileExInfoStandard, &data)) {
				sSize.Format(_T("  (%.1f MB)"), (((__int64)data.nFileSizeHigh << 32) | data.nFileSizeLow) / (1024.0 * 1024.0));
			}
			CString sLine = CString(_T("      ")) + sFile.Mid(sFile.ReverseFind(_T('\\')) + 1) + sSize;
			nItem = (int)::SendMessage(m_hList, LB_ADDSTRING, 0, (LPARAM)(LPCTSTR)sLine);
			::SendMessage(m_hList, LB_SETITEMDATA, nItem, (LPARAM)nFile);
		}
	}
	CString sStatus;
	if (nGroups == 0) {
		sStatus.Format(CNLS::GetString(_T("No similar photos found in %d files.")), (int)n);
	} else {
		sStatus.Format(CNLS::GetString(_T("%d groups of similar photos found in %d files.")), nGroups, (int)n);
	}
	::SetWindowText(m_hStatus, sStatus);
}

CString CDuplicatesDlg::GetSelectedFile() {
	int nItem = (int)::SendMessage(m_hList, LB_GETCURSEL, 0, 0);
	if (nItem < 0) return CString();
	LPARAM nFile = ::SendMessage(m_hList, LB_GETITEMDATA, nItem, 0);
	return (nFile >= 0 && (size_t)nFile < m_files.size()) ? m_files[nFile] : CString();
}

LRESULT CALLBACK CDuplicatesDlg::WndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
	CDuplicatesDlg* pThis = (CDuplicatesDlg*)::GetWindowLongPtr(hWnd, GWLP_USERDATA);
	switch (nMsg) {
		case WM_NCCREATE:
			::SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCT*)lParam)->lpCreateParams);
			break;
		case WM_SCAN_PROGRESS:
			if (pThis != NULL) {
				CString sStatus;
				sStatus.Format(CNLS::GetString(_T("Comparing photos: %d / %d")), (int)pThis->m_nScanned, (int)pThis->m_files.size());
				::SetWindowText(pThis->m_hStatus, sStatus);
			}
			return 0;
		case WM_SCAN_DONE:
			if (pThis != NULL) pThis->ShowResult();
			return 0;
		case WM_COMMAND:
			if (pThis == NULL) break;
			if (LOWORD(wParam) == IDCANCEL) {
				pThis->m_bDone = true;
				::PostMessage(hWnd, WM_NULL, 0, 0);
				return 0;
			}
			if (LOWORD(wParam) == ID_SHOW || LOWORD(wParam) == IDOK || (LOWORD(wParam) == ID_LIST && HIWORD(wParam) == LBN_DBLCLK)) {
				CString sFile = pThis->GetSelectedFile();
				if (!sFile.IsEmpty()) pThis->m_pMainDlg->ShowFile(sFile);
				return 0;
			}
			if (LOWORD(wParam) == ID_RECYCLE) {
				CString sFile = pThis->GetSelectedFile();
				if (sFile.IsEmpty()) return 0;
				// SHFileOperation needs a double zero terminated list
				CString sFrom = sFile + _T('|');
				sFrom.Replace(_T('|'), _T('\0'));
				SHFILEOPSTRUCT operation{ 0 };
				operation.hwnd = hWnd;
				operation.wFunc = FO_DELETE;
				operation.pFrom = sFrom;
				operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
				if (::SHFileOperation(&operation) == 0 && !operation.fAnyOperationsAborted) {
					int nItem = (int)::SendMessage(pThis->m_hList, LB_GETCURSEL, 0, 0);
					::SendMessage(pThis->m_hList, LB_DELETESTRING, nItem, 0);
				}
				return 0;
			}
			break;
		case WM_CLOSE:
			if (pThis != NULL) pThis->m_bDone = true;
			::PostMessage(hWnd, WM_NULL, 0, 0);
			return 0;
	}
	return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}
