#include "StdAfx.h"
#include "StartScreen.h"
#include "SettingsProvider.h"
#include "NLS.h"
#include "DarkUI.h"
#include "ScreenCapture.h"
#include <gdiplus.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <map>
#include <set>
#include <mutex>
#include <thread>
#include <atomic>
#include <algorithm>

#pragma comment(lib, "shlwapi.lib")

using namespace Gdiplus;

// The project targets an old _WIN32_IE, the thumbnail interface of Windows Vista is declared here
MIDL_INTERFACE("bcc18b79-ba16-442f-80c4-8a59c30c463b")
IStartScreenImageFactory : public IUnknown {
public:
	virtual HRESULT STDMETHODCALLTYPE GetImage(SIZE size, int nFlags, HBITMAP* phBitmap) = 0;
};
static const int IMAGE_FACTORY_BIGGERSIZEOK = 0x1;

static HRESULT CreateShellItem(LPCWSTR sPath, REFIID riid, void** ppv) {
	typedef HRESULT (STDAPICALLTYPE *CreateItemFn)(PCWSTR, IBindCtx*, REFIID, void**);
	static CreateItemFn createItem = (CreateItemFn)::GetProcAddress(::GetModuleHandle(_T("shell32.dll")), "SHCreateItemFromParsingName");
	return createItem != NULL ? createItem(sPath, NULL, riid, ppv) : E_NOTIMPL;
}

///////////////////////////////////////////////////////////////////////////////////
// Lists
///////////////////////////////////////////////////////////////////////////////////

static const int MAX_RECENT = 40;
static bool s_bTestLists = false;
static std::vector<CString> s_testRecent, s_testPinned, s_testShots;

static CString ListFileName(LPCTSTR sName) {
	CString sINI = CSettingsProvider::This().GetUserINIFileName();
	int nSlash = sINI.ReverseFind(_T('\\'));
	return sINI.Left(nSlash + 1) + sName;
}

// Lines of the list file, recent entries are "<FILETIME>\t<path>"
static std::vector<CString> ReadLines(LPCTSTR sName) {
	std::vector<CString> lines;
	FILE* pFile = NULL;
	if (_tfopen_s(&pFile, ListFileName(sName), _T("r, ccs=UTF-8")) != 0 || pFile == NULL) return lines;
	wchar_t buffer[2048];
	while (fgetws(buffer, 2048, pFile) != NULL) {
		CString sLine(buffer);
		sLine.TrimRight(_T("\r\n"));
		if (!sLine.IsEmpty()) lines.push_back(sLine);
	}
	fclose(pFile);
	return lines;
}

static void WriteLines(LPCTSTR sName, const std::vector<CString>& lines) {
	CString sFile = ListFileName(sName);
	::SHCreateDirectoryEx(NULL, sFile.Left(sFile.ReverseFind(_T('\\'))), NULL);
	FILE* pFile = NULL;
	if (_tfopen_s(&pFile, sFile, _T("w, ccs=UTF-8")) != 0 || pFile == NULL) return;
	for (const CString& sLine : lines) {
		fputws(sLine, pFile);
		fputws(L"\n", pFile);
	}
	fclose(pFile);
}

static CString RecentPath(const CString& sLine, __int64* pTime = NULL) {
	int nTab = sLine.Find(_T('\t'));
	if (pTime != NULL) *pTime = nTab > 0 ? _ttoi64(sLine.Left(nTab)) : 0;
	return nTab > 0 ? sLine.Mid(nTab + 1) : sLine;
}

void CStartScreen::AddRecent(LPCTSTR sFile) {
	if (s_bTestLists || sFile == NULL || sFile[0] == 0) return;
	std::vector<CString> lines = ReadLines(_T("StartRecent.txt"));
	lines.erase(std::remove_if(lines.begin(), lines.end(), [&](const CString& sLine) { return RecentPath(sLine).CompareNoCase(sFile) == 0; }), lines.end());
	FILETIME now;
	::GetSystemTimeAsFileTime(&now);
	CString sLine;
	sLine.Format(_T("%I64d\t%s"), ((__int64)now.dwHighDateTime << 32) | now.dwLowDateTime, sFile);
	lines.insert(lines.begin(), sLine);
	if (lines.size() > MAX_RECENT) lines.resize(MAX_RECENT);
	WriteLines(_T("StartRecent.txt"), lines);
}

bool CStartScreen::IsPinned(LPCTSTR sFile) {
	if (sFile == NULL) return false;
	std::vector<CString> lines = s_bTestLists ? s_testPinned : ReadLines(_T("StartPinned.txt"));
	for (const CString& sLine : lines) if (sLine.CompareNoCase(sFile) == 0) return true;
	return false;
}

void CStartScreen::SetPinned(LPCTSTR sFile, bool bPinned) {
	if (s_bTestLists || sFile == NULL || sFile[0] == 0) return;
	std::vector<CString> lines = ReadLines(_T("StartPinned.txt"));
	lines.erase(std::remove_if(lines.begin(), lines.end(), [&](const CString& sLine) { return sLine.CompareNoCase(sFile) == 0; }), lines.end());
	// the newest first
	if (bPinned) lines.insert(lines.begin(), CString(sFile));
	WriteLines(_T("StartPinned.txt"), lines);
}

void CStartScreen::SetTestLists(const std::vector<CString>& recent, const std::vector<CString>& pinned, const std::vector<CString>& screenshots) {
	s_bTestLists = true;
	s_testRecent = recent;
	s_testPinned = pinned;
	s_testShots = screenshots;
}

// The newest images of the screenshot folder, with the time they were written
static void ReadScreenshots(std::vector<CString>& files, std::vector<__int64>& times) {
	struct CFile { CString Path; __int64 Time; };
	std::vector<CFile> found;
	CString sFolder = ScreenCapture::GetScreenshotFolder();
	WIN32_FIND_DATA findData;
	HANDLE hFind = ::FindFirstFile(sFolder + _T("\\*"), &findData);
	if (hFind != INVALID_HANDLE_VALUE) {
		do {
			if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
			LPCTSTR sExtension = ::PathFindExtension(findData.cFileName);
			if (_tcsicmp(sExtension, _T(".png")) != 0 && _tcsicmp(sExtension, _T(".jpg")) != 0 && _tcsicmp(sExtension, _T(".jpeg")) != 0 &&
				_tcsicmp(sExtension, _T(".webp")) != 0 && _tcsicmp(sExtension, _T(".bmp")) != 0) continue;
			CFile file = { sFolder + _T("\\") + findData.cFileName, ((__int64)findData.ftLastWriteTime.dwHighDateTime << 32) | findData.ftLastWriteTime.dwLowDateTime };
			found.push_back(file);
		} while (::FindNextFile(hFind, &findData));
		::FindClose(hFind);
	}
	std::sort(found.begin(), found.end(), [](const CFile& a, const CFile& b) { return a.Time > b.Time; });
	for (size_t i = 0; i < found.size() && i < 40; i++) {
		files.push_back(found[i].Path);
		times.push_back(found[i].Time);
	}
}

///////////////////////////////////////////////////////////////////////////////////
// Thumbnails, loaded by the shell (thumbnail cache, also HEIC and RAW) in a background thread
///////////////////////////////////////////////////////////////////////////////////

struct CStartScreen::CThumbnails {
	std::mutex Lock;
	std::map<CString, std::shared_ptr<Bitmap>> Images; // NULL: no thumbnail
	std::set<CString> Loading;
	std::atomic<bool> Cancel;
	CThumbnails() : Cancel(false) {}
};

