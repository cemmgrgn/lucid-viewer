#pragma once

#include <vector>

class CMainDlg;

// Toolbar with the main menu (top), status bar (bottom) and information side panel (right) of the main window.
// All three are painted into the non-client area of the main window, so the client area stays the image area
// and nothing is painted over the image. Nothing is shown in full screen mode or when the window title bar is hidden.
class CWindowChrome
{
public:
	// Hit test code for the clickable toolbar items and the side panel
	enum { HT_CHROME = HTOBJECT };

	CWindowChrome(CMainDlg* pMainDlg);
	~CWindowChrome();

	// True if the chrome can be shown in the current window mode (not full screen, title bar shown)
	bool IsEnabled();

	bool IsToolbarShown() { return IsEnabled() && m_bShowToolbar; }
	bool IsStatusBarShown() { return IsEnabled() && m_bShowStatusBar; }
	bool IsSidePanelShown() { return IsEnabled() && m_bShowSidePanel; }

	// The settings independent of the window mode, as used for check marks in the menu
	bool GetShowToolbar() { return m_bShowToolbar; }
	bool GetShowStatusBar() { return m_bShowStatusBar; }
	bool GetShowSidePanel() { return m_bShowSidePanel; }
	void SetShowToolbar(bool bShow);
	void SetShowStatusBar(bool bShow);
	void SetShowSidePanel(bool bShow);

	// Size of the non-client area added by the chrome, cx: side panel, cy: toolbar plus status bar
	CSize GetExtraSize();

	// Must be called after the main window style changed (full screen, title bar), recalculates the frame
	void UpdateFrame();

	// Updates the border size used for window size calculations, call after switching full screen or title bar mode
	// before calculating the new window size
	void SyncBorderSize();

	// Non-client message handling, called by the main dialog
	// Shrinks the client rectangle (in parent coordinates) as calculated by DefWindowProc() by the chrome areas
	void OnNCCalcSize(RECT* pClientRect);
	// Paints the chrome into the window DC, call after DefWindowProc() painted the frame
	void Paint();
	// Returns the hit test code for the given screen point, 0 if the point is not on the chrome
	LRESULT HitTest(CPoint ptScreen);
	void OnNCMouseMove(CPoint ptScreen);
	void OnNCMouseLeave();
	void OnNCLButtonDown(CPoint ptScreen);
	void OnNCLButtonUp(CPoint ptScreen);
	// Returns true if the mouse wheel was used to scroll the side panel
	bool OnMouseWheel(CPoint ptScreen, int nDelta);
	bool OnTimer(int nTimerId);

	// Shows a message in the status bar for some seconds
	void ShowMessage(LPCTSTR sMessage);

	// Shows the 'Convert to' menu below the convert toolbar button, or at the mouse position if the toolbar is hidden.
	// Returns the selected command, 0 if none.
	int TrackConvertMenu();

private:
	enum EItemType { IT_Menu, IT_Button, IT_Separator };

	struct CToolbarItem {
		EItemType Type;
		CString Text; // menu title, or button text if there is no icon font
		wchar_t Glyph; // icon in the icon font
		int Command; // command for buttons, index of the menu for menu titles
		LPCTSTR Tooltip; // English, translated with CNLS
		bool Mirror; // paint the glyph mirrored horizontally
		bool NeedsImage; // disabled without image
		CRect Rect; // in window coordinates, set by UpdateLayout()
	};

	struct CColors {
		COLORREF Background, PanelBackground, Border, Text, DimText, Hover, Pressed, Checked, Accent;
	};

	CMainDlg* m_pMainDlg;
	bool m_bShowToolbar, m_bShowStatusBar, m_bShowSidePanel;
	std::vector<CToolbarItem> m_items;
	CColors m_colors;
	HFONT m_hFont, m_hBoldFont, m_hIconFont;
	bool m_bHasIconFont;
	int m_nHoverItem, m_nPressedItem;
	bool m_bTrackingMouse;
	CString m_sMessage; // temporary status bar message
	int m_nSideScroll; // scroll position of the side panel content in pixels
	int m_nSideContentHeight;
	CString m_sLocationURL;
	CRect m_locationRect; // in window coordinates

	// rectangles of the chrome parts in window coordinates
	CRect m_windowRect, m_toolbarRect, m_statusBarRect, m_sidePanelRect;

	int ToolbarHeight();
	int StatusBarHeight();
	int SidePanelWidth();
	void UpdateLayout();
	int FindItem(CPoint ptWindow);
	bool IsItemEnabled(const CToolbarItem& item);
	bool IsItemChecked(const CToolbarItem& item);
	CPoint ScreenToWindow(CPoint ptScreen);
	void SaveSettings();
	void ExecuteItem(int nIndex);

	void PaintToolbar(CDC& dc);
	void PaintStatusBar(CDC& dc);
	void PaintSidePanel(CDC& dc);
	int PaintSidePanelContent(CDC& dc, int nWidth); // returns the content height
	void PaintHistogram(CDC& dc, const CRect& rect);
	void PaintItem(CDC& dc, const CToolbarItem& item, int nIndex);
	CString GetStatusText();
};
