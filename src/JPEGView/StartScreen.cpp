#include "StdAfx.h"
#include "StartScreen.h"
#include "SettingsProvider.h"
#include "NLS.h"
#include "DarkUI.h"
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
static std::vector<CString> s_testRecent, s_testPinned;

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

void CStartScreen::SetTestLists(const std::vector<CString>& recent, const std::vector<CString>& pinned) {
	s_bTestLists = true;
	s_testRecent = recent;
	s_testPinned = pinned;
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

static std::shared_ptr<Bitmap> ToBitmap(HBITMAP hBitmap) {
	DIBSECTION section;
	if (::GetObject(hBitmap, sizeof(section), &section) != sizeof(section) || section.dsBm.bmBitsPixel != 32 || section.dsBm.bmBits == NULL) {
		Bitmap* pSource = Bitmap::FromHBITMAP(hBitmap, NULL);
		if (pSource == NULL) return NULL;
		std::shared_ptr<Bitmap> pCopy(pSource->Clone(0, 0, (INT)pSource->GetWidth(), (INT)pSource->GetHeight(), PixelFormat32bppRGB));
		delete pSource;
		return pCopy;
	}
	int nWidth = section.dsBm.bmWidth, nHeight = section.dsBm.bmHeight;
	bool bBottomUp = section.dsBmih.biHeight > 0;
	const BYTE* pBits = (const BYTE*)section.dsBm.bmBits;
	int nStride = section.dsBm.bmWidthBytes;
	bool bAlpha = false;
	for (int y = 0; y < nHeight && !bAlpha; y++) {
		const BYTE* pRow = pBits + (size_t)y * nStride;
		for (int x = 0; x < nWidth; x++) if (pRow[x * 4 + 3] != 0) { bAlpha = true; break; }
	}
	PixelFormat format = bAlpha ? PixelFormat32bppPARGB : PixelFormat32bppRGB;
	std::shared_ptr<Bitmap> pBitmap(new Bitmap(nWidth, nHeight, format));
	BitmapData data;
	Rect rect(0, 0, nWidth, nHeight);
	if (pBitmap->LockBits(&rect, ImageLockModeWrite, format, &data) != Ok) return NULL;
	for (int y = 0; y < nHeight; y++) {
		const BYTE* pSource = pBits + (size_t)(bBottomUp ? nHeight - 1 - y : y) * nStride;
		memcpy((BYTE*)data.Scan0 + (size_t)y * data.Stride, pSource, (size_t)nWidth * 4);
	}
	pBitmap->UnlockBits(&data);
	return pBitmap;
}

void CStartScreen::LoadThumbnails() {
	std::vector<CString> paths;
	{
		std::lock_guard<std::mutex> lock(m_pThumbnails->Lock);
		for (const std::vector<CString>* pList : { &m_recent, &m_pinned }) {
			for (const CString& sPath : *pList) {
				if (m_pThumbnails->Images.count(sPath) == 0 && m_pThumbnails->Loading.count(sPath) == 0) {
					m_pThumbnails->Loading.insert(sPath);
					paths.push_back(sPath);
				}
			}
		}
	}
	if (paths.empty()) return;
	std::shared_ptr<CThumbnails> pThumbnails = m_pThumbnails;
	HWND hWnd = m_hWnd;
	int nSize = S(320);
	std::thread([pThumbnails, paths, hWnd, nSize]() {
		HRESULT hr = ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
		for (const CString& sPath : paths) {
			if (pThumbnails->Cancel) break;
			std::shared_ptr<Bitmap> pBitmap;
			IStartScreenImageFactory* pFactory = NULL;
			if (SUCCEEDED(CreateShellItem(sPath, __uuidof(IStartScreenImageFactory), (void**)&pFactory))) {
				HBITMAP hBitmap = NULL;
				SIZE size = { nSize, nSize };
				if (SUCCEEDED(pFactory->GetImage(size, IMAGE_FACTORY_BIGGERSIZEOK, &hBitmap)) && hBitmap != NULL) {
					pBitmap = ToBitmap(hBitmap);
					::DeleteObject(hBitmap);
				}
				pFactory->Release();
			}
			{
				std::lock_guard<std::mutex> lock(pThumbnails->Lock);
				pThumbnails->Images[sPath] = pBitmap;
				pThumbnails->Loading.erase(sPath);
			}
			if (hWnd != NULL) ::InvalidateRect(hWnd, NULL, FALSE);
		}
		if (SUCCEEDED(hr)) ::CoUninitialize();
	}).detach();
}

///////////////////////////////////////////////////////////////////////////////////
// Start screen
///////////////////////////////////////////////////////////////////////////////////

CStartScreen::CStartScreen(HWND hWnd) : m_hWnd(hWnd), m_eHoverKind(HIT_NONE), m_nHoverIndex(-1), m_nScroll(0), m_nRecentScroll(0),
	m_nContentHeight(0), m_nRecentRowWidth(0), m_nRecentVisibleWidth(0), m_clientRect(0, 0, 0, 0), m_sPinShortcut(_T("Ctrl+B")) {
	HDC hDC = ::GetDC(NULL);
	m_nDpi = ::GetDeviceCaps(hDC, LOGPIXELSX);
	::ReleaseDC(NULL, hDC);
	m_pThumbnails = std::make_shared<CThumbnails>();
}

CStartScreen::~CStartScreen() {
	m_pThumbnails->Cancel = true;
}

void CStartScreen::Reload() {
	m_recent.clear();
	m_recentTimes.clear();
	m_pinned.clear();
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
	m_nScroll = 0;
	m_nRecentScroll = 0;
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
		g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
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

		// ---- last opened photos in a row, scrolled with the mouse wheel
		if (!m_recent.empty()) {
			PaintText(g, CNLS::GetString(_T("Recently opened")), sectionFont, colors.Text, RectF(x0, y, fWidth, (REAL)S(26)));
			CString sClear = CNLS::GetString(_T("Clear list"));
			float fClearWidth = TextWidth(g, sClear, hintFont) + S(4);
			RectF clearRect(x0 + fWidth - fClearWidth, y + S(4), fClearWidth, (REAL)S(20));
			PaintText(g, sClear, hintFont, IsHover(HIT_CLEAR_RECENT, 0) ? colors.Text : colors.Accent, clearRect, StringAlignmentFar);
			AddHit(clearRect, HIT_CLEAR_RECENT, 0);
			y += S(38);
			float fCardWidth = (REAL)S(232), fThumbHeight = (REAL)S(156), fGap = (REAL)S(16);
			float fRowHeight = fThumbHeight + S(8) + S(20) + S(18);
			m_nRecentRowWidth = (int)(m_recent.size() * (fCardWidth + fGap) - fGap);
			m_nRecentVisibleWidth = (int)fWidth;
			m_nRecentScroll = max(0, min(m_nRecentScroll, m_nRecentRowWidth - m_nRecentVisibleWidth));
			RectF rowRect(x0, y, fWidth, fRowHeight);
			AddHit(rowRect, HIT_RECENT_ROW, 0);
			GraphicsState state = g.Save();
			g.SetClip(RectF(x0 - S(2), y - S(2), fWidth + S(4), fRowHeight + S(4)), CombineModeIntersect);
			for (int i = 0; i < (int)m_recent.size(); i++) {
				float fX = x0 + i * (fCardWidth + fGap) - m_nRecentScroll;
				if (fX + fCardWidth < x0 || fX > x0 + fWidth) continue;
				RectF thumbRect(fX, y, fCardWidth, fThumbHeight);
				std::shared_ptr<Bitmap> pBitmap;
				{
					std::lock_guard<std::mutex> lock(m_pThumbnails->Lock);
					auto it = m_pThumbnails->Images.find(m_recent[i]);
					if (it != m_pThumbnails->Images.end()) pBitmap = it->second;
				}
				if (pBitmap) {
					FillRound(g, thumbRect, (REAL)S(8), colors.Control);
					DrawCover(g, pBitmap.get(), thumbRect, (REAL)S(8));
				} else {
					FillRound(g, thumbRect, (REAL)S(8), colors.Control);
					PaintText(g, L"\xE91B", addIconFont, colors.SubText, thumbRect, StringAlignmentCenter, StringAlignmentCenter);
				}
				bool bHover = IsHover(HIT_RECENT, i);
				if (bHover) {
					DrawRound(g, RectF(thumbRect.X + 1, thumbRect.Y + 1, thumbRect.Width - 2, thumbRect.Height - 2), (REAL)S(8), colors.Accent, (REAL)S(2));
				} else {
					DrawRound(g, thumbRect, (REAL)S(8), colors.Border, 1.0f);
				}
				CString sName = ::PathFindFileName(m_recent[i]);
				PaintText(g, sName, textFont, colors.Text, RectF(fX + S(2), y + fThumbHeight + S(8), fCardWidth - S(4), (REAL)S(20)));
				CString sMeta = FormatOpenTime(m_recentTimes[i]);
				CString sFolder = FolderName(m_recent[i]);
				if (!sFolder.IsEmpty()) sMeta += (sMeta.IsEmpty() ? _T("") : _T(" \x00B7 ")) + sFolder;
				PaintText(g, sMeta, smallFont, colors.SubText, RectF(fX + S(2), y + fThumbHeight + S(28), fCardWidth - S(4), (REAL)S(18)));
				RectF cardRect(max(fX, x0), y, min(fX + fCardWidth, x0 + fWidth) - max(fX, x0), fRowHeight);
				AddHit(cardRect, HIT_RECENT, i);
			}
			g.Restore(state);
			// the row can be scrolled: small marks at the cut side
			if (m_nRecentRowWidth > m_nRecentVisibleWidth) {
				float fTrack = fWidth, fThumb = max((float)S(40), fTrack * m_nRecentVisibleWidth / m_nRecentRowWidth);
				float fPos = (fTrack - fThumb) * m_nRecentScroll / max(1, m_nRecentRowWidth - m_nRecentVisibleWidth);
				FillRound(g, RectF(x0, y + fRowHeight + S(8), fTrack, (REAL)S(3)), (REAL)S(1), colors.Control);
				FillRound(g, RectF(x0 + fPos, y + fRowHeight + S(8), fThumb, (REAL)S(3)), (REAL)S(1), colors.SubText);
			}
			y += fRowHeight + S(40);
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

		// scroll position
		if (m_nContentHeight > nH) {
			float fThumb = max((float)S(40), (float)nH * nH / m_nContentHeight);
			float fPos = (nH - fThumb) * m_nScroll / max(1, m_nContentHeight - nH);
			FillRound(g, RectF((REAL)(nW - S(8)), fPos + S(2), (REAL)S(4), fThumb - S(4)), (REAL)S(2), colors.Border);
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
		if (m_hits[i].Kind != HIT_RECENT_ROW && m_hits[i].Rect.PtInRect(CPoint(nX, nY))) return &m_hits[i];
	}
	return NULL;
}

bool CStartScreen::IsOverItem(int nX, int nY) {
	return HitTest(nX, nY) != NULL;
}

bool CStartScreen::OnMouseMove(int nX, int nY) {
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

bool CStartScreen::OnMouseWheel(int nX, int nY, int nDelta) {
	for (const CHit& hit : m_hits) {
		if (hit.Kind == HIT_RECENT_ROW && hit.Rect.PtInRect(CPoint(nX, nY)) && m_nRecentRowWidth > m_nRecentVisibleWidth) {
			int nOld = m_nRecentScroll;
			m_nRecentScroll = max(0, min(m_nRecentScroll - nDelta * S(248) / WHEEL_DELTA, m_nRecentRowWidth - m_nRecentVisibleWidth));
			return nOld != m_nRecentScroll;
		}
	}
	int nOld = m_nScroll;
	m_nScroll = max(0, min(m_nScroll - nDelta * S(100) / WHEEL_DELTA, m_nContentHeight - m_clientRect.Height()));
	return nOld != m_nScroll;
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
	m_nScroll = nScroll;
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
	if (pHit == NULL || (pHit->Kind != HIT_RECENT && pHit->Kind != HIT_PINNED && pHit->Kind != HIT_UNPIN)) return result;
	bool bRecent = pHit->Kind == HIT_RECENT;
	int nIndex = pHit->Index;
	CString sFile = bRecent ? m_recent[nIndex] : m_pinned[nIndex];
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
