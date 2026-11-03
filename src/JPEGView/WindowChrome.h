#pragma once

#include <vector>
#include "ProcessParams.h"

class CMainDlg;

// Toolbar with the main menu (top), status bar (bottom) and side panel (right) of the main window.
// All three are painted into the non-client area of the main window, so the client area stays the image area
// and nothing is painted over the image. Nothing is shown in full screen mode or when the window title bar is hidden.
// The side panel has two tabs: image information (file, EXIF, histogram) and image editing (sliders), it can be collapsed.
class CWindowChrome
{
public:
	// Hit test code for the clickable toolbar items and the side panel
	enum { HT_CHROME = HTOBJECT };

	// Tabs of the side panel
	enum ESideTab { TAB_INFO = 0, TAB_EDIT = 1 };

	CWindowChrome(CMainDlg* pMainDlg);
	~CWindowChrome();

	// True if the chrome can be shown in the current window mode (not full screen, title bar shown)
	bool IsEnabled();

	bool IsToolbarShown() { return IsEnabled() && m_bShowToolbar; }
	bool IsStatusBarShown() { return IsEnabled() && m_bShowStatusBar; }
	bool IsSidePanelShown() { return IsEnabled() && m_bShowSidePanel; }
	// True if the side panel shows its content (shown and not collapsed)
	bool IsSidePanelExpanded() { return IsSidePanelShown() && !m_bSideCollapsed; }
	bool IsEditTabShown() { return IsSidePanelExpanded() && m_eSideTab == TAB_EDIT; }

	// The settings independent of the window mode, as used for check marks in the menu
	bool GetShowToolbar() { return m_bShowToolbar; }
	bool GetShowStatusBar() { return m_bShowStatusBar; }
	bool GetShowSidePanel() { return m_bShowSidePanel; }
	bool GetSidePanelCollapsed() { return m_bSideCollapsed; }
	void SetShowToolbar(bool bShow);
	void SetShowStatusBar(bool bShow);
	void SetShowSidePanel(bool bShow);
	void SetSidePanelCollapsed(bool bCollapsed);
	// Shows the side panel expanded with the given tab
	void ShowSideTab(ESideTab eTab);

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
	void OnNCLButtonDown(CPoint ptScreen, bool bDoubleClick);
	void OnNCLButtonUp(CPoint ptScreen);
	// Returns true if the mouse wheel was used to scroll the side panel
	bool OnMouseWheel(CPoint ptScreen, int nDelta);
	bool OnTimer(int nTimerId);

	// Dragging of a side panel slider, the main window has the mouse captured while dragging
	bool IsDragging() { return m_nDragSlider >= 0; }
	void OnDragMove(CPoint ptScreen);
	void EndDrag();

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

	// Slider on the edit tab, the value is a member of the image processing parameters of the main dialog
	struct CSideSlider {
		LPCTSTR Name; // English, translated with CNLS
		double CImageProcessingParams::* Value;
		double Min, Max, Default;
		bool Logarithmic, Inverted;
		int Group; // 0: no group, 1: local density correction, 2: automatic correction
	};

	// Clickable element of the side panel, rectangles in window coordinates
	enum ESideHitType { SH_Collapse, SH_Expand, SH_Tab, SH_Location, SH_Slider, SH_GroupCheck, SH_Button };
	struct CSideHit {
		ESideHitType Type;
		int Value; // tab, slider index, group or command
		CRect Rect;
		LPCTSTR Tooltip; // English, can be NULL
	};

	struct CColors {
		COLORREF Background, PanelBackground, Border, Text, DimText, Hover, Pressed, Checked, Accent;
	};

	CMainDlg* m_pMainDlg;
	bool m_bShowToolbar, m_bShowStatusBar, m_bShowSidePanel, m_bSideCollapsed;
	ESideTab m_eSideTab;
	std::vector<CToolbarItem> m_items;
	std::vector<CSideSlider> m_sliders;
	std::vector<CSideHit> m_sideHits; // rebuilt on each paint of the side panel
	CColors m_colors;
	HFONT m_hFont, m_hBoldFont, m_hIconFont, m_hSmallIconFont;
	bool m_bHasIconFont;
	int m_nHoverItem, m_nPressedItem; // toolbar items
	int m_nHoverSide; // index into m_sideHits
	bool m_bTrackingMouse;
	CString m_sMessage; // temporary status bar message
	int m_nSideScroll; // scroll position of the side panel content in pixels
	int m_nSideContentHeight;
	int m_nDragSlider; // index of the slider being dragged, -1 if none
	CString m_sLocationURL; // map URL of the GPS location shown on the info tab
	CPoint m_hitOrigin; // window coordinates of the origin used by AddSideHit()
	HWND m_hTooltip;
	CString m_sTooltipText;
	CPoint m_tooltipPos;

	// rectangles of the chrome parts in window coordinates
	CRect m_windowRect, m_toolbarRect, m_statusBarRect, m_sidePanelRect;

	int ToolbarHeight();
	int StatusBarHeight();
	int SidePanelWidth(); // current width, depends on collapsed state
	void UpdateLayout();
	int FindItem(CPoint ptWindow);
	int FindSideHit(CPoint ptWindow);
	bool IsItemEnabled(const CToolbarItem& item);
	bool IsItemChecked(const CToolbarItem& item);
	CPoint ScreenToWindow(CPoint ptScreen);
	void SaveSettings();
	void ExecuteItem(int nIndex);
	int TrackOverflowMenu(const CRect& rectButton); // menu with the toolbar buttons not fitting, returns the command
	void ExecuteSideHit(const CSideHit& hit, CPoint ptWindow, bool bDoubleClick);
	CString GetTooltipText(LPCTSTR sTooltip, int nCommand);
	void ScheduleTooltip(LPCTSTR sText, CRect rectWindow);
	void HideTooltip();

	void SetSliderFromPosition(int nSlider, int nXWindow);
	double SliderToPosition(const CSideSlider& slider, double dValue);
	double PositionToSlider(const CSideSlider& slider, double dPosition);
	bool IsSliderGroupEnabled(int nGroup);
	double GetSliderDefault(const CSideSlider& slider);

	void PaintToolbar(CDC& dc);
	void PaintStatusBar(CDC& dc);
	void PaintSidePanel(CDC& dc);
	void PaintCollapsedSidePanel(CDC& dc);
	int PaintInfoTab(CDC& dc, int nWidth, int nY); // returns the content height
	int PaintEditTab(CDC& dc, int nWidth, int nY); // returns the content height
	void PaintHistogram(CDC& dc, const CRect& rect);
	void PaintItem(CDC& dc, const CToolbarItem& item, int nIndex);
	void PaintSideButton(CDC& dc, const CRect& rect, LPCTSTR sText, bool bChecked, bool bHover, bool bEnabled);
	void PaintCheckBox(CDC& dc, const CRect& rect, bool bChecked);
	void AddSideHit(ESideHitType eType, int nValue, const CRect& rectPanel, LPCTSTR sTooltip);
	CString GetStatusText();
};
