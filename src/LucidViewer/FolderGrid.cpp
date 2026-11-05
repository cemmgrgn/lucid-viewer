#include "StdAfx.h"
#include "FolderGrid.h"
#include "NLS.h"
#include "DarkUI.h"
#include <gdiplus.h>
#include <shobjidl.h>
#include <map>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <algorithm>

using namespace Gdiplus;

// The project targets an old _WIN32_IE, the thumbnail interface of Windows Vista is declared here (as in StartScreen.cpp)
MIDL_INTERFACE("bcc18b79-ba16-442f-80c4-8a59c30c463b")
IFolderGridImageFactory : public IUnknown {
public:
	virtual HRESULT STDMETHODCALLTYPE GetImage(SIZE size, int nFlags, HBITMAP* phBitmap) = 0;
};
static const int GRID_IMAGE_FACTORY_BIGGERSIZEOK = 0x1;

static const int TILE_SIZE = 150;  // thumbnail area at 96 DPI
static const int LABEL_HEIGHT = 22;
static const int GAP = 10;
static const int HEADER_HEIGHT = 40;
static const int PADDING = 20;
static const size_t MAX_CACHED_THUMBNAILS = 1500;

///////////////////////////////////////////////////////////////////////////////////
// Thumbnails, loaded by the shell in background threads (only the visible ones are requested)
///////////////////////////////////////////////////////////////////////////////////

struct CFolderGrid::CThumbnails {
	std::mutex Lock;
	std::condition_variable Wake;
	std::deque<CString> Queue;
	std::set<CString> Loading;
	std::map<CString, std::shared_ptr<Bitmap>> Images; // NULL: no thumbnail
	std::atomic<bool> Cancel;
	HWND Window;
	int Size;
	CThumbnails() : Cancel(false), Window(NULL), Size(150) {}
};

// Copy of the shell bitmap, top-down and premultiplied (see StartScreen.cpp)
static std::shared_ptr<Bitmap> GridToBitmap(HBITMAP hBitmap) {
	BITMAP info;
	if (::GetObject(hBitmap, sizeof(info), &info) == 0 || info.bmWidth <= 0 || info.bmHeight == 0) return NULL;
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
		for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
	}
	std::shared_ptr<Bitmap> pBitmap(new Bitmap(nWidth, nHeight, PixelFormat32bppPARGB));
	BitmapData data;
	Rect rect(0, 0, nWidth, nHeight);
	if (pBitmap->GetLastStatus() != Ok || pBitmap->LockBits(&rect, ImageLockModeWrite, PixelFormat32bppPARGB, &data) != Ok) return NULL;
	for (int y = 0; y < nHeight; y++) {
		memcpy((BYTE*)data.Scan0 + (size_t)y * data.Stride, &pixels[(size_t)y * nWidth * 4], (size_t)nWidth * 4);
	}
	pBitmap->UnlockBits(&data);
	return pBitmap;
}

static std::shared_ptr<Bitmap> LoadGridThumbnail(const CString& sPath, int nSize) {
	typedef HRESULT (STDAPICALLTYPE *CreateItemFn)(PCWSTR, IBindCtx*, REFIID, void**);
	static CreateItemFn createItem = (CreateItemFn)::GetProcAddress(::GetModuleHandle(_T("shell32.dll")), "SHCreateItemFromParsingName");
	std::shared_ptr<Bitmap> pBitmap;
	IFolderGridImageFactory* pFactory = NULL;
	if (createItem != NULL && SUCCEEDED(createItem(sPath, NULL, __uuidof(IFolderGridImageFactory), (void**)&pFactory)) && pFactory != NULL) {
		HBITMAP hBitmap = NULL;
		SIZE size = { nSize, nSize };
		if (SUCCEEDED(pFactory->GetImage(size, GRID_IMAGE_FACTORY_BIGGERSIZEOK, &hBitmap)) && hBitmap != NULL) {
			pBitmap = GridToBitmap(hBitmap);
			::DeleteObject(hBitmap);
		}
		pFactory->Release();
	}
	return pBitmap;
}