// Copy of the shell bitmap, always top-down (GetDIBits with a negative height): the row order of the
// shell bitmaps is not reliable. Stored premultiplied, the fastest format for drawing with GDI+.
static std::shared_ptr<Bitmap> ToBitmap(HBITMAP hBitmap) {
	BITMAP info;
	if (::GetObject(hBitmap, sizeof(info), &info) == 0 || info.bmWidth <= 0 || info.bmHeight <= 0) return NULL;
	int nWidth = info.bmWidth, nHeight = abs(info.bmHeight);
	BITMAPINFO bmi = { 0 };
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = nWidth;
	bmi.bmiHeader.biHeight = -nHeight;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;
	std::vector<BYTE> pixels((size_t)nWidth * nHeight * 4);
	HDC hDC = ::GetDC(NULL);
	int nLines = ::GetDIBits(hDC, hBitmap, 0, nHeight, &pixels[0], &bmi, DIB_RGB_COLORS);
	::ReleaseDC(NULL, hDC);
	if (nLines != nHeight) return NULL;
	bool bAlpha = false;
	for (size_t i = 3; i < pixels.size(); i += 4) {
		if (pixels[i] != 0) { bAlpha = true; break; }
	}
	if (!bAlpha) {
		for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255; // photos: no alpha channel
	}
	std::shared_ptr<Bitmap> pBitmap(new Bitmap(nWidth, nHeight, PixelFormat32bppPARGB));
	BitmapData data;
	Rect rect(0, 0, nWidth, nHeight);
	if (pBitmap->LockBits(&rect, ImageLockModeWrite, PixelFormat32bppPARGB, &data) != Ok) return NULL;
	for (int y = 0; y < nHeight; y++) {
		memcpy((BYTE*)data.Scan0 + (size_t)y * data.Stride, &pixels[(size_t)y * nWidth * 4], (size_t)nWidth * 4);
	}
	pBitmap->UnlockBits(&data);
	return pBitmap;
}

static const int IMAGE_FACTORY_INCACHEONLY = 0x10;

static std::shared_ptr<Bitmap> GetThumbnail(const CString& sPath, int nSize, bool bCacheOnly) {
	std::shared_ptr<Bitmap> pBitmap;
	IStartScreenImageFactory* pFactory = NULL;
	if (SUCCEEDED(CreateShellItem(sPath, __uuidof(IStartScreenImageFactory), (void**)&pFactory))) {
		HBITMAP hBitmap = NULL;
		SIZE size = { nSize, nSize };
		int nFlags = IMAGE_FACTORY_BIGGERSIZEOK | (bCacheOnly ? IMAGE_FACTORY_INCACHEONLY : 0);
		if (SUCCEEDED(pFactory->GetImage(size, nFlags, &hBitmap)) && hBitmap != NULL) {
			pBitmap = ToBitmap(hBitmap);
			::DeleteObject(hBitmap);
		}
		pFactory->Release();
	}
	return pBitmap;
}

void CStartScreen::LoadThumbnails() {
	std::shared_ptr<std::vector<CString>> pPaths = std::make_shared<std::vector<CString>>();
	{
		std::lock_guard<std::mutex> lock(m_pThumbnails->Lock);
		for (const std::vector<CString>* pList : { &m_recent, &m_shots, &m_pinned }) {
			for (const CString& sPath : *pList) {
				if (m_pThumbnails->Images.count(sPath) == 0 && m_pThumbnails->Loading.count(sPath) == 0) {
					m_pThumbnails->Loading.insert(sPath);
					pPaths->push_back(sPath);
				}
			}
		}
	}
	if (pPaths->empty()) return;
	std::shared_ptr<CThumbnails> pThumbnails = m_pThumbnails;
	HWND hWnd = m_hWnd;
	int nSize = S(320);
	// first all thumbnails from the thumbnail cache of Windows (fast), then the missing ones are made, by several threads
	std::shared_ptr<std::atomic<int>> pNextCached = std::make_shared<std::atomic<int>>(0);
	std::shared_ptr<std::atomic<int>> pNextMade = std::make_shared<std::atomic<int>>(0);
	unsigned nThreads = max(1u, min(4u, std::thread::hardware_concurrency()));
	for (unsigned t = 0; t < nThreads; t++) {
		std::thread([pThumbnails, pPaths, pNextCached, pNextMade, hWnd, nSize]() {
			HRESULT hr = ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
			for (int nPass = 0; nPass < 2; nPass++) {
				std::atomic<int>& next = nPass == 0 ? *pNextCached : *pNextMade;
				for (int i = next++; i < (int)pPaths->size(); i = next++) {
					if (pThumbnails->Cancel) break;
					const CString& sPath = (*pPaths)[i];
					if (nPass == 1) {
						std::lock_guard<std::mutex> lock(pThumbnails->Lock);
						if (pThumbnails->Loading.count(sPath) == 0) continue; // found in the cache
					}
					std::shared_ptr<Bitmap> pBitmap = GetThumbnail(sPath, nSize, nPass == 0);
					if (nPass == 0 && !pBitmap) continue;
					{
						std::lock_guard<std::mutex> lock(pThumbnails->Lock);
						pThumbnails->Images[sPath] = pBitmap;
						pThumbnails->Loading.erase(sPath);
					}
					if (hWnd != NULL) ::InvalidateRect(hWnd, NULL, FALSE);
				}
			}
			if (SUCCEEDED(hr)) ::CoUninitialize();
		}).detach();
	}
}

///////////////////////////////////////////////////////////////////////////////////
// Start screen
///////////////////////////////////////////////////////////////////////////////////

CStartScreen::CStartScreen(HWND hWnd) : m_hWnd(hWnd), m_eHoverKind(HIT_NONE), m_nHoverIndex(-1), m_nScroll(0),
	m_nContentHeight(0), m_nKeyRow(ROW_RECENT), m_clientRect(0, 0, 0, 0), m_sPinShortcut(_T("Ctrl+B")),
	m_nScrollTarget(0), m_nDrag(-1), m_nDragStartMouse(0), m_nDragStartScroll(0) {
	for (int i = 0; i < ROW_COUNT; i++) m_nRowScroll[i] = m_nRowWidth[i] = m_nRowVisible[i] = m_nRowStep[i] = m_nRowTarget[i] = 0;
	HDC hDC = ::GetDC(NULL);
	m_nDpi = ::GetDeviceCaps(hDC, LOGPIXELSX);
	::ReleaseDC(NULL, hDC);
	m_pThumbnails = std::make_shared<CThumbnails>();
}

static CStartScreen* s_pAnimating = NULL;
static UINT_PTR s_nAnimationTimer = 0;

CStartScreen::~CStartScreen() {
	m_pThumbnails->Cancel = true;
	if (s_pAnimating == this) {
		::KillTimer(NULL, s_nAnimationTimer);
		s_pAnimating = NULL;
	}
}

///////////////////////////////////////////////////////////////////////////////////
// Smooth scrolling
///////////////////////////////////////////////////////////////////////////////////

void CStartScreen::SetScrollTarget(int nTarget) {
	m_nScrollTarget = max(0, min(nTarget, m_nContentHeight - m_clientRect.Height()));
	StartAnimation();
}

void CStartScreen::SetRowTarget(int nRow, int nTarget) {
	m_nRowTarget[nRow] = max(0, min(nTarget, m_nRowWidth[nRow] - m_nRowVisible[nRow]));
	StartAnimation();
}

