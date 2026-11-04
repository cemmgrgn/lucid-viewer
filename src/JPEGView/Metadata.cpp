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

static const struct { LPCTSTR Canonical; LPCTSTR Name; } s_fallbackNames[] = {
	{ _T("System.GPS.LatitudeRef"), _T("Latitude direction") }, { _T("System.GPS.LongitudeRef"), _T("Longitude direction") },
	{ _T("System.GPS.AltitudeRef"), _T("Altitude reference") }, { _T("System.GPS.Date"), _T("GPS time") },
	{ _T("System.GPS.DestBearing"), _T("Direction of the target") }, { _T("System.GPS.DestBearingRef"), _T("Direction of the target reference") },
	{ _T("System.GPS.ImgDirection"), _T("Camera direction") }, { _T("System.GPS.ImgDirectionRef"), _T("Camera direction reference") },
	{ _T("System.GPS.Speed"), _T("Speed") }, { _T("System.GPS.SpeedRef"), _T("Speed unit") },
};

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
		if (value.vt == VT_R8 && s.GetLength() > 10 && s.SpanIncluding(_T("0123456789.,-")) == s) {
			s.Format(_T("%.6g"), value.dblVal);
		}
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

///////////////////////////////////////////////////////////////////////////////////
// EXIF block of PNG (eXIf chunk) and WebP (EXIF chunk) files, the property handlers of Windows do not read it
///////////////////////////////////////////////////////////////////////////////////

enum { IFD_MAIN = 0, IFD_GPS = 1 };

static const struct { int Ifd; WORD Tag; const PROPERTYKEY* Key; } s_exifTags[] = {
	{ IFD_MAIN, 0x010E, &PKEY_Title }, { IFD_MAIN, 0x010F, &PKEY_Photo_CameraManufacturer }, { IFD_MAIN, 0x0110, &PKEY_Photo_CameraModel },
	{ IFD_MAIN, 0x0112, &PKEY_Photo_Orientation }, { IFD_MAIN, 0x0131, &PKEY_SoftwareUsed }, { IFD_MAIN, 0x013B, &PKEY_Author },
	{ IFD_MAIN, 0x8298, &PKEY_Copyright }, { IFD_MAIN, 0x829A, &PKEY_Photo_ExposureTime }, { IFD_MAIN, 0x829D, &PKEY_Photo_FNumber },
	{ IFD_MAIN, 0x8822, &PKEY_Photo_ProgramMode }, { IFD_MAIN, 0x8827, &PKEY_Photo_ISOSpeed }, { IFD_MAIN, 0x9003, &PKEY_Photo_DateTaken },
	{ IFD_MAIN, 0x9201, &PKEY_Photo_ShutterSpeed }, { IFD_MAIN, 0x9202, &PKEY_Photo_Aperture }, { IFD_MAIN, 0x9203, &PKEY_Photo_Brightness },
	{ IFD_MAIN, 0x9204, &PKEY_Photo_ExposureBias }, { IFD_MAIN, 0x9207, &PKEY_Photo_MeteringMode }, { IFD_MAIN, 0x9209, &PKEY_Photo_Flash },
	{ IFD_MAIN, 0x920A, &PKEY_Photo_FocalLength }, { IFD_MAIN, 0xA001, &PKEY_Image_ColorSpace }, { IFD_MAIN, 0xA403, &PKEY_Photo_WhiteBalance },
	{ IFD_MAIN, 0xA405, &PKEY_Photo_FocalLengthInFilm }, { IFD_MAIN, 0xA433, &PKEY_Photo_LensManufacturer }, { IFD_MAIN, 0xA434, &PKEY_Photo_LensModel },
	{ IFD_GPS, 1, &PKEY_GPS_LatitudeRef }, { IFD_GPS, 2, &PKEY_GPS_Latitude }, { IFD_GPS, 3, &PKEY_GPS_LongitudeRef }, { IFD_GPS, 4, &PKEY_GPS_Longitude },
	{ IFD_GPS, 5, &PKEY_GPS_AltitudeRef }, { IFD_GPS, 6, &PKEY_GPS_Altitude }, { IFD_GPS, 12, &PKEY_GPS_SpeedRef }, { IFD_GPS, 13, &PKEY_GPS_Speed },
	{ IFD_GPS, 16, &PKEY_GPS_ImgDirectionRef }, { IFD_GPS, 17, &PKEY_GPS_ImgDirection }, { IFD_GPS, 23, &PKEY_GPS_DestBearingRef },
	{ IFD_GPS, 24, &PKEY_GPS_DestBearing },
};

struct CExifValue {
	int Ifd;
	WORD Tag;
	bool IsText;
	CString Text;
	std::vector<double> Numbers;
};

