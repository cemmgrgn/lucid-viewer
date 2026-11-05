// Unit tests of the text helpers of Lucid Viewer (src/LucidViewer/TextUtils.h).
// Built and run by the CI workflow: msbuild tests\UnitTests\UnitTests.vcxproj, then UnitTests.exe (exit code 0: passed).

#include <atlstr.h>
#include <stdio.h>
#include "../../src/LucidViewer/TextUtils.h"

static int s_nFailed = 0;
static int s_nPassed = 0;

#define CHECK(condition) Check((condition), #condition, __LINE__)
#define CHECK_TEXT(actual, expected) CheckText((actual), (expected), #actual, __LINE__)

static void Check(bool bCondition, const char* sText, int nLine) {
	if (bCondition) {
		s_nPassed++;
	} else {
		s_nFailed++;
		printf("FAILED line %d: %s\n", nLine, sText);
	}
}

static void CheckText(const CString& sActual, LPCTSTR sExpected, const char* sText, int nLine) {
	if (sActual == sExpected) {
		s_nPassed++;
	} else {
		s_nFailed++;
		wprintf(L"FAILED line %d: %S\n    got:      '%s'\n    expected: '%s'\n", nLine, sText, (LPCTSTR)sActual, sExpected);
	}
}

static void TestCompareVersions() {
	CHECK(TextUtils::CompareVersions(_T("1.0.0"), _T("1.0.0")) == 0);
	CHECK(TextUtils::CompareVersions(_T("v1.1.0"), _T("1.0.0")) > 0);
	CHECK(TextUtils::CompareVersions(_T("1.0.0"), _T("1.0.1")) < 0);
	CHECK(TextUtils::CompareVersions(_T("1.10.0"), _T("1.9.9")) > 0); // numbers, not text
	CHECK(TextUtils::CompareVersions(_T("2"), _T("1.99.99")) > 0);
	CHECK(TextUtils::CompareVersions(_T("1.0"), _T("1.0.0")) == 0);
	CHECK(TextUtils::CompareVersions(_T("V2.0.0"), _T("v2.0.0")) == 0);
}

static void TestTags() {
	CHECK_TEXT(TextUtils::NormalizeTags(_T("a,b ; c;;A")), _T("a; b; c"));
	CHECK_TEXT(TextUtils::NormalizeTags(_T("  ")), _T(""));
	CHECK_TEXT(TextUtils::NormalizeTags(_T("Holiday; holiday; Sea")), _T("Holiday; Sea"));
	CHECK(TextUtils::HasTag(_T("Holiday; Sea"), _T("sea")));
	CHECK(TextUtils::HasTag(_T("Holiday; Sea"), _T(" holiday ")));
	CHECK(!TextUtils::HasTag(_T("Holiday; Sea"), _T("Se")));
	CHECK(!TextUtils::HasTag(_T(""), _T("x")));
	CHECK_TEXT(TextUtils::RemoveTags(_T("a; b; c"), _T("B")), _T("a; c"));
	CHECK_TEXT(TextUtils::RemoveTags(_T("a; b; c"), _T("a, c")), _T("b"));
	CHECK_TEXT(TextUtils::RemoveTags(_T("a; b"), _T("x")), _T("a; b"));
}

static void TestCommandNames() {
	CString sKeyMap = _T("Ctrl+Z  IDM_UNDO_TRANSPARENCY\n// no shortcut: IDM_REDO\n");
	CHECK(!TextUtils::ContainsCommandName(sKeyMap, _T("IDM_UNDO")));
	CHECK(TextUtils::ContainsCommandName(sKeyMap, _T("IDM_UNDO_TRANSPARENCY")));
	CHECK(TextUtils::ContainsCommandName(sKeyMap, _T("IDM_REDO")));
	CHECK(!TextUtils::ContainsCommandName(sKeyMap, _T("IDM_RED")));
	CHECK(!TextUtils::ContainsCommandName(sKeyMap, _T("")));
}

static void TestRename() {
	CHECK(TextUtils::RenameDigits(5) == 2);
	CHECK(TextUtils::RenameDigits(99) == 2);
	CHECK(TextUtils::RenameDigits(100) == 3);
	CHECK(TextUtils::RenameDigits(1234) == 4);
	CHECK_TEXT(TextUtils::ExpandRenamePattern(_T("Holiday {n}"), _T("IMG_1"), 7, 2, _T(""), _T("")), _T("Holiday 07"));
	CHECK_TEXT(TextUtils::ExpandRenamePattern(_T("{date} {time} {name}"), _T("IMG_1"), 1, 2, _T("2024-05-01"), _T("10-20-30")),
		_T("2024-05-01 10-20-30 IMG_1"));
	CHECK_TEXT(TextUtils::ExpandRenamePattern(_T("a:b/c"), _T("x"), 1, 2, _T(""), _T("")), _T("a_b_c"));
	CHECK_TEXT(TextUtils::ExpandRenamePattern(_T("   "), _T("old"), 1, 2, _T(""), _T("")), _T("old"));
}

static void TestMimeTypes() {
	CHECK_TEXT(TextUtils::ImageMimeType(_T("JPG")), _T("image/jpeg"));
	CHECK_TEXT(TextUtils::ImageMimeType(_T(".png")), _T("image/png"));
	CHECK_TEXT(TextUtils::ImageMimeType(_T("tif")), _T("image/tiff"));
	CHECK_TEXT(TextUtils::ImageMimeType(_T("ico")), _T("image/x-icon"));
	CHECK_TEXT(TextUtils::ImageMimeType(_T("webp")), _T("image/webp"));
}

static void TestSearchQuery() {
	TextUtils::CSearchQuery query = TextUtils::ParseSearchQuery(_T("Beach tag:Holiday stars:4 from:2024-01-01 to:2024-12-31 sunset"));
	CHECK(query.Words.size() == 2);
	if (query.Words.size() == 2) {
		CHECK_TEXT(query.Words[0], _T("beach"));
		CHECK_TEXT(query.Words[1], _T("sunset"));
	}
	CHECK(query.Tags.size() == 1);
	if (query.Tags.size() == 1) CHECK_TEXT(query.Tags[0], _T("Holiday"));
	CHECK(query.MinStars == 4);
	CHECK_TEXT(query.From, _T("2024-01-01"));
	CHECK_TEXT(query.To, _T("2024-12-31"));
	CHECK(TextUtils::ParseSearchQuery(_T("stars:9")).MinStars == 5);
	CHECK(TextUtils::ParseSearchQuery(_T("tag:")).Tags.empty());
	CHECK(TextUtils::ParseSearchQuery(_T("")).Words.empty());
}

int main() {
	TestCompareVersions();
	TestTags();
	TestCommandNames();
	TestRename();
	TestMimeTypes();
	TestSearchQuery();
	printf("%d passed, %d failed\n", s_nPassed, s_nFailed);
	return s_nFailed == 0 ? 0 : 1;
}
