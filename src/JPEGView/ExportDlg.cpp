#include "StdAfx.h"
#include "resource.h"
#include "ExportDlg.h"
#include "MainDlg.h"
#include "JPEGImage.h"
#include "FileList.h"
#include "SaveImage.h"
#include "SettingsProvider.h"
#include "HelpersGUI.h"
#include "Helpers.h"
#include "NLS.h"
#include <commctrl.h>
#include <math.h>

static const LPCTSTR EXPORT_CLASS = _T("JPEGViewExport");
static const UINT_PTR ESTIMATE_TIMER = 1;
static const int ESTIMATE_DELAY = 300; // ms after the last change
static const int ESTIMATE_LONG_EDGE = 2048; // the estimate encodes smaller copies and extrapolates the size
static const int ID_FORMAT = 101, ID_QUALITY = 102, ID_LONG_EDGE = 103, ID_METADATA = 104;

struct CExportFormat {
	LPCTSTR Name;
	LPCTSTR Ending;
	EImageFormat Format;
	bool HasQuality;
	bool Lossless;
	bool Heif;
};

static const CExportFormat s_formats[] = {
	{ _T("JPEG"), _T("jpg"), IF_JPEG, true, false, false },
	{ _T("PNG"), _T("png"), IF_PNG, false, false, false },
	{ _T("WEBP"), _T("webp"), IF_WEBP, true, false, false },
	{ _T("WEBP lossless"), _T("webp"), IF_WEBP, false, true, false },
	{ _T("HEIC"), _T("heic"), IF_HEIF, true, false, true },
	{ _T("TIFF"), _T("tif"), IF_TIFF, false, false, false },
	{ _T("BMP"), _T("bmp"), IF_WindowsBMP, false, false, false },
};
static const int FORMAT_COUNT = sizeof(s_formats) / sizeof(s_formats[0]);

CExportDlg::CExportDlg(CMainDlg* pMainDlg) {
	m_pMainDlg = pMainDlg;
	m_hWnd = NULL;
	m_hFont = NULL;
	m_bDone = m_bOK = false;
}

bool CExportDlg::DoModal() {
	CJPEGImage* pImage = m_pMainDlg->GetCurrentImage();
	if (pImage == NULL) {
		return false;
	}
	// drawings are part of the exported image
	m_pMainDlg->ApplyAnnotations();

	HINSTANCE hInstance = ::GetModuleHandle(NULL);
	WNDCLASS wndClass{ 0 };
	wndClass.lpfnWndProc = WndProc;
	wndClass.hInstance = hInstance;
	wndClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	wndClass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wndClass.lpszClassName = EXPORT_CLASS;
	::RegisterClass(&wndClass);
	INITCOMMONCONTROLSEX initControls = { sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES };
	::InitCommonControlsEx(&initControls);

	TCHAR sTempPath[MAX_PATH];
	::GetTempPath(MAX_PATH, sTempPath);
	m_sEstimateFile = CString(sTempPath) + _T("JPEGViewExportEstimate.tmp");

	HWND hParent = m_pMainDlg->GetHWND();
	int nW = HelpersGUI::ScaleToScreen(440), nH = HelpersGUI::ScaleToScreen(300);
	CRect parentRect;
	::GetWindowRect(hParent, &parentRect);
	m_hWnd = ::CreateWindowEx(WS_EX_DLGMODALFRAME, EXPORT_CLASS, CNLS::GetString(_T("Export")), WS_POPUP | WS_CAPTION | WS_SYSMENU,
		parentRect.CenterPoint().x - nW / 2, parentRect.CenterPoint().y - nH / 2, nW, nH, hParent, NULL, hInstance, this);
	if (m_hWnd == NULL) {
		return false;
	}
	CreateControls();
	UpdateControls();
	::EnableWindow(hParent, FALSE);
	::ShowWindow(m_hWnd, SW_SHOWNORMAL);
	MSG msg;
	while (!m_bDone && ::GetMessage(&msg, NULL, 0, 0) > 0) {
		if (!::IsDialogMessage(m_hWnd, &msg)) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
	}
	bool bWritten = false;
	::EnableWindow(hParent, TRUE);
	::ShowWindow(m_hWnd, SW_HIDE);
	::SetActiveWindow(hParent);
	if (m_bOK) {
		bWritten = Export();
	}
	::DestroyWindow(m_hWnd);
	::DeleteObject(m_hFont);
	::DeleteFile(m_sEstimateFile);
	return bWritten;
}