void CStartScreen::StartAnimation() {
	if (!CSettingsProvider::This().SmoothAnimations()) {
		m_nScroll = m_nScrollTarget;
		for (int i = 0; i < ROW_COUNT; i++) m_nRowScroll[i] = m_nRowTarget[i];
		::InvalidateRect(m_hWnd, NULL, FALSE);
		return;
	}
	if (s_pAnimating == this) return;
	if (s_pAnimating != NULL) ::KillTimer(NULL, s_nAnimationTimer);
	s_pAnimating = this;
	s_nAnimationTimer = ::SetTimer(NULL, 0, 10, AnimationTimer);
	AnimationStep();
}

// Moves the shown positions a part of the way to the targets, returns false when all are there
bool CStartScreen::AnimationStep() {
	auto Step = [](int& nCurrent, int nTarget) {
		int nDiff = nTarget - nCurrent;
		if (nDiff == 0) return false;
		int nMove = (int)(nDiff * 0.22);
		if (nMove == 0) nMove = nDiff > 0 ? 1 : -1;
		nCurrent += abs(nDiff) <= 2 ? nDiff : nMove;
		return true;
	};
	bool bMoving = Step(m_nScroll, m_nScrollTarget);
	for (int i = 0; i < ROW_COUNT; i++) bMoving = Step(m_nRowScroll[i], m_nRowTarget[i]) || bMoving;
	if (bMoving) ::InvalidateRect(m_hWnd, NULL, FALSE);
	return bMoving;
}

void CALLBACK CStartScreen::AnimationTimer(HWND, UINT, UINT_PTR, DWORD) {
	if (s_pAnimating == NULL || !s_pAnimating->AnimationStep()) {
		::KillTimer(NULL, s_nAnimationTimer);
		s_pAnimating = NULL;
	}
}

void CStartScreen::Reload() {
	m_recent.clear();
	m_recentTimes.clear();
	m_pinned.clear();
	m_shots.clear();
	m_shotTimes.clear();
	if (s_bTestLists) {
		FILETIME now;
		::GetSystemTimeAsFileTime(&now);
		for (size_t i = 0; i < s_testShots.size(); i++) {
			m_shots.push_back(s_testShots[i]);
			m_shotTimes.push_back((((__int64)now.dwHighDateTime << 32) | now.dwLowDateTime) - (__int64)i * 1800 * 10000000LL);
		}
	} else if (CSettingsProvider::This().ScreenshotHistory()) {
		ReadScreenshots(m_shots, m_shotTimes);
	}
	std::vector<CString> recent;
	if (s_bTestLists) {
		recent = s_testRecent;
	} else {
		recent = ReadLines(_T("StartRecent.txt"));
	}
	for (const CString& sLine : recent) {
		__int64 nTime = 0;
		CString sPath = RecentPath(sLine, &nTime);
		if (::GetFileAttributes(sPath) == INVALID_FILE_ATTRIBUTES) continue; // deleted or moved
		m_recent.push_back(sPath);
		m_recentTimes.push_back(nTime);
	}
	for (const CString& sPath : s_bTestLists ? s_testPinned : ReadLines(_T("StartPinned.txt"))) {
		if (::GetFileAttributes(sPath) != INVALID_FILE_ATTRIBUTES) m_pinned.push_back(sPath);
	}
	m_nScroll = m_nScrollTarget = 0;
	for (int i = 0; i < ROW_COUNT; i++) m_nRowScroll[i] = m_nRowTarget[i] = 0;
	LoadThumbnails();
}

///////////////////////////////////////////////////////////////////////////////////
// Painting
///////////////////////////////////////////////////////////////////////////////////

static Color ToColor(COLORREF color, BYTE nAlpha = 255) {
	return Color(nAlpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

static COLORREF Darker(COLORREF color, int nPercent) {
	return RGB(GetRValue(color) * nPercent / 100, GetGValue(color) * nPercent / 100, GetBValue(color) * nPercent / 100);
}

static void AddRoundRect(GraphicsPath& path, const RectF& rect, float fRadius) {
	float d = 2 * fRadius;
	path.AddArc(rect.X, rect.Y, d, d, 180, 90);
	path.AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270, 90);
	path.AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0, 90);
	path.AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90, 90);
	path.CloseFigure();
}

static void FillRound(Graphics& g, const RectF& rect, float fRadius, COLORREF color, BYTE nAlpha = 255) {
	GraphicsPath path;
	AddRoundRect(path, rect, fRadius);
	SolidBrush brush(ToColor(color, nAlpha));
	g.FillPath(&brush, &path);
}

static void DrawRound(Graphics& g, const RectF& rect, float fRadius, COLORREF color, float fWidth, bool bDashed = false) {
	GraphicsPath path;
	AddRoundRect(path, rect, fRadius);
	Pen pen(ToColor(color), fWidth);
	if (bDashed) pen.SetDashStyle(DashStyleDash);
	g.DrawPath(&pen, &path);
}

static void PaintText(Graphics& g, LPCWSTR sText, const Font& font, COLORREF color, const RectF& rect,
	StringAlignment horizontal = StringAlignmentNear, StringAlignment vertical = StringAlignmentNear, bool bWrap = false, BYTE nAlpha = 255) {
	StringFormat format;
	format.SetAlignment(horizontal);
	format.SetLineAlignment(vertical);
	format.SetTrimming(StringTrimmingEllipsisCharacter);
	if (!bWrap) format.SetFormatFlags(StringFormatFlagsNoWrap);
	SolidBrush brush(ToColor(color, nAlpha));
	g.DrawString(sText, -1, &font, rect, &format, &brush);
}

static float TextWidth(Graphics& g, LPCWSTR sText, const Font& font) {
	RectF bounds;
	g.MeasureString(sText, -1, &font, PointF(0, 0), &bounds);
	return bounds.Width;
}

// The image fills the rectangle, the parts outside are cut off
static void DrawCover(Graphics& g, Bitmap* pBitmap, const RectF& rect, float fRadius) {
	GraphicsState state = g.Save();
	GraphicsPath path;
	AddRoundRect(path, rect, fRadius);
	g.SetClip(&path, CombineModeIntersect);
	float fWidth = (float)pBitmap->GetWidth(), fHeight = (float)pBitmap->GetHeight();
	float fScale = max(rect.Width / fWidth, rect.Height / fHeight);
	float fSourceWidth = rect.Width / fScale, fSourceHeight = rect.Height / fScale;
	g.DrawImage(pBitmap, rect, (fWidth - fSourceWidth) / 2, (fHeight - fSourceHeight) / 2, fSourceWidth, fSourceHeight, UnitPixel);
	g.Restore(state);
}