static void ThumbnailWorker(std::shared_ptr<CFolderGrid::CThumbnails> pThumbnails) {
	HRESULT hr = ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	for (;;) {
		CString sPath;
		int nSize;
		{
			std::unique_lock<std::mutex> lock(pThumbnails->Lock);
			pThumbnails->Wake.wait(lock, [&]() { return pThumbnails->Cancel || !pThumbnails->Queue.empty(); });
			if (pThumbnails->Cancel) break;
			sPath = pThumbnails->Queue.front();
			pThumbnails->Queue.pop_front();
			if (pThumbnails->Images.count(sPath) != 0 || pThumbnails->Loading.count(sPath) != 0) continue;
			pThumbnails->Loading.insert(sPath);
			nSize = pThumbnails->Size;
		}
		std::shared_ptr<Bitmap> pBitmap = LoadGridThumbnail(sPath, nSize);
		HWND hWnd = NULL;
		{
			std::lock_guard<std::mutex> lock(pThumbnails->Lock);
			pThumbnails->Loading.erase(sPath);
			if (pThumbnails->Cancel) break;
			if (pThumbnails->Images.size() >= MAX_CACHED_THUMBNAILS) pThumbnails->Images.clear();
			pThumbnails->Images[sPath] = pBitmap;
			hWnd = pThumbnails->Window;
		}
		if (hWnd != NULL) ::InvalidateRect(hWnd, NULL, FALSE);
	}
	if (SUCCEEDED(hr)) ::CoUninitialize();
}

///////////////////////////////////////////////////////////////////////////////////
// Grid
///////////////////////////////////////////////////////////////////////////////////

CFolderGrid::CFolderGrid(HWND hWnd) : m_hWnd(hWnd), m_bGroupByMonth(false), m_nFocus(-1), m_nAnchor(-1), m_nHover(-1),
	m_nScroll(0), m_nContentHeight(0), m_clientRect(0, 0, 0, 0), m_nLayoutWidth(-1), m_nColumns(1) {
	HDC hDC = ::GetDC(NULL);
	m_nDpi = ::GetDeviceCaps(hDC, LOGPIXELSX);
	::ReleaseDC(NULL, hDC);
	m_pThumbnails = std::make_shared<CThumbnails>();
	m_pThumbnails->Window = hWnd;
	m_pThumbnails->Size = S(TILE_SIZE);
	unsigned nThreads = max(1u, min(3u, std::thread::hardware_concurrency()));
	for (unsigned i = 0; i < nThreads; i++) {
		std::thread(ThumbnailWorker, m_pThumbnails).detach();
	}
}

CFolderGrid::~CFolderGrid() {
	std::lock_guard<std::mutex> lock(m_pThumbnails->Lock);
	m_pThumbnails->Cancel = true;
	m_pThumbnails->Window = NULL;
	m_pThumbnails->Queue.clear();
	// the GDI+ bitmaps are released here, not by a worker that may end after GDI+ was shut down
	m_pThumbnails->Images.clear();
	m_pThumbnails->Wake.notify_all();
}

void CFolderGrid::SetItems(const std::vector<CItem>& items, bool bGroupByMonth, LPCTSTR sFocus) {
	m_items = items;
	m_bGroupByMonth = bGroupByMonth;
	m_selection.clear();
	m_nFocus = m_nAnchor = m_nHover = -1;
	m_nLayoutWidth = -1;
	m_nScroll = 0;
	for (int i = 0; i < (int)m_items.size(); i++) {
		if (sFocus != NULL && m_items[i].Path.CompareNoCase(sFocus) == 0) {
			m_nFocus = m_nAnchor = i;
			m_selection.insert(i);
			break;
		}
	}
	if (m_nFocus < 0 && !m_items.empty()) m_nFocus = m_nAnchor = 0;
	if (m_clientRect.Width() > 0) {
		Layout(m_clientRect.Width());
		EnsureVisible(m_nFocus);
	}
}

static CString MonthText(__int64 nDate) {
	FILETIME utc = { (DWORD)(nDate & 0xFFFFFFFF), (DWORD)(nDate >> 32) };
	FILETIME local;
	SYSTEMTIME time;
	if (nDate <= 0 || !::FileTimeToLocalFileTime(&utc, &local) || !::FileTimeToSystemTime(&local, &time)) {
		return CNLS::GetString(_T("Unknown date"));
	}
	wchar_t sText[64] = { 0 };
	::GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &time, L"MMMM yyyy", sText, 64, NULL);
	return CString(sText);
}

