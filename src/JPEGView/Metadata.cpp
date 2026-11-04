#include "StdAfx.h"
#include "Metadata.h"
#include "NLS.h"
#include <propkey.h>
#include <propvarutil.h>
#include <shobjidl.h>
#include <algorithm>

#pragma comment(lib, "propsys.lib")

namespace Metadata {

// Shown first in this order, the others are sorted by name
static LPCTSTR s_order[] = {
	_T("System.Title"), _T("System.Subject"), _T("System.Rating"), _T("System.Keywords"), _T("System.Comment"),
	_T("System.Author"), _T("System.Copyright"), _T("System.Photo.DateTaken"), _T("System.Photo.CameraManufacturer"),
	_T("System.Photo.CameraModel"), _T("System.Photo.LensManufacturer"), _T("System.Photo.LensModel"),
	_T("System.Photo.FNumber"), _T("System.Photo.ExposureTime"), _T("System.Photo.ISOSpeed"), _T("System.Photo.FocalLength"),
	_T("System.Image.Dimensions"), _T("System.Image.HorizontalSize"), _T("System.Image.VerticalSize"),
	_T("System.GPS.Latitude"), _T("System.GPS.LatitudeRef"), _T("System.GPS.Longitude"), _T("System.GPS.LongitudeRef"), _T("System.GPS.Altitude"),
};

// Editable properties shown even if the file has no value, so they can be added
static const PROPERTYKEY* s_commonKeys[] = {
	&PKEY_Title, &PKEY_Subject, &PKEY_Keywords, &PKEY_Comment, &PKEY_Author, &PKEY_Copyright, &PKEY_Photo_DateTaken,
};

static LPCTSTR s_descriptionNames[] = {
	_T("System.Title"), _T("System.Subject"), _T("System.Keywords"), _T("System.Comment"), _T("System.Author"),
	_T("System.Copyright"), _T("System.Rating"), _T("System.SimpleRating"), _T("System.RatingText"), _T("System.Category"), _T("System.Company"),
};

static const int MAX_DISPLAY_LENGTH = 300;

LPCTSTR GetGroupName(EGroup eGroup) {
	switch (eGroup) {
		case GROUP_DESCRIPTION: return _T("Description");
		case GROUP_CAMERA: return _T("Camera");
		case GROUP_IMAGE: return _T("Image");
		case GROUP_LOCATION: return _T("Location");
		default: return _T("Other");
	}
}

static EGroup GetGroup(const CString& sCanonical) {
	if (sCanonical.Find(_T("System.GPS.")) == 0) return GROUP_LOCATION;
	if (sCanonical.Find(_T("System.Photo.")) == 0) return GROUP_CAMERA;
	if (sCanonical.Find(_T("System.Image.")) == 0) return GROUP_IMAGE;
	for (LPCTSTR sName : s_descriptionNames) {
		if (sCanonical == sName) return GROUP_DESCRIPTION;
	}
	return GROUP_OTHER;
}

static int GetOrder(const CString& sCanonical) {
	for (int i = 0; i < sizeof(s_order) / sizeof(s_order[0]); i++) {
		if (sCanonical == s_order[i]) return i;
	}
	return 1000;
}

static CString FormatDouble(double dValue) {
	CString s;
	s.Format(_T("%.10g"), dValue); // not localized, can be parsed again
	return s;
}

// Text for the edit window, dates in local time and numbers not localized
static CString GetEditText(const PROPVARIANT& value) {
	if (value.vt == VT_FILETIME) {
		SYSTEMTIME utc, local;
		if (::FileTimeToSystemTime(&value.filetime, &utc) && ::SystemTimeToTzSpecificLocalTime(NULL, &utc, &local)) {
			CString s;
			s.Format(_T("%04d-%02d-%02d %02d:%02d:%02d"), local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond);
			return s;
		}
		return CString();
	}
	if (value.vt == VT_R8) {
		return FormatDouble(value.dblVal);
	}
	if (value.vt == (VT_VECTOR | VT_R8)) {
		CString s;
		for (ULONG i = 0; i < value.cadbl.cElems; i++) {
			s += (i > 0 ? _T("; ") : _T("")) + FormatDouble(value.cadbl.pElems[i]);
		}
		return s;
	}
	if ((value.vt & ~VT_VECTOR) == VT_BLOB || value.vt == VT_STREAM || value.vt == VT_UNKNOWN || value.vt == (VT_VECTOR | VT_UI1)) {
		return CString();
	}
	PWSTR pText = NULL;
	CString s;
	if (SUCCEEDED(::PropVariantToStringAlloc(value, &pText))) {
		s = pText;
		::CoTaskMemFree(pText);
	}
	return s;
}

static CString GetDisplayText(const PROPERTYKEY& key, const PROPVARIANT& value) {
	if ((value.vt & ~VT_VECTOR) == VT_BLOB || value.vt == VT_STREAM || value.vt == VT_UNKNOWN || value.vt == (VT_VECTOR | VT_UI1)) {
		return CNLS::GetString(_T("(binary data)"));
	}
	PWSTR pText = NULL;
	CString s;
	if (value.vt == (VT_VECTOR | VT_R8)) {
		s = GetEditText(value); // Windows shows all digits of the binary value (26.5100000000094838)
	} else if (SUCCEEDED(::PSFormatForDisplayAlloc(key, value, PDFF_DEFAULT, &pText))) {
		s = pText;
		::CoTaskMemFree(pText);
	} else {
		s = GetEditText(value);
	}
	// Windows adds invisible text direction marks to dates and numbers, they would be copied too
	for (wchar_t c : { (wchar_t)0x200E, (wchar_t)0x200F, (wchar_t)0x202A, (wchar_t)0x202B, (wchar_t)0x202C, (wchar_t)0x202D, (wchar_t)0x202E }) {
		s.Remove(c);
	}
	s.Trim();
	if (s.GetLength() > MAX_DISPLAY_LENGTH) {
		s = s.Left(MAX_DISPLAY_LENGTH) + _T("...");
	}
	return s;
}

static VARTYPE GetPropertyType(const PROPERTYKEY& key) {
	IPropertyDescription* pDescription = NULL;
	VARTYPE vt = VT_LPWSTR;
	if (SUCCEEDED(::PSGetPropertyDescription(key, IID_PPV_ARGS(&pDescription)))) {
		if (FAILED(pDescription->GetPropertyType(&vt)) || vt == VT_EMPTY) {
			vt = VT_LPWSTR;
		}
		pDescription->Release();
	}
	return vt;
}

void Read(LPCTSTR sFileName, std::vector<CEntry>& entries) {
	entries.clear();
	IPropertyStore* pStore = NULL;
	if (FAILED(::SHGetPropertyStoreFromParsingName(sFileName, NULL, GPS_HANDLERPROPERTIESONLY, IID_PPV_ARGS(&pStore)))) {
		return;
	}
	// all values are read first, the file can not be opened for writing while the read only store is open
	std::vector<std::pair<PROPERTYKEY, PROPVARIANT>> values;
	DWORD nCount = 0;
	pStore->GetCount(&nCount);
	for (DWORD i = 0; i < nCount; i++) {
		PROPERTYKEY key;
		if (FAILED(pStore->GetAt(i, &key))) continue;
		PROPVARIANT value;
		::PropVariantInit(&value);
		if (SUCCEEDED(pStore->GetValue(key, &value)) && value.vt != VT_EMPTY && value.vt != VT_NULL) {
			values.push_back({ key, value });
		} else {
			::PropVariantClear(&value);
		}
	}
	pStore->Release();

	// the capabilities tell which properties the handler of the file format can write
	IPropertyStore* pWritableStore = NULL;
	IPropertyStoreCapabilities* pCapabilities = NULL;
	if (SUCCEEDED(::SHGetPropertyStoreFromParsingName(sFileName, NULL, GPS_READWRITE, IID_PPV_ARGS(&pWritableStore)))) {
		pWritableStore->QueryInterface(IID_PPV_ARGS(&pCapabilities));
	}
	auto IsWritable = [&](const PROPERTYKEY& key, IPropertyDescription* pDescription) {
		if (pWritableStore == NULL) return false;
		PROPDESC_TYPE_FLAGS flags = PDTF_DEFAULT;
		if (SUCCEEDED(pDescription->GetTypeFlags(PDTF_ISINNATE, &flags)) && (flags & PDTF_ISINNATE) != 0) return false;
		return pCapabilities == NULL || pCapabilities->IsPropertyWritable(key) == S_OK;
	};
	auto AddEntry = [&](const PROPERTYKEY& key, const PROPVARIANT* pValue) {
		IPropertyDescription* pDescription = NULL;
		if (FAILED(::PSGetPropertyDescription(key, IID_PPV_ARGS(&pDescription)))) {
			return;
		}
		CEntry entry;
		entry.Key = key;
		PWSTR pText = NULL;
		CString sCanonical;
		if (SUCCEEDED(pDescription->GetCanonicalName(&pText))) {
			sCanonical = pText;
			::CoTaskMemFree(pText);
		}
		pText = NULL;
		if (SUCCEEDED(pDescription->GetDisplayName(&pText)) && pText != NULL && pText[0] != 0) {
			entry.Name = pText;
		} else {
			entry.Name = sCanonical.Find(_T("System.")) == 0 ? sCanonical.Mid(7) : sCanonical;
		}
		if (pText != NULL) ::CoTaskMemFree(pText);
		entry.Group = GetGroup(sCanonical);
		entry.Order = GetOrder(sCanonical);
		entry.Writable = IsWritable(key, pDescription);
		entry.Present = pValue != NULL;
		if (pValue != NULL) {
			entry.Value = GetDisplayText(key, *pValue);
			entry.EditValue = GetEditText(*pValue);
		}
		pDescription->Release();
		// internal identifiers like the media class GUID are not useful
		bool bIsGuid = !entry.Writable && entry.Value.GetLength() == 38 && entry.Value[0] == _T('{') && entry.Value[37] == _T('}');
		if (!entry.Name.IsEmpty() && !bIsGuid && (!entry.Present || !entry.Value.IsEmpty() || entry.Writable)) {
			entries.push_back(entry);
		}
	};

	for (auto& value : values) {
		AddEntry(value.first, &value.second);
		::PropVariantClear(&value.second);
	}
	// common editable properties the file does not have yet
	if (pWritableStore != NULL) {
		for (const PROPERTYKEY* pKey : s_commonKeys) {
			bool bFound = false;
			for (const CEntry& entry : entries) {
				if (IsEqualPropertyKey(entry.Key, *pKey)) {
					bFound = true;
					break;
				}
			}
			if (!bFound && (pCapabilities == NULL || pCapabilities->IsPropertyWritable(*pKey) == S_OK)) {
				AddEntry(*pKey, NULL);
			}
		}
	}
	if (pCapabilities != NULL) pCapabilities->Release();
	if (pWritableStore != NULL) pWritableStore->Release();

	std::stable_sort(entries.begin(), entries.end(), [](const CEntry& a, const CEntry& b) {
		if (a.Group != b.Group) return a.Group < b.Group;
		if (a.Order != b.Order) return a.Order < b.Order;
		return a.Name.CompareNoCase(b.Name) < 0;
	});
}

///////////////////////////////////////////////////////////////////////////////////
// Parsing of the edited text
///////////////////////////////////////////////////////////////////////////////////

static bool ParseDouble(CString sText, double& dValue) {
	sText.Trim();
	sText.Replace(_T(','), _T('.'));
	if (sText.IsEmpty()) return false;
	TCHAR* pEnd = NULL;
	dValue = _tcstod(sText, &pEnd);
	return pEnd != NULL && *pEnd == 0;
}

static bool ParseDate(const CString& sText, FILETIME& fileTime) {
	int n1 = 0, n2 = 0, n3 = 0, nHour = 0, nMinute = 0, nSecond = 0;
	TCHAR sep1[2], sep2[2];
	int nFields = _stscanf_s(sText, _T("%d%1[-:./]%d%1[-:./]%d %d:%d:%d"), &n1, sep1, 2, &n2, sep2, 2, &n3, &nHour, &nMinute, &nSecond);
	if (nFields < 5) return false;
	SYSTEMTIME local = { 0 };
	if (n1 > 31) { // year first
		local.wYear = (WORD)n1; local.wMonth = (WORD)n2; local.wDay = (WORD)n3;
	} else {
		local.wDay = (WORD)n1; local.wMonth = (WORD)n2; local.wYear = (WORD)n3;
	}
	local.wHour = (WORD)nHour; local.wMinute = (WORD)nMinute; local.wSecond = (WORD)nSecond;
	if (local.wYear < 1601 || local.wMonth < 1 || local.wMonth > 12 || local.wDay < 1 || local.wDay > 31 || nHour > 23 || nMinute > 59 || nSecond > 59) {
		return false;
	}
	SYSTEMTIME utc;
	return ::TzSpecificLocalTimeToSystemTime(NULL, &local, &utc) && ::SystemTimeToFileTime(&utc, &fileTime);
}

static bool ParseValue(const PROPERTYKEY& key, const CString& sText, PROPVARIANT& value) {
	VARTYPE vt = GetPropertyType(key);
	if (vt == VT_FILETIME) {
		FILETIME fileTime;
		return ParseDate(sText, fileTime) && SUCCEEDED(::InitPropVariantFromFileTime(&fileTime, &value));
	}
	if (vt == VT_R8) {
		double dValue;
		return ParseDouble(sText, dValue) && SUCCEEDED(::InitPropVariantFromDouble(dValue, &value));
	}
	if (vt == (VT_VECTOR | VT_R8)) {
		std::vector<double> values;
		int nStart = 0;
		CString sPart = sText.Tokenize(_T(";"), nStart);
		while (nStart >= 0) {
			double dValue;
			if (!ParseDouble(sPart, dValue)) return false;
			values.push_back(dValue);
			sPart = sText.Tokenize(_T(";"), nStart);
		}
		return !values.empty() && SUCCEEDED(::InitPropVariantFromDoubleVector(&values[0], (ULONG)values.size(), &value));
	}
	if (vt == (VT_VECTOR | VT_LPWSTR)) {
		return SUCCEEDED(::InitPropVariantFromStringAsVector(sText, &value));
	}
	PROPVARIANT text;
	if (FAILED(::InitPropVariantFromString(sText, &text))) return false;
	bool bOK = vt == VT_LPWSTR ? SUCCEEDED(::PropVariantCopy(&value, &text)) : SUCCEEDED(::PropVariantChangeType(&value, text, PVCHF_DEFAULT, vt));
	::PropVariantClear(&text);
	return bOK;
}

static bool WriteValues(LPCTSTR sFileName, const std::vector<std::pair<PROPERTYKEY, PROPVARIANT*>>& values, int& nWritten) {
	nWritten = 0;
	IPropertyStore* pStore = NULL;
	if (FAILED(::SHGetPropertyStoreFromParsingName(sFileName, NULL, GPS_READWRITE, IID_PPV_ARGS(&pStore)))) {
		return false;
	}
	for (const auto& value : values) {
		if (SUCCEEDED(pStore->SetValue(value.first, *value.second))) {
			nWritten++;
		}
	}
	bool bOK = nWritten > 0 && SUCCEEDED(pStore->Commit());
	pStore->Release();
	return bOK;
}

///////////////////////////////////////////////////////////////////////////////////
// Edit window
///////////////////////////////////////////////////////////////////////////////////

static const int ID_REMOVE = 100;

struct CEditWindow {
	HWND Edit;
	int Result; // 0: cancel, IDOK, ID_REMOVE
	bool Done;
};

static LRESULT CALLBACK EditWndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
	CEditWindow* pWindow = (CEditWindow*)::GetWindowLongPtr(hWnd, GWLP_USERDATA);
	if (nMsg == WM_CREATE) {
		::SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCT*)lParam)->lpCreateParams);
		return 0;
	}
	if (pWindow != NULL && (nMsg == WM_COMMAND || nMsg == WM_CLOSE)) {
		int nId = (nMsg == WM_CLOSE) ? IDCANCEL : LOWORD(wParam);
		if (nId == IDOK || nId == IDCANCEL || nId == ID_REMOVE) {
			pWindow->Result = (nId == IDCANCEL) ? 0 : nId;
			pWindow->Done = true;
			::PostMessage(hWnd, WM_NULL, 0, 0);
			return 0;
		}
	}
	if (nMsg == WM_CTLCOLORSTATIC) {
		::SetBkMode((HDC)wParam, TRANSPARENT);
		return (LRESULT)::GetSysColorBrush(COLOR_BTNFACE);
	}
	return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