static CString FormatOpenTime(__int64 nTime) {
	if (nTime <= 0) return CString();
	FILETIME utc = { (DWORD)(nTime & 0xFFFFFFFF), (DWORD)(nTime >> 32) };
	FILETIME local;
	SYSTEMTIME time, now;
	if (!::FileTimeToLocalFileTime(&utc, &local) || !::FileTimeToSystemTime(&local, &time)) return CString();
	::GetLocalTime(&now);
	CString sText;
	FILETIME nowFile, localMidnight;
	SYSTEMTIME midnight = now;
	midnight.wHour = midnight.wMinute = midnight.wSecond = midnight.wMilliseconds = 0;
	::SystemTimeToFileTime(&now, &nowFile);
	::SystemTimeToFileTime(&midnight, &localMidnight);
	__int64 nMidnight = ((__int64)localMidnight.dwHighDateTime << 32) | localMidnight.dwLowDateTime;
	__int64 nLocal = ((__int64)local.dwHighDateTime << 32) | local.dwLowDateTime;
	const __int64 DAY = 24LL * 3600 * 10000000;
	if (nLocal >= nMidnight) {
		sText.Format(_T("%s %02d:%02d"), CNLS::GetString(_T("Today")), time.wHour, time.wMinute);
	} else if (nLocal >= nMidnight - DAY) {
		sText.Format(_T("%s %02d:%02d"), CNLS::GetString(_T("Yesterday")), time.wHour, time.wMinute);
	} else {
		wchar_t sDate[64] = { 0 };
		::GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &time, NULL, sDate, 64, NULL);
		sText = sDate;
	}
	return sText;
}

static CString FolderName(const CString& sPath) {
	CString sFolder = sPath.Left(max(0, sPath.ReverseFind(_T('\\'))));
	int nSlash = sFolder.ReverseFind(_T('\\'));
	return nSlash >= 0 ? sFolder.Mid(nSlash + 1) : sFolder;
}

static std::unique_ptr<FontFamily> CreateIconFamily() {
	std::unique_ptr<FontFamily> pFamily(new FontFamily(L"Segoe Fluent Icons"));
	if (pFamily->GetLastStatus() != Ok) pFamily.reset(new FontFamily(L"Segoe MDL2 Assets"));
	return pFamily;
}

