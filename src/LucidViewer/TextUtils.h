#pragma once

// Text helpers without Windows UI dependencies (only ATL CString), used by the program and by the unit tests
// (tests/UnitTests). Header only.

#include <atlstr.h>
#include <vector>

namespace TextUtils {

	// Compares two versions 'major.minor.patch[.build]' (a leading 'v' is ignored), returns <0, 0 or >0
	inline int CompareVersions(LPCTSTR sVersion1, LPCTSTR sVersion2) {
		CString s1(sVersion1), s2(sVersion2);
		s1.TrimLeft(_T("vV"));
		s2.TrimLeft(_T("vV"));
		int nPos1 = 0, nPos2 = 0;
		for (int nPart = 0; nPart < 4; nPart++) {
			CString sPart1 = (nPos1 >= 0) ? s1.Tokenize(_T("."), nPos1) : CString();
			CString sPart2 = (nPos2 >= 0) ? s2.Tokenize(_T("."), nPos2) : CString();
			int n1 = _ttoi(sPart1), n2 = _ttoi(sPart2);
			if (n1 != n2) return (n1 < n2) ? -1 : 1;
		}
		return 0;
	}

	// The tags of a list "a; b,c" in their order, trimmed, without empty ones and doubles (case is ignored)
	inline std::vector<CString> SplitTags(const CString& sText) {
		CString sInput(sText);
		sInput.Replace(_T(','), _T(';'));
		std::vector<CString> tags;
		int nStart = 0;
		CString sTag = sInput.Tokenize(_T(";"), nStart);
		while (nStart >= 0) {
			sTag.Trim();
			bool bDouble = false;
			for (const CString& sExisting : tags) bDouble = bDouble || sExisting.CompareNoCase(sTag) == 0;
			if (!sTag.IsEmpty() && !bDouble) tags.push_back(sTag);
			sTag = sInput.Tokenize(_T(";"), nStart);
		}
		return tags;
	}

	inline CString JoinTags(const std::vector<CString>& tags) {
		CString sResult;
		for (const CString& sTag : tags) {
			if (!sResult.IsEmpty()) sResult += _T("; ");
			sResult += sTag;
		}
		return sResult;
	}

	// "a,b ; c;;a" -> "a; b; c"
	inline CString NormalizeTags(const CString& sText) {
		return JoinTags(SplitTags(sText));
	}

	// True if the list of tags has the tag (case is ignored)
	inline bool HasTag(const CString& sTags, LPCTSTR sTag) {
		CString sSearched(sTag);
		sSearched.Trim();
		for (const CString& s : SplitTags(sTags)) {
			if (s.CompareNoCase(sSearched) == 0) return true;
		}
		return false;
	}

	// The tags of sTags without the ones of sRemove
	inline CString RemoveTags(const CString& sTags, const CString& sRemove) {
		std::vector<CString> removed = SplitTags(sRemove), kept;
		for (const CString& sTag : SplitTags(sTags)) {
			bool bRemove = false;
			for (const CString& s : removed) bRemove = bRemove || s.CompareNoCase(sTag) == 0;
			if (!bRemove) kept.push_back(sTag);
		}
		return JoinTags(kept);
	}

	// True if the text has the command name as a whole word: IDM_UNDO is not found in IDM_UNDO_TRANSPARENCY
	inline bool ContainsCommandName(const CString& sText, const CString& sCommand) {
		if (sCommand.IsEmpty()) return false;
		auto IsNamePart = [](TCHAR c) { return _istalnum(c) || c == _T('_'); };
		int nPos = 0;
		while ((nPos = sText.Find(sCommand, nPos)) >= 0) {
			int nEnd = nPos + sCommand.GetLength();
			TCHAR cBefore = (nPos > 0) ? sText[nPos - 1] : _T(' ');
			TCHAR cAfter = (nEnd < sText.GetLength()) ? sText[nEnd] : _T(' ');
			if (!IsNamePart(cBefore) && !IsNamePart(cAfter)) return true;
			nPos = nEnd;
		}
		return false;
	}

	// New file name (without folder and ending) of the batch rename: {name} old name, {n} number with nDigits digits,
	// {date} and {time} as given. Characters not allowed in file names become '_', an empty result is the old name.
	inline CString ExpandRenamePattern(const CString& sPattern, const CString& sName, int nNumber, int nDigits, const CString& sDate, const CString& sTime) {
		CString sNumber;
		sNumber.Format(_T("%0*d"), nDigits, nNumber);
		CString sNew = sPattern;
		sNew.Replace(_T("{name}"), sName);
		sNew.Replace(_T("{n}"), sNumber);
		sNew.Replace(_T("{date}"), sDate);
		sNew.Replace(_T("{time}"), sTime);
		for (LPCTSTR p = _T("\\/:*?\"<>|"); *p != 0; p++) sNew.Replace(*p, _T('_'));
		sNew.Trim();
		return sNew.IsEmpty() ? sName : sNew;
	}

	// Number of digits of the numbers of a batch rename of nCount files, at least 2
	inline int RenameDigits(size_t nCount) {
		int nDigits = 2;
		for (size_t n = nCount; n >= 100; n /= 10) nDigits++;
		return nDigits;
	}

	// MIME type of a data URI by the file ending ('jpg' -> 'image/jpeg')
	inline CString ImageMimeType(const CString& sEnding) {
		CString s(sEnding);
		s.TrimLeft(_T('.'));
		s.MakeLower();
		if (s == _T("jpg") || s == _T("jpe") || s == _T("jfif")) s = _T("jpeg");
		if (s == _T("tif")) s = _T("tiff");
		if (s == _T("ico")) s = _T("x-icon");
		if (s == _T("svg")) s = _T("svg+xml");
		return CString(_T("image/")) + s;
	}

	// Parsed text of the photo search: words of the name, tag:x, stars:n, from:yyyy-mm-dd, to:yyyy-mm-dd
	struct CSearchQuery {
		std::vector<CString> Words; // lower case
		std::vector<CString> Tags;
		int MinStars = 0;
		CString From, To; // yyyy-mm-dd, empty if not given
	};

	inline CSearchQuery ParseSearchQuery(const CString& sQuery) {
		CSearchQuery query;
		int nStart = 0;
		CString sToken = sQuery.Tokenize(_T(" "), nStart);
		while (nStart >= 0) {
			CString sLower = sToken;
			sLower.MakeLower();
			if (sLower.Left(4) == _T("tag:")) { if (sToken.GetLength() > 4) query.Tags.push_back(sToken.Mid(4)); }
			else if (sLower.Left(6) == _T("stars:")) query.MinStars = max(0, min(5, _ttoi(sToken.Mid(6))));
			else if (sLower.Left(5) == _T("from:")) query.From = sToken.Mid(5);
			else if (sLower.Left(3) == _T("to:")) query.To = sToken.Mid(3);
			else if (!sToken.IsEmpty()) query.Words.push_back(sLower);
			sToken = sQuery.Tokenize(_T(" "), nStart);
		}
		return query;
	}
}
