#include "StdAfx.h"
#include "resource.h"
#include "Converter.h"
#include "JPEGImage.h"
#include "ImageLoadThread.h"
#include "SaveImage.h"
#include "SettingsProvider.h"
#include "ProcessingThreadPool.h"
#include "Helpers.h"
#include "NLS.h"
#include <shlobj.h>
#include <shellapi.h>
#include <commctrl.h>
#include <process.h>

namespace Converter {

	struct CFormat {
		LPCTSTR Id;
		LPCTSTR MenuText;
		EOutputFormat Format;
	};

	static const CFormat s_formats[] = {
		{ _T("jpg"), _T("JPG"), OF_JPEG },
		{ _T("png"), _T("PNG"), OF_PNG },
		{ _T("webp"), _T("WEBP"), OF_WEBP },
		{ _T("webpll"), _T("WEBP (lossless)"), OF_WEBP_LOSSLESS },
		{ _T("bmp"), _T("BMP"), OF_BMP },
		{ _T("tiff"), _T("TIFF"), OF_TIFF },
		{ _T("gif"), _T("GIF"), OF_GIF },
		{ _T("ico"), _T("ICO (icon)"), OF_ICO },
		{ _T("pdf"), _T("PDF"), OF_PDF },
		{ _T("heic"), _T("HEIC"), OF_HEIC },
		// AVIF (OF_AVIF) is not offered: Windows has no AV1 encoder, it needs an encoder library such as libaom
		{ _T("same"), _T("Same format"), OF_SameAsSource },
	};

	static const COperation s_operations[] = {
		{ _T(""), _T(""), false },
		{ _T("scale75"), _T("Scale to 75 %"), true },
		{ _T("scale50"), _T("Scale to 50 %"), false },
		{ _T("scale25"), _T("Scale to 25 %"), false },
		{ _T("fit1920"), _T("Fit long edge to 1920 px"), false },
		{ _T("fit1280"), _T("Fit long edge to 1280 px"), false },
		{ _T("rotl"), _T("Rotate left"), true },
		{ _T("rotr"), _T("Rotate right"), false },
		{ _T("nometa"), _T("Remove metadata (EXIF, GPS)"), true },
	};

	// Old preset ids (first version of the Explorer menu) and their current ids
	static const LPCTSTR s_legacyIds[][2] = {
		{ _T("webp-lossless"), _T("webpll") },
		{ _T("scale75"), _T("scale75:same") },
		{ _T("scale50"), _T("scale50:same") },
		{ _T("scale25"), _T("scale25:same") },
		{ _T("rotate-left"), _T("rotl:same") },
		{ _T("rotate-right"), _T("rotr:same") },
	};

	// File endings that get the Explorer context menu, all are readable by JPEGView without extra codecs
	static const LPCTSTR s_explorerFileEndings[] = {
		_T("jpg"), _T("jpeg"), _T("jfif"), _T("png"), _T("gif"), _T("bmp"), _T("tif"), _T("tiff"), _T("webp"),
		_T("heic"), _T("heif"), _T("hif"), _T("avif"), _T("jxl"), _T("qoi"), _T("psd"), _T("tga")
	};

	static const LPCTSTR EXPLORER_VERB_NAME = _T("JPEGView.Convert");
	static const LPCTSTR COMMAND_CONVERT = _T("/convert:");
	static const LPCTSTR COMMAND_REGISTER = _T("/registerconvertmenu");
	static const LPCTSTR COMMAND_UNREGISTER = _T("/unregisterconvertmenu");

	//////////////////////////////////////////////////////////////////////////////////////////////
	// Helpers
	//////////////////////////////////////////////////////////////////////////////////////////////

	// No processing at all, the pixels are saved as they are (except for scaling)
	static CImageProcessingParams NeutralProcessingParams() {
		return CImageProcessingParams(0.0, 1.0, 1.0, 0.0, 0.0, 0.5, 0.5, 0.25, 0.5, 0.0, 0.0, 0.0);
	}

	static EProcessingFlags ConversionProcessingFlags() {
		return (EProcessingFlags)(PFLAG_HighQualityResampling | PFLAG_NoProcessingAfterLoad);
	}

	// The formats offered in the sub menu of an operation
	static bool IsFormatOfOperation(int nOperation, EOutputFormat eFormat) {
		LPCTSTR sOperation = s_operations[nOperation].Id;
		if (nOperation == 0) {
			return eFormat != OF_SameAsSource;
		}
		if (_tcscmp(sOperation, _T("nometa")) == 0) {
			return eFormat == OF_SameAsSource;
		}
		return eFormat == OF_SameAsSource || eFormat == OF_JPEG || eFormat == OF_PNG || eFormat == OF_WEBP;
	}

	static const CFormat* FindFormat(LPCTSTR sId) {
		for (const CFormat& format : s_formats) {
			if (_tcsicmp(format.Id, sId) == 0) {
				return &format;
			}
		}
		return NULL;
	}