void CStartScreen::Paint(HDC hDC, const CRect& clientRect) {
	m_clientRect = clientRect;
	m_hits.clear();
	int nW = clientRect.Width(), nH = clientRect.Height();
	if (nW <= 0 || nH <= 0) return;
	HDC hMemDC = ::CreateCompatibleDC(hDC);
	HBITMAP hBitmap = ::CreateCompatibleBitmap(hDC, nW, nH);
	HGDIOBJ hOldBitmap = ::SelectObject(hMemDC, hBitmap);
	{
		Graphics g(hMemDC);
		g.SetSmoothingMode(SmoothingModeAntiAlias);
		g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
		g.SetInterpolationMode(InterpolationModeHighQualityBilinear);
		g.SetPixelOffsetMode(PixelOffsetModeHalf);
		const DarkUI::CPalette& colors = DarkUI::Colors();
		g.Clear(ToColor(colors.Window));

		FontFamily family(L"Segoe UI"), semibold(L"Segoe UI Semibold");
		std::unique_ptr<FontFamily> pIcons = CreateIconFamily();
		Font titleFont(&semibold, (REAL)S(28), FontStyleRegular, UnitPixel);
		Font sectionFont(&semibold, (REAL)S(18), FontStyleRegular, UnitPixel);
		Font textFont(&family, (REAL)S(14), FontStyleRegular, UnitPixel);
		Font buttonFont(&family, (REAL)S(14), FontStyleRegular, UnitPixel);
		Font primaryFont(&semibold, (REAL)S(14), FontStyleRegular, UnitPixel);
		Font smallFont(&family, (REAL)S(12), FontStyleRegular, UnitPixel);
		Font hintFont(&family, (REAL)S(13), FontStyleRegular, UnitPixel);
		Font iconFont(pIcons.get(), (REAL)S(16), FontStyleRegular, UnitPixel);
		Font bigIconFont(pIcons.get(), (REAL)S(40), FontStyleRegular, UnitPixel);
		Font addIconFont(pIcons.get(), (REAL)S(24), FontStyleRegular, UnitPixel);

		int nMaxWidth = S(1360);
		int nContentWidth = min(nW, nMaxWidth);
		int nPad = nW < S(760) ? S(20) : S(40);
		float x0 = (float)((nW - nContentWidth) / 2 + nPad);
		float fWidth = (float)(nContentWidth - 2 * nPad);
		float y = (float)(nPad - m_nScroll);
		auto AddHit = [&](const RectF& rect, EHitKind eKind, int nIndex) {
			CHit hit = { CRect((int)rect.X + clientRect.left, (int)rect.Y + clientRect.top, (int)(rect.X + rect.Width) + clientRect.left,
				(int)(rect.Y + rect.Height) + clientRect.top), eKind, nIndex };
			m_hits.push_back(hit);
		};
		auto IsHover = [&](EHitKind eKind, int nIndex) { return m_eHoverKind == eKind && m_nHoverIndex == nIndex; };

		// ---- header: title and the actions
		bool bEmpty = m_recent.empty() && m_pinned.empty();
		PaintText(g, CNLS::GetString(_T("Start")), titleFont, colors.Text, RectF(x0, y, fWidth, (REAL)S(40)));
		PaintText(g, CNLS::GetString(bEmpty ? _T("No photo opened yet.") : _T("Your recently opened photos and your picks.")), textFont, colors.SubText,
			RectF(x0, y + S(44), fWidth, (REAL)S(22)));
		struct { wchar_t Glyph; LPCTSTR Text; } buttons[] = {
			{ 0xE8E5, _T("Open file") }, { 0xE8B7, _T("Open folder") }, { 0xE77F, _T("Paste") }, { 0xE722, _T("Screenshot") },
		};
		const int nButtons = sizeof(buttons) / sizeof(buttons[0]);
		float buttonWidths[nButtons];
		float fButtonsWidth = 0;
		for (int i = 0; i < nButtons; i++) {
			buttonWidths[i] = TextWidth(g, CNLS::GetString(buttons[i].Text), i == 0 ? primaryFont : buttonFont) + S(16) + S(8) + 2 * S(16);
			fButtonsWidth += buttonWidths[i] + (i > 0 ? S(8) : 0);
		}
		bool bButtonsBelow = fButtonsWidth > fWidth - S(320);
		float fButtonX = bButtonsBelow ? x0 : x0 + fWidth - fButtonsWidth;
		float fButtonY = bButtonsBelow ? y + S(80) : y + S(10);
		for (int i = 0; i < nButtons; i++) {
			RectF rect(fButtonX, fButtonY, buttonWidths[i], (REAL)S(40));
			bool bPrimary = i == 0, bHover = IsHover(HIT_BUTTON, i);
			if (bPrimary) {
				FillRound(g, rect, (REAL)S(6), bHover ? Darker(colors.Accent, 88) : colors.Accent);
			} else {
				FillRound(g, rect, (REAL)S(6), bHover ? colors.ControlHot : colors.Control);
				DrawRound(g, rect, (REAL)S(6), colors.Border, 1.0f);
			}
			COLORREF textColor = bPrimary ? colors.AccentText : colors.Text;
			wchar_t sGlyph[2] = { buttons[i].Glyph, 0 };
			PaintText(g, sGlyph, iconFont, textColor, RectF(rect.X + S(16), rect.Y, (REAL)S(18), rect.Height), StringAlignmentNear, StringAlignmentCenter);
			PaintText(g, CNLS::GetString(buttons[i].Text), bPrimary ? primaryFont : buttonFont, textColor,
				RectF(rect.X + S(16) + S(24), rect.Y, rect.Width - S(16) - S(24) - S(8), rect.Height), StringAlignmentNear, StringAlignmentCenter);
			AddHit(rect, HIT_BUTTON, i);
			fButtonX += buttonWidths[i] + S(8);
		}
		y += (bButtonsBelow ? S(80) + S(40) : S(72)) + S(36);

		// ---- rows of photos (the last opened ones, the screenshots), scrolled with the wheel, the arrow buttons and keys
		auto PaintRow = [&](int nRow, const std::vector<CString>& files, const std::vector<__int64>& times, EHitKind eCardKind, bool bFolderMeta) {
			float fCardWidth = (REAL)S(232), fThumbHeight = (REAL)S(156), fGap = (REAL)S(16);
			float fRowHeight = fThumbHeight + S(8) + S(20) + S(18);
			m_nRowWidth[nRow] = (int)(files.size() * (fCardWidth + fGap) - fGap);
			m_nRowVisible[nRow] = (int)fWidth;
			m_nRowStep[nRow] = (int)(fCardWidth + fGap);
			m_nRowScroll[nRow] = max(0, min(m_nRowScroll[nRow], m_nRowWidth[nRow] - m_nRowVisible[nRow]));
			m_nRowTarget[nRow] = max(0, min(m_nRowTarget[nRow], m_nRowWidth[nRow] - m_nRowVisible[nRow]));
			m_rowTrack[nRow] = m_rowThumb[nRow] = CRect(0, 0, 0, 0);
			int nScrollX = m_nRowScroll[nRow];
			AddHit(RectF(x0, y, fWidth, fRowHeight), HIT_ROW, nRow);
			GraphicsState state = g.Save();
			g.SetClip(RectF(x0 - S(2), y - S(2), fWidth + S(4), fRowHeight + S(4)), CombineModeIntersect);
			for (int i = 0; i < (int)files.size(); i++) {
				float fX = x0 + i * (fCardWidth + fGap) - nScrollX;
				if (fX + fCardWidth < x0 || fX > x0 + fWidth) continue;
				RectF thumbRect(fX, y, fCardWidth, fThumbHeight);
				std::shared_ptr<Bitmap> pBitmap;
				{
					std::lock_guard<std::mutex> lock(m_pThumbnails->Lock);
					auto it = m_pThumbnails->Images.find(files[i]);
					if (it != m_pThumbnails->Images.end()) pBitmap = it->second;
				}
				FillRound(g, thumbRect, (REAL)S(8), colors.Control);
				if (pBitmap) {
					DrawCover(g, pBitmap.get(), thumbRect, (REAL)S(8));
				} else {
					PaintText(g, L"\xE91B", addIconFont, colors.SubText, thumbRect, StringAlignmentCenter, StringAlignmentCenter);
				}
				if (IsHover(eCardKind, i)) {
					DrawRound(g, RectF(thumbRect.X + 1, thumbRect.Y + 1, thumbRect.Width - 2, thumbRect.Height - 2), (REAL)S(8), colors.Accent, (REAL)S(2));
				} else {
					DrawRound(g, thumbRect, (REAL)S(8), colors.Border, 1.0f);
				}
				PaintText(g, ::PathFindFileName(files[i]), textFont, colors.Text, RectF(fX + S(2), y + fThumbHeight + S(8), fCardWidth - S(4), (REAL)S(20)));
				CString sMeta = FormatOpenTime(times[i]);
				CString sFolder = bFolderMeta ? FolderName(files[i]) : CString();
				if (!sFolder.IsEmpty()) sMeta += (sMeta.IsEmpty() ? _T("") : _T(" \x00B7 ")) + sFolder;
				PaintText(g, sMeta, smallFont, colors.SubText, RectF(fX + S(2), y + fThumbHeight + S(28), fCardWidth - S(4), (REAL)S(18)));
				RectF cardRect(max(fX, x0), y, min(fX + fCardWidth, x0 + fWidth) - max(fX, x0), fRowHeight);
				AddHit(cardRect, eCardKind, i);
			}
			g.Restore(state);
			if (m_nRowWidth[nRow] > m_nRowVisible[nRow]) {
				// position of the row and the buttons for scrolling at the cut sides
				float fTrack = fWidth, fThumb = max((float)S(40), fTrack * m_nRowVisible[nRow] / m_nRowWidth[nRow]);
				float fPos = (fTrack - fThumb) * nScrollX / max(1, m_nRowWidth[nRow] - m_nRowVisible[nRow]);
				bool bBarActive = IsHover(HIT_ROW_BAR, nRow) || m_nDrag == 1 + nRow;
				float fBarHeight = (REAL)S(bBarActive ? 6 : 3), fBarY = y + fRowHeight + S(8) - (bBarActive ? S(1) : 0);
				FillRound(g, RectF(x0, fBarY, fTrack, fBarHeight), fBarHeight / 2, colors.Control);
				FillRound(g, RectF(x0 + fPos, fBarY, fThumb, fBarHeight), fBarHeight / 2, bBarActive ? colors.Text : colors.SubText);
				// the track can be clicked and the thumb dragged, a larger area than drawn
				RectF trackHit(x0, y + fRowHeight, fTrack, (REAL)S(20));
				AddHit(trackHit, HIT_ROW_BAR, nRow);
				m_rowTrack[nRow] = m_hits.back().Rect;
				m_rowThumb[nRow] = CRect((int)(x0 + fPos) + clientRect.left, m_rowTrack[nRow].top, (int)(x0 + fPos + fThumb) + clientRect.left, m_rowTrack[nRow].bottom);
				float fButton = (REAL)S(40), fButtonY = y + (fThumbHeight - fButton) / 2;
				for (int nSide = 0; nSide < 2; nSide++) {
					bool bVisible = nSide == 0 ? nScrollX > 0 : nScrollX < m_nRowWidth[nRow] - m_nRowVisible[nRow];
					if (!bVisible) continue;
					EHitKind eKind = nSide == 0 ? HIT_ROW_PREV : HIT_ROW_NEXT;
					RectF buttonRect(nSide == 0 ? x0 + S(8) : x0 + fWidth - S(8) - fButton, fButtonY, fButton, fButton);
					SolidBrush shadow(Color(IsHover(eKind, nRow) ? 235 : 190, GetRValue(colors.Window), GetGValue(colors.Window), GetBValue(colors.Window)));
					g.FillEllipse(&shadow, buttonRect);
					Pen ring(ToColor(colors.Border), 1.0f);
					g.DrawEllipse(&ring, buttonRect);
					PaintText(g, nSide == 0 ? L"\xE76B" : L"\xE76C", iconFont, colors.Text, buttonRect, StringAlignmentCenter, StringAlignmentCenter);
					AddHit(buttonRect, eKind, nRow);
				}
			}
			y += fRowHeight + S(40);
		};
		auto PaintRowTitle = [&](LPCTSTR sTitle, LPCTSTR sLink, EHitKind eLinkKind) {
			PaintText(g, CNLS::GetString(sTitle), sectionFont, colors.Text, RectF(x0, y, fWidth, (REAL)S(26)));
			CString sLinkText = CNLS::GetString(sLink);
			float fLinkWidth = TextWidth(g, sLinkText, hintFont) + S(4);
			RectF linkRect(x0 + fWidth - fLinkWidth, y + S(4), fLinkWidth, (REAL)S(20));
			PaintText(g, sLinkText, hintFont, IsHover(eLinkKind, 0) ? colors.Text : colors.Accent, linkRect, StringAlignmentFar);
			AddHit(linkRect, eLinkKind, 0);
			y += S(38);
		};
		if (!m_recent.empty()) {
			PaintRowTitle(_T("Recently opened"), _T("Clear list"), HIT_CLEAR_RECENT);
			PaintRow(ROW_RECENT, m_recent, m_recentTimes, HIT_RECENT, true);
		} else {
			// first start: the photos can be dropped here
			RectF dropRect(x0, y, fWidth, (REAL)S(200));
			FillRound(g, dropRect, (REAL)S(12), colors.Surface);
			DrawRound(g, dropRect, (REAL)S(12), colors.Border, (REAL)(1.5f * m_nDpi / 96), true);
			PaintText(g, L"\xEB9F", bigIconFont, colors.Accent, RectF(x0, y + S(30), fWidth, (REAL)S(56)), StringAlignmentCenter, StringAlignmentCenter);
			PaintText(g, CNLS::GetString(_T("Drag a photo here")), sectionFont, colors.Text, RectF(x0, y + S(96), fWidth, (REAL)S(28)), StringAlignmentCenter);
			PaintText(g, CNLS::GetString(_T("JPEG, PNG, WebP, HEIC, AVIF, JPEG XL, RAW and more")), textFont, colors.SubText,
				RectF(x0, y + S(130), fWidth, (REAL)S(22)), StringAlignmentCenter);
			y += S(200) + S(40);
		}
		if (!m_shots.empty()) {
			PaintRowTitle(_T("Screenshots"), _T("Open folder"), HIT_SHOT_FOLDER);
			PaintRow(ROW_SHOTS, m_shots, m_shotTimes, HIT_SHOT, false);
		}

		// ---- the photos the user pinned
		CString sPinnedTitle = CNLS::GetString(_T("My picks"));
		PaintText(g, sPinnedTitle, sectionFont, colors.Text, RectF(x0, y, fWidth, (REAL)S(26)));
		if (!m_pinned.empty()) {
			CString sCount;
			sCount.Format(CNLS::GetString(_T("%d photos")), (int)m_pinned.size());
			float fTitleWidth = TextWidth(g, sPinnedTitle, sectionFont);
			PaintText(g, sCount, hintFont, colors.SubText, RectF(x0 + fTitleWidth + S(10), y + S(4), (REAL)S(200), (REAL)S(20)));
		}
		y += S(38);
		float fGap = (REAL)S(12);
		int nColumns = max(2, (int)((fWidth + fGap) / (S(172) + fGap)));
		float fTile = (fWidth - (nColumns - 1) * fGap) / nColumns;
		auto CellRect = [&](int nCell) {
			return RectF(x0 + (nCell % nColumns) * (fTile + fGap), y + (nCell / nColumns) * (fTile + fGap), fTile, fTile);
		};
		{
			RectF addRect = CellRect(0);
			bool bHover = IsHover(HIT_ADD_PINNED, 0);
			if (bHover) FillRound(g, addRect, (REAL)S(8), colors.ControlHot);
			DrawRound(g, RectF(addRect.X + 1, addRect.Y + 1, addRect.Width - 2, addRect.Height - 2), (REAL)S(8), colors.Border, (REAL)(1.5f * m_nDpi / 96), true);
			PaintText(g, L"\xE710", addIconFont, colors.Text, RectF(addRect.X, addRect.Y + addRect.Height / 2 - S(34), addRect.Width, (REAL)S(32)), StringAlignmentCenter, StringAlignmentCenter);
			PaintText(g, CNLS::GetString(_T("Add photos")), textFont, colors.Text, RectF(addRect.X, addRect.Y + addRect.Height / 2 + S(4), addRect.Width, (REAL)S(22)), StringAlignmentCenter);
			AddHit(addRect, HIT_ADD_PINNED, 0);
		}
		int nCells = 1 + (int)m_pinned.size();
		for (int i = 0; i < (int)m_pinned.size(); i++) {
			RectF rect = CellRect(i + 1);
			if (rect.Y > nH || rect.Y + rect.Height < 0) continue;
			std::shared_ptr<Bitmap> pBitmap;
			{
				std::lock_guard<std::mutex> lock(m_pThumbnails->Lock);
				auto it = m_pThumbnails->Images.find(m_pinned[i]);
				if (it != m_pThumbnails->Images.end()) pBitmap = it->second;
			}
			FillRound(g, rect, (REAL)S(8), colors.Control);
			if (pBitmap) {
				DrawCover(g, pBitmap.get(), rect, (REAL)S(8));
			} else {
				PaintText(g, L"\xE91B", addIconFont, colors.SubText, rect, StringAlignmentCenter, StringAlignmentCenter);
			}
			AddHit(rect, HIT_PINNED, i);
			bool bHover = IsHover(HIT_PINNED, i) || IsHover(HIT_UNPIN, i);
			if (bHover) {
				// name over a dark gradient and the button to unpin
				GraphicsState state = g.Save();
				GraphicsPath clip;
				AddRoundRect(clip, rect, (REAL)S(8));
				g.SetClip(&clip, CombineModeIntersect);
				RectF shade(rect.X, rect.Y + rect.Height - S(48), rect.Width, (REAL)S(48));
				LinearGradientBrush gradient(PointF(0, shade.Y - 1), PointF(0, shade.Y + shade.Height), Color(0, 0, 0, 0), Color(180, 0, 0, 0));
				g.FillRectangle(&gradient, shade);
				PaintText(g, ::PathFindFileName(m_pinned[i]), smallFont, RGB(255, 255, 255), RectF(rect.X + S(10), rect.Y + rect.Height - S(26), rect.Width - S(20), (REAL)S(18)));
				g.Restore(state);
				DrawRound(g, RectF(rect.X + 1, rect.Y + 1, rect.Width - 2, rect.Height - 2), (REAL)S(8), colors.Accent, (REAL)S(2));
				RectF unpinRect(rect.X + rect.Width - S(40), rect.Y + S(8), (REAL)S(32), (REAL)S(32));
				SolidBrush circle(IsHover(HIT_UNPIN, i) ? Color(220, 0, 0, 0) : Color(150, 0, 0, 0));
				g.FillEllipse(&circle, unpinRect);
				PaintText(g, L"\xE711", iconFont, RGB(255, 255, 255), unpinRect, StringAlignmentCenter, StringAlignmentCenter);
				AddHit(unpinRect, HIT_UNPIN, i);
			} else {
				DrawRound(g, rect, (REAL)S(8), colors.Border, 1.0f);
			}
		}
		if (m_pinned.empty()) {
			int nSpan = min(3, nColumns - 1);
			RectF first = CellRect(1);
			RectF hint(first.X, first.Y, nSpan * fTile + (nSpan - 1) * fGap, fTile);
			FillRound(g, hint, (REAL)S(8), colors.Surface);
			CString sHint;
			sHint.Format(CNLS::GetString(_T("Pin your favorite photos here. Press %s on an open photo or right click > Pin to start screen.")), (LPCTSTR)m_sPinShortcut);
			PaintText(g, sHint, textFont, colors.Text, RectF(hint.X + S(24), hint.Y, hint.Width - S(48), hint.Height), StringAlignmentNear, StringAlignmentCenter, true);
			nCells = 1 + nSpan;
		}
		int nRows = (nCells + nColumns - 1) / nColumns;
		y += nRows * (fTile + fGap) + S(8);

		PaintText(g, CNLS::GetString(_T("Tip: drag a photo onto this window to open it.")), hintFont, colors.SubText, RectF(x0, y, fWidth, (REAL)S(40)), StringAlignmentNear, StringAlignmentNear, true);
		y += S(40) + nPad;
		m_nContentHeight = (int)(y + m_nScroll);

		// scroll bar of the page: the track can be clicked and the thumb dragged
		m_vTrack = m_vThumb = CRect(0, 0, 0, 0);
		m_nScrollTarget = max(0, min(m_nScrollTarget, m_nContentHeight - nH));
		if (m_nContentHeight > nH) {
			float fThumb = max((float)S(40), (float)nH * nH / m_nContentHeight);
			float fPos = (nH - fThumb) * m_nScroll / max(1, m_nContentHeight - nH);
			bool bActive = IsHover(HIT_VBAR, 0) || m_nDrag == 0;
			float fBarWidth = (REAL)S(bActive ? 8 : 4);
			if (bActive) FillRound(g, RectF((REAL)(nW - S(12)), (REAL)S(2), (REAL)S(10), (REAL)(nH - S(4))), (REAL)S(5), colors.Control, 160);
			FillRound(g, RectF((REAL)(nW - S(4)) - fBarWidth, fPos + S(2), fBarWidth, fThumb - S(4)), fBarWidth / 2, bActive ? colors.SubText : colors.Border);
			AddHit(RectF((REAL)(nW - S(16)), 0, (REAL)S(16), (REAL)nH), HIT_VBAR, 0);
			m_vTrack = m_hits.back().Rect;
			m_vThumb = CRect(m_vTrack.left, (int)fPos + clientRect.top, m_vTrack.right, (int)(fPos + fThumb) + clientRect.top);
		}
	}
	::BitBlt(hDC, clientRect.left, clientRect.top, nW, nH, hMemDC, 0, 0, SRCCOPY);
	::SelectObject(hMemDC, hOldBitmap);
	::DeleteObject(hBitmap);
	::DeleteDC(hMemDC);
}