class CTiff {
public:
	CTiff(const unsigned char* d, size_t n) : m_d(d), m_n(n), m_bLittle(false), m_bValid(false) {
		if (n >= 8 && ((d[0] == 'I' && d[1] == 'I' && d[2] == 42 && d[3] == 0) || (d[0] == 'M' && d[1] == 'M' && d[2] == 0 && d[3] == 42))) {
			m_bLittle = d[0] == 'I';
			m_bValid = true;
		}
	}
	bool Valid() const { return m_bValid; }
	bool Little() const { return m_bLittle; }
	bool Has(size_t nPos, size_t nSize) const { return nPos <= m_n && nSize <= m_n - nPos; }
	unsigned U16(size_t p) const { return Has(p, 2) ? (m_bLittle ? m_d[p] | (m_d[p + 1] << 8) : (m_d[p] << 8) | m_d[p + 1]) : 0; }
	unsigned U32(size_t p) const { return Has(p, 4) ? (m_bLittle ? m_d[p] | (m_d[p + 1] << 8) | (m_d[p + 2] << 16) | ((unsigned)m_d[p + 3] << 24) :
		((unsigned)m_d[p] << 24) | (m_d[p + 1] << 16) | (m_d[p + 2] << 8) | m_d[p + 3]) : 0; }
	static size_t TypeSize(unsigned nType) {
		static const size_t sizes[] = { 0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8 };
		return nType < 13 ? sizes[nType] : 0;
	}
	// position of the value data of the entry at nEntry, SIZE_MAX if invalid
	size_t ValuePos(size_t nEntry, size_t& nTotal) const {
		unsigned nType = U16(nEntry + 2), nCount = U32(nEntry + 4);
		size_t nSize = TypeSize(nType);
		if (nSize == 0 || nCount > 0x10000000) return SIZE_MAX;
		nTotal = nSize * nCount;
		size_t nPos = nTotal <= 4 ? nEntry + 8 : U32(nEntry + 8);
		return Has(nPos, nTotal) ? nPos : SIZE_MAX;
	}
	const unsigned char* Data() const { return m_d; }
private:
	const unsigned char* m_d;
	size_t m_n;
	bool m_bLittle, m_bValid;
};

static void ReadExifIfd(const CTiff& tiff, size_t nOffset, int nIfd, std::vector<CExifValue>& values, int nDepth) {
	if (nDepth > 3 || !tiff.Has(nOffset, 2)) return;
	unsigned nEntries = tiff.U16(nOffset);
	for (unsigned i = 0; i < nEntries && i < 1000; i++) {
		size_t nEntry = nOffset + 2 + 12 * (size_t)i;
		if (!tiff.Has(nEntry, 12)) break;
		WORD nTag = (WORD)tiff.U16(nEntry);
		unsigned nType = tiff.U16(nEntry + 2), nCount = tiff.U32(nEntry + 4);
		if (nIfd == IFD_MAIN && (nTag == 0x8769 || nTag == 0x8825)) {
			ReadExifIfd(tiff, tiff.U32(nEntry + 8), nTag == 0x8825 ? IFD_GPS : IFD_MAIN, values, nDepth + 1);
			continue;
		}
		size_t nTotal = 0;
		size_t nPos = tiff.ValuePos(nEntry, nTotal);
		if (nPos == SIZE_MAX) continue;
		CExifValue value;
		value.Ifd = nIfd;
		value.Tag = nTag;
		value.IsText = nType == 2;
		if (value.IsText) {
			CStringA sText((const char*)tiff.Data() + nPos, (int)strnlen((const char*)tiff.Data() + nPos, nTotal));
			value.Text = CA2W(sText, CP_UTF8);
			value.Text.Trim();
		} else {
			for (unsigned n = 0; n < nCount && n < 16; n++) {
				size_t p = nPos + n * CTiff::TypeSize(nType);
				switch (nType) {
					case 1: case 7: value.Numbers.push_back(tiff.Data()[p]); break;
					case 6: value.Numbers.push_back((signed char)tiff.Data()[p]); break;
					case 3: value.Numbers.push_back(tiff.U16(p)); break;
					case 8: value.Numbers.push_back((short)tiff.U16(p)); break;
					case 4: value.Numbers.push_back(tiff.U32(p)); break;
					case 9: value.Numbers.push_back((int)tiff.U32(p)); break;
					case 5: if (tiff.U32(p + 4) != 0) value.Numbers.push_back((double)tiff.U32(p) / tiff.U32(p + 4)); break;
					case 10: if (tiff.U32(p + 4) != 0) value.Numbers.push_back((double)(int)tiff.U32(p) / (int)tiff.U32(p + 4)); break;
				}
			}
		}
		values.push_back(value);
	}
}

