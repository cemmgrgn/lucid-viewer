#include "StdAfx.h"
#include "UITest.h"
#include "DarkUI.h"
#include "SettingsEditor.h"
#include "PrintDlg.h"
#include "PrintParameters.h"
#include "JPEGImage.h"
#include "ProcessParams.h"
#include <shellapi.h>

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
	}
	return ::GetFileAttributes(sOutput) != INVALID_FILE_ATTRIBUTES ? nResult : 2;
}

}
