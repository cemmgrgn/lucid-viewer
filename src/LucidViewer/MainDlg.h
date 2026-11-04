// Main dialog of Lucid Viewer
/////////////////////////////////////////////////////////////////////////////

#pragma once

#include <vector>
#include "MessageDef.h"
#include "ProcessParams.h"
#include "Helpers.h"
#include "CropCtl.h"
#include "Metadata.h"
#include <memory>

class CFileList;
class CJPEGProvider;
class CJPEGImage;
class CButtonCtrl;
class CTextCtrl;
class CPanel;
class CPanelMgr;
class CEXIFDisplayCtl;
class CImageProcPanelCtl;
class CNavigationPanelCtl;
class CRotationPanelCtl;
class CTiltCorrectionPanelCtl;
class CUnsharpMaskPanelCtl;
class CWndButtonPanelCtl;
class CInfoButtonPanelCtl;
class CZoomNavigatorCtl;
class CCropCtl;
class CKeyMap;
class CDirectoryWatcher;
class CUserCommand;
class CPrintImage;
class CHelpDlg;
class CWindowChrome;
class CAnnotations;

enum EMouseEvent;

// The main dialog is a full screen modal dialog with no border and no window title.
// This dialog is the main window of the Lucid Viewer application.
class CMainDlg : public CDialogImpl<CMainDlg>
{
public:
	enum { IDD = IDD_MAINDLG };

	// Used in GotoImage() call
	enum EImagePosition {
		POS_First,
		POS_Last,
		POS_Next,
		POS_NextSlideShow,
		POS_NextAnimation,
		POS_Previous,
		POS_Current,
		POS_Clipboard,
		POS_Toggle,
		POS_AwayFromCurrent
	};

	CMainDlg(bool bForceFullScreen);
	~CMainDlg();

	BEGIN_MSG_MAP(CMainDlg)
		MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBackground)
		MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
		MESSAGE_HANDLER(WM_SIZE, OnSize)
		MESSAGE_HANDLER(WM_GETMINMAXINFO, OnGetMinMaxInfo)
		MESSAGE_HANDLER(WM_PAINT, OnPaint)
		MESSAGE_HANDLER(WM_NCHITTEST, OnNCHitTest)
		MESSAGE_HANDLER(WM_NCLBUTTONDOWN, OnNCLButtonDown)
		MESSAGE_HANDLER(WM_NCLBUTTONDBLCLK, OnNCLButtonDown)
		MESSAGE_HANDLER(WM_NCLBUTTONUP, OnNCLButtonUp)
		MESSAGE_HANDLER(WM_NCMOUSEMOVE, OnNCMouseMove)
		MESSAGE_HANDLER(WM_NCMOUSELEAVE, OnNCMouseLeave)
		MESSAGE_HANDLER(WM_NCCALCSIZE, OnNCCalcSize)
		MESSAGE_HANDLER(WM_NCPAINT, OnNCPaint)
		MESSAGE_HANDLER(WM_NCACTIVATE, OnNCActivate)
		MESSAGE_HANDLER(WM_SETCURSOR, OnSetCursor)
		MESSAGE_HANDLER(WM_CONVERSION_FINISHED, OnConversionFinished)
		MESSAGE_HANDLER(WM_RATING_READ, OnRatingRead)
		MESSAGE_HANDLER(WM_UPDATE_CHECKED, OnUpdateChecked)
		MESSAGE_HANDLER(WM_EXECUTE_COMMAND, OnExecuteCommand)
		MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
		MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
		MESSAGE_HANDLER(WM_LBUTTONDBLCLK, OnLButtonDblClk)
		MESSAGE_HANDLER(WM_RBUTTONDOWN, OnRButtonDown)
		MESSAGE_HANDLER(WM_RBUTTONUP, OnRButtonUp)
		MESSAGE_HANDLER(WM_MBUTTONDOWN, OnMButtonDown)
		MESSAGE_HANDLER(WM_MBUTTONUP, OnMButtonUp)
		MESSAGE_HANDLER(WM_XBUTTONDOWN, OnXButtonDown)
		MESSAGE_HANDLER(WM_XBUTTONDBLCLK, OnXButtonDown)
		MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
		MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
		MESSAGE_HANDLER(WM_KEYDOWN, OnKeyDown)
		MESSAGE_HANDLER(WM_SYSKEYDOWN, OnSysKeyDown)
		MESSAGE_HANDLER(WM_GETDLGCODE, OnGetDlgCode)
		MESSAGE_HANDLER(WM_TIMER, OnTimer)
		MESSAGE_HANDLER(WM_MOUSEHWHEEL, OnMouseHWheel)
		MESSAGE_HANDLER(WM_CONTEXTMENU, OnContextMenu)
		MESSAGE_HANDLER(WM_CTLCOLOREDIT, OnCtlColorEdit)
		MESSAGE_HANDLER(WM_IMAGE_LOAD_COMPLETED, OnImageLoadCompleted)
		MESSAGE_HANDLER(WM_DISPLAYED_FILE_CHANGED_ON_DISK, OnDisplayedFileChangedOnDisk)
		MESSAGE_HANDLER(WM_ACTIVE_DIRECTORY_FILELIST_CHANGED, OnActiveDirectoryFilelistChanged)
		MESSAGE_HANDLER(WM_DROPFILES, OnDropFiles)
		MESSAGE_HANDLER(WM_CLOSE, OnClose)
		MESSAGE_HANDLER(WM_LOAD_FILE_ASYNCH, OnLoadFileAsynch)
		MESSAGE_HANDLER(WM_COPYDATA, OnAnotherInstanceStarted) 
		COMMAND_ID_HANDLER(IDOK, OnOK)
		COMMAND_ID_HANDLER(IDCANCEL, OnCancel)
	END_MSG_MAP()