	// Parses a conversion id 'format' or 'operation:format' with any number in the operations scaleN and fitN,
	// e.g. 'fit2560:webp'. The parsed preset has no operation group (Operation = -1). Returns false for invalid ids.
	static bool ParsePresetId(LPCTSTR sId, CPreset& preset) {
		CString sPresetId = CString(sId).Trim();
		int nColon = sPresetId.Find(_T(':'));
		CString sOperation = (nColon < 0) ? CString(_T("")) : sPresetId.Left(nColon).MakeLower();
		const CFormat* pFormat = FindFormat((nColon < 0) ? sPresetId : sPresetId.Mid(nColon + 1));
		if (pFormat == NULL || (sOperation.IsEmpty() && pFormat->Format == OF_SameAsSource)) {
			return false;
		}
		preset.Id = sOperation.IsEmpty() ? CString(pFormat->Id) : sOperation + _T(":") + pFormat->Id;
		preset.Operation = -1;
		preset.Format = pFormat->Format;
		preset.ScalePercent = 100;
		preset.MaxLongEdge = 0;
		preset.Rotation = 0;
		preset.KeepMetadata = true;
		CString sText;
		if (sOperation.IsEmpty()) {
			sText = CNLS::GetString(pFormat->MenuText);
		} else {
			bool bScale = sOperation.Left(5) == _T("scale");
			bool bFit = sOperation.Left(3) == _T("fit");
			CString sNumber = bScale ? sOperation.Mid(5) : bFit ? sOperation.Mid(3) : CString(_T(""));
			if ((bScale || bFit) && (sNumber.IsEmpty() || sNumber.SpanIncluding(_T("0123456789")) != sNumber)) {
				return false;
			}
			int nNumber = _ttoi(sNumber);
			if (bScale && nNumber >= 1 && nNumber <= 1000) {
				preset.ScalePercent = nNumber;
				sText.Format(CNLS::GetString(_T("Scale to %d %%")), nNumber);
			} else if (bFit && nNumber >= 16 && nNumber <= 65535) {
				preset.MaxLongEdge = nNumber;
				sText.Format(CNLS::GetString(_T("Fit long edge to %d px")), nNumber);
			} else if (sOperation == _T("rotl")) {
				preset.Rotation = 270;
				sText = CNLS::GetString(_T("Rotate left"));
			} else if (sOperation == _T("rotr")) {
				preset.Rotation = 90;
				sText = CNLS::GetString(_T("Rotate right"));
			} else if (sOperation == _T("rot180")) {
				preset.Rotation = 180;
				sText = CNLS::GetString(_T("Rotate 180"));
			} else if (sOperation == _T("nometa")) {
				preset.KeepMetadata = false;
				sText = CNLS::GetString(_T("Remove metadata (EXIF, GPS)"));
			} else {
				return false;
			}
			sText += CString(_T(" \x2192 ")) + CNLS::GetString(pFormat->MenuText);
		}
		preset.MenuText = sText;
		return true;
	}

	static std::vector<CPreset> CreatePresets() {
		std::vector<CPreset> presets;
		for (int nOperation = 0; nOperation < sizeof(s_operations) / sizeof(COperation); nOperation++) {
			const COperation& operation = s_operations[nOperation];
			for (const CFormat& format : s_formats) {
				if (!IsFormatOfOperation(nOperation, format.Format)) {
					continue;
				}
				CPreset preset;
				preset.Id = (nOperation == 0) ? CString(format.Id) : CString(operation.Id) + _T(":") + format.Id;
				preset.MenuText = format.MenuText;
				preset.Operation = nOperation;
				preset.Format = format.Format;
				preset.ScalePercent = 100;
				preset.MaxLongEdge = 0;
				preset.Rotation = 0;
				preset.KeepMetadata = true;
				CString sOperation = operation.Id;
				if (sOperation.Left(5) == _T("scale")) preset.ScalePercent = _ttoi(sOperation.Mid(5));
				if (sOperation.Left(3) == _T("fit")) preset.MaxLongEdge = _ttoi(sOperation.Mid(3));
				if (sOperation == _T("rotl")) preset.Rotation = 270;
				if (sOperation == _T("rotr")) preset.Rotation = 90;
				if (sOperation == _T("nometa")) preset.KeepMetadata = false;
				presets.push_back(preset);
			}
		}

		// user defined conversions from the INI file, shown after the built in ones
		CString sCustom = CSettingsProvider::This().ConvertCustomPresets();
		int nStart = 0;
		CString sToken = sCustom.Tokenize(_T(";,"), nStart);
		while (nStart >= 0) {
			CPreset preset;
			if (ParsePresetId(sToken, preset)) {
				bool bExists = false;
				for (const CPreset& existing : presets) {
					bExists |= existing.Id.CompareNoCase(preset.Id) == 0;
				}
				if (!bExists) {
					presets.push_back(preset);
				}
			}
			sToken = sCustom.Tokenize(_T(";,"), nStart);
		}
		return presets;
	}

	static LPCTSTR GetFileEnding(EOutputFormat eFormat) {
		switch (eFormat) {
			case OF_JPEG: return _T("jpg");
			case OF_PNG: return _T("png");
			case OF_WEBP:
			case OF_WEBP_LOSSLESS: return _T("webp");
			case OF_BMP: return _T("bmp");
			case OF_TIFF: return _T("tif");
			case OF_GIF: return _T("gif");
			case OF_ICO: return _T("ico");
			case OF_PDF: return _T("pdf");
			case OF_HEIC: return _T("heic");
			case OF_AVIF: return _T("avif");
		}
		return _T("jpg");
	}