static bool ReadAt(HANDLE hFile, unsigned __int64 nPos, void* pBuffer, DWORD nSize) {
	LARGE_INTEGER pos;
	pos.QuadPart = (LONGLONG)nPos;
	DWORD nRead = 0;
	return ::SetFilePointerEx(hFile, pos, NULL, FILE_BEGIN) && ::ReadFile(hFile, pBuffer, nSize, &nRead, NULL) && nRead == nSize;
}

static unsigned BigEndian32(const unsigned char* p) { return ((unsigned)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

// Finds the EXIF block (TIFF header) in a PNG or WebP file: file position and size, false if there is none
static bool FindEmbeddedExif(HANDLE hFile, unsigned __int64& nPos, DWORD& nSize, bool& bPng) {
	LARGE_INTEGER fileSize;
	if (!::GetFileSizeEx(hFile, &fileSize)) return false;
	unsigned __int64 nFileSize = (unsigned __int64)fileSize.QuadPart;
	unsigned char header[12];
	if (!ReadAt(hFile, 0, header, 12)) return false;
	static const unsigned char pngSignature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
	if (memcmp(header, pngSignature, 8) == 0) {
		bPng = true;
		unsigned __int64 nChunk = 8;
		for (int i = 0; i < 100000 && nChunk + 12 <= nFileSize; i++) {
			unsigned char chunk[8];
			if (!ReadAt(hFile, nChunk, chunk, 8)) return false;
			unsigned nLength = BigEndian32(chunk);
			if (memcmp(chunk + 4, "eXIf", 4) == 0) {
				if (nLength < 8 || nLength > 16 * 1024 * 1024 || nChunk + 8 + nLength > nFileSize) return false;
				nPos = nChunk + 8;
				nSize = nLength;
				return true;
			}
			if (memcmp(chunk + 4, "IEND", 4) == 0) return false;
			nChunk += 12 + (unsigned __int64)nLength;
		}
		return false;
	}
	if (memcmp(header, "RIFF", 4) == 0 && memcmp(header + 8, "WEBP", 4) == 0) {
		bPng = false;
		unsigned __int64 nChunk = 12;
		for (int i = 0; i < 10000 && nChunk + 8 <= nFileSize; i++) {
			unsigned char chunk[8];
			if (!ReadAt(hFile, nChunk, chunk, 8)) return false;
			unsigned nLength = chunk[4] | (chunk[5] << 8) | (chunk[6] << 16) | ((unsigned)chunk[7] << 24);
			if (memcmp(chunk, "EXIF", 4) == 0) {
				if (nLength < 8 || nLength > 16 * 1024 * 1024 || nChunk + 8 + nLength > nFileSize) return false;
				nPos = nChunk + 8;
				nSize = nLength;
				unsigned char prefix[6];
				if (ReadAt(hFile, nPos, prefix, 6) && memcmp(prefix, "Exif\0\0", 6) == 0) {
					nPos += 6;
					nSize -= 6;
				}
				return true;
			}
			nChunk += 8 + (unsigned __int64)nLength + (nLength & 1);
		}
	}
	return false;
}

static bool ReadEmbeddedExif(LPCTSTR sFileName, std::vector<unsigned char>& tiff) {
	HANDLE hFile = ::CreateFile(sFileName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE) return false;
	unsigned __int64 nPos = 0;
	DWORD nSize = 0;
	bool bPng = false;
	bool bOK = FindEmbeddedExif(hFile, nPos, nSize, bPng);
	if (bOK) {
		tiff.resize(nSize);
		bOK = ReadAt(hFile, nPos, &tiff[0], nSize);
	}
	::CloseHandle(hFile);
	return bOK;
}

static bool ParseExifDate(const CString& sText, SYSTEMTIME& time) {
	memset(&time, 0, sizeof(time));
	int nYear, nMonth, nDay, nHour = 0, nMinute = 0, nSecond = 0;
	if (_stscanf_s(sText, _T("%d:%d:%d %d:%d:%d"), &nYear, &nMonth, &nDay, &nHour, &nMinute, &nSecond) < 3) return false;
	if (nYear < 1601 || nMonth < 1 || nMonth > 12 || nDay < 1 || nDay > 31) return false;
	time.wYear = (WORD)nYear; time.wMonth = (WORD)nMonth; time.wDay = (WORD)nDay;
	time.wHour = (WORD)nHour; time.wMinute = (WORD)nMinute; time.wSecond = (WORD)nSecond;
	return true;
}

// Values of the EXIF block that are not in the list of Windows yet
static void AddEmbeddedExif(LPCTSTR sFileName, std::vector<std::pair<PROPERTYKEY, PROPVARIANT>>& values, std::vector<PROPERTYKEY>& embeddedKeys) {
	std::vector<unsigned char> data;
	if (!ReadEmbeddedExif(sFileName, data)) return;
	CTiff tiff(&data[0], data.size());
	if (!tiff.Valid()) return;
	std::vector<CExifValue> exif;
	ReadExifIfd(tiff, tiff.U32(4), IFD_MAIN, exif, 0);

	auto Find = [&](int nIfd, WORD nTag) -> const CExifValue* {
		for (const CExifValue& value : exif) if (value.Ifd == nIfd && value.Tag == nTag) return &value;
		return NULL;
	};
	auto Add = [&](const PROPERTYKEY& key, PROPVARIANT& value) {
		for (auto& existing : values) {
			if (IsEqualPropertyKey(existing.first, key)) {
				::PropVariantClear(&value);
				return;
			}
		}
		values.push_back({ key, value });
		embeddedKeys.push_back(key);
	};
	for (const auto& mapping : s_exifTags) {
		const CExifValue* pValue = Find(mapping.Ifd, mapping.Tag);
		if (pValue == NULL) continue;
		PROPVARIANT value;
		::PropVariantInit(&value);
		HRESULT hr = E_FAIL;
		if (IsEqualPropertyKey(*mapping.Key, PKEY_Photo_DateTaken)) {
			// local time of the camera, with the time zone if the camera wrote it
			SYSTEMTIME local, utc;
			FILETIME fileTime;
			if (pValue->IsText && ParseExifDate(pValue->Text, local)) {
				const CExifValue* pOffset = Find(IFD_MAIN, 0x9011);
				int nSign = 1, nHours = 0, nMinutes = 0;
				bool bOffset = pOffset != NULL && pOffset->IsText && pOffset->Text.GetLength() >= 6 &&
					_stscanf_s(pOffset->Text.Mid(1), _T("%d:%d"), &nHours, &nMinutes) == 2;
				if (bOffset) nSign = pOffset->Text[0] == _T('-') ? -1 : 1;
				if (bOffset && ::SystemTimeToFileTime(&local, &fileTime)) {
					ULARGE_INTEGER n;
					n.LowPart = fileTime.dwLowDateTime;
					n.HighPart = fileTime.dwHighDateTime;
					n.QuadPart -= (ULONGLONG)(nSign * (nHours * 60 + nMinutes)) * 60 * 10000000;
					fileTime.dwLowDateTime = n.LowPart;
					fileTime.dwHighDateTime = n.HighPart;
					hr = ::InitPropVariantFromFileTime(&fileTime, &value);
				} else if (::TzSpecificLocalTimeToSystemTime(NULL, &local, &utc) && ::SystemTimeToFileTime(&utc, &fileTime)) {
					hr = ::InitPropVariantFromFileTime(&fileTime, &value);
				}
			}
		} else {
			VARTYPE vt = GetPropertyType(*mapping.Key);
			if (pValue->IsText) {
				LPCWSTR texts[1] = { pValue->Text };
				if (pValue->Text.IsEmpty()) hr = E_FAIL;
				else if (vt == (VT_VECTOR | VT_LPWSTR)) hr = ::InitPropVariantFromStringVector(texts, 1, &value);
				else if (vt == VT_LPWSTR) hr = ::InitPropVariantFromString(pValue->Text, &value);
			} else if (!pValue->Numbers.empty()) {
				if (vt == (VT_VECTOR | VT_R8)) {
					hr = ::InitPropVariantFromDoubleVector(&pValue->Numbers[0], (ULONG)pValue->Numbers.size(), &value);
				} else if ((vt & VT_VECTOR) == 0) {
					PROPVARIANT number;
					::InitPropVariantFromDouble(pValue->Numbers[0], &number);
					hr = ::PropVariantChangeType(&value, number, PVCHF_DEFAULT, vt);
					::PropVariantClear(&number);
				}
			}
		}
		if (SUCCEEDED(hr) && value.vt != VT_EMPTY) Add(*mapping.Key, value);
		else ::PropVariantClear(&value);
	}
	// GPS time stamp (UTC)
	const CExifValue* pDate = Find(IFD_GPS, 29);
	const CExifValue* pTime = Find(IFD_GPS, 7);
	SYSTEMTIME gpsTime;
	if (pDate != NULL && pDate->IsText && ParseExifDate(pDate->Text, gpsTime)) {
		if (pTime != NULL && pTime->Numbers.size() == 3) {
			gpsTime.wHour = (WORD)pTime->Numbers[0];
			gpsTime.wMinute = (WORD)pTime->Numbers[1];
			gpsTime.wSecond = (WORD)pTime->Numbers[2];
		}
		FILETIME fileTime;
		PROPVARIANT value;
		if (::SystemTimeToFileTime(&gpsTime, &fileTime) && SUCCEEDED(::InitPropVariantFromFileTime(&fileTime, &value))) Add(PKEY_GPS_Date, value);
	}
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
	std::vector<PROPERTYKEY> embeddedKeys;
	AddEmbeddedExif(sFileName, values, embeddedKeys);

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
			for (const auto& fallback : s_fallbackNames) {
				if (sCanonical == fallback.Canonical) entry.Name = CNLS::GetString(fallback.Name);
			}
		}
		if (pText != NULL) ::CoTaskMemFree(pText);
		entry.Group = GetGroup(sCanonical);
		entry.Order = GetOrder(sCanonical);
		entry.Embedded = false;
		for (const PROPERTYKEY& embeddedKey : embeddedKeys) {
			if (IsEqualPropertyKey(embeddedKey, key)) entry.Embedded = true;
		}
		// Windows can not write the values it does not read
		entry.Writable = !entry.Embedded && IsWritable(key, pDescription);
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

	double dLatitude = 0, dLongitude = 0;
	bool bLatitude = false, bLongitude = false, bSouth = false, bWest = false, bEmbeddedLocation = false;
	for (auto& value : values) {
		const PROPVARIANT& v = value.second;
		if (v.vt == (VT_VECTOR | VT_R8) && v.cadbl.cElems == 3) {
			double dDecimal = v.cadbl.pElems[0] + v.cadbl.pElems[1] / 60 + v.cadbl.pElems[2] / 3600;
			if (IsEqualPropertyKey(value.first, PKEY_GPS_Latitude)) { dLatitude = dDecimal; bLatitude = true; }
			if (IsEqualPropertyKey(value.first, PKEY_GPS_Longitude)) { dLongitude = dDecimal; bLongitude = true; }
		}
		if (v.vt == VT_LPWSTR && v.pwszVal != NULL) {
			if (IsEqualPropertyKey(value.first, PKEY_GPS_LatitudeRef)) bSouth = v.pwszVal[0] == L'S' || v.pwszVal[0] == L's';
			if (IsEqualPropertyKey(value.first, PKEY_GPS_LongitudeRef)) bWest = v.pwszVal[0] == L'W' || v.pwszVal[0] == L'w';
		}
		for (const PROPERTYKEY& embeddedKey : embeddedKeys) {
			if (IsEqualPropertyKey(embeddedKey, value.first) && IsEqualPropertyKey(value.first, PKEY_GPS_Latitude)) bEmbeddedLocation = true;
		}
	}
	for (auto& value : values) {
		AddEntry(value.first, &value.second);
		::PropVariantClear(&value.second);
	}
	if (bLatitude && bLongitude) {
		CEntry coordinates;
		coordinates.Key = PKEY_GPS_Latitude;
		coordinates.Name = CNLS::GetString(_T("Coordinates"));
		coordinates.Value.Format(_T("%.6f, %.6f"), bSouth ? -dLatitude : dLatitude, bWest ? -dLongitude : dLongitude);
		coordinates.EditValue = coordinates.Value;
		coordinates.Group = GROUP_LOCATION;
		coordinates.Order = -1;
		coordinates.Writable = false;
		coordinates.Present = true;
		coordinates.Embedded = bEmbeddedLocation;
		entries.push_back(coordinates);
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

// Offset after the EOI marker of the first image of a JPEG file, -1 if not a JPEG or not found
static __int64 GetEndOfFirstJpegImage(const std::vector<char>& data) {
	const unsigned char* d = (const unsigned char*)(data.empty() ? NULL : &data[0]);
	size_t n = data.size();
	if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return -1;
	size_t pos = 2;
	while (pos + 1 < n) {
		if (d[pos] != 0xFF) return -1;
		while (pos < n && d[pos] == 0xFF) pos++;
		if (pos >= n) return -1;
		unsigned char marker = d[pos++];
		if (marker == 0xD9) return (__int64)pos;
		if ((marker >= 0xD0 && marker <= 0xD7) || marker == 0x01 || marker == 0xD8) continue;
		if (pos + 2 > n) return -1;
		size_t length = ((size_t)d[pos] << 8) | d[pos + 1];
		pos += length;
		if (marker == 0xDA) {
			// entropy coded data up to the next marker
			while (pos + 1 < n) {
				if (d[pos] == 0xFF) {
					unsigned char next = d[pos + 1];
					if (next == 0x00 || (next >= 0xD0 && next <= 0xD7)) { pos += 2; continue; }
					if (next == 0xFF) { pos++; continue; }
					break;
				}
				pos++;
			}
		}
	}
	return -1;
}

static bool ReadFileBytes(LPCTSTR sFileName, std::vector<char>& data) {
	HANDLE hFile = ::CreateFile(sFileName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE) return false;
	LARGE_INTEGER size;
	bool bOK = ::GetFileSizeEx(hFile, &size) && size.QuadPart < 512 * 1024 * 1024;
	if (bOK) {
		data.resize((size_t)size.QuadPart);
		DWORD nRead = 0;
		bOK = data.empty() || (::ReadFile(hFile, &data[0], (DWORD)data.size(), &nRead, NULL) && nRead == data.size());
	}
	::CloseHandle(hFile);
	return bOK;
}

// Multi picture format (APP2 "MPF"): the image list has the size of the first image and the offsets of the others,
// relative to the MP header. Returns false if there is none.
struct CMpfEntries {
	size_t Base;          // position of the MP header (TIFF header) in the file
	size_t EntriesPos;    // position of the first 16 byte entry
	int Count;
	bool LittleEndian;
};

static unsigned int ReadUInt(const char* p, bool bLittle, int nBytes) {
	const unsigned char* d = (const unsigned char*)p;
	unsigned int n = 0;
	for (int i = 0; i < nBytes; i++) n |= (unsigned int)d[bLittle ? i : nBytes - 1 - i] << (8 * i);
	return n;
}

static void WriteUInt(char* p, unsigned int n, bool bLittle) {
	for (int i = 0; i < 4; i++) p[bLittle ? i : 3 - i] = (char)((n >> (8 * i)) & 0xFF);
}

static bool FindMpf(const std::vector<char>& data, __int64 nEnd, CMpfEntries& mpf) {
	size_t nLimit = (size_t)min((__int64)data.size(), nEnd > 0 ? nEnd : (__int64)data.size());
	static const char signature[4] = { 'M', 'P', 'F', 0 };
	for (size_t i = 0; i + 16 < nLimit && i < 256 * 1024; i++) {
		if (memcmp(&data[i], signature, 4) != 0) continue;
		size_t nBase = i + 4;
		bool bLittle = data[nBase] == 'I';
		unsigned int nIfd = ReadUInt(&data[nBase + 4], bLittle, 4);
		if (nBase + nIfd + 2 > data.size()) return false;
		int nTags = (int)ReadUInt(&data[nBase + nIfd], bLittle, 2);
		int nCount = 0;
		unsigned int nEntries = 0;
		for (int t = 0; t < nTags; t++) {
			size_t e = nBase + nIfd + 2 + 12 * t;
			if (e + 12 > data.size()) return false;
			unsigned int nTag = ReadUInt(&data[e], bLittle, 2);
			unsigned int nValue = ReadUInt(&data[e + 8], bLittle, 4);
			if (nTag == 0xB001) nCount = (int)nValue;
			if (nTag == 0xB002) nEntries = nValue;
		}
		if (nCount < 2 || nEntries == 0 || nBase + nEntries + 16 * nCount > data.size()) return false;
		mpf.Base = nBase;
		mpf.EntriesPos = nBase + nEntries;
		mpf.Count = nCount;
		mpf.LittleEndian = bLittle;
		return true;
	}
	return false;
}

CJpegTrailerGuard::CJpegTrailerGuard(LPCTSTR sFileName) : m_sFileName(sFileName) {
	std::vector<char> data;
	if (ReadFileBytes(sFileName, data)) {
		__int64 nEnd = GetEndOfFirstJpegImage(data);
		if (nEnd > 0 && nEnd < (__int64)data.size()) {
			m_trailer.assign(data.begin() + (size_t)nEnd, data.end());
			CMpfEntries mpf;
			if (FindMpf(data, nEnd, mpf)) {
				for (int k = 1; k < mpf.Count; k++) {
					__int64 nOffset = ReadUInt(&data[mpf.EntriesPos + 16 * k + 8], mpf.LittleEndian, 4);
					__int64 nInTrailer = (__int64)mpf.Base + nOffset - nEnd;
					m_trailerOffsets.push_back((nOffset != 0 && nInTrailer >= 0 && nInTrailer < (__int64)m_trailer.size()) ? nInTrailer : -1);
				}
			}
		}
	}
}

bool CJpegTrailerGuard::Restore() {
	if (m_trailer.empty()) return true;
	std::vector<char> data;
	if (!ReadFileBytes(m_sFileName, data)) return false;
	__int64 nEnd = GetEndOfFirstJpegImage(data);
	if (nEnd <= 0) return true; // not a JPEG anymore
	if (nEnd == (__int64)data.size()) {
		data.insert(data.end(), m_trailer.begin(), m_trailer.end());
	} else if (data.size() - (size_t)nEnd != m_trailer.size() || memcmp(&data[(size_t)nEnd], &m_trailer[0], m_trailer.size()) != 0) {
		return true; // other data than before, nothing is changed
	}
	// the first image changed its size (new EXIF data): the image list of MPF must point to the moved images
	CMpfEntries mpf;
	if (FindMpf(data, nEnd, mpf) && mpf.Count - 1 == (int)m_trailerOffsets.size()) {
		WriteUInt(&data[mpf.EntriesPos + 4], (unsigned int)nEnd, mpf.LittleEndian);
		for (int k = 1; k < mpf.Count; k++) {
			if (m_trailerOffsets[k - 1] >= 0) {
				WriteUInt(&data[mpf.EntriesPos + 16 * k + 8], (unsigned int)(nEnd + m_trailerOffsets[k - 1] - (__int64)mpf.Base), mpf.LittleEndian);
			}
		}
	}
	HANDLE hFile = ::CreateFile(m_sFileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE) return false;
	DWORD nWritten = 0;
	bool bOK = ::WriteFile(hFile, &data[0], (DWORD)data.size(), &nWritten, NULL) && nWritten == data.size() && ::SetEndOfFile(hFile);
	::CloseHandle(hFile);
	return bOK;
}

static bool WriteValues(LPCTSTR sFileName, const std::vector<std::pair<PROPERTYKEY, PROPVARIANT*>>& values, int& nWritten) {
	nWritten = 0;
	CJpegTrailerGuard trailerGuard(sFileName);
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
	if (bOK) trailerGuard.Restore();
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
	if (IsEqualPropertyKey(entry.Key, PKEY_Title) || IsEqualPropertyKey(entry.Key, PKEY_Subject)) {
		sHint = CString(CNLS::GetString(_T("In JPEG files Windows stores title and subject in the same field, both change."))) + _T("\n") + sHint;
	}
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

///////////////////////////////////////////////////////////////////////////////////
// Removing the location from the EXIF block of PNG and WebP files
///////////////////////////////////////////////////////////////////////////////////

static unsigned Crc32(const unsigned char* p, size_t n, unsigned nCrc = 0xFFFFFFFF) {
	static unsigned table[256];
	static bool bInitialized = false;
	if (!bInitialized) {
		for (unsigned i = 0; i < 256; i++) {
			unsigned c = i;
			for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320 ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
		bInitialized = true;
	}
	for (size_t i = 0; i < n; i++) nCrc = table[(nCrc ^ p[i]) & 0xFF] ^ (nCrc >> 8);
	return nCrc;
}

// Empties the GPS directory of the TIFF block in place (the size does not change), returns the number of removed values
static int StripGps(unsigned char* d, size_t n) {
	CTiff tiff(d, n);
	if (!tiff.Valid()) return 0;
	size_t nIfd0 = tiff.U32(4);
	if (!tiff.Has(nIfd0, 2)) return 0;
	unsigned nEntries = tiff.U16(nIfd0);
	for (unsigned i = 0; i < nEntries && i < 1000; i++) {
		size_t nEntry = nIfd0 + 2 + 12 * (size_t)i;
		if (!tiff.Has(nEntry, 12)) break;
		if (tiff.U16(nEntry) != 0x8825) continue;
		size_t nGps = tiff.U32(nEntry + 8);
		if (!tiff.Has(nGps, 2)) return 0;
		unsigned nGpsEntries = tiff.U16(nGps);
		if (!tiff.Has(nGps + 2, 12 * (size_t)nGpsEntries + 4)) return 0;
		for (unsigned k = 0; k < nGpsEntries; k++) {
			size_t nTotal = 0;
			size_t nPos = tiff.ValuePos(nGps + 2 + 12 * (size_t)k, nTotal);
			if (nPos != SIZE_MAX && nTotal > 4) memset(d + nPos, 0, nTotal);
		}
		// an empty directory, the pointer to it stays valid
		memset(d + nGps, 0, 2 + 12 * (size_t)nGpsEntries + 4);
		return (int)nGpsEntries;
	}
	return 0;
}

// Removes the GPS values of the EXIF block of a PNG or WebP file, also the "exif:GPS..." text chunks of PNG files (ImageMagick).
// Returns the number of removed values, -1 on error.
static int RemoveEmbeddedLocation(LPCTSTR sFileName) {
	std::vector<char> data;
	if (!ReadFileData(sFileName, data) || data.size() < 12) return -1;
	unsigned char* d = (unsigned char*)&data[0];
	size_t n = data.size();
	int nRemoved = 0, nRemovedTexts = 0;
	static const unsigned char pngSignature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
	if (memcmp(d, pngSignature, 8) == 0) {
		std::vector<char> result(data.begin(), data.begin() + 8);
		size_t nChunk = 8;
		while (nChunk + 12 <= n) {
			size_t nLength = BigEndian32(d + nChunk);
			if (nLength > n - nChunk - 12) return -1;
			unsigned char* pType = d + nChunk + 4;
			unsigned char* pData = d + nChunk + 8;
			bool bSkip = false;
			if (memcmp(pType, "tEXt", 4) == 0 || memcmp(pType, "zTXt", 4) == 0 || memcmp(pType, "iTXt", 4) == 0) {
				bSkip = nLength >= 8 && memcmp(pData, "exif:GPS", 8) == 0;
				if (bSkip) nRemovedTexts++;
			} else if (memcmp(pType, "eXIf", 4) == 0) {
				int nGps = StripGps(pData, nLength);
				if (nGps > 0) {
					nRemoved += nGps;
					unsigned nCrc = Crc32(pType, 4 + nLength) ^ 0xFFFFFFFF;
					unsigned char* pCrc = pData + nLength;
					pCrc[0] = (unsigned char)(nCrc >> 24); pCrc[1] = (unsigned char)(nCrc >> 16);
					pCrc[2] = (unsigned char)(nCrc >> 8); pCrc[3] = (unsigned char)nCrc;
				}
			}
			if (!bSkip) result.insert(result.end(), data.begin() + nChunk, data.begin() + nChunk + 12 + nLength);
			bool bEnd = memcmp(pType, "IEND", 4) == 0;
			nChunk += 12 + nLength;
			if (bEnd) break;
		}
		if (nChunk < n) result.insert(result.end(), data.begin() + nChunk, data.end());
		if (nRemoved == 0) nRemoved = nRemovedTexts;
		if (nRemoved == 0) return 0;
		return WriteFileData(sFileName, result) ? nRemoved : -1;
	}
	if (memcmp(d, "RIFF", 4) == 0 && memcmp(d + 8, "WEBP", 4) == 0) {
		size_t nChunk = 12;
		while (nChunk + 8 <= n) {
			size_t nLength = d[nChunk + 4] | (d[nChunk + 5] << 8) | (d[nChunk + 6] << 16) | ((size_t)d[nChunk + 7] << 24);
			if (nLength > n - nChunk - 8) return -1;
			if (memcmp(d + nChunk, "EXIF", 4) == 0) {
				size_t nStart = nChunk + 8;
				if (nLength >= 6 && memcmp(d + nStart, "Exif\0\0", 6) == 0) nStart += 6;
				nRemoved += StripGps(d + nStart, nChunk + 8 + nLength - nStart);
			}
			nChunk += 8 + nLength + (nLength & 1);
		}
		if (nRemoved == 0) return 0;
		return WriteFileData(sFileName, data) ? nRemoved : -1;
	}
	return 0;
}

bool ReadFileData(LPCTSTR sFileName, std::vector<char>& data) {
	return ReadFileBytes(sFileName, data);
}

bool WriteFileData(LPCTSTR sFileName, const std::vector<char>& data) {
	// written to a new file first, ReplaceFile keeps the creation time, attributes and security of the old one
	CString sTemp = CString(sFileName) + _T(".jpegview-tmp");
	HANDLE hFile = ::CreateFile(sTemp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) return false;
	DWORD nWritten = 0;
	bool bOK = data.empty() || (::WriteFile(hFile, &data[0], (DWORD)data.size(), &nWritten, NULL) && nWritten == data.size());
	bOK = ::FlushFileBuffers(hFile) && bOK;
	::CloseHandle(hFile);
	if (bOK) {
		bOK = ::ReplaceFile(sFileName, sTemp, NULL, REPLACEFILE_IGNORE_MERGE_ERRORS, NULL, NULL) != FALSE;
	}
	if (!bOK) ::DeleteFile(sTemp);
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
	int nWritten = 0;
	if (!values.empty() && !WriteValues(sFileName, values, nWritten)) {
		sError = CNLS::GetString(_T("The value could not be written to the file."));
		return -1;
	}
	// the location in the EXIF block of PNG and WebP files, Windows can not remove it
	bool bEmbeddedLocation = false;
	for (const CEntry& entry : entries) {
		if (entry.Embedded && entry.Present && entry.Group == GROUP_LOCATION) bEmbeddedLocation = true;
	}
	if (bEmbeddedLocation) {
		int nRemoved = RemoveEmbeddedLocation(sFileName);
		if (nRemoved < 0) {
			sError = CNLS::GetString(_T("The value could not be written to the file."));
			return -1;
		}
		nWritten += nRemoved;
	}
	return nWritten;
}

bool WriteTexts(LPCTSTR sFileName, const std::vector<std::pair<PROPERTYKEY, CString>>& texts) {
	std::vector<PROPVARIANT> values(texts.size());
	std::vector<std::pair<PROPERTYKEY, PROPVARIANT*>> pointers;
	for (size_t i = 0; i < texts.size(); i++) {
		::PropVariantInit(&values[i]);
		if (texts[i].second.IsEmpty() || ParseValue(texts[i].first, texts[i].second, values[i])) {
			pointers.push_back({ texts[i].first, &values[i] });
		}
	}
	int nWritten = 0;
	bool bOK = !pointers.empty() && WriteValues(sFileName, pointers, nWritten);
	for (PROPVARIANT& value : values) ::PropVariantClear(&value);
	return bOK;
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