static int MonthKey(__int64 nDate) {
	FILETIME utc = { (DWORD)(nDate & 0xFFFFFFFF), (DWORD)(nDate >> 32) };
	FILETIME local;
	SYSTEMTIME time;
	if (nDate <= 0 || !::FileTimeToLocalFileTime(&utc, &local) || !::FileTimeToSystemTime(&local, &time)) return 0;
	return time.wYear * 100 + time.wMonth;
}

void CFolderGrid::Layout(int nWidth) {
	m_nLayoutWidth = nWidth;
	m_itemRects.assign(m_items.size(), CRect(0, 0, 0, 0));
	m_headers.clear();
	int nTile = S(TILE_SIZE), nGap = S(GAP), nPad = S(PADDING), nLabel = S(LABEL_HEIGHT);
	m_nColumns = max(1, (nWidth - 2 * nPad + nGap) / (nTile + nGap));
	int y = nPad;
	int nColumn = 0;
	int nLastMonth = -1;
	for (int i = 0; i < (int)m_items.size(); i++) {
		if (m_bGroupByMonth) {
			int nMonth = MonthKey(m_items[i].Date);
			if (nMonth != nLastMonth) {
				if (nColumn > 0) y += nTile + nLabel + nGap;
				nColumn = 0;
				CHeader header = { y, MonthText(m_items[i].Date) };
				m_headers.push_back(header);
				y += S(HEADER_HEIGHT);
				nLastMonth = nMonth;
			}
		}
		int x = nPad + nColumn * (nTile + nGap);
		m_itemRects[i] = CRect(x, y, x + nTile, y + nTile + nLabel);
		if (++nColumn >= m_nColumns) {
			nColumn = 0;
			y += nTile + nLabel + nGap;
		}
	}
	if (nColumn > 0) y += nTile + nLabel + nGap;
	m_nContentHeight = y + nPad;
}

void CFolderGrid::ClampScroll() {
	m_nScroll = max(0, min(m_nScroll, m_nContentHeight - m_clientRect.Height()));
}

void CFolderGrid::EnsureVisible(int nIndex) {
	if (nIndex < 0 || nIndex >= (int)m_itemRects.size()) return;
	const CRect& rect = m_itemRects[nIndex];
	if (rect.top - S(8) < m_nScroll) {
		m_nScroll = rect.top - S(HEADER_HEIGHT);
	} else if (rect.bottom + S(8) > m_nScroll + m_clientRect.Height()) {
		m_nScroll = rect.bottom + S(8) - m_clientRect.Height();
	}
	ClampScroll();
}

int CFolderGrid::HitTest(int nX, int nY) const {
	if (m_nLayoutWidth < 0) return -1;
	int x = nX - m_clientRect.left, y = nY - m_clientRect.top + m_nScroll;
	for (int i = 0; i < (int)m_itemRects.size(); i++) {
		if (m_itemRects[i].PtInRect(CPoint(x, y))) return i;
	}
	return -1;
}