HWND CExportDlg::AddControl(LPCTSTR sClass, LPCTSTR sText, DWORD nStyle, int nX, int nY, int nWidth, int nHeight, int nId) {
	HWND hControl = ::CreateWindowEx((_tcscmp(sClass, _T("EDIT")) == 0) ? WS_EX_CLIENTEDGE : 0, sClass, sText, WS_CHILD | WS_VISIBLE | nStyle,
		HelpersGUI::ScaleToScreen(nX), HelpersGUI::ScaleToScreen(nY), HelpersGUI::ScaleToScreen(nWidth), HelpersGUI::ScaleToScreen(nHeight),
		m_hWnd, (HMENU)(INT_PTR)nId, ::GetModuleHandle(NULL), NULL);
	::SendMessage(hControl, WM_SETFONT, (WPARAM)m_hFont, FALSE);
	return hControl;
}

void CExportDlg::CreateControls() {
	NONCLIENTMETRICS metrics{ 0 };
	metrics.cbSize = sizeof(metrics);
	::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
	m_hFont = ::CreateFontIndirect(&metrics.lfMessageFont);
	CJPEGImage* pImage = m_pMainDlg->GetCurrentImage();

	AddControl(_T("STATIC"), CNLS::GetString(_T("Format:")), SS_LEFT, 16, 20, 140, 20, 0);
	m_hFormat = AddControl(_T("COMBOBOX"), _T(""), CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 160, 16, 160, 200, ID_FORMAT);
	for (int i = 0; i < FORMAT_COUNT; i++) {
		::SendMessage(m_hFormat, CB_ADDSTRING, 0, (LPARAM)CNLS::GetString(s_formats[i].Name));
	}
	::SendMessage(m_hFormat, CB_SETCURSEL, 0, 0);

	AddControl(_T("STATIC"), CNLS::GetString(_T("Quality:")), SS_LEFT, 16, 58, 140, 20, 0);
	m_hQuality = AddControl(TRACKBAR_CLASS, _T(""), TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 156, 54, 200, 28, ID_QUALITY);
	::SendMessage(m_hQuality, TBM_SETRANGE, TRUE, MAKELPARAM(1, 100));
	::SendMessage(m_hQuality, TBM_SETPOS, TRUE, CSettingsProvider::This().JPEGSaveQuality());
	m_hQualityText = AddControl(_T("STATIC"), _T(""), SS_LEFT, 362, 58, 60, 20, 0);

	AddControl(_T("STATIC"), CNLS::GetString(_T("Long edge (pixels):")), SS_LEFT, 16, 100, 140, 20, 0);
	CString sLongEdge;
	sLongEdge.Format(_T("%d"), max(pImage->OrigWidth(), pImage->OrigHeight()));
	m_hLongEdge = AddControl(_T("EDIT"), sLongEdge, ES_AUTOHSCROLL | ES_NUMBER | WS_TABSTOP, 160, 96, 90, 24, ID_LONG_EDGE);
	CString sOriginal;
	sOriginal.Format(_T("%s %d \x00D7 %d"), CNLS::GetString(_T("Original:")), pImage->OrigWidth(), pImage->OrigHeight());
	AddControl(_T("STATIC"), sOriginal, SS_LEFT, 260, 100, 170, 20, 0);

	m_hKeepMetadata = AddControl(_T("BUTTON"), CNLS::GetString(_T("Keep metadata (EXIF, GPS)")), BS_AUTOCHECKBOX | WS_TABSTOP, 16, 136, 300, 22, ID_METADATA);
	::SendMessage(m_hKeepMetadata, BM_SETCHECK, BST_CHECKED, 0);

	m_hEstimate = AddControl(_T("STATIC"), _T(""), SS_LEFT, 16, 174, 400, 20, 0);

	AddControl(_T("BUTTON"), CNLS::GetString(_T("Export...")), BS_DEFPUSHBUTTON | WS_TABSTOP, 224, 216, 94, 28, IDOK);
	AddControl(_T("BUTTON"), CNLS::GetString(_T("Cancel")), BS_PUSHBUTTON | WS_TABSTOP, 326, 216, 94, 28, IDCANCEL);
}

