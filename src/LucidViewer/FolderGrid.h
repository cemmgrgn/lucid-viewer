#pragma once

#include <vector>
#include <set>
#include <memory>

// Grid of the thumbnails of the current folder (or list), drawn over the image area like the start screen.
// Several images can be selected (click, Ctrl+click, Shift+click, Ctrl+A, Shift+arrow keys); the main window runs the
// actions on the selection (rating, tags, copy, move, recycle bin, convert, rename, compare). When the images are
// sorted by the date taken, they are grouped by month (timeline).
class CFolderGrid {
public:
	enum EAction { ACTION_NONE, ACTION_REPAINT, ACTION_OPEN, ACTION_CLOSE };
	struct CItem {
		CString Path;
		__int64 Date; // FILETIME of the date taken (used for the month groups), 0 if not grouped
	};

	CFolderGrid(HWND hWnd);
	~CFolderGrid();

	// Sets the images, the item with the path sFocus gets the focus and is selected
	void SetItems(const std::vector<CItem>& items, bool bGroupByMonth, LPCTSTR sFocus);
	int GetCount() const { return (int)m_items.size(); }

	void Paint(HDC hDC, const CRect& clientRect);
	// Returns true if a repaint is needed
	bool OnMouseMove(int nX, int nY);
	EAction OnClick(int nX, int nY, bool bCtrl, bool bShift);
	EAction OnDoubleClick(int nX, int nY);
	// Right click: an item that is not selected becomes the only selected one. Returns false if not over an item.
	bool OnRightClick(int nX, int nY);
	bool OnMouseWheel(int nDelta);
	EAction OnKey(int nVirtualKey, bool bCtrl, bool bShift);
	bool IsOverItem(int nX, int nY) const { return HitTest(nX, nY) >= 0; }

	void SelectAll();
	// Selected files in the order of the grid
	std::vector<CString> GetSelection() const;
	int GetSelectionCount() const { return (int)m_selection.size(); }
	// File with the keyboard focus (opened with Enter), empty if none
	CString GetFocusedFile() const;

	// thumbnail cache shared with the loading threads
	struct CThumbnails;

private:
	struct CHeader {
		int Y;
		CString Text;
	};

	HWND m_hWnd;
	int m_nDpi;
	std::vector<CItem> m_items;
	bool m_bGroupByMonth;
	std::set<int> m_selection;
	int m_nFocus, m_nAnchor, m_nHover;
	int m_nScroll, m_nContentHeight;
	CRect m_clientRect;
	// layout in content coordinates (without scrolling), made for m_nLayoutWidth
	int m_nLayoutWidth, m_nColumns;
	std::vector<CRect> m_itemRects;
	std::vector<CHeader> m_headers;
	std::shared_ptr<CThumbnails> m_pThumbnails;

	int S(int n) const { return ::MulDiv(n, m_nDpi, 96); }
	void Layout(int nWidth);
	int HitTest(int nX, int nY) const;
	void EnsureVisible(int nIndex);
	void ClampScroll();
	void SelectRange(int nFrom, int nTo, bool bAdd);
	void MoveFocus(int nNewFocus, bool bShift, bool bCtrl);
	// index of the item in the row above/below (nDirection -1/+1), the same column as far as possible
	int ItemInNextRow(int nIndex, int nDirection) const;
	void RequestThumbnails(const std::vector<CString>& files);
};