// Returns 0 (cancel), IDOK or ID_REMOVE
static int ShowEditWindow(HWND hParent, const CEntry& entry, VARTYPE vt, CString& sText) {
	HINSTANCE hInstance = ::GetModuleHandle(NULL);
	WNDCLASS wndClass{ 0 };
	wndClass.lpfnWndProc = EditWndProc;
	wndClass.hInstance = hInstance;
	wndClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	wndClass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wndClass.lpszClassName = _T("JPEGViewMetadataEdit");
	::RegisterClass(&wndClass);

	HDC hDC = ::GetDC(NULL);
	int nDPI = ::GetDeviceCaps(hDC, LOGPIXELSX);
	::ReleaseDC(NULL, hDC);
	auto S = [nDPI](int n) { return ::MulDiv(n, nDPI, 96); };

	CString sHint = CNLS::GetString(_T("An empty value removes it from the file."));
	if (vt == VT_FILETIME) {
		sHint = CString(CNLS::GetString(_T("Format: year-month-day hour:minute:second, for example 2024-05-31 14:30:00"))) + _T("\n") + sHint;
	} else if ((vt & VT_VECTOR) != 0) {
		sHint = CString(CNLS::GetString(_T("Separate several values with a semicolon (;)"))) + _T("\n") + sHint;
	}

	int nWidth = S(480), nInner = nWidth - S(24);
	RECT clientRect = { 0, 0, nWidth, S(150) };
	DWORD nStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU;
	::AdjustWindowRectEx(&clientRect, nStyle, FALSE, WS_EX_DLGMODALFRAME);
	RECT parentRect;
	::GetWindowRect(hParent, &parentRect);
	int nWindowWidth = clientRect.right - clientRect.left, nWindowHeight = clientRect.bottom - clientRect.top;
	CEditWindow window = { NULL, 0, false };
	HWND hWnd = ::CreateWindowEx(WS_EX_DLGMODALFRAME, _T("JPEGViewMetadataEdit"), entry.Name, nStyle,
		(parentRect.left + parentRect.right - nWindowWidth) / 2, (parentRect.top + parentRect.bottom - nWindowHeight) / 2,
		nWindowWidth, nWindowHeight, hParent, NULL, hInstance, &window);
	if (hWnd == NULL) {
		return 0;
	}
	NONCLIENTMETRICS metrics{ 0 };
	metrics.cbSize = sizeof(metrics);
	::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
	HFONT hFont = ::CreateFontIndirect(&metrics.lfMessageFont);
	HWND hHint = ::CreateWindow(_T("STATIC"), sHint, WS_CHILD | WS_VISIBLE, S(12), S(12), nInner, S(40), hWnd, NULL, hInstance, NULL);
	window.Edit = ::CreateWindowEx(WS_EX_CLIENTEDGE, _T("EDIT"), sText, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
		S(12), S(60), nInner, S(26), hWnd, NULL, hInstance, NULL);
	int nButton = S(100), nButtonY = S(106);
	HWND hOK = ::CreateWindow(_T("BUTTON"), CNLS::GetString(_T("OK")), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
		nWidth - S(12) - 2 * nButton - S(8), nButtonY, nButton, S(30), hWnd, (HMENU)IDOK, hInstance, NULL);
	HWND hCancel = ::CreateWindow(_T("BUTTON"), CNLS::GetString(_T("Cancel")), WS_CHILD | WS_VISIBLE | WS_TABSTOP,
		nWidth - S(12) - nButton, nButtonY, nButton, S(30), hWnd, (HMENU)IDCANCEL, hInstance, NULL);
	HWND hRemove = NULL;
	if (entry.Present) {
		hRemove = ::CreateWindow(_T("BUTTON"), CNLS::GetString(_T("Remove")), WS_CHILD | WS_VISIBLE | WS_TABSTOP,
			S(12), nButtonY, nButton, S(30), hWnd, (HMENU)(INT_PTR)ID_REMOVE, hInstance, NULL);
	}
	for (HWND hControl : { hHint, window.Edit, hOK, hCancel, hRemove }) {
		if (hControl != NULL) ::SendMessage(hControl, WM_SETFONT, (WPARAM)hFont, FALSE);
	}
	::SendMessage(window.Edit, EM_SETSEL, 0, -1);
	::EnableWindow(hParent, FALSE);
	::ShowWindow(hWnd, SW_SHOWNORMAL);
	::SetFocus(window.Edit);
	MSG msg;
	while (!window.Done && ::GetMessage(&msg, NULL, 0, 0) > 0) {
		if (!::IsDialogMessage(hWnd, &msg)) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
	}
	if (window.Result == IDOK) {
		int nLength = ::GetWindowTextLength(window.Edit);
		::GetWindowText(window.Edit, sText.GetBuffer(nLength + 1), nLength + 1);
		sText.ReleaseBuffer();
	}
	::EnableWindow(hParent, TRUE);
	::DestroyWindow(hWnd);
	::SetActiveWindow(hParent);
	::DeleteObject(hFont);
	return window.Result;
}