// Handler prototypes (uncomment arguments if needed):
//	LRESULT MessageHandler(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
//	LRESULT CommandHandler(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
//	LRESULT NotifyHandler(int /*idCtrl*/, LPNMHDR /*pnmh*/, BOOL& /*bHandled*/)


	LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnOK(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnCancel(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnEraseBackground(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnPaint(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnGetMinMaxInfo(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnLButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnNCLButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnNCHitTest(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnNCLButtonUp(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnNCMouseMove(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnNCMouseLeave(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnNCCalcSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnNCPaint(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnNCActivate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSetCursor(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnConversionFinished(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnRatingRead(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnUpdateChecked(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnExecuteCommand(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/) { ExecuteCommand((int)wParam); return 0; }
	LRESULT OnRButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnRButtonUp(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnLButtonUp(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnLButtonDblClk(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnMButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnMButtonUp(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnXButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnMouseMove(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnMouseWheel(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnKeyDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnSysKeyDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnGetDlgCode(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnTimer(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnMouseHWheel(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/);
	LRESULT OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnCtlColorEdit(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
	LRESULT OnImageLoadCompleted(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/);
	LRESULT OnDisplayedFileChangedOnDisk(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/);
	LRESULT OnActiveDirectoryFilelistChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/);
	LRESULT OnDropFiles(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/);
	LRESULT OnAnotherInstanceStarted(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/);
	LRESULT OnLoadFileAsynch(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/);
	LRESULT OnClose(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/);

	// Called by main()
	void SetStartupInfo(LPCTSTR sStartupFile, int nAutostartSlideShow, Helpers::ESorting eSorting, Helpers::ETransitionEffect eEffect, 
		int nTransitionTime, bool bAutoExit, int nDisplayMonitor);

	// Called by the different controller classes
	HWND GetHWND() { return m_hWnd; }
	bool IsShowFileName() { return m_bShowFileName; }
	bool IsInMovieMode() { return m_bMovieMode; }
	bool IsInZoomMode() { return m_bZoomModeOnLeftMouse; }
	bool IsPlayingAnimation() { return m_bIsAnimationPlaying; }
	bool IsFullScreenMode() { return m_bFullScreenMode; }
	bool IsLandscapeMode() { return m_bLandscapeMode; }
	bool IsHQResampling() { return m_bHQResampling; }
	bool IsAutoContrast() { return m_bAutoContrast; }
	bool IsAutoContrastSection() { return m_bAutoContrastSection; }
	bool IsLDC() { return m_bLDC; }
	bool IsKeepParams() { return m_bKeepParams; }
	bool IsSpanVirtualDesktop() { return m_bSpanVirtualDesktop; }
	bool IsCropping() { return m_pCropCtl->IsCropping(); }
	bool IsDoCropping() { return m_pCropCtl->IsDoCropping(); }
	bool IsDoDragging() { return m_bDoDragging; }
	bool IsInZooming() { return m_bInZooming; }
	bool IsShowZoomFactor() { return m_bShowZoomFactor; }
	bool IsPanMouseCursorSet() { return m_bPanMouseCursorSet; }
	bool IsMouseOn() { return m_bMouseOn; }
	bool IsWindowBorderless() { return m_bWindowBorderless; }
	bool IsAlwaysOnTop() { return m_bAlwaysOnTop; }
	bool IsToolbarShown();
	bool IsStatusBarShown();
	bool IsSidePanelShown();
	CWindowChrome* GetWindowChrome() { return m_pChrome; }
	// Before/after comparison: the left part of the image is shown unedited, the split can be dragged
	bool IsCompareMode() { return m_bCompareMode; }
	CAnnotations* GetAnnotations() { return m_pAnnotations; }
	// Crop tool of the side panel: the left mouse button selects the crop rectangle without holding Ctrl
	bool IsCropTool() { return m_bCropTool; }
	// The IDM_CROPMODE_xxx command of the current crop aspect ratio
	int GetCropModeCommand() { return m_nCropModeCommand; }
	// Star rating of the current file as shown by Explorer (0: none, 1-5 stars, -1: not available)
	int GetRating();
	// Same, but does not wait: when the rating is not known yet it is read in the background
	// (the first use of the Windows property system takes ~60 ms), 0 is returned and the panel is repainted later
	int GetRatingNoWait();
	struct CRatingResult { CString File; int Rating; int Generation; };
	// Writes the star rating (0-5) into the current file
	void SetRating(int nStars);
	// Undo (Ctrl+Z) and redo (Ctrl+Y) of all operations: editing values, drawings, crop, rotation, resize, perspective,
	// removed colors, rating, metadata, moving to the recycle bin and lossless JPEG transformations.
	// PushUndo() saves the current state before an operation, bPixels also copies the original pixels.
	void PushUndo(bool bPixels);
	// For changes over several messages (dragging a slider): BeginEdit() remembers the state, EndEdit() saves it if changed
	void BeginEdit();
	void EndEdit();
	bool CanUndo() { return !m_undoStack.empty(); }
	bool CanRedo() { return !m_redoStack.empty(); }
	// All metadata of the current file (cached), empty if not available
	const std::vector<Metadata::CEntry>& GetMetadata();
	// Shows the edit window for the metadata entry with the given index and writes the value into the file
	void EditMetadata(int nIndex);
	// True if the drawing tools are shown and a tool is selected, the mouse draws on the image then
	bool IsDrawMode();
	// Draws the drawings into the original pixels of the image
	void ApplyAnnotations();
	// True if colors were made transparent with the remove color tool
	bool HasTransparency() { return !m_transparencyMask.empty(); }
	// Shows the given file of the current folder (thumbnail strip)
	void ShowFile(LPCTSTR sFileName) { OpenFile(sFileName, false); }
	void SetCompareMode(bool bCompare);
	// Called when the image processing parameters were changed from outside, e.g. by the side panel sliders
	void OnImageProcessingChanged();
	// Sets all image processing parameters and flags back to the defaults
	void ResetImageProcessing();
	// The default image processing parameters as set in the INI file
	CImageProcessingParams GetDefaultImageProcessingParams();
	// Creates the popup menu for the menu bar title with the given index (File, Edit, View, Navigate, Tools, Help).
	// The caller must destroy the returned menu.
	HMENU CreateMenuBarMenu(int nIndex);

	CPoint GetMousePos() { return CPoint(m_nMouseX, m_nMouseY); }
	double GetZoom() { return m_dZoom; }
	int GetRotation() { return m_nRotation; }
	CJPEGImage* GetCurrentImage() { return m_pCurrentImage; }
	CPanelMgr* GetPanelMgr() { return m_pPanelMgr; }
	LPCTSTR CurrentFileName(bool bFileTitle);
	CFileList* GetFileList() { return m_pFileList; }
	CNavigationPanelCtl* GetNavPanelCtl() { return m_pNavPanelCtl; }
	CEXIFDisplayCtl* GetEXIFDisplayCtl() { return m_pEXIFDisplayCtl; }
	CUnsharpMaskPanelCtl* GetUnsharpMaskPanelCtl() { return m_pUnsharpMaskPanelCtl; }
	CImageProcPanelCtl* GetImageProcPanelCtl() { return m_pImageProcPanelCtl; }
	CRotationPanelCtl* GetRotationPanelCtl() { return m_pRotationPanelCtl; }
	CTiltCorrectionPanelCtl* GetTiltCorrectionPanelCtl() { return m_pTiltCorrectionPanelCtl; }
	CZoomNavigatorCtl* GetZoomNavigatorCtl() { return m_pZoomNavigatorCtl; }
	CWndButtonPanelCtl* GetWndButtonPanelCtl() { return m_pWndButtonPanelCtl; }
	CInfoButtonPanelCtl* GetInfoButtonPanelCtl() { return m_pInfoButtonPanelCtl; }
	CCropCtl* GetCropCtl() { return m_pCropCtl; }
	const CRect& ClientRect() { return m_clientRect; }
	const CRect& WindowRectOnClose() { return m_windowRectOnClose; } // only valid after having closed the window
	const CRect& MonitorRect() { return m_monitorRect; }
	const CSize& VirtualImageSize() { return m_virtualImageSize; }
	CJPEGProvider* GetJPEGProvider() { return m_pJPEGProvider; }
	CKeyMap* GetKeyMap() { return m_pKeyMap; }
	CPoint GetDIBOffset() { return m_DIBOffsets; }
	double GetZoomMultiplier(CJPEGImage* pImage, const CRect& clientRect);
	Helpers::EAutoZoomMode GetAutoZoomMode() { return m_bFullScreenMode ? m_eAutoZoomModeFullscreen : m_eAutoZoomModeWindowed; }
	CPoint GetOffsets() { return m_offsets; }
	CImageProcessingParams* GetImageProcessingParams() { return m_pImageProcParams; }
	EProcessingFlags CreateDefaultProcessingFlags(bool bKeepParams = false);
	void DisplayErrors(CJPEGImage* pCurrentImage, const CRect& clientRect, CDC& dc);
	void DisplayFileName(const CRect& imageProcessingArea, CDC& dc, double realizedZoom);
	void BlendBlackRect(CDC & targetDC, CPanel& panel, float fBlendFactor);

	void UpdateWindowTitle();
	void MouseOff();
	void MouseOn();
	void GotoImage(EImagePosition ePos);
	void ReloadImage(bool keepParameters, bool updateWindow = true);
	void ResetZoomTo100Percents(bool bZoomToMouse);
	void ResetZoomToFitScreen(bool bFillWithCrop, bool bAllowEnlarge, bool bAdjustWindowSize);
	bool PerformPan(int dx, int dy, bool bAbsolute);
	void StartDragging(int nX, int nY, bool bDragWithZoomNavigator);
	void DoDragging();
	void EndDragging();
	void SetCursorForMoveSection();
	bool ScreenToImage(float & fX, float & fY); 
	bool ImageToScreen(float & fX, float & fY);
	void ExecuteCommand(int nCommand);
	bool PrepareForModalPanel(); // returns if navigation panel was enabled, turns it off
	int TrackPopupMenu(CPoint pos, HMENU hMenu);
	void AdjustWindowToImage(bool bAfterStartup);
	bool IsAdjustWindowToImage();
	bool IsImageExactlyFittingWindow();
	Helpers::ETransitionEffect GetTransitionEffect() { return m_eTransitionEffect; }
	int GetTransitionTime() { return m_nTransitionTime; }
	bool IsInSlideShowWithTransition() { return m_bMovieMode && UseSlideShowTransitionEffect(); }

	// Called by button clicked handlers - must be static
	// pContext is a pointer to the main dialog
	static void OnExecuteCommand(void* pContext, int nParameter, CButtonCtrl & sender);
	static bool IsCurrentImageFitToScreen(void* pContext);

private:

	CString m_sStartupFile; // file passed on command line
	int m_nAutoStartSlideShow; // if positive: Auto start slide show with given interval in seconds, passed on command line
	bool m_bAutoExit;
	Helpers::ESorting m_eForcedSorting; // forced sorting mode on command line
	CFileList* m_pFileList; // used for navigation
	CDirectoryWatcher* m_pDirectoryWatcher; // notifies the main window when the current file changed or a file in the current directory was added or deleted
	CJPEGProvider * m_pJPEGProvider; // reads image (of any format, not only JPEGs) files, using read ahead
	CJPEGImage * m_pCurrentImage; // currently displayed image
	bool m_bOutOfMemoryLastImage; // true if the last image could not be requested because not enough memory
	bool m_bExceptionErrorLastImage; // true if the last image could not be requested because of an unhandled exception
	int m_nLastLoadError; // one of HelpersGUI::EFileLoadError
	
	// Current parameter set
	int m_nRotation; // this can only be 0, 90, 180 or 270
	int m_nUserRotation; // Rotation delta from user, can only be 0, 90, 180 or 270
	bool m_bUserZoom;
	bool m_bUserPan; // user has zoomed and panned away from default values
	bool m_bResizeForNewImage;
	double m_dZoom, m_dRealizedZoom;
	double m_dStartZoom; // zoom when start zoomin in zoom mode
	double m_dZoomAtResizeStart; // zoom factor when user started resizing Lucid Viewer main window
	double m_dZoomMult;
	bool m_bZoomMode;
	bool m_bZoomModeOnLeftMouse;
	Helpers::EAutoZoomMode m_eAutoZoomModeWindowed;
	Helpers::EAutoZoomMode m_eAutoZoomModeFullscreen;
	Helpers::EAutoZoomMode m_autoZoomFitToScreen;
	bool m_isUserFitToScreen;

	CImageProcessingParams* m_pImageProcParams;
	bool m_bHQResampling;
	bool m_bAutoContrast;
	bool m_bAutoContrastSection;
	bool m_bLDC;
	bool m_bLandscapeMode;
	bool m_bKeepParams;

	// used to enable switch between two sets of parameters with CTRL-A
	CImageProcessingParams* m_pImageProcParams2;
	EProcessingFlags m_eProcessingFlags2;

	// set of parameters used when m_bKeepParams is true
	CImageProcessingParams* m_pImageProcParamsKept;
	EProcessingFlags m_eProcessingFlagsKept;
	double m_dZoomKept;
	CPoint m_offsetKept;
	bool m_bCurrentImageInParamDB;
	bool m_bCurrentImageIsSpecialProcessing;
	double m_dCurrentInitialLightenShadows;

	bool m_bDragging;
	bool m_bDoDragging;
	bool m_bMovieMode;
	double m_dMovieFPS;
	bool m_bProcFlagsTouched;
	EProcessingFlags m_eProcFlagsBeforeMovie;
	bool m_bInTrackPopupMenu;
	CPoint m_offsets; // Note: These offsets are center of image based
	CPoint m_DIBOffsets;
	int m_nCapturedX, m_nCapturedY;
	int m_nMouseX, m_nMouseY;
	bool m_bDefaultSelectionMode;
	bool m_bShowFileName;
	bool m_bFullScreenMode;
	bool m_bAutoFitWndToImage;
	bool m_bLockPaint;
	int m_nCurrentTimeout;
	POINT m_startMouse;
	CSize m_virtualImageSize;
	bool m_bInZooming;
	bool m_bTemporaryLowQ;
	bool m_bShowZoomFactor;
	bool m_bSpanVirtualDesktop;
	bool m_bPanMouseCursorSet;
	bool m_bMouseOn;
	bool m_bKeepParametersBeforeAnimation;
	bool m_bIsAnimationPlaying;
	int m_nLastAnimationOffset;
	int m_nExpectedNextAnimationTickCount;
	int m_nMonitor;
	WINDOWPLACEMENT m_storedWindowPlacement;
	CRect m_monitorRect;
	CRect m_clientRect;
	CRect m_windowRectOnClose;
	CString m_sSaveDirectory;
	CString m_sSaveExtension;
	CCropCtl* m_pCropCtl;
	CZoomNavigatorCtl* m_pZoomNavigatorCtl;
	CImageProcPanelCtl* m_pImageProcPanelCtl;
	CNavigationPanelCtl* m_pNavPanelCtl;
	CEXIFDisplayCtl* m_pEXIFDisplayCtl;
	CWndButtonPanelCtl* m_pWndButtonPanelCtl;
	CInfoButtonPanelCtl* m_pInfoButtonPanelCtl;
	CUnsharpMaskPanelCtl* m_pUnsharpMaskPanelCtl;
	CRotationPanelCtl* m_pRotationPanelCtl;
	CTiltCorrectionPanelCtl* m_pTiltCorrectionPanelCtl;
	CPanelMgr* m_pPanelMgr;
	CKeyMap* m_pKeyMap;
	CPrintImage* m_pPrintImage;
	CHelpDlg* m_pHelpDlg;
	Helpers::ETransitionEffect m_eTransitionEffect;
	int m_nTransitionTime;
	DWORD m_nLastSlideShowImageTickCount;
	bool m_bUseLosslessWEBP;
	bool m_isBeforeFileSelected;
	double m_dLastImageDisplayTime;
	bool m_bWindowBorderless;
	bool m_bAlwaysOnTop;
	bool m_bSelectZoom;  // keeps track of select-to-zoom mode when CTRL+SHIFT+LMouse
	CWindowChrome* m_pChrome; // toolbar, status bar and side panel
	volatile LONG m_nRunningConversions;
	bool m_bCompareMode;
	CAnnotations* m_pAnnotations; // drawings over the current image
	std::vector<uint8> m_transparencyMask; // alpha per original pixel (0: removed) of the remove color tool, empty if not used
	std::vector<uint8> m_transparencyBackup; // original pixels before the first removed color
	// Makes the area around the image point with similar color transparent, shown as checker board
	void RemoveColorAt(float fX, float fY);
	void ClearTransparency(bool bRestorePixels);
	// Combines the alpha values (0: transparent) with the current transparency and shows the result over a checker board
	void ApplyTransparencyMask(const std::vector<uint8>& alpha);
	bool m_bTransparencySaved; // the current transparency was saved as PNG
	// Asks before drawings or removed colors that are not saved get lost, returns true to continue
	bool ConfirmDiscardEdits();
	CString m_sRatingFile; // file of the cached rating
	CString m_sRatingPendingFile; // file whose rating is being read in the background
	int m_nRatingGeneration; // changes when the cached rating becomes invalid, late background results are ignored
	// undo and redo
	struct CEditState {
		CImageProcessingParams Params;
		bool AutoContrast, AutoContrastSection, LDC, LandscapeMode;
		int Rotation, UserRotation;
		std::shared_ptr<CAnnotations> Annotations;
		std::shared_ptr<CPixelSnapshot> Pixels; // NULL if the operation did not change the pixels
		std::shared_ptr<std::vector<uint8>> TransparencyMask, TransparencyBackup; // only with pixels
	};
	enum EUndoKind { UNDO_EDIT, UNDO_RATING, UNDO_METADATA, UNDO_DELETE, UNDO_LOSSLESS, UNDO_FILE, UNDO_MOVE };
	struct CUndoEntry {
		EUndoKind Kind;
		CString File;
		CEditState State;
		int Rating;
		std::vector<std::pair<PROPERTYKEY, CString>> Metadata;
		int Command;
		std::shared_ptr<std::vector<char>> FileData; // UNDO_FILE: the whole file before the change
		CString Target; // UNDO_MOVE: the file name after the move (File is the name before)
	};
	std::vector<CUndoEntry> m_undoStack, m_redoStack;
	CEditState m_pendingEdit;
	bool m_bEditPending;
	bool m_bUndoing; // no new undo entries while undoing
	int m_nCommandDepth;
	void CaptureState(CEditState& state, bool bPixels);
	void RestoreState(const CEditState& state);
	bool IsSameEditState(const CEditState& a, const CEditState& b);
	void PushUndoEntry(const CUndoEntry& entry);
	void Undo();
	void Redo();
	bool SwapFileData(const CUndoEntry& entry, bool bRedo = false);
	// Start screen (gallery) shown when no image is open or called with IDM_START_SCREEN
	class CStartScreen* m_pStartScreen;
	bool m_bStartScreen;  // shown over the open image
	bool m_bRecordRecent; // the next loaded image is added to the last opened ones
	bool IsStartScreenActive();
	// Smooth zoom: the target is found like PerformZoom does, the shown zoom moves to it in a few frames
	bool AnimateZoom(double dValue, bool bExponent, bool bZoomToMouse, bool bAdjustWindowToImage);
	void ZoomAnimationStep();
	void StopZoomAnimation();
	static void CALLBACK ZoomAnimationTimer(HWND, UINT, UINT_PTR, DWORD);
	bool m_bZoomAnimation, m_bZoomFrame, m_bZoomAnimToMouse, m_bZoomAnimAdjust;
	double m_dZoomFrom, m_dZoomTo;
	DWORD m_nZoomAnimStart;
	CJPEGImage* m_pZoomAnimImage;
	void HandleStartScreenAction(int nAction, const CString& sFile);
	void OpenFolderWithDialog();
	// Copies or moves the current file into a folder chosen with the folder dialog
	void CopyOrMoveToFolder(bool bMove);
	// Moves a file, the current image is left first if it is the moved file. Returns false on error.
	bool MoveImageFile(const CString& sFrom, const CString& sTo);
	CString m_sLastTargetFolder; // folder of the last 'Copy to folder' or 'Move to folder'
	bool m_bUpdateCheckRunning;
	void ExecuteCommandInternal(int nCommand);
	// Sub menu for sorting and filtering by the star rating
	HMENU CreateRatingMenu();
	// After the settings window: reads the settings again and applies what can be applied without restart
	void ApplyChangedSettings(const std::vector<CString>& changedKeys, bool bKeyMapChanged);
	CString m_sMetadataFile; // file of the cached metadata
	std::vector<Metadata::CEntry> m_metadata;
	LPCTSTR GetMetadataFileName(); // current file if metadata can be read from it, else NULL
	void AfterMetadataChanged();
	bool m_bCropTool;
	bool m_bDragOutPending; // left button pressed on an image fitting the window, moving drags the file out
	CPoint m_dragOutStart;
	int m_nCropModeCommand;
	int m_nRating;
	void PaintAnnotations(CDC& dc);
	double m_dCompareSplit; // position of the split line relative to the client width
	bool m_bDraggingCompareSplit;
	std::vector<uint8> m_compareBuffer; // composed before/after DIB

	// Composes the unedited image (left of the split) and the processed DIB (right of it) into the compare buffer
	void* ComposeCompareDIB(void* pProcessedDIB, CSize clippedSize);
	// Client x coordinate of the compare split line
	int GetCompareSplitX();
	bool IsOnCompareSplit(int nX);
	void PaintCompareSplit(CDC& dc);

	// Loads the context menu and sets the check marks and enabled states. Returns the popup menu, hMenuRoot must be destroyed by the caller.
	HMENU LoadPreparedContextMenu(HMENU& hMenuRoot);
	// Converts the current file in a background thread with the given preset (index into Converter::GetPresets())
	void StartConversion(int nPresetIndex);
	void ShowConvertMenuAndExecute();

	void ExploreFile();
	bool OpenFileWithDialog(bool bFullScreen, bool bAfterStartup);
	void OpenFile(LPCTSTR sFileName, bool bAfterStartup);
	bool SaveImage(bool bFullSize);
	bool SaveImageNoPrompt(LPCTSTR sFileName, bool bFullSize);
	void BatchCopy();
	void SetAsDefaultViewer();
	void HandleUserCommands(uint32 virtualKeyCode);
	void ExecuteUserCommand(CUserCommand* pUserCommand);
	void GotoImage(EImagePosition ePos, int nFlags);
	void AdjustLDC(int nMode, double dInc);
	void AdjustGamma(double dFactor);
	void AdjustContrast(double dInc);
	void AdjustSharpen(double dInc);
	bool PerformZoom(double dValue, bool bExponent, bool bZoomToMouse, bool bAdjustWindowToImage);
	void ZoomToSelection();
	double GetZoomFactorForFitToScreen(bool bFillWithCrop, bool bAllowEnlarge);
	CProcessParams CreateProcessParams(bool bNoProcessingAfterLoad);
	void ResetParamsToDefault();
	void StartSlideShowTimer(int nMilliSeconds);
	void StopSlideShowTimer(void);
	void StartMovieMode(double dFPS);
	void StopMovieMode();
	void StartLowQTimer(int nTimeout);
	void InitParametersForNewImage();
	void ExchangeProcessingParams();
	void SaveParameters();
	void AfterNewImageLoaded(bool bSynchronize, bool bAfterStartup, bool noAdjustWindow);
	CRect ScreenToDIB(const CSize& sizeDIB, const CRect& rect);
	void ToggleMonitor();
	CRect GetZoomTextRect(CRect imageProcessingArea);
	void EditINIFile(bool bGlobalINI);
	int GetLoadErrorAfterOpenFile();
	void CheckIfApplyAutoFitWndToImage(bool bInInitDialog);
	void PrefetchDIB(const CRect& clientRect);
	bool HandleMouseButtonByKeymap(int nMouseButton, bool bExecuteCommand = true);
	bool UseSlideShowTransitionEffect();
	void PaintToDC(CDC& dc);
	void AnimateTransition();
	void CleanupAndTerminate();
	void InvalidateHelpDlg();
	bool CloseHelpDlg();
	LONG SetCurrentWindowStyle();
	// this is for animated GIFs
	void StartAnimation();
	void AdjustAnimationFrameTime();
	void StopAnimation();
	void ToggleAlwaysOnTop();
};