	// Maps OF_SameAsSource to a writable format with the same file ending as the source file
	static EOutputFormat ResolveOutputFormat(EOutputFormat eFormat, LPCTSTR sSourceFile, CString& sEnding) {
		if (eFormat != OF_SameAsSource) {
			sEnding = GetFileEnding(eFormat);
			return eFormat;
		}
		LPCTSTR sSourceEnding = _tcsrchr(sSourceFile, _T('.'));
		CString sSource = (sSourceEnding == NULL) ? CString(_T("")) : CString(sSourceEnding + 1).MakeLower();
		if (sSource == _T("jpg") || sSource == _T("jpeg") || sSource == _T("jfif")) { sEnding = sSource; return OF_JPEG; }
		if (sSource == _T("png")) { sEnding = sSource; return OF_PNG; }
		if (sSource == _T("webp")) { sEnding = sSource; return OF_WEBP; }
		if (sSource == _T("bmp")) { sEnding = sSource; return OF_BMP; }
		if (sSource == _T("tif") || sSource == _T("tiff")) { sEnding = sSource; return OF_TIFF; }
		if (sSource == _T("gif")) { sEnding = sSource; return OF_GIF; }
		sEnding = _T("jpg");
		return OF_JPEG;
	}

	// Gets a file name in the output folder that does not exist yet: 'name.png', 'name (1).png', 'name (2).png'...
	// The output folder is the folder of the source file or the folder set with ConvertOutputFolder in the INI file.
	static CString GetUniqueOutputFileName(LPCTSTR sSourceFile, LPCTSTR sEnding) {
		CString sBase = sSourceFile;
		int nDot = sBase.ReverseFind(_T('.'));
		int nSlash = sBase.ReverseFind(_T('\\'));
		if (nDot > nSlash) {
			sBase = sBase.Left(nDot);
		}
		CString sOutputFolder = CString(CSettingsProvider::This().ConvertOutputFolder()).Trim();
		sOutputFolder.TrimRight(_T("\\/"));
		if (!sOutputFolder.IsEmpty() && nSlash >= 0) {
			bool bAbsolute = sOutputFolder.GetLength() >= 2 && (sOutputFolder[1] == _T(':') || sOutputFolder.Left(2) == _T("\\\\"));
			CString sFolder = bAbsolute ? sOutputFolder : CString(sSourceFile).Left(nSlash) + _T("\\") + sOutputFolder;
			int nResult = ::SHCreateDirectoryEx(NULL, sFolder, NULL);
			if (nResult == ERROR_SUCCESS || nResult == ERROR_ALREADY_EXISTS || nResult == ERROR_FILE_EXISTS) {
				sBase = sFolder + sBase.Mid(nSlash);
			}
		}
		CString sCandidate = sBase + _T(".") + sEnding;
		for (int i = 1; ::GetFileAttributes(sCandidate) != INVALID_FILE_ATTRIBUTES; i++) {
			sCandidate.Format(_T("%s (%d).%s"), (LPCTSTR)sBase, i, sEnding);
		}
		return sCandidate;
	}

	// Loads the image with EXIF auto rotation and the additional rotation applied, no other processing.
	// Returns NULL if the image cannot be read.
	static CJPEGImage* LoadImageForConversion(LPCTSTR sSourceFile, int nRotation, bool& bOutOfMemory) {
		CImageLoadThread loadThread;
		HANDLE hEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
		const int MAX_SIZE = 65535;
		CProcessParams processParams(MAX_SIZE, MAX_SIZE, CSize(MAX_SIZE, MAX_SIZE), CRotationParams(0), nRotation, 1.0,
			Helpers::ZM_FitToScreenNoZoom, CPoint(0, 0), NeutralProcessingParams(), ConversionProcessingFlags());
		int nHandle = loadThread.AsyncLoad(sSourceFile, 0, processParams, NULL, hEvent);
		::WaitForSingleObject(hEvent, INFINITE);
		CImageData imageData = loadThread.GetLoadedImage(nHandle);
		::CloseHandle(hEvent);
		bOutOfMemory = imageData.IsRequestFailedOutOfMemory;
		return imageData.Image;
	}

	static CString GetEXEFileName() {
		TCHAR buffer[MAX_PATH];
		::GetModuleFileName(NULL, buffer, MAX_PATH);
		return CString(buffer);
	}

	static void NotifyShellAssociationsChanged() {
		::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
	}

	// Number of presets of an operation, a sub menu is only created for operations with more than one preset
	static int CountPresetsOfOperation(int nOperation) {
		int nCount = 0;
		for (const CPreset& preset : GetPresets()) {
			if (preset.Operation == nOperation) nCount++;
		}
		return nCount;
	}

	// Writes the registry keys of a cascading Explorer menu entry, returns false on error
	static bool WriteExplorerVerb(CRegKey& parentShellKey, LPCTSTR sKeyName, LPCTSTR sText, bool bSeparatorBefore, LPCTSTR sCommand, CRegKey* pVerbKey) {
		CRegKey verbKey;
		if (verbKey.Create(parentShellKey, sKeyName) != ERROR_SUCCESS) {
			return false;
		}
		verbKey.SetStringValue(_T("MUIVerb"), sText);
		verbKey.SetStringValue(_T("MultiSelectModel"), _T("Player"));
		if (bSeparatorBefore) {
			verbKey.SetDWORDValue(_T("CommandFlags"), 0x40); // ECF_SEPARATORBEFORE
		}
		bool bSuccess = true;
		if (sCommand != NULL) {
			CRegKey commandKey;
			bSuccess = commandKey.Create(verbKey, _T("command")) == ERROR_SUCCESS && commandKey.SetStringValue(NULL, sCommand) == ERROR_SUCCESS;
		} else {
			verbKey.SetStringValue(_T("SubCommands"), _T(""));
		}
		if (pVerbKey != NULL) {
			pVerbKey->Attach(verbKey.Detach());
		}
		return bSuccess;
	}