///////////////////////////////////////////////////////////////////////////////////
// Mouse
///////////////////////////////////////////////////////////////////////////////////

const CStartScreen::CHit* CStartScreen::HitTest(int nX, int nY) const {
	// the later ones are on top (unpin button over the tile)
	for (int i = (int)m_hits.size() - 1; i >= 0; i--) {
		if (m_hits[i].Kind != HIT_ROW && m_hits[i].Rect.PtInRect(CPoint(nX, nY))) return &m_hits[i];
	}
	return NULL;
}

bool CStartScreen::IsOverItem(int nX, int nY) {
	return HitTest(nX, nY) != NULL;
}

bool CStartScreen::ScrollRow(int nRow, int nPixels) {
	if (nRow < 0 || nRow >= ROW_COUNT || m_nRowWidth[nRow] <= m_nRowVisible[nRow]) return false;
	int nOld = m_nRowTarget[nRow];
	SetRowTarget(nRow, m_nRowTarget[nRow] + nPixels);
	return nOld != m_nRowTarget[nRow];
}

bool CStartScreen::OnKey(int nVirtualKey) {
	int nPage = max(S(40), m_clientRect.Height() - S(80));
	int nMaxScroll = max(0, m_nContentHeight - m_clientRect.Height());
	switch (nVirtualKey) {
		case VK_LEFT: ScrollRow(m_nKeyRow, -max(1, m_nRowStep[m_nKeyRow])); return true;
		case VK_RIGHT: ScrollRow(m_nKeyRow, max(1, m_nRowStep[m_nKeyRow])); return true;
		case VK_UP: SetScrollTarget(m_nScrollTarget - S(120)); return true;
		case VK_DOWN: SetScrollTarget(m_nScrollTarget + S(120)); return true;
		case VK_PRIOR: SetScrollTarget(m_nScrollTarget - nPage); return true;
		case VK_NEXT: SetScrollTarget(m_nScrollTarget + nPage); return true;
		case VK_HOME: SetScrollTarget(0); return true;
		case VK_END: SetScrollTarget(nMaxScroll); return true;
	}
	return false;
}