static Color GridColor(COLORREF color, BYTE nAlpha = 255) {
	return Color(nAlpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

void CFolderGrid::Paint(HDC hDC, const CRect& clientRect) {
	bool bSizeChanged = clientRect.Width() != m_clientRect.Width() || clientRect.Height() != m_clientRect.Height();
	m_clientRect = clientRect;
	int nW = clientRect.Width(), nH = clientRect.Height();
	if (nW <= 0 || nH <= 0) return;
	if (m_nLayoutWidth != nW) {
		Layout(nW);
		if (bSizeChanged) EnsureVisible(m_nFocus);
	}
	ClampScroll();

	HDC hMemDC = ::CreateCompatibleDC(hDC);
	HBITMAP hBitmap = ::CreateCompatibleBitmap(hDC, nW, nH);
	HGDIOBJ hOldBitmap = ::SelectObject(hMemDC, hBitmap);
	std::vector<CString> missing;
	{
		Graphics g(hMemDC);
		g.SetSmoothingMode(SmoothingModeAntiAlias);
		g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
		g.SetInterpolationMode(InterpolationModeHighQualityBilinear);
		g.SetPixelOffsetMode(PixelOffsetModeHalf);
		const DarkUI::CPalette& colors = DarkUI::Colors();
		g.Clear(GridColor(colors.Window));
		FontFamily family(L"Segoe UI"), semibold(L"Segoe UI Semibold");
		Font labelFont(&family, (REAL)S(12), FontStyleRegular, UnitPixel);
		Font headerFont(&semibold, (REAL)S(17), FontStyleRegular, UnitPixel);
		Font checkFont(&semibold, (REAL)S(13), FontStyleRegular, UnitPixel);
		StringFormat labelFormat;
		labelFormat.SetAlignment(StringAlignmentCenter);
		labelFormat.SetLineAlignment(StringAlignmentCenter);
		labelFormat.SetTrimming(StringTrimmingEllipsisCharacter);
		labelFormat.SetFormatFlags(StringFormatFlagsNoWrap);
		SolidBrush textBrush(GridColor(colors.Text)), subBrush(GridColor(colors.SubText));

		if (m_items.empty()) {
			RectF all(0, 0, (REAL)nW, (REAL)nH);
			g.DrawString(CNLS::GetString(_T("No images in this folder")), -1, &headerFont, all, &labelFormat, &subBrush);
		}
		for (const CHeader& header : m_headers) {
			int y = header.Y - m_nScroll;
			if (y + S(HEADER_HEIGHT) < 0 || y > nH) continue;
			RectF rect((REAL)S(PADDING), (REAL)y, (REAL)(nW - 2 * S(PADDING)), (REAL)S(HEADER_HEIGHT) - S(8));
			StringFormat headerFormat;
			headerFormat.SetLineAlignment(StringAlignmentCenter);
			g.DrawString(header.Text, -1, &headerFont, rect, &headerFormat, &textBrush);
		}

		std::lock_guard<std::mutex> lock(m_pThumbnails->Lock);
		for (int i = 0; i < (int)m_items.size(); i++) {
			CRect rect = m_itemRects[i];
			rect.OffsetRect(0, -m_nScroll);
			if (rect.bottom < 0 || rect.top > nH) continue;
			bool bSelected = m_selection.count(i) != 0;
			RectF tile((REAL)rect.left, (REAL)rect.top, (REAL)rect.Width(), (REAL)S(TILE_SIZE));
			if (bSelected || i == m_nHover) {
				SolidBrush back(GridColor(bSelected ? colors.Selection : colors.ControlHot));
				g.FillRectangle(&back, (REAL)rect.left - S(4), (REAL)rect.top - S(4), (REAL)rect.Width() + S(8), (REAL)rect.Height() + S(6));
			}
			auto iter = m_pThumbnails->Images.find(m_items[i].Path);
			if (iter == m_pThumbnails->Images.end()) {
				missing.push_back(m_items[i].Path);
				SolidBrush placeholder(GridColor(colors.Control));
				g.FillRectangle(&placeholder, tile);
			} else if (iter->second) {
				// the whole thumbnail, centered
				Bitmap* pBitmap = iter->second.get();
				float fWidth = (float)pBitmap->GetWidth(), fHeight = (float)pBitmap->GetHeight();
				float fScale = min(tile.Width / fWidth, tile.Height / fHeight);
				RectF target(tile.X + (tile.Width - fWidth * fScale) / 2, tile.Y + (tile.Height - fHeight * fScale) / 2, fWidth * fScale, fHeight * fScale);
				g.DrawImage(pBitmap, target);
			} else {
				SolidBrush placeholder(GridColor(colors.Control));
				g.FillRectangle(&placeholder, tile);
			}
			LPCTSTR sName = _tcsrchr(m_items[i].Path, _T('\\'));
			RectF label((REAL)rect.left, (REAL)(rect.top + S(TILE_SIZE)), (REAL)rect.Width(), (REAL)S(LABEL_HEIGHT));
			g.DrawString(sName != NULL ? sName + 1 : (LPCTSTR)m_items[i].Path, -1, &labelFont, label, &labelFormat, &textBrush);
			if (bSelected) {
				// check mark in the corner
				RectF mark((REAL)rect.left + S(6), (REAL)rect.top + S(6), (REAL)S(20), (REAL)S(20));
				SolidBrush accent(GridColor(colors.Accent));
				g.FillEllipse(&accent, mark);
				SolidBrush accentText(GridColor(colors.AccentText));
				g.DrawString(L"\x2713", -1, &checkFont, mark, &labelFormat, &accentText);
			}
			if (i == m_nFocus) {
				Pen focusPen(GridColor(colors.Accent), (REAL)max(1, S(2)));
				g.DrawRectangle(&focusPen, (REAL)rect.left - S(4), (REAL)rect.top - S(4), (REAL)rect.Width() + S(8), (REAL)rect.Height() + S(6));
			}
		}
		// scroll position
		if (m_nContentHeight > nH) {
			int nThumb = max(S(30), nH * nH / m_nContentHeight);
			int nTop = (nH - nThumb) * m_nScroll / max(1, m_nContentHeight - nH);
			SolidBrush thumb(GridColor(colors.Border));
			g.FillRectangle(&thumb, (REAL)(nW - S(6)), (REAL)nTop, (REAL)S(4), (REAL)nThumb);
		}
	}
	::BitBlt(hDC, clientRect.left, clientRect.top, nW, nH, hMemDC, 0, 0, SRCCOPY);
	::SelectObject(hMemDC, hOldBitmap);
	::DeleteObject(hBitmap);
	::DeleteDC(hMemDC);
	if (!missing.empty()) RequestThumbnails(missing);
}

void CFolderGrid::RequestThumbnails(const std::vector<CString>& files) {
	std::lock_guard<std::mutex> lock(m_pThumbnails->Lock);
	// only the visible ones are loaded: the queue is replaced
	m_pThumbnails->Queue.clear();
	for (const CString& sFile : files) {
		if (m_pThumbnails->Loading.count(sFile) == 0) m_pThumbnails->Queue.push_back(sFile);
	}
	m_pThumbnails->Wake.notify_all();
}

bool CFolderGrid::OnMouseMove(int nX, int nY) {
	int nHover = HitTest(nX, nY);
	if (nHover == m_nHover) return false;
	m_nHover = nHover;
	return true;
}

void CFolderGrid::SelectRange(int nFrom, int nTo, bool bAdd) {
	if (!bAdd) m_selection.clear();
	for (int i = min(nFrom, nTo); i <= max(nFrom, nTo); i++) {
		if (i >= 0 && i < (int)m_items.size()) m_selection.insert(i);
	}
}

CFolderGrid::EAction CFolderGrid::OnClick(int nX, int nY, bool bCtrl, bool bShift) {
	int nIndex = HitTest(nX, nY);
	if (nIndex < 0) {
		if (!bCtrl && !bShift && !m_selection.empty()) {
			m_selection.clear();
			return ACTION_REPAINT;
		}
		return ACTION_NONE;
	}
	if (bShift && m_nAnchor >= 0) {
		SelectRange(m_nAnchor, nIndex, bCtrl);
	} else if (bCtrl) {
		if (m_selection.count(nIndex) != 0) m_selection.erase(nIndex);
		else m_selection.insert(nIndex);
		m_nAnchor = nIndex;
	} else {
		m_selection.clear();
		m_selection.insert(nIndex);
		m_nAnchor = nIndex;
	}
	m_nFocus = nIndex;
	return ACTION_REPAINT;
}

CFolderGrid::EAction CFolderGrid::OnDoubleClick(int nX, int nY) {
	int nIndex = HitTest(nX, nY);
	if (nIndex < 0) return ACTION_NONE;
	m_nFocus = nIndex;
	return ACTION_OPEN;
}

bool CFolderGrid::OnRightClick(int nX, int nY) {
	int nIndex = HitTest(nX, nY);
	if (nIndex < 0) return false;
	if (m_selection.count(nIndex) == 0) {
		m_selection.clear();
		m_selection.insert(nIndex);
		m_nAnchor = nIndex;
	}
	m_nFocus = nIndex;
	return true;
}

bool CFolderGrid::OnMouseWheel(int nDelta) {
	int nOld = m_nScroll;
	m_nScroll -= nDelta * S(TILE_SIZE + LABEL_HEIGHT + GAP) / WHEEL_DELTA;
	ClampScroll();
	return m_nScroll != nOld;
}

int CFolderGrid::ItemInNextRow(int nIndex, int nDirection) const {
	if (nIndex < 0 || nIndex >= (int)m_itemRects.size()) return nIndex;
	const CRect& current = m_itemRects[nIndex];
	int nBest = nIndex, nBestDistance = 0x7FFFFFFF;
	int nTargetRowTop = -1;
	for (int i = nIndex + nDirection; i >= 0 && i < (int)m_itemRects.size(); i += nDirection) {
		const CRect& rect = m_itemRects[i];
		if (rect.top == current.top) continue;
		if (nTargetRowTop < 0) nTargetRowTop = rect.top;
		if (rect.top != nTargetRowTop) break;
		int nDistance = abs(rect.left - current.left);
		if (nDistance < nBestDistance) {
			nBestDistance = nDistance;
			nBest = i;
		}
	}
	return nBest;
}

void CFolderGrid::MoveFocus(int nNewFocus, bool bShift, bool bCtrl) {
	if (m_items.empty()) return;
	nNewFocus = max(0, min((int)m_items.size() - 1, nNewFocus));
	if (bShift) {
		if (m_nAnchor < 0) m_nAnchor = m_nFocus >= 0 ? m_nFocus : nNewFocus;
		SelectRange(m_nAnchor, nNewFocus, false);
	} else if (!bCtrl) {
		m_selection.clear();
		m_selection.insert(nNewFocus);
		m_nAnchor = nNewFocus;
	}
	m_nFocus = nNewFocus;
	EnsureVisible(m_nFocus);
}

CFolderGrid::EAction CFolderGrid::OnKey(int nVirtualKey, bool bCtrl, bool bShift) {
	int nFocus = m_nFocus < 0 ? 0 : m_nFocus;
	int nPage = max(1, m_clientRect.Height() / max(1, S(TILE_SIZE + LABEL_HEIGHT + GAP))) * m_nColumns;
	switch (nVirtualKey) {
		case VK_LEFT: MoveFocus(nFocus - 1, bShift, bCtrl); return ACTION_REPAINT;
		case VK_RIGHT: MoveFocus(nFocus + 1, bShift, bCtrl); return ACTION_REPAINT;
		case VK_UP: MoveFocus(ItemInNextRow(nFocus, -1), bShift, bCtrl); return ACTION_REPAINT;
		case VK_DOWN: MoveFocus(ItemInNextRow(nFocus, 1), bShift, bCtrl); return ACTION_REPAINT;
		case VK_PRIOR: MoveFocus(nFocus - nPage, bShift, bCtrl); return ACTION_REPAINT;
		case VK_NEXT: MoveFocus(nFocus + nPage, bShift, bCtrl); return ACTION_REPAINT;
		case VK_HOME: MoveFocus(0, bShift, bCtrl); return ACTION_REPAINT;
		case VK_END: MoveFocus((int)m_items.size() - 1, bShift, bCtrl); return ACTION_REPAINT;
		case VK_SPACE:
			if (m_nFocus >= 0) {
				if (m_selection.count(m_nFocus) != 0 && bCtrl) m_selection.erase(m_nFocus);
				else m_selection.insert(m_nFocus);
			}
			return ACTION_REPAINT;
		case VK_RETURN:
			return m_nFocus >= 0 ? ACTION_OPEN : ACTION_NONE;
		case VK_ESCAPE:
			if (m_selection.size() > 1) {
				m_selection.clear();
				if (m_nFocus >= 0) m_selection.insert(m_nFocus);
				return ACTION_REPAINT;
			}
			return ACTION_CLOSE;
		case 'A':
			if (bCtrl) {
				SelectAll();
				return ACTION_REPAINT;
			}
			break;
	}
	return ACTION_NONE;
}

void CFolderGrid::SelectAll() {
	for (int i = 0; i < (int)m_items.size(); i++) m_selection.insert(i);
}

std::vector<CString> CFolderGrid::GetSelection() const {
	std::vector<CString> files;
	for (int nIndex : m_selection) {
		if (nIndex >= 0 && nIndex < (int)m_items.size()) files.push_back(m_items[nIndex].Path);
	}
	return files;
}

CString CFolderGrid::GetFocusedFile() const {
	return (m_nFocus >= 0 && m_nFocus < (int)m_items.size()) ? m_items[m_nFocus].Path : CString();
}