	//////////////////////////////////////////////////////////////////////////////////////////////
	// Batch conversion: Explorer starts one process per selected file. The first process of a conversion
	// collects the files of the others (WM_COPYDATA to a message window), converts all of them one after
	// the other and shows the progress. The other processes end immediately.
	//////////////////////////////////////////////////////////////////////////////////////////////

	static const LPCTSTR COLLECTOR_CLASS = _T("JPEGViewConvertCollector");
	static const LPCTSTR PROGRESS_CLASS_NAME = _T("JPEGViewConvertProgress");
	static const ULONG_PTR COPYDATA_FILE = 0x4A56434E;
	static const UINT_PTR COLLECT_TIMER = 1;
	static const int COLLECT_IDLE_TIME = 500; // ms without new files until the conversion starts
	static const UINT WM_BATCH_PROGRESS = WM_APP + 1;
	static const UINT WM_BATCH_DONE = WM_APP + 2;
	static const int ID_CANCEL_BUTTON = 100;

	struct CBatch {
		CRITICAL_SECTION Lock;
		std::vector<CString> Files;
		const CPreset* Preset;
		size_t Done;
		volatile bool Cancel;
		bool Collecting;
		CString Errors;
		HWND ProgressWnd, TextWnd, BarWnd;
	};
	static CBatch* s_pBatch = NULL;