bool CStartScreen::OnMouseMove(int nX, int nY) {
	if (m_nDrag >= 0) {
		// the thumb follows the mouse
		if (m_nDrag == 0) {
			int nTrack = max(1, m_vTrack.Height() - m_vThumb.Height());
			int nRange = m_nContentHeight - m_clientRect.Height();
			m_nScroll = m_nScrollTarget = max(0, min(nRange, m_nDragStartScroll + (int)((__int64)(nY - m_nDragStartMouse) * nRange / nTrack)));
		} else {
			int nRow = m_nDrag - 1;
			int nTrack = max(1, m_rowTrack[nRow].Width() - m_rowThumb[nRow].Width());
			int nRange = m_nRowWidth[nRow] - m_nRowVisible[nRow];
			m_nRowScroll[nRow] = m_nRowTarget[nRow] = max(0, min(nRange, m_nDragStartScroll + (int)((__int64)(nX - m_nDragStartMouse) * nRange / nTrack)));
		}
		return true;
	}
	for (const CHit& hit : m_hits) {
		if (hit.Kind == HIT_ROW && hit.Rect.PtInRect(CPoint(nX, nY))) m_nKeyRow = hit.Index;
	}
	const CHit* pHit = HitTest(nX, nY);
	EHitKind eKind = pHit != NULL ? pHit->Kind : HIT_NONE;
	int nIndex = pHit != NULL ? pHit->Index : -1;
	if (eKind == m_eHoverKind && nIndex == m_nHoverIndex) return false;
	m_eHoverKind = eKind;
	m_nHoverIndex = nIndex;
	return true;
}

void CStartScreen::OnMouseLeave() {
	m_eHoverKind = HIT_NONE;
	m_nHoverIndex = -1;
}

bool CStartScreen::OnMouseWheel(int nX, int nY, int nDelta, bool bHorizontal) {
	if (bHorizontal) {
		// tilt wheel, touchpad or Shift: the row under the mouse (else the last one used)
		int nRow = m_nKeyRow;
		for (const CHit& hit : m_hits) {
			if (hit.Kind == HIT_ROW && hit.Rect.PtInRect(CPoint(nX, nY))) nRow = hit.Index;
		}
		return ScrollRow(nRow, nDelta * S(248) / WHEEL_DELTA);
	}
	// the vertical wheel always scrolls the page, also over a row
	int nOld = m_nScrollTarget;
	SetScrollTarget(m_nScrollTarget - nDelta * S(120) / WHEEL_DELTA);
	return nOld != m_nScrollTarget;
}

void CStartScreen::OnMouseUp() {
	if (m_nDrag >= 0) {
		m_nDrag = -1;
		::InvalidateRect(m_hWnd, NULL, FALSE);
	}
}

void CStartScreen::AddPinnedWithDialog() {
	IFileOpenDialog* pDialog = NULL;
	if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDialog)))) return;
	DWORD nOptions = 0;
	pDialog->GetOptions(&nOptions);
	pDialog->SetOptions(nOptions | FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM);
	CString sImages = CNLS::GetString(_T("Images"));
	COMDLG_FILTERSPEC filters[] = {
		{ sImages, L"*.jpg;*.jpeg;*.jfif;*.png;*.webp;*.heic;*.heif;*.avif;*.jxl;*.gif;*.bmp;*.tif;*.tiff;*.dng;*.cr2;*.cr3;*.nef;*.arw;*.orf;*.rw2;*.raf;*.qoi;*.psd" },
		{ L"*.*", L"*.*" },
	};
	pDialog->SetFileTypes(2, filters);
	pDialog->SetTitle(CNLS::GetString(_T("Add photos")));
	if (SUCCEEDED(pDialog->Show(m_hWnd))) {
		IShellItemArray* pItems = NULL;
		if (SUCCEEDED(pDialog->GetResults(&pItems))) {
			DWORD nCount = 0;
			pItems->GetCount(&nCount);
			// the first selected file first
			for (int i = (int)nCount - 1; i >= 0; i--) {
				IShellItem* pItem = NULL;
				if (SUCCEEDED(pItems->GetItemAt(i, &pItem))) {
					PWSTR pPath = NULL;
					if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pPath))) {
						SetPinned(pPath, true);
						::CoTaskMemFree(pPath);
					}
					pItem->Release();
				}
			}
			pItems->Release();
		}
	}
	pDialog->Release();
	int nScroll = m_nScroll;
	Reload();
	m_nScroll = m_nScrollTarget = nScroll;
}

