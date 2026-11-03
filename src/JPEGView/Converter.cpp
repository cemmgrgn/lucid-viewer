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

namespace Converter {

	static const CPreset s_presets[] = {
		{ _T("jpg"), _T("JPG"), OF_JPEG, 100, 0, false },
		{ _T("png"), _T("PNG"), OF_PNG, 100, 0, false },
		{ _T("webp"), _T("WEBP"), OF_WEBP, 100, 0, false },
		{ _T("webp-lossless"), _T("WEBP (lossless)"), OF_WEBP_LOSSLESS, 100, 0, false },
		{ _T("bmp"), _T("BMP"), OF_BMP, 100, 0, false },
		{ _T("tiff"), _T("TIFF"), OF_TIFF, 100, 0, false },
		{ _T("gif"), _T("GIF"), OF_GIF, 100, 0, false },
		{ _T("ico"), _T("ICO (icon)"), OF_ICO, 100, 0, false },
		{ _T("pdf"), _T("PDF"), OF_PDF, 100, 0, false },
		{ _T("scale75"), _T("Scale to 75 %"), OF_SameAsSource, 75, 0, true },
		{ _T("scale50"), _T("Scale to 50 %"), OF_SameAsSource, 50, 0, false },
		{ _T("scale25"), _T("Scale to 25 %"), OF_SameAsSource, 25, 0, false },
		{ _T("rotate-left"), _T("Rotate left"), OF_SameAsSource, 100, 270, true },
		{ _T("rotate-right"), _T("Rotate right"), OF_SameAsSource, 100, 90, false },
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

	// Gets a file name next to the source file that does not exist yet: 'name.png', 'name (1).png', 'name (2).png'...
	static CString GetUniqueOutputFileName(LPCTSTR sSourceFile, LPCTSTR sEnding) {
		CString sBase = sSourceFile;
		int nDot = sBase.ReverseFind(_T('.'));
		int nSlash = sBase.ReverseFind(_T('\\'));
		if (nDot > nSlash) {
			sBase = sBase.Left(nDot);
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

	//////////////////////////////////////////////////////////////////////////////////////////////
	// Public
	//////////////////////////////////////////////////////////////////////////////////////////////

	const CPreset* GetPresets(int& nCount) {
		nCount = sizeof(s_presets) / sizeof(CPreset);
		return s_presets;
	}

	const CPreset* FindPreset(LPCTSTR sId) {
		int nCount;
		const CPreset* pPresets = GetPresets(nCount);
		for (int i = 0; i < nCount; i++) {
			if (_tcsicmp(pPresets[i].Id, sId) == 0) {
				return &pPresets[i];
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
		if (preset.ScalePercent != 100) {
			targetSize.cx = max(1, Helpers::RoundToInt(targetSize.cx * preset.ScalePercent / 100.0));
			targetSize.cy = max(1, Helpers::RoundToInt(targetSize.cy * preset.ScalePercent / 100.0));
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
			default: {
				EImageFormat eImageFormat = (eFormat == OF_JPEG) ? IF_JPEG : (eFormat == OF_PNG) ? IF_PNG : (eFormat == OF_BMP) ? IF_WindowsBMP :
					(eFormat == OF_TIFF) ? IF_TIFF : (eFormat == OF_GIF) ? IF_GIF : IF_WEBP;
				bSuccess = CSaveImage::SaveImageAs(sOutputFile, eImageFormat, pImage, params, eFlags, targetSize, eFormat == OF_WEBP_LOSSLESS);
				break;
			}
		}
		delete pImage;

		if (!bSuccess) {
			sError = CNLS::GetString(_T("Error writing file to disk!"));
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

		int nExitCode = 0;
		const CPreset* pPreset = NULL;
		std::list<CString> files;
		bool bRegister = false, bUnregister = false;
		for (int i = 1; i < nArgs; i++) {
			if (_tcsnicmp(pArgs[i], COMMAND_CONVERT, _tcslen(COMMAND_CONVERT)) == 0) {
				pPreset = FindPreset(pArgs[i] + _tcslen(COMMAND_CONVERT));
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
		if (pPreset == NULL) {
			::MessageBox(NULL, CNLS::GetString(_T("Unknown conversion. Valid conversions are: jpg, png, webp, webp-lossless, bmp, tiff, gif, ico, pdf, scale75, scale50, scale25, rotate-left, rotate-right")),
				_T(JPEGVIEW_TITLE), MB_OK | MB_ICONERROR);
			return 1;
		}

		CProcessingThreadPool::This().CreateThreadPoolThreads();

		CString sErrors;
		for (const CString& sFile : files) {
			CString sOutputFile, sError;
			if (!ConvertFile(sFile, *pPreset, sOutputFile, sError)) {
				sErrors += sFile + _T("\n    ") + sError + _T("\n");
				nExitCode = 1;
			}
		}
		if (!sErrors.IsEmpty()) {
			::MessageBox(NULL, CString(CNLS::GetString(_T("The following files could not be converted:"))) + _T("\n\n") + sErrors,
				_T(JPEGVIEW_TITLE), MB_OK | MB_ICONWARNING);
		}
		return nExitCode;
	}

	HMENU CreatePresetMenu(int nFirstCommandId) {
		HMENU hMenu = ::CreatePopupMenu();
		int nCount;
		const CPreset* pPresets = GetPresets(nCount);
		for (int i = 0; i < nCount; i++) {
			if (pPresets[i].SeparatorBefore) {
				::AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
			}
			::AppendMenu(hMenu, MF_STRING, nFirstCommandId + i, CNLS::GetString(pPresets[i].MenuText));
		}
		return hMenu;
	}

	bool RegisterExplorerMenu() {
		CString sExe = GetEXEFileName();
		int nCount;
		const CPreset* pPresets = GetPresets(nCount);
		bool bSuccess = true;
		for (LPCTSTR sEnding : s_explorerFileEndings) {
			CString sKeyName;
			sKeyName.Format(_T("Software\\Classes\\SystemFileAssociations\\.%s\\shell\\%s"), sEnding, EXPLORER_VERB_NAME);
			CRegKey verbKey;
			if (verbKey.Create(HKEY_CURRENT_USER, sKeyName) != ERROR_SUCCESS) {
				bSuccess = false;
				continue;
			}
			verbKey.SetStringValue(_T("MUIVerb"), CNLS::GetString(_T("Convert to")));
			verbKey.SetStringValue(_T("Icon"), CString(_T("\"")) + sExe + _T("\",0"));
			verbKey.SetStringValue(_T("SubCommands"), _T(""));
			verbKey.SetStringValue(_T("MultiSelectModel"), _T("Player"));
			for (int i = 0; i < nCount; i++) {
				// the key names define the order in the menu
				CString sSubKeyName;
				sSubKeyName.Format(_T("shell\\%02d_%s"), i, pPresets[i].Id);
				CRegKey presetKey;
				if (presetKey.Create(verbKey, sSubKeyName) != ERROR_SUCCESS) {
					bSuccess = false;
					continue;
				}
				presetKey.SetStringValue(_T("MUIVerb"), CNLS::GetString(pPresets[i].MenuText));
				presetKey.SetStringValue(_T("MultiSelectModel"), _T("Player"));
				if (pPresets[i].SeparatorBefore) {
					presetKey.SetDWORDValue(_T("CommandFlags"), 0x40); // ECF_SEPARATORBEFORE
				}
				CRegKey commandKey;
				if (commandKey.Create(presetKey, _T("command")) == ERROR_SUCCESS) {
					CString sCommand;
					sCommand.Format(_T("\"%s\" %s%s \"%%1\""), (LPCTSTR)sExe, COMMAND_CONVERT, pPresets[i].Id);
					commandKey.SetStringValue(NULL, sCommand);
				} else {
					bSuccess = false;
				}
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