void CExportDlg::UpdateControls() {
	int nFormat = max(0, (int)::SendMessage(m_hFormat, CB_GETCURSEL, 0, 0));
	const CExportFormat& format = s_formats[nFormat];
	::EnableWindow(m_hQuality, format.HasQuality);
	CString sQuality;
	if (format.HasQuality) {
		sQuality.Format(_T("%d"), (int)::SendMessage(m_hQuality, TBM_GETPOS, 0, 0));
	}
	::SetWindowText(m_hQualityText, sQuality);
	// only JPEG keeps the EXIF data when written by JPEGView
	::EnableWindow(m_hKeepMetadata, format.Format == IF_JPEG);
	::SetWindowText(m_hEstimate, CNLS::GetString(_T("Estimated size: calculating...")));
	::SetTimer(m_hWnd, ESTIMATE_TIMER, ESTIMATE_DELAY, NULL);
}

CSize CExportDlg::GetTargetSize() {
	CJPEGImage* pImage = m_pMainDlg->GetCurrentImage();
	TCHAR buffer[32];
	::GetWindowText(m_hLongEdge, buffer, 32);
	int nLongEdge = _ttoi(buffer);
	int nOrigLongEdge = max(pImage->OrigWidth(), pImage->OrigHeight());
	if (nLongEdge <= 0 || nLongEdge > nOrigLongEdge * 4) {
		nLongEdge = nOrigLongEdge;
	}
	double dScale = (double)nLongEdge / nOrigLongEdge;
	return CSize(max(1, Helpers::RoundToInt(pImage->OrigWidth() * dScale)), max(1, Helpers::RoundToInt(pImage->OrigHeight() * dScale)));
}

bool CExportDlg::WriteFile(LPCTSTR sFileName, CSize targetSize) {
	CJPEGImage* pImage = m_pMainDlg->GetCurrentImage();
	int nFormat = max(0, (int)::SendMessage(m_hFormat, CB_GETCURSEL, 0, 0));
	const CExportFormat& format = s_formats[nFormat];
	int nQuality = (int)::SendMessage(m_hQuality, TBM_GETPOS, 0, 0);
	bool bKeepMetadata = ::SendMessage(m_hKeepMetadata, BM_GETCHECK, 0, 0) == BST_CHECKED;
	EProcessingFlags eFlags = m_pMainDlg->CreateDefaultProcessingFlags();
	eFlags = SetProcessingFlag(eFlags, PFLAG_HighQualityResampling, true);
	CSaveImage::SetQualityOverride(format.HasQuality ? nQuality : -1);
	bool bSuccess = format.Heif ?
		CSaveImage::SaveImageAsHEIF(sFileName, pImage, *m_pMainDlg->GetImageProcessingParams(), eFlags, targetSize, false) :
		CSaveImage::SaveImageAs(sFileName, format.Format, pImage, *m_pMainDlg->GetImageProcessingParams(), eFlags, targetSize, format.Lossless, bKeepMetadata);
	CSaveImage::SetQualityOverride(-1);
	return bSuccess;
}

// Encodes the image with the given long edge (or the target size if smaller), returns the size in bytes, 0 on error
static double EncodeSample(CExportDlg* pDlg, bool (CExportDlg::*writeFile)(LPCTSTR, CSize), LPCTSTR sFile, CSize targetSize, int nLongEdge, double& dPixels) {
	double dScale = min(1.0, (double)nLongEdge / max(targetSize.cx, targetSize.cy));
	CSize sampleSize(max(1, Helpers::RoundToInt(targetSize.cx * dScale)), max(1, Helpers::RoundToInt(targetSize.cy * dScale)));
	dPixels = (double)sampleSize.cx * sampleSize.cy;
	if (!(pDlg->*writeFile)(sFile, sampleSize)) {
		return 0;
	}
	WIN32_FILE_ATTRIBUTE_DATA fileData;
	::GetFileAttributesEx(sFile, GetFileExInfoStandard, &fileData);
	return (double)fileData.nFileSizeLow;
}