	static LRESULT CALLBACK CollectorWndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
		if (nMsg == WM_COPYDATA && s_pBatch != NULL) {
			COPYDATASTRUCT* pData = (COPYDATASTRUCT*)lParam;
			if (pData->dwData == COPYDATA_FILE && pData->cbData >= sizeof(TCHAR)) {
				::EnterCriticalSection(&s_pBatch->Lock);
				s_pBatch->Files.push_back(CString((LPCTSTR)pData->lpData, pData->cbData / sizeof(TCHAR) - 1));
				::LeaveCriticalSection(&s_pBatch->Lock);
				if (s_pBatch->Collecting) {
					::SetTimer(hWnd, COLLECT_TIMER, COLLECT_IDLE_TIME, NULL);
				} else if (s_pBatch->ProgressWnd != NULL) {
					::PostMessage(s_pBatch->ProgressWnd, WM_BATCH_PROGRESS, 0, 0);
				}
				return TRUE;
			}
		}
		if (nMsg == WM_TIMER && wParam == COLLECT_TIMER) {
			::KillTimer(hWnd, COLLECT_TIMER);
			s_pBatch->Collecting = false;
			::PostQuitMessage(0);
			return 0;
		}
		return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
	}

	static void UpdateProgress() {
		::EnterCriticalSection(&s_pBatch->Lock);
		size_t nTotal = s_pBatch->Files.size();
		size_t nDone = s_pBatch->Done;
		CString sCurrent = (nDone < nTotal) ? s_pBatch->Files[nDone] : CString();
		::LeaveCriticalSection(&s_pBatch->Lock);
		int nSlash = sCurrent.ReverseFind(_T('\\'));
		CString sText;
		sText.Format(_T("%s %d / %d\n%s"), CNLS::GetString(_T("Converting")), (int)min(nDone + 1, nTotal), (int)nTotal, (LPCTSTR)sCurrent.Mid(nSlash + 1));
		::SetWindowText(s_pBatch->TextWnd, sText);
		::SendMessage(s_pBatch->BarWnd, PBM_SETRANGE32, 0, (LPARAM)nTotal);
		::SendMessage(s_pBatch->BarWnd, PBM_SETPOS, (WPARAM)nDone, 0);
	}

	static LRESULT CALLBACK ProgressWndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
		switch (nMsg) {
			case WM_BATCH_PROGRESS:
				UpdateProgress();
				return 0;
			case WM_BATCH_DONE:
				::PostQuitMessage(0);
				return 0;
			case WM_COMMAND:
				if (LOWORD(wParam) == ID_CANCEL_BUTTON) {
					s_pBatch->Cancel = true;
					::EnableWindow((HWND)lParam, FALSE);
				}
				return 0;
			case WM_CLOSE:
				s_pBatch->Cancel = true;
				return 0;
		}
		return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
	}

	static unsigned __stdcall BatchThread(void*) {
		for (;;) {
			::EnterCriticalSection(&s_pBatch->Lock);
			bool bFinished = s_pBatch->Done >= s_pBatch->Files.size() || s_pBatch->Cancel;
			CString sFile = bFinished ? CString() : s_pBatch->Files[s_pBatch->Done];
			::LeaveCriticalSection(&s_pBatch->Lock);
			if (bFinished) {
				break;
			}
			if (s_pBatch->ProgressWnd != NULL) {
				::PostMessage(s_pBatch->ProgressWnd, WM_BATCH_PROGRESS, 0, 0);
			}
			CString sOutputFile, sError;
			if (!ConvertFile(sFile, *s_pBatch->Preset, sOutputFile, sError)) {
				s_pBatch->Errors += sFile + _T("\n    ") + sError + _T("\n");
			}
			::EnterCriticalSection(&s_pBatch->Lock);
			s_pBatch->Done++;
			::LeaveCriticalSection(&s_pBatch->Lock);
		}
		if (s_pBatch->ProgressWnd != NULL) {
			::PostMessage(s_pBatch->ProgressWnd, WM_BATCH_DONE, 0, 0);
		}
		return 0;
	}

	static void CreateProgressWindow() {
		HINSTANCE hInstance = ::GetModuleHandle(NULL);
		INITCOMMONCONTROLSEX initControls = { sizeof(INITCOMMONCONTROLSEX), ICC_PROGRESS_CLASS };
		::InitCommonControlsEx(&initControls);
		WNDCLASS wndClass{ 0 };
		wndClass.lpfnWndProc = ProgressWndProc;
		wndClass.hInstance = hInstance;
		wndClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
		wndClass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
		wndClass.hIcon = ::LoadIcon(hInstance, MAKEINTRESOURCE(IDR_MAINFRAME));
		wndClass.lpszClassName = PROGRESS_CLASS_NAME;
		::RegisterClass(&wndClass);

		HDC hDC = ::GetDC(NULL);
		double dScale = ::GetDeviceCaps(hDC, LOGPIXELSX) / 96.0;
		::ReleaseDC(NULL, hDC);
		int nW = (int)(420 * dScale), nH = (int)(150 * dScale), nPad = (int)(12 * dScale);
		RECT workArea;
		::SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
		HWND hWnd = ::CreateWindowEx(WS_EX_TOPMOST, PROGRESS_CLASS_NAME, CNLS::GetString(_T("Convert to")),
			WS_POPUP | WS_CAPTION | WS_SYSMENU, (workArea.left + workArea.right - nW) / 2, (workArea.top + workArea.bottom - nH) / 2,
			nW, nH, NULL, NULL, hInstance, NULL);
		RECT client;
		::GetClientRect(hWnd, &client);
		NONCLIENTMETRICS metrics{ 0 };
		metrics.cbSize = sizeof(metrics);
		::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
		HFONT hFont = ::CreateFontIndirect(&metrics.lfMessageFont);
		int nButtonW = (int)(90 * dScale), nButtonH = (int)(26 * dScale), nBarH = (int)(16 * dScale);
		s_pBatch->TextWnd = ::CreateWindow(_T("STATIC"), _T(""), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_ENDELLIPSIS, nPad, nPad,
			client.right - 2 * nPad, (int)(36 * dScale), hWnd, NULL, hInstance, NULL);
		s_pBatch->BarWnd = ::CreateWindow(PROGRESS_CLASS, _T(""), WS_CHILD | WS_VISIBLE, nPad, nPad + (int)(42 * dScale),
			client.right - 2 * nPad, nBarH, hWnd, NULL, hInstance, NULL);
		HWND hButton = ::CreateWindow(_T("BUTTON"), CNLS::GetString(_T("Cancel")), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
			client.right - nPad - nButtonW, client.bottom - nPad - nButtonH, nButtonW, nButtonH, hWnd, (HMENU)(INT_PTR)ID_CANCEL_BUTTON, hInstance, NULL);
		::SendMessage(s_pBatch->TextWnd, WM_SETFONT, (WPARAM)hFont, FALSE);
		::SendMessage(hButton, WM_SETFONT, (WPARAM)hFont, FALSE);
		s_pBatch->ProgressWnd = hWnd;
		UpdateProgress();
		::ShowWindow(hWnd, SW_SHOWNORMAL);
	}

	// Converts the files, collecting the files of the other processes started for the same conversion. Returns the exit code.
	static int RunBatch(const CPreset& preset, const std::list<CString>& files) {
		CString sBatchName = CString(_T("JPEGViewConvert_")) + preset.Id;
		sBatchName.Replace(_T(':'), _T('_'));
		HANDLE hMutex = ::CreateMutex(NULL, TRUE, sBatchName);
		bool bLeader = ::GetLastError() != ERROR_ALREADY_EXISTS;
		if (!bLeader) {
			// hand the files over to the first process, it may still be starting up
			for (int nTry = 0; nTry < 100 && !bLeader; nTry++) {
				HWND hCollector = ::FindWindowEx(HWND_MESSAGE, NULL, COLLECTOR_CLASS, sBatchName);
				if (hCollector != NULL) {
					bool bSent = true;
					for (const CString& sFile : files) {
						COPYDATASTRUCT data = { COPYDATA_FILE, (DWORD)((sFile.GetLength() + 1) * sizeof(TCHAR)), (void*)(LPCTSTR)sFile };
						DWORD_PTR nResult = 0;
						bSent &= ::SendMessageTimeout(hCollector, WM_COPYDATA, 0, (LPARAM)&data, SMTO_ABORTIFHUNG, 5000, &nResult) != 0 && nResult == TRUE;
					}
					if (bSent) {
						::CloseHandle(hMutex);
						return 0;
					}
				}
				DWORD nWait = ::WaitForSingleObject(hMutex, 30);
				bLeader = nWait == WAIT_OBJECT_0 || nWait == WAIT_ABANDONED; // the first process has ended, convert here
			}
		}

		CBatch batch;
		::InitializeCriticalSection(&batch.Lock);
		batch.Files.assign(files.begin(), files.end());
		batch.Preset = &preset;
		batch.Done = 0;
		batch.Cancel = false;
		batch.Collecting = true;
		batch.ProgressWnd = batch.TextWnd = batch.BarWnd = NULL;
		s_pBatch = &batch;

		HINSTANCE hInstance = ::GetModuleHandle(NULL);
		WNDCLASS collectorClass{ 0 };
		collectorClass.lpfnWndProc = CollectorWndProc;
		collectorClass.hInstance = hInstance;
		collectorClass.lpszClassName = COLLECTOR_CLASS;
		::RegisterClass(&collectorClass);
		HWND hCollector = ::CreateWindow(COLLECTOR_CLASS, sBatchName, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);
		MSG msg;
		if (hCollector != NULL) {
			::SetTimer(hCollector, COLLECT_TIMER, COLLECT_IDLE_TIME, NULL);
			while (::GetMessage(&msg, NULL, 0, 0) > 0) {
				::DispatchMessage(&msg);
			}
		}
		batch.Collecting = false;

		CProcessingThreadPool::This().CreateThreadPoolThreads();
		if (batch.Files.size() > 1) {
			CreateProgressWindow();
		}
		HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, BatchThread, NULL, 0, NULL);
		if (batch.ProgressWnd != NULL) {
			while (::GetMessage(&msg, NULL, 0, 0) > 0) {
				if (!::IsDialogMessage(batch.ProgressWnd, &msg)) {
					::TranslateMessage(&msg);
					::DispatchMessage(&msg);
				}
			}
		}
		::WaitForSingleObject(hThread, INFINITE);
		::CloseHandle(hThread);
		if (batch.ProgressWnd != NULL) {
			::DestroyWindow(batch.ProgressWnd);
		}
		if (hCollector != NULL) {
			::DestroyWindow(hCollector);
		}
		::ReleaseMutex(hMutex);
		::CloseHandle(hMutex);
		s_pBatch = NULL;
		::DeleteCriticalSection(&batch.Lock);

		if (!batch.Errors.IsEmpty()) {
			::MessageBox(NULL, CString(CNLS::GetString(_T("The following files could not be converted:"))) + _T("\n\n") + batch.Errors,
				_T(JPEGVIEW_TITLE), MB_OK | MB_ICONWARNING);
			return 1;
		}
		return 0;
	}

	//////////////////////////////////////////////////////////////////////////////////////////////
	// Public
	//////////////////////////////////////////////////////////////////////////////////////////////

	const std::vector<CPreset>& GetPresets() {
		static std::vector<CPreset> s_presets = CreatePresets();
		return s_presets;
	}

	const COperation* GetOperations(int& nCount) {
		nCount = sizeof(s_operations) / sizeof(COperation);
		return s_operations;
	}

	const CPreset* FindPreset(LPCTSTR sId) {
		CString sSearchId = sId;
		for (int i = 0; i < sizeof(s_legacyIds) / sizeof(s_legacyIds[0]); i++) {
			if (sSearchId.CompareNoCase(s_legacyIds[i][0]) == 0) {
				sSearchId = s_legacyIds[i][1];
				break;
			}
		}
		for (const CPreset& preset : GetPresets()) {
			if (preset.Id.CompareNoCase(sSearchId) == 0) {
				return &preset;
			}
		}
		return NULL;
	}

	bool ConvertFile(LPCTSTR sSourceFile, const CPreset& preset, CString& sOutputFile, CString& sError) {
		sOutputFile.Empty();
		sError.Empty();

		bool bOutOfMemory = false;
		CJPEGImage* pImage = LoadImageForConversion(sSourceFile, preset.Rotation, bOutOfMemory);
		if (pImage == NULL) {
			sError = CNLS::GetString(bOutOfMemory ? _T("The image could not be loaded because there is not enough memory.") : _T("The image could not be read."));
			return false;
		}

		CString sEnding;
		EOutputFormat eFormat = ResolveOutputFormat(preset.Format, sSourceFile, sEnding);
		CSize targetSize = pImage->OrigSize();
		double dScale = preset.ScalePercent / 100.0;
		if (preset.MaxLongEdge > 0) {
			int nLongEdge = max(targetSize.cx, targetSize.cy);
			dScale = min(1.0, (double)preset.MaxLongEdge / nLongEdge);
		}
		if (dScale != 1.0) {
			targetSize.cx = max(1, Helpers::RoundToInt(targetSize.cx * dScale));
			targetSize.cy = max(1, Helpers::RoundToInt(targetSize.cy * dScale));
		}

		sOutputFile = GetUniqueOutputFileName(sSourceFile, sEnding);
		CImageProcessingParams params = NeutralProcessingParams();
		EProcessingFlags eFlags = (EProcessingFlags)PFLAG_HighQualityResampling;
		bool bSuccess = false;
		switch (eFormat) {
			case OF_ICO:
				bSuccess = CSaveImage::SaveImageAsIcon(sOutputFile, pImage, params, eFlags);
				break;
			case OF_PDF:
				bSuccess = CSaveImage::SaveImageAsPDF(sOutputFile, pImage, params, eFlags, targetSize);
				break;
			case OF_HEIC:
			case OF_AVIF:
				bSuccess = CSaveImage::SaveImageAsHEIF(sOutputFile, pImage, params, eFlags, targetSize, eFormat == OF_AVIF);
				break;
			default: {
				EImageFormat eImageFormat = (eFormat == OF_JPEG) ? IF_JPEG : (eFormat == OF_PNG) ? IF_PNG : (eFormat == OF_BMP) ? IF_WindowsBMP :
					(eFormat == OF_TIFF) ? IF_TIFF : (eFormat == OF_GIF) ? IF_GIF : IF_WEBP;
				bSuccess = CSaveImage::SaveImageAs(sOutputFile, eImageFormat, pImage, params, eFlags, targetSize,
					eFormat == OF_WEBP_LOSSLESS, preset.KeepMetadata);
				break;
			}
		}
		delete pImage;

		if (!bSuccess) {
			sError = CNLS::GetString((eFormat == OF_HEIC || eFormat == OF_AVIF) ?
				_T("The HEIF/AVIF encoder of Windows is not available (HEIF and HEVC video extensions, AVIF needs Windows 11).") :
				_T("Error writing file to disk!"));
			sOutputFile.Empty();
		}
		return bSuccess;
	}

	bool IsConverterCommandLine(LPCTSTR sCommandLine) {
		return Helpers::stristr(sCommandLine, COMMAND_CONVERT) != NULL ||
			Helpers::stristr(sCommandLine, COMMAND_REGISTER) != NULL ||
			Helpers::stristr(sCommandLine, COMMAND_UNREGISTER) != NULL;
	}

	int RunCommandLine() {
		int nArgs = 0;
		LPWSTR* pArgs = ::CommandLineToArgvW(::GetCommandLineW(), &nArgs);
		if (pArgs == NULL) {
			return 1;
		}

		const CPreset* pPreset = NULL;
		CString sPresetId;
		std::list<CString> files;
		bool bRegister = false, bUnregister = false;
		for (int i = 1; i < nArgs; i++) {
			if (_tcsnicmp(pArgs[i], COMMAND_CONVERT, _tcslen(COMMAND_CONVERT)) == 0) {
				sPresetId = pArgs[i] + _tcslen(COMMAND_CONVERT);
				pPreset = FindPreset(sPresetId);
			} else if (_tcsicmp(pArgs[i], COMMAND_REGISTER) == 0) {
				bRegister = true;
			} else if (_tcsicmp(pArgs[i], COMMAND_UNREGISTER) == 0) {
				bUnregister = true;
			} else if (pArgs[i][0] != _T('/')) {
				files.push_back(CString(pArgs[i]));
			}
		}
		::LocalFree(pArgs);

		if (bRegister || bUnregister) {
			return (bRegister ? RegisterExplorerMenu() : UnregisterExplorerMenu()) ? 0 : 1;
		}
		CPreset parsedPreset;
		if (pPreset == NULL && ParsePresetId(sPresetId, parsedPreset)) {
			pPreset = &parsedPreset;
		}
		if (pPreset == NULL) {
			CString sValidIds;
			for (const CPreset& preset : GetPresets()) {
				sValidIds += (sValidIds.IsEmpty() ? _T("") : _T(", ")) + preset.Id;
			}
			::MessageBox(NULL, CString(CNLS::GetString(_T("Unknown conversion:"))) + _T(" ") + sPresetId + _T("\n\n") +
				CNLS::GetString(_T("Valid conversions:")) + _T("\n") + sValidIds, _T(JPEGVIEW_TITLE), MB_OK | MB_ICONERROR);
			return 1;
		}

		if (files.empty()) {
			return 0;
		}
		return RunBatch(*pPreset, files);
	}

	HMENU CreatePresetMenu(int nFirstCommandId) {
		HMENU hMenu = ::CreatePopupMenu();
		const std::vector<CPreset>& presets = GetPresets();
		int nOperations;
		const COperation* pOperations = GetOperations(nOperations);
		for (int nOperation = 0; nOperation < nOperations; nOperation++) {
			int nPresetsOfOperation = CountPresetsOfOperation(nOperation);
			if (nPresetsOfOperation == 0) {
				continue;
			}
			if (pOperations[nOperation].SeparatorBefore) {
				::AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
			}
			HMENU hTarget = hMenu;
			if (nOperation > 0 && nPresetsOfOperation > 1) {
				hTarget = ::CreatePopupMenu();
				::AppendMenu(hMenu, MF_POPUP | MF_STRING, (UINT_PTR)hTarget, CNLS::GetString(pOperations[nOperation].MenuText));
			}
			for (int i = 0; i < (int)presets.size(); i++) {
				if (presets[i].Operation != nOperation) {
					continue;
				}
				// a single preset of an operation is shown with the text of the operation
				LPCTSTR sText = (nOperation > 0 && nPresetsOfOperation == 1) ? pOperations[nOperation].MenuText : (LPCTSTR)presets[i].MenuText;
				::AppendMenu(hTarget, MF_STRING, nFirstCommandId + i, CNLS::GetString(sText));
			}
		}
		// user defined conversions
		bool bFirstCustom = true;
		for (int i = 0; i < (int)presets.size(); i++) {
			if (presets[i].Operation < 0) {
				if (bFirstCustom) {
					::AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
					bFirstCustom = false;
				}
				::AppendMenu(hMenu, MF_STRING, nFirstCommandId + i, presets[i].MenuText);
			}
		}
		return hMenu;
	}

	bool RegisterExplorerMenu() {
		// remove an older menu structure first
		UnregisterExplorerMenu();

		CString sExe = GetEXEFileName();
		const std::vector<CPreset>& presets = GetPresets();
		int nOperations;
		const COperation* pOperations = GetOperations(nOperations);
		bool bSuccess = true;
		for (LPCTSTR sEnding : s_explorerFileEndings) {
			CString sShellKeyName;
			sShellKeyName.Format(_T("Software\\Classes\\SystemFileAssociations\\.%s\\shell"), sEnding);
			CRegKey shellKey;
			if (shellKey.Create(HKEY_CURRENT_USER, sShellKeyName) != ERROR_SUCCESS) {
				bSuccess = false;
				continue;
			}
			CRegKey rootVerb;
			if (!WriteExplorerVerb(shellKey, EXPLORER_VERB_NAME, CNLS::GetString(_T("Convert to")), false, NULL, &rootVerb)) {
				bSuccess = false;
				continue;
			}
			rootVerb.SetStringValue(_T("Icon"), CString(_T("\"")) + sExe + _T("\",0"));
			CRegKey rootShell;
			if (rootShell.Create(rootVerb, _T("shell")) != ERROR_SUCCESS) {
				bSuccess = false;
				continue;
			}

			// the key names define the order in the menu
			int nKey = 0;
			for (int nOperation = 0; nOperation < nOperations; nOperation++) {
				int nPresetsOfOperation = CountPresetsOfOperation(nOperation);
				if (nPresetsOfOperation == 0) {
					continue;
				}
				CRegKey subShell;
				CRegKey* pTargetShell = &rootShell;
				bool bSeparatorBefore = pOperations[nOperation].SeparatorBefore;
				if (nOperation > 0 && nPresetsOfOperation > 1) {
					CString sKeyName;
					sKeyName.Format(_T("%02d_%s"), nKey++, pOperations[nOperation].Id);
					CRegKey operationVerb;
					if (!WriteExplorerVerb(rootShell, sKeyName, CNLS::GetString(pOperations[nOperation].MenuText), bSeparatorBefore, NULL, &operationVerb) ||
						subShell.Create(operationVerb, _T("shell")) != ERROR_SUCCESS) {
						bSuccess = false;
						continue;
					}
					pTargetShell = &subShell;
					bSeparatorBefore = false;
				}
				int nSubKey = 0;
				for (const CPreset& preset : presets) {
					if (preset.Operation != nOperation) {
						continue;
					}
					CString sKeyName, sCommand;
					bool bTopLevel = pTargetShell == &rootShell;
					sKeyName.Format(_T("%02d_%s"), bTopLevel ? nKey++ : nSubKey++, (LPCTSTR)preset.Id);
					sKeyName.Replace(_T(':'), _T('_'));
					sCommand.Format(_T("\"%s\" %s%s \"%%1\""), (LPCTSTR)sExe, COMMAND_CONVERT, (LPCTSTR)preset.Id);
					LPCTSTR sText = (nOperation > 0 && nPresetsOfOperation == 1) ? pOperations[nOperation].MenuText : (LPCTSTR)preset.MenuText;
					if (!WriteExplorerVerb(*pTargetShell, sKeyName, CNLS::GetString(sText), bSeparatorBefore && bTopLevel, sCommand, NULL)) {
						bSuccess = false;
					}
					bSeparatorBefore = false;
				}
			}
			// user defined conversions
			bool bFirstCustom = true;
			for (const CPreset& preset : presets) {
				if (preset.Operation >= 0) {
					continue;
				}
				CString sKeyName, sCommand;
				sKeyName.Format(_T("%02d_%s"), nKey++, (LPCTSTR)preset.Id);
				sKeyName.Replace(_T(':'), _T('_'));
				sCommand.Format(_T("\"%s\" %s%s \"%%1\""), (LPCTSTR)sExe, COMMAND_CONVERT, (LPCTSTR)preset.Id);
				if (!WriteExplorerVerb(rootShell, sKeyName, preset.MenuText, bFirstCustom, sCommand, NULL)) {
					bSuccess = false;
				}
				bFirstCustom = false;
			}
		}
		NotifyShellAssociationsChanged();
		return bSuccess;
	}

	bool UnregisterExplorerMenu() {
		bool bSuccess = true;
		for (LPCTSTR sEnding : s_explorerFileEndings) {
			CString sShellKeyName;
			sShellKeyName.Format(_T("Software\\Classes\\SystemFileAssociations\\.%s\\shell"), sEnding);
			CRegKey shellKey;
			if (shellKey.Open(HKEY_CURRENT_USER, sShellKeyName) == ERROR_SUCCESS) {
				LONG nResult = shellKey.RecurseDeleteKey(EXPLORER_VERB_NAME);
				if (nResult != ERROR_SUCCESS && nResult != ERROR_FILE_NOT_FOUND) {
					bSuccess = false;
				}
			}
		}
		NotifyShellAssociationsChanged();
		return bSuccess;
	}

	bool IsExplorerMenuRegistered() {
		CString sKeyName;
		sKeyName.Format(_T("Software\\Classes\\SystemFileAssociations\\.jpg\\shell\\%s"), EXPLORER_VERB_NAME);
		CRegKey key;
		return key.Open(HKEY_CURRENT_USER, sKeyName, KEY_READ) == ERROR_SUCCESS;
	}
}
