#include "StdAfx.h"
#include "SettingsEditor.h"
#include "SettingsProvider.h"
#include "NLS.h"
#include "KeyMap.h"
#include "Helpers.h"
#include <algorithm>
#include <commdlg.h>
#include <commctrl.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <vector>
#include <set>

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
			if (bNumber) options.push_back({ sValue, sTrimmed });
		}
		// "'auto' for 2/3 of screen size", "'tr'    Turkish"
		if (sTrimmed.GetLength() > 2 && sTrimmed[0] == _T('\'')) {
			int nEnd = sTrimmed.Find(_T('\''), 1);
			if (nEnd > 1) {
				CString sValue = sTrimmed.Mid(1, nEnd - 1);
				if (sValue.Find(_T(' ')) < 0) options.push_back({ sValue, sTrimmed });
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
	::GetPrivateProfileString(_T("JPEGView"), sKey, MISSING, buffer, 2048, sFileName);
	bFound = _tcscmp(buffer, MISSING) != 0;
	return bFound ? CString(buffer) : CString();
}

///////////////////////////////////////////////////////////////////////////////////
// Window
///////////////////////////////////////////////////////////////////////////////////

static const int ID_SEARCH = 100, ID_CATEGORIES = 101, ID_SAVE = 102, ID_CANCEL = 103, ID_EDIT_INI = 104, ID_EDIT_KEYMAP = 105, ID_DEFAULTS = 106;
static const int ID_FIRST_CONTROL = 1000, ID_FIRST_RESET = 3000;

struct CEditor {
	HWND Window, Page, Search, Categories;
	HFONT Font, BoldFont, SmallFont;
	int Dpi;
	std::vector<CSetting> Settings;
	std::vector<CString> CategoryNames;
	std::vector<int> Shown; // indices into Settings of the controls on the page
	std::vector<HWND> ShownControls;
	std::set<HWND> Descriptions;
	int Scroll, PageHeight;
	bool Done, Saved, Updating, KeyMapChanged;
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
		case KIND_BOOL:
			::SendMessage(hControl, BM_SETCHECK, setting.Value.CompareNoCase(_T("true")) == 0 ? BST_CHECKED : BST_UNCHECKED, 0);
			break;
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
	// removes the old controls
	HWND hChild = ::GetWindow(editor.Page, GW_CHILD);
	while (hChild != NULL) {
		HWND hNext = ::GetWindow(hChild, GW_HWNDNEXT);
		::DestroyWindow(hChild);
		hChild = hNext;
	}
	editor.Descriptions.clear();
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
		} else {
			CString sText = setting.Key + _T(" ") + Humanize(setting.Key) + _T(" ") + setting.Comment + _T(" ") + CNLS::GetString(setting.Category);
			sText.MakeLower();
			if (sText.Find(sFind) >= 0) editor.Shown.push_back(i);
		}
	}

	CRect pageRect;
	::GetClientRect(editor.Page, &pageRect);
	HINSTANCE hInstance = ::GetModuleHandle(NULL);
	int nPad = editor.S(12);
	int nControlWidth = editor.S(230), nResetWidth = editor.S(28);
	int nTextWidth = pageRect.Width() - 3 * nPad - nControlWidth - nResetWidth - editor.S(::GetSystemMetrics(SM_CXVSCROLL) > 0 ? 4 : 0);
	int nY = nPad;
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
		CString sLabel = Humanize(setting.Key);
		CString sDescription = setting.Comment;
		sDescription.Replace(_T("\n"), _T(" "));
		sDescription.Trim();
		sDescription += (sDescription.IsEmpty() ? _T("") : _T("  ")) + CString(_T("[")) + setting.Key + _T("]");
		::SelectObject(hDC, editor.SmallFont);
		CRect descriptionRect(0, 0, nTextWidth, 0);
		::DrawText(hDC, sDescription, -1, &descriptionRect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
		int nLabelHeight = editor.S(20);
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
		nY += max(nLabelHeight + descriptionRect.Height(), editor.S(26)) + editor.S(14);
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
	::InvalidateRect(editor.Page, NULL, TRUE);
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
		case KIND_BOOL:
			setting.Value = ::SendMessage(hControl, BM_GETCHECK, 0, 0) == BST_CHECKED ? _T("true") : _T("false");
			break;
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
		case WM_CTLCOLORSTATIC: {
			HDC hDC = (HDC)wParam;
			::SetBkColor(hDC, ::GetSysColor(COLOR_WINDOW));
			bool bDescription = pEditor->Descriptions.count((HWND)lParam) > 0;
			::SetTextColor(hDC, bDescription ? RGB(100, 100, 100) : ::GetSysColor(COLOR_WINDOWTEXT));
			return (LRESULT)::GetSysColorBrush(COLOR_WINDOW);
		}
		case WM_CTLCOLORBTN:
			return (LRESULT)::GetSysColorBrush(COLOR_WINDOW);
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
			fputws(L"[JPEGView]\n", pFile);
			fclose(pFile);
		}
	}
	bool bOK = true;
	for (CSetting& setting : editor.Settings) {
		if (setting.Value != setting.Initial) {
			if (!::WritePrivateProfileString(_T("JPEGView"), setting.Key, setting.Value, sUserINI)) bOK = false;
		}
	}
	return bOK;
}