bool Edit(HWND hParent, LPCTSTR sFileName, const CEntry& entry, CString& sError) {
	if (!entry.Writable) {
		return false;
	}
	VARTYPE vt = GetPropertyType(entry.Key);
	CString sText = entry.EditValue;
	int nResult = ShowEditWindow(hParent, entry, vt, sText);
	if (nResult == 0) {
		return false;
	}
	if (nResult == ID_REMOVE) {
		sText.Empty();
	}
	sText.Trim();
	if (entry.Present ? (sText == entry.EditValue) : sText.IsEmpty()) {
		return false; // nothing changed
	}
	PROPVARIANT value;
	::PropVariantInit(&value);
	if (!sText.IsEmpty() && !ParseValue(entry.Key, sText, value)) {
		sError = CNLS::GetString(_T("The value is not valid."));
		return false;
	}
	int nWritten = 0;
	bool bOK = WriteValues(sFileName, { { entry.Key, &value } }, nWritten);
	::PropVariantClear(&value);
	if (!bOK) {
		sError = CNLS::GetString(_T("The value could not be written to the file."));
	}
	return bOK;
}

int Remove(LPCTSTR sFileName, const std::vector<CEntry>& entries, bool bLocationOnly, CString& sError) {
	PROPVARIANT empty;
	::PropVariantInit(&empty);
	std::vector<std::pair<PROPERTYKEY, PROPVARIANT*>> values;
	for (const CEntry& entry : entries) {
		// the orientation is kept, removing it would turn the image
		if (entry.Writable && entry.Present && (!bLocationOnly || entry.Group == GROUP_LOCATION) && !IsEqualPropertyKey(entry.Key, PKEY_Photo_Orientation)) {
			values.push_back({ entry.Key, &empty });
		}
	}
	if (values.empty()) {
		return 0;
	}
	int nWritten = 0;
	if (!WriteValues(sFileName, values, nWritten)) {
		sError = CNLS::GetString(_T("The value could not be written to the file."));
		return -1;
	}
	return nWritten;
}

CString ToText(const std::vector<CEntry>& entries) {
	CString sText;
	int nGroup = -1;
	for (const CEntry& entry : entries) {
		if (!entry.Present) continue;
		if (entry.Group != nGroup) {
			nGroup = entry.Group;
			sText += (sText.IsEmpty() ? _T("") : _T("\r\n")) + CString(_T("[")) + CNLS::GetString(GetGroupName(entry.Group)) + _T("]\r\n");
		}
		sText += entry.Name + _T(": ") + entry.Value + _T("\r\n");
	}
	return sText;
}

}
