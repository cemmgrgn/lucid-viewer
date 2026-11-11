#include "StdAfx.h"
#include "SettingsEditor.h"
#include "SettingsProvider.h"
#include "NLS.h"
#include "KeyMap.h"
#include "Helpers.h"
#include <algorithm>
#include <functional>
#include "HelpersGUI.h"
#include <commdlg.h>
#include <commctrl.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <vector>
#include <set>
#include "DarkUI.h"

namespace SettingsEditor {

///////////////////////////////////////////////////////////////////////////////////
// Settings read from the commented global INI file
///////////////////////////////////////////////////////////////////////////////////

enum EKind { KIND_TEXT, KIND_BOOL, KIND_CHOICE, KIND_COLOR, KIND_LANGUAGE };

struct CSetting {
	CString Key, Default, Comment, Category;
	EKind Kind;
	std::vector<std::pair<CString, CString>> Options; // value, label
	CString Value, Initial;
};

static CString Trim(const CString& s) {
	CString t(s);
	t.Trim();
	return t;
}

// "ShowEXIFDateInTitle" -> "Show EXIF Date In Title"
static CString Humanize(const CString& sKey) {
	CString s;
	for (int i = 0; i < sKey.GetLength(); i++) {
		TCHAR c = sKey[i];
		bool bUpper = _istupper(c) != 0, bDigit = _istdigit(c) != 0;
		if (i > 0 && (bUpper || bDigit)) {
			TCHAR p = sKey[i - 1];
			bool bPrevLower = _istlower(p) != 0, bPrevUpper = _istupper(p) != 0, bPrevDigit = _istdigit(p) != 0;
			bool bNextLower = i + 1 < sKey.GetLength() && _istlower(sKey[i + 1]) != 0;
			if ((bUpper && (bPrevLower || bPrevDigit || (bPrevUpper && bNextLower))) || (bDigit && !bPrevDigit)) {
				s += _T(' ');
			}
		}
		s += c;
	}
	return s;
}

// Title case of an upper case header: "WINDOW BEHAVIOR OPTIONS" -> "Window behavior options"
static CString HeaderToName(const CString& sHeader) {
	CString s = Trim(sHeader);
	int nParen = s.Find(_T(" ("));
	if (nParen > 0) s = s.Left(nParen);
	CString sLower = s;
	sLower.MakeLower();
	if (!sLower.IsEmpty()) sLower.SetAt(0, _totupper(sLower[0]));
	// keep the usual abbreviations
	LPCTSTR abbreviations[] = { _T("exif"), _T("jpeg"), _T("ini"), _T("raw") };
	for (LPCTSTR sAbbreviation : abbreviations) {
		CString sUpper(sAbbreviation);
		sUpper.MakeUpper();
		CString sFind = CString(_T(" ")) + sAbbreviation;
		sLower.Replace(sFind, CString(_T(" ")) + sUpper);
		if (sLower.Find(CString(sAbbreviation)) == 0 || sLower.Left((int)_tcslen(sAbbreviation)).CompareNoCase(sAbbreviation) == 0) {
			if (sLower.GetLength() == (int)_tcslen(sAbbreviation) || sLower[(int)_tcslen(sAbbreviation)] == _T(' ')) {
				sLower = sUpper + sLower.Mid((int)_tcslen(sAbbreviation));
			}
		}
	}
	return sLower;
}

// The names and descriptions of the settings are translated with the string table (strings_xx.txt).
// The English text is the name made from the key ("ShowToolbar" -> "Show Toolbar") and the comment above the key in the
// global INI file, joined to one line with single spaces. tools/i18n/settings_strings.py writes them into strings.txt.
static CString OneLine(CString s) {
	s.Replace(_T('\n'), _T(' '));
	s.Replace(_T('\t'), _T(' '));
	while (s.Replace(_T("  "), _T(" ")) > 0);
	s.Trim();
	return s;
}

static CString SettingName(const CSetting& setting) {
	return CNLS::GetString(Humanize(setting.Key));
}

static CString SettingDescription(const CSetting& setting) {
	CString sDescription = setting.Comment;
	if (setting.Kind == KIND_LANGUAGE) {
		// the language list is in the dropdown, the file name rules are not needed here
		int nCut = sDescription.Find(_T("Lucid Viewer will"));
		if (nCut > 0) sDescription = sDescription.Left(nCut);
	}
	sDescription = OneLine(sDescription);
	return sDescription.IsEmpty() ? sDescription : CString(CNLS::GetString(sDescription));
}

static void SplitList(CString sList, std::vector<CString>& values) {
	sList.Replace(_T(" or "), _T(","));
	sList.Replace(_T(" and "), _T(","));
	sList.Remove(_T('\''));
	sList.Remove(_T('"'));
	int nStart = 0;
	CString sPart = sList.Tokenize(_T(","), nStart);
	while (nStart >= 0) {
		sPart.Trim();
		sPart.TrimRight(_T('.'));
		if (!sPart.IsEmpty() && sPart.Find(_T(' ')) < 0) values.push_back(sPart);
		sPart = sList.Tokenize(_T(","), nStart);
	}
}

// "'auto' for 2/3 of screen size" -> "auto \x2014 2/3 of screen size"
static CString OptionLabel(const CString& sValue, CString sRest) {
	sRest.Trim();
	sRest.TrimLeft(_T(":"));
	sRest.Trim();
	if (sRest.Left(4).CompareNoCase(_T("for ")) == 0) sRest = sRest.Mid(4);
	else if (sRest.Left(3).CompareNoCase(_T("to ")) == 0) sRest = sRest.Mid(3);
	sRest.TrimRight(_T(".,;"));
	return sRest.IsEmpty() ? sValue : sValue + _T(" \x2014 ") + CNLS::GetString(sRest);
}

static void ParseOptions(CSetting& setting) {
	CString sDefault = Trim(setting.Default);
	if (sDefault.CompareNoCase(_T("true")) == 0 || sDefault.CompareNoCase(_T("false")) == 0) {
		setting.Kind = KIND_BOOL;
		return;
	}
	if (setting.Key == _T("Language")) {
		setting.Kind = KIND_LANGUAGE;
		return;
	}
	if (setting.Key.Right(5) == _T("Color")) {
		int r, g, b;
		if (_stscanf_s(sDefault, _T("%d %d %d"), &r, &g, &b) == 3) {
			setting.Kind = KIND_COLOR;
			return;
		}
	}
	std::vector<std::pair<CString, CString>> options;
	int nStart = 0;
	CString sComment = setting.Comment;
	CString sLine = sComment.Tokenize(_T("\n"), nStart);
	while (nStart >= 0) {
		CString sTrimmed = Trim(sLine);
		int nColon = sTrimmed.Find(_T(':'));
		// "0: open embedded thumbnail"
		if (nColon > 0) {
			CString sValue = Trim(sTrimmed.Left(nColon));
			bool bNumber = !sValue.IsEmpty();
			for (int i = 0; i < sValue.GetLength(); i++) {
				if (!_istdigit(sValue[i]) && !(i == 0 && sValue[i] == _T('-'))) bNumber = false;
			}
			if (bNumber) options.push_back({ sValue, OptionLabel(sValue, sTrimmed.Mid(nColon + 1)) });
		}
		// "'auto' for 2/3 of screen size", "'tr'    Turkish"
		if (sTrimmed.GetLength() > 2 && sTrimmed[0] == _T('\'')) {
			int nEnd = sTrimmed.Find(_T('\''), 1);
			if (nEnd > 1) {
				CString sValue = sTrimmed.Mid(1, nEnd - 1);
				if (sValue.Find(_T(' ')) < 0) options.push_back({ sValue, OptionLabel(sValue, sTrimmed.Mid(nEnd + 1)) });
			}
		}
		// "...: dark or light" with the default value as one of the two words
		int nOr = sTrimmed.Find(_T(" or "));
		if (nOr > 0) {
			int nStart1 = nOr;
			while (nStart1 > 0 && (_istalnum(sTrimmed[nStart1 - 1]) || sTrimmed[nStart1 - 1] == _T('-'))) nStart1--;
			int nEnd2 = nOr + 4;
			while (nEnd2 < sTrimmed.GetLength() && (_istalnum(sTrimmed[nEnd2]) || sTrimmed[nEnd2] == _T('-'))) nEnd2++;
			CString sFirst = sTrimmed.Mid(nStart1, nOr - nStart1), sSecond = sTrimmed.Mid(nOr + 4, nEnd2 - nOr - 4);
			CString sDefaultValue = Trim(setting.Default);
			if (!sFirst.IsEmpty() && !sSecond.IsEmpty() && (sFirst.CompareNoCase(sDefaultValue) == 0 || sSecond.CompareNoCase(sDefaultValue) == 0)) {
				for (const CString& sValue : { sFirst, sSecond }) {
					bool bFound = false;
					for (auto& option : options) if (option.first.CompareNoCase(sValue) == 0) bFound = true;
					if (!bFound) options.push_back({ sValue, sValue });
				}
			}
		}
		// "CPUType can be AutoDetect, Generic, MMX, SSE or AVX2", "Possible values: Never, OnlyWhenNoRecycleBin, Always", "one of 'auto', 'metric' or 'english'"
		LPCTSTR markers[] = { _T("can be "), _T("Possible values:"), _T("one of ") };
		for (LPCTSTR sMarker : markers) {
			int nPos = sTrimmed.Find(sMarker);
			if (nPos >= 0) {
				CString sList = sTrimmed.Mid(nPos + (int)_tcslen(sMarker));
				int nEndOfSentence = sList.Find(_T(". "));
				if (nEndOfSentence > 0) sList = sList.Left(nEndOfSentence);
				std::vector<CString> values;
				SplitList(sList, values);
				if (values.size() >= 2) {
					for (const CString& sValue : values) {
						bool bFound = false;
						for (auto& option : options) if (option.first.CompareNoCase(sValue) == 0) bFound = true;
						if (!bFound) options.push_back({ sValue, sValue });
					}
				}
			}
		}
		sLine = sComment.Tokenize(_T("\n"), nStart);
	}
	// only a real list if the default value is one of the values
	bool bDefaultFound = false;
	for (auto& option : options) {
		if (option.first.CompareNoCase(sDefault) == 0) bDefaultFound = true;
	}
	if (options.size() >= 2 && bDefaultFound) {
		setting.Kind = KIND_CHOICE;
		setting.Options = options;
	} else {
		setting.Kind = KIND_TEXT;
	}
}

static void ReadDefinitions(LPCTSTR sFileName, std::vector<CSetting>& settings, std::vector<CString>& categories) {
	FILE* pFile = NULL;
	if (_tfopen_s(&pFile, sFileName, _T("r, ccs=UTF-8")) != 0 || pFile == NULL) {
		return;
	}
	CString sMajor, sMinor, sComment, sPrevious;
	wchar_t buffer[4096];
	while (fgetws(buffer, 4096, pFile) != NULL) {
		CString sLine(buffer);
		sLine.TrimRight(_T("\r\n"));
		CString sTrimmed = Trim(sLine);
		bool bSeparator = sTrimmed.Find(_T("; ***")) == 0 || sTrimmed.Find(_T("; ---")) == 0;
		if (sTrimmed.IsEmpty() || sTrimmed[0] == _T('[')) {
			sComment.Empty();
		} else if (bSeparator) {
			sComment.Empty();
		} else if (sTrimmed.Find(_T("; * ")) == 0 && Trim(sPrevious).Find(_T("; ***")) == 0) {
			sMajor = HeaderToName(sTrimmed.Mid(4));
			sMinor.Empty();
			sComment.Empty();
		} else if (sTrimmed.Find(_T("; *")) == 0) {
			// description of a major section
		} else if (sTrimmed.Find(_T("; - ")) == 0 && Trim(sPrevious).Find(_T("; ---")) == 0) {
			sMinor = HeaderToName(sTrimmed.Mid(4));
			sComment.Empty();
		} else if (sTrimmed[0] == _T(';')) {
			CString sText = sTrimmed.Mid(1);
			if (!sText.IsEmpty() && sText[0] == _T(' ')) sText = sText.Mid(1);
			sComment += sText + _T("\n");
		} else {
			int nEquals = sTrimmed.Find(_T('='));
			if (nEquals > 0) {
				CSetting setting;
				setting.Key = Trim(sTrimmed.Left(nEquals));
				setting.Default = sTrimmed.Mid(nEquals + 1);
				setting.Comment = sComment;
				setting.Comment.TrimRight(_T("\n"));
				setting.Category = sMinor.IsEmpty() ? sMajor : sMinor;
				if (setting.Category.IsEmpty()) setting.Category = _T("Program options");
				bool bValidKey = true;
				for (int i = 0; i < setting.Key.GetLength(); i++) {
					if (!_istalnum(setting.Key[i]) && setting.Key[i] != _T('_')) bValidKey = false;
				}
				bool bDuplicate = false;
				for (const CSetting& existing : settings) if (existing.Key.CompareNoCase(setting.Key) == 0) bDuplicate = true;
				if (bValidKey && !bDuplicate) {
					ParseOptions(setting);
					settings.push_back(setting);
					bool bFound = false;
					for (const CString& sCategory : categories) if (sCategory == setting.Category) bFound = true;
					if (!bFound) categories.push_back(setting.Category);
				}
			}
			sComment.Empty();
		}
		sPrevious = sLine;
	}
	fclose(pFile);
}

static CString ReadValue(LPCTSTR sFileName, LPCTSTR sKey, bool& bFound) {
	TCHAR buffer[2048];
	const TCHAR* MISSING = _T("\x01\x02missing");
	::GetPrivateProfileString(_T("LucidViewer"), sKey, MISSING, buffer, 2048, sFileName);
	bFound = _tcscmp(buffer, MISSING) != 0;
	return bFound ? CString(buffer) : CString();
}

///////////////////////////////////////////////////////////////////////////////////
// Window
///////////////////////////////////////////////////////////////////////////////////

static const int ID_SEARCH = 100, ID_CATEGORIES = 101, ID_SAVE = 102, ID_CANCEL = 103, ID_EDIT_INI = 104, ID_EDIT_KEYMAP = 105, ID_DEFAULTS = 106;
static const int ID_FIRST_CONTROL = 1000, ID_FIRST_RESET = 3000;

// The first page shows the settings changed most often, in this order
static const TCHAR* COMMON_CATEGORY = _T("Frequently used");
static LPCTSTR s_commonKeys[] = {
	_T("Language"), _T("ChromeTheme"), _T("ShowToolbar"), _T("ShowStatusBar"), _T("ShowSidePanel"), _T("ShowFilmstrip"), _T("SidePanelWidth"),
	_T("SingleInstance"), _T("DefaultWindowRect"), _T("NavigateWithMouseWheel"), _T("BackgroundColor"), _T("HighQualityResampling"),
	_T("JPEGSaveQuality"), _T("WEBPSaveQuality"), _T("DeleteConfirmation"), _T("ScreenshotHotkey"), _T("ScreenshotTextHotkey"),
	_T("ScreenshotFolder"), _T("OcrLanguage"), _T("ConvertOutputFolder"), _T("ShowStartScreen"),
};

struct CEditor {
	HWND Window, Page, Search, Categories;
	HFONT Font, BoldFont, SmallFont, TitleFont;
	int Dpi;
	std::vector<CSetting> Settings;
	std::vector<CString> CategoryNames;
	std::vector<int> Shown; // indices into Settings of the controls on the page
	std::vector<HWND> ShownControls;
	std::set<HWND> Descriptions;
	std::set<HWND> Lines;
	int Scroll, PageHeight;
	bool Done, Saved, Updating, KeyMapChanged, Sizing;
	std::vector<std::pair<HWND, bool>> Buttons; // button, at the left side
	int S(int n) { return ::MulDiv(n, Dpi, 96); }
};

static CEditor* s_pEditor = NULL;

static COLORREF ParseColor(const CString& sValue) {
	int r = 0, g = 0, b = 0;
	_stscanf_s(sValue, _T("%d %d %d"), &r, &g, &b);
	return RGB(max(0, min(255, r)), max(0, min(255, g)), max(0, min(255, b)));
}

static void SetControlValue(CEditor& editor, int nSetting, HWND hControl) {
	CSetting& setting = editor.Settings[nSetting];
	editor.Updating = true;
	switch (setting.Kind) {
		case KIND_BOOL: {
			bool bOn = setting.Value.CompareNoCase(_T("true")) == 0;
			::SendMessage(hControl, BM_SETCHECK, bOn ? BST_CHECKED : BST_UNCHECKED, 0);
			::SetWindowText(hControl, CNLS::GetString(bOn ? _T("On") : _T("Off")));
			break;
		}
		case KIND_CHOICE:
		case KIND_LANGUAGE: {
			int nCount = (int)::SendMessage(hControl, CB_GETCOUNT, 0, 0);
			int nSelect = -1;
			for (int i = 0; i < nCount; i++) {
				int nOption = (int)::SendMessage(hControl, CB_GETITEMDATA, i, 0);
				if (nOption >= 0 && nOption < (int)setting.Options.size() && setting.Options[nOption].first.CompareNoCase(Trim(setting.Value)) == 0) nSelect = i;
			}
			::SendMessage(hControl, CB_SETCURSEL, nSelect, 0);
			if (nSelect < 0) ::SetWindowText(hControl, setting.Value);
			// the text of the edit field is not shown selected
			::SendMessage(hControl, CB_SETEDITSEL, 0, MAKELPARAM(-1, 0));
			break;
		}
		case KIND_COLOR:
		case KIND_TEXT:
			::SetWindowText(hControl, setting.Value);
			break;
	}
	editor.Updating = false;
}

static void LayoutPage(CEditor& editor) {
	// nothing is drawn while the page is built, else the rows appear one by one
	::SendMessage(editor.Page, WM_SETREDRAW, FALSE, 0);
	// removes the old controls
	HWND hChild = ::GetWindow(editor.Page, GW_CHILD);
	while (hChild != NULL) {
		HWND hNext = ::GetWindow(hChild, GW_HWNDNEXT);
		::DestroyWindow(hChild);
		hChild = hNext;
	}
	editor.Descriptions.clear();
	editor.Lines.clear();
	editor.ShownControls.clear();
	editor.Shown.clear();
	editor.Scroll = 0;

	// the settings of the selected category or all matching the search text
	TCHAR sSearch[256];
	::GetWindowText(editor.Search, sSearch, 256);
	CString sFind = Trim(sSearch);
	sFind.MakeLower();
	int nCategory = (int)::SendMessage(editor.Categories, LB_GETCURSEL, 0, 0);
	CString sCategory = (nCategory >= 0 && nCategory < (int)editor.CategoryNames.size()) ? editor.CategoryNames[nCategory] : CString();
	for (int i = 0; i < (int)editor.Settings.size(); i++) {
		const CSetting& setting = editor.Settings[i];
		if (sFind.IsEmpty()) {
			if (setting.Category == sCategory) editor.Shown.push_back(i);
		} else if (sCategory != COMMON_CATEGORY || true) {
			// the English and the translated texts
			CString sText = setting.Key + _T(" ") + Humanize(setting.Key) + _T(" ") + setting.Comment + _T(" ") + CNLS::GetString(setting.Category) +
				_T(" ") + SettingName(setting) + _T(" ") + SettingDescription(setting);
			sText.MakeLower();
			if (sText.Find(sFind) >= 0) editor.Shown.push_back(i);
		}
	}

	if (sFind.IsEmpty() && sCategory == COMMON_CATEGORY) {
		for (LPCTSTR sKey : s_commonKeys) {
			for (int i = 0; i < (int)editor.Settings.size(); i++) {
				if (editor.Settings[i].Key.CompareNoCase(sKey) == 0) editor.Shown.push_back(i);
			}
		}
	}

	CRect pageRect;
	::GetClientRect(editor.Page, &pageRect);
	HINSTANCE hInstance = ::GetModuleHandle(NULL);
	int nPad = editor.S(16);
	int nControlWidth = editor.S(230), nResetWidth = editor.S(28);
	int nTextWidth = pageRect.Width() - 3 * nPad - nControlWidth - nResetWidth - editor.S(::GetSystemMetrics(SM_CXVSCROLL) > 0 ? 4 : 0);
	int nY = nPad;
	// page title: the category or the search
	{
		CString sTitle = sFind.IsEmpty() ? CString(CNLS::GetString(sCategory)) : CString(CNLS::GetString(_T("Search results")));
		HWND hTitle = ::CreateWindow(_T("STATIC"), sTitle, WS_CHILD | WS_VISIBLE | SS_NOPREFIX, nPad, nY, pageRect.Width() - 2 * nPad, editor.S(34),
			editor.Page, NULL, hInstance, NULL);
		::SendMessage(hTitle, WM_SETFONT, (WPARAM)editor.TitleFont, FALSE);
		nY += editor.S(44);
	}
	HDC hDC = ::GetDC(editor.Page);
	CString sLastCategory;
	for (int nIndex = 0; nIndex < (int)editor.Shown.size(); nIndex++) {
		int nSetting = editor.Shown[nIndex];
		CSetting& setting = editor.Settings[nSetting];
		// in the search result the category is shown as header
		if (!sFind.IsEmpty() && setting.Category != sLastCategory) {
			sLastCategory = setting.Category;
			HWND hHeader = ::CreateWindow(_T("STATIC"), CNLS::GetString(setting.Category), WS_CHILD | WS_VISIBLE | SS_NOPREFIX,
				nPad, nY, pageRect.Width() - 2 * nPad, editor.S(22), editor.Page, NULL, hInstance, NULL);
			::SendMessage(hHeader, WM_SETFONT, (WPARAM)editor.BoldFont, FALSE);
			editor.Descriptions.insert(hHeader);
			nY += editor.S(26);
		}
		CString sLabel = SettingName(setting);
		CString sDescription = SettingDescription(setting);
		sDescription += (sDescription.IsEmpty() ? _T("") : _T("  ")) + CString(_T("[")) + setting.Key + _T("]");
		::SelectObject(hDC, editor.SmallFont);
		CRect descriptionRect(0, 0, nTextWidth, 0);
		::DrawText(hDC, sDescription, -1, &descriptionRect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
		// a long translated name takes more lines
		::SelectObject(hDC, editor.BoldFont);
		CRect labelRect(0, 0, nTextWidth, 0);
		::DrawText(hDC, sLabel, -1, &labelRect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
		int nLabelHeight = max((int)labelRect.Height() + editor.S(2), editor.S(20));
		HWND hLabel = ::CreateWindow(_T("STATIC"), sLabel, WS_CHILD | WS_VISIBLE | SS_NOPREFIX, nPad, nY, nTextWidth, nLabelHeight,
			editor.Page, NULL, hInstance, NULL);
		::SendMessage(hLabel, WM_SETFONT, (WPARAM)editor.BoldFont, FALSE);
		HWND hDescription = ::CreateWindow(_T("STATIC"), sDescription, WS_CHILD | WS_VISIBLE | SS_NOPREFIX, nPad, nY + nLabelHeight,
			nTextWidth, descriptionRect.Height(), editor.Page, NULL, hInstance, NULL);
		::SendMessage(hDescription, WM_SETFONT, (WPARAM)editor.SmallFont, FALSE);
		editor.Descriptions.insert(hDescription);

		int nX = pageRect.Width() - nPad - nResetWidth - editor.S(4) - nControlWidth;
		int nId = ID_FIRST_CONTROL + nIndex;
		HWND hControl = NULL;
		switch (setting.Kind) {
			case KIND_BOOL:
				hControl = ::CreateWindow(_T("BUTTON"), CNLS::GetString(_T("On")), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
					nX, nY, nControlWidth, editor.S(24), editor.Page, (HMENU)(INT_PTR)nId, hInstance, NULL);
				break;
			case KIND_CHOICE:
			case KIND_LANGUAGE:
				hControl = ::CreateWindow(_T("COMBOBOX"), _T(""), WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
					(setting.Kind == KIND_LANGUAGE ? CBS_DROPDOWNLIST : CBS_DROPDOWN),
					nX, nY, nControlWidth, editor.S(300), editor.Page, (HMENU)(INT_PTR)nId, hInstance, NULL);
				for (int nOption = 0; nOption < (int)setting.Options.size(); nOption++) {
					int nItem = (int)::SendMessage(hControl, CB_ADDSTRING, 0, (LPARAM)(LPCTSTR)setting.Options[nOption].second);
					::SendMessage(hControl, CB_SETITEMDATA, nItem, nOption);
				}
				::SendMessage(hControl, CB_SETDROPPEDWIDTH, editor.S(420), 0);
				break;
			case KIND_COLOR:
				hControl = ::CreateWindowEx(WS_EX_CLIENTEDGE, _T("EDIT"), _T(""), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
					nX, nY, nControlWidth - editor.S(70), editor.S(24), editor.Page, (HMENU)(INT_PTR)nId, hInstance, NULL);
				{
					HWND hPick = ::CreateWindow(_T("BUTTON"), CNLS::GetString(_T("Color...")), WS_CHILD | WS_VISIBLE | WS_TABSTOP,
						nX + nControlWidth - editor.S(66), nY, editor.S(66), editor.S(24), editor.Page, (HMENU)(INT_PTR)(nId + 1000), hInstance, NULL);
					::SendMessage(hPick, WM_SETFONT, (WPARAM)editor.Font, FALSE);
				}
				break;
			default:
				hControl = ::CreateWindowEx(WS_EX_CLIENTEDGE, _T("EDIT"), _T(""), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
					nX, nY, nControlWidth, editor.S(24), editor.Page, (HMENU)(INT_PTR)nId, hInstance, NULL);
				break;
		}
		::SendMessage(hControl, WM_SETFONT, (WPARAM)editor.Font, FALSE);
		editor.ShownControls.push_back(hControl);
		SetControlValue(editor, nSetting, hControl);
		HWND hReset = ::CreateWindow(_T("BUTTON"), _T("\x21BA"), WS_CHILD | WS_VISIBLE | WS_TABSTOP,
			pageRect.Width() - nPad - nResetWidth, nY, nResetWidth, editor.S(24), editor.Page, (HMENU)(INT_PTR)(ID_FIRST_RESET + nIndex), hInstance, NULL);
		::SendMessage(hReset, WM_SETFONT, (WPARAM)editor.Font, FALSE);
		nY += max(nLabelHeight + descriptionRect.Height(), editor.S(26)) + editor.S(10);
		// thin line between the settings
		HWND hLine = ::CreateWindow(_T("STATIC"), _T(""), WS_CHILD | WS_VISIBLE, nPad, nY, pageRect.Width() - 2 * nPad, 1,
			editor.Page, NULL, hInstance, NULL);
		editor.Lines.insert(hLine);
		nY += editor.S(10);
	}
	::ReleaseDC(editor.Page, hDC);
	if (editor.Shown.empty()) {
		HWND hEmpty = ::CreateWindow(_T("STATIC"), CNLS::GetString(_T("No setting found.")), WS_CHILD | WS_VISIBLE,
			nPad, nPad, pageRect.Width() - 2 * nPad, editor.S(24), editor.Page, NULL, hInstance, NULL);
		::SendMessage(hEmpty, WM_SETFONT, (WPARAM)editor.Font, FALSE);
	}
	editor.PageHeight = nY;
	SCROLLINFO info = { sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_POS, 0, max(0, nY - 1), (UINT)pageRect.Height(), 0 };
	::SetScrollInfo(editor.Page, SB_VERT, &info, TRUE);
	DarkUI::ApplyToWindow(editor.Page);
	// the theme change selects the text of the combo boxes again, it is not shown selected
	for (HWND hControl : editor.ShownControls) {
		HWND hEdit = ::FindWindowEx(hControl, NULL, _T("Edit"), NULL);
		if (hEdit != NULL) ::SendMessage(hEdit, EM_SETSEL, (WPARAM)-1, 0);
	}
	::SendMessage(editor.Page, WM_SETREDRAW, TRUE, 0);
	::RedrawWindow(editor.Page, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
}

static void ScrollPage(CEditor& editor, int nNewScroll) {
	CRect pageRect;
	::GetClientRect(editor.Page, &pageRect);
	nNewScroll = max(0, min(nNewScroll, editor.PageHeight - pageRect.Height()));
	if (nNewScroll == editor.Scroll) return;
	::ScrollWindowEx(editor.Page, 0, editor.Scroll - nNewScroll, NULL, NULL, NULL, NULL, SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
	editor.Scroll = nNewScroll;
	SCROLLINFO info = { sizeof(SCROLLINFO), SIF_POS };
	info.nPos = nNewScroll;
	::SetScrollInfo(editor.Page, SB_VERT, &info, TRUE);
}

static void OnControlChanged(CEditor& editor, int nIndex, int nCode) {
	if (editor.Updating || nIndex < 0 || nIndex >= (int)editor.Shown.size()) return;
	CSetting& setting = editor.Settings[editor.Shown[nIndex]];
	HWND hControl = editor.ShownControls[nIndex];
	switch (setting.Kind) {
		case KIND_BOOL: {
			bool bOn = ::SendMessage(hControl, BM_GETCHECK, 0, 0) == BST_CHECKED;
			setting.Value = bOn ? _T("true") : _T("false");
			::SetWindowText(hControl, CNLS::GetString(bOn ? _T("On") : _T("Off")));
			break;
		}
		case KIND_CHOICE:
		case KIND_LANGUAGE:
			if (nCode == CBN_SELCHANGE) {
				int nItem = (int)::SendMessage(hControl, CB_GETCURSEL, 0, 0);
				int nOption = nItem >= 0 ? (int)::SendMessage(hControl, CB_GETITEMDATA, nItem, 0) : -1;
				if (nOption >= 0 && nOption < (int)setting.Options.size()) setting.Value = setting.Options[nOption].first;
			} else if (nCode == CBN_EDITCHANGE) {
				TCHAR sText[2048];
				::GetWindowText(hControl, sText, 2048);
				setting.Value = sText;
			}
			break;
		default: {
			TCHAR sText[2048];
			::GetWindowText(hControl, sText, 2048);
			setting.Value = sText;
			break;
		}
	}
}

static LRESULT CALLBACK PageWndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
	CEditor* pEditor = s_pEditor;
	if (pEditor == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
	switch (nMsg) {
		case WM_VSCROLL: {
			SCROLLINFO info = { sizeof(SCROLLINFO), SIF_ALL };
			::GetScrollInfo(hWnd, SB_VERT, &info);
			int nPos = pEditor->Scroll;
			switch (LOWORD(wParam)) {
				case SB_LINEUP: nPos -= pEditor->S(40); break;
				case SB_LINEDOWN: nPos += pEditor->S(40); break;
				case SB_PAGEUP: nPos -= info.nPage; break;
				case SB_PAGEDOWN: nPos += info.nPage; break;
				case SB_THUMBTRACK: case SB_THUMBPOSITION: nPos = info.nTrackPos; break;
				case SB_TOP: nPos = 0; break;
				case SB_BOTTOM: nPos = pEditor->PageHeight; break;
			}
			ScrollPage(*pEditor, nPos);
			return 0;
		}
		case WM_MOUSEWHEEL:
			ScrollPage(*pEditor, pEditor->Scroll - GET_WHEEL_DELTA_WPARAM(wParam) * pEditor->S(90) / WHEEL_DELTA);
			return 0;
		case WM_CTLCOLORSTATIC:
			if (pEditor->Lines.count((HWND)lParam) > 0) return (LRESULT)DarkUI::Brush(DarkUI::Colors().Border);
			return (LRESULT)DarkUI::CtlColor(nMsg, wParam, lParam, DarkUI::Colors().Surface, pEditor->Descriptions.count((HWND)lParam) > 0);
		case WM_CTLCOLORBTN:
		case WM_CTLCOLOREDIT:
		case WM_CTLCOLORLISTBOX:
			return (LRESULT)DarkUI::CtlColor(nMsg, wParam, lParam, DarkUI::Colors().Surface);
		case WM_ERASEBKGND: {
			RECT rect;
			::GetClientRect(hWnd, &rect);
			::FillRect((HDC)wParam, &rect, DarkUI::Brush(DarkUI::Colors().Surface));
			return 1;
		}
		case WM_NOTIFY: {
			LRESULT result = 0;
			if (DarkUI::CustomDrawButton(lParam, result)) return result;
			break;
		}
		case WM_COMMAND: {
			int nId = LOWORD(wParam), nCode = HIWORD(wParam);
			if (nId >= ID_FIRST_RESET && nId < ID_FIRST_RESET + (int)pEditor->Shown.size()) {
				int nIndex = nId - ID_FIRST_RESET;
				CSetting& setting = pEditor->Settings[pEditor->Shown[nIndex]];
				setting.Value = setting.Default;
				SetControlValue(*pEditor, pEditor->Shown[nIndex], pEditor->ShownControls[nIndex]);
			} else if (nId >= ID_FIRST_CONTROL + 1000 && nId < ID_FIRST_CONTROL + 1000 + (int)pEditor->Shown.size()) {
				// color picker
				int nIndex = nId - ID_FIRST_CONTROL - 1000;
				CSetting& setting = pEditor->Settings[pEditor->Shown[nIndex]];
				static COLORREF customColors[16];
				CHOOSECOLOR choose = { sizeof(CHOOSECOLOR) };
				choose.hwndOwner = pEditor->Window;
				choose.rgbResult = ParseColor(setting.Value);
				choose.lpCustColors = customColors;
				choose.Flags = CC_FULLOPEN | CC_RGBINIT;
				if (::ChooseColor(&choose)) {
					setting.Value.Format(_T("%d %d %d"), GetRValue(choose.rgbResult), GetGValue(choose.rgbResult), GetBValue(choose.rgbResult));
					SetControlValue(*pEditor, pEditor->Shown[nIndex], pEditor->ShownControls[nIndex]);
				}
			} else if (nId >= ID_FIRST_CONTROL && nId < ID_FIRST_CONTROL + (int)pEditor->Shown.size()) {
				if (nCode == BN_CLICKED || nCode == CBN_SELCHANGE || nCode == CBN_EDITCHANGE || nCode == EN_CHANGE) {
					OnControlChanged(*pEditor, nId - ID_FIRST_CONTROL, nCode);
				}
			}
			return 0;
		}
	}
	return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

static bool SaveSettings(CEditor& editor) {
	CSettingsProvider& sp = CSettingsProvider::This();
	CString sUserINI = sp.GetUserINIFileName();
	int nSlash = sUserINI.ReverseFind(_T('\\'));
	if (nSlash > 0) ::SHCreateDirectoryEx(NULL, sUserINI.Left(nSlash), NULL);
	if (::GetFileAttributes(sUserINI) == INVALID_FILE_ATTRIBUTES) {
		// a new user INI file in UTF-16 keeps non ASCII values
		FILE* pFile = NULL;
		if (_tfopen_s(&pFile, sUserINI, _T("w, ccs=UTF-16LE")) == 0 && pFile != NULL) {
			fputws(L"[LucidViewer]\n", pFile);
			fclose(pFile);
		}
	}
	bool bOK = true;
	for (CSetting& setting : editor.Settings) {
		if (setting.Value != setting.Initial) {
			if (!::WritePrivateProfileString(_T("LucidViewer"), setting.Key, setting.Value, sUserINI)) bOK = false;
		}
	}
	return bOK;
}

static bool HasChanges(CEditor& editor) {
	for (CSetting& setting : editor.Settings) if (setting.Value != setting.Initial) return true;
	return false;
}

// Places the search field, the category list, the page and the buttons into the window, the buttons are as wide as their text
static void LayoutWindow(CEditor& editor) {
	if (editor.Page == NULL) return;
	CRect client;
	::GetClientRect(editor.Window, &client);
	int nPad = editor.S(10), nListWidth = editor.S(240), nButtonHeight = editor.S(30);
	::MoveWindow(editor.Search, nPad, nPad, nListWidth, editor.S(26), TRUE);
	::MoveWindow(editor.Categories, nPad, nPad + editor.S(32), nListWidth, max(0, client.Height() - 3 * nPad - nButtonHeight - editor.S(32)), TRUE);
	::MoveWindow(editor.Page, 2 * nPad + nListWidth, nPad, max(0, client.Width() - 3 * nPad - nListWidth), max(0, client.Height() - 3 * nPad - nButtonHeight), TRUE);
	HDC hDC = ::GetDC(editor.Window);
	HGDIOBJ hOldFont = ::SelectObject(hDC, editor.Font);
	int nLeftX = nPad, nRightX = client.Width() - nPad;
	int nButtonY = client.Height() - nPad - nButtonHeight;
	for (int i = (int)editor.Buttons.size() - 1; i >= 0; i--) {
		bool bLeft = editor.Buttons[i].second;
		HWND hButton = editor.Buttons[i].first;
		TCHAR sText[256] = { 0 };
		::GetWindowText(hButton, sText, 256);
		CRect textRect(0, 0, 0, 0);
		::DrawText(hDC, sText, -1, &textRect, DT_CALCRECT | DT_SINGLELINE);
		int nButtonWidth = max(editor.S(bLeft ? 120 : 100), (int)textRect.Width() + editor.S(28));
		if (!bLeft) {
			nRightX -= nButtonWidth;
			::MoveWindow(hButton, nRightX, nButtonY, nButtonWidth, nButtonHeight, TRUE);
			nRightX -= editor.S(8);
		}
	}
	for (auto& button : editor.Buttons) {
		if (!button.second) continue;
		TCHAR sText[256] = { 0 };
		::GetWindowText(button.first, sText, 256);
		CRect textRect(0, 0, 0, 0);
		::DrawText(hDC, sText, -1, &textRect, DT_CALCRECT | DT_SINGLELINE);
		int nButtonWidth = max(editor.S(120), (int)textRect.Width() + editor.S(28));
		::MoveWindow(button.first, nLeftX, nButtonY, nButtonWidth, nButtonHeight, TRUE);
		nLeftX += nButtonWidth + editor.S(8);
	}
	::SelectObject(hDC, hOldFont);
	::ReleaseDC(editor.Window, hDC);
}

static LRESULT CALLBACK EditorWndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
	CEditor* pEditor = s_pEditor;
	if (pEditor == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
	switch (nMsg) {
		case WM_COMMAND:
			switch (LOWORD(wParam)) {
				case ID_SEARCH:
					if (HIWORD(wParam) == EN_CHANGE) LayoutPage(*pEditor);
					return 0;
				case ID_CATEGORIES:
					if (HIWORD(wParam) == LBN_SELCHANGE) {
						if (::GetWindowTextLength(pEditor->Search) > 0) ::SetWindowText(pEditor->Search, _T("")); // shows the category
						else LayoutPage(*pEditor);
					}
					return 0;
				case ID_SAVE:
				case IDOK:
					if (::GetFocus() == pEditor->Search) return 0;
					if (HasChanges(*pEditor)) {
						if (!SaveSettings(*pEditor)) {
							::MessageBox(hWnd, CNLS::GetString(_T("The settings could not be written to the user INI file.")), CNLS::GetString(_T("Settings")), MB_OK | MB_ICONERROR);
							return 0;
						}
						pEditor->Saved = true;
					}
					pEditor->Done = true;
					::PostMessage(hWnd, WM_NULL, 0, 0);
					return 0;
				case ID_CANCEL:
				case IDCANCEL:
					if (HasChanges(*pEditor)) {
						int nAnswer = HelpersGUI::AskToSave(hWnd, CNLS::GetString(_T("Save the changed settings?")));
						if (nAnswer == IDCANCEL) return 0;
						if (nAnswer == IDYES) {
							::SetFocus(pEditor->Categories);
							::SendMessage(hWnd, WM_COMMAND, ID_SAVE, 0);
							return 0;
						}
					}
					pEditor->Done = true;
					::PostMessage(hWnd, WM_NULL, 0, 0);
					return 0;
				case ID_EDIT_INI:
					::ShellExecute(hWnd, _T("open"), Helpers::SystemProgramPath(_T("notepad.exe")), CString(_T("\"")) + CSettingsProvider::This().GetUserINIFileName() + _T("\""), NULL, SW_SHOWNORMAL);
					return 0;
				case ID_EDIT_KEYMAP:
					if (EditShortcuts(hWnd)) pEditor->KeyMapChanged = true;
					return 0;
				case ID_DEFAULTS:
					if (IDYES == ::MessageBox(hWnd, CNLS::GetString(_T("Set all settings of this page to the default values?")), CNLS::GetString(_T("Settings")), MB_YESNO | MB_ICONQUESTION)) {
						for (size_t i = 0; i < pEditor->Shown.size(); i++) {
							CSetting& setting = pEditor->Settings[pEditor->Shown[i]];
							setting.Value = setting.Default;
							SetControlValue(*pEditor, pEditor->Shown[i], pEditor->ShownControls[i]);
						}
					}
					return 0;
			}
			break;
		case WM_SIZE:
			if (pEditor->Page != NULL && wParam != SIZE_MINIMIZED) {
				LayoutWindow(*pEditor);
				// the page is built again when the user stops dragging the border
				if (!pEditor->Sizing) LayoutPage(*pEditor);
				::InvalidateRect(hWnd, NULL, TRUE);
			}
			break;
		case WM_ENTERSIZEMOVE:
			pEditor->Sizing = true;
			break;
		case WM_EXITSIZEMOVE:
			if (pEditor->Sizing) {
				pEditor->Sizing = false;
				if (pEditor->Page != NULL) LayoutPage(*pEditor);
			}
			break;
		case WM_GETMINMAXINFO: {
			MINMAXINFO* pInfo = (MINMAXINFO*)lParam;
			pInfo->ptMinTrackSize.x = pEditor->S(760);
			pInfo->ptMinTrackSize.y = pEditor->S(460);
			return 0;
		}
		case WM_MEASUREITEM: {
			MEASUREITEMSTRUCT* pMeasure = (MEASUREITEMSTRUCT*)lParam;
			pMeasure->itemHeight = pEditor->S(34);
			return TRUE;
		}
		case WM_CTLCOLORLISTBOX:
			if ((HWND)lParam == pEditor->Categories) {
				::SetTextColor((HDC)wParam, DarkUI::Colors().Text);
				::SetBkColor((HDC)wParam, DarkUI::Colors().Window);
				return (LRESULT)DarkUI::Brush(DarkUI::Colors().Window);
			}
			return (LRESULT)DarkUI::CtlColor(nMsg, wParam, lParam, DarkUI::Colors().Window);
		case WM_CTLCOLORSTATIC:
		case WM_CTLCOLOREDIT:
		case WM_CTLCOLORBTN:
			return (LRESULT)DarkUI::CtlColor(nMsg, wParam, lParam, DarkUI::Colors().Window);
		case WM_ERASEBKGND: {
			RECT rect;
			::GetClientRect(hWnd, &rect);
			::FillRect((HDC)wParam, &rect, DarkUI::Brush(DarkUI::Colors().Window));
			return 1;
		}
		case WM_NOTIFY: {
			LRESULT result = 0;
			if (DarkUI::CustomDrawButton(lParam, result)) return result;
			break;
		}
		case WM_DRAWITEM: {
			DRAWITEMSTRUCT* pDraw = (DRAWITEMSTRUCT*)lParam;
			if (pDraw->CtlID != ID_CATEGORIES || pDraw->itemID == (UINT)-1) break;
			bool bSelected = (pDraw->itemState & ODS_SELECTED) != 0;
			const DarkUI::CPalette& colors = DarkUI::Colors();
			::FillRect(pDraw->hDC, &pDraw->rcItem, DarkUI::Brush(colors.Window));
			if (bSelected) {
				// rounded selection with an accent mark, like the navigation of the Windows settings
				RECT selection = { pDraw->rcItem.left + pEditor->S(2), pDraw->rcItem.top + pEditor->S(2), pDraw->rcItem.right - pEditor->S(4), pDraw->rcItem.bottom - pEditor->S(2) };
				HGDIOBJ hOldBrush = ::SelectObject(pDraw->hDC, DarkUI::Brush(colors.Selection));
				HGDIOBJ hOldPen = ::SelectObject(pDraw->hDC, ::GetStockObject(NULL_PEN));
				::RoundRect(pDraw->hDC, selection.left, selection.top, selection.right + 1, selection.bottom + 1, pEditor->S(8), pEditor->S(8));
				::SelectObject(pDraw->hDC, DarkUI::Brush(colors.Accent));
				::RoundRect(pDraw->hDC, selection.left, pDraw->rcItem.top + pEditor->S(9), selection.left + pEditor->S(4), pDraw->rcItem.bottom - pEditor->S(8), pEditor->S(3), pEditor->S(3));
				::SelectObject(pDraw->hDC, hOldBrush);
				::SelectObject(pDraw->hDC, hOldPen);
			}
			TCHAR sText[256] = { 0 };
			::SendMessage(pDraw->hwndItem, LB_GETTEXT, pDraw->itemID, (LPARAM)sText);
			RECT textRect = pDraw->rcItem;
			textRect.left += pEditor->S(16);
			HGDIOBJ hOld = ::SelectObject(pDraw->hDC, bSelected ? pEditor->BoldFont : pEditor->Font);
			::SetBkMode(pDraw->hDC, TRANSPARENT);
			::SetTextColor(pDraw->hDC, colors.Text);
			::DrawText(pDraw->hDC, sText, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
			::SelectObject(pDraw->hDC, hOld);
			return TRUE;
		}
		case WM_CLOSE:
			::SendMessage(hWnd, WM_COMMAND, ID_CANCEL, 0);
			return 0;
	}
	return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

static void AddLanguages(CSetting& setting) {
	setting.Options.clear();
	setting.Options.push_back({ _T("auto"), CNLS::GetString(_T("Automatic (Windows language)")) });
	setting.Options.push_back({ _T("en"), _T("English") });
	WIN32_FIND_DATA findData;
	HANDLE hFind = ::FindFirstFile(CString(CSettingsProvider::This().GetEXEPath()) + _T("strings_*.txt"), &findData);
	if (hFind != INVALID_HANDLE_VALUE) {
		do {
			CString sCode(findData.cFileName);
			sCode = sCode.Mid(8, sCode.GetLength() - 12); // strings_xx.txt
			WCHAR sName[128] = { 0 };
			CString sLabel = sCode;
			if (::GetLocaleInfoEx(sCode, LOCALE_SNATIVELANGUAGENAME, sName, 128) > 0) {
				sLabel = CString(sName) + _T(" (") + sCode + _T(")");
			}
			setting.Options.push_back({ sCode, sLabel });
		} while (::FindNextFile(hFind, &findData));
		::FindClose(hFind);
	}
}

static int s_nSnapshotCategory = 0;

void SetSnapshotCategory(int nCategory) {
	s_nSnapshotCategory = nCategory;
}

bool Show(HWND hParent, CResult& result) {
	CSettingsProvider& sp = CSettingsProvider::This();
	CEditor editor;
	editor.Done = editor.Saved = editor.Updating = editor.KeyMapChanged = editor.Sizing = false;
	editor.Window = editor.Page = editor.Search = editor.Categories = NULL;
	editor.Scroll = editor.PageHeight = 0;
	ReadDefinitions(sp.GetGlobalINIFileName(), editor.Settings, editor.CategoryNames);
	if (editor.Settings.empty()) {
		::MessageBox(hParent, CNLS::GetString(_T("The INI file with the settings was not found.")), CNLS::GetString(_T("Settings")), MB_OK | MB_ICONERROR);
		return false;
	}
	for (CSetting& setting : editor.Settings) {
		bool bFound = false;
		CString sValue = ReadValue(sp.GetUserINIFileName(), setting.Key, bFound);
		if (!bFound) sValue = ReadValue(sp.GetGlobalINIFileName(), setting.Key, bFound);
		setting.Value = setting.Initial = bFound ? sValue : setting.Default;
		if (setting.Kind == KIND_LANGUAGE) AddLanguages(setting);
	}
	s_pEditor = &editor;

	HINSTANCE hInstance = ::GetModuleHandle(NULL);
	WNDCLASS wndClass = { 0 };
	wndClass.lpfnWndProc = EditorWndProc;
	wndClass.hInstance = hInstance;
	wndClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	wndClass.hbrBackground = NULL; // WM_ERASEBKGND, light or dark
	wndClass.hIcon = ::LoadIcon(hInstance, MAKEINTRESOURCE(128));
	wndClass.lpszClassName = _T("LucidViewerSettingsEditor");
	::RegisterClass(&wndClass);
	WNDCLASS pageClass = { 0 };
	pageClass.lpfnWndProc = PageWndProc;
	pageClass.hInstance = hInstance;
	pageClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	pageClass.hbrBackground = NULL;
	pageClass.lpszClassName = _T("LucidViewerSettingsPage");
	::RegisterClass(&pageClass);

	HDC hDC = ::GetDC(NULL);
	editor.Dpi = ::GetDeviceCaps(hDC, LOGPIXELSX);
	::ReleaseDC(NULL, hDC);
	NONCLIENTMETRICS metrics = { 0 };
	metrics.cbSize = sizeof(metrics);
	::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
	editor.Font = ::CreateFontIndirect(&metrics.lfMessageFont);
	LOGFONT bold = metrics.lfMessageFont;
	bold.lfWeight = FW_SEMIBOLD;
	editor.BoldFont = ::CreateFontIndirect(&bold);
	LOGFONT smallFont = metrics.lfMessageFont;
	smallFont.lfHeight = smallFont.lfHeight * 9 / 10;
	editor.SmallFont = ::CreateFontIndirect(&smallFont);
	LOGFONT titleFont = metrics.lfMessageFont;
	titleFont.lfHeight = titleFont.lfHeight * 16 / 10;
	titleFont.lfWeight = FW_SEMIBOLD;
	editor.TitleFont = ::CreateFontIndirect(&titleFont);

	int nWidth = editor.S(1080), nHeight = editor.S(720);
	RECT parentRect = { 0, 0, 0, 0 };
	if (hParent != NULL) ::GetWindowRect(hParent, &parentRect);
	HMONITOR hMonitor = ::MonitorFromWindow(hParent, MONITOR_DEFAULTTONEAREST);
	MONITORINFO monitorInfo = { sizeof(MONITORINFO) };
	::GetMonitorInfo(hMonitor, &monitorInfo);
	nWidth = min(nWidth, (int)(monitorInfo.rcWork.right - monitorInfo.rcWork.left) - 20);
	nHeight = min(nHeight, (int)(monitorInfo.rcWork.bottom - monitorInfo.rcWork.top) - 20);
	int nX = max((int)monitorInfo.rcWork.left, (int)(parentRect.left + parentRect.right - nWidth) / 2);
	int nY = max((int)monitorInfo.rcWork.top, (int)(parentRect.top + parentRect.bottom - nHeight) / 2);
	editor.Window = ::CreateWindowEx(0, _T("LucidViewerSettingsEditor"), CNLS::GetString(_T("Settings")),
		WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MAXIMIZEBOX, nX, nY, nWidth, nHeight, hParent, NULL, hInstance, NULL);
	if (editor.Window == NULL) {
		s_pEditor = NULL;
		return false;
	}
	CRect client;
	::GetClientRect(editor.Window, &client);
	int nPad = editor.S(10), nListWidth = editor.S(240), nButtonHeight = editor.S(30);
	HWND hSearch = ::CreateWindowEx(WS_EX_CLIENTEDGE, _T("EDIT"), _T(""), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
		nPad, nPad, nListWidth, editor.S(26), editor.Window, (HMENU)(INT_PTR)ID_SEARCH, hInstance, NULL);
	editor.Search = hSearch;
	::SendMessage(editor.Search, EM_SETCUEBANNER, TRUE, (LPARAM)CNLS::GetString(_T("Search in all settings")));
	editor.Categories = ::CreateWindowEx(0, _T("LISTBOX"), _T(""), WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
		nPad, nPad + editor.S(32), nListWidth, client.Height() - 3 * nPad - nButtonHeight - editor.S(32), editor.Window, (HMENU)(INT_PTR)ID_CATEGORIES, hInstance, NULL);
	editor.CategoryNames.insert(editor.CategoryNames.begin(), CString(COMMON_CATEGORY));
	for (const CString& sCategory : editor.CategoryNames) {
		::SendMessage(editor.Categories, LB_ADDSTRING, 0, (LPARAM)CNLS::GetString(sCategory));
	}

	HWND hPage = ::CreateWindowEx(0, _T("LucidViewerSettingsPage"), _T(""), WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | WS_CLIPCHILDREN,
		2 * nPad + nListWidth, nPad, client.Width() - 3 * nPad - nListWidth, client.Height() - 3 * nPad - nButtonHeight, editor.Window, NULL, hInstance, NULL);

	struct { int Id; LPCTSTR Text; bool Left; } buttons[] = {
		{ ID_EDIT_INI, _T("Edit INI file..."), true }, { ID_EDIT_KEYMAP, _T("Edit keyboard shortcuts..."), true }, { ID_DEFAULTS, _T("Defaults of this page"), true },
		{ ID_SAVE, _T("Save"), false }, { ID_CANCEL, _T("Cancel"), false },
	};
	// created in the tab order, placed by LayoutWindow
	for (auto& button : buttons) {
		HWND hButton = ::CreateWindow(_T("BUTTON"), CNLS::GetString(button.Text), WS_CHILD | WS_VISIBLE | WS_TABSTOP | (button.Id == ID_SAVE ? BS_DEFPUSHBUTTON : 0),
			0, 0, editor.S(110), nButtonHeight, editor.Window, (HMENU)(INT_PTR)button.Id, hInstance, NULL);
		::SendMessage(hButton, WM_SETFONT, (WPARAM)editor.Font, FALSE);
		editor.Buttons.push_back({ hButton, button.Left });
	}
	editor.Page = hPage;
	::SendMessage(editor.Search, WM_SETFONT, (WPARAM)editor.Font, FALSE);
	::SendMessage(editor.Categories, WM_SETFONT, (WPARAM)editor.Font, FALSE);
	::SendMessage(editor.Categories, LB_SETCURSEL, DarkUI::IsSnapshotMode() ? s_nSnapshotCategory : 0, 0);
	DarkUI::SetBackground(editor.Window, DarkUI::Colors().Window);
	DarkUI::SetBackground(editor.Page, DarkUI::Colors().Surface);
	DarkUI::ApplyToWindow(editor.Window);
	LayoutWindow(editor);
	LayoutPage(editor);

	if (hParent != NULL) ::EnableWindow(hParent, FALSE);
	if (DarkUI::IsSnapshotMode()) {
		DarkUI::ShowSnapshot(editor.Window, ID_CANCEL);
	} else {
		::ShowWindow(editor.Window, SW_SHOWNORMAL);
		::SetFocus(editor.Categories);
	}
	MSG msg;
	while (!editor.Done && ::GetMessage(&msg, NULL, 0, 0) > 0) {
		if (msg.message == WM_MOUSEWHEEL) {
			// the wheel scrolls the page also when the mouse is over a control of it
			POINT pt = { GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam) };
			RECT pageRect;
			::GetWindowRect(editor.Page, &pageRect);
			HWND hTarget = msg.hwnd;
			TCHAR sClass[32] = { 0 };
			::GetClassName(hTarget, sClass, 32);
			bool bDroppedCombo = _tcscmp(sClass, _T("ComboLBox")) == 0;
			if (::PtInRect(&pageRect, pt) && !bDroppedCombo) {
				::SendMessage(editor.Page, WM_MOUSEWHEEL, msg.wParam, msg.lParam);
				continue;
			}
		}
		if (!::IsDialogMessage(editor.Window, &msg)) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
	}
	if (hParent != NULL) ::EnableWindow(hParent, TRUE);
	::DestroyWindow(editor.Window);
	if (hParent != NULL) ::SetActiveWindow(hParent);
	::DeleteObject(editor.Font);
	::DeleteObject(editor.BoldFont);
	::DeleteObject(editor.SmallFont);
	::DeleteObject(editor.TitleFont);
	s_pEditor = NULL;

	result.Saved = editor.Saved;
	result.KeyMapChanged = editor.KeyMapChanged;
	result.ChangedKeys.clear();
	if (editor.Saved) {
		for (const CSetting& setting : editor.Settings) {
			if (setting.Value != setting.Initial) result.ChangedKeys.push_back(setting.Key);
		}
	}
	return editor.Saved || editor.KeyMapChanged;
}

void Restart(HWND hParent, LPCTSTR sCurrentFile) {
	// the new process waits until this one has ended (single instance mode). Started directly, not with cmd.exe:
	// cmd would expand % in the file name.
	CString sParameters;
	if (sCurrentFile != NULL && *sCurrentFile != 0) {
		sParameters = CString(_T("\"")) + sCurrentFile + _T("\"");
	}
	Helpers::StartThisProgram(sParameters, ::GetCurrentProcessId());
	::PostMessage(hParent, WM_CLOSE, 0, 0);
}

///////////////////////////////////////////////////////////////////////////////////
// Keyboard shortcut editor
///////////////////////////////////////////////////////////////////////////////////

static const int ID_SC_LIST = 200, ID_SC_SEARCH = 201, ID_SC_ADD = 202, ID_SC_REMOVE = 203, ID_SC_DEFAULTS = 204, ID_SC_NOTEPAD = 205, ID_SC_SAVE = 206, ID_SC_CANCEL = 207, ID_SC_ONLY_KEYS = 208;

static LPCTSTR s_shortcutCategories[] = { _T("File"), _T("Navigation"), _T("View and zoom"), _T("Editing"), _T("Drawing"), _T("Tools"),
	_T("Slideshow"), _T("Other"), _T("Mouse") };
static const int CATEGORY_MOUSE = 8;

// Category of a command by its name
static int GetCommandCategory(const CString& sName) {
	auto Has = [&](LPCTSTR sPart) { return sName.Find(sPart) >= 0; };
	if (Has(_T("ANNOTATION")) || Has(_T("SIGNATURE")) || Has(_T("DRAWPANEL")) || Has(_T("TRANSPAREN")) || Has(_T("REMOVE_BACKGROUND"))) return 4;
	if (Has(_T("SLIDESHOW")) || Has(_T("MOVIE")) || Has(_T("EFFECT"))) return 6;
	if (Has(_T("PARAM_DB"))) return 3;
	if (Has(_T("SCREENSHOT")) || Has(_T("SETTINGS")) || Has(_T("SHORTCUTS")) || Has(_T("SIMILAR")) || Has(_T("CONFIG")) || Has(_T("HELP")) ||
		Has(_T("ABOUT")) || Has(_T("USER_CMD")) || Has(_T("OCR")) || Has(_T("EXPLORER_CONVERT")) || Has(_T("SAVE_PARAMETERS")) || Has(_T("OPENWITH")) || Has(_T("UPDATES")) || Has(_T("PICK_COLOR")) || Has(_T("SEARCH"))) return 5;
	if (Has(_T("OPEN")) || Has(_T("SAVE")) || Has(_T("RELOAD")) || Has(_T("PRINT")) || Has(_T("COPY")) || Has(_T("PASTE")) || Has(_T("EXPLORE")) ||
		Has(_T("BATCH")) || Has(_T("RENAME")) || Has(_T("RECYCLE")) || Has(_T("DELETE")) || Has(_T("TOUCH")) || Has(_T("WALLPAPER")) || Has(_T("EXPORT")) ||
		Has(_T("SHARE")) || Has(_T("CONVERT")) || Has(_T("EXIT")) || Has(_T("METADATA")) || Has(_T("TO_FOLDER")) || Has(_T("PROPERTIES")) || Has(_T("DUPLICATE")) || Has(_T("FILE_NAME")) ||
		Has(_T("BASE64")) || Has(_T("LOCK_SCREEN")) || Has(_T("EDIT_TAGS")) || Has(_T("ON_MAP"))) return 0;
	if (Has(_T("NEXT")) || Has(_T("PREV")) || Has(_T("FIRST")) || Has(_T("LAST")) || Has(_T("LOOP")) || Has(_T("SORT")) || Has(_T("RATING")) || Has(_T("GOTO")) || Has(_T("FILTER_BY_TAG")) || sName == _T("IDM_TOGGLE")) return 1;
	if (Has(_T("GRID")) || Has(_T("TIMELINE")) || Has(_T("ZOOM")) || Has(_T("FIT")) || Has(_T("FILL")) || Has(_T("SPAN")) || Has(_T("FULL_SCREEN")) || Has(_T("SHOW_")) || Has(_T("TOGGLE_")) ||
		Has(_T("HIDE")) || Has(_T("ALWAYS_ON_TOP")) || Has(_T("PAN_")) || Has(_T("MINIMIZE")) || Has(_T("MONITOR"))) return 2;
	if (Has(_T("CONTRAST")) || Has(_T("GAMMA")) || Has(_T("SHARPEN")) || Has(_T("COLOR")) || Has(_T("LDC")) || Has(_T("AUTO_")) || Has(_T("LANDSCAPE")) ||
		Has(_T("UNSHARP")) || Has(_T("RESET")) || Has(_T("KEEP")) || Has(_T("ROTATE")) || Has(_T("MIRROR")) || Has(_T("CROP")) || Has(_T("CHANGESIZE")) ||
		Has(_T("PERSPECTIVE")) || Has(_T("COMPARE")) || Has(_T("EDITPANEL")) || Has(_T("UNDO")) || Has(_T("REDO")) || Has(_T("LOSSLESS")) ||
		Has(_T("RESAMPLING")) || Has(_T("SHADOWS")) || Has(_T("HIGHLIGHTS"))) return 3;
	return 7;
}

// English names of the commands that are not in the context menu
static struct { LPCTSTR Name; LPCTSTR Text; } s_commandTexts[] = {
	{ _T("IDM_UNDO"), _T("Undo") }, { _T("IDM_REDO"), _T("Redo") }, { _T("IDM_SHOW_METADATA"), _T("Metadata...") },
	{ _T("IDM_SCREENSHOT"), _T("Take screenshot") }, { _T("IDM_SCREENSHOT_TEXT"), _T("Copy text from screen") },
	{ _T("IDM_SCREENSHOT_TRAY"), _T("Screenshot tool in the notification area") }, { _T("IDM_OCR_COPY_TEXT"), _T("Copy text from image") },
	{ _T("IDM_SHARE"), _T("Share...") }, { _T("IDM_EXPORT"), _T("Export...") }, { _T("IDM_FIND_SIMILAR"), _T("Find similar photos...") },
	{ _T("IDM_SETTINGS"), _T("Settings...") }, { _T("IDM_SHOW_SHORTCUTS"), _T("Keyboard shortcuts...") }, { _T("IDM_TOGGLE_TOOLBAR"), _T("Toolbar") },
	{ _T("IDM_TOGGLE_STATUSBAR"), _T("Status bar") }, { _T("IDM_TOGGLE_SIDEPANEL"), _T("Information panel") },
	{ _T("IDM_TOGGLE_EDITPANEL"), _T("Edit image (brightness, contrast, colors)") }, { _T("IDM_TOGGLE_DRAWPANEL"), _T("Draw and write on the image") },
	{ _T("IDM_TOGGLE_FILMSTRIP"), _T("Thumbnail strip") }, { _T("IDM_TOGGLE_CROP_TOOL"), _T("Crop tool") },
	{ _T("IDM_COMPARE_BEFORE_AFTER"), _T("Compare before and after editing") }, { _T("IDM_AUTO_ENHANCE"), _T("Automatic color, contrast and shadow correction") },
	{ _T("IDM_ANNOTATION_UNDO"), _T("Remove the last drawing") }, { _T("IDM_ANNOTATION_CLEAR"), _T("Remove all drawings") },
	{ _T("IDM_ANNOTATION_APPLY"), _T("Draw into the image") }, { _T("IDM_SIGNATURE_SAVE"), _T("Save pen strokes as signature") },
	{ _T("IDM_SIGNATURE_INSERT"), _T("Insert signature") }, { _T("IDM_SIGNATURE_DRAW"), _T("Draw a new signature...") },
	{ _T("IDM_REMOVE_BACKGROUND"), _T("Remove background") }, { _T("IDM_SAVE_TRANSPARENT"), _T("Save as transparent PNG...") },
	{ _T("IDM_UNDO_TRANSPARENCY"), _T("Restore removed colors") }, { _T("IDM_SORT_RATING"), _T("Sort by star rating") },
	{ _T("IDM_RATING_FILTER_0"), _T("All images") }, { _T("IDM_RATING_FILTER_1"), _T("1 star or more") }, { _T("IDM_RATING_FILTER_2"), _T("2 stars or more") },
	{ _T("IDM_RATING_FILTER_3"), _T("3 stars or more") }, { _T("IDM_RATING_FILTER_4"), _T("4 stars or more") }, { _T("IDM_RATING_FILTER_5"), _T("Only 5 stars") },
	{ _T("IDM_METADATA_COPY"), _T("Copy all metadata") }, { _T("IDM_METADATA_REMOVE_LOCATION"), _T("Remove location") },
	{ _T("IDM_METADATA_REMOVE_ALL"), _T("Remove all editable metadata") }, { _T("IDM_MOVE_TO_RECYCLE_BIN_CONFIRM"), _T("Move to recycle bin") },
	{ _T("IDM_CHANGESIZE"), _T("Resize image...") }, { _T("IDM_CONVERT_MENU"), _T("Convert to") },
	{ _T("IDM_TOGGLE_SIDEPANEL_COLLAPSED"), _T("Collapse the side panel") }, { _T("IDM_EXPLORER_CONVERT_MENU"), _T("'Convert to' in the Explorer context menu") },
	{ _T("IDM_HELP"), _T("Keyboard shortcuts...") },
	{ _T("IDM_START_SCREEN"), _T("Start screen") }, { _T("IDM_PIN_TO_START"), _T("Pin to start screen") },
	{ _T("IDM_COPY_FILE"), _T("Copy file") }, { _T("IDM_COPY_TO_FOLDER"), _T("Copy to folder...") },
	{ _T("IDM_MOVE_TO_FOLDER"), _T("Move to folder...") }, { _T("IDM_CHECK_FOR_UPDATES"), _T("Check for updates...") },
	{ _T("IDM_FILE_PROPERTIES"), _T("File properties...") }, { _T("IDM_OPEN_WITH_OTHER_APP"), _T("Open with another app...") },
	{ _T("IDM_OPEN_IN_NEW_WINDOW"), _T("Open in new window") },
	{ _T("IDM_DUPLICATE_FILE"), _T("Duplicate file") }, { _T("IDM_COPY_FILE_NAME"), _T("Copy file name") },
	{ _T("IDM_GOTO_IMAGE"), _T("Go to image number...") }, { _T("IDM_COPY_BASE64"), _T("Copy as data URI (base64)") },
	{ _T("IDM_SET_LOCK_SCREEN"), _T("Set as lock screen") }, { _T("IDM_PICK_COLOR"), _T("Pick color") },
	{ _T("IDM_SORT_DATE_TAKEN"), _T("Sort by date taken") }, { _T("IDM_EDIT_TAGS"), _T("Edit tags...") },
	{ _T("IDM_FILTER_BY_TAG"), _T("Filter by tag...") }, { _T("IDM_SHOW_ON_MAP"), _T("Show on map") },
	{ _T("IDM_FOLDER_GRID"), _T("Thumbnails of the folder") }, { _T("IDM_TIMELINE"), _T("Timeline (by date taken)") },
	{ _T("IDM_SEARCH_PHOTOS"), _T("Search photos...") },
	{ _T("IDM_METADATA_CHOOSE_FIELDS"), _T("Choose metadata fields...") },
	{ _T("IDM_SAVE_SCREEN"), _T("Save as shown on screen...") },
	{ _T("IDM_SAVE_ALLOW_NO_PROMPT"), _T("Save") },
	{ _T("IDM_TOGGLE"), _T("Switch between this and the marked image") },
	{ _T("IDM_MARK_FOR_TOGGLE"), _T("Mark the image for switching") },
	{ _T("IDM_EXCHANGE_PROC_PARAMS"), _T("Swap edit settings with the marked image") },
	{ _T("IDM_ZOOM_INC"), _T("Zoom in") },
	{ _T("IDM_ZOOM_DEC"), _T("Zoom out") },
	{ _T("IDM_TOGGLE_RESAMPLING_QUALITY"), _T("Resampling quality: high / fast") },
	{ _T("IDM_TOGGLE_FIT_TO_SCREEN_100_PERCENTS"), _T("Fit to screen / 100%") },
	{ _T("IDM_PAN_UP"), _T("Move the zoomed image up") },
	{ _T("IDM_PAN_DOWN"), _T("Move the zoomed image down") },
	{ _T("IDM_PAN_LEFT"), _T("Move the zoomed image left") },
	{ _T("IDM_PAN_RIGHT"), _T("Move the zoomed image right") },
	{ _T("IDM_ROTATE_90_LOSSLESS_CONFIRM"), _T("Rotate +90 without quality loss (JPEG)") },
	{ _T("IDM_ROTATE_270_LOSSLESS_CONFIRM"), _T("Rotate -90 without quality loss (JPEG)") },
	{ _T("IDM_AUTO_CORRECTION"), _T("Automatic color and contrast correction on / off") },
	{ _T("IDM_AUTO_CORRECTION_SECTION"), _T("Automatic correction for the visible part") },
	{ _T("IDM_LDC"), _T("Shadow and highlight correction on / off") },
	{ _T("IDM_LANDSCAPE_MODE"), _T("Landscape enhancement mode") },
	{ _T("IDM_KEEP_PARAMETERS"), _T("Keep edit settings for the next images") },
	{ _T("IDM_SAVE_PARAM_DB"), _T("Remember edit settings for this image") },
	{ _T("IDM_COLOR_CORRECTION_INC"), _T("Color correction stronger") },
	{ _T("IDM_COLOR_CORRECTION_DEC"), _T("Color correction weaker") },
	{ _T("IDM_CONTRAST_CORRECTION_INC"), _T("Contrast correction stronger") },
	{ _T("IDM_CONTRAST_CORRECTION_DEC"), _T("Contrast correction weaker") },
	{ _T("IDM_CONTRAST_INC"), _T("More contrast") },
	{ _T("IDM_CONTRAST_DEC"), _T("Less contrast") },
	{ _T("IDM_GAMMA_INC"), _T("Brightness (gamma) up") },
	{ _T("IDM_GAMMA_DEC"), _T("Brightness (gamma) down") },
	{ _T("IDM_LDC_SHADOWS_INC"), _T("Lighten shadows more") },
	{ _T("IDM_LDC_SHADOWS_DEC"), _T("Lighten shadows less") },
	{ _T("IDM_LDC_HIGHLIGHTS_INC"), _T("Darken highlights more") },
	{ _T("IDM_LDC_HIGHLIGHTS_DEC"), _T("Darken highlights less") },
	{ _T("IDM_SHARPEN_INC"), _T("Sharpen more") },
	{ _T("IDM_SHARPEN_DEC"), _T("Sharpen less") },
	{ _T("IDM_SLIDESHOW_RESUME"), _T("Resume slideshow") },
	{ _T("IDM_TOGGLE_MONITOR"), _T("Move to the next monitor") },
	{ _T("IDM_ALWAYS_ON_TOP"), _T("Window always on top") },
	{ _T("IDM_HIDE_TITLE_BAR"), _T("Hide window title bar") },
};

// Mouse actions, shown on the page but they can not be changed here
static struct { LPCTSTR Action; LPCTSTR Text; } s_mouseActions[] = {
	{ _T("Mouse wheel"), _T("Next / previous image or zoom (setting NavigateWithMouseWheel)") },
	{ _T("Ctrl + mouse wheel"), _T("Zoom") },
	{ _T("Drag with the left button"), _T("Move the zoomed image, or select an area to crop") },
	{ _T("Ctrl + drag"), _T("Select the area to crop") },
	{ _T("Double click"), _T("Full screen on / off") },
	{ _T("Forward / back mouse button"), _T("Next / previous image") },
	{ _T("Right click"), _T("Menu with all functions") },
};

struct CShortcutEditor {
	HWND Window, List, Search, OnlyKeys;
	std::vector<CString> Labels; // readable names of the commands, same order as Commands
	HFONT Font;
	int Dpi;
	std::vector<CString> Commands;                     // IDM names in the order of the symbols file
	std::vector<std::pair<CString, CString>> Mapping;  // key, IDM name
	std::vector<std::pair<CString, CString>> InitialMapping; // to know if there are changes when closing
	bool Done, Saved;
	int S(int n) { return ::MulDiv(n, Dpi, 96); }
};

static CShortcutEditor* s_pShortcuts = NULL;

static CString GetUserKeyMapFileName() {
	return CString(Helpers::AppDataPath()) + _T("KeyMap.txt");
}

static void ReadKeyMapFile(LPCTSTR sFileName, std::vector<std::pair<CString, CString>>& mapping) {
	mapping.clear();
	FILE* pFile = NULL;
	if (_tfopen_s(&pFile, sFileName, _T("r")) != 0 || pFile == NULL) return;
	char line[1024];
	while (fgets(line, sizeof(line), pFile) != NULL) {
		CString sLine(line);
		sLine.Trim();
		if (sLine.IsEmpty() || sLine.Left(2) == _T("//")) continue;
		int nSpace = sLine.FindOneOf(_T(" \t"));
		if (nSpace <= 0) continue;
		CString sKey = sLine.Left(nSpace), sCommand = Trim(sLine.Mid(nSpace));
		int nComment = sCommand.Find(_T("//"));
		if (nComment >= 0) sCommand = Trim(sCommand.Left(nComment));
		if (sCommand.Left(4) == _T("IDM_")) mapping.push_back({ sKey, sCommand });
	}
	fclose(pFile);
}

// "IDM_TOGGLE_SIDEPANEL" -> "Toggle sidepanel"
static CString CommandLabel(const CString& sCommand) {
	CString s = sCommand.Left(4) == _T("IDM_") ? sCommand.Mid(4) : sCommand;
	s.Replace(_T('_'), _T(' '));
	s.MakeLower();
	if (!s.IsEmpty()) s.SetAt(0, _totupper(s[0]));
	return s;
}

static CString GetLabel(CShortcutEditor& editor, int nCommand) {
	return (nCommand >= 0 && nCommand < (int)editor.Labels.size() && !editor.Labels[nCommand].IsEmpty()) ? editor.Labels[nCommand] : CommandLabel(editor.Commands[nCommand]);
}

static CString KeysOfCommand(CShortcutEditor& editor, const CString& sCommand) {
	CString sKeys;
	for (auto& entry : editor.Mapping) {
		if (entry.second == sCommand) sKeys += (sKeys.IsEmpty() ? _T("") : _T(", ")) + entry.first;
	}
	return sKeys;
}

static void FillShortcutList(CShortcutEditor& editor) {
	TCHAR sSearch[128];
	::GetWindowText(editor.Search, sSearch, 128);
	CString sFind = Trim(sSearch);
	sFind.MakeLower();
	int nSelected = (int)::SendMessage(editor.List, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
	LPARAM nSelectedCommand = -1;
	if (nSelected >= 0) {
		LVITEM item = { LVIF_PARAM };
		item.iItem = nSelected;
		ListView_GetItem(editor.List, &item);
		nSelectedCommand = item.lParam;
	}
	bool bOnlyKeys = ::SendMessage(editor.OnlyKeys, BM_GETCHECK, 0, 0) == BST_CHECKED;
	::SendMessage(editor.List, WM_SETREDRAW, FALSE, 0);
	ListView_DeleteAllItems(editor.List);
	int nRow = 0;
	for (int nCategory = 0; nCategory <= CATEGORY_MOUSE; nCategory++) {
	if (nCategory == CATEGORY_MOUSE) {
		// mouse actions, they can not be selected for editing
		for (int m = 0; m < (int)(sizeof(s_mouseActions) / sizeof(s_mouseActions[0])); m++) {
			CString sLabel = CNLS::GetString(s_mouseActions[m].Text), sKeys = CNLS::GetString(s_mouseActions[m].Action);
			CString sText = sLabel + _T(" ") + sKeys;
			sText.MakeLower();
			if (!sFind.IsEmpty() && sText.Find(sFind) < 0) continue;
			LVITEM item = { LVIF_TEXT | LVIF_PARAM | LVIF_GROUPID };
			item.iItem = nRow++;
			item.pszText = (LPTSTR)(LPCTSTR)sLabel;
			item.lParam = -1;
			item.iGroupId = CATEGORY_MOUSE;
			int nIndex = ListView_InsertItem(editor.List, &item);
			ListView_SetItemText(editor.List, nIndex, 1, (LPTSTR)(LPCTSTR)sKeys);
		}
		continue;
	}
	for (int i = 0; i < (int)editor.Commands.size(); i++) {
		if (GetCommandCategory(editor.Commands[i]) != nCategory) continue;
		CString sLabel = GetLabel(editor, i), sKeys = KeysOfCommand(editor, editor.Commands[i]);
		if (bOnlyKeys && sKeys.IsEmpty() && sFind.IsEmpty()) continue;
		CString sText = sLabel + _T(" ") + sKeys + _T(" ") + editor.Commands[i];
		sText.MakeLower();
		if (!sFind.IsEmpty() && sText.Find(sFind) < 0) continue;
		LVITEM item = { LVIF_TEXT | LVIF_PARAM | LVIF_GROUPID };
		item.iItem = nRow;
		item.pszText = (LPTSTR)(LPCTSTR)sLabel;
		item.lParam = i;
		item.iGroupId = nCategory;
		int nIndex = ListView_InsertItem(editor.List, &item);
		ListView_SetItemText(editor.List, nIndex, 1, (LPTSTR)(LPCTSTR)sKeys);
		ListView_SetItemText(editor.List, nIndex, 2, (LPTSTR)(LPCTSTR)editor.Commands[i]);
		if (i == nSelectedCommand) {
			ListView_SetItemState(editor.List, nIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			ListView_EnsureVisible(editor.List, nIndex, FALSE);
		}
		nRow++;
	}
	}
	::SendMessage(editor.List, WM_SETREDRAW, TRUE, 0);
	::InvalidateRect(editor.List, NULL, TRUE);
}

static int GetSelectedCommand(CShortcutEditor& editor) {
	int nSelected = (int)::SendMessage(editor.List, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
	if (nSelected < 0) return -1;
	LVITEM item = { LVIF_PARAM };
	item.iItem = nSelected;
	ListView_GetItem(editor.List, &item);
	return (int)item.lParam;
}

// Waits for a key combination, returns its name as used in the key map file (e.g. "Ctrl+Shift+K"), empty if cancelled
static LRESULT CALLBACK CaptureWndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
	switch (nMsg) {
		case WM_CTLCOLORSTATIC:
			return (LRESULT)DarkUI::CtlColor(nMsg, wParam, lParam, DarkUI::Colors().Window);
		case WM_ERASEBKGND: {
			RECT rect;
			::GetClientRect(hWnd, &rect);
			::FillRect((HDC)wParam, &rect, DarkUI::Brush(DarkUI::Colors().Window));
			return 1;
		}
	}
	return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

static CString CaptureShortcut(HWND hParent, const CString& sCommandLabel, int nDpi) {
	HINSTANCE hInstance = ::GetModuleHandle(NULL);
	auto S = [nDpi](int n) { return ::MulDiv(n, nDpi, 96); };
	RECT parentRect;
	::GetWindowRect(hParent, &parentRect);
	int nWidth = S(420), nHeight = S(150);
	WNDCLASS wndClass = { 0 };
	wndClass.lpfnWndProc = CaptureWndProc;
	wndClass.hInstance = hInstance;
	wndClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	wndClass.hbrBackground = NULL;
	wndClass.lpszClassName = _T("LucidViewerShortcutCapture");
	::RegisterClass(&wndClass);
	HWND hWnd = ::CreateWindowEx(WS_EX_DLGMODALFRAME, _T("LucidViewerShortcutCapture"), CNLS::GetString(_T("New keyboard shortcut")), WS_POPUP | WS_CAPTION | WS_SYSMENU,
		(parentRect.left + parentRect.right - nWidth) / 2, (parentRect.top + parentRect.bottom - nHeight) / 2, nWidth, nHeight, hParent, NULL, hInstance, NULL);
	if (hWnd == NULL) return CString();
	NONCLIENTMETRICS metrics = { 0 };
	metrics.cbSize = sizeof(metrics);
	::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
	HFONT hFont = ::CreateFontIndirect(&metrics.lfMessageFont);
	CString sText = sCommandLabel + _T("\n\n") + CNLS::GetString(_T("Press the new key combination. Click here to cancel."));
	HWND hText = ::CreateWindow(_T("STATIC"), sText, WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOTIFY, S(12), S(16), nWidth - S(30), nHeight - S(60), hWnd, (HMENU)1, hInstance, NULL);
	::SendMessage(hText, WM_SETFONT, (WPARAM)hFont, FALSE);
	DarkUI::ApplyToWindow(hWnd);
	::EnableWindow(hParent, FALSE);
	::ShowWindow(hWnd, SW_SHOWNORMAL);
	::SetForegroundWindow(hWnd);
	::SetFocus(hWnd);
	CString sResult;
	bool bDone = false;
	MSG msg;
	while (!bDone && ::GetMessage(&msg, NULL, 0, 0) > 0) {
		if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) {
			int nKey = (int)msg.wParam;
			if (nKey == VK_SHIFT || nKey == VK_CONTROL || nKey == VK_MENU || nKey == VK_LWIN || nKey == VK_RWIN) continue;
			bool bAlt = (::GetKeyState(VK_MENU) & 0x8000) != 0, bCtrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0, bShift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
			sResult = CKeyMap::GetShortcutKey(CKeyMap::GetCombinedKeyCode(nKey, bAlt, bCtrl, bShift));
			sResult.Remove(_T(' '));
			if (CKeyMap::GetVirtualKeyCode(sResult) == -1) sResult.Empty();
			bDone = true;
			continue;
		}
		if (msg.message == WM_LBUTTONDOWN || (msg.message == WM_NCLBUTTONDOWN && msg.wParam == HTCLOSE)) {
			bDone = true;
			continue;
		}
		::TranslateMessage(&msg);
		::DispatchMessage(&msg);
	}
	::EnableWindow(hParent, TRUE);
	::DestroyWindow(hWnd);
	::SetActiveWindow(hParent);
	::DeleteObject(hFont);
	return sResult;
}

static bool SaveKeyMap(CShortcutEditor& editor) {
	CString sFileName = GetUserKeyMapFileName();
	FILE* pFile = NULL;
	if (_tfopen_s(&pFile, sFileName, _T("w")) != 0 || pFile == NULL) return false;
	fputs("// This file defines the key mapping for Lucid Viewer, it was written by the keyboard shortcut editor (Tools > Settings).\n", pFile);
	fputs("// See KeyMap-README.html for more information on how to customize this file.\n\n", pFile);
	for (const CString& sCommand : editor.Commands) {
		bool bHasKey = false;
		for (auto& entry : editor.Mapping) {
			if (entry.second != sCommand) continue;
			CStringA sKey(entry.first), sName(entry.second);
			fprintf(pFile, "%-24s%s\n", (LPCSTR)sKey, (LPCSTR)sName);
			bHasKey = true;
		}
		// named without key, the default key of a new command is not added then
		if (!bHasKey) fprintf(pFile, "// no shortcut: %s\n", (LPCSTR)CStringA(sCommand));
	}
	fclose(pFile);
	return true;
}

static LRESULT CALLBACK ShortcutWndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
	CShortcutEditor* pEditor = s_pShortcuts;
	if (pEditor == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
	switch (nMsg) {
		case WM_CTLCOLORSTATIC:
		case WM_CTLCOLOREDIT:
		case WM_CTLCOLORBTN:
		case WM_CTLCOLORLISTBOX:
			return (LRESULT)DarkUI::CtlColor(nMsg, wParam, lParam, DarkUI::Colors().Window);
		case WM_ERASEBKGND: {
			RECT rect;
			::GetClientRect(hWnd, &rect);
			::FillRect((HDC)wParam, &rect, DarkUI::Brush(DarkUI::Colors().Window));
			return 1;
		}
		case WM_NOTIFY: {
			NMHDR* pHeader = (NMHDR*)lParam;
			LRESULT result = 0;
			if (DarkUI::CustomDrawButton(lParam, result)) return result;
			if (pHeader->idFrom == ID_SC_LIST && pHeader->code == NM_CUSTOMDRAW && DarkUI::IsDark()) {
				// the theme draws the group headers dark blue, not readable on the dark background
				NMLVCUSTOMDRAW* pDraw = (NMLVCUSTOMDRAW*)lParam;
				if (pDraw->nmcd.dwDrawStage == CDDS_PREPAINT && pDraw->dwItemType == LVCDI_GROUP) {
					int nGroup = (int)pDraw->nmcd.dwItemSpec;
					wchar_t sHeader[128] = { 0 };
					LVGROUP group = { sizeof(LVGROUP) };
					group.mask = LVGF_HEADER | LVGF_STATE;
					group.stateMask = LVGS_COLLAPSED;
					group.pszHeader = sHeader;
					group.cchHeader = 128;
					ListView_GetGroupInfo(pHeader->hwndFrom, nGroup, &group);
					RECT rect;
					if (!ListView_GetGroupRect(pHeader->hwndFrom, nGroup, LVGGR_HEADER, &rect)) rect = pDraw->rcText;
					const DarkUI::CPalette& colors = DarkUI::Colors();
					HDC hDC = pDraw->nmcd.hdc;
					::FillRect(hDC, &rect, DarkUI::Brush(colors.Surface));
					HGDIOBJ hOldFont = ::SelectObject(hDC, pEditor->Font);
					::SetBkMode(hDC, TRANSPARENT);
					::SetTextColor(hDC, colors.Accent);
					RECT textRect = rect;
					textRect.left += pEditor->S(8);
					RECT measure = textRect;
					::DrawText(hDC, sHeader, -1, &measure, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_CALCRECT);
					::DrawText(hDC, sHeader, -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
					// line to the right of the header and the collapse mark
					int nLineY = (rect.top + rect.bottom) / 2;
					RECT line = { measure.right + pEditor->S(10), nLineY, rect.right - pEditor->S(28), nLineY + 1 };
					if (line.right > line.left) ::FillRect(hDC, &line, DarkUI::Brush(colors.Border));
					RECT markRect = { rect.right - pEditor->S(24), rect.top, rect.right - pEditor->S(6), rect.bottom };
					::SetTextColor(hDC, colors.SubText);
					::DrawText(hDC, (group.state & LVGS_COLLAPSED) ? L"\x203A" : L"\x2304", -1, &markRect, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
					::SelectObject(hDC, hOldFont);
					return CDRF_SKIPDEFAULT;
				}
				if (pDraw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
				return CDRF_DODEFAULT;
			}
			if (pHeader->idFrom == ID_SC_LIST && pHeader->code == NM_DBLCLK) {
				::SendMessage(hWnd, WM_COMMAND, ID_SC_ADD, 0);
			}
			return 0;
		}
		case WM_COMMAND:
			switch (LOWORD(wParam)) {
				case ID_SC_SEARCH:
					if (HIWORD(wParam) == EN_CHANGE) FillShortcutList(*pEditor);
					return 0;
				case ID_SC_ONLY_KEYS:
					FillShortcutList(*pEditor);
					return 0;
				case ID_SC_ADD: {
					int nCommand = GetSelectedCommand(*pEditor);
					if (nCommand < 0) return 0;
					const CString& sCommand = pEditor->Commands[nCommand];
					CString sKey = CaptureShortcut(hWnd, GetLabel(*pEditor, nCommand), pEditor->Dpi);
					if (sKey.IsEmpty()) return 0;
					for (size_t i = 0; i < pEditor->Mapping.size(); i++) {
						if (pEditor->Mapping[i].first.CompareNoCase(sKey) != 0) continue;
						if (pEditor->Mapping[i].second == sCommand) return 0;
						CString sQuestion;
						sQuestion.Format(CNLS::GetString(_T("%s is used for '%s'. Use it for '%s' instead?")), (LPCTSTR)sKey,
							(LPCTSTR)CommandLabel(pEditor->Mapping[i].second), (LPCTSTR)CommandLabel(sCommand));
						if (IDYES != ::MessageBox(hWnd, sQuestion, CNLS::GetString(_T("Keyboard shortcuts")), MB_YESNO | MB_ICONQUESTION)) return 0;
						pEditor->Mapping.erase(pEditor->Mapping.begin() + i);
						break;
					}
					pEditor->Mapping.push_back({ sKey, sCommand });
					FillShortcutList(*pEditor);
					return 0;
				}
				case ID_SC_REMOVE: {
					int nCommand = GetSelectedCommand(*pEditor);
					if (nCommand < 0) return 0;
					const CString& sCommand = pEditor->Commands[nCommand];
					pEditor->Mapping.erase(std::remove_if(pEditor->Mapping.begin(), pEditor->Mapping.end(),
						[&](const std::pair<CString, CString>& entry) { return entry.second == sCommand; }), pEditor->Mapping.end());
					FillShortcutList(*pEditor);
					return 0;
				}
				case ID_SC_DEFAULTS:
					if (IDYES == ::MessageBox(hWnd, CNLS::GetString(_T("Restore the default keyboard shortcuts?")), CNLS::GetString(_T("Keyboard shortcuts")), MB_YESNO | MB_ICONQUESTION)) {
						ReadKeyMapFile(CString(CSettingsProvider::This().GetEXEPath()) + _T("KeyMap.txt.default"), pEditor->Mapping);
						FillShortcutList(*pEditor);
					}
					return 0;
				case ID_SC_NOTEPAD:
					if (::GetFileAttributes(GetUserKeyMapFileName()) == INVALID_FILE_ATTRIBUTES) {
						::CopyFile(CString(CSettingsProvider::This().GetEXEPath()) + _T("KeyMap.txt.default"), GetUserKeyMapFileName(), TRUE);
					}
					::ShellExecute(hWnd, _T("open"), Helpers::SystemProgramPath(_T("notepad.exe")), _T("\"") + GetUserKeyMapFileName() + _T("\""), NULL, SW_SHOWNORMAL);
					return 0;
				case ID_SC_SAVE:
				case IDOK:
					if (::GetFocus() == pEditor->Search && LOWORD(wParam) == IDOK) return 0;
					if (!SaveKeyMap(*pEditor)) {
						::MessageBox(hWnd, CNLS::GetString(_T("The keyboard shortcuts could not be saved.")), CNLS::GetString(_T("Keyboard shortcuts")), MB_OK | MB_ICONERROR);
						return 0;
					}
					pEditor->Saved = true;
					pEditor->Done = true;
					::PostMessage(hWnd, WM_NULL, 0, 0);
					return 0;
				case ID_SC_CANCEL:
				case IDCANCEL:
					if (pEditor->Mapping != pEditor->InitialMapping) {
						int nAnswer = HelpersGUI::AskToSave(hWnd, CNLS::GetString(_T("Save the changed keyboard shortcuts?")));
						if (nAnswer == IDCANCEL) return 0;
						if (nAnswer == IDYES) {
							::SendMessage(hWnd, WM_COMMAND, ID_SC_SAVE, 0);
							return 0;
						}
					}
					pEditor->Done = true;
					::PostMessage(hWnd, WM_NULL, 0, 0);
					return 0;
			}
			break;
		case WM_CLOSE:
			::SendMessage(hWnd, WM_COMMAND, ID_SC_CANCEL, 0);
			return 0;
	}
	return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

bool EditShortcuts(HWND hParent) {
	CShortcutEditor editor;
	editor.Done = editor.Saved = false;
	// all commands of the symbols file
	FILE* pFile = NULL;
	if (_tfopen_s(&pFile, CString(CSettingsProvider::This().GetEXEPath()) + _T("symbols.km"), _T("r")) == 0 && pFile != NULL) {
		char line[512];
		while (fgets(line, sizeof(line), pFile) != NULL) {
			CString sLine(line);
			sLine.Trim();
			if (sLine.Left(12) != _T("#define IDM_")) continue;
			CString sName = sLine.Mid(8);
			int nSpace = sName.FindOneOf(_T(" \t"));
			if (nSpace > 0) sName = sName.Left(nSpace);
			if (sName != _T("IDM_DONOTHING")) editor.Commands.push_back(sName);
		}
		fclose(pFile);
	}
	// readable (translated) names: the texts of the context menu, else the texts of the new functions
	{
		std::vector<int> values;
		FILE* pSymbols = NULL;
		if (_tfopen_s(&pSymbols, CString(CSettingsProvider::This().GetEXEPath()) + _T("symbols.km"), _T("r")) == 0 && pSymbols != NULL) {
			char line[512];
			while (fgets(line, sizeof(line), pSymbols) != NULL) {
				char sName[256];
				int nValue;
				if (sscanf_s(line, "#define %255s %d", sName, (unsigned)sizeof(sName), &nValue) == 2) {
					CString sCommand(sName);
					for (size_t i = 0; i < editor.Commands.size(); i++) {
						if (editor.Commands[i] == sCommand) {
							if (values.size() < editor.Commands.size()) values.resize(editor.Commands.size(), -1);
							values[i] = nValue;
						}
					}
				}
			}
			fclose(pSymbols);
		}
		editor.Labels.assign(editor.Commands.size(), CString());
		HMENU hMenuRoot = ::LoadMenu(::GetModuleHandle(NULL), _T("PopupMenu"));
		if (hMenuRoot != NULL) {
			HMENU hPopup = ::GetSubMenu(hMenuRoot, 0);
			std::function<void(HMENU, const CString&)> collect = [&](HMENU hMenu, const CString& sParent) {
				for (int i = 0; i < ::GetMenuItemCount(hMenu); i++) {
					HMENU hSub = ::GetSubMenu(hMenu, i);
					if (hSub != NULL) {
						// items of sub menus get the name of the sub menu as prefix
						TCHAR sSubText[256] = { 0 };
						::GetMenuString(hMenu, i, sSubText, 256, MF_BYPOSITION);
						CString sSub(sSubText);
						sSub = CString(CNLS::GetString(sSub));
						sSub.Remove(_T('&'));
						collect(hSub, sSub);
						continue;
					}
					UINT nId = ::GetMenuItemID(hMenu, i);
					TCHAR sText[256] = { 0 };
					::GetMenuString(hMenu, i, sText, 256, MF_BYPOSITION);
					CString sLabel(sText);
					int nTab = sLabel.Find(_T('\t'));
					if (nTab >= 0) sLabel = sLabel.Left(nTab);
					sLabel.Trim();
					sLabel = CString(CNLS::GetString(sLabel)); // the resource has the English texts
					sLabel.Remove(_T('&'));
					if (!sParent.IsEmpty() && !sLabel.IsEmpty()) sLabel = sParent + _T(": ") + sLabel;
					for (size_t c = 0; c < values.size(); c++) {
						if (values[c] == (int)nId && editor.Labels[c].IsEmpty() && !sLabel.IsEmpty()) editor.Labels[c] = sLabel;
					}
				}
			};
			collect(hPopup, CString());
			::DestroyMenu(hMenuRoot);
		}
		for (auto& entry : s_commandTexts) {
			for (size_t c = 0; c < editor.Commands.size(); c++) {
				if (editor.Commands[c] == entry.Name) editor.Labels[c] = CNLS::GetString(entry.Text);
			}
		}
	}
	CString sUserFile = GetUserKeyMapFileName();
	ReadKeyMapFile(::GetFileAttributes(sUserFile) != INVALID_FILE_ATTRIBUTES ? sUserFile : CString(CSettingsProvider::This().GetEXEPath()) + _T("KeyMap.txt.default"), editor.Mapping);
	s_pShortcuts = &editor;

	INITCOMMONCONTROLSEX init = { sizeof(INITCOMMONCONTROLSEX), ICC_LISTVIEW_CLASSES };
	::InitCommonControlsEx(&init);
	HINSTANCE hInstance = ::GetModuleHandle(NULL);
	WNDCLASS wndClass = { 0 };
	wndClass.lpfnWndProc = ShortcutWndProc;
	wndClass.hInstance = hInstance;
	wndClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	wndClass.hbrBackground = NULL;
	wndClass.lpszClassName = _T("LucidViewerShortcutEditor");
	::RegisterClass(&wndClass);
	HDC hDC = ::GetDC(NULL);
	editor.Dpi = ::GetDeviceCaps(hDC, LOGPIXELSX);
	::ReleaseDC(NULL, hDC);
	NONCLIENTMETRICS metrics = { 0 };
	metrics.cbSize = sizeof(metrics);
	::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
	editor.Font = ::CreateFontIndirect(&metrics.lfMessageFont);

	int nWidth = editor.S(900), nHeight = editor.S(680);
	RECT parentRect = { 0, 0, nWidth, nHeight };
	if (hParent != NULL) ::GetWindowRect(hParent, &parentRect);
	editor.Window = ::CreateWindowEx(WS_EX_DLGMODALFRAME, _T("LucidViewerShortcutEditor"), CNLS::GetString(_T("Keyboard shortcuts")), WS_POPUP | WS_CAPTION | WS_SYSMENU,
		max(0, (int)(parentRect.left + parentRect.right - nWidth) / 2), max(0, (int)(parentRect.top + parentRect.bottom - nHeight) / 2), nWidth, nHeight, hParent, NULL, hInstance, NULL);
	if (editor.Window == NULL) {
		s_pShortcuts = NULL;
		return false;
	}
	CRect client;
	::GetClientRect(editor.Window, &client);
	int nPad = editor.S(10), nButtonHeight = editor.S(30);
	editor.Search = ::CreateWindowEx(WS_EX_CLIENTEDGE, _T("EDIT"), _T(""), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
		nPad, nPad, editor.S(300), editor.S(26), editor.Window, (HMENU)(INT_PTR)ID_SC_SEARCH, hInstance, NULL);
	::SendMessage(editor.Search, EM_SETCUEBANNER, TRUE, (LPARAM)CNLS::GetString(_T("Search command or key")));
	editor.OnlyKeys = ::CreateWindow(_T("BUTTON"), CNLS::GetString(_T("Only commands with a shortcut")), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
		nPad + editor.S(316), nPad + editor.S(3), editor.S(300), editor.S(22), editor.Window, (HMENU)(INT_PTR)ID_SC_ONLY_KEYS, hInstance, NULL);
	::SendMessage(editor.OnlyKeys, BM_SETCHECK, BST_CHECKED, 0);
	::SendMessage(editor.OnlyKeys, WM_SETFONT, (WPARAM)editor.Font, FALSE);
	editor.List = ::CreateWindowEx(0, WC_LISTVIEW, _T(""), WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
		nPad, nPad + editor.S(34), client.Width() - 2 * nPad, client.Height() - 3 * nPad - nButtonHeight - editor.S(34), editor.Window, (HMENU)(INT_PTR)ID_SC_LIST, hInstance, NULL);
	ListView_SetExtendedListViewStyle(editor.List, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	ListView_EnableGroupView(editor.List, TRUE);
	for (int nCategory = 0; nCategory <= CATEGORY_MOUSE; nCategory++) {
		LVGROUP group = { sizeof(LVGROUP) };
		group.mask = LVGF_HEADER | LVGF_GROUPID | LVGF_STATE;
		CString sHeader = CNLS::GetString(s_shortcutCategories[nCategory]);
		group.pszHeader = (LPWSTR)(LPCWSTR)sHeader;
		group.iGroupId = nCategory;
		group.stateMask = LVGS_COLLAPSIBLE;
		group.state = LVGS_COLLAPSIBLE;
		ListView_InsertGroup(editor.List, -1, &group);
	}
	struct { LPCTSTR Title; int Width; } columns[] = { { _T("Command"), 380 }, { _T("Shortcut"), 220 }, { _T("Name in KeyMap.txt"), 230 } };
	for (int i = 0; i < 3; i++) {
		LVCOLUMN column = { LVCF_TEXT | LVCF_WIDTH };
		column.pszText = (LPTSTR)CNLS::GetString(columns[i].Title);
		column.cx = editor.S(columns[i].Width);
		ListView_InsertColumn(editor.List, i, &column);
	}
	struct { int Id; LPCTSTR Text; bool Left; } buttons[] = {
		{ ID_SC_ADD, _T("Add shortcut..."), true }, { ID_SC_REMOVE, _T("Remove shortcuts"), true }, { ID_SC_DEFAULTS, _T("Defaults"), true }, { ID_SC_NOTEPAD, _T("Open file..."), true },
		{ ID_SC_SAVE, _T("Save"), false }, { ID_SC_CANCEL, _T("Cancel"), false },
	};
	int nLeftX = nPad, nRightX = client.Width() - nPad, nButtonY = client.Height() - nPad - nButtonHeight;
	// the buttons are as wide as their (translated) text
	HDC hMeasureDC = ::GetDC(editor.Window);
	HGDIOBJ hOldMeasureFont = ::SelectObject(hMeasureDC, editor.Font);
	auto ButtonWidth = [&](LPCTSTR sText, int nMin) {
		CRect textRect(0, 0, 0, 0);
		::DrawText(hMeasureDC, sText, -1, &textRect, DT_CALCRECT | DT_SINGLELINE);
		return max(editor.S(nMin), (int)textRect.Width() + editor.S(28));
	};
	for (int i = (int)(sizeof(buttons) / sizeof(buttons[0])) - 1; i >= 0; i--) {
		if (buttons[i].Left) continue;
		int nButtonWidth = ButtonWidth(CNLS::GetString(buttons[i].Text), 100);
		nRightX -= nButtonWidth;
		HWND hButton = ::CreateWindow(_T("BUTTON"), CNLS::GetString(buttons[i].Text), WS_CHILD | WS_VISIBLE | WS_TABSTOP | (buttons[i].Id == ID_SC_SAVE ? BS_DEFPUSHBUTTON : 0),
			nRightX, nButtonY, nButtonWidth, nButtonHeight, editor.Window, (HMENU)(INT_PTR)buttons[i].Id, hInstance, NULL);
		::SendMessage(hButton, WM_SETFONT, (WPARAM)editor.Font, FALSE);
		nRightX -= editor.S(8);
	}
	for (auto& button : buttons) {
		if (!button.Left) continue;
		int nButtonWidth = ButtonWidth(CNLS::GetString(button.Text), 130);
		HWND hButton = ::CreateWindow(_T("BUTTON"), CNLS::GetString(button.Text), WS_CHILD | WS_VISIBLE | WS_TABSTOP,
			nLeftX, nButtonY, nButtonWidth, nButtonHeight, editor.Window, (HMENU)(INT_PTR)button.Id, hInstance, NULL);
		::SendMessage(hButton, WM_SETFONT, (WPARAM)editor.Font, FALSE);
		nLeftX += nButtonWidth + editor.S(8);
	}
	::SelectObject(hMeasureDC, hOldMeasureFont);
	::ReleaseDC(editor.Window, hMeasureDC);
	::SendMessage(editor.Search, WM_SETFONT, (WPARAM)editor.Font, FALSE);
	::SendMessage(editor.List, WM_SETFONT, (WPARAM)editor.Font, FALSE);
	FillShortcutList(editor);
	editor.InitialMapping = editor.Mapping;
	DarkUI::SetBackground(editor.Window, DarkUI::Colors().Window);
	DarkUI::ApplyToWindow(editor.Window);

	if (hParent != NULL) ::EnableWindow(hParent, FALSE);
	if (DarkUI::IsSnapshotMode()) {
		DarkUI::ShowSnapshot(editor.Window, ID_SC_CANCEL);
	} else {
		::ShowWindow(editor.Window, SW_SHOWNORMAL);
		::SetFocus(editor.Search);
	}
	MSG msg;
	while (!editor.Done && ::GetMessage(&msg, NULL, 0, 0) > 0) {
		if (!::IsDialogMessage(editor.Window, &msg)) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
	}
	if (hParent != NULL) ::EnableWindow(hParent, TRUE);
	::DestroyWindow(editor.Window);
	if (hParent != NULL) ::SetActiveWindow(hParent);
	::DeleteObject(editor.Font);
	s_pShortcuts = NULL;
	return editor.Saved;
}

}
