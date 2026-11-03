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
		if (pPreset == NULL) {
			CString sValidIds;
			for (const CPreset& preset : GetPresets()) {
				sValidIds += (sValidIds.IsEmpty() ? _T("") : _T(", ")) + preset.Id;
			}
			::MessageBox(NULL, CString(CNLS::GetString(_T("Unknown conversion:"))) + _T(" ") + sPresetId + _T("\n\n") +
				CNLS::GetString(_T("Valid conversions:")) + _T("\n") + sValidIds, _T(JPEGVIEW_TITLE), MB_OK | MB_ICONERROR);
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