void CExportDlg::UpdateEstimate() {
	CSize targetSize = GetTargetSize();
	double dTargetPixels = (double)targetSize.cx * targetSize.cy;
	// Smaller copies need more bytes per pixel. Two copies (1024 and 2048 pixels long edge) give the trend,
	// it is extrapolated to the target size: bytes ~ pixels ^ k
	double dPixels1, dPixels2;
	double dBytes2 = EncodeSample(this, &CExportDlg::WriteFile, m_sEstimateFile, targetSize, ESTIMATE_LONG_EDGE, dPixels2);
	double dBytes = dBytes2;
	if (dBytes2 > 0 && dPixels2 < dTargetPixels) {
		double dBytes1 = EncodeSample(this, &CExportDlg::WriteFile, m_sEstimateFile, targetSize, ESTIMATE_LONG_EDGE / 2, dPixels1);
		double dExponent = (dBytes1 > 0 && dPixels1 < dPixels2) ? log(dBytes2 / dBytes1) / log(dPixels2 / dPixels1) : 1.0;
		dExponent = max(0.3, min(1.0, dExponent));
		dBytes = dBytes2 * pow(dTargetPixels / dPixels2, dExponent);
	}
	CString sText;
	if (dBytes > 0) {
		CString sSize;
		if (dBytes >= 1024 * 1024) {
			sSize.Format(_T("%.1f MB"), dBytes / (1024 * 1024));
		} else {
			sSize.Format(_T("%.0f KB"), max(1.0, dBytes / 1024));
		}
		sText.Format(_T("%s ~%s  (%d \x00D7 %d)"), CNLS::GetString(_T("Estimated size:")), (LPCTSTR)sSize, targetSize.cx, targetSize.cy);
	} else {
		sText = CNLS::GetString(_T("This format can not be written."));
	}
	m_pMainDlg->GetCurrentImage()->SetDIBInvalid(); // the estimate requested a DIB of another size
	::SetWindowText(m_hEstimate, sText);
}

bool CExportDlg::Export() {
	int nFormat = max(0, (int)::SendMessage(m_hFormat, CB_GETCURSEL, 0, 0));
	const CExportFormat& format = s_formats[nFormat];
	LPCTSTR sCurrent = m_pMainDlg->GetFileList()->Current();
	CString sProposal = (sCurrent != NULL && !m_pMainDlg->GetCurrentImage()->IsClipboardImage()) ? CString(sCurrent) : CString(_T("image"));
	int nDot = sProposal.ReverseFind(_T('.'));
	if (nDot > sProposal.ReverseFind(_T('\\'))) {
		sProposal = sProposal.Left(nDot);
	}
	sProposal += CString(_T("_export.")) + format.Ending;
	CString sFilter;
	sFilter.Format(_T("%s (*.%s)|*.%s|"), format.Name, format.Ending, format.Ending);
	CString sFileName;
	int nFilterIndex = 1;
	if (!HelpersGUI::ShowFileDialog(m_pMainDlg->GetHWND(), true, sFilter, format.Ending, sProposal, CNLS::GetString(_T("Export")), nFilterIndex, sFileName)) {
		return false;
	}
	HCURSOR hOldCursor = ::SetCursor(::LoadCursor(NULL, IDC_WAIT));
	bool bSuccess = WriteFile(sFileName, GetTargetSize());
	::SetCursor(hOldCursor);
	m_pMainDlg->GetCurrentImage()->SetDIBInvalid();
	if (!bSuccess) {
		::MessageBox(m_pMainDlg->GetHWND(), CNLS::GetString(_T("Error writing file to disk!")), CNLS::GetString(_T("Export")), MB_OK | MB_ICONERROR);
	}
	return bSuccess;
}

LRESULT CALLBACK CExportDlg::WndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
	CExportDlg* pThis = (CExportDlg*)::GetWindowLongPtr(hWnd, GWLP_USERDATA);
	switch (nMsg) {
		case WM_NCCREATE:
			::SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCT*)lParam)->lpCreateParams);
			break;
		case WM_COMMAND:
			if (pThis == NULL) break;
			if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
				pThis->m_bOK = LOWORD(wParam) == IDOK;
				pThis->m_bDone = true;
				::PostMessage(hWnd, WM_NULL, 0, 0);
				return 0;
			}
			if ((LOWORD(wParam) == ID_FORMAT && HIWORD(wParam) == CBN_SELCHANGE) || (LOWORD(wParam) == ID_LONG_EDGE && HIWORD(wParam) == EN_CHANGE) ||
				(LOWORD(wParam) == ID_METADATA && HIWORD(wParam) == BN_CLICKED)) {
				pThis->UpdateControls();
			}
			break;
		case WM_HSCROLL:
			if (pThis != NULL && (HWND)lParam == pThis->m_hQuality) {
				pThis->UpdateControls();
			}
			break;
		case WM_TIMER:
			if (pThis != NULL && wParam == ESTIMATE_TIMER) {
				::KillTimer(hWnd, ESTIMATE_TIMER);
				HCURSOR hOldCursor = ::SetCursor(::LoadCursor(NULL, IDC_WAIT));
				pThis->UpdateEstimate();
				::SetCursor(hOldCursor);
			}
			return 0;
		case WM_CLOSE:
			if (pThis != NULL) pThis->m_bDone = true;
			::PostMessage(hWnd, WM_NULL, 0, 0);
			return 0;
	}
	return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}
