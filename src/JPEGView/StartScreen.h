#pragma once

#include <vector>
#include <memory>

// Start screen (gallery) shown when no image is open: the last opened photos in a row, the screenshots in a row
// (if the screenshot history is on) and below the photos the user pinned. The lists are stored as text files in the
// folder of the user INI file.
class CStartScreen {
public:
	enum EAction { ACTION_NONE, ACTION_OPEN_FILE, ACTION_OPEN_DIALOG, ACTION_OPEN_FOLDER, ACTION_PASTE, ACTION_SCREENSHOT };
	struct CResult {
		EAction Action;
		CString File;
	};

	CStartScreen(HWND hWnd);
	~CStartScreen();

	// Lists of all start screens
	static void AddRecent(LPCTSTR sFile);
	static bool IsPinned(LPCTSTR sFile);
	static void SetPinned(LPCTSTR sFile, bool bPinned);
	// UI test: lists used instead of the stored ones
	static void SetTestLists(const std::vector<CString>& recent, const std::vector<CString>& pinned, const std::vector<CString>& screenshots);

	// Reads the lists again and loads the missing thumbnails in the background
	void Reload();
	// Shortcut shown in the hint for pinning (key map)
	void SetPinShortcut(LPCTSTR sShortcut) { m_sPinShortcut = sShortcut; }

	void Paint(HDC hDC, const CRect& clientRect);
	// Returns true if the hover state changed (repaint)
	bool OnMouseMove(int nX, int nY);
	void OnMouseLeave();
	CResult OnClick(int nX, int nY);
	CResult OnContextMenu(HWND hWnd, int nX, int nY, POINT screenPoint);
	// Returns true if scrolled (repaint)
	bool OnMouseWheel(int nX, int nY, int nDelta);
	// Arrow keys scroll the rows, up/down and page keys the page. Returns true if the key was used (repaint)
	bool OnKey(int nVirtualKey);
	bool IsOverItem(int nX, int nY);

private:
	enum EHitKind { HIT_NONE, HIT_BUTTON, HIT_CLEAR_RECENT, HIT_RECENT, HIT_ADD_PINNED, HIT_PINNED, HIT_UNPIN, HIT_ROW,
		HIT_SHOT, HIT_SHOT_FOLDER, HIT_ROW_PREV, HIT_ROW_NEXT };
	enum { ROW_RECENT = 0, ROW_SHOTS = 1, ROW_COUNT = 2 };
	struct CHit {
		CRect Rect;
		EHitKind Kind;
		int Index;
	};
	struct CThumbnails;

	HWND m_hWnd;
	int m_nDpi;
	std::vector<CString> m_recent, m_pinned, m_shots;
	std::vector<__int64> m_recentTimes, m_shotTimes; // FILETIME of opening or writing, 0 if unknown
	CString m_sPinShortcut;
	std::vector<CHit> m_hits;
	EHitKind m_eHoverKind;
	int m_nHoverIndex;
	int m_nScroll, m_nContentHeight;
	int m_nRowScroll[ROW_COUNT], m_nRowWidth[ROW_COUNT], m_nRowVisible[ROW_COUNT], m_nRowStep[ROW_COUNT];
	int m_nKeyRow; // row scrolled with the arrow keys: the one under the mouse, else the first
	CRect m_clientRect;
	std::shared_ptr<CThumbnails> m_pThumbnails;

	int S(int n) const { return ::MulDiv(n, m_nDpi, 96); }
	const CHit* HitTest(int nX, int nY) const;
	void LoadThumbnails();
	void AddPinnedWithDialog();
	bool ScrollRow(int nRow, int nPixels);
};