CStartScreen::CResult CStartScreen::OnClick(int nX, int nY) {
	CResult result = { ACTION_NONE, CString() };
	const CHit* pHit = HitTest(nX, nY);
	if (pHit == NULL) return result;
	CHit hit = *pHit;
	switch (hit.Kind) {
		case HIT_BUTTON: {
			EAction actions[] = { ACTION_OPEN_DIALOG, ACTION_OPEN_FOLDER, ACTION_PASTE, ACTION_SCREENSHOT };
			if (hit.Index >= 0 && hit.Index < 4) result.Action = actions[hit.Index];
			break;
		}
		case HIT_CLEAR_RECENT:
			if (!s_bTestLists) WriteLines(_T("StartRecent.txt"), std::vector<CString>());
			m_recent.clear();
			m_recentTimes.clear();
			::InvalidateRect(m_hWnd, NULL, FALSE);
			break;
		case HIT_RECENT:
			result.Action = ACTION_OPEN_FILE;
			result.File = m_recent[hit.Index];
			break;
		case HIT_SHOT:
			result.Action = ACTION_OPEN_FILE;
			result.File = m_shots[hit.Index];
			break;
		case HIT_SHOT_FOLDER:
			::ShellExecute(m_hWnd, _T("open"), ScreenCapture::GetScreenshotFolder(), NULL, NULL, SW_SHOWNORMAL);
			break;
		case HIT_VBAR:
			if (m_vThumb.PtInRect(CPoint(nX, nY))) {
				m_nDrag = 0;
				m_nDragStartMouse = nY;
				m_nDragStartScroll = m_nScroll;
			} else {
				// the thumb jumps to the clicked place
				int nTrack = max(1, m_vTrack.Height() - m_vThumb.Height());
				int nRange = m_nContentHeight - m_clientRect.Height();
				SetScrollTarget((int)((__int64)(nY - m_vTrack.top - m_vThumb.Height() / 2) * nRange / nTrack));
			}
			::InvalidateRect(m_hWnd, NULL, FALSE);
			break;
		case HIT_ROW_BAR: {
			int nRow = hit.Index;
			if (m_rowThumb[nRow].PtInRect(CPoint(nX, m_rowThumb[nRow].top + 1)) && nX >= m_rowThumb[nRow].left && nX < m_rowThumb[nRow].right) {
				m_nDrag = 1 + nRow;
				m_nDragStartMouse = nX;
				m_nDragStartScroll = m_nRowScroll[nRow];
			} else {
				int nTrack = max(1, m_rowTrack[nRow].Width() - m_rowThumb[nRow].Width());
				int nRange = m_nRowWidth[nRow] - m_nRowVisible[nRow];
				SetRowTarget(nRow, (int)((__int64)(nX - m_rowTrack[nRow].left - m_rowThumb[nRow].Width() / 2) * nRange / nTrack));
			}
			::InvalidateRect(m_hWnd, NULL, FALSE);
			break;
		}
		case HIT_ROW_PREV:
		case HIT_ROW_NEXT: {
			// one page less one card, the cut card is shown whole
			int nRow = hit.Index;
			int nPage = max(m_nRowStep[nRow], m_nRowVisible[nRow] - m_nRowStep[nRow]);
			ScrollRow(nRow, hit.Kind == HIT_ROW_PREV ? -nPage : nPage);
			::InvalidateRect(m_hWnd, NULL, FALSE);
			break;
		}
		case HIT_PINNED:
			result.Action = ACTION_OPEN_FILE;
			result.File = m_pinned[hit.Index];
			break;
		case HIT_UNPIN:
			SetPinned(m_pinned[hit.Index], false);
			m_pinned.erase(m_pinned.begin() + hit.Index);
			m_eHoverKind = HIT_NONE;
			::InvalidateRect(m_hWnd, NULL, FALSE);
			break;
		case HIT_ADD_PINNED:
			AddPinnedWithDialog();
			::InvalidateRect(m_hWnd, NULL, FALSE);
			break;
	}
	return result;
}

CStartScreen::CResult CStartScreen::OnContextMenu(HWND hWnd, int nX, int nY, POINT screenPoint) {
	CResult result = { ACTION_NONE, CString() };
	const CHit* pHit = HitTest(nX, nY);
	if (pHit == NULL || (pHit->Kind != HIT_RECENT && pHit->Kind != HIT_PINNED && pHit->Kind != HIT_UNPIN && pHit->Kind != HIT_SHOT)) return result;
	bool bRecent = pHit->Kind == HIT_RECENT;
	int nIndex = pHit->Index;
	CString sFile = bRecent ? m_recent[nIndex] : (pHit->Kind == HIT_SHOT ? m_shots[nIndex] : m_pinned[nIndex]);
	bool bPinned = IsPinned(sFile);
	enum { CMD_OPEN = 1, CMD_FOLDER, CMD_PIN, CMD_REMOVE };
	HMENU hMenu = ::CreatePopupMenu();
	::AppendMenu(hMenu, MF_STRING, CMD_OPEN, CNLS::GetString(_T("Open")));
	::AppendMenu(hMenu, MF_STRING, CMD_FOLDER, CNLS::GetString(_T("Open containing folder")));
	::AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
	::AppendMenu(hMenu, MF_STRING, CMD_PIN, CNLS::GetString(bPinned ? _T("Unpin from start screen") : _T("Pin to start screen")));
	if (bRecent) ::AppendMenu(hMenu, MF_STRING, CMD_REMOVE, CNLS::GetString(_T("Remove from list")));
	int nCommand = ::TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPoint.x, screenPoint.y, 0, hWnd, NULL);
	::DestroyMenu(hMenu);
	switch (nCommand) {
		case CMD_OPEN:
			result.Action = ACTION_OPEN_FILE;
			result.File = sFile;
			break;
		case CMD_FOLDER: {
			PIDLIST_ABSOLUTE pidl = ::ILCreateFromPath(sFile);
			if (pidl != NULL) {
				::SHOpenFolderAndSelectItems(pidl, 0, NULL, 0);
				::ILFree(pidl);
			}
			break;
		}
		case CMD_PIN:
			SetPinned(sFile, !bPinned);
			Reload();
			break;
		case CMD_REMOVE: {
			if (!s_bTestLists) {
				std::vector<CString> lines = ReadLines(_T("StartRecent.txt"));
				lines.erase(std::remove_if(lines.begin(), lines.end(), [&](const CString& sLine) { return RecentPath(sLine).CompareNoCase(sFile) == 0; }), lines.end());
				WriteLines(_T("StartRecent.txt"), lines);
			}
			m_recentTimes.erase(m_recentTimes.begin() + nIndex);
			m_recent.erase(m_recent.begin() + nIndex);
			break;
		}
	}
	m_eHoverKind = HIT_NONE;
	::InvalidateRect(m_hWnd, NULL, FALSE);
	return result;
}