static bool HasChanges(CEditor& editor) {
	for (CSetting& setting : editor.Settings) if (setting.Value != setting.Initial) return true;
	return false;
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
					pEditor->Done = true;
					::PostMessage(hWnd, WM_NULL, 0, 0);
					return 0;
				case ID_EDIT_INI:
					::ShellExecute(hWnd, _T("open"), _T("notepad.exe"), CString(_T("\"")) + CSettingsProvider::This().GetUserINIFileName() + _T("\""), NULL, SW_SHOWNORMAL);
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
		case WM_CLOSE:
			pEditor->Done = true;
			::PostMessage(hWnd, WM_NULL, 0, 0);
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

bool Show(HWND hParent, CResult& result) {
	CSettingsProvider& sp = CSettingsProvider::This();
	CEditor editor;
	editor.Done = editor.Saved = editor.Updating = editor.KeyMapChanged = false;
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
	wndClass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wndClass.hIcon = ::LoadIcon(hInstance, MAKEINTRESOURCE(128));
	wndClass.lpszClassName = _T("JPEGViewSettingsEditor");
	::RegisterClass(&wndClass);
	WNDCLASS pageClass = { 0 };
	pageClass.lpfnWndProc = PageWndProc;
	pageClass.hInstance = hInstance;
	pageClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	pageClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	pageClass.lpszClassName = _T("JPEGViewSettingsPage");
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

	int nWidth = editor.S(1000), nHeight = editor.S(680);
	RECT parentRect;
	::GetWindowRect(hParent, &parentRect);
	HMONITOR hMonitor = ::MonitorFromWindow(hParent, MONITOR_DEFAULTTONEAREST);
	MONITORINFO monitorInfo = { sizeof(MONITORINFO) };
	::GetMonitorInfo(hMonitor, &monitorInfo);
	nWidth = min(nWidth, (int)(monitorInfo.rcWork.right - monitorInfo.rcWork.left) - 20);
	nHeight = min(nHeight, (int)(monitorInfo.rcWork.bottom - monitorInfo.rcWork.top) - 20);
	int nX = max((int)monitorInfo.rcWork.left, (int)(parentRect.left + parentRect.right - nWidth) / 2);
	int nY = max((int)monitorInfo.rcWork.top, (int)(parentRect.top + parentRect.bottom - nHeight) / 2);
	editor.Window = ::CreateWindowEx(WS_EX_DLGMODALFRAME, _T("JPEGViewSettingsEditor"), CNLS::GetString(_T("Settings")),
		WS_POPUP | WS_CAPTION | WS_SYSMENU, nX, nY, nWidth, nHeight, hParent, NULL, hInstance, NULL);
	if (editor.Window == NULL) {
		s_pEditor = NULL;
		return false;
	}
	CRect client;
	::GetClientRect(editor.Window, &client);
	int nPad = editor.S(10), nListWidth = editor.S(240), nButtonHeight = editor.S(30);
	editor.Search = ::CreateWindowEx(WS_EX_CLIENTEDGE, _T("EDIT"), _T(""), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
		nPad, nPad, nListWidth, editor.S(26), editor.Window, (HMENU)(INT_PTR)ID_SEARCH, hInstance, NULL);
	::SendMessage(editor.Search, EM_SETCUEBANNER, TRUE, (LPARAM)CNLS::GetString(_T("Search in all settings")));
	editor.Categories = ::CreateWindowEx(WS_EX_CLIENTEDGE, _T("LISTBOX"), _T(""), WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
		nPad, nPad + editor.S(32), nListWidth, client.Height() - 3 * nPad - nButtonHeight - editor.S(32), editor.Window, (HMENU)(INT_PTR)ID_CATEGORIES, hInstance, NULL);
	for (const CString& sCategory : editor.CategoryNames) {
		::SendMessage(editor.Categories, LB_ADDSTRING, 0, (LPARAM)CNLS::GetString(sCategory));
	}
	::SendMessage(editor.Categories, LB_SETITEMHEIGHT, 0, editor.S(24));
	editor.Page = ::CreateWindowEx(WS_EX_CLIENTEDGE, _T("JPEGViewSettingsPage"), _T(""), WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | WS_CLIPCHILDREN,
		2 * nPad + nListWidth, nPad, client.Width() - 3 * nPad - nListWidth, client.Height() - 3 * nPad - nButtonHeight, editor.Window, NULL, hInstance, NULL);

	struct { int Id; LPCTSTR Text; bool Left; } buttons[] = {
		{ ID_EDIT_INI, _T("Edit INI file..."), true }, { ID_EDIT_KEYMAP, _T("Edit keyboard shortcuts..."), true }, { ID_DEFAULTS, _T("Defaults of this page"), true },
		{ ID_SAVE, _T("Save"), false }, { ID_CANCEL, _T("Cancel"), false },
	};
	int nLeftX = nPad, nRightX = client.Width() - nPad;
	int nButtonY = client.Height() - nPad - nButtonHeight;
	for (int i = (int)(sizeof(buttons) / sizeof(buttons[0])) - 1; i >= 0; i--) {
		if (buttons[i].Left) continue;
		int nButtonWidth = editor.S(110);
		nRightX -= nButtonWidth;
		HWND hButton = ::CreateWindow(_T("BUTTON"), CNLS::GetString(buttons[i].Text), WS_CHILD | WS_VISIBLE | WS_TABSTOP | (buttons[i].Id == ID_SAVE ? BS_DEFPUSHBUTTON : 0),
			nRightX, nButtonY, nButtonWidth, nButtonHeight, editor.Window, (HMENU)(INT_PTR)buttons[i].Id, hInstance, NULL);
		::SendMessage(hButton, WM_SETFONT, (WPARAM)editor.Font, FALSE);
		nRightX -= editor.S(8);
	}
	for (auto& button : buttons) {
		if (!button.Left) continue;
		int nButtonWidth = editor.S(180);
		HWND hButton = ::CreateWindow(_T("BUTTON"), CNLS::GetString(button.Text), WS_CHILD | WS_VISIBLE | WS_TABSTOP,
			nLeftX, nButtonY, nButtonWidth, nButtonHeight, editor.Window, (HMENU)(INT_PTR)button.Id, hInstance, NULL);
		::SendMessage(hButton, WM_SETFONT, (WPARAM)editor.Font, FALSE);
		nLeftX += nButtonWidth + editor.S(8);
	}
	::SendMessage(editor.Search, WM_SETFONT, (WPARAM)editor.Font, FALSE);
	::SendMessage(editor.Categories, WM_SETFONT, (WPARAM)editor.Font, FALSE);
	::SendMessage(editor.Categories, LB_SETCURSEL, 0, 0);
	LayoutPage(editor);

	::EnableWindow(hParent, FALSE);
	::ShowWindow(editor.Window, SW_SHOWNORMAL);
	::SetFocus(editor.Categories);
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
	::EnableWindow(hParent, TRUE);
	::DestroyWindow(editor.Window);
	::SetActiveWindow(hParent);
	::DeleteObject(editor.Font);
	::DeleteObject(editor.BoldFont);
	::DeleteObject(editor.SmallFont);
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
	TCHAR sExe[MAX_PATH];
	::GetModuleFileName(NULL, sExe, MAX_PATH);
	// started after a moment so that this instance has ended (single instance mode)
	CString sParameters;
	sParameters.Format(_T("/c ping -n 2 127.0.0.1 >nul & start \"\" \"%s\" \"%s\""), sExe, sCurrentFile != NULL ? sCurrentFile : _T(""));
	::ShellExecute(NULL, _T("open"), _T("cmd.exe"), sParameters, NULL, SW_HIDE);
	::PostMessage(hParent, WM_CLOSE, 0, 0);
}

///////////////////////////////////////////////////////////////////////////////////
// Keyboard shortcut editor
///////////////////////////////////////////////////////////////////////////////////

static const int ID_SC_LIST = 200, ID_SC_SEARCH = 201, ID_SC_ADD = 202, ID_SC_REMOVE = 203, ID_SC_DEFAULTS = 204, ID_SC_NOTEPAD = 205, ID_SC_SAVE = 206, ID_SC_CANCEL = 207;

struct CShortcutEditor {
	HWND Window, List, Search;
	HFONT Font;
	int Dpi;
	std::vector<CString> Commands;                     // IDM names in the order of the symbols file
	std::vector<std::pair<CString, CString>> Mapping;  // key, IDM name
	bool Done, Saved;
	int S(int n) { return ::MulDiv(n, Dpi, 96); }
};

static CShortcutEditor* s_pShortcuts = NULL;

static CString GetUserKeyMapFileName() {
	return CString(Helpers::JPEGViewAppDataPath()) + _T("KeyMap.txt");
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
	::SendMessage(editor.List, WM_SETREDRAW, FALSE, 0);
	ListView_DeleteAllItems(editor.List);
	int nRow = 0;
	for (int i = 0; i < (int)editor.Commands.size(); i++) {
		CString sLabel = CommandLabel(editor.Commands[i]), sKeys = KeysOfCommand(editor, editor.Commands[i]);
		CString sText = sLabel + _T(" ") + sKeys + _T(" ") + editor.Commands[i];
		sText.MakeLower();
		if (!sFind.IsEmpty() && sText.Find(sFind) < 0) continue;
		LVITEM item = { LVIF_TEXT | LVIF_PARAM };
		item.iItem = nRow;
		item.pszText = (LPTSTR)(LPCTSTR)sLabel;
		item.lParam = i;
		int nIndex = ListView_InsertItem(editor.List, &item);
		ListView_SetItemText(editor.List, nIndex, 1, (LPTSTR)(LPCTSTR)sKeys);
		ListView_SetItemText(editor.List, nIndex, 2, (LPTSTR)(LPCTSTR)editor.Commands[i]);
		if (i == nSelectedCommand) {
			ListView_SetItemState(editor.List, nIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			ListView_EnsureVisible(editor.List, nIndex, FALSE);
		}
		nRow++;
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
static CString CaptureShortcut(HWND hParent, const CString& sCommandLabel, int nDpi) {
	HINSTANCE hInstance = ::GetModuleHandle(NULL);
	auto S = [nDpi](int n) { return ::MulDiv(n, nDpi, 96); };
	RECT parentRect;
	::GetWindowRect(hParent, &parentRect);
	int nWidth = S(420), nHeight = S(150);
	WNDCLASS wndClass = { 0 };
	wndClass.lpfnWndProc = ::DefWindowProc;
	wndClass.hInstance = hInstance;
	wndClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	wndClass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wndClass.lpszClassName = _T("JPEGViewShortcutCapture");
	::RegisterClass(&wndClass);
	HWND hWnd = ::CreateWindowEx(WS_EX_DLGMODALFRAME, _T("JPEGViewShortcutCapture"), CNLS::GetString(_T("New keyboard shortcut")), WS_POPUP | WS_CAPTION | WS_SYSMENU,
		(parentRect.left + parentRect.right - nWidth) / 2, (parentRect.top + parentRect.bottom - nHeight) / 2, nWidth, nHeight, hParent, NULL, hInstance, NULL);
	if (hWnd == NULL) return CString();
	NONCLIENTMETRICS metrics = { 0 };
	metrics.cbSize = sizeof(metrics);
	::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
	HFONT hFont = ::CreateFontIndirect(&metrics.lfMessageFont);
	CString sText = sCommandLabel + _T("\n\n") + CNLS::GetString(_T("Press the new key combination. Click here to cancel."));
	HWND hText = ::CreateWindow(_T("STATIC"), sText, WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOTIFY, S(12), S(16), nWidth - S(30), nHeight - S(60), hWnd, (HMENU)1, hInstance, NULL);
	::SendMessage(hText, WM_SETFONT, (WPARAM)hFont, FALSE);
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
	fputs("// This file defines the key mapping for JPEGView, it was written by the keyboard shortcut editor (Tools > Settings).\n", pFile);
	fputs("// See KeyMap-README.html for more information on how to customize this file.\n\n", pFile);
	for (const CString& sCommand : editor.Commands) {
		for (auto& entry : editor.Mapping) {
			if (entry.second != sCommand) continue;
			CStringA sKey(entry.first), sName(entry.second);
			fprintf(pFile, "%-24s%s\n", (LPCSTR)sKey, (LPCSTR)sName);
		}
	}
	fclose(pFile);
	return true;
}

static LRESULT CALLBACK ShortcutWndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
	CShortcutEditor* pEditor = s_pShortcuts;
	if (pEditor == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
	switch (nMsg) {
		case WM_NOTIFY: {
			NMHDR* pHeader = (NMHDR*)lParam;
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
				case ID_SC_ADD: {
					int nCommand = GetSelectedCommand(*pEditor);
					if (nCommand < 0) return 0;
					const CString& sCommand = pEditor->Commands[nCommand];
					CString sKey = CaptureShortcut(hWnd, CommandLabel(sCommand), pEditor->Dpi);
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
					::ShellExecute(hWnd, _T("open"), _T("notepad.exe"), _T("\"") + GetUserKeyMapFileName() + _T("\""), NULL, SW_SHOWNORMAL);
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
					pEditor->Done = true;
					::PostMessage(hWnd, WM_NULL, 0, 0);
					return 0;
			}
			break;
		case WM_CLOSE:
			pEditor->Done = true;
			::PostMessage(hWnd, WM_NULL, 0, 0);
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
	wndClass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wndClass.lpszClassName = _T("JPEGViewShortcutEditor");
	::RegisterClass(&wndClass);
	HDC hDC = ::GetDC(NULL);
	editor.Dpi = ::GetDeviceCaps(hDC, LOGPIXELSX);
	::ReleaseDC(NULL, hDC);
	NONCLIENTMETRICS metrics = { 0 };
	metrics.cbSize = sizeof(metrics);
	::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
	editor.Font = ::CreateFontIndirect(&metrics.lfMessageFont);

	int nWidth = editor.S(820), nHeight = editor.S(600);
	RECT parentRect;
	::GetWindowRect(hParent, &parentRect);
	editor.Window = ::CreateWindowEx(WS_EX_DLGMODALFRAME, _T("JPEGViewShortcutEditor"), CNLS::GetString(_T("Keyboard shortcuts")), WS_POPUP | WS_CAPTION | WS_SYSMENU,
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
	editor.List = ::CreateWindowEx(WS_EX_CLIENTEDGE, WC_LISTVIEW, _T(""), WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
		nPad, nPad + editor.S(34), client.Width() - 2 * nPad, client.Height() - 3 * nPad - nButtonHeight - editor.S(34), editor.Window, (HMENU)(INT_PTR)ID_SC_LIST, hInstance, NULL);
	ListView_SetExtendedListViewStyle(editor.List, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	struct { LPCTSTR Title; int Width; } columns[] = { { _T("Command"), 300 }, { _T("Shortcut"), 200 }, { _T("Name in KeyMap.txt"), 250 } };
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
	for (int i = (int)(sizeof(buttons) / sizeof(buttons[0])) - 1; i >= 0; i--) {
		if (buttons[i].Left) continue;
		nRightX -= editor.S(100);
		HWND hButton = ::CreateWindow(_T("BUTTON"), CNLS::GetString(buttons[i].Text), WS_CHILD | WS_VISIBLE | WS_TABSTOP | (buttons[i].Id == ID_SC_SAVE ? BS_DEFPUSHBUTTON : 0),
			nRightX, nButtonY, editor.S(100), nButtonHeight, editor.Window, (HMENU)(INT_PTR)buttons[i].Id, hInstance, NULL);
		::SendMessage(hButton, WM_SETFONT, (WPARAM)editor.Font, FALSE);
		nRightX -= editor.S(8);
	}
	for (auto& button : buttons) {
		if (!button.Left) continue;
		HWND hButton = ::CreateWindow(_T("BUTTON"), CNLS::GetString(button.Text), WS_CHILD | WS_VISIBLE | WS_TABSTOP,
			nLeftX, nButtonY, editor.S(130), nButtonHeight, editor.Window, (HMENU)(INT_PTR)button.Id, hInstance, NULL);
		::SendMessage(hButton, WM_SETFONT, (WPARAM)editor.Font, FALSE);
		nLeftX += editor.S(138);
	}
	::SendMessage(editor.Search, WM_SETFONT, (WPARAM)editor.Font, FALSE);
	::SendMessage(editor.List, WM_SETFONT, (WPARAM)editor.Font, FALSE);
	FillShortcutList(editor);

	::EnableWindow(hParent, FALSE);
	::ShowWindow(editor.Window, SW_SHOWNORMAL);
	::SetFocus(editor.Search);
	MSG msg;
	while (!editor.Done && ::GetMessage(&msg, NULL, 0, 0) > 0) {
		if (!::IsDialogMessage(editor.Window, &msg)) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
	}
	::EnableWindow(hParent, TRUE);
	::DestroyWindow(editor.Window);
	::SetActiveWindow(hParent);
	::DeleteObject(editor.Font);
	s_pShortcuts = NULL;
	return editor.Saved;
}

}
