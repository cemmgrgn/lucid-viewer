// MainDlg.cpp : implementation of the CMainDlg class
//
/////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "resource.h"
#include <math.h>
#include <limits.h>

#include "MainDlg.h"
#include "HelpDlg.h"
#include "FileList.h"
#include "JPEGProvider.h"
#include "JPEGImage.h"
#include "SettingsProvider.h"
#include "BasicProcessing.h"
#include "MultiMonitorSupport.h"
#include "HistogramCorr.h"
#include "UserCommand.h"
#include "Clipboard.h"
#include "ParameterDB.h"
#include "SaveImage.h"
#include "NLS.h"
#include "HelpersGUI.h"
#include "TimerEventIDs.h"
#include "FileOpenDialog.h"
#include "BatchCopyDlg.h"
#include "FileExtensionsDlg.h"
#include "FileExtensionsRegistry.h"
#include "ManageOpenWithDlg.h"
#include "AboutDlg.h"
#include "CropSizeDlg.h"
#include "ResizeDlg.h"
#include "ResizeFilter.h"
#include "EXIFReader.h"
#include "EXIFHelpers.h"
#include "RawMetadata.h"
#include "ProcessingThreadPool.h"
#include "PaintMemDCMgr.h"
#include "PanelMgr.h"
#include "ZoomNavigatorCtl.h"
#include "ImageProcPanelCtl.h"
#include "WndButtonPanelCtl.h"
#include "InfoButtonPanelCtl.h"
#include "RotationPanelCtl.h"
#include "TiltCorrectionPanelCtl.h"
#include "UnsharpMaskPanelCtl.h"
#include "EXIFDisplayCtl.h"
#include "NavigationPanelCtl.h"
#include "NavigationPanel.h"
#include "KeyMap.h"
#include "JPEGLosslessTransform.h"
#include "DirectoryWatcher.h"
#include "DesktopWallpaper.h"
#include "PrintImage.h"
#include "WindowChrome.h"
#include "Converter.h"
#include "SettingsDlg.h"
#include "Annotations.h"
#include "Ocr.h"
#include "Share.h"
#include "ExportDlg.h"
#include "DuplicatesDlg.h"
#include "BackgroundRemoval.h"
#include <propsys.h>
#include <propkey.h>
#include <gdiplus.h>
#include <process.h>

//////////////////////////////////////////////////////////////////////////////////////////////
// Constants
//////////////////////////////////////////////////////////////////////////////////////////////

static const int MIN_WND_WIDTH = 320;
static const int MIN_WND_HEIGHT = 240;

static const double GAMMA_FACTOR = 1.02; // multiplicator for gamma value
static const double CONTRAST_INC = 0.03; // increment for contrast value
static const double SHARPEN_INC = 0.05; // increment for sharpen value
static const double LDC_INC = 0.1; // increment for LDC (lighten shadows and darken highlights)
static const int NUM_THREADS = 1; // number of readahead threads to use
static const int READ_AHEAD_BUFFERS = 2; // number of readahead buffers to use (NUM_THREADS+1 is a good choice)
static const int ZOOM_TIMEOUT = 200; // refinement done after this many milliseconds
static const int ZOOM_TEXT_TIMEOUT = 1000; // zoom label disappears after this many milliseconds

static const int DARKEN_HIGHLIGHTS = 0; // used in AdjustLDC() call
static const int BRIGHTEN_SHADOWS = 1; // used in AdjustLDC() call

static const int ZOOM_TEXT_RECT_WIDTH = 70; // zoom label width
static const int ZOOM_TEXT_RECT_HEIGHT = 25; // zoom label height
static const int ZOOM_TEXT_RECT_OFFSET = 35; // zoom label offset from right border
static const int PAN_STEP = 48; // number of pixels to pan if pan with cursor keys (SHIFT+up/down/left/right)

static const bool SHOW_TIMING_INFO = false; // Set to true for debugging

static const int NO_REQUEST = 1; // used in GotoImage() method
static const int NO_REMOVE_KEY_MSG = 2; // used in GotoImage() method
static const int KEEP_PARAMETERS = 4; // used in GotoImage() method
static const int NO_UPDATE_WINDOW = 8; // used in GotoImage() method

static const CSize HUGE_SIZE = CSize(99999999, 99999999);

//////////////////////////////////////////////////////////////////////////////////////////////
// Helpers
//////////////////////////////////////////////////////////////////////////////////////////////

// Gets default image processing parameters as set in INI file
static CImageProcessingParams GetDefaultProcessingParams() {
	CSettingsProvider& sp = CSettingsProvider::This();
	return CImageProcessingParams(
		sp.Contrast(),
		sp.Gamma(),
		sp.Saturation(),
		sp.Sharpen(),
		0.0, 0.5,
		sp.BrightenShadows(),
		sp.DarkenHighlights(),
		sp.BrightenShadowsSteepness(),
		sp.CyanRed(),
		sp.MagentaGreen(),
		sp.YellowBlue());
}

// Gets hardcoded processing parameters, turning off all processing except sharpening according to INI
static CImageProcessingParams GetNoProcessingParams() {
	return CImageProcessingParams(0.0, 1.0, 1.0, CSettingsProvider::This().Sharpen(), 0.0, 0.5, 0.5, 0.25, 0.5, 0.0, 0.0, 0.0); 
}

// Gets default image processing flags as set in INI file
static EProcessingFlags GetDefaultProcessingFlags(bool bLandscapeMode) {
	CSettingsProvider& sp = CSettingsProvider::This();
	EProcessingFlags eProcFlags = PFLAG_None;
	eProcFlags = SetProcessingFlag(eProcFlags, PFLAG_HighQualityResampling, sp.HighQualityResampling());
	eProcFlags = SetProcessingFlag(eProcFlags, PFLAG_AutoContrast, sp.AutoContrastCorrection());
	eProcFlags = SetProcessingFlag(eProcFlags, PFLAG_LDC, sp.LocalDensityCorrection());
	eProcFlags = SetProcessingFlag(eProcFlags, PFLAG_LandscapeMode, bLandscapeMode);
	return eProcFlags;
}

// Create processing flags from literal boolean values
static EProcessingFlags CreateProcessingFlags(bool bHQResampling, bool bAutoContrast, 
											  bool bAutoContrastSection, bool bLDC, bool bKeepParams,
											  bool bLandscapeMode) {
	EProcessingFlags eProcFlags = PFLAG_None;
	eProcFlags = SetProcessingFlag(eProcFlags, PFLAG_HighQualityResampling, bHQResampling);
	eProcFlags = SetProcessingFlag(eProcFlags, PFLAG_AutoContrast, bAutoContrast);
	eProcFlags = SetProcessingFlag(eProcFlags, PFLAG_AutoContrastSection, bAutoContrastSection);
	eProcFlags = SetProcessingFlag(eProcFlags, PFLAG_LDC, bLDC);
	eProcFlags = SetProcessingFlag(eProcFlags, PFLAG_KeepParams, bKeepParams);
	eProcFlags = SetProcessingFlag(eProcFlags, PFLAG_LandscapeMode, bLandscapeMode);
	return eProcFlags;
}

// Initialize boolean processing values from flag bitfield
static void InitFromProcessingFlags(EProcessingFlags eFlags, bool& bHQResampling, bool& bAutoContrast, 
									bool& bAutoContrastSection, bool& bLDC, bool& bLandscapeMode) {
	bHQResampling = GetProcessingFlag(eFlags, PFLAG_HighQualityResampling);
	bAutoContrast = GetProcessingFlag(eFlags, PFLAG_AutoContrast);
	bAutoContrastSection = GetProcessingFlag(eFlags, PFLAG_AutoContrastSection);
	bLDC = GetProcessingFlag(eFlags, PFLAG_LDC);
	bLandscapeMode = GetProcessingFlag(eFlags, PFLAG_LandscapeMode);
}

// Sets the parameters according to the landscape enhancement mode
static HHOOK s_hConfirmDeleteHook = NULL;

// Message filter of the delete confirmation: pressing Del again (not the auto repeat of the first press) confirms
static LRESULT CALLBACK ConfirmDeleteHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
	if (nCode == MSGF_DIALOGBOX) {
		MSG* pMsg = (MSG*)lParam;
		if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_DELETE && (pMsg->lParam & (1 << 30)) == 0) {
			::PostMessage(::GetAncestor(pMsg->hwnd, GA_ROOT), WM_COMMAND, IDOK, 0);
			return TRUE;
		}
	}
	return ::CallNextHookEx(s_hConfirmDeleteHook, nCode, wParam, lParam);
}

// Asks to delete a file, Enter (default button) or a second Del press confirms, returns true if confirmed
static bool ConfirmDelete(HWND hWnd, LPCTSTR sText, bool bHasRecycleBin) {
	s_hConfirmDeleteHook = ::SetWindowsHookEx(WH_MSGFILTER, ConfirmDeleteHookProc, NULL, ::GetCurrentThreadId());
	int nResult = ::MessageBox(hWnd, sText, CNLS::GetString(_T("Confirm")), MB_OKCANCEL | MB_DEFBUTTON1 | (bHasRecycleBin ? MB_ICONQUESTION : MB_ICONWARNING));
	if (s_hConfirmDeleteHook != NULL) {
		::UnhookWindowsHookEx(s_hConfirmDeleteHook);
		s_hConfirmDeleteHook = NULL;
	}
	return nResult == IDOK;
}

static CImageProcessingParams _SetLandscapeModeParams(bool bLandscapeMode, const CImageProcessingParams& params) {
	if (bLandscapeMode) {
		return CSettingsProvider::This().LandscapeModeParams(params);
	} else {
		return params;
	}
}

// Sets the processing flags according to the landscape enhancement mode
static EProcessingFlags _SetLandscapeModeFlags(EProcessingFlags eFlags) {
	if (GetProcessingFlag(eFlags, PFLAG_LandscapeMode)) {
		eFlags = SetProcessingFlag(eFlags, PFLAG_AutoContrast, true);
		eFlags = SetProcessingFlag(eFlags, PFLAG_LDC, true);
		return eFlags;
	} else {
		return eFlags;
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Public
//////////////////////////////////////////////////////////////////////////////////////////////

CMainDlg::CMainDlg(bool bForceFullScreen) {
	CSettingsProvider& sp = CSettingsProvider::This();

	CResizeFilterCache::This(); // Access before multiple threads are created

	// Read the string table for the requested language if one is present
	CNLS::ReadStringTable(CNLS::GetStringTableFileName(sp.Language()));

	m_bLandscapeMode = sp.LandscapeMode();
	m_pImageProcParams = new CImageProcessingParams(GetDefaultProcessingParams());
	InitFromProcessingFlags(GetDefaultProcessingFlags(m_bLandscapeMode), m_bHQResampling, m_bAutoContrast, m_bAutoContrastSection, m_bLDC, m_bLandscapeMode);

	// Initialize second parameter set using hard coded values, turning off all corrections except sharpening
	m_pImageProcParams2 = new CImageProcessingParams(GetNoProcessingParams()); 
	m_eProcessingFlags2 = PFLAG_HighQualityResampling;

	m_pImageProcParamsKept = new CImageProcessingParams();
	m_eProcessingFlagsKept = PFLAG_HighQualityResampling;
	m_dZoomKept = -1;
	m_offsetKept = CPoint(0, 0);
	m_bCurrentImageInParamDB = false;
	m_bCurrentImageIsSpecialProcessing = false;
	m_dCurrentInitialLightenShadows = -1;

	m_bDefaultSelectionMode = sp.DefaultSelectionMode();
	m_bShowFileName = sp.ShowFileName();
	m_bKeepParams = sp.KeepParams();
	m_eAutoZoomModeWindowed = sp.AutoZoomMode();
	m_eAutoZoomModeFullscreen = sp.AutoZoomModeFullscreen();

	m_eTransitionEffect = sp.SlideShowTransitionEffect();
	m_nTransitionTime = sp.SlideShowEffectTimeMs();

	CHistogramCorr::SetContrastCorrectionStrength((float)sp.AutoContrastAmount());
	CHistogramCorr::SetBrightnessCorrectionStrength((float)sp.AutoBrightnessAmount());

	m_pFileList = NULL;
	m_pJPEGProvider = NULL;
	m_pCurrentImage = NULL;
	m_bOutOfMemoryLastImage = false;
	m_bExceptionErrorLastImage = false;
	m_nLastLoadError = HelpersGUI::FileLoad_Ok;

	m_dMovieFPS = 1.0;
	m_nAutoStartSlideShow = false;
	m_eForcedSorting = Helpers::FS_Undefined;

	m_eProcFlagsBeforeMovie = PFLAG_None;
	m_nRotation = 0;
	m_nUserRotation = 0;
	m_dZoomAtResizeStart = 1.0;
	m_bZoomMode = false;
	m_bZoomModeOnLeftMouse = false;
	m_bUserZoom = false;
	m_bUserPan = false;
	m_bMovieMode = false;
	m_bProcFlagsTouched = false;
	m_bInTrackPopupMenu = false;
	m_bResizeForNewImage = false;
	m_dZoom = -1.0;
	m_dRealizedZoom = -1.0;
	m_dZoomMult = -1.0;
	m_bDragging = false;
	m_bDoDragging = false;
	m_offsets = CPoint(0, 0);
	m_DIBOffsets = CPoint(0, 0);
	m_nCapturedX = m_nCapturedY = 0;
	m_nMouseX = m_nMouseY = 0;
	m_bAutoFitWndToImage = sp.DefaultWndToImage();
	m_bFullScreenMode = bForceFullScreen || (sp.ShowFullScreen() && !sp.AutoFullScreen());
	m_bLockPaint = true;
	m_nCurrentTimeout = 0;
	m_startMouse.x = m_startMouse.y = -1;
	m_virtualImageSize = CSize(-1, -1);
	m_bInZooming = false;
	m_bTemporaryLowQ = false;
	m_bShowZoomFactor = false;
	m_bSpanVirtualDesktop = false;
	m_bPanMouseCursorSet = false;
	m_storedWindowPlacement.length = sizeof(WINDOWPLACEMENT);
	m_nMonitor = 0;
	m_monitorRect = CRect(0, 0, 0, 0);
	m_windowRectOnClose = CRect(0, 0, 0, 0);
	m_bMouseOn = false;
	m_bKeepParametersBeforeAnimation = false;
	m_bIsAnimationPlaying = false;
	m_nLastAnimationOffset = 0;
	m_nExpectedNextAnimationTickCount = 0;
	m_bUseLosslessWEBP = false;
	m_isBeforeFileSelected = true;
	m_dLastImageDisplayTime = 0.0;
	m_isUserFitToScreen = false;
	m_autoZoomFitToScreen = Helpers::ZM_FillScreen;
	m_bWindowBorderless = sp.WindowBorderlessOnStartup();  // unlike AlwaysOnTop, this is set early on initialize as it affects calculations of the window size, position, etc
	m_bAlwaysOnTop = false;  // default normal window.  this will be set to true when AlwaysOnTop is toggled if set to startup in INI
	m_bSelectZoom = false;  // this value is set when LButtonDown happens, to be read by LButtonUp

	m_pPanelMgr = new CPanelMgr();
	m_pZoomNavigatorCtl = NULL;
	m_pEXIFDisplayCtl = NULL;
	m_pWndButtonPanelCtl = NULL;
	m_pInfoButtonPanelCtl = NULL;
	m_pRotationPanelCtl = NULL;
	m_pTiltCorrectionPanelCtl = NULL;
	m_pUnsharpMaskPanelCtl = NULL;
	m_pImageProcPanelCtl = NULL;
	m_pNavPanelCtl = NULL;
	m_pCropCtl = new CCropCtl(this);
	m_pKeyMap = new CKeyMap(); // routine to load the keymap, it's not as simple as just loading one file anymore, but all logic handled by CKeyMap
	m_pPrintImage = new CPrintImage(CSettingsProvider::This().PrintMargin(), CSettingsProvider::This().DefaultPrintWidth());
	m_pHelpDlg = NULL;
	m_pChrome = NULL;
	m_pAnnotations = new CAnnotations();
	m_nRating = -1;
	m_bCropTool = false;
	m_bDragOutPending = false;
	m_nCropModeCommand = IDM_CROPMODE_FREE;
	m_nRunningConversions = 0;
	m_bCompareMode = false;
	m_dCompareSplit = 0.5;
	m_bDraggingCompareSplit = false;
}

CMainDlg::~CMainDlg() {
	delete m_pChrome;
	delete m_pAnnotations;
	delete m_pDirectoryWatcher;
	delete m_pFileList;
	if (m_pJPEGProvider != NULL) delete m_pJPEGProvider;
	delete m_pImageProcParams;
	delete m_pImageProcParamsKept;
	delete m_pZoomNavigatorCtl;
	delete m_pCropCtl;
	delete m_pPanelMgr; // this will delete all panel controllers and all panels
	delete m_pKeyMap;
}

void CMainDlg::SetStartupInfo(LPCTSTR sStartupFile, int nAutostartSlideShow, Helpers::ESorting eSorting, Helpers::ETransitionEffect eEffect, 
	int nTransitionTime, bool bAutoExit, int nDisplayMonitor) { 
	m_sStartupFile = sStartupFile; m_nAutoStartSlideShow = nAutostartSlideShow; m_eForcedSorting = eSorting;
	m_bAutoExit = bAutoExit;
	if ((int)eEffect >= 0) m_eTransitionEffect = eEffect;
	if (nTransitionTime > 0) m_nTransitionTime = nTransitionTime;
	if (nDisplayMonitor >= 0) CSettingsProvider::This().SetMonitorOverride(nDisplayMonitor);
}

LRESULT CMainDlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {	
	UpdateWindowTitle();

	// set the scaling of the screen (DPI) compared to 96 DPI (design value)
	CPaintDC dc(this->m_hWnd);
	HelpersGUI::ScreenScaling = ::GetDeviceCaps(dc, LOGPIXELSX)/96.0f;

	::SetClassLongPtr(m_hWnd, GCLP_HCURSOR, NULL);

	// toolbar, status bar and side panel, must exist before the window size is calculated
	m_pChrome = new CWindowChrome(this);

	m_pDirectoryWatcher = new CDirectoryWatcher(m_hWnd);

	// determine the monitor rectangle and client rectangle
	CSettingsProvider& sp = CSettingsProvider::This();
	m_nMonitor = sp.DisplayMonitor();
	m_monitorRect = CMultiMonitorSupport::GetMonitorRect(m_nMonitor);
	// move to correct monitor - but no redraw as Windows does not like that this early
	::SetWindowPos(m_hWnd, HWND_TOP, m_monitorRect.left, m_monitorRect.top, m_monitorRect.Width(), m_monitorRect.Height(),
		SWP_NOZORDER | SWP_NOCOPYBITS | SWP_NOACTIVATE | SWP_NOREDRAW);
	m_clientRect = m_bFullScreenMode ? m_monitorRect : CMultiMonitorSupport::GetDefaultClientRectInWindowMode(sp.AutoFullScreen());

	// Create image processing panel at bottom of screen
	m_pImageProcPanelCtl = new CImageProcPanelCtl(this, m_pImageProcParams, &m_bLDC, &m_bAutoContrast);
	m_pPanelMgr->AddPanelController(m_pImageProcPanelCtl);

	// Create EXIF display panel
	m_pEXIFDisplayCtl = new CEXIFDisplayCtl(this, m_pImageProcPanelCtl->GetPanel());
	m_pPanelMgr->AddPanelController(m_pEXIFDisplayCtl);

	// Create unsharp mask panel
	m_pUnsharpMaskPanelCtl = new CUnsharpMaskPanelCtl(this, m_pImageProcPanelCtl->GetPanel());
	m_pPanelMgr->AddPanelController(m_pUnsharpMaskPanelCtl);

	// Create rotation panel
	m_pRotationPanelCtl = new CRotationPanelCtl(this, m_pImageProcPanelCtl->GetPanel());
	m_pPanelMgr->AddPanelController(m_pRotationPanelCtl);

	// Create perspective correction panel
	m_pTiltCorrectionPanelCtl = new CTiltCorrectionPanelCtl(this, m_pImageProcPanelCtl->GetPanel());
	m_pPanelMgr->AddPanelController(m_pTiltCorrectionPanelCtl);

	// Create navigation panel
	m_pNavPanelCtl = new CNavigationPanelCtl(this, m_pImageProcPanelCtl->GetPanel(), &m_bFullScreenMode);
	m_pPanelMgr->AddPanelController(m_pNavPanelCtl);

	// Create window button panel (on top, right)
	m_pWndButtonPanelCtl = new CWndButtonPanelCtl(this, m_pImageProcPanelCtl->GetPanel());
	m_pPanelMgr->AddPanelController(m_pWndButtonPanelCtl);

	// Create info button panel (on top, left)
	m_pInfoButtonPanelCtl = new CInfoButtonPanelCtl(this, m_pImageProcPanelCtl->GetPanel());
	m_pPanelMgr->AddPanelController(m_pInfoButtonPanelCtl);

	// Create zoom navigator
	m_pZoomNavigatorCtl = new CZoomNavigatorCtl(this, m_pImageProcPanelCtl->GetPanel(), m_pNavPanelCtl->GetPanel());

	// set icons (for toolbar)
	HICON hIcon = (HICON)::LoadImage(_Module.GetResourceInstance(), MAKEINTRESOURCE(IDR_MAINFRAME), 
		IMAGE_ICON, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR | LR_SHARED);
	SetIcon(hIcon, TRUE);
	HICON hIconSmall = (HICON)::LoadImage(_Module.GetResourceInstance(), MAKEINTRESOURCE(IDR_MAINFRAME), 
		IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR | LR_SHARED);
	SetIcon(hIconSmall, FALSE);

	// turn on/off mouse coursor
	m_bMouseOn = !m_bFullScreenMode;
	::ShowCursor(m_bMouseOn);

	// intitialize list of files to show with startup file (and folder)
	m_pFileList = new CFileList(m_sStartupFile, *m_pDirectoryWatcher,
		(m_eForcedSorting == Helpers::FS_Undefined) ? sp.Sorting() : m_eForcedSorting, sp.IsSortedAscending(), sp.WrapAroundFolder(),
		0, m_eForcedSorting != Helpers::FS_Undefined);
	m_pFileList->SetNavigationMode(sp.Navigation());

	// create thread pool for processing requests on multiple CPU cores
	CProcessingThreadPool::This().CreateThreadPoolThreads();

	// create JPEG provider and request first image - do no processing yet if not in fullscreen mode (as we do not know the size yet)
	m_pJPEGProvider = new CJPEGProvider(m_hWnd, NUM_THREADS, READ_AHEAD_BUFFERS);	
	m_pCurrentImage = m_pJPEGProvider->RequestImage(m_pFileList, CJPEGProvider::FORWARD,
		m_pFileList->Current(), 0, CreateProcessParams(!m_bFullScreenMode), m_bOutOfMemoryLastImage, m_bExceptionErrorLastImage);
	if (m_pCurrentImage != NULL && m_pCurrentImage->IsAnimation()) {
		StartAnimation();
	}
	m_nLastLoadError = GetLoadErrorAfterOpenFile();
	CheckIfApplyAutoFitWndToImage(true);
	AfterNewImageLoaded(true, true, false); // synchronize to per image parameters

	if (!m_bFullScreenMode) {
		// Window mode, set correct window size
		SetCurrentWindowStyle();
		if (!IsAdjustWindowToImage()) {
			CRect windowRect = CMultiMonitorSupport::GetDefaultWindowRect();
			this->SetWindowPos(HWND_TOP, windowRect.left, windowRect.top, windowRect.Width(), windowRect.Height(), SWP_NOZORDER | SWP_NOCOPYBITS);
		} else {
			AdjustWindowToImage(true);
		}
		if (sp.DefaultMaximized()) {
			this->ShowWindow(SW_MAXIMIZE);
		}
	} else {
		PrefetchDIB(m_monitorRect);
		SetWindowLong(GWL_STYLE, WS_VISIBLE);
		SetWindowPos(HWND_TOP, &m_monitorRect, SWP_NOZORDER);
		SetWindowPos(NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS | SWP_FRAMECHANGED);
	}

	if (CSettingsProvider::This().WindowAlwaysOnTopOnStartup()) {
		// if set by default for startup ... it is false by default, toggle = true
		ToggleAlwaysOnTop();
	}

	m_bLockPaint = false;
	m_isBeforeFileSelected = m_sStartupFile.IsEmpty();

	if (m_nAutoStartSlideShow > 0) {
		StartMovieMode(1.0 / m_nAutoStartSlideShow);
	}

	if (!m_bFullScreenMode) {
		m_pChrome->UpdateFrame();
	}

	this->Invalidate(FALSE);

	this->DragAcceptFiles();

	return TRUE;
}

LRESULT CMainDlg::OnPaint(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
	static bool s_bFirst = true;

	if (m_bLockPaint) {
		return 0;
	}

	// On first paint show 'Open File' dialog if no file passed on command line
	if (s_bFirst) {
		s_bFirst = false;
		if (m_sStartupFile.IsEmpty()) {
			if (CSettingsProvider::This().SkipFileOpenDialogOnStartup())
				m_isBeforeFileSelected = false;
			else {
				OpenFileWithDialog(true, true);
				Invalidate(TRUE);
			}
		}
	}

	CPaintDC dc(m_hWnd);
	m_dRealizedZoom = 1.0;

	this->GetClientRect(&m_clientRect);
	CRect imageProcessingArea = m_pImageProcPanelCtl->PanelRect();
	CRectF visRectZoomNavigator(0.0f, 0.0f, 1.0f, 1.0f);
	CBrush backBrush;
	backBrush.CreateSolidBrush(CSettingsProvider::This().ColorBackground());

#ifdef DEBUG
	CString a; a.Format(_T("client rect w/h pix: %d %d = %d\n"), m_clientRect.Width(), m_clientRect.Height(), m_clientRect.Width() * m_clientRect.Height());
	//::OutputDebugString(a);
#endif

	std::list<CRect> excludedClippingRects;

	// Panels are handled over memory DCs to eliminate flickering
	CPaintMemDCMgr memDCMgr(dc);

	if (m_pCurrentImage == NULL) {
		m_pPanelMgr->OnPrePaint(dc);
		m_pPanelMgr->PrepareMemDCMgr(memDCMgr, excludedClippingRects);
		memDCMgr.ExcludeFromClippingRegion(dc, excludedClippingRects);
		dc.FillRect(&m_clientRect, backBrush);
	} else {
		// do this as very first - may changes size of image
		m_pCurrentImage->VerifyRotation(CRotationParams(m_pCurrentImage->GetRotationParams(), m_nRotation));

		// Find out zoom multiplier if not yet known
		if (m_dZoomMult < 0.0) {
			m_dZoomMult = GetZoomMultiplier(m_pCurrentImage, m_clientRect);
		}

		// find out the new vitual image size and the size of the bitmap to request
		CSize newSize = Helpers::GetVirtualImageSize(m_pCurrentImage->OrigSize(),
			m_clientRect.Size(), IsAdjustWindowToImage() ? Helpers::ZM_FitToScreenNoZoom : 
			   (m_isUserFitToScreen ? m_autoZoomFitToScreen : GetAutoZoomMode()), m_dZoom);
		m_virtualImageSize = newSize;
		m_dRealizedZoom = (double)newSize.cx / m_pCurrentImage->OrigSize().cx;
		CPoint unlimitedOffsets = m_offsets;
		m_offsets = Helpers::LimitOffsets(m_offsets, m_clientRect.Size(), newSize);
		m_DIBOffsets = m_bZoomMode ? (unlimitedOffsets - m_offsets) : CPoint(0, 0);

		// Clip to client rectangle and request the DIB
		CSize clippedSize(min(m_clientRect.Width(), newSize.cx), min(m_clientRect.Height(), newSize.cy));
		CPoint offsetsInImage = m_pCurrentImage->ConvertOffset(newSize, clippedSize, m_offsets);

		void* pDIBData;
		if (m_pUnsharpMaskPanelCtl->IsVisible()) {
			pDIBData = m_pUnsharpMaskPanelCtl->GetUSMDIBForPreview(clippedSize, offsetsInImage, 
				*m_pImageProcParams, CreateProcessingFlags(m_bHQResampling && !m_bTemporaryLowQ && !m_bZoomMode, m_bAutoContrast, m_bAutoContrastSection, m_bLDC, false, m_bLandscapeMode));
		} else if (m_pRotationPanelCtl->IsVisible()) {
			pDIBData = m_pRotationPanelCtl->GetDIBForPreview(newSize, clippedSize, offsetsInImage, 
				*m_pImageProcParams, CreateProcessingFlags(false, m_bAutoContrast, m_bAutoContrastSection, m_bLDC, false, m_bLandscapeMode));
		} else if (m_pTiltCorrectionPanelCtl->IsVisible()) {
			pDIBData = m_pTiltCorrectionPanelCtl->GetDIBForPreview(newSize, clippedSize, offsetsInImage, 
				*m_pImageProcParams, CreateProcessingFlags(false, m_bAutoContrast, m_bAutoContrastSection, m_bLDC, false, m_bLandscapeMode));
		} else {
			pDIBData = m_pCurrentImage->GetDIB(newSize, clippedSize, offsetsInImage,
				*m_pImageProcParams,
				CreateProcessingFlags(m_bHQResampling && !m_bTemporaryLowQ && !m_bZoomMode, m_bAutoContrast, m_bAutoContrastSection, m_bLDC, false, m_bLandscapeMode));
			if (m_bCompareMode && pDIBData != NULL) {
				pDIBData = ComposeCompareDIB(pDIBData, clippedSize);
			}
		}

		// Zoom navigator - check if visible and create exclusion rectangle
		if (m_pZoomNavigatorCtl->IsVisible()) {
			visRectZoomNavigator = m_pZoomNavigatorCtl->GetVisibleRect(newSize, clippedSize, offsetsInImage);
			excludedClippingRects.push_back(m_pZoomNavigatorCtl->PanelRect());
		}

		m_pPanelMgr->OnPrePaint(dc);
		m_pPanelMgr->PrepareMemDCMgr(memDCMgr, excludedClippingRects);
		memDCMgr.ExcludeFromClippingRegion(dc, excludedClippingRects);

		// Paint the DIB
		if (pDIBData != NULL) {
			BITMAPINFO bmInfo{ 0 };
			CPoint ptDIBStart = HelpersGUI::DrawDIB32bppWithBlackBorders(dc, bmInfo, pDIBData, backBrush, m_clientRect, clippedSize, m_DIBOffsets);
			// The DIB is also blitted into the memory DCs of the panels
			memDCMgr.BlitImageToMemDC(pDIBData, &bmInfo, ptDIBStart, m_pNavPanelCtl->CurrentBlendingFactor());
		}
		if (m_bZoomMode) m_offsets = unlimitedOffsets;
	}

	// Restore the old clipping region by adding the excluded rectangles again
	memDCMgr.IncludeIntoClippingRegion(dc, excludedClippingRects);

	// paint zoom navigator
	CTrapezoid trapezoid = m_pTiltCorrectionPanelCtl->IsVisible() ? m_pTiltCorrectionPanelCtl->GetCurrentTrapezoid(CZoomNavigator::GetNavigatorRect(m_pCurrentImage, m_pImageProcPanelCtl->PanelRect(), m_pNavPanelCtl->PanelRect()).Size()) : CTrapezoid();
	m_pZoomNavigatorCtl->OnPaint(dc, visRectZoomNavigator, m_pImageProcParams,
		CreateProcessingFlags(!m_pRotationPanelCtl->IsVisible() && !m_pTiltCorrectionPanelCtl->IsVisible(), m_bAutoContrast, false, m_bLDC, false, m_bLandscapeMode), 
		m_pRotationPanelCtl->IsVisible() ? m_pRotationPanelCtl->GetLQRotationAngle() : 0.0, 
		m_pTiltCorrectionPanelCtl->IsVisible() ? &trapezoid : NULL);

	// Display file name if enabled
	DisplayFileName(imageProcessingArea, dc, m_dRealizedZoom);

	// Display errors and warnings
	DisplayErrors(m_pCurrentImage, m_clientRect, dc);

	// Now blit the memory DCs of the panels to the screen
	memDCMgr.PaintMemDCToScreen();

	// Show timing info if requested
	if (SHOW_TIMING_INFO && m_pCurrentImage != NULL) {
		TCHAR buff[256];
		_stprintf_s(buff, 256, _T("Loading: %.2f ms, Last op: %.2f ms, Last resize: %s, Last sharpen: %.2f ms"), m_pCurrentImage->GetLoadTickCount(), 
			m_pCurrentImage->LastOpTickCount(), CBasicProcessing::TimingInfo(), m_pCurrentImage->GetUnsharpMaskTickCount());
		dc.SetTextColor(RGB(255, 255, 255));
		dc.SetBkMode(OPAQUE);
		dc.TextOut(5, 5, buff);
		dc.SetBkMode(TRANSPARENT);
	}

	// Show current zoom factor
	if (m_bInZooming || m_bShowZoomFactor) {
		TCHAR buff[32];
		_stprintf_s(buff, 32, _T("%d %%"), int(m_dZoom*100 + 0.5));
		dc.SetTextColor(CSettingsProvider::This().ColorGUI());
		HelpersGUI::SelectDefaultFileNameFont(dc);
		HelpersGUI::DrawTextBordered(dc, buff, GetZoomTextRect(imageProcessingArea), DT_RIGHT);
	}

	// let crop controller and panels paint its stuff
	m_pCropCtl->OnPaint(dc);
	if (m_pCurrentImage != NULL && !m_pAnnotations->IsEmpty()) {
		PaintAnnotations(dc);
	}
	if (m_bCompareMode && m_pCurrentImage != NULL) {
		PaintCompareSplit(dc);
	}
	m_pPanelMgr->OnPostPaint(dc);

	SetCursorForMoveSection();

	// status bar and side panel show information about the image that may have changed
	m_pChrome->Paint();

	return 0;
}

void CMainDlg::PaintToDC(CDC& dc) {
	COLORREF backColor = CSettingsProvider::This().ColorBackground();
	if (backColor == 0)
		backColor = RGB(0, 0, 1); // these f**ing nVidia drivers have a bug when blending pure black
	CBrush backBrush;
	backBrush.CreateSolidBrush(backColor);
	m_dRealizedZoom = 1.0;

	CJPEGImage* pCurrentImage = GetCurrentImage();
	if (pCurrentImage == NULL) {
		dc.FillRect(&m_clientRect, backBrush);
	} else {
		// do this as very first - may changes size of image
		pCurrentImage->VerifyRotation(CRotationParams(pCurrentImage->GetRotationParams(), GetRotation()));

		// find out the new virtual image size and the size of the bitmap to request
		double dZoom = m_dZoom;
		CSize newSize = Helpers::GetVirtualImageSize(pCurrentImage->OrigSize(), m_clientRect.Size(), GetAutoZoomMode(), dZoom);
		CPoint offsets = Helpers::LimitOffsets(GetOffsets(), m_clientRect.Size(), newSize);
		m_dRealizedZoom = (double)newSize.cx / m_pCurrentImage->OrigSize().cx;

		// Clip to client rectangle and request the DIB
		CSize clippedSize(min(m_clientRect.Width(), newSize.cx), min(m_clientRect.Height(), newSize.cy));
		CPoint offsetsInImage = pCurrentImage->ConvertOffset(newSize, clippedSize, offsets);

		void* pDIBData = pCurrentImage->GetDIB(newSize, clippedSize, offsetsInImage, 
				*GetImageProcessingParams(),
				CreateDefaultProcessingFlags());
		if (pDIBData != NULL) {
			BITMAPINFO bmInfo{ 0 };
			CPoint ptDIBStart = HelpersGUI::DrawDIB32bppWithBlackBorders(dc, bmInfo, pDIBData, backBrush, m_clientRect, clippedSize, CPoint(0, 0));
		}

		CRect imageProcessingArea = m_pImageProcPanelCtl->PanelRect();

		if (m_pEXIFDisplayCtl->IsVisible()) {
			m_pEXIFDisplayCtl->OnPrePaintMainDlg(dc);
			BlendBlackRect(dc, *m_pEXIFDisplayCtl->GetPanel(), 0.5f); 
			m_pEXIFDisplayCtl->OnPaintPanel(dc, CPoint(0, 0));
		}

		DisplayFileName(imageProcessingArea, dc, m_dRealizedZoom);
		DisplayErrors(pCurrentImage, m_clientRect, dc);
	}
}

void CMainDlg::BlendBlackRect(CDC & targetDC, CPanel& panel, float fBlendFactor) {
	int nW = panel.PanelRect().Width(), nH = panel.PanelRect().Height();
	CDC memDCPanel;
	memDCPanel.CreateCompatibleDC(targetDC);
	CBitmap bitmapPanel;
	bitmapPanel.CreateCompatibleBitmap(targetDC, nW, nH);
	memDCPanel.SelectBitmap(bitmapPanel);
	memDCPanel.FillSolidRect(0, 0, nW, nH, RGB(0, 0, 1)); // nVidia workaround: blending pure black has a bug
	
	BLENDFUNCTION blendFunc{ 0 };
	blendFunc.BlendOp = AC_SRC_OVER;
	blendFunc.SourceConstantAlpha = (unsigned char)(fBlendFactor*255 + 0.5f);
	blendFunc.AlphaFormat = 0;
	targetDC.AlphaBlend(panel.PanelRect().left, panel.PanelRect().top, nW, nH, memDCPanel, 0, 0, nW, nH, blendFunc);
}

void CMainDlg::DisplayErrors(CJPEGImage* pCurrentImage, const CRect& clientRect, CDC& dc) {
	dc.SetTextColor(CSettingsProvider::This().ColorGUI());
	if (m_sStartupFile.IsEmpty() && m_pCurrentImage == NULL) {
		CRect rectText(0, clientRect.Height()/2 - HelpersGUI::ScaleToScreen(40), clientRect.Width(), clientRect.Height());
		if (m_isBeforeFileSelected) {
			dc.DrawText(CNLS::GetString(_T("Select file to display in 'File Open' dialog")), -1, &rectText, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
		} else {
			dc.DrawText(CString(CNLS::GetString(_T("No image loaded!"))) + _T("\n\n") + CNLS::GetString(_T("Right mouse button: Context menu")) + _T("\nCtrl-V: ") +
				CNLS::GetString(_T("Paste from clipboard")) + _T("\nCtrl-O: ") + CNLS::GetString(_T("Open new image or slideshow file")) + _T("\n\n") +
				CNLS::GetString(_T("Press ESC to exit...")), -1, &rectText, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
	   }
	} else if (pCurrentImage == NULL) {
		HelpersGUI::DrawImageLoadErrorText(dc, clientRect,
			(m_nLastLoadError == HelpersGUI::FileLoad_SlideShowListInvalid) ? m_sStartupFile :
			(m_nLastLoadError == HelpersGUI::FileLoad_NoFilesInDirectory) ? m_pFileList->CurrentDirectory() : CurrentFileName(false),
			m_nLastLoadError,
			(m_bOutOfMemoryLastImage ? HelpersGUI::FileLoad_OutOfMemory : 0) | (m_bExceptionErrorLastImage ? HelpersGUI::FileLoad_ExceptionError : 0));
	}
}

void CMainDlg::DisplayFileName(const CRect& imageProcessingArea, CDC& dc, double realizedZoom) {
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(CSettingsProvider::This().ColorFileName());
	dc.SetBkColor(RGB(0, 0, 0));

	// the status bar shows the file name next to the image instead of over it
	if (m_bShowFileName && !IsStatusBarShown()) {
		HelpersGUI::SelectDefaultFileNameFont(dc);
		CString sFileName = Helpers::GetFileInfoString(CSettingsProvider::This().FileNameFormat(), m_pCurrentImage, m_pFileList, realizedZoom);
		HelpersGUI::DrawTextBordered(dc, sFileName, CRect(HelpersGUI::ScaleToScreen(2) + imageProcessingArea.left, 0, imageProcessingArea.right, HelpersGUI::ScaleToScreen(30)), DT_LEFT); 
	}
}

LRESULT CMainDlg::OnSize(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	bool bKeepFitToScreen = !m_bResizeForNewImage && fabs(m_dZoom - GetZoomFactorForFitToScreen(false, false)) < 0.01;
	this->GetClientRect(&m_clientRect);
	this->Invalidate(FALSE);
	if (m_clientRect.Width() < HelpersGUI::ScaleToScreen(800)) {
		if (m_pImageProcPanelCtl != NULL) m_pImageProcPanelCtl->SetVisible(false);
	}
	if (m_pNavPanelCtl != NULL) {
		m_pNavPanelCtl->AdjustMaximalWidth(m_clientRect.Width() - HelpersGUI::ScaleToScreen(16));
	}
	m_nMouseX = m_nMouseY = -1;

	// keep fit to screen
	if (bKeepFitToScreen) {
		if (fabs(m_dZoom - GetZoomFactorForFitToScreen(false, false)) >= 0.00001) {
			StartLowQTimer(ZOOM_TIMEOUT);
		}
		ResetZoomToFitScreen(false, false, false);
	}
	return 0;
}

LRESULT CMainDlg::OnGetMinMaxInfo(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	if (m_pJPEGProvider != NULL) {
		MINMAXINFO* pMinMaxInfo = (MINMAXINFO*) lParam;
		CSize minimalSize = CSettingsProvider::This().MinimalWindowSize();
		pMinMaxInfo->ptMinTrackSize = CPoint(max(0, minimalSize.cx), max(0, minimalSize.cy));
		return 1;
	} else {
		return 0;
	}
}

LRESULT CMainDlg::OnAnotherInstanceStarted(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
	bHandled = FALSE;
	COPYDATASTRUCT* pData = (COPYDATASTRUCT*)lParam;
	if (pData != NULL && pData->dwData == KEY_MAGIC && pData->cbData > 0 && 
		((m_bFullScreenMode && CSettingsProvider::This().SingleFullScreenInstance()) || CSettingsProvider::This().SingleInstance())) {
		m_sStartupFile = CString((LPCTSTR)pData->lpData, pData->cbData / sizeof(TCHAR) - 1);
		::PostMessage(m_hWnd, WM_LOAD_FILE_ASYNCH, 0, KEY_MAGIC);
		bHandled = TRUE;
		return KEY_MAGIC;
	}
	return 0;
}

LRESULT CMainDlg::OnLoadFileAsynch(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
	bHandled = lParam == KEY_MAGIC;
	if (lParam == KEY_MAGIC && ::IsWindowEnabled(m_hWnd)) {
		m_pPanelMgr->CancelModalPanel();
		GetNavPanelCtl()->HideNavPanelTemporary(true);
		StopMovieMode();
		StopAnimation();
		MouseOn();
		if (m_sStartupFile.IsEmpty()) {
			OpenFile(false, false);
		} else {
			OpenFile(m_sStartupFile, false);
		}
	}
	return 0;
}

LRESULT CMainDlg::OnClose(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
	GetWindowRect(m_windowRectOnClose);
	bHandled = FALSE;
	return 0;
}

LRESULT CMainDlg::OnLButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	this->SetCapture();
	bool isCropping = m_pCropCtl->IsCropping();
	CPoint pointClicked(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	if (IsDrawMode() && !isCropping) {
		float fX = (float)pointClicked.x, fY = (float)pointClicked.y;
		ScreenToImage(fX, fY);
		if (m_pAnnotations->GetTool() == CAnnotations::TOOL_REMOVE_COLOR) {
			::ReleaseCapture();
			RemoveColorAt(fX, fY);
			return 0;
		}
		if (m_pAnnotations->GetTool() == CAnnotations::TOOL_TEXT) {
			::ReleaseCapture();
			CString sText;
			if (CAnnotations::AskForText(m_hWnd, CNLS::GetString(_T("Text")), CNLS::GetString(_T("OK")), CNLS::GetString(_T("Cancel")), sText)) {
				m_pAnnotations->AddText(fX, fY, m_dRealizedZoom, sText);
			}
		} else {
			m_pAnnotations->BeginShape(fX, fY, m_dRealizedZoom);
		}
		Invalidate(FALSE);
		m_pChrome->Paint();
		return 0;
	}
	if (m_bCompareMode && !isCropping && IsOnCompareSplit(pointClicked.x)) {
		m_bDraggingCompareSplit = true;
		::SetCursor(::LoadCursor(NULL, IDC_SIZEWE));
		return 0;
	}
	bool bEatenByPanel = isCropping ? false : m_pPanelMgr->OnMouseLButton(MouseEvent_BtnDown, pointClicked.x, pointClicked.y);

	if (!bEatenByPanel) {
		bool bCtrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
		bool bShift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;

		bool bDraggingRequired = m_virtualImageSize.cx > m_clientRect.Width() || m_virtualImageSize.cy > m_clientRect.Height();
		bool bHandleByCropping = isCropping || m_pCropCtl->HitHandle(pointClicked.x, pointClicked.y) != CCropCtl::HH_None;
		bool bTransformPanelShown = m_pRotationPanelCtl->IsVisible() || m_pTiltCorrectionPanelCtl->IsVisible();
		if (bHandleByCropping || !m_pZoomNavigatorCtl->OnMouseLButton(MouseEvent_BtnDown, pointClicked.x, pointClicked.y)) {
			if (!m_bZoomModeOnLeftMouse && !bHandleByCropping && HandleMouseButtonByKeymap(VK_LBUTTON)) {
				return 0;
			}
			bool bZoomMode = (bShift || m_bZoomModeOnLeftMouse) && !bCtrl && !bTransformPanelShown && !bHandleByCropping && !m_pUnsharpMaskPanelCtl->IsVisible();
			if (bZoomMode) {
				m_bZoomMode = true;
				m_dStartZoom = m_dZoom;
				m_nCapturedX = m_nMouseX; m_nCapturedY = m_nMouseY;
			} else if ((bCtrl || bHandleByCropping || m_bCropTool || (!bDraggingRequired && m_bDefaultSelectionMode)) && !bTransformPanelShown) {
				// always go into selection/crop when in the right state and CTRL held down, otherwise it depends on the DefaultSelectionMode setting
				m_bSelectZoom = bShift;  // if shift, go into select-to-zoom mode (no crop popup)
				m_pCropCtl->StartCropping(pointClicked.x, pointClicked.y);
			} else if (bDraggingRequired && !bTransformPanelShown) {
				StartDragging(pointClicked.x, pointClicked.y, false);
			} else if (!bTransformPanelShown && m_pCurrentImage != NULL && !m_pCurrentImage->IsClipboardImage()) {
				// the image fits the window, dragging moves the file to other applications as in Windows Photos
				m_bDragOutPending = true;
				m_dragOutStart = pointClicked;
			} 
		}
		SetCursorForMoveSection();
	}

	m_pCropCtl->ResetStartCropOnNextClick();
	return 0;
}

LRESULT CMainDlg::OnLButtonUp(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	m_bDragOutPending = false;
	if (m_pChrome->IsDragging()) {
		// slider of the side panel, the drag was started in the non-client area
		m_pChrome->EndDrag();
	} else if (m_pAnnotations->IsDrawing()) {
		m_pAnnotations->EndShape();
		m_pChrome->Paint();
	} else if (m_bDraggingCompareSplit) {
		m_bDraggingCompareSplit = false;
	} else if (m_bZoomMode) {
		m_bZoomMode = false;
		AdjustWindowToImage(false);
		Invalidate(FALSE);
	} else if (m_bDragging) {
		EndDragging();
	} else if (m_pCropCtl->IsCropping()) {
		m_pCropCtl->EndCropping(!m_bSelectZoom);
		if (m_bSelectZoom) {
			// select to zoom
			m_bSelectZoom = false;
			ZoomToSelection();
			m_pCropCtl->AbortCropping();
		}
	} else {
		m_pPanelMgr->OnMouseLButton(MouseEvent_BtnUp, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
	}
	::ReleaseCapture();
	InvalidateHelpDlg();
	return 0;
}


// based on https://www.codeproject.com/Articles/18400/How-to-move-a-dialog-which-does-not-have-a-caption
// https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-nchittest
LRESULT CMainDlg::OnNCHitTest(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if (m_pChrome != NULL) {
		LRESULT nHitTest = m_pChrome->HitTest(CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
		if (nHitTest != 0) {
			return nHitTest;
		}
	}

	bool bAlt = (::GetKeyState(VK_MENU) & 0x8000) != 0;
	bool bLButton = ::GetAsyncKeyState(MK_LBUTTON) < 0;

	// only move when alt is held down, double click causes this to expand as well
	if (!m_bFullScreenMode && bAlt && ::DefWindowProc(m_hWnd, uMsg, wParam, lParam) == HTCLIENT && bLButton) {
		// don't allow intercepting if we're in full screen mode
		// (which is really just the window repositioned so the titlebar falls off the screen)
		return HTCAPTION;
	}

	bHandled = FALSE;  // if not moving window, considered unhandled, or else all the mouse button code stops working
	return 0;
}

LRESULT CMainDlg::OnNCLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if (m_pChrome != NULL && wParam == CWindowChrome::HT_CHROME) {
		m_pChrome->OnNCLButtonDown(CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)), uMsg == WM_NCLBUTTONDBLCLK);
		return 0;
	}
	m_dZoomAtResizeStart = m_dZoom;
	bHandled = FALSE; // do not handle message, this would block correct processing by OS
	return 0;
}

LRESULT CMainDlg::OnNCLButtonUp(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if (m_pChrome != NULL && wParam == CWindowChrome::HT_CHROME) {
		m_pChrome->OnNCLButtonUp(CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
		return 0;
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CMainDlg::OnNCMouseMove(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
	if (m_pChrome != NULL) {
		m_pChrome->OnNCMouseMove(CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CMainDlg::OnNCMouseLeave(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	if (m_pChrome != NULL) {
		m_pChrome->OnNCMouseLeave();
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CMainDlg::OnNCCalcSize(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if (m_pChrome == NULL) {
		bHandled = FALSE;
		return 0;
	}
	// let Windows calculate the standard frame, then reserve the space for toolbar, status bar and side panel
	LRESULT lResult = ::DefWindowProc(m_hWnd, uMsg, wParam, lParam);
	RECT* pClientRect = wParam ? &(((NCCALCSIZE_PARAMS*)lParam)->rgrc[0]) : (RECT*)lParam;
	m_pChrome->OnNCCalcSize(pClientRect);
	return lResult;
}

LRESULT CMainDlg::OnNCPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if (m_pChrome == NULL || !m_pChrome->IsEnabled()) {
		bHandled = FALSE;
		return 0;
	}
	::DefWindowProc(m_hWnd, uMsg, wParam, lParam);
	m_pChrome->Paint();
	return 0;
}

LRESULT CMainDlg::OnNCActivate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
	if (m_pChrome == NULL || !m_pChrome->IsEnabled()) {
		bHandled = FALSE;
		return 0;
	}
	LRESULT lResult = ::DefWindowProc(m_hWnd, uMsg, wParam, lParam);
	m_pChrome->Paint();
	return lResult;
}

LRESULT CMainDlg::OnSetCursor(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
	// the window class has no cursor, set it for the toolbar and side panel
	if (LOWORD(lParam) == CWindowChrome::HT_CHROME) {
		::SetCursor(::LoadCursor(NULL, IDC_ARROW));
		return TRUE;
	}
	bHandled = FALSE;
	return 0;
}

LRESULT CMainDlg::OnRButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
	bHandled = HandleMouseButtonByKeymap(VK_RBUTTON);
	return 0;
}

LRESULT CMainDlg::OnRButtonUp(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	bHandled = HandleMouseButtonByKeymap(VK_RBUTTON, false);
	return 0;
}

LRESULT CMainDlg::OnMButtonDown(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	this->SetCapture();
	if (HandleMouseButtonByKeymap(VK_MBUTTON)) {
		return 0;
	}
	StartDragging(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), false);
	return 0;
}

LRESULT CMainDlg::OnMButtonUp(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	EndDragging();
	::ReleaseCapture();
	return 0;
}

LRESULT CMainDlg::OnLButtonDblClk(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	if (!m_bDragging && !m_pCropCtl->IsCropping()) {
		if (m_pPanelMgr->OnMouseLButton(MouseEvent_BtnDblClk, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam))) {
			return 0;
		}
	}
	if (!m_pCropCtl->IsCropping() && m_pCurrentImage != NULL) {
		if (HandleMouseButtonByKeymap(VK_LBUTTONDBLCLK)) {
			return 0;
		}
		double dZoom = -1.0;
		CSize sizeAutoZoom = Helpers::GetVirtualImageSize(m_pCurrentImage->OrigSize(),
			m_clientRect.Size(), IsAdjustWindowToImage() ? Helpers::ZM_FitToScreenNoZoom : GetAutoZoomMode(), dZoom);
		if (sizeAutoZoom != m_virtualImageSize) {
			ExecuteCommand(GetAutoZoomMode() * 10 + IDM_AUTO_ZOOM_FIT_NO_ZOOM);
		} else {
			ResetZoomTo100Percents(true);
		}
	}
	return 0;
}

LRESULT CMainDlg::OnXButtonDown(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	if (!m_pPanelMgr->IsModalPanelShown()) {
		if (HandleMouseButtonByKeymap((GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? VK_XBUTTON1 : VK_XBUTTON2)) {
			return 0;
		}
		bool bExchangeXButtons = CSettingsProvider::This().ExchangeXButtons();
		if (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) {
			GotoImage(bExchangeXButtons ? POS_Next : POS_Previous);
		} else {
			GotoImage(bExchangeXButtons ? POS_Previous : POS_Next);
		}
	}
	return 0;
}

LRESULT CMainDlg::OnMouseMove(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	// Turn mouse pointer on when mouse has moved some distance
	int nOldMouseY = m_nMouseY;
	int nOldMouseX = m_nMouseX;
	m_nMouseX = GET_X_LPARAM(lParam);
	m_nMouseY = GET_Y_LPARAM(lParam);
	if (m_pChrome->IsDragging()) {
		CPoint ptScreen(m_nMouseX, m_nMouseY);
		ClientToScreen(&ptScreen);
		m_pChrome->OnDragMove(ptScreen);
		return 0;
	}
	if (m_bDragOutPending && (::GetKeyState(VK_LBUTTON) & 0x8000) != 0) {
		if (abs(m_nMouseX - m_dragOutStart.x) > ::GetSystemMetrics(SM_CXDRAG) || abs(m_nMouseY - m_dragOutStart.y) > ::GetSystemMetrics(SM_CYDRAG)) {
			m_bDragOutPending = false;
			::ReleaseCapture();
			if (m_pFileList->Current() != NULL) {
				Share::DragFile(m_hWnd, m_pFileList->Current());
			}
		}
		return 0;
	}
	m_bDragOutPending = false;
	if (m_pAnnotations->IsDrawing()) {
		float fX = (float)m_nMouseX, fY = (float)m_nMouseY;
		ScreenToImage(fX, fY);
		m_pAnnotations->ContinueShape(fX, fY);
		::SetCursor(::LoadCursor(NULL, IDC_CROSS));
		Invalidate(FALSE);
		return 0;
	}
	if (m_bDraggingCompareSplit) {
		m_dCompareSplit = max(0.0, min(1.0, (double)m_nMouseX / max(1, m_clientRect.Width())));
		::SetCursor(::LoadCursor(NULL, IDC_SIZEWE));
		Invalidate(FALSE);
		return 0;
	}
	if (!m_bDragging) {
		if (m_startMouse.x == -1 && m_startMouse.y == -1) {
			m_startMouse.x = m_nMouseX;
			m_startMouse.y = m_nMouseY;
		} else {
			if (abs(m_nMouseX - m_startMouse.x) + abs(m_nMouseY - m_startMouse.y) > 50) {
				MouseOn();
			}
		}
	}

	// Do dragging or cropping when needed, else pass event to panel manager and zoom navigator
	bool bMouseCursorSet = false;
	if (m_bZoomMode) {
		int nDX = m_nMouseX - m_nCapturedX;
		double dFactor = 1 + pow(nDX * nDX, 0.8) / 1500;
		if (nDX < 0) dFactor = 1.0/dFactor;
		PerformZoom(m_dStartZoom * dFactor, false, true, false);
	} else if (m_bDragging) {
		DoDragging();
	} else if (m_pCropCtl->IsCropping()) {
		bMouseCursorSet = m_pCropCtl->DoCropping(m_nMouseX, m_nMouseY);
	} else if (!m_pPanelMgr->OnMouseMove(m_nMouseX, m_nMouseY)) {
		m_pZoomNavigatorCtl->OnMouseMove(nOldMouseX, nOldMouseY);
	}
	if (!m_bPanMouseCursorSet && !bMouseCursorSet) {
		if (!m_pPanelMgr->MouseCursorCaptured()) {
			bool bOnSplit = m_bCompareMode && !m_bDragging && !m_pCropCtl->IsCropping() && IsOnCompareSplit(m_nMouseX);
			::SetCursor(::LoadCursor(NULL, (IsDrawMode() || (m_bCropTool && m_pCurrentImage != NULL)) ? IDC_CROSS : bOnSplit ? IDC_SIZEWE : IDC_ARROW));
		}
	}

	return 0;
}

LRESULT CMainDlg::OnMouseWheel(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/) {
	bool bCtrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
	int nDelta = GET_WHEEL_DELTA_WPARAM(wParam);
	if (m_pChrome->OnMouseWheel(CPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)), nDelta)) {
		return 0; // side panel scrolled
	}
	if (!bCtrl && CSettingsProvider::This().NavigateWithMouseWheel() && !m_pPanelMgr->IsModalPanelShown()) {
		if (nDelta < 0) {
			GotoImage(POS_Next);
		} else if (nDelta > 0) {
			GotoImage(POS_Previous);
		}
	} else if (m_dZoom > 0 && !m_pUnsharpMaskPanelCtl->IsVisible()) {
		PerformZoom(CSettingsProvider::This().MouseWheelZoomSpeed() * double(nDelta) / WHEEL_DELTA, true, m_bMouseOn, true);
	}
	return 0;
}

LRESULT CMainDlg::OnKeyDown(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	bool bCtrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
	bool bShift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
	bool bAlt = (::GetKeyState(VK_MENU) & 0x8000) != 0;
	if (m_pPanelMgr->OnKeyDown((unsigned int)wParam, bShift, bAlt, bCtrl)) {
		return 1; // a panel has handled the key
	}
	bool bHandled = false;
	if (wParam == VK_ESCAPE && CloseHelpDlg()) {
		bHandled = true;
	} else if (wParam == VK_ESCAPE && m_pCropCtl->IsCropping()) {
		bHandled = true;
		m_pCropCtl->AbortCropping();
	} else if (!bCtrl && wParam != VK_ESCAPE && m_nLastLoadError == HelpersGUI::FileLoad_NoFilesInDirectory && !m_sStartupFile.IsEmpty()) {
		// search in subfolders if initial directory has no images
		bHandled = true;
		m_pFileList->SetNavigationMode(Helpers::NM_LoopSubDirectories);
		GotoImage(POS_Next);
	} else if (!bCtrl && !bShift && !bAlt && ((wParam >= '0' && wParam <= '5') || (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD5))) {
		// star rating as in Windows Photos and Lightroom, 0 removes it
		bHandled = true;
		SetRating((int)wParam - ((wParam >= VK_NUMPAD0) ? VK_NUMPAD0 : '0'));
	} else if (wParam >= '1' && wParam <= '9' && (!bShift || bCtrl)) {
		// Start the slideshow
		bHandled = true;
		int nValue = (int)wParam - '1' + 1;
		if (bCtrl && bShift) {
			nValue *= 10; // 1/100 seconds
		} else if (bCtrl) {
			nValue *= 100; // 1/10 seconds
		} else {
			nValue *= 1000; // seconds
		}
		StartMovieMode(1000.0/nValue);
	} else if (wParam == VK_F1) {
		bHandled = true;
		ExecuteCommand(IDM_HELP);
	} else {
		int nCommand = m_pKeyMap->GetCommandIdForKey((int)wParam, bAlt, bCtrl, bShift);
		if (nCommand > 0) {
			bHandled = true;
			ExecuteCommand(nCommand);
		}
	}

	if (!bHandled) {
		// look if any of the user commands wants to handle this key
		if (m_pFileList->Current() != NULL) {
			HandleUserCommands(CKeyMap::GetCombinedKeyCode((int)wParam, bAlt, bCtrl, bShift));
		}
	}

	return 1;
}

LRESULT CMainDlg::OnSysKeyDown(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	if (m_pPanelMgr->IsModalPanelShown()) {
		return 1;
	}
	bool bShift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
	if (wParam == VK_F4) {
		CleanupAndTerminate();
	} else {
		int nCommand = m_pKeyMap->GetCommandIdForKey((int)wParam, true, false, bShift);
		if (nCommand > 0) {
			ExecuteCommand(nCommand);
		} else if (m_pFileList->Current() != NULL) {
			HandleUserCommands(CKeyMap::GetCombinedKeyCode((int)wParam, true, false, bShift));
		}
	}
	return 1;
}


LRESULT CMainDlg::OnGetDlgCode(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	// need to request key messages, else the dialog proc eats them all up
	return DLGC_WANTALLKEYS;
}

LRESULT CMainDlg::OnImageLoadCompleted(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	// route to JPEG provider
	m_pJPEGProvider->OnImageLoadCompleted((int)lParam);
	return 0;
}

LRESULT CMainDlg::OnDisplayedFileChangedOnDisk(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	if (CSettingsProvider::This().ReloadWhenDisplayedImageChanged() && m_pCurrentImage != NULL && !m_pCurrentImage->IsClipboardImage() &&
		m_pFileList != NULL && m_pFileList->CanOpenCurrentFileForReading()) {
		ExecuteCommand(IDM_RELOAD);
	}
	return 0;
}

LRESULT CMainDlg::OnActiveDirectoryFilelistChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	if (CSettingsProvider::This().ReloadWhenDisplayedImageChanged() && m_pFileList != NULL && m_pFileList->CurrentFileExists()) {
		m_pFileList->Reload(NULL, false);
		Invalidate(FALSE);
	}
	return 0;
}

LRESULT CMainDlg::OnDropFiles(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	HDROP hDrop = (HDROP) wParam;
	if (hDrop != NULL && !m_pPanelMgr->IsModalPanelShown()) {
		const int BUFF_SIZE = 512;
		TCHAR buff[BUFF_SIZE];
		if (::DragQueryFile(hDrop, 0, (LPTSTR) &buff, BUFF_SIZE - 1) > 0) {
			if (::GetFileAttributes(buff) & FILE_ATTRIBUTE_DIRECTORY) {
				_tcsncat_s(buff, BUFF_SIZE, _T("\\"), BUFF_SIZE);
			}
			OpenFile(buff, false);
		}
		::DragFinish(hDrop);
	}
	return 0;
}

LRESULT CMainDlg::OnTimer(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	if (wParam == ANIMATION_TIMER_EVENT_ID || (wParam == SLIDESHOW_TIMER_EVENT_ID && m_nCurrentTimeout > 0)) {
		// Remove all timer messages for slideshow and animation events that accumulated in the queue
		MSG msg;
		while (::PeekMessage(&msg, this->m_hWnd, WM_TIMER, WM_TIMER, PM_REMOVE)) {
			if (msg.wParam != SLIDESHOW_TIMER_EVENT_ID && msg.wParam != ANIMATION_TIMER_EVENT_ID) {
				BOOL bNotUsed;
				OnTimer(WM_TIMER, msg.wParam, msg.lParam, bNotUsed);
			}
			if (msg.wParam == SLIDESHOW_TIMER_EVENT_ID && wParam == ANIMATION_TIMER_EVENT_ID) {
				// if there are queued slideshow timer events and we process an animation event, the slideshow event has preceedence
				wParam = SLIDESHOW_TIMER_EVENT_ID;
			}
		}
		// Goto next image if no other messages to process are pending
		if (!::PeekMessage(&msg, this->m_hWnd, 0, 0, PM_NOREMOVE)) {
			int nRealDisplayTimeMs = ::GetTickCount() - m_nLastSlideShowImageTickCount;
			if (m_nCurrentTimeout > 250 && wParam == SLIDESHOW_TIMER_EVENT_ID) {
				if (m_nCurrentTimeout - nRealDisplayTimeMs > 100) {
					// restart timer
					::Sleep(m_nCurrentTimeout - nRealDisplayTimeMs);
					::KillTimer(this->m_hWnd, SLIDESHOW_TIMER_EVENT_ID);
					::SetTimer(this->m_hWnd, SLIDESHOW_TIMER_EVENT_ID, m_nCurrentTimeout, NULL);
				}
			}
			GotoImage((wParam == ANIMATION_TIMER_EVENT_ID) ? POS_NextAnimation : POS_NextSlideShow, NO_REMOVE_KEY_MSG);
			if (wParam == SLIDESHOW_TIMER_EVENT_ID && UseSlideShowTransitionEffect()) {
				AnimateTransition();
			}
			if (wParam != ANIMATION_TIMER_EVENT_ID) {
				m_nLastSlideShowImageTickCount = ::GetTickCount();
			}
		}
	} else if (wParam == ZOOM_TIMER_EVENT_ID) {
		::KillTimer(this->m_hWnd, ZOOM_TIMER_EVENT_ID);
		if (m_bTemporaryLowQ || m_bInZooming) {
			::SetTimer(this->m_hWnd, ZOOM_TEXT_TIMER_EVENT_ID, ZOOM_TEXT_TIMEOUT, NULL);
			if (m_bInZooming) m_bShowZoomFactor = true;
			m_bTemporaryLowQ = false;
			m_bInZooming = false;
			if (m_bHQResampling && m_pCurrentImage != NULL) {
				this->Invalidate(FALSE);
			}
		}
	} else if (wParam == ZOOM_TEXT_TIMER_EVENT_ID) {
		m_bShowZoomFactor = false;
		::KillTimer(this->m_hWnd, ZOOM_TEXT_TIMER_EVENT_ID);
		m_pZoomNavigatorCtl->InvalidateZoomNavigatorRect();
		CRect imageProcArea = m_pImageProcPanelCtl->PanelRect();
		this->InvalidateRect(GetZoomTextRect(imageProcArea), FALSE);
	} else if (!m_pChrome->OnTimer((int)wParam)) {
		if (!m_pCropCtl->OnTimer((int)wParam)) {
			m_pPanelMgr->OnTimer((int)wParam);
		}
	}
	return 0;
}

LRESULT CMainDlg::OnContextMenu(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/) {
	if (m_pPanelMgr->IsModalPanelShown()) {
		return 1;
	}
	int nX = GET_X_LPARAM(lParam);
	int nY = GET_Y_LPARAM(lParam);

	MouseOn();

	if (m_pCropCtl->IsCropping()) {
		m_pCropCtl->ShowCropContextMenu();
		return 1;
	}

	HMENU hMenu = NULL;
	HMENU hMenuTrackPopup = LoadPreparedContextMenu(hMenu);
	if (hMenuTrackPopup == NULL) return 1;

	// 'Convert to' submenu after 'Save processed image...'
	if (m_pCurrentImage != NULL && !m_pCurrentImage->IsClipboardImage() && !m_bMovieMode) {
		for (int i = 0; i < ::GetMenuItemCount(hMenuTrackPopup); i++) {
			if (::GetMenuItemID(hMenuTrackPopup, i) == IDM_SAVE) {
				::InsertMenu(hMenuTrackPopup, i + 1, MF_BYPOSITION | MF_POPUP | MF_STRING,
					(UINT_PTR)Converter::CreatePresetMenu(IDM_FIRST_CONVERT_CMD), CNLS::GetString(_T("Convert to")));
				break;
			}
		}
	}

	int nMenuCmd = TrackPopupMenu(CPoint(nX, nY), hMenuTrackPopup);
	ExecuteCommand(nMenuCmd);

	::DestroyMenu(hMenu);
	return 1;
}

HMENU CMainDlg::LoadPreparedContextMenu(HMENU& hMenuRoot) {
	HMENU hMenu = ::LoadMenu(_Module.m_hInst, _T("PopupMenu"));
	hMenuRoot = hMenu;
	if (hMenu == NULL) return NULL;

	HMENU hMenuTrackPopup = ::GetSubMenu(hMenu, 0);
	HelpersGUI::TranslateMenuStrings(hMenuTrackPopup, m_pKeyMap);
	
	if (m_pEXIFDisplayCtl->IsActive()) ::CheckMenuItem(hMenuTrackPopup, IDM_SHOW_FILEINFO, MF_CHECKED);
	if (m_bShowFileName) ::CheckMenuItem(hMenuTrackPopup, IDM_SHOW_FILENAME, MF_CHECKED);
	if (m_pNavPanelCtl->IsActive()) ::CheckMenuItem(hMenuTrackPopup, IDM_SHOW_NAVPANEL, MF_CHECKED);
	if (m_bAutoContrast) ::CheckMenuItem(hMenuTrackPopup, IDM_AUTO_CORRECTION, MF_CHECKED);
	if (m_bLDC) ::CheckMenuItem(hMenuTrackPopup, IDM_LDC, MF_CHECKED);
	if (m_bKeepParams) ::CheckMenuItem(hMenuTrackPopup, IDM_KEEP_PARAMETERS, MF_CHECKED);
	HMENU hMenuNavigation = ::GetSubMenu(hMenuTrackPopup, SUBMENU_POS_NAVIGATION);
	::CheckMenuItem(hMenuNavigation,  m_pFileList->GetNavigationMode()*10 + IDM_LOOP_FOLDER, MF_CHECKED);
	HMENU hMenuOrdering = ::GetSubMenu(hMenuTrackPopup, SUBMENU_POS_DISPLAY_ORDER);
	::CheckMenuItem(hMenuOrdering,  
		(m_pFileList->GetSorting() == Helpers::FS_LastModTime) ? IDM_SORT_MOD_DATE :
		(m_pFileList->GetSorting() == Helpers::FS_CreationTime) ? IDM_SORT_CREATION_DATE :
		(m_pFileList->GetSorting() == Helpers::FS_FileName) ? IDM_SORT_NAME :
		(m_pFileList->GetSorting() == Helpers::FS_Random) ? IDM_SORT_RANDOM : IDM_SORT_SIZE
		, MF_CHECKED);
	::CheckMenuItem(hMenuOrdering, m_pFileList->IsSortedAscending() ? IDM_SORT_ASCENDING : IDM_SORT_DESCENDING, MF_CHECKED);
	if (m_pFileList->GetSorting() == Helpers::FS_Random) {
		::EnableMenuItem(hMenuOrdering, IDM_SORT_ASCENDING, MF_BYCOMMAND | MF_GRAYED);
		::EnableMenuItem(hMenuOrdering, IDM_SORT_DESCENDING, MF_BYCOMMAND | MF_GRAYED);
	}
	HMENU hMenuMovie = ::GetSubMenu(hMenuTrackPopup, SUBMENU_POS_MOVIE);
	if (!m_bMovieMode) ::EnableMenuItem(hMenuMovie, IDM_STOP_MOVIE, MF_BYCOMMAND | MF_GRAYED);
	HMENU hMenuZoom = ::GetSubMenu(hMenuTrackPopup, SUBMENU_POS_ZOOM);
	if (m_bSpanVirtualDesktop) ::CheckMenuItem(hMenuZoom,  IDM_SPAN_SCREENS, MF_CHECKED);
	if (m_bFullScreenMode) ::CheckMenuItem(hMenuZoom,  IDM_FULL_SCREEN_MODE, MF_CHECKED);
	if (m_bWindowBorderless) ::CheckMenuItem(hMenuZoom, IDM_HIDE_TITLE_BAR, MF_CHECKED);
	if (m_bAlwaysOnTop) ::CheckMenuItem(hMenuZoom, IDM_ALWAYS_ON_TOP, MF_CHECKED);
	if (IsAdjustWindowToImage() && IsImageExactlyFittingWindow()) ::CheckMenuItem(hMenuZoom, IDM_FIT_WINDOW_TO_IMAGE, MF_CHECKED);
	HMENU hMenuAutoZoomMode = ::GetSubMenu(hMenuTrackPopup, SUBMENU_POS_AUTOZOOMMODE);
	::CheckMenuItem(hMenuAutoZoomMode, GetAutoZoomMode() * 10 + IDM_AUTO_ZOOM_FIT_NO_ZOOM, MF_CHECKED);
	HMENU hMenuSettings = ::GetSubMenu(hMenuTrackPopup, SUBMENU_POS_SETTINGS);
	HMENU hMenuModDate = ::GetSubMenu(hMenuTrackPopup, SUBMENU_POS_MODDATE);
	HMENU hMenuUserCommands = ::GetSubMenu(hMenuTrackPopup, SUBMENU_POS_USER_COMMANDS);
	HMENU hMenuOpenWithCommands = ::GetSubMenu(hMenuTrackPopup, SUBMENU_POS_OPENWITH);
	HMENU hMenuWallpaper = ::GetSubMenu(hMenuTrackPopup, SUBMENU_POS_WALLPAPER);

	if (!HelpersGUI::CreateUserCommandsMenu(hMenuUserCommands)) {
		::DeleteMenu(hMenuTrackPopup, SUBMENU_POS_USER_COMMANDS + 1, MF_BYPOSITION);
		::DeleteMenu(hMenuTrackPopup, SUBMENU_POS_USER_COMMANDS, MF_BYPOSITION);
		::DeleteMenu(hMenuTrackPopup, SUBMENU_POS_USER_COMMANDS - 1, MF_BYPOSITION);
	}
	if (!m_bFullScreenMode) {
		// Transition effect and speed only available in full screen mode
		::DeleteMenu(hMenuMovie, 9, MF_BYPOSITION);
		::DeleteMenu(hMenuMovie, 9, MF_BYPOSITION);
	} else {
		::CheckMenuItem(hMenuMovie, m_eTransitionEffect + IDM_EFFECT_NONE, MF_CHECKED);
		int nIndex = (m_nTransitionTime < 180) ? 0 : (m_nTransitionTime < 375) ? 1 : (m_nTransitionTime < 750) ? 2 : (m_nTransitionTime < 1500) ? 3 : 4;
		::CheckMenuItem(hMenuMovie, nIndex + IDM_EFFECTTIME_VERY_FAST, MF_CHECKED);
	}

	if (CParameterDB::This().IsEmpty()) ::EnableMenuItem(hMenuSettings, IDM_BACKUP_PARAMDB, MF_BYCOMMAND | MF_GRAYED);
	if (CSettingsProvider::This().StoreToEXEPath()) ::EnableMenuItem(hMenuSettings, IDM_UPDATE_USER_CONFIG, MF_BYCOMMAND | MF_GRAYED);
	if (m_bFullScreenMode) ::EnableMenuItem(hMenuZoom, IDM_FIT_WINDOW_TO_IMAGE, MF_BYCOMMAND | MF_GRAYED);
	if (!m_bFullScreenMode) ::EnableMenuItem(hMenuZoom, IDM_SPAN_SCREENS, MF_BYCOMMAND | MF_GRAYED);
	if (m_bFullScreenMode) ::EnableMenuItem(hMenuZoom, IDM_HIDE_TITLE_BAR, MF_BYCOMMAND | MF_GRAYED);

	::EnableMenuItem(hMenuMovie, IDM_SLIDESHOW_START, MF_BYCOMMAND | MF_GRAYED);
	::EnableMenuItem(hMenuMovie, IDM_MOVIE_START_FPS, MF_BYCOMMAND | MF_GRAYED);

	if (!CSettingsProvider::This().AllowEditGlobalSettings()) {
		::DeleteMenu(hMenuSettings, 0, MF_BYPOSITION);
	}

	bool bCanPaste = ::IsClipboardFormatAvailable(CF_DIB);
	if (!bCanPaste) ::EnableMenuItem(hMenuTrackPopup, IDM_PASTE, MF_BYCOMMAND | MF_GRAYED);

	bool bCanDoLosslessJPEGTransform = (m_pCurrentImage != NULL) && m_pCurrentImage->GetImageFormat() == IF_JPEG && !m_pCurrentImage->IsDestructivelyProcessed();

	if (!bCanDoLosslessJPEGTransform) ::EnableMenuItem(hMenuTrackPopup, SUBMENU_POS_TRANSFORM_LOSSLESS, MF_BYPOSITION | MF_GRAYED);

	if (m_pCurrentImage == NULL) {
		::EnableMenuItem(hMenuTrackPopup, IDM_SAVE, MF_BYCOMMAND | MF_GRAYED);
		::EnableMenuItem(hMenuTrackPopup, IDM_RELOAD, MF_BYCOMMAND | MF_GRAYED);
		//::EnableMenuItem(hMenuTrackPopup, IDM_EXPLORE, MF_BYCOMMAND | MF_GRAYED);  // can still show path to an image which could not be loaded.  If file doesn't exist, nothing happens anyways
		::EnableMenuItem(hMenuTrackPopup, IDM_PRINT, MF_BYCOMMAND | MF_GRAYED);
		::EnableMenuItem(hMenuTrackPopup, IDM_COPY, MF_BYCOMMAND | MF_GRAYED);
		::EnableMenuItem(hMenuTrackPopup, IDM_COPY_FULL, MF_BYCOMMAND | MF_GRAYED);
		::EnableMenuItem(hMenuTrackPopup, IDM_COPY_PATH, MF_BYCOMMAND | MF_GRAYED);
		::EnableMenuItem(hMenuTrackPopup, IDM_SAVE_PARAM_DB, MF_BYCOMMAND | MF_GRAYED);
		::EnableMenuItem(hMenuTrackPopup, IDM_CLEAR_PARAM_DB, MF_BYCOMMAND | MF_GRAYED);
		::EnableMenuItem(hMenuTrackPopup, SUBMENU_POS_ZOOM, MF_BYPOSITION  | MF_GRAYED);
		::EnableMenuItem(hMenuTrackPopup, SUBMENU_POS_MODDATE, MF_BYPOSITION  | MF_GRAYED);
		::EnableMenuItem(hMenuTrackPopup, SUBMENU_POS_TRANSFORM, MF_BYPOSITION  | MF_GRAYED);
		::EnableMenuItem(hMenuTrackPopup, SUBMENU_POS_WALLPAPER, MF_BYPOSITION | MF_GRAYED);
	} else {
		if (m_bKeepParams || m_pCurrentImage->IsClipboardImage() ||
			CParameterDB::This().FindEntry(m_pCurrentImage->GetPixelHash()) == NULL)
			::EnableMenuItem(hMenuTrackPopup, IDM_CLEAR_PARAM_DB, MF_BYCOMMAND | MF_GRAYED);
		if (m_bKeepParams || m_pCurrentImage->IsClipboardImage())
			::EnableMenuItem(hMenuTrackPopup, IDM_SAVE_PARAM_DB, MF_BYCOMMAND | MF_GRAYED);
		if (m_pCurrentImage->IsClipboardImage()) {
			::EnableMenuItem(hMenuTrackPopup, IDM_EXPLORE, MF_BYCOMMAND | MF_GRAYED);  // cannot explore clipboard image
			::EnableMenuItem(hMenuTrackPopup, IDM_COPY_PATH, MF_BYCOMMAND | MF_GRAYED);
			::EnableMenuItem(hMenuModDate, IDM_TOUCH_IMAGE, MF_BYCOMMAND | MF_GRAYED);
			::EnableMenuItem(hMenuModDate, IDM_TOUCH_IMAGE_EXIF, MF_BYCOMMAND | MF_GRAYED);
		}
		if (m_pCurrentImage->GetEXIFReader() == NULL || !m_pCurrentImage->GetEXIFReader()->GetAcquisitionTimePresent()) {
			::EnableMenuItem(hMenuModDate, IDM_TOUCH_IMAGE_EXIF, MF_BYCOMMAND | MF_GRAYED);
		}
		int windowsVersion = Helpers::GetWindowsVersion();
		if (m_pCurrentImage->IsClipboardImage() || (windowsVersion < 600 && m_pCurrentImage->GetImageFormat() != IF_WindowsBMP) || 
			(windowsVersion < 602 && !(m_pCurrentImage->GetImageFormat() == IF_WindowsBMP || m_pCurrentImage->GetImageFormat() == IF_JPEG)) ||
			!m_pCurrentImage->IsGDIPlusFormat()) {
			::EnableMenuItem(hMenuWallpaper, IDM_SET_WALLPAPER_ORIG, MF_BYCOMMAND | MF_GRAYED);
		}
	}
	if (!HelpersGUI::CreateOpenWithCommandsMenu(hMenuOpenWithCommands) || m_pCurrentImage == NULL) {
		::DeleteMenu(hMenuTrackPopup, SUBMENU_POS_OPENWITH, MF_BYPOSITION);
	}
	if (m_bMovieMode) {
		::EnableMenuItem(hMenuTrackPopup, IDM_SAVE, MF_BYCOMMAND | MF_GRAYED);
		::EnableMenuItem(hMenuTrackPopup, IDM_RELOAD, MF_BYCOMMAND | MF_GRAYED);
		::EnableMenuItem(hMenuTrackPopup, IDM_PRINT, MF_BYCOMMAND | MF_GRAYED);
		::EnableMenuItem(hMenuTrackPopup, IDM_BATCH_COPY, MF_BYCOMMAND | MF_GRAYED);
		::EnableMenuItem(hMenuTrackPopup, IDM_SAVE_PARAMETERS, MF_BYCOMMAND | MF_GRAYED);
		::EnableMenuItem(hMenuTrackPopup, IDM_SAVE_PARAM_DB, MF_BYCOMMAND | MF_GRAYED);
		::EnableMenuItem(hMenuTrackPopup, IDM_CLEAR_PARAM_DB, MF_BYCOMMAND | MF_GRAYED);
	} else {
		// Delete the 'Stop movie' menu entry if no movie is playing
		::DeleteMenu(hMenuTrackPopup, 0, MF_BYPOSITION);
		::DeleteMenu(hMenuTrackPopup, 0, MF_BYPOSITION);
	}

	// Toolbar, status bar and side panel after 'Show navigation panel', so the toolbar can be switched on again from here.
	// Inserted last as the code above uses fixed menu positions.
	for (int i = 0; i < ::GetMenuItemCount(hMenuTrackPopup); i++) {
		if (::GetMenuItemID(hMenuTrackPopup, i) == IDM_SHOW_NAVPANEL) {
			::InsertMenu(hMenuTrackPopup, i + 1, MF_BYPOSITION | MF_STRING | (m_pChrome->GetShowSidePanel() ? MF_CHECKED : 0),
				IDM_TOGGLE_SIDEPANEL, CNLS::GetString(_T("Information panel")));
			::InsertMenu(hMenuTrackPopup, i + 1, MF_BYPOSITION | MF_STRING | (m_pChrome->GetShowStatusBar() ? MF_CHECKED : 0),
				IDM_TOGGLE_STATUSBAR, CNLS::GetString(_T("Status bar")));
			::InsertMenu(hMenuTrackPopup, i + 1, MF_BYPOSITION | MF_STRING | (m_pChrome->GetShowToolbar() ? MF_CHECKED : 0),
				IDM_TOGGLE_TOOLBAR, CNLS::GetString(_T("Toolbar")));
			break;
		}
	}

	return hMenuTrackPopup;
}

// Make the text edit control for renaming image colored black/white
LRESULT CMainDlg::OnCtlColorEdit(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& /*bHandled*/) {
	HDC hDC = (HDC) wParam;
	::SetTextColor(hDC, RGB(255, 255, 255));
	::SetBkColor(hDC, RGB(0, 0, 0));
	return (LRESULT)::GetStockObject(BLACK_BRUSH);
}

LRESULT CMainDlg::OnEraseBackground(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled) {
	// prevent erasing background
	bHandled = TRUE;
	return TRUE;
}

LRESULT CMainDlg::OnOK(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	// NOP
	return 0;
}

LRESULT CMainDlg::OnCancel(WORD /*wNotifyCode*/, WORD wID, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	CleanupAndTerminate();
	return 0;
}

///////////////////////////////////////////////////////////////////////////////////
// Static helpers for being called from UI controls
///////////////////////////////////////////////////////////////////////////////////

void CMainDlg::OnExecuteCommand(void* pContext, int nParameter, CButtonCtrl & sender) {
	((CMainDlg*)pContext)->ExecuteCommand(nParameter);
}

bool CMainDlg::IsCurrentImageFitToScreen(void* pContext) {
	CMainDlg* pThis = (CMainDlg*)pContext;
	return fabs(pThis->m_dZoom - pThis->GetZoomFactorForFitToScreen(false, true)) <= 0.01;
}

///////////////////////////////////////////////////////////////////////////////////
// Private
///////////////////////////////////////////////////////////////////////////////////

void CMainDlg::InvalidateHelpDlg() {
	if (m_pHelpDlg != NULL && !m_pHelpDlg->IsDestroyed()) {
		m_pHelpDlg->Invalidate();
	}
}

bool CMainDlg::CloseHelpDlg() {
	if (m_pHelpDlg != NULL && !m_pHelpDlg->IsDestroyed()) {
		m_pHelpDlg->DestroyDialog();
		delete m_pHelpDlg;
		m_pHelpDlg = NULL;
		return true;
	}
	return false;
}

void CMainDlg::ExecuteCommand(int nCommand) {
	if (nCommand >= IDM_CROPMODE_FREE && nCommand <= IDM_CROPMODE_USER) {
		m_nCropModeCommand = nCommand;
	}
	CSettingsProvider& sp = CSettingsProvider::This();
	InvalidateHelpDlg();
	switch (nCommand) {
		case IDM_HELP:
			if (m_pHelpDlg == NULL || m_pHelpDlg->IsDestroyed()) {
				delete m_pHelpDlg;
				m_pHelpDlg = new CHelpDlg(this);
				m_pHelpDlg->Create(m_hWnd);
				m_pHelpDlg->ShowWindow(SW_SHOWNORMAL);
			}
			else CloseHelpDlg();
			MouseOn();
			break;
		case IDM_TOGGLE:
			if (m_pFileList->FileMarkedForToggle()) {
				GotoImage(POS_Toggle);
			}
			break;
		case IDM_MARK_FOR_TOGGLE:
			if (m_pFileList->Current() != NULL && m_pCurrentImage != NULL && !m_pCurrentImage->IsClipboardImage()) {
				m_pFileList->MarkCurrentFile();
			}
			break;
		case IDM_MINIMIZE:
			this->ShowWindow(SW_MINIMIZE);
			break;
		case IDM_OPEN:
			OpenFileWithDialog(false, false);
			break;
		case IDM_EXPLORE:
			if (m_pCurrentImage != NULL && m_pCurrentImage->IsClipboardImage()) {
				// don't try to "Explore" path if clipboard image
				break;
			}
			// otherwise, allowed even for invalid file loads
			ExploreFile();
			break;
		case IDM_SAVE:
		case IDM_SAVE_SCREEN:
		case IDM_SAVE_ALLOW_NO_PROMPT:
			if (m_pCurrentImage != NULL) {
				if (nCommand == IDM_SAVE_ALLOW_NO_PROMPT && sp.SaveWithoutPrompt() && !m_pCurrentImage->IsClipboardImage()) {
					SaveImageNoPrompt(CurrentFileName(false), true);
				} else {
					SaveImage(nCommand != IDM_SAVE_SCREEN);
				}
			}
			break;
		case IDM_RELOAD:
			ReloadImage(false);
			break;
		case IDM_PRINT:
			if (m_pCurrentImage != NULL) {
				StopAnimation(); // stop any running animation
				if (m_pPrintImage->Print(this->m_hWnd, m_pCurrentImage, *m_pImageProcParams, CreateDefaultProcessingFlags(), m_pFileList->Current())) {
					this->Invalidate(FALSE);
				}
			}
			break;
		case IDM_COPY:
			if (m_pCurrentImage != NULL) {
				CClipboard::CopyImageToClipboard(this->m_hWnd, m_pCurrentImage, m_pFileList->Current());
			}
			break;
		case IDM_COPY_FULL:
			if (m_pCurrentImage != NULL) {
				CClipboard::CopyFullImageToClipboard(this->m_hWnd, m_pCurrentImage, *m_pImageProcParams, CreateDefaultProcessingFlags(), m_pFileList->Current());
				this->Invalidate(FALSE);
			}
			break;
		case IDM_COPY_PATH:
			if (m_pCurrentImage != NULL && !m_pCurrentImage->IsClipboardImage()) {
				CClipboard::CopyPathToClipboard(this->m_hWnd, m_pCurrentImage, m_pFileList->Current());
			}
			break;
		case IDM_PASTE:
			if (::IsClipboardFormatAvailable(CF_DIB)) {
				GotoImage(POS_Clipboard);
			}
			break;
		case IDM_BATCH_COPY:
			if (m_pCurrentImage != NULL) {
				BatchCopy();
			}
			break;
		case IDM_RENAME:
			m_pImageProcPanelCtl->EnterRenameCurrentFile();
			break;
		case IDM_MOVE_TO_RECYCLE_BIN:
		case IDM_MOVE_TO_RECYCLE_BIN_CONFIRM:
		case IDM_MOVE_TO_RECYCLE_BIN_CONFIRM_PERMANENT_DELETE:
			MouseOn();
			if (m_pCurrentImage != NULL && m_pFileList != NULL && !m_pCurrentImage->IsClipboardImage() && sp.AllowFileDeletion()) {
				LPCTSTR currentFileName = CurrentFileName(false);
				bool noConfirmation = nCommand == IDM_MOVE_TO_RECYCLE_BIN ||
					(nCommand == IDM_MOVE_TO_RECYCLE_BIN_CONFIRM_PERMANENT_DELETE && CFileList::DriveHasRecycleBin(currentFileName));
				// with a recycle bin the file can be restored: Enter confirms (default button), Esc cancels
				bool bHasRecycleBin = CFileList::DriveHasRecycleBin(currentFileName);
				LPCTSTR sQuestion = bHasRecycleBin ? _T("Move the current image to the recycle bin?") : _T("Do you really want to delete the current image file on disk?");
				if (noConfirmation || ConfirmDelete(m_hWnd, CString(CNLS::GetString(sQuestion)) + _T("\n\n") + currentFileName + _T("\n\n") +
						CNLS::GetString(_T("Enter or Del: delete, Esc: cancel")), bHasRecycleBin)) {
					CFileList* fileListOfDeletedImage = m_pFileList;
					GotoImage(POS_AwayFromCurrent, NO_REQUEST);
					if (m_pFileList->DeleteFile(currentFileName)) {
						fileListOfDeletedImage->Reload(NULL, false);
						m_pFileList->DeleteHistory(true);
						Invalidate();
						GotoImage(POS_Current);
					} else {
						Invalidate();
						if (m_pFileList->Current() == NULL) GotoImage(POS_First);
						else GotoImage(POS_Previous);
					}
				}
			}
			break;
		case IDM_SHOW_FILEINFO:
			if (m_pChrome->IsEnabled()) {
				// in window mode the information is on the side panel, the overlay over the image is only used in full screen mode
				if (m_pChrome->IsSidePanelExpanded() && !m_pChrome->IsEditTabShown()) {
					m_pChrome->SetShowSidePanel(false);
				} else {
					m_pChrome->ShowSideTab(CWindowChrome::TAB_INFO);
				}
				Invalidate(FALSE);
				break;
			}
			m_pEXIFDisplayCtl->SetActive(!m_pEXIFDisplayCtl->IsActive());
			m_pNavPanelCtl->GetNavPanel()->GetBtnShowInfo()->SetActive(m_pEXIFDisplayCtl->IsActive());
			break;
		case IDM_SHOW_FILENAME:
			m_bShowFileName = !m_bShowFileName;
			this->Invalidate(FALSE);
			break;
		case IDM_SHOW_NAVPANEL:
			m_pNavPanelCtl->SetActive(!m_pNavPanelCtl->IsActive());
			break;
		case IDM_NEXT:
			GotoImage(POS_Next);
			break;
		case IDM_PREV:
			GotoImage(POS_Previous);
			break;
		case IDM_FIRST:
			GotoImage(POS_First);
			break;
		case IDM_LAST:
			GotoImage(POS_Last);
			break;
		case IDM_LOOP_FOLDER:
		case IDM_LOOP_RECURSIVELY:
		case IDM_LOOP_SIBLINGS:
			m_pFileList->SetNavigationMode(
				(nCommand == IDM_LOOP_FOLDER) ? Helpers::NM_LoopDirectory :
				(nCommand == IDM_LOOP_RECURSIVELY) ? Helpers::NM_LoopSubDirectories : 
				Helpers::NM_LoopSameDirectoryLevel);
			break;
		case IDM_SORT_MOD_DATE:
		case IDM_SORT_CREATION_DATE:
		case IDM_SORT_NAME:
		case IDM_SORT_RANDOM:
		case IDM_SORT_SIZE:
			m_pFileList->SetSorting(
				(nCommand == IDM_SORT_CREATION_DATE) ? Helpers::FS_CreationTime : 
				(nCommand == IDM_SORT_MOD_DATE) ? Helpers::FS_LastModTime : 
				(nCommand == IDM_SORT_RANDOM) ? Helpers::FS_Random : 
				(nCommand == IDM_SORT_SIZE) ? Helpers::FS_FileSize : Helpers::FS_FileName, m_pFileList->IsSortedAscending());
			if (m_pEXIFDisplayCtl->IsActive() || m_bShowFileName) {
				this->Invalidate(FALSE);
			}
			break;
		case IDM_SORT_ASCENDING:
		case IDM_SORT_DESCENDING:
			m_pFileList->SetSorting(m_pFileList->GetSorting(), nCommand == IDM_SORT_ASCENDING);
			if (m_pEXIFDisplayCtl->IsActive() || m_bShowFileName) {
				this->Invalidate(FALSE);
			}
			break;
		case IDM_STOP_MOVIE:
			StopMovieMode();
			break;
		case IDM_SLIDESHOW_RESUME:
			StartMovieMode(m_dMovieFPS);
			break;
		case IDM_SLIDESHOW_1:
		case IDM_SLIDESHOW_2:
		case IDM_SLIDESHOW_3:
		case IDM_SLIDESHOW_4:
		case IDM_SLIDESHOW_5:
		case IDM_SLIDESHOW_7:
		case IDM_SLIDESHOW_10:
		case IDM_SLIDESHOW_20:
			StartMovieMode(1.0/(nCommand - IDM_SLIDESHOW_START));
			break;
		case IDM_EFFECT_NONE:
		case IDM_EFFECT_BLEND:
		case IDM_EFFECT_SLIDE_RL:
		case IDM_EFFECT_SLIDE_LR:
		case IDM_EFFECT_SLIDE_TB:
		case IDM_EFFECT_SLIDE_BT:
		case IDM_EFFECT_ROLL_RL:
		case IDM_EFFECT_ROLL_LR:
		case IDM_EFFECT_ROLL_TB:
		case IDM_EFFECT_ROLL_BT:
		case IDM_EFFECT_SCROLL_RL:
		case IDM_EFFECT_SCROLL_LR:
		case IDM_EFFECT_SCROLL_TB:
		case IDM_EFFECT_SCROLL_BT:
			m_eTransitionEffect = (Helpers::ETransitionEffect)(nCommand - IDM_EFFECT_NONE);
			break;
		case IDM_EFFECTTIME_VERY_FAST:
		case IDM_EFFECTTIME_FAST:
		case IDM_EFFECTTIME_NORMAL:
		case IDM_EFFECTTIME_SLOW:
		case IDM_EFFECTTIME_VERY_SLOW:
			m_nTransitionTime = 125 * (1 << (nCommand - IDM_EFFECTTIME_VERY_FAST));
			break;
		case IDM_MOVIE_5_FPS:
		case IDM_MOVIE_10_FPS:
		case IDM_MOVIE_25_FPS:
		case IDM_MOVIE_30_FPS:
		case IDM_MOVIE_50_FPS:
		case IDM_MOVIE_100_FPS:
			StartMovieMode(nCommand - IDM_MOVIE_START_FPS);
			break;
		case IDM_SAVE_PARAM_DB:
			if (m_pCurrentImage != NULL && !m_bMovieMode && !m_bKeepParams) {
				CParameterDBEntry newEntry;
				EProcessingFlags procFlags = CreateProcessingFlags(m_bHQResampling, m_bAutoContrast, false, m_bLDC, false, m_bLandscapeMode);
				newEntry.InitFromProcessParams(*m_pImageProcParams, procFlags, m_pCurrentImage->IsAnimation() ? CRotationParams(m_nRotation) : CRotationParams(m_pCurrentImage->GetRotationParams(), m_nRotation));
				if ((m_bUserZoom || m_bUserPan) && !m_pCurrentImage->IsCropped()) {
					newEntry.InitGeometricParams(m_pCurrentImage->OrigSize(), m_dZoom, m_offsets, 
						m_bAutoFitWndToImage ? CMultiMonitorSupport::GetMonitorRect(m_hWnd).Size() : m_clientRect.Size(), m_bAutoFitWndToImage);
				}
				newEntry.SetHash(m_pCurrentImage->GetPixelHash());
				if (CParameterDB::This().AddEntry(newEntry)) {
					// these parameters need to be updated when image is reused from cache
					m_pCurrentImage->SetInitialParameters(*m_pImageProcParams, procFlags, m_nRotation, m_dZoom, m_offsets);
					m_pCurrentImage->SetIsInParamDB(true);
					m_pImageProcPanelCtl->ShowHideSaveDBButtons();
				}
			}
			break;
		case IDM_CLEAR_PARAM_DB:
			if (m_pCurrentImage != NULL && !m_bMovieMode && !m_bKeepParams) {
				if (CParameterDB::This().DeleteEntry(m_pCurrentImage->GetPixelHash())) {
					// restore initial parameters and realize the parameters
					EProcessingFlags procFlags = GetDefaultProcessingFlags(m_bLandscapeMode);
					m_pCurrentImage->RestoreInitialParameters(m_pFileList->Current(), 
						GetDefaultProcessingParams(), procFlags, 0, -1, CPoint(0, 0), CSize(0, 0), CSize(0, 0));
					*m_pImageProcParams = GetDefaultProcessingParams();
					// Sunset and night shot detection may has changed this
					m_pImageProcParams->LightenShadows = m_pCurrentImage->GetInitialProcessParams().LightenShadows;
					InitFromProcessingFlags(procFlags, m_bHQResampling, m_bAutoContrast, m_bAutoContrastSection, m_bLDC, m_bLandscapeMode);
					m_nRotation = m_pCurrentImage->GetInitialRotation();
					m_nUserRotation = 0;
					m_dZoom = -1;
					m_pCurrentImage->SetIsInParamDB(false);
					m_pImageProcPanelCtl->ShowHideSaveDBButtons();
					if (fabs(m_pCurrentImage->GetRotationParams().FreeRotation) > 0.009) {
						ReloadImage(false); // free rotation cannot be restored, needs reload
					}
					this->Invalidate(FALSE);
				}
			}
			break;
		case IDM_ROTATE_90:
		case IDM_ROTATE_270:
			if (m_pCurrentImage != NULL) {
				uint32 nRotationDelta = (nCommand == IDM_ROTATE_90) ? 90 : 270;
				m_nRotation = (m_nRotation + nRotationDelta) % 360;
				m_nUserRotation = (m_nUserRotation + nRotationDelta) % 360;
				m_pCurrentImage->Rotate(nRotationDelta);
				if (!IsAdjustWindowToImage()) m_dZoom = -1;
				m_bUserZoom = false;
				m_bUserPan = false;
				AdjustWindowToImage(false);
				this->Invalidate(FALSE);
			}
			break;
		case IDM_ROTATE:
			m_pCropCtl->AbortCropping();
			GetRotationPanelCtl()->SetVisible(true);
			break;
		case IDM_CHANGESIZE:
			if (m_pCurrentImage != NULL) {
				MouseOn();
				CResizeDlg dlgResize(m_pCurrentImage->OrigSize());
				if (dlgResize.DoModal(m_hWnd) == IDOK) {
					HCURSOR hOldCursor = ::SetCursor(::LoadCursor(NULL, IDC_WAIT));
					m_pCurrentImage->ResizeOriginalPixels(dlgResize.GetFilter(), dlgResize.GetNewSize());
					::SetCursor(hOldCursor);
					m_pImageProcPanelCtl->ShowHideSaveDBButtons();
					this->Invalidate(FALSE);
				}
			}
			break;
		case IDM_PERSPECTIVE:
			m_pCropCtl->AbortCropping();
			GetTiltCorrectionPanelCtl()->SetVisible(true);
			break;
		case IDM_MIRROR_H:
		case IDM_MIRROR_V:
			if (m_pCurrentImage != NULL) {
				m_pCurrentImage->Mirror(nCommand == IDM_MIRROR_H);
				m_pImageProcPanelCtl->ShowHideSaveDBButtons();
				this->Invalidate(FALSE);
			}
			break;
		case IDM_ROTATE_90_LOSSLESS:
		case IDM_ROTATE_90_LOSSLESS_CONFIRM:
		case IDM_ROTATE_270_LOSSLESS:
		case IDM_ROTATE_270_LOSSLESS_CONFIRM:
		case IDM_ROTATE_180_LOSSLESS:
		case IDM_MIRROR_H_LOSSLESS:
		case IDM_MIRROR_V_LOSSLESS:
			if (m_pCurrentImage != NULL && m_pCurrentImage->GetImageFormat() == IF_JPEG) {
				bool bCanTransformWithoutCrop = m_pCurrentImage->CanUseLosslessJPEGTransformations();
				bool bAskIfToCrop = !(bCanTransformWithoutCrop ||sp.CropWithoutPromptLosslessJPEG());
				bool bPerformTransformation = true;
				bool bCrop = false;
				MouseOn();
				if (bAskIfToCrop) {
					bCrop = IDYES == ::MessageBox(m_hWnd, CString(CNLS::GetString(_T("Image width and height must be dividable by the JPEG block size (8 or 16) for lossless transformations!"))) + _T("\n") +
						CNLS::GetString(_T("The transformation can be applied if the image is cropped to the next matching size but this will remove some border pixels.")) + _T("\n") +
						CNLS::GetString(_T("Crop the image and apply transformation? Cropping cannot be undone!")) + _T("\n\n") +
						CNLS::GetString(_T("Note: Set the key 'CropWithoutPromptLosslessJPEG=true' in the INI file to always crop without showing this message.")), 
						CNLS::GetString(_T("Lossless JPEG transformations")), MB_YESNO | MB_ICONEXCLAMATION | MB_DEFBUTTON2);
				}
				if (!bAskIfToCrop || bCrop) {
					if (!bAskIfToCrop && (nCommand == IDM_ROTATE_90_LOSSLESS_CONFIRM || nCommand == IDM_ROTATE_270_LOSSLESS_CONFIRM)) {
						LPCTSTR sConfirmMsg = (nCommand == IDM_ROTATE_90_LOSSLESS_CONFIRM) ?
							CNLS::GetString(_T("Rotate current file on disk lossless by 90 deg (W/H must be multiple of 16)")) :
							CNLS::GetString(_T("Rotate current file on disk lossless by 270 deg (W/H must be multiple of 16)"));
						bPerformTransformation = IDYES == ::MessageBox(m_hWnd, 
							sConfirmMsg, CNLS::GetString(_T("Confirm")), MB_YESNOCANCEL | MB_ICONWARNING);
					}
					if (bPerformTransformation) {
						CJPEGLosslessTransform::EResult eResult =
							CJPEGLosslessTransform::PerformTransformation(m_pFileList->Current(), m_pFileList->Current(), HelpersGUI::CommandIdToLosslessTransformation(nCommand), bCrop || sp.CropWithoutPromptLosslessJPEG());
						if (eResult != CJPEGLosslessTransform::Success) {
							::MessageBox(m_hWnd, CString(CNLS::GetString(_T("Performing the lossless transformation failed!"))) +
								_T("\n") + CNLS::GetString(_T("Reason:")) + _T(" ") + HelpersGUI::LosslessTransformationResultToString(eResult),
								CNLS::GetString(_T("Lossless JPEG transformations")), MB_OK | MB_ICONWARNING);
						} else {
							ReloadImage(false); // reload current image
						}
					}
				}
			}
			break;
		case IDM_AUTO_CORRECTION:
			m_bAutoContrastSection = false;
			m_bAutoContrast = !m_bAutoContrast;
			this->Invalidate(FALSE);
			break;
		case IDM_AUTO_CORRECTION_SECTION:
			m_bAutoContrast = true;
			m_bAutoContrastSection = !m_bAutoContrastSection;
			this->Invalidate(FALSE);
			break;
		case IDM_LDC:
			m_bLDC = !m_bLDC;
			this->Invalidate(FALSE);
			break;
		case IDM_LANDSCAPE_MODE:
			m_bLandscapeMode = !m_bLandscapeMode;
			m_pNavPanelCtl->GetNavPanel()->GetBtnLandscapeMode()->SetActive(m_bLandscapeMode);
			if (m_bLandscapeMode) {
				*m_pImageProcParams = _SetLandscapeModeParams(true, *m_pImageProcParams);
				if (m_pCurrentImage != NULL) {
					m_pImageProcParams->LightenShadows *= m_pCurrentImage->GetLightenShadowFactor();
				}
				m_bLDC = true;
				m_bAutoContrast = true;
			} else {
				EProcessingFlags eProcFlags = GetDefaultProcessingFlags(false);
				CImageProcessingParams ipa = GetDefaultProcessingParams();
				if (m_pCurrentImage != NULL) {
					m_pCurrentImage->GetFileParams(m_pFileList->Current(), eProcFlags, ipa);
				}
				m_bLDC = GetProcessingFlag(eProcFlags, PFLAG_LDC);
				m_bAutoContrast = GetProcessingFlag(eProcFlags, PFLAG_AutoContrast);
				*m_pImageProcParams = ipa;
			}
			this->Invalidate(FALSE);
			break;
		case IDM_KEEP_PARAMETERS:
			m_bKeepParams = !m_bKeepParams;
			m_pNavPanelCtl->GetNavPanel()->GetBtnKeepParams()->SetActive(m_bKeepParams);
			if (m_bKeepParams) {
				*m_pImageProcParamsKept = *m_pImageProcParams;
				m_eProcessingFlagsKept = CreateProcessingFlags(m_bHQResampling, m_bAutoContrast, false, m_bLDC, m_bKeepParams, m_bLandscapeMode);
				m_dZoomKept = (m_bUserZoom || IsAdjustWindowToImage()) ? m_dZoom : -1;
				m_offsetKept = m_bUserPan ? m_offsets : CPoint(0, 0);
			}
			m_pImageProcPanelCtl->ShowHideSaveDBButtons();
			break;
		case IDM_SAVE_PARAMETERS:
			SaveParameters();
			break;
		case IDM_FIT_TO_SCREEN:
		case IDM_FIT_TO_SCREEN_NO_ENLARGE:
			ResetZoomToFitScreen(false, nCommand == IDM_FIT_TO_SCREEN, true);
			break;
		case IDM_FILL_WITH_CROP:
			ResetZoomToFitScreen(true, true, true);
			break;
		case IDM_TOGGLE_FIT_TO_SCREEN_100_PERCENTS:
		case IDM_TOGGLE_FILL_WITH_CROP_100_PERCENTS:
			if (fabs(m_dZoom - 1) < 0.01) {
				ResetZoomToFitScreen(nCommand == IDM_TOGGLE_FILL_WITH_CROP_100_PERCENTS, true, true);
			} else {
				ResetZoomTo100Percents(m_bMouseOn);
			}
			break;
		case IDM_SPAN_SCREENS:
			if (m_bFullScreenMode && CMultiMonitorSupport::IsMultiMonitorSystem()) {
				m_dZoom = -1.0;
				this->Invalidate(FALSE);
				if (m_bSpanVirtualDesktop) {
					this->SetWindowPlacement(&m_storedWindowPlacement);
				} else {
					this->GetWindowPlacement(&m_storedWindowPlacement);
					CRect rectAllScreens = CMultiMonitorSupport::GetVirtualDesktop();
					this->SetWindowPos(HWND_TOP, &rectAllScreens, SWP_NOZORDER);
				}
				m_bSpanVirtualDesktop = !m_bSpanVirtualDesktop;
				this->GetClientRect(&m_clientRect);
			}
			break;
		case IDM_FULL_SCREEN_MODE:
			m_bFullScreenMode = !m_bFullScreenMode;
			m_pChrome->SyncBorderSize();
			m_dZoomAtResizeStart = 1.0;
			if (!m_bFullScreenMode) {
				CRect windowRect;

				// restore hidden title bar if enabled
				SetCurrentWindowStyle();

				HICON hIconSmall = (HICON)::LoadImage(_Module.GetResourceInstance(), MAKEINTRESOURCE(IDR_MAINFRAME), 
					IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
				SetIcon(hIconSmall, FALSE);
				CRect defaultWindowRect = CMultiMonitorSupport::GetDefaultWindowRect();
				double dZoom = -1;
				windowRect = sp.ExplicitWindowRect() ?
					defaultWindowRect :
					Helpers::GetWindowRectMatchingImageSize(
						m_hWnd,
						CSize(MIN_WND_WIDTH, MIN_WND_HEIGHT),
						defaultWindowRect.Size(),
						dZoom, m_pCurrentImage, false, true, m_bWindowBorderless);
				this->SetWindowPos(HWND_TOP, windowRect.left, windowRect.top, windowRect.Width(), windowRect.Height(), SWP_NOZORDER | SWP_NOCOPYBITS);
				this->MouseOn();
				m_bSpanVirtualDesktop = false;
			} else {
				if (!IsZoomed() && sp.ExplicitWindowRect()) {
					// Save the old window rect to be able to restore it
					CRect rect;
					GetWindowRect(&rect);
					CMultiMonitorSupport::SetDefaultWindowRect(rect);
				}
				HMONITOR hMonitor = ::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
				MONITORINFO monitorInfo;
				monitorInfo.cbSize = sizeof(MONITORINFO);
				if (::GetMonitorInfo(hMonitor, &monitorInfo)) {
					CRect monitorRect(&(monitorInfo.rcMonitor));
					this->SetWindowLongW(GWL_STYLE, WS_VISIBLE);
					this->SetWindowPos(HWND_TOP, monitorRect.left, monitorRect.top, monitorRect.Width(), monitorRect.Height(), SWP_NOZORDER | SWP_NOCOPYBITS);
				}
				this->MouseOn();
			}
			m_dZoom = -1;
			StartLowQTimer(ZOOM_TIMEOUT);
			this->SetWindowPos(NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOCOPYBITS | SWP_FRAMECHANGED);
			break;
		case IDM_HIDE_TITLE_BAR:
			if (!m_bFullScreenMode) {
				// only available when full screen mode is not active

				m_bWindowBorderless = !m_bWindowBorderless;
				m_pChrome->SyncBorderSize();
				SetCurrentWindowStyle();

				// get the size of the border to shift the window pos downwards
				int windowCaptionHeight = Helpers::GetWindowCaptionSize();
				double dZoom = -1;
				CRect windowRect = Helpers::GetWindowRectMatchingImageSize(
					m_hWnd, CSize(MIN_WND_WIDTH, MIN_WND_HEIGHT), HUGE_SIZE, dZoom, m_pCurrentImage, false, true, m_bWindowBorderless);

				// don't try to adjust for an image that isn't loaded!
				if (m_pCurrentImage != NULL)
				{
					// this is the new top to move it to so that the experience seems seamless
					int newTop;
					int t = m_pCurrentImage->OrigHeight();

					// these are experimental values figured out through trial and error
					// it appears if the caption size is odd, and just using /2,
					// it causes the window to shift up one pixel at a time when going between borderless and not borderless repeatedly
					// in other cases, it shifts downwards depending on rounding errors resizing the window and image... hard to hunt down but it's as good as it can get right now
					if (windowCaptionHeight % 2 == 0) {
						newTop = m_bWindowBorderless ?
							windowRect.top + (windowCaptionHeight / 2) :
							windowRect.top - (windowCaptionHeight / 2);
					} else {
						newTop = m_bWindowBorderless ?
							windowRect.top + (windowCaptionHeight / 2) :
							windowRect.top - (windowCaptionHeight / 2) + 1;
					}

					// tell the window the Frame has changed, not sure if it makes a difference
					this->SetWindowPos(HWND_TOP, windowRect.left, newTop, windowRect.Width(), windowRect.Height(), SWP_NOZORDER | SWP_NOCOPYBITS | SWP_FRAMECHANGED);

					// don't auto adjust unless auto is selected in options
					if (IsAdjustWindowToImage() && !(m_bAutoFitWndToImage && !IsImageExactlyFittingWindow())) {
						AdjustWindowToImage(false);
						this->Invalidate(FALSE);
					}

					StartLowQTimer(ZOOM_TIMEOUT);  // trigger a redraw as if zoom changed (might not be necessary)
				} else {
					m_pChrome->UpdateFrame(); // shows or hides toolbar, status bar and side panel
				}
			}

			break;
		case IDM_ALWAYS_ON_TOP:
			ToggleAlwaysOnTop();

			break;
		case IDM_FIT_WINDOW_TO_IMAGE:
			// Note: If auto fit is on but the window size does not match the image size (due to manual window resizing), restore window to image
			if (!(m_bAutoFitWndToImage && !IsImageExactlyFittingWindow()))
				m_bAutoFitWndToImage = !m_bAutoFitWndToImage;
			AdjustWindowToImage(false);
			break;
		case IDM_ZOOM_400:
			PerformZoom(4.0, false, m_bMouseOn, true);
			break;
		case IDM_ZOOM_200:
			PerformZoom(2.0, false, m_bMouseOn, true);
			break;
		case IDM_ZOOM_100:
			ResetZoomTo100Percents(m_bMouseOn);
			break;
		case IDM_ZOOM_50:
			PerformZoom(0.5, false, m_bMouseOn, true);
			break;
		case IDM_ZOOM_25:
			PerformZoom(0.25, false, m_bMouseOn, true);
			break;
		case IDM_ZOOM_INC:
		case IDM_ZOOM_DEC:
			PerformZoom((nCommand == IDM_ZOOM_INC) ? 1 : -1, true, m_bMouseOn, true);
			break;
		case IDM_ZOOM_MODE:
			m_bZoomModeOnLeftMouse = !m_bZoomModeOnLeftMouse;
			m_pNavPanelCtl->GetNavPanel()->GetBtnZoomMode()->SetActive(m_bZoomModeOnLeftMouse);
			break;
		case IDM_AUTO_ZOOM_FIT_NO_ZOOM:
		case IDM_AUTO_ZOOM_FILL_NO_ZOOM:
		case IDM_AUTO_ZOOM_FIT:
		case IDM_AUTO_ZOOM_FILL:
			{
				Helpers::EAutoZoomMode eAutoZoomMode = (Helpers::EAutoZoomMode)((nCommand - IDM_AUTO_ZOOM_FIT_NO_ZOOM) / 10);
				if (m_eAutoZoomModeFullscreen == m_eAutoZoomModeWindowed)
					m_eAutoZoomModeFullscreen = m_eAutoZoomModeWindowed = eAutoZoomMode;
				else if (m_bFullScreenMode)
					m_eAutoZoomModeFullscreen = eAutoZoomMode;
				else
					m_eAutoZoomModeWindowed = eAutoZoomMode;
				m_dZoom = -1.0;
				m_offsets = CPoint(0, 0);
				this->Invalidate(FALSE);
				AdjustWindowToImage(false);
				break;
			}
		case IDM_EDIT_GLOBAL_CONFIG:
		case IDM_EDIT_USER_CONFIG:
			EditINIFile(nCommand == IDM_EDIT_GLOBAL_CONFIG);
			break;
		case IDM_UPDATE_USER_CONFIG:
			if (::MessageBox(m_hWnd, CString(CNLS::GetString(_T("Update user settings with new settings from settings template file?"))) + _T('\n') +
				CNLS::GetString(_T("All existing user settings will be preserved.")), _T(JPEGVIEW_TITLE), MB_YESNOCANCEL | MB_ICONQUESTION) == IDYES) {
				CSettingsProvider::This().UpdateUserSettings();
			}
			break;
		case IDM_MANAGE_OPEN_WITH_MENU:
			{
				CManageOpenWithDlg dlgManageOpenWithMenu;
				dlgManageOpenWithMenu.DoModal();
			}
			break;
		case IDM_SET_AS_DEFAULT_VIEWER:
			SetAsDefaultViewer();
			break;
		case IDM_BACKUP_PARAMDB:
			CParameterDB::This().BackupParamDB(m_hWnd);
			break;
		case IDM_RESTORE_PARAMDB:
			CParameterDB::This().RestoreParamDB(m_hWnd);
			break;
		case IDM_ABOUT:
			{
				MouseOn();
				HMODULE hMod = ::LoadLibrary(_T("RICHED32.DLL"));
				CAboutDlg dlgAbout;
				dlgAbout.DoModal();
				::FreeLibrary(hMod);
			}
			break;
		case IDM_EXIT:
			CleanupAndTerminate();
			break;
		case IDM_DEFAULT_ESC:
			if (m_bMovieMode) {
				if (m_bAutoExit)
					CleanupAndTerminate();
				else
					StopMovieMode(); // stop any running movie/slideshow
			} else if (m_bIsAnimationPlaying) {
				if (m_bAutoExit)
					CleanupAndTerminate();
				else
					StopAnimation(); // stop any running animation
			} else {
				CleanupAndTerminate();
			}
			break;
		case IDM_ZOOM_SEL:
			ZoomToSelection();
			break;
		case IDM_CROP_SEL:
			if (m_pCurrentImage != NULL) {
				m_pCurrentImage->Crop(m_pCropCtl->GetImageCropRect(false));
				m_pImageProcPanelCtl->ShowHideSaveDBButtons();
				this->Invalidate(FALSE);
				AdjustWindowToImage(false);
			}
			break;
		case IDM_LOSSLESS_CROP_SEL:
			m_pCropCtl->CropLossless();
			break;
		case IDM_COPY_SEL:
			if (m_pCurrentImage != NULL) {
				CClipboard::CopyFullImageToClipboard(this->m_hWnd, m_pCurrentImage, *m_pImageProcParams, 
					CreateDefaultProcessingFlags(),
					m_pCropCtl->GetImageCropRect(false), NULL);
				this->Invalidate(FALSE);
			}
			break;
		case IDM_CROPMODE_FREE:
			m_pCropCtl->SetCropMode(CCropCtl::CM_Free);

			break;
		case IDM_CROPMODE_FIXED_SIZE:
			{
				CCropSizeDlg dlgSetCropSize;
				dlgSetCropSize.DoModal();
			}
			m_pCropCtl->SetCropMode(CCropCtl::CM_FixedSize);
			break;
		case IDM_CROPMODE_5_4:
			m_pCropCtl->SetCropRectAR(CSize(5, 4)); // 1.25
			break;
		case IDM_CROPMODE_4_3:
			m_pCropCtl->SetCropRectAR(CSize(4, 3)); // 1.333333333333333333
			break;
		case IDM_CROPMODE_7_5:
			m_pCropCtl->SetCropRectAR(CSize(7, 5)); // 1.4
			break;
		case IDM_CROPMODE_3_2:
			m_pCropCtl->SetCropRectAR(CSize(3, 2)); // 1.5
			break;
		case IDM_CROPMODE_16_9:
			m_pCropCtl->SetCropRectAR(CSize(16, 9)); // 1.777777777777777778
			break;
		case IDM_CROPMODE_16_10:
			m_pCropCtl->SetCropRectAR(CSize(16, 10)); // 1.6
			break;
		case IDM_CROPMODE_1_1:
			m_pCropCtl->SetCropRectAR(CSize(1, 1));
			break;
		case IDM_CROPMODE_USER:
			m_pCropCtl->SetCropRectAR(sp.UserCropAspectRatio());
			break;
		case IDM_CROPMODE_IMAGE:
			m_pCropCtl->SetCropMode(CCropCtl::CM_FixedAspectRatioImage);
			break;
		case IDM_TOUCH_IMAGE:
		case IDM_TOUCH_IMAGE_EXIF:
			if (m_pCurrentImage != NULL) {
				LPCTSTR strFileName = CurrentFileName(false);
				bool bOk;
				if (nCommand == IDM_TOUCH_IMAGE_EXIF) {
					bOk = EXIFHelpers::SetModificationDateToEXIF(strFileName, m_pCurrentImage);
				} else {
					SYSTEMTIME st;
					::GetSystemTime(&st);  // gets current time
					bOk = EXIFHelpers::SetModificationDate(strFileName, st);
				}
				if (bOk) {
					m_pFileList->ModificationTimeChanged();
					if (m_pEXIFDisplayCtl->IsActive()) {
						this->Invalidate(FALSE);
					}
				}
			}
			break;
		case IDM_TOUCH_IMAGE_EXIF_FOLDER:
			if (m_pCurrentImage != NULL && m_pFileList->CurrentDirectory() != NULL) {
				MouseOn();
				EXIFHelpers::EXIFResult result = EXIFHelpers::SetModificationDateToEXIFAllFiles(m_pFileList->CurrentDirectory());
				TCHAR buff1[128];
				_stprintf_s(buff1, 128, CNLS::GetString(_T("Number of JPEG files in folder: %d")), result.NumberOfSucceededFiles + result.NumberOfFailedFiles);
				TCHAR buff2[256];
				_stprintf_s(buff2, 256, CNLS::GetString(_T("EXIF date successfully set on %d images, failed on %d images")), result.NumberOfSucceededFiles, result.NumberOfFailedFiles);
				::MessageBox(m_hWnd, CString(buff1) + _T('\n') + buff2, _T(JPEGVIEW_TITLE), MB_OK | MB_ICONINFORMATION);
				m_pFileList->Reload();
				if (m_pEXIFDisplayCtl->IsActive()) {
					this->Invalidate(FALSE);
				}
			}
			break;
		case IDM_CONTRAST_CORRECTION_INC:
		case IDM_CONTRAST_CORRECTION_DEC:
			if (m_bAutoContrast) {
				double dInc = (nCommand == IDM_CONTRAST_CORRECTION_INC) ? 0.05 : -0.05;
				m_pImageProcParams->ContrastCorrectionFactor = max(0.0, min(1.0, m_pImageProcParams->ContrastCorrectionFactor + dInc));
				this->Invalidate(FALSE);
			}
			break;
		case IDM_COLOR_CORRECTION_INC:
		case IDM_COLOR_CORRECTION_DEC:
			if (m_bAutoContrast) {
				double dInc = (nCommand == IDM_COLOR_CORRECTION_INC) ? 0.05 : -0.05;
				m_pImageProcParams->ColorCorrectionFactor = max(-0.5, min(0.5, m_pImageProcParams->ColorCorrectionFactor + dInc));
				this->Invalidate(FALSE);
			}
			break;
		case IDM_CONTRAST_INC:
		case IDM_CONTRAST_DEC:
			AdjustContrast((nCommand == IDM_CONTRAST_INC)? CONTRAST_INC : -CONTRAST_INC);
			break;
		case IDM_GAMMA_INC:
		case IDM_GAMMA_DEC:
			AdjustGamma((nCommand == IDM_GAMMA_INC)? 1.0/GAMMA_FACTOR : GAMMA_FACTOR);
			break;
		case IDM_LDC_SHADOWS_INC:	
		case IDM_LDC_SHADOWS_DEC:
		case IDM_LDC_HIGHLIGHTS_INC:
		case IDM_LDC_HIGHLIGHTS_DEC:
			AdjustLDC((nCommand == IDM_LDC_HIGHLIGHTS_INC || nCommand == IDM_LDC_HIGHLIGHTS_DEC) ? DARKEN_HIGHLIGHTS : BRIGHTEN_SHADOWS,
				(nCommand == IDM_LDC_SHADOWS_INC || nCommand == IDM_LDC_HIGHLIGHTS_INC) ? LDC_INC : -LDC_INC);
			break;
		case IDM_TOGGLE_RESAMPLING_QUALITY:
			m_bHQResampling = !m_bHQResampling;
			this->Invalidate(FALSE);
			break;
		case IDM_TOGGLE_MONITOR:
			ToggleMonitor();
			break;
		case IDM_EXCHANGE_PROC_PARAMS:
			ExchangeProcessingParams();
			break;
		case IDM_PAN_UP:
		case IDM_PAN_DOWN:
		case IDM_PAN_RIGHT:
		case IDM_PAN_LEFT:
			PerformPan((nCommand == IDM_PAN_LEFT) ? PAN_STEP : (nCommand == IDM_PAN_RIGHT) ? -PAN_STEP : 0,
				(nCommand == IDM_PAN_UP) ? PAN_STEP : (nCommand == IDM_PAN_DOWN) ? -PAN_STEP : 0, false);
			break;
		case IDM_SHARPEN_INC:
		case IDM_SHARPEN_DEC:
			AdjustSharpen((nCommand == IDM_SHARPEN_INC) ? SHARPEN_INC : -SHARPEN_INC);
			break;
		case IDM_CONTEXT_MENU:
			BOOL bNotUsed;
			OnContextMenu(0, 0, (m_nMouseX & 0xFFFF) | ((m_nMouseY & 0xFFFF) << 16), bNotUsed);
			break;
		case IDM_SET_WALLPAPER_ORIG:
			if (m_pFileList->Current() != NULL && m_pCurrentImage != NULL) {
				SetDesktopWallpaper::SetFileAsWallpaper(*m_pCurrentImage, m_pFileList->Current());
			}
			break;
		case IDM_SET_WALLPAPER_DISPLAY:
			if (m_pCurrentImage != NULL) {
				SetDesktopWallpaper::SetProcessedImageAsWallpaper(*m_pCurrentImage);
			}
			break;
		case IDM_TOGGLE_TOOLBAR:
			m_pChrome->SetShowToolbar(!m_pChrome->GetShowToolbar());
			Invalidate(FALSE);
			break;
		case IDM_TOGGLE_STATUSBAR:
			m_pChrome->SetShowStatusBar(!m_pChrome->GetShowStatusBar());
			Invalidate(FALSE);
			break;
		case IDM_TOGGLE_SIDEPANEL:
			m_pChrome->SetShowSidePanel(!m_pChrome->GetShowSidePanel());
			Invalidate(FALSE);
			break;
		case IDM_TOGGLE_EDITPANEL:
			if (m_pChrome->IsEnabled()) {
				// the sliders are on the edit tab of the side panel, the panel over the image is not needed
				m_pImageProcPanelCtl->SetPinned(false);
				m_pChrome->ShowSideTab(m_pChrome->IsEditTabShown() ? CWindowChrome::TAB_INFO : CWindowChrome::TAB_EDIT);
				Invalidate(FALSE);
			} else {
				m_pImageProcPanelCtl->SetPinned(!m_pImageProcPanelCtl->IsPinned());
			}
			m_pChrome->Paint();
			break;
		case IDM_TOGGLE_SIDEPANEL_COLLAPSED:
			if (!m_pChrome->GetShowSidePanel()) {
				m_pChrome->SetShowSidePanel(true);
				m_pChrome->SetSidePanelCollapsed(false);
			} else {
				m_pChrome->SetSidePanelCollapsed(!m_pChrome->GetSidePanelCollapsed());
			}
			Invalidate(FALSE);
			break;
		case IDM_COMPARE_BEFORE_AFTER:
			SetCompareMode(!m_bCompareMode);
			break;
		case IDM_REMOVE_BACKGROUND:
			if (m_pCurrentImage != NULL && m_pCurrentImage->OriginalPixels() != NULL) {
				if (!BackgroundRemoval::IsModelAvailable() &&
					!BackgroundRemoval::DownloadModel(m_hWnd, CNLS::GetString(_T("Downloading the background removal model (176 MB)")), CNLS::GetString(_T("Cancel")))) {
					m_pChrome->ShowMessage(CNLS::GetString(_T("The background removal model could not be downloaded.")));
					break;
				}
				HCURSOR hOldCursor = ::SetCursor(::LoadCursor(NULL, IDC_WAIT));
				std::vector<uint8> alpha;
				std::wstring sError;
				// the original pixels may show the checker board of a previous removal, the backup has the colors
				const void* pPixels = m_transparencyBackup.empty() ? m_pCurrentImage->OriginalPixels() : &m_transparencyBackup[0];
				int nChannels = m_transparencyBackup.empty() ? m_pCurrentImage->OriginalChannels() : 4;
				bool bSuccess = BackgroundRemoval::ComputeMask(pPixels, m_pCurrentImage->OrigWidth(), m_pCurrentImage->OrigHeight(), nChannels, alpha, sError);
				::SetCursor(hOldCursor);
				if (!bSuccess) {
					m_pChrome->ShowMessage(sError.c_str());
				} else {
					ApplyTransparencyMask(alpha);
					m_pChrome->ShowSideTab(CWindowChrome::TAB_DRAW);
					m_pChrome->ShowMessage(CNLS::GetString(_T("Background removed. Save it with 'Save as transparent PNG'.")));
				}
			}
			break;
		case IDM_FIND_SIMILAR:
			if (m_pFileList != NULL && m_pFileList->Size() > 1) {
				MouseOn();
				CDuplicatesDlg dlgSimilar(this);
				dlgSimilar.DoModal();
			}
			break;
		case IDM_TOGGLE_FILMSTRIP:
			m_pChrome->SetShowFilmstrip(!m_pChrome->GetShowFilmstrip());
			Invalidate(FALSE);
			break;
		case IDM_SAVE_TRANSPARENT:
			if (m_pCurrentImage != NULL && HasTransparency() && m_pCurrentImage->OriginalPixels() != NULL) {
				LPCTSTR sCurrent = m_pFileList->Current();
				CString sProposal = (sCurrent != NULL && !m_pCurrentImage->IsClipboardImage()) ? CString(sCurrent) : CString(_T("image"));
				int nDot = sProposal.ReverseFind(_T('.'));
				if (nDot > sProposal.ReverseFind(_T('\\'))) sProposal = sProposal.Left(nDot);
				sProposal += _T("_transparent.png");
				CString sFileName;
				int nFilterIndex = 1;
				if (HelpersGUI::ShowFileDialog(m_hWnd, true, _T("PNG (*.png)|*.png|"), _T("png"), sProposal, NULL, nFilterIndex, sFileName)) {
					HCURSOR hOldCursor = ::SetCursor(::LoadCursor(NULL, IDC_WAIT));
					bool bSuccess = CAnnotations::SaveTransparentPNG(sFileName, &m_transparencyBackup[0], &m_transparencyMask[0],
						m_pCurrentImage->OrigWidth(), m_pCurrentImage->OrigHeight());
					::SetCursor(hOldCursor);
					m_pFileList->Reload();
					m_pChrome->ShowMessage(CNLS::GetString(bSuccess ? _T("The transparent image was saved.") : _T("Error writing file to disk!")));
				}
			}
			break;
		case IDM_UNDO_TRANSPARENCY:
			ClearTransparency(true);
			Invalidate(FALSE);
			m_pChrome->Paint();
			break;
		case IDM_EXPORT:
			if (m_pCurrentImage != NULL) {
				MouseOn();
				CExportDlg dlgExport(this);
				if (dlgExport.DoModal()) {
					m_pFileList->Reload();
					m_pChrome->ShowMessage(CNLS::GetString(_T("The image was exported.")));
				}
				Invalidate(FALSE);
			}
			break;
		case IDM_SHARE:
			if (m_pCurrentImage != NULL && !m_pCurrentImage->IsClipboardImage() && m_pFileList->Current() != NULL) {
				if (!Share::ShareFile(m_hWnd, m_pFileList->Current())) {
					m_pChrome->ShowMessage(CNLS::GetString(_T("The share window of Windows is not available.")));
				}
			}
			break;
		case IDM_TOGGLE_CROP_TOOL:
			m_bCropTool = !m_bCropTool;
			if (m_bCropTool) {
				m_pChrome->ShowMessage(CNLS::GetString(_T("Drag on the image to select the area to crop.")));
			}
			m_pChrome->Paint();
			break;
		case IDM_OCR_COPY_TEXT:
			if (m_pCurrentImage != NULL && m_pCurrentImage->OriginalPixels() != NULL) {
				HCURSOR hOldCursor = ::SetCursor(::LoadCursor(NULL, IDC_WAIT));
				std::wstring sText, sError;
				bool bSuccess = Ocr::RecognizeText(m_pCurrentImage->OriginalPixels(), m_pCurrentImage->OrigWidth(), m_pCurrentImage->OrigHeight(),
					m_pCurrentImage->OriginalChannels(), sText, sError);
				::SetCursor(hOldCursor);
				if (!bSuccess) {
					m_pChrome->ShowMessage(sError.c_str());
				} else if (sText.empty()) {
					m_pChrome->ShowMessage(CNLS::GetString(_T("No text was found in the image.")));
				} else if (::OpenClipboard(m_hWnd)) {
					::EmptyClipboard();
					size_t nBytes = (sText.size() + 1) * sizeof(wchar_t);
					HGLOBAL hText = ::GlobalAlloc(GMEM_MOVEABLE, nBytes);
					if (hText != NULL) {
						memcpy(::GlobalLock(hText), sText.c_str(), nBytes);
						::GlobalUnlock(hText);
						::SetClipboardData(CF_UNICODETEXT, hText);
					}
					::CloseClipboard();
					int nLines = 0;
					for (wchar_t c : sText) if (c == L'\n') nLines++;
					CString sMessage;
					sMessage.Format(CNLS::GetString(_T("%d lines of text were copied to the clipboard.")), nLines);
					m_pChrome->ShowMessage(sMessage);
				}
			}
			break;
		case IDM_TOGGLE_DRAWPANEL:
			if (m_pChrome->IsEnabled()) {
				m_pChrome->ShowSideTab(m_pChrome->IsDrawTabShown() ? CWindowChrome::TAB_INFO : CWindowChrome::TAB_DRAW);
				Invalidate(FALSE);
			}
			break;
		case IDM_ANNOTATION_UNDO:
			m_pAnnotations->Undo();
			Invalidate(FALSE);
			m_pChrome->Paint();
			break;
		case IDM_ANNOTATION_CLEAR:
			m_pAnnotations->Clear();
			Invalidate(FALSE);
			m_pChrome->Paint();
			break;
		case IDM_ANNOTATION_APPLY:
			ApplyAnnotations();
			break;
		case IDM_SIGNATURE_SAVE:
			m_pChrome->ShowMessage(CNLS::GetString(m_pAnnotations->SaveSignature() ? _T("The pen strokes were saved as signature.") :
				_T("Write your signature with the pen first.")));
			break;
		case IDM_SIGNATURE_INSERT:
			if (m_pCurrentImage != NULL && m_pAnnotations->InsertSignature(m_pCurrentImage->OrigWidth() / 2.0f,
				m_pCurrentImage->OrigHeight() / 2.0f, m_pCurrentImage->OrigWidth() / 4.0f)) {
				Invalidate(FALSE);
				m_pChrome->Paint();
			}
			break;
		case IDM_SETTINGS:
			{
				MouseOn();
				CSettingsDlg dlgSettings(this);
				if (dlgSettings.DoModal()) {
					Invalidate(FALSE);
				}
			}
			break;
		case IDM_RESET_PROCESSING:
			ResetImageProcessing();
			break;
		case IDM_UNSHARP_MASK:
			if (m_pCurrentImage != NULL) {
				m_pUnsharpMaskPanelCtl->SetVisible(true);
			}
			break;
		case IDM_CONVERT_MENU:
			ShowConvertMenuAndExecute();
			break;
		case IDM_EXPLORER_CONVERT_MENU:
			{
				bool bRegistered = Converter::IsExplorerMenuRegistered();
				bool bSuccess = bRegistered ? Converter::UnregisterExplorerMenu() : Converter::RegisterExplorerMenu();
				m_pChrome->ShowMessage(CNLS::GetString(!bSuccess ? _T("The Explorer context menu could not be changed.") :
					bRegistered ? _T("'Convert to' was removed from the Explorer context menu.") : _T("'Convert to' was added to the Explorer context menu.")));
			}
			break;
	}
	if (nCommand >= IDM_FIRST_CONVERT_CMD && nCommand <= IDM_LAST_CONVERT_CMD) {
		StartConversion(nCommand - IDM_FIRST_CONVERT_CMD);
	}
	if (nCommand >= IDM_FIRST_USER_CMD && nCommand <= IDM_LAST_USER_CMD) {
		ExecuteUserCommand(HelpersGUI::FindUserCommand(nCommand - IDM_FIRST_USER_CMD));
	}
	if (nCommand >= IDM_FIRST_OPENWITH_CMD && nCommand <= IDM_LAST_OPENWITH_CMD) {
		ExecuteUserCommand(HelpersGUI::FindOpenWithCommand(nCommand - IDM_FIRST_OPENWITH_CMD));
	}
}

// Setting window styles have gotten out of hand with the addition of no title bar
// instead of each call trying to figure out the logic, consolidate it to one function
LONG CMainDlg::SetCurrentWindowStyle() {
	if (!m_bWindowBorderless) {
		return this->SetWindowLongW(GWL_STYLE, this->GetWindowLongW(GWL_STYLE) | WS_OVERLAPPEDWINDOW | WS_VISIBLE);
	} else {
		return this->SetWindowLongW(GWL_STYLE, this->GetWindowLongW(GWL_STYLE) & ~WS_OVERLAPPEDWINDOW | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_VISIBLE);  // lose resizing
		// just doing (& ~WS_CAPTION) leads to having a sliver of white bar on top but allows for resizing
	}
}


void CMainDlg::ExploreFile() {
	ITEMIDLIST* pidl = ILCreateFromPath(CurrentFileName(false));
	if (pidl) {
		// https://docs.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-shopenfolderandselectitems
		SHOpenFolderAndSelectItems(pidl, 0, 0, 0);
		ILFree(pidl);
	}
}

bool CMainDlg::OpenFileWithDialog(bool bFullScreen, bool bAfterStartup) {
	StopMovieMode();
	StopAnimation();
	MouseOn();
	LPCTSTR sFileEndings = CFileList::GetSupportedFileEndings();
	CString sFilter = CString(CNLS::GetString(_T("Images"))) + _T(" (") + sFileEndings + _T(")|") + sFileEndings + _T("|") +
		CNLS::GetString(_T("All Files")) + _T("|*.*|");
	CString sFileName;
	int nFilterIndex = 1;
	LPCTSTR sCurrent = m_pFileList->Current();
	if (HelpersGUI::ShowFileDialog(this->m_hWnd, false, sFilter, _T("jpg"), (sCurrent != NULL) ? sCurrent : m_pFileList->CurrentDirectory(),
		NULL, nFilterIndex, sFileName)) {
		m_isBeforeFileSelected = false;
		OpenFile(sFileName, bAfterStartup);
		return true;
	}
	m_isBeforeFileSelected = false;
	return false;
}

void CMainDlg::OpenFile(LPCTSTR sFileName, bool bAfterStartup) {
	StopMovieMode();
	StopAnimation();
	// recreate file list based on image opened
	Helpers::ESorting eOldSorting = m_pFileList->GetSorting();
	bool oOldAscending = m_pFileList->IsSortedAscending();
	delete m_pFileList;
	m_sStartupFile = sFileName;
	m_pFileList = new CFileList(m_sStartupFile, *m_pDirectoryWatcher, eOldSorting, oOldAscending, CSettingsProvider::This().WrapAroundFolder());
	// free current image and all read ahead images
	InitParametersForNewImage();
	m_pJPEGProvider->NotifyNotUsed(m_pCurrentImage);
	m_pJPEGProvider->ClearAllRequests();
	m_pCurrentImage = m_pJPEGProvider->RequestImage(m_pFileList, CJPEGProvider::FORWARD,
		m_pFileList->Current(), 0, CreateProcessParams(!m_bFullScreenMode && (bAfterStartup || IsAdjustWindowToImage())),
		m_bOutOfMemoryLastImage, m_bExceptionErrorLastImage);
	m_nLastLoadError = GetLoadErrorAfterOpenFile();
	if (bAfterStartup) CheckIfApplyAutoFitWndToImage(false);
	AfterNewImageLoaded(true, false, false);
	if (m_pCurrentImage != NULL && m_pCurrentImage->IsAnimation()) {
		StartAnimation();
	}
	m_startMouse.x = m_startMouse.y = -1;
	m_sSaveDirectory = _T("");
	MouseOff();
	this->Invalidate(FALSE);
}

bool CMainDlg::SaveImage(bool bFullSize) {
	if (m_bMovieMode) {
		return false;
	}

	MouseOn();

	CString sCurrentFile;
	if (m_sSaveDirectory.GetLength() == 0) {
		sCurrentFile = CurrentFileName(false);
	} else {
		sCurrentFile = m_sSaveDirectory + CurrentFileName(true);
	}
	int nIndexPoint = sCurrentFile.ReverseFind(_T('.'));
	if (nIndexPoint > 0) {
		sCurrentFile = sCurrentFile.Left(nIndexPoint);
		sCurrentFile += _T("_proc");
	}

	CString sExtension = m_sSaveExtension;
	if (sExtension.IsEmpty()) {
		sExtension = CSettingsProvider::This().DefaultSaveFormat();
	}
	// NOTE: this list is used in the "Edit with" registry entry in JPEGView.Setup, update that when this updates
	CString sFilter = CString(_T("JPEG (*.jpg;*.jpeg)|*.jpg;*.jpeg|BMP (*.bmp)|*.bmp|PNG (*.png)|*.png|TIFF (*.tiff;*.tif)|*.tiff;*.tif|WEBP (*.webp)|*.webp|WEBP lossless (*.webp)|*.webp|QOI (*.qoi)|*.qoi|")) +
		CNLS::GetString(_T("All Files")) + _T("|*.*|");
	int nFilterIndex = 1;
	if (sExtension.CompareNoCase(_T("bmp")) == 0) {
		nFilterIndex = 2;
	} else if (sExtension.CompareNoCase(_T("png")) == 0) {
		nFilterIndex = 3;
	} else if (sExtension.CompareNoCase(_T("tiff")) == 0 || sExtension.CompareNoCase(_T("tif")) == 0) {
		nFilterIndex = 4;
	} else if (sExtension.CompareNoCase(_T("webp")) == 0) {
		nFilterIndex = m_bUseLosslessWEBP ? 6 : 5;
	} else if (sExtension.CompareNoCase(_T("qoi")) == 0) {
		nFilterIndex = 7;
	}
	CString sFileName;
	if (HelpersGUI::ShowFileDialog(m_hWnd, true, sFilter, sExtension, sCurrentFile + _T(".") + sExtension,
		bFullSize ? NULL : CNLS::GetString(_T("Save as (in screen size/resolution)")), nFilterIndex, sFileName)) {
		m_sSaveDirectory = sFileName;
		m_bUseLosslessWEBP = nFilterIndex == 6;
		m_sSaveExtension = m_sSaveDirectory.Right(m_sSaveDirectory.GetLength() - m_sSaveDirectory.ReverseFind(_T('.')) - 1);
		m_sSaveDirectory = m_sSaveDirectory.Left(m_sSaveDirectory.ReverseFind(_T('\\')) + 1);
		return SaveImageNoPrompt(sFileName, bFullSize);
	}
	return false;
}

bool CMainDlg::SaveImageNoPrompt(LPCTSTR sFileName, bool bFullSize) {
	if (m_bMovieMode) {
		return false;
	}
	// the drawings become part of the saved image
	if (!m_pAnnotations->IsEmpty()) {
		ApplyAnnotations();
		UpdateWindow(); // creates the DIB with the drawings for saving in screen size
	}

	MouseOn();

	HCURSOR hOldCursor = ::SetCursor(::LoadCursor(NULL, IDC_WAIT));	

	if (CSaveImage::SaveImage(sFileName, m_pCurrentImage, *m_pImageProcParams, 
		CreateDefaultProcessingFlags(), bFullSize, m_bUseLosslessWEBP)) {
		m_pFileList->Reload(); // maybe image is stored to current directory - needs reload
		::SetCursor(hOldCursor);
		Invalidate();
		return true;
	} else {
		::SetCursor(hOldCursor);
		::MessageBox(m_hWnd, CNLS::GetString(_T("Error saving file")), 
			CNLS::GetString(_T("Error writing file to disk!")), MB_ICONSTOP | MB_OK);
		return false;
	}
}

void CMainDlg::BatchCopy() {
	if (m_bMovieMode) {
		return;
	}
	MouseOn();

	CBatchCopyDlg dlgBatchCopy(*m_pFileList);
	dlgBatchCopy.DoModal();
	this->Invalidate(FALSE);
}

void CMainDlg::SetAsDefaultViewer() {
	if (m_bMovieMode) {
		return;
	}
	MouseOn();

	if (Helpers::GetWindowsVersion() >= 602) {
		// It is Windows 8 or later, launch the system provided assiciation setting dialog
		CFileExtensionsRegistrationWindows8 registry;
		if (registry.RegisterJPEGView()) {
			registry.LaunchApplicationAssociationDialog();
		} else {
			CString sError = CNLS::GetString(_T("Error while writing the following registry key"));
			sError += _T(":\n");
			sError += registry.GetLastFailedRegistryKey();
			::MessageBox(m_hWnd, sError, CNLS::GetString(_T("Error")), MB_OK | MB_ICONERROR);
		}
	} else {
		CFileExtensionsDlg dlgSetAsDefaultViewer;
		dlgSetAsDefaultViewer.DoModal();
	}
	Invalidate(FALSE);
}


void CMainDlg::HandleUserCommands(uint32 virtualKeyCode) {
	if (::GetFileAttributes(CurrentFileName(false)) == INVALID_FILE_ATTRIBUTES) {
		return; // file does not exist
	}

	// iterate over user command list
	std::list<CUserCommand*>::iterator iter;
	std::list<CUserCommand*> & userCmdList = CSettingsProvider::This().UserCommandList();
	for (iter = userCmdList.begin( ); iter != userCmdList.end( ); iter++ ) {
		if ((*iter)->GetKeyCode() == virtualKeyCode) {
			ExecuteUserCommand(*iter);
			return;
		}
	}
	// iterate over open with command list
	std::list<CUserCommand*> & openWithCmdList = CSettingsProvider::This().OpenWithCommandList();
	for (iter = openWithCmdList.begin( ); iter != openWithCmdList.end( ); iter++ ) {
		if ((*iter)->GetKeyCode() == virtualKeyCode) {
			ExecuteUserCommand(*iter);
			return;
		}
	}
}

void CMainDlg::ExecuteUserCommand(CUserCommand* pUserCommand) {
	MouseOn();
	LPCTSTR sCurrentFileName = CurrentFileName(false);
	if (pUserCommand != NULL && pUserCommand->CanExecute(this->m_hWnd, sCurrentFileName)) {
		bool bReloadCurrent = false;
		// First go to next, then execute the command and reload current file.
		// Otherwise we get into troubles when the current image was deleted or moved.
		CFileList* oldFileList = m_pFileList;
		if (pUserCommand->MoveToNextAfterCommand()) {
			// don't send a request if we reload all, it would be canceled anyway later
			GotoImage(POS_AwayFromCurrent, pUserCommand->NeedsReloadAll() ? NO_REQUEST : 0);
		}
		if (pUserCommand->Execute(this->m_hWnd, sCurrentFileName)) {
			if (pUserCommand->NeedsReloadAll() || pUserCommand->NeedsReloadFileList()) {
				m_pFileList->Reload(NULL, false);
				if (oldFileList != m_pFileList) {
					// If GotoImage(POS_AwayFromCurrent) changed the file list, the old file list also needs to be reloaded
					oldFileList->Reload(NULL, false);
				}
				m_pFileList->DeleteHistory(true);
			}
			if (pUserCommand->NeedsReloadAll()) {
				m_pJPEGProvider->NotifyNotUsed(m_pCurrentImage);
				m_pJPEGProvider->ClearAllRequests();
				m_pCurrentImage = NULL;
				bReloadCurrent = true;
			} else {
				if (pUserCommand->NeedsReloadFileList()) {
					bReloadCurrent = m_pFileList->Current() == NULL; // needs "reload" in this case
					Invalidate();
				}
				if (pUserCommand->NeedsReloadCurrent() && !pUserCommand->MoveToNextAfterCommand()) {
					bReloadCurrent = true;
				}
			}
			if (bReloadCurrent) {
				ReloadImage(false);
			}
		}
	}
}

void CMainDlg::StartDragging(int nX, int nY, bool bDragWithZoomNavigator) {
	m_startMouse.x = m_startMouse.y = -1;
	m_bDragging = true;
	if (bDragWithZoomNavigator) {
		m_pZoomNavigatorCtl->StartDragging(nX, nY);
	}
	m_nCapturedX = nX;
	m_nCapturedY = nY;
	SetCursorForMoveSection();
	m_pNavPanelCtl->StartNavPanelAnimation(true, true);
}

void CMainDlg::DoDragging() {
	if (m_bDragging && m_pCurrentImage != NULL) {
		int nXDelta = m_nMouseX - m_nCapturedX;
		int nYDelta = m_nMouseY - m_nCapturedY;
		if (!m_bDoDragging && (nXDelta != 0 || nYDelta != 0)) {
			m_bDoDragging = true;
		}
		if (m_pZoomNavigatorCtl->IsDragging()) {
			m_pZoomNavigatorCtl->DoDragging(nXDelta, nYDelta);
		} else {
			if (PerformPan(nXDelta, nYDelta, false)) {
				m_nCapturedX = m_nMouseX;
				m_nCapturedY = m_nMouseY;
			}
		}
	}
}

void CMainDlg::EndDragging() {
	if (!m_bDoDragging && !m_pZoomNavigatorCtl->IsPointInZoomNavigatorThumbnail(CPoint(m_nMouseX, m_nMouseY))) {
		if (!GetNavPanelCtl()->IsVisible()) {
			MouseOn();
			GetNavPanelCtl()->ShowNavPanelTemporary();
		} else {
			GetNavPanelCtl()->HideNavPanelTemporary();
		}
	}
	m_bDragging = false;
	m_bDoDragging = false;
	m_pZoomNavigatorCtl->EndDragging();
	if (m_pCurrentImage != NULL) {
		m_pCurrentImage->VerifyDIBPixelsCreated();
	}
	if (m_DIBOffsets != CPoint(0, 0)) {
		Invalidate(FALSE);
	} else {
		this->InvalidateRect(m_pNavPanelCtl->PanelRect(), FALSE);
	}
	SetCursorForMoveSection();
}

void CMainDlg::GotoImage(EImagePosition ePos) {
	GotoImage(ePos, 0);
}

void CMainDlg::GotoImage(EImagePosition ePos, int nFlags) {
	// Timer handling for slideshows
	if (ePos == POS_Next || ePos == POS_NextSlideShow) {
		if (m_nCurrentTimeout > 0) {
			StartSlideShowTimer(m_nCurrentTimeout);
		}
		StopAnimation();
	} else if (ePos != POS_NextAnimation) {
		StopMovieMode();
		StopAnimation();
	}

	m_pCropCtl->CancelCropping(); // cancel any running crop

	// solving a specific "edge" case, pun intended
	// (doesn't happen with first image, because we would be at frame 0 already)
	// if wraparound = false,
	// if we are at the last image,
	// if we're at the last index of a multi-framed image (not animation)
	// don't wrap around to index 0
	bool bNoWrapAroundEdgeFrame = !CSettingsProvider::This().WrapAroundFolder() &&
		(m_pCurrentImage == NULL ? false : m_pCurrentImage->NumberOfFrames() > 1 && !m_pCurrentImage->IsAnimation());

	int nFrameIndex = 0;
	bool bCheckIfSameImage = true;
	m_pFileList->SetCheckpoint();
	CFileList* pOldFileList = m_pFileList;
	int nOldFrameIndex = (m_pCurrentImage == NULL) ? 0 : m_pCurrentImage->FrameIndex();
	CJPEGProvider::EReadAheadDirection eDirection = CJPEGProvider::FORWARD;
	switch (ePos) {
		case POS_First:
			m_pFileList->First();
			break;
		case POS_Last:
			m_pFileList->Last();
			break;
		case POS_Next:
		case POS_NextAnimation:
			{
				bool bGotoNextImage = true;
				nFrameIndex = Helpers::GetFrameIndex(m_pCurrentImage, true, ePos == POS_NextAnimation, bGotoNextImage);
				if (bGotoNextImage)
					m_pFileList = m_pFileList->Next();
				else
					bNoWrapAroundEdgeFrame = false; // the "next" operation didn't request going to the next image (next frame index is not current one)
				break;
			}
		case POS_NextSlideShow:
			m_pFileList = m_pFileList->Next();
			break;
		case POS_Previous:
			{
				bool bGotoPrevImage;
				nFrameIndex = Helpers::GetFrameIndex(m_pCurrentImage, false, false, bGotoPrevImage);
				if (bGotoPrevImage) m_pFileList = m_pFileList->Prev();
				eDirection = CJPEGProvider::BACKWARD;
				bNoWrapAroundEdgeFrame = false; // previous image at edge is not a wraparound
				break;
			}
		case POS_Toggle:
			m_pFileList->ToggleBetweenMarkedAndCurrentFile();
			eDirection = CJPEGProvider::TOGGLE;
			break;
		case POS_Current:
		case POS_Clipboard:
			bCheckIfSameImage = false; // do something even when not moving iterator on filelist
			break;
		case POS_AwayFromCurrent:
			m_pFileList = m_pFileList->AwayFromCurrent();
			break;
	}

	if (bCheckIfSameImage && (m_pFileList == pOldFileList && (nOldFrameIndex == nFrameIndex || bNoWrapAroundEdgeFrame) && !m_pFileList->ChangedSinceCheckpoint())) {
		if (m_bMovieMode && m_bAutoExit) {
			CleanupAndTerminate();
		} else {
			if (CSettingsProvider::This().FlashWindowAlert()) {
				// Flash window to notify user
				FlashWindow(TRUE);  // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-flashwindow
			}

			if (CSettingsProvider::This().BeepSoundAlert()) {
				// Use the system's default beep sound
				MessageBeep(0xFFFFFFFF);  // https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-messagebeep
			}

			return; // not placed on a new image, don't do anything
		}
	}

	if (ePos != POS_Current && ePos != POS_NextAnimation && ePos != POS_Clipboard && ePos != POS_AwayFromCurrent) {
		MouseOff();
	}

	if (nFlags & KEEP_PARAMETERS) {
		if (!(m_bUserZoom || IsAdjustWindowToImage())) {
			m_dZoom = -1;
		}
	} else {
		InitParametersForNewImage();
	}
	m_pJPEGProvider->NotifyNotUsed(m_pCurrentImage);
	if (ePos == POS_Current || ePos == POS_AwayFromCurrent) {
		m_pJPEGProvider->ClearRequest(m_pCurrentImage, ePos == POS_AwayFromCurrent);
	}
	m_pCurrentImage = NULL;

	// do not perform a new image request if flagged
	if (nFlags & NO_REQUEST) {
		return;
	}

	CProcessParams procParams = CreateProcessParams(false);
	if (nFlags & KEEP_PARAMETERS) {
		procParams.ProcFlags = SetProcessingFlag(procParams.ProcFlags, PFLAG_KeepParams, true);
	}
	if (ePos == POS_Clipboard) {
		m_pCurrentImage = CClipboard::PasteImageFromClipboard(m_hWnd, procParams.ImageProcParams, procParams.ProcFlags);
		if (m_pCurrentImage != NULL) {
			m_pCurrentImage->SetFileDependentProcessParams(_T("_cbrd_"), &procParams);
			m_nLastLoadError = HelpersGUI::FileLoad_Ok;
		} else {
			m_nLastLoadError = HelpersGUI::FileLoad_PasteFromClipboardFailed;
		}
	} else {
		m_pCurrentImage = m_pJPEGProvider->RequestImage(m_pFileList, (ePos == POS_AwayFromCurrent) ? CJPEGProvider::NONE : eDirection,  
			m_pFileList->Current(), nFrameIndex, procParams,
			m_bOutOfMemoryLastImage, m_bExceptionErrorLastImage);
		m_nLastLoadError = (m_pCurrentImage == NULL) ? ((m_pFileList->Current() == NULL) ? HelpersGUI::FileLoad_NoFilesInDirectory : HelpersGUI::FileLoad_LoadError) : HelpersGUI::FileLoad_Ok;
	}
	double minimalDisplayTime = CSettingsProvider::This().MinimalDisplayTime();
	bool bSynchronize = (nFlags & KEEP_PARAMETERS) == 0;
	AfterNewImageLoaded(bSynchronize, false, minimalDisplayTime > 0);

	// if it is an animation (currently only animated GIF) start movie automatically
	if (m_pCurrentImage != NULL && m_pCurrentImage->IsAnimation()) {
		if (m_bIsAnimationPlaying) {
			AdjustAnimationFrameTime();
		} else {
			StartAnimation();
		}
	}

	// Sleep if a minimal display time is defined
	double currentTime = Helpers::GetExactTickCount();
	if (ePos == POS_Next || ePos == POS_Previous) {
		double imageTime = currentTime - m_dLastImageDisplayTime;
		if (minimalDisplayTime > 0 && (imageTime < minimalDisplayTime)) ::Sleep((int)(minimalDisplayTime - imageTime));
	}
	m_dLastImageDisplayTime = Helpers::GetExactTickCount();

	// Do that now when using a minimal display time, as it has been skipped in AfterNewImageLoaded() to avoid wrong display during the ::Sleep()
	if (minimalDisplayTime > 0 && bSynchronize && !m_bIsAnimationPlaying) {
		AdjustWindowToImage(false);
	}

	if (((nFlags & NO_UPDATE_WINDOW) == 0) && !(ePos == POS_NextSlideShow && UseSlideShowTransitionEffect())) {
		this->Invalidate(FALSE);
		// this will force to wait until really redrawn, preventing to process images but do not show them
		this->UpdateWindow();
	}

	// remove key messages accumulated so far
	if (!(nFlags & NO_REMOVE_KEY_MSG)) {
		MSG msg;
		while (::PeekMessage(&msg, this->m_hWnd, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE));
	}
}

void CMainDlg::ReloadImage(bool keepParameters, bool updateWindow) {
	int flags = keepParameters ? KEEP_PARAMETERS : 0;
	GotoImage(CMainDlg::POS_Current, updateWindow ? flags : flags | NO_UPDATE_WINDOW);
}

void CMainDlg::AdjustLDC(int nMode, double dInc) {
	if (nMode == BRIGHTEN_SHADOWS) {
		m_pImageProcParams->LightenShadows = min(1.0, max(0.0, m_pImageProcParams->LightenShadows + dInc));
	} else {
		m_pImageProcParams->DarkenHighlights = min(1.0, max(0.0, m_pImageProcParams->DarkenHighlights + dInc));
	}
	this->Invalidate(FALSE);
}

void CMainDlg::AdjustGamma(double dFactor) {
	m_pImageProcParams->Gamma *= dFactor;
	m_pImageProcParams->Gamma = min(2.0, max(0.5, m_pImageProcParams->Gamma));
	this->Invalidate(FALSE);
}

void CMainDlg::AdjustContrast(double dInc) {
	m_pImageProcParams->Contrast += dInc;
	m_pImageProcParams->Contrast = min(0.5, max(-0.5, m_pImageProcParams->Contrast));
	this->Invalidate(FALSE);
}

void CMainDlg::AdjustSharpen(double dInc) {
	m_pImageProcParams->Sharpen += dInc;
	m_pImageProcParams->Sharpen = min(0.5, max(0.0, m_pImageProcParams->Sharpen));
	this->Invalidate(FALSE);
}

// returns true on success, false if nothing was done
bool CMainDlg::PerformZoom(double dValue, bool bExponent, bool bZoomToMouse, bool bAdjustWindowToImage) {
	double dOldZoom = m_dZoom;
	m_bUserZoom = true;
	m_isUserFitToScreen = false;
	if (bExponent) {
		m_dZoom = m_dZoom * pow(m_dZoomMult, dValue);
	} else {
		m_dZoom = dValue;
	}

	if (m_pCurrentImage == NULL) {
		m_dZoom = max(Helpers::ZoomMin, min(Helpers::ZoomMax, m_dZoom));
		return true;
	}

	double dZoomMin = max(0.0001, min(Helpers::ZoomMin, GetZoomFactorForFitToScreen(false, false) * 0.5));
	m_dZoom = max(dZoomMin, min(Helpers::ZoomMax, m_dZoom));

	// always try to snap to 100%... aka if within 1% of 100%, snap exactly to it
	if (abs(m_dZoom - 1.0) < 0.01) {
		m_dZoom = 1.0;
	}

	// only pause on some percent if enabled
	double pauseAtZoom = CSettingsProvider::This().ZoomPauseFactor();
	if (pauseAtZoom != 0) {
		// snap to zoom factor... aka if within 1% of the set zoom factor, snap exactly to it
		// skip it if the zoom factor is 100% since it's already checked above - save one expensive calculation of abs()
		if (pauseAtZoom != 1 && abs(m_dZoom - pauseAtZoom) < 0.01) {
			m_dZoom = pauseAtZoom;
		}

		if ((dOldZoom - pauseAtZoom) * (m_dZoom - pauseAtZoom) <= 0 && m_bInZooming && !m_bZoomMode) {
			// make a stop at 100 % (or whatever % is configured)
			m_dZoom = pauseAtZoom;
		}
	}

	// Never create images more than 65535 pixels wide or high - the basic processing cannot handle it
	int nOldXSize = (int)(m_pCurrentImage->OrigWidth() * dOldZoom + 0.5);
	int nOldYSize = (int)(m_pCurrentImage->OrigHeight() * dOldZoom + 0.5);
	int nNewXSize = (int)(m_pCurrentImage->OrigWidth() * m_dZoom + 0.5);
	int nNewYSize = (int)(m_pCurrentImage->OrigHeight() * m_dZoom + 0.5);
	if (nNewXSize > 65535 || nNewYSize > 65535) {
		double dFac = 65535.0/max(nNewXSize, nNewYSize);
		m_dZoom = m_dZoom*dFac;
		nNewXSize = (int)(m_pCurrentImage->OrigWidth() * m_dZoom + 0.5);
		nNewYSize = (int)(m_pCurrentImage->OrigHeight() * m_dZoom + 0.5);
	}

#ifdef DEBUG
	CString a; a.Format(_T("dZoom dOldZoom: %f %f\n"), m_dZoom, dOldZoom);
	::OutputDebugString(a);
#endif

	// because we've increased/decreased to the maximum zoom allowed,
	// only actually perform the zoom if the old and new dimensions differ
	// the float arithmetic doesn't always come up with the same answer, but the new sizes can be directly compared
	if (nNewXSize == nOldXSize && nNewYSize == nOldYSize) {
		// because there's rounding errors, it is possible to get stuck zooming out from fractional zoom,
		// due to the new/old size being the same, but the zoom factor still changing
		// hence, only reset the zoom value IF when zooming out (aka, it is getting smaller)
		//
		// when zooming in, do not set the old zoom value so that it can "escape" the initial few steps where
		// the zoom ratio has changed, but the size is still the same, due to rounding errors
		if (bExponent && dValue < 0) {
			// zooming out
			// NOTE: this gets triggered on pauseAtZoom
			m_dZoom = dOldZoom;  // restore previous zoom value
		}
		// zooming in does not require restoring the previous zoom value
		// because the code which checks the 65535 will re-scale the zoom factor to ensure it never exceeds 65535

		return false; // then do nothing
	}


	if (bZoomToMouse) {
		// zoom to mouse
		int nCenterX = m_bZoomMode ? m_nCapturedX : m_nMouseX;
		int nCenterY = m_bZoomMode ? m_nCapturedY : m_nMouseY;
		int nOldX = nOldXSize/2 - m_clientRect.Width()/2 + nCenterX - m_offsets.x;
		int nOldY = nOldYSize/2 - m_clientRect.Height()/2 + nCenterY - m_offsets.y;
		double dFac = m_dZoom/dOldZoom;
		m_offsets.x = Helpers::RoundToInt(nNewXSize/2 - m_clientRect.Width()/2 + nCenterX - nOldX*dFac);
		m_offsets.y = Helpers::RoundToInt(nNewYSize/2 - m_clientRect.Height()/2 + nCenterY - nOldY*dFac);
	} else {
		// zoom to center
		m_offsets.x = (int) (m_offsets.x*m_dZoom/dOldZoom);
		m_offsets.y = (int) (m_offsets.y*m_dZoom/dOldZoom);
	}

	m_bInZooming = true;
	StartLowQTimer(ZOOM_TIMEOUT);
	if (fabs(dOldZoom - m_dZoom) > 0.0001 || m_bZoomMode) {
		this->Invalidate(FALSE);
		InvalidateHelpDlg();
		if (bAdjustWindowToImage) {
			AdjustWindowToImage(false);
		}
	}

	return true;
}

bool CMainDlg::PerformPan(int dx, int dy, bool bAbsolute) {
	if ((m_virtualImageSize.cx > 0 && m_virtualImageSize.cx > m_clientRect.Width()) ||
		(m_virtualImageSize.cy > 0 && m_virtualImageSize.cy > m_clientRect.Height())) {
		if (bAbsolute) {
			m_offsets = CPoint(dx, dy);
		} else {
			m_offsets = CPoint(m_offsets.x + dx, m_offsets.y + dy);
		}
		m_bUserPan = true;

		this->Invalidate(FALSE);
		return true;
	}
	return false;
}

void CMainDlg::ZoomToSelection() {
	CRect zoomRect(m_pCropCtl->GetImageCropRect(false));
	if (zoomRect.Width() > 0 && zoomRect.Height() > 0 && m_pCurrentImage != NULL) {
		float fZoom;
		CPoint offsets;
		Helpers::GetZoomParameters(fZoom, offsets, m_pCurrentImage->OrigSize(), m_clientRect.Size(), zoomRect);
		if (fZoom > 0) {
			// if PerformZoom returns false, the zoom rect was invalid, don't move the offsets
			if (PerformZoom(fZoom, false, m_bMouseOn, false)) {
				m_offsets = offsets;
				m_bUserPan = true;
			}
		}
	}
}

double CMainDlg::GetZoomFactorForFitToScreen(bool bFillWithCrop, bool bAllowEnlarge) {
	if (m_pCurrentImage != NULL) {
		double dZoom;
		Helpers::GetImageRect(m_pCurrentImage->OrigWidth(), m_pCurrentImage->OrigHeight(), 
			m_clientRect.Width(), m_clientRect.Height(), true, bFillWithCrop, false, dZoom);
		double dZoomMax = bAllowEnlarge ? Helpers::ZoomMax : max(1.0, m_dZoomAtResizeStart);
		return max(0.0001, min(dZoomMax, dZoom));
	} else {
		return 1.0;
	}
}

void CMainDlg::ResetZoomToFitScreen(bool bFillWithCrop, bool bAllowEnlarge, bool bAdjustWindowSize) {
	m_isUserFitToScreen = false;
	if (m_pCurrentImage != NULL) {
		if (bAdjustWindowSize && !m_bFullScreenMode && !IsZoomed() && m_bAutoFitWndToImage) {
			m_dZoom = bAllowEnlarge ? Helpers::ZoomMax : 1;
			CRect wndRect = Helpers::GetWindowRectMatchingImageSize(m_hWnd, CSize(MIN_WND_WIDTH, MIN_WND_HEIGHT), HUGE_SIZE, m_dZoom, m_pCurrentImage, false, true, m_bWindowBorderless);
			if (m_dZoom <= 1) {
				m_dZoom = -1;
			}
			m_bResizeForNewImage = true;
			this->SetWindowPos(HWND_TOP, wndRect.left, wndRect.top, wndRect.Width(), wndRect.Height(), SWP_NOZORDER | SWP_NOCOPYBITS);
			this->GetClientRect(&m_clientRect);
			m_bResizeForNewImage = false;
			this->Invalidate(FALSE);
		} else {
			double dOldZoom = m_dZoom;
			m_dZoom = GetZoomFactorForFitToScreen(bFillWithCrop, bAllowEnlarge);
			if (bAllowEnlarge && !IsAdjustWindowToImage()) {
				m_isUserFitToScreen = true;
				m_dZoom = -1;
				m_autoZoomFitToScreen = bFillWithCrop ? Helpers::ZM_FillScreen : Helpers::ZM_FitToScreen;
			}
			m_bUserZoom = m_dZoom > 1.0;
			m_bUserPan = false;
			if (fabs(dOldZoom - m_dZoom) > 0.01) {
				this->Invalidate(FALSE);
			}
		}
	}
}

void CMainDlg::ResetZoomTo100Percents(bool bZoomToMouse) {
	if (m_pCurrentImage != NULL && fabs(m_dZoom - 1) > 0.01) {
		// the current design (unless changed) cursor always shows in windowed mode, so always zoom to cursor when not fullscreen
		PerformZoom(1.0, false, bZoomToMouse || !m_bFullScreenMode, true);
	}
}

CProcessParams CMainDlg::CreateProcessParams(bool bNoProcessingAfterLoad) {
	int nClientWidth = m_clientRect.Width();
	int nClientHeight = m_clientRect.Height();
	if (nClientWidth == 0 && nClientHeight == 0) {
		CRect rect = CMultiMonitorSupport::GetMonitorRect(m_hWnd);
		nClientWidth = rect.Width();
		nClientHeight = rect.Height();
	}
	Helpers::EAutoZoomMode eAutoZoomMode = GetAutoZoomMode();
	if (IsAdjustWindowToImage() && !bNoProcessingAfterLoad) {
		CSize maxClientSize = Helpers::GetMaxClientSize(m_hWnd);
		nClientWidth = maxClientSize.cx;
		nClientHeight = maxClientSize.cy;
		eAutoZoomMode = Helpers::ZM_FitToScreenNoZoom;
	}
	if (m_bKeepParams) {
		double dOldLightenShadows = m_pImageProcParamsKept->LightenShadows;
		*m_pImageProcParamsKept = *m_pImageProcParams;
		// when last image was detected as sunset or nightshot, do not take this value for next image
		if (m_bCurrentImageIsSpecialProcessing && m_pImageProcParams->LightenShadows == m_dCurrentInitialLightenShadows) {
			m_pImageProcParamsKept->LightenShadows = dOldLightenShadows;
		}
		m_eProcessingFlagsKept = CreateProcessingFlags(m_bHQResampling, m_bAutoContrast, false, m_bLDC, m_bKeepParams, m_bLandscapeMode);
		m_dZoomKept = (m_bUserZoom || IsAdjustWindowToImage()) ? m_dZoom : -1;
		m_offsetKept = m_bUserPan ? m_offsets : CPoint(0, 0);
		if (m_isUserFitToScreen && !IsAdjustWindowToImage()) { eAutoZoomMode = m_autoZoomFitToScreen; }

		return CProcessParams(nClientWidth, nClientHeight,
			CMultiMonitorSupport::GetMonitorRect(m_hWnd).Size(),
			CRotationParams(0),
			m_nUserRotation,
			m_dZoomKept,
			eAutoZoomMode,
			m_offsetKept,
			_SetLandscapeModeParams(m_bLandscapeMode, *m_pImageProcParamsKept), 
			SetProcessingFlag(_SetLandscapeModeFlags(m_eProcessingFlagsKept), PFLAG_NoProcessingAfterLoad, bNoProcessingAfterLoad));
	} else {
		m_isUserFitToScreen = false;
		CSettingsProvider& sp = CSettingsProvider::This();
		return CProcessParams(nClientWidth, nClientHeight, 
			CMultiMonitorSupport::GetMonitorRect(m_hWnd).Size(),
			CRotationParams(0), 0, -1, eAutoZoomMode, CPoint(0, 0),
			_SetLandscapeModeParams(m_bLandscapeMode, GetDefaultProcessingParams()),
			SetProcessingFlag(_SetLandscapeModeFlags(GetDefaultProcessingFlags(m_bLandscapeMode)), PFLAG_NoProcessingAfterLoad, bNoProcessingAfterLoad));
	}
}

void CMainDlg::ResetParamsToDefault() {
	CSettingsProvider& sp = CSettingsProvider::This();
	m_nRotation = 0;
	m_nUserRotation = 0;
	m_dZoom = m_dZoomMult = -1.0;
	m_offsets = CPoint(0, 0);
	m_bUserZoom = false;
	m_bUserPan = false;
	*m_pImageProcParams = GetDefaultProcessingParams();
	InitFromProcessingFlags(GetDefaultProcessingFlags(m_bLandscapeMode), m_bHQResampling, m_bAutoContrast, m_bAutoContrastSection, m_bLDC, m_bLandscapeMode);
}

void CMainDlg::StartSlideShowTimer(int nMilliSeconds) {
	m_nCurrentTimeout = nMilliSeconds;
	::SetTimer(this->m_hWnd, SLIDESHOW_TIMER_EVENT_ID, nMilliSeconds, NULL);
	m_pNavPanelCtl->EndNavPanelAnimation();
	m_nLastSlideShowImageTickCount = ::GetTickCount();
}

void CMainDlg::StopSlideShowTimer(void) {
	if (m_nCurrentTimeout > 0) {
		m_nCurrentTimeout = 0;
		::KillTimer(this->m_hWnd, SLIDESHOW_TIMER_EVENT_ID);
	}
}

void CMainDlg::StartMovieMode(double dFPS) {
	// if more than this number of frames are requested per seconds, it is considered to be a movie
	const double cdFPSMovie = 4.9;

	m_dMovieFPS = dFPS;

	// Save processing flags at the time movie mode starts
	if (!m_bMovieMode) {
		m_eProcFlagsBeforeMovie = CreateDefaultProcessingFlags(m_bKeepParams);
	}
	// Keep parameters during movie mode
	if (!m_bKeepParams && dFPS > cdFPSMovie) {
		ExecuteCommand(IDM_KEEP_PARAMETERS);
	}
	// Turn off high quality resamping and auto corrections when requested to play many frames per second
	if (dFPS > cdFPSMovie) {
		m_bProcFlagsTouched = true;
		m_bHQResampling = false;
		m_bAutoContrastSection = false;
		m_bAutoContrast = false;
		m_bLDC = false;
		m_bLandscapeMode = false;
	}
	m_bMovieMode = true;
	StartSlideShowTimer(Helpers::RoundToInt(1000.0/dFPS));
	AfterNewImageLoaded(false, false, false);
	Invalidate(FALSE);
}

void CMainDlg::StopMovieMode() {
	if (m_bMovieMode) {
		// undo changes done on processing parameters due to movie mode
		if (!GetProcessingFlag(m_eProcFlagsBeforeMovie, PFLAG_KeepParams)) {
			if (m_bKeepParams) {
				ExecuteCommand(IDM_KEEP_PARAMETERS);
			}
		}
		if (m_bProcFlagsTouched) {
			if (GetProcessingFlag(m_eProcFlagsBeforeMovie, PFLAG_AutoContrast)) {
				m_bAutoContrast = true;
			}
			if (GetProcessingFlag(m_eProcFlagsBeforeMovie, PFLAG_LDC)) {
				m_bLDC = true;
			}
			if (GetProcessingFlag(m_eProcFlagsBeforeMovie, PFLAG_HighQualityResampling)) {
				m_bHQResampling = true;
			}
			if (GetProcessingFlag(m_eProcFlagsBeforeMovie, PFLAG_LandscapeMode)) {
				m_bLandscapeMode = true;
			}
		}
		m_bMovieMode = false;
		m_bProcFlagsTouched = false;
		StopSlideShowTimer();
		AfterNewImageLoaded(false, false, false);
		this->Invalidate(FALSE);
	}
}

void CMainDlg::StartLowQTimer(int nTimeout) {
	m_bTemporaryLowQ = true;
	::KillTimer(this->m_hWnd, ZOOM_TIMER_EVENT_ID);
	::SetTimer(this->m_hWnd, ZOOM_TIMER_EVENT_ID, nTimeout, NULL);
}

void CMainDlg::MouseOff() {
	if (m_bMouseOn) {
		if (m_nMouseY < m_clientRect.bottom - m_pImageProcPanelCtl->PanelRect().Height() && 
			!m_bInTrackPopupMenu && !m_pNavPanelCtl->PanelRect().PtInRect(CPoint(m_nMouseX, m_nMouseY))) {
			if (m_bFullScreenMode) {
				// cursor only hides when in full screen mode
				while (::ShowCursor(FALSE) >= 0);
			}
			m_startMouse.x = m_startMouse.y = -1;
			m_bMouseOn = false;
		}
		this->InvalidateRect(m_pNavPanelCtl->PanelRect(), FALSE);
	}
}

void CMainDlg::MouseOn() {
	if (!m_bMouseOn) {
		::ShowCursor(TRUE);
		m_bMouseOn = true;
		if (m_pNavPanelCtl != NULL) { // can be called very early
			this->InvalidateRect(m_pNavPanelCtl->PanelRect(), FALSE);
		}
	}
}

void CMainDlg::InitParametersForNewImage() {
	if (!m_bKeepParams) {
		ResetParamsToDefault();
	} else if (!(m_bUserZoom || IsAdjustWindowToImage())) {
		m_dZoom = -1;
	}

	m_bAutoContrastSection = false;
	*m_pImageProcParams2 = GetNoProcessingParams();
	m_eProcessingFlags2 = PFLAG_HighQualityResampling;

	if (!m_bIsAnimationPlaying) {
		m_bInZooming = m_bTemporaryLowQ = m_bShowZoomFactor = false;
	}
}

void CMainDlg::ExchangeProcessingParams() {
	bool bOldAutoContrastSection = m_bAutoContrastSection;
	CImageProcessingParams tempParams = *m_pImageProcParams;
	EProcessingFlags tempFlags = CreateDefaultProcessingFlags();
	*m_pImageProcParams = *m_pImageProcParams2;
	InitFromProcessingFlags(m_eProcessingFlags2, m_bHQResampling, m_bAutoContrast, m_bAutoContrastSection, m_bLDC, m_bLandscapeMode);
	*m_pImageProcParams2 = tempParams;
	m_eProcessingFlags2 = tempFlags;
	m_bAutoContrastSection = bOldAutoContrastSection; // this flag cannot be exchanged this way
	this->Invalidate(FALSE);
}

void CMainDlg::AfterNewImageLoaded(bool bSynchronize, bool bAfterStartup, bool noAdjustWindow) {
	m_pAnnotations->Clear(); // drawings belong to the image they were made on
	ClearTransparency(false);
	UpdateWindowTitle();
	InvalidateHelpDlg();
	m_pDirectoryWatcher->SetCurrentFile(CurrentFileName(false));
	if (!m_bIsAnimationPlaying) m_pNavPanelCtl->HideNavPanelTemporary();
	m_pPanelMgr->AfterNewImageLoaded();
	m_pCropCtl->AbortCropping();
	if (m_pCurrentImage != NULL) m_pCropCtl->SetImageSize(m_pCurrentImage->OrigSize()); // inform CropCtl of the image size for CropImageAR
	m_pPrintImage->ClearOffsets();
	if (bSynchronize) {
		// after loading an image, the per image processing parameters must be synchronized with
		// the current processing parameters
		bool bLastWasSpecialProcessing = m_bCurrentImageIsSpecialProcessing;
		bool bLastWasInParamDB = m_bCurrentImageInParamDB;
		m_bCurrentImageInParamDB = false;
		m_bCurrentImageIsSpecialProcessing = false;
		if (m_pCurrentImage != NULL) {
			m_dCurrentInitialLightenShadows = m_pCurrentImage->GetInitialProcessParams().LightenShadows;
			m_bCurrentImageInParamDB = m_pCurrentImage->IsInParamDB();
			m_bCurrentImageIsSpecialProcessing = m_pCurrentImage->GetLightenShadowFactor() != 1.0f;
			if (!m_bKeepParams) {
				m_bHQResampling = GetProcessingFlag(m_pCurrentImage->GetInitialProcessFlags(), PFLAG_HighQualityResampling);
				m_bAutoContrast = GetProcessingFlag(m_pCurrentImage->GetInitialProcessFlags(), PFLAG_AutoContrast);
				m_bLDC = GetProcessingFlag(m_pCurrentImage->GetInitialProcessFlags(), PFLAG_LDC);

				m_nRotation = m_pCurrentImage->GetInitialRotation();
				m_dZoom = m_pCurrentImage->GetInititialZoom();
				m_offsets = m_pCurrentImage->GetInitialOffsets();

				if (m_pCurrentImage->HasZoomStoredInParamDB()) {
					m_bUserZoom = m_bUserPan = true;
				}

				*m_pImageProcParams = m_pCurrentImage->GetInitialProcessParams();
			} else if (m_bCurrentImageIsSpecialProcessing && m_bKeepParams) {
				// set this factor, no matter if we keep parameters
				m_pImageProcParams->LightenShadows = m_pCurrentImage->GetInitialProcessParams().LightenShadows;
			} else if (bLastWasSpecialProcessing && m_bKeepParams) {
				// take kept value when last was special processing as the special processing value is not usable
				m_pImageProcParams->LightenShadows = m_pImageProcParamsKept->LightenShadows;
			}
			if (m_bKeepParams) {
				m_nRotation = m_pCurrentImage->GetInitialRotation() + m_nUserRotation;
			}
		}
		if (!bAfterStartup && !m_bIsAnimationPlaying && !noAdjustWindow) {
			AdjustWindowToImage(false);
		}
	}
}

bool CMainDlg::IsAdjustWindowToImage() {
	return !m_bFullScreenMode && !::IsZoomed(m_hWnd) && m_bAutoFitWndToImage;
}

bool CMainDlg::IsImageExactlyFittingWindow() {
	return (m_pCurrentImage != NULL) && m_pCurrentImage->DIBWidth() == m_clientRect.Width() && m_pCurrentImage->DIBHeight() == m_clientRect.Height();
}

void CMainDlg::AdjustWindowToImage(bool bAfterStartup) {
	if (IsAdjustWindowToImage() && (m_pCurrentImage != NULL || bAfterStartup)) {
		// window size shall be adjusted to image size (at least keep aspect ratio)
		double dZoom = m_dZoom;
		CRect windowRect = Helpers::GetWindowRectMatchingImageSize(m_hWnd, CSize(MIN_WND_WIDTH, MIN_WND_HEIGHT), HUGE_SIZE, dZoom, m_pCurrentImage, bAfterStartup, dZoom < 0, m_bWindowBorderless);
		CRect defaultRect = CMultiMonitorSupport::GetDefaultWindowRect();
		if (bAfterStartup && CSettingsProvider::This().ExplicitWindowRect()) {
			windowRect = CRect(defaultRect.TopLeft(), windowRect.Size());
		}
		m_bResizeForNewImage = true;
		this->Invalidate(FALSE);
		this->SetWindowPos(HWND_TOP, windowRect.left, windowRect.top, windowRect.Width(), windowRect.Height(), SWP_NOZORDER | SWP_NOCOPYBITS);
		this->GetClientRect(&m_clientRect);
		m_bResizeForNewImage = false;
	}
}

void CMainDlg::CheckIfApplyAutoFitWndToImage(bool bInInitDialog) {
	if (CSettingsProvider::This().AutoFullScreen()) {
		if (!Helpers::CanDisplayImageWithoutResize(m_hWnd, m_pCurrentImage)) {
			if (!bInInitDialog) {
				ExecuteCommand(IDM_FULL_SCREEN_MODE);
			}
			m_bFullScreenMode = true;
			m_bTemporaryLowQ = false;
			MouseOff();
		} else {
			m_bAutoFitWndToImage = true;
		}
	}
}

void CMainDlg::SaveParameters() {
	if (m_bMovieMode) {
		return;
	}

	EProcessingFlags eFlags = CreateDefaultProcessingFlags(m_bKeepParams);
	CString sText = HelpersGUI::GetINIFileSaveConfirmationText(*m_pImageProcParams, eFlags,
		m_pFileList->GetNavigationMode(), m_pFileList->GetSorting(), m_pFileList->IsSortedAscending(), GetAutoZoomMode(), m_pNavPanelCtl->IsActive(),
		m_bShowFileName, m_pEXIFDisplayCtl->IsActive(), m_eTransitionEffect);

	if (IDYES == this->MessageBox(sText, CNLS::GetString(_T("Confirm save default parameters")), MB_YESNO | MB_ICONQUESTION)) {
		CSettingsProvider::This().SaveSettings(*m_pImageProcParams, eFlags, m_pFileList->GetNavigationMode(), m_pFileList->GetSorting(), m_pFileList->IsSortedAscending(),
			m_eAutoZoomModeWindowed, m_eAutoZoomModeFullscreen, m_pNavPanelCtl->IsActive(), m_bShowFileName, m_pEXIFDisplayCtl->IsActive(), m_eTransitionEffect);
	}
}

CRect CMainDlg::ScreenToDIB(const CSize& sizeDIB, const CRect& rect) {
	int nOffsetX = (sizeDIB.cx - m_clientRect.Width())/2;
	int nOffsetY = (sizeDIB.cy - m_clientRect.Height())/2;

	CRect rectDIB = CRect(rect.left + nOffsetX, rect.top + nOffsetY, rect.right + nOffsetX, rect.bottom + nOffsetY);
	
	CRect rectClipped;
	rectClipped.IntersectRect(rectDIB, CRect(0, 0, sizeDIB.cx, sizeDIB.cy));
	return rectClipped;
}

bool CMainDlg::ScreenToImage(float & fX, float & fY) {
	if (m_pCurrentImage == NULL) {
		return false;
	}
	int nOffsetX = (m_pCurrentImage->DIBWidth() - m_clientRect.Width())/2;
	int nOffsetY = (m_pCurrentImage->DIBHeight() - m_clientRect.Height())/2;

	fX += nOffsetX;
	fY += nOffsetY;

	m_pCurrentImage->DIBToOrig(fX, fY);

	return true;
}

bool CMainDlg::ImageToScreen(float & fX, float & fY) {
	if (m_pCurrentImage == NULL) {
		return false;
	}
	m_pCurrentImage->OrigToDIB(fX, fY);

	int nOffsetX = (m_pCurrentImage->DIBWidth() - m_clientRect.Width())/2;
	int nOffsetY = (m_pCurrentImage->DIBHeight() - m_clientRect.Height())/2;

	fX -= nOffsetX;
	fY -= nOffsetY;

	return true;
}

/// <summary>
/// Get current filename/filepath
/// </summary>
/// <param name="bFileTitle">If true, returns only the filename part.  If false, returns complete filepath</param>
/// <returns>Returns the filename either just the title or full filepath</returns>
LPCTSTR CMainDlg::CurrentFileName(bool bFileTitle) {
	if (m_pCurrentImage != NULL && m_pCurrentImage->IsClipboardImage()) {
		return CNLS::GetString(_T("Clipboard Image"));
	}

	if (m_pFileList != NULL) {
		return bFileTitle ? m_pFileList->CurrentFileTitle() : m_pFileList->Current();
	} else {
		return NULL;
	}
}

void CMainDlg::SetCursorForMoveSection() {
	if (!m_pCropCtl->IsCropping()) {
		if (m_pZoomNavigatorCtl->IsPointInZoomNavigatorThumbnail(CPoint(m_nMouseX, m_nMouseY)) || m_bDragging) {
			::SetCursor(::LoadCursor(NULL, IDC_SIZEALL));
			m_bPanMouseCursorSet = true;
		} else if (m_bPanMouseCursorSet) {
			::SetCursor(::LoadCursor(NULL, IDC_ARROW));
			m_bPanMouseCursorSet = false;
		}
	}
}

void CMainDlg::ToggleMonitor() {
	if (CMultiMonitorSupport::IsMultiMonitorSystem() && !m_bSpanVirtualDesktop) {
		int nMaxMonitorIdx = ::GetSystemMetrics(SM_CMONITORS);
		if (m_nMonitor == -1) {
			// index unknown, search
			for (int i = 0; i < nMaxMonitorIdx; i++) {
				if (m_monitorRect == CMultiMonitorSupport::GetMonitorRect(i)) {
					m_nMonitor = i;
					break;
				}
			}
		}
		m_dZoom = -1.0;
		this->Invalidate(FALSE);
		m_nMonitor = (m_nMonitor + 1) % nMaxMonitorIdx;
		m_monitorRect = CMultiMonitorSupport::GetMonitorRect(m_nMonitor);
		SetWindowPos(HWND_TOP, &m_monitorRect, SWP_NOZORDER);
		this->GetClientRect(&m_clientRect);
	}
}

CRect CMainDlg::GetZoomTextRect(CRect imageProcessingArea) {
	int nZoomTextRectBottomOffset = (m_clientRect.Width() < HelpersGUI::ScaleToScreen(800)) ? 25 : ZOOM_TEXT_RECT_OFFSET;
	int nStartX = imageProcessingArea.right - HelpersGUI::ScaleToScreen(ZOOM_TEXT_RECT_WIDTH + ZOOM_TEXT_RECT_OFFSET);
	if (m_pImageProcPanelCtl->IsVisible()) {
		nStartX = max(nStartX, m_pImageProcPanelCtl->GetUnsharpMaskButtonRect().right);
	}
	int nEndX = nStartX + HelpersGUI::ScaleToScreen(ZOOM_TEXT_RECT_WIDTH);
	if (m_pImageProcPanelCtl->IsVisible()) {
		nEndX = min(nEndX, imageProcessingArea.right - 2);
	}
	return CRect(nStartX, 
		imageProcessingArea.bottom - HelpersGUI::ScaleToScreen(ZOOM_TEXT_RECT_HEIGHT + nZoomTextRectBottomOffset), 
		nEndX, imageProcessingArea.bottom - HelpersGUI::ScaleToScreen(nZoomTextRectBottomOffset));
}

void CMainDlg::EditINIFile(bool bGlobalINI) {
	LPCTSTR sINIFileName = bGlobalINI ? CSettingsProvider::This().GetGlobalINIFileName() : CSettingsProvider::This().GetUserINIFileName();
	if (!bGlobalINI) {
		if (!CSettingsProvider::This().ExistsUserINI()) {
			// No user INI file, ask if global INI shall be copied
			if (IDYES == ::MessageBox(m_hWnd, CNLS::GetString(_T("No user INI file exists yet. Create user INI file from INI file template?")), _T(JPEGVIEW_TITLE), MB_YESNO | MB_ICONQUESTION)) {
				CSettingsProvider::This().CopyUserINIFromTemplate();
			} else {
				return;
			}
		}
	}

	Helpers::EIniEditor iniEditor = CSettingsProvider::This().IniEditor();
	CString command;
	LPCTSTR argument;
	if (iniEditor == Helpers::INI_Notepad) {
		command = _T("notepad.exe");
		argument = sINIFileName;
	} else if (iniEditor == Helpers::INI_System) {
		command = sINIFileName;
		argument = NULL;
	} else {
		command = CSettingsProvider::This().CustomIniEditor();
		command.Replace(_T("%exepath%"), CSettingsProvider::This().GetEXEPath());	
		argument = sINIFileName;
	}
	::ShellExecute(m_hWnd, _T("open"), command, argument, NULL, SW_SHOW);
}

void CMainDlg::UpdateWindowTitle() {
	bool bShowFullPathInTitle  = CSettingsProvider::This().ShowFullPathInTitle();
	LPCTSTR sCurrentFileName = CurrentFileName(!bShowFullPathInTitle);

	if (sCurrentFileName == NULL || m_pCurrentImage == NULL) {
		this->SetWindowText(_T(JPEGVIEW_TITLE));
	} else {
		CString sWindowText =  sCurrentFileName;
		sWindowText += Helpers::GetMultiframeIndex(m_pCurrentImage);
		if (CSettingsProvider::This().ShowEXIFDateInTitle()) {
			CEXIFReader* pEXIF = m_pCurrentImage->GetEXIFReader();
			CRawMetadata* pRawMetadata = m_pCurrentImage->GetRawMetadata();
			if (pEXIF != NULL && pEXIF->GetAcquisitionTime().wYear > 1600) {
				sWindowText += " - " + Helpers::SystemTimeToString(pEXIF->GetAcquisitionTime());
			} else if (pRawMetadata != NULL && pRawMetadata->GetAcquisitionTime().wYear > 1985) {
				sWindowText += " - " + Helpers::SystemTimeToString(pRawMetadata->GetAcquisitionTime());
			}
		}
		sWindowText += " - " + CString(JPEGVIEW_TITLE);
		this->SetWindowText(sWindowText);
	}
}

bool CMainDlg::PrepareForModalPanel() {
	m_pImageProcPanelCtl->SetVisible(false);
	bool bOldShowNavPanel = m_pNavPanelCtl->IsActive();
	m_pNavPanelCtl->SetActive(false);
	StopSlideShowTimer();
	StopMovieMode();
	StopAnimation();
	return bOldShowNavPanel;
}

int CMainDlg::TrackPopupMenu(CPoint pos, HMENU hMenu) {
	m_bInTrackPopupMenu = true;
	int nMenuCmd = ::TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD,
		pos.x, pos.y, 0, this->m_hWnd, NULL);
	m_bInTrackPopupMenu = false;
	return nMenuCmd;
}

///////////////////////////////////////////////////////////////////////////////////
// Menu bar, toolbar and conversion
///////////////////////////////////////////////////////////////////////////////////

bool CMainDlg::IsToolbarShown() { return m_pChrome != NULL && m_pChrome->IsToolbarShown(); }
bool CMainDlg::IsStatusBarShown() { return m_pChrome != NULL && m_pChrome->IsStatusBarShown(); }
bool CMainDlg::IsSidePanelShown() { return m_pChrome != NULL && m_pChrome->IsSidePanelShown(); }

// Entries of the menu bar menus: a command is copied from the context menu, MENUBAR_SUBMENU(cmd) copies the
// sub menu of the context menu containing the command, negative values are entries that are not in the context menu
#define MENUBAR_SUBMENU(cmd) ((cmd) | 0x40000000)
enum {
	MBAR_SEPARATOR = -1, MBAR_CONVERT = -2, MBAR_TOOLBAR = -3, MBAR_STATUSBAR = -4, MBAR_SIDEPANEL = -5,
	MBAR_EDITPANEL = -6, MBAR_EXPLORER_MENU = -7, MBAR_FIRST_IMAGE = -8, MBAR_LAST_IMAGE = -9, MBAR_HELP = -10,
	MBAR_COMPARE = -11, MBAR_RESET = -12, MBAR_UNSHARP = -13, MBAR_COLLAPSE = -14, MBAR_RESIZE = -15, MBAR_SETTINGS = -16, MBAR_OCR = -17, MBAR_SHARE = -18, MBAR_EXPORT = -19, MBAR_FILMSTRIP = -20, MBAR_SIMILAR = -21, MBAR_REMOVE_BACKGROUND = -22
};

static const int s_menuBarFile[] = { IDM_OPEN, MENUBAR_SUBMENU(IDM_FIRST_OPENWITH_CMD), IDM_SAVE, MBAR_EXPORT, MBAR_CONVERT, MBAR_SHARE, IDM_RELOAD, MBAR_SEPARATOR,
	IDM_EXPLORE, IDM_COPY_PATH, IDM_PRINT, IDM_BATCH_COPY, MENUBAR_SUBMENU(IDM_TOUCH_IMAGE), MENUBAR_SUBMENU(IDM_SET_WALLPAPER_ORIG), MBAR_SEPARATOR, IDM_EXIT };
static const int s_menuBarEdit[] = { IDM_COPY, IDM_COPY_FULL, IDM_PASTE, MBAR_SEPARATOR, MENUBAR_SUBMENU(IDM_ROTATE_90), MENUBAR_SUBMENU(IDM_ROTATE_90_LOSSLESS),
	MBAR_EDITPANEL, MBAR_COMPARE, MBAR_RESET, MBAR_UNSHARP, MBAR_RESIZE, MBAR_OCR, MBAR_REMOVE_BACKGROUND, MBAR_SEPARATOR, IDM_AUTO_CORRECTION, IDM_LDC, IDM_KEEP_PARAMETERS, MBAR_SEPARATOR, IDM_SAVE_PARAM_DB, IDM_CLEAR_PARAM_DB };
static const int s_menuBarView[] = { MBAR_TOOLBAR, MBAR_STATUSBAR, MBAR_FILMSTRIP, MBAR_SIDEPANEL, MBAR_COLLAPSE, MBAR_SEPARATOR, IDM_SHOW_FILEINFO, IDM_SHOW_FILENAME, IDM_SHOW_NAVPANEL,
	MBAR_SEPARATOR, MENUBAR_SUBMENU(IDM_FIT_TO_SCREEN), MENUBAR_SUBMENU(IDM_AUTO_ZOOM_FIT_NO_ZOOM) };
static const int s_menuBarNavigate[] = { IDM_NEXT, IDM_PREV, MBAR_FIRST_IMAGE, MBAR_LAST_IMAGE, MBAR_SEPARATOR, MENUBAR_SUBMENU(IDM_LOOP_FOLDER),
	MENUBAR_SUBMENU(IDM_SORT_MOD_DATE), MENUBAR_SUBMENU(IDM_SLIDESHOW_START), IDM_STOP_MOVIE };
static const int s_menuBarTools[] = { MBAR_CONVERT, MBAR_EXPLORER_MENU, MBAR_SEPARATOR, MBAR_SIMILAR, MBAR_SETTINGS, MBAR_SEPARATOR, MENUBAR_SUBMENU(IDM_EDIT_USER_CONFIG), MENUBAR_SUBMENU(IDM_FIRST_USER_CMD) };
static const int s_menuBarHelp[] = { MBAR_HELP, IDM_ABOUT };

// Position of the (non popup) item with the given command, -1 if not found
static int FindMenuCommandPos(HMENU hMenu, UINT nCommand) {
	for (int i = 0; i < ::GetMenuItemCount(hMenu); i++) {
		if (::GetSubMenu(hMenu, i) == NULL && ::GetMenuItemID(hMenu, i) == nCommand) {
			return i;
		}
	}
	return -1;
}

// Position of the popup item whose sub menu directly contains the given command, -1 if not found
static int FindSubMenuPos(HMENU hMenu, UINT nCommand) {
	for (int i = 0; i < ::GetMenuItemCount(hMenu); i++) {
		HMENU hSubMenu = ::GetSubMenu(hMenu, i);
		if (hSubMenu != NULL && FindMenuCommandPos(hSubMenu, nCommand) >= 0) {
			return i;
		}
	}
	return -1;
}

// Appends a deep copy of the item at position nPos of hSource to hTarget
static void AppendMenuItemCopy(HMENU hSource, int nPos, HMENU hTarget) {
	TCHAR buffer[256];
	MENUITEMINFO info{ 0 };
	info.cbSize = sizeof(info);
	info.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID | MIIM_SUBMENU | MIIM_STRING;
	info.dwTypeData = buffer;
	info.cch = 255;
	if (!::GetMenuItemInfo(hSource, nPos, TRUE, &info)) {
		return;
	}
	if (info.hSubMenu != NULL) {
		HMENU hSubMenuCopy = ::CreatePopupMenu();
		for (int i = 0; i < ::GetMenuItemCount(info.hSubMenu); i++) {
			AppendMenuItemCopy(info.hSubMenu, i, hSubMenuCopy);
		}
		info.hSubMenu = hSubMenuCopy;
	}
	info.dwTypeData = buffer;
	::InsertMenuItem(hTarget, ::GetMenuItemCount(hTarget), TRUE, &info);
}

// Appends a menu item with the translated text and the keyboard shortcut of the command
static void AppendMenuString(CKeyMap* pKeyMap, HMENU hMenu, UINT nCommand, LPCTSTR sEnglishText, bool bChecked, bool bEnabled) {
	CString sText = CNLS::GetString(sEnglishText);
	CString sKey = (pKeyMap != NULL) ? pKeyMap->GetKeyStringForCommand(nCommand) : CString();
	if (!sKey.IsEmpty()) {
		sText += _T("	") + sKey;
	}
	::AppendMenu(hMenu, MF_STRING | (bChecked ? MF_CHECKED : 0) | (bEnabled ? 0 : MF_GRAYED), nCommand, sText);
}

// Removes separators at the start, at the end and following each other
static void RemoveSurplusSeparators(HMENU hMenu) {
	bool bLastWasSeparator = true;
	for (int i = 0; i < ::GetMenuItemCount(hMenu); i++) {
		bool bIsSeparator = (::GetMenuState(hMenu, i, MF_BYPOSITION) & MF_SEPARATOR) != 0;
		if (bIsSeparator && bLastWasSeparator) {
			::DeleteMenu(hMenu, i, MF_BYPOSITION);
			i--;
		} else {
			bLastWasSeparator = bIsSeparator;
		}
	}
	int nCount = ::GetMenuItemCount(hMenu);
	if (nCount > 0 && (::GetMenuState(hMenu, nCount - 1, MF_BYPOSITION) & MF_SEPARATOR) != 0) {
		::DeleteMenu(hMenu, nCount - 1, MF_BYPOSITION);
	}
}

HMENU CMainDlg::CreateMenuBarMenu(int nIndex) {
	const int* pEntries;
	int nEntries;
	switch (nIndex) {
		case 0: pEntries = s_menuBarFile; nEntries = sizeof(s_menuBarFile) / sizeof(int); break;
		case 1: pEntries = s_menuBarEdit; nEntries = sizeof(s_menuBarEdit) / sizeof(int); break;
		case 2: pEntries = s_menuBarView; nEntries = sizeof(s_menuBarView) / sizeof(int); break;
		case 3: pEntries = s_menuBarNavigate; nEntries = sizeof(s_menuBarNavigate) / sizeof(int); break;
		case 4: pEntries = s_menuBarTools; nEntries = sizeof(s_menuBarTools) / sizeof(int); break;
		case 5: pEntries = s_menuBarHelp; nEntries = sizeof(s_menuBarHelp) / sizeof(int); break;
		default: return NULL;
	}

	// The context menu has the texts, shortcuts, check marks and enabled states already set up
	HMENU hContextMenuRoot = NULL;
	HMENU hContextMenu = LoadPreparedContextMenu(hContextMenuRoot);
	if (hContextMenu == NULL) {
		return NULL;
	}

	bool bHasFileImage = m_pCurrentImage != NULL && !m_pCurrentImage->IsClipboardImage() && !m_bMovieMode;
	HMENU hMenu = ::CreatePopupMenu();
	for (int i = 0; i < nEntries; i++) {
		int nEntry = pEntries[i];
		switch (nEntry) {
			case MBAR_SEPARATOR:
				::AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				break;
			case MBAR_CONVERT:
				::AppendMenu(hMenu, MF_POPUP | MF_STRING | (bHasFileImage ? 0 : MF_GRAYED),
					(UINT_PTR)Converter::CreatePresetMenu(IDM_FIRST_CONVERT_CMD), CNLS::GetString(_T("Convert to")));
				break;
			case MBAR_TOOLBAR:
				AppendMenuString(m_pKeyMap, hMenu, IDM_TOGGLE_TOOLBAR, _T("Toolbar"), m_pChrome->GetShowToolbar(), true);
				break;
			case MBAR_STATUSBAR:
				AppendMenuString(m_pKeyMap, hMenu, IDM_TOGGLE_STATUSBAR, _T("Status bar"), m_pChrome->GetShowStatusBar(), true);
				break;
			case MBAR_SIDEPANEL:
				AppendMenuString(m_pKeyMap, hMenu, IDM_TOGGLE_SIDEPANEL, _T("Information panel"), m_pChrome->GetShowSidePanel(), true);
				break;
			case MBAR_EDITPANEL:
				AppendMenuString(m_pKeyMap, hMenu, IDM_TOGGLE_EDITPANEL, _T("Image processing panel"),
					m_pChrome->IsEnabled() ? m_pChrome->IsEditTabShown() : m_pImageProcPanelCtl->IsPinned(), m_pCurrentImage != NULL);
				break;
			case MBAR_COMPARE:
				AppendMenuString(m_pKeyMap, hMenu, IDM_COMPARE_BEFORE_AFTER, _T("Compare before and after editing"), m_bCompareMode, m_pCurrentImage != NULL);
				break;
			case MBAR_RESET:
				AppendMenuString(m_pKeyMap, hMenu, IDM_RESET_PROCESSING, _T("Reset all values to the defaults"), false, m_pCurrentImage != NULL);
				break;
			case MBAR_REMOVE_BACKGROUND:
				AppendMenuString(m_pKeyMap, hMenu, IDM_REMOVE_BACKGROUND, _T("Remove background"), false, m_pCurrentImage != NULL);
				break;
			case MBAR_SIMILAR:
				AppendMenuString(m_pKeyMap, hMenu, IDM_FIND_SIMILAR, _T("Find similar photos..."), false, m_pFileList != NULL && m_pFileList->Size() > 1);
				break;
			case MBAR_FILMSTRIP:
				AppendMenuString(m_pKeyMap, hMenu, IDM_TOGGLE_FILMSTRIP, _T("Thumbnail strip"), m_pChrome->GetShowFilmstrip(), m_pChrome->GetShowStatusBar());
				break;
			case MBAR_EXPORT:
				AppendMenuString(m_pKeyMap, hMenu, IDM_EXPORT, _T("Export..."), false, m_pCurrentImage != NULL);
				break;
			case MBAR_SHARE:
				AppendMenuString(m_pKeyMap, hMenu, IDM_SHARE, _T("Share..."), false, bHasFileImage);
				break;
			case MBAR_OCR:
				AppendMenuString(m_pKeyMap, hMenu, IDM_OCR_COPY_TEXT, _T("Copy text from image"), false, m_pCurrentImage != NULL);
				break;
			case MBAR_SETTINGS:
				AppendMenuString(m_pKeyMap, hMenu, IDM_SETTINGS, _T("Settings..."), false, true);
				break;
			case MBAR_RESIZE:
				AppendMenuString(m_pKeyMap, hMenu, IDM_CHANGESIZE, _T("Resize image..."), false, m_pCurrentImage != NULL);
				break;
			case MBAR_UNSHARP:
				AppendMenuString(m_pKeyMap, hMenu, IDM_UNSHARP_MASK, _T("Unsharp mask..."), false, m_pCurrentImage != NULL);
				break;
			case MBAR_COLLAPSE:
				AppendMenuString(m_pKeyMap, hMenu, IDM_TOGGLE_SIDEPANEL_COLLAPSED, _T("Collapse the side panel"),
					m_pChrome->GetShowSidePanel() && m_pChrome->GetSidePanelCollapsed(), m_pChrome->GetShowSidePanel());
				break;
			case MBAR_EXPLORER_MENU:
				AppendMenuString(m_pKeyMap, hMenu, IDM_EXPLORER_CONVERT_MENU, _T("'Convert to' in the Explorer context menu"), Converter::IsExplorerMenuRegistered(), true);
				break;
			case MBAR_FIRST_IMAGE:
				AppendMenuString(m_pKeyMap, hMenu, IDM_FIRST, _T("First image"), false, m_pCurrentImage != NULL);
				break;
			case MBAR_LAST_IMAGE:
				AppendMenuString(m_pKeyMap, hMenu, IDM_LAST, _T("Last image"), false, m_pCurrentImage != NULL);
				break;
			case MBAR_HELP:
				::AppendMenu(hMenu, MF_STRING, IDM_HELP, CString(CNLS::GetString(_T("Help and keyboard shortcuts"))) + _T("\tF1"));
				break;
			default:
				if (nEntry & 0x40000000) {
					int nPos = FindSubMenuPos(hContextMenu, nEntry & ~0x40000000);
					if (nPos >= 0) AppendMenuItemCopy(hContextMenu, nPos, hMenu);
				} else {
					int nPos = FindMenuCommandPos(hContextMenu, nEntry);
					if (nPos >= 0) AppendMenuItemCopy(hContextMenu, nPos, hMenu);
				}
				break;
		}
	}
	::DestroyMenu(hContextMenuRoot);
	RemoveSurplusSeparators(hMenu);
	return hMenu;
}

void CMainDlg::ShowConvertMenuAndExecute() {
	if (m_pCurrentImage == NULL || m_pCurrentImage->IsClipboardImage() || m_bMovieMode) {
		return;
	}
	MouseOn();
	int nCommand = m_pChrome->TrackConvertMenu();
	if (nCommand != 0) {
		ExecuteCommand(nCommand);
	}
}

namespace {
	struct CConversionJob {
		HWND TargetWindow;
		CString SourceFile;
		int PresetIndex;
		volatile LONG* RunningConversions;
	};

	unsigned __stdcall ConversionThread(void* pArgument) {
		CConversionJob* pJob = (CConversionJob*)pArgument;
		::CoInitialize(NULL);
		CString sOutputFile, sError;
		bool bSuccess = Converter::ConvertFile(pJob->SourceFile, Converter::GetPresets()[pJob->PresetIndex], sOutputFile, sError);
		CString* pResult = new CString(bSuccess ? sOutputFile : (pJob->SourceFile + _T("\n") + sError));
		::InterlockedDecrement(pJob->RunningConversions);
		if (!::PostMessage(pJob->TargetWindow, WM_CONVERSION_FINISHED, bSuccess ? 1 : 0, (LPARAM)pResult)) {
			delete pResult;
		}
		::CoUninitialize();
		delete pJob;
		return 0;
	}
}

void CMainDlg::StartConversion(int nPresetIndex) {
	int nCount = (int)Converter::GetPresets().size();
	if (nPresetIndex < 0 || nPresetIndex >= nCount || m_pCurrentImage == NULL || m_pCurrentImage->IsClipboardImage()) {
		return;
	}
	LPCTSTR sCurrentFile = CurrentFileName(false);
	if (sCurrentFile == NULL) {
		return;
	}
	CConversionJob* pJob = new CConversionJob();
	pJob->TargetWindow = m_hWnd;
	pJob->SourceFile = sCurrentFile;
	pJob->PresetIndex = nPresetIndex;
	pJob->RunningConversions = &m_nRunningConversions;
	::InterlockedIncrement(&m_nRunningConversions);
	m_pChrome->ShowMessage(CString(CNLS::GetString(_T("Converting..."))) + _T(" ") + CurrentFileName(true));
	HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, &ConversionThread, pJob, 0, NULL);
	if (hThread == NULL) {
		::InterlockedDecrement(&m_nRunningConversions);
		delete pJob;
		return;
	}
	::CloseHandle(hThread);
}

LRESULT CMainDlg::OnConversionFinished(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/) {
	CString* pResult = (CString*)lParam;
	if (pResult == NULL) {
		return 0;
	}
	if (wParam == 1) {
		LPCTSTR sFileTitle = _tcsrchr(*pResult, _T('\\'));
		m_pChrome->ShowMessage(CString(CNLS::GetString(_T("Converted:"))) + _T(" ") + ((sFileTitle != NULL) ? sFileTitle + 1 : (LPCTSTR)*pResult));
		if (!IsStatusBarShown()) {
			::MessageBeep(MB_OK);
		}
	} else {
		::MessageBox(m_hWnd, CString(CNLS::GetString(_T("The following files could not be converted:"))) + _T("\n\n") + *pResult,
			_T(JPEGVIEW_TITLE), MB_OK | MB_ICONWARNING);
	}
	delete pResult;
	return 0;
}

int CMainDlg::GetLoadErrorAfterOpenFile() {
	if (m_pCurrentImage == NULL) {
		if (CurrentFileName(false) == NULL) {
			if (m_pFileList->IsSlideShowList()) {
				return HelpersGUI::FileLoad_SlideShowListInvalid;
			}
			return HelpersGUI::FileLoad_NoFilesInDirectory;
		}
		return HelpersGUI::FileLoad_LoadError;
	}
	return HelpersGUI::FileLoad_Ok;
}

void CMainDlg::PrefetchDIB(const CRect& clientRect) {
	if (m_pCurrentImage == NULL) return;

	CSize newSize = Helpers::GetVirtualImageSize(m_pCurrentImage->OrigSize(), clientRect.Size(), GetAutoZoomMode(), m_dZoom);
	CSize clippedSize(min(clientRect.Width(), newSize.cx), min(clientRect.Height(), newSize.cy));
	CPoint offsetsInImage = m_pCurrentImage->ConvertOffset(newSize, clippedSize, Helpers::LimitOffsets(m_offsets, clientRect.Size(), newSize));
	m_pCurrentImage->GetDIB(newSize, clippedSize, offsetsInImage, *m_pImageProcParams, CreateDefaultProcessingFlags());
}

double CMainDlg::GetZoomMultiplier(CJPEGImage* pImage, const CRect& clientRect) {
	double dZoomToFit;
	CSize fittedSize = Helpers::GetImageRect(pImage->OrigWidth(), pImage->OrigHeight(), 
		clientRect.Width(), clientRect.Height(), true, false, false, dZoomToFit);
	// Zoom multiplier (zoom step) should be around 1.1 but reach the value 1.0 after an integral number
	// of zooming steps
	int n = Helpers::RoundToInt(log(1/dZoomToFit)/log(1.1));
	return (n == 0) ? 1.1 : exp(log(1/dZoomToFit)/n);
}

EProcessingFlags CMainDlg::CreateDefaultProcessingFlags(bool bKeepParams) {
	return CreateProcessingFlags(m_bHQResampling, m_bAutoContrast, m_bAutoContrastSection, m_bLDC, bKeepParams, m_bLandscapeMode);
}

bool CMainDlg::HandleMouseButtonByKeymap(int nMouseButtonCode, bool bExecuteCommand) {
	if (m_pPanelMgr->IsModalPanelShown()) {
		return false;
	}
	bool bAlt = (::GetKeyState(VK_MENU) & 0x8000) != 0;
	bool bCtrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
	bool bShift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
	int nCommand = m_pKeyMap->GetCommandIdForKey(nMouseButtonCode, bAlt, bCtrl, bShift);
	if (nCommand > 0) {
		if (bExecuteCommand) {
			ExecuteCommand(nCommand);
		}
		return true;
	}
	return false;
}

bool CMainDlg::UseSlideShowTransitionEffect() {
	return m_bFullScreenMode && m_nCurrentTimeout >= 1000 && m_eTransitionEffect != Helpers::TE_None;
}

void CMainDlg::AnimateTransition() {

	int nFrameTimeMs = (m_eTransitionEffect >= Helpers::TE_RollLR || m_eTransitionEffect <= Helpers::TE_ScrollBT) ? 10 : 20;

	// paint to memory DC
	int nW = m_clientRect.Width(), nH = m_clientRect.Height();
	CDC paintDC(::GetDC(m_hWnd));
	CDC memDC;
	memDC.CreateCompatibleDC(paintDC);
	CBitmap memDCBitmap;
	memDCBitmap.CreateCompatibleBitmap(paintDC, nW, nH);
	memDC.SelectBitmap(memDCBitmap);
	PaintToDC(memDC);

	int nSteps = max(1, (m_nTransitionTime + 20) / nFrameTimeMs);

	BLENDFUNCTION blendFunc{ 0 };
	blendFunc.BlendOp = AC_SRC_OVER;
	blendFunc.AlphaFormat = 0;
	float fAlphaStep = 255.0f / nSteps;

	DWORD lastTime = ::GetTickCount();
	for (int i = 0; i <= nSteps; i++) {
		switch (m_eTransitionEffect)
		{
		case Helpers::TE_Blend:
			{
				if (i == nSteps) {
					paintDC.BitBlt(0, 0, nW, nH, memDC, 0, 0, SRCCOPY);
				} else {
					float fFactor = (float)i / nSteps ;
					blendFunc.SourceConstantAlpha = min(255, (int)((fFactor * fFactor * i + 1) * fAlphaStep + 0.5f));
					paintDC.AlphaBlend(0, 0, nW, nH, memDC, 0, 0, nW, nH, blendFunc);
				}
				break;
			}
		case Helpers::TE_SlideLR:
		case Helpers::TE_SlideRL:
		case Helpers::TE_SlideBT:
		case Helpers::TE_SlideTB:
			{
				float fFactor = (float)(i + 1) / (nSteps + 1);
				int nFracHeight = (int)(nH * fFactor + 0.5f);
				int nFracWidth = (int)(nW * fFactor + 0.5f);
				int nStartX = (m_eTransitionEffect == Helpers::TE_SlideLR) ? nFracWidth - nW : (m_eTransitionEffect == Helpers::TE_SlideRL) ? nW - nFracWidth : 0;
				int nStartY = (m_eTransitionEffect == Helpers::TE_SlideTB) ? nFracHeight - nH : (m_eTransitionEffect == Helpers::TE_SlideBT) ? nH - nFracHeight : 0;
				paintDC.BitBlt(nStartX, nStartY, nW, nH, memDC, 0, 0, SRCCOPY);
				break;
			}
		case Helpers::TE_RollLR:
		case Helpers::TE_RollRL:
		case Helpers::TE_RollBT:
		case Helpers::TE_RollTB:
			{
				float fFactor = (float)(i + 1) / (nSteps + 1);
				int nFracHeight = (int)(nH * fFactor + 0.5f);
				int nClipH = (m_eTransitionEffect == Helpers::TE_RollLR || m_eTransitionEffect == Helpers::TE_RollRL) ? nH : 1 + nH / (nSteps + 1);
				int nFracWidth = (int)(nW * fFactor + 0.5f);
				int nClipW = (m_eTransitionEffect == Helpers::TE_RollTB || m_eTransitionEffect == Helpers::TE_RollBT) ? nW : 1 + nW / (nSteps + 1);
				int nClipX = (m_eTransitionEffect == Helpers::TE_RollLR) ? nFracWidth - nClipW : (m_eTransitionEffect == Helpers::TE_RollRL) ? nW - nFracWidth : 0;
				int nClipY = (m_eTransitionEffect == Helpers::TE_RollTB) ? nFracHeight - nClipH : (m_eTransitionEffect == Helpers::TE_RollBT) ? nH - nFracHeight : 0;
				CRgn region;
				region.CreateRectRgn(nClipX, nClipY, nClipX + nClipW, nClipY + nClipH);
				paintDC.SelectClipRgn(region);
				paintDC.BitBlt(0, 0, nW, nH, memDC, 0, 0, SRCCOPY);
				break;
			}
		case Helpers::TE_ScrollLR:
		case Helpers::TE_ScrollRL:
		case Helpers::TE_ScrollBT:
		case Helpers::TE_ScrollTB:
			{
				float fFactorLast = (float)i / (nSteps + 1);
				float fFactor = (float)(i + 1) / (nSteps + 1);
				int nScrollW, nScrollH;
				int nStartX, nStartY;
				int nStartClipX, nStartClipY, nEndClipX, nEndClipY;
				if (m_eTransitionEffect == Helpers::TE_ScrollLR) {
					nScrollW = (int)(nW * fFactor + 0.5f) - (int)(nW * fFactorLast + 0.5f);
					nScrollH = 0;
					nStartX = (int)(nW * fFactor + 0.5f) - nW;
					nStartY = 0;
					nStartClipX = nStartClipY = 0;
					nEndClipX = nScrollW; nEndClipY = nH;
				} else if (m_eTransitionEffect == Helpers::TE_ScrollRL) {
					nScrollW = - ((int)(nW * fFactor + 0.5f) - (int)(nW * fFactorLast + 0.5f));
					nScrollH = 0;
					nStartX = nW - (int)(nW * fFactor + 0.5f);
					nStartY = 0;
					nStartClipX = nW + nScrollW;
					nStartClipY = 0;
					nEndClipX = nW; nEndClipY = nH;
				} else if (m_eTransitionEffect == Helpers::TE_ScrollTB) {
					nScrollW = 0;
					nScrollH = (int)(nH * fFactor + 0.5f) - (int)(nH * fFactorLast + 0.5f);
					nStartX = 0;
					nStartY = (int)(nH * fFactor + 0.5f) - nH;
					nStartClipX = 0; nStartClipY = 0;
					nEndClipX = nW; nEndClipY = nScrollH;
				} else {
					nScrollW = 0;
					nScrollH = -((int)(nH * fFactor + 0.5f) - (int)(nH * fFactorLast + 0.5f));
					nStartX = 0;
					nStartY = nH - (int)(nH * fFactor + 0.5f);
					nStartClipX = 0; nStartClipY = nH + nScrollH;
					nEndClipX = nW; nEndClipY = nH;
				}

				paintDC.SelectClipRgn(NULL);
				paintDC.BitBlt(nScrollW, nScrollH, nW, nH, paintDC, 0, 0, SRCCOPY);
				
				CRgn region;
				region.CreateRectRgn(nStartClipX, nStartClipY, nEndClipX, nEndClipY);
				paintDC.SelectClipRgn(region);

				paintDC.BitBlt(nStartX, nStartY, nW, nH, memDC, 0, 0, SRCCOPY);
				break;
			}
		}
		DWORD time = ::GetTickCount();
		if (time - lastTime < nFrameTimeMs) {
			::Sleep(nFrameTimeMs - (time - lastTime));
		}
		lastTime = time;
		
		// terminate if a key is pressed or context menu shall be shown
		MSG msg;
		if (::PeekMessage(&msg, m_hWnd, WM_KEYFIRST, WM_KEYLAST, PM_NOREMOVE)) break;
		if (::PeekMessage(&msg, m_hWnd, WM_CONTEXTMENU, WM_CONTEXTMENU, PM_NOREMOVE)) break;
	}
}

void CMainDlg::CleanupAndTerminate() {
	StopMovieMode();
	StopAnimation();
	delete m_pJPEGProvider; // delete this early to properly shut down the loading threads
	m_pJPEGProvider = NULL;
	EndDialog(0);
}
	
void CMainDlg::StartAnimation() {
	if (m_bIsAnimationPlaying) {
		return;
	}
	m_eProcFlagsBeforeMovie = CreateDefaultProcessingFlags(m_bKeepParams);
	m_bKeepParametersBeforeAnimation = IsKeepParams();
	if (!m_bKeepParametersBeforeAnimation) {
		ExecuteCommand(IDM_KEEP_PARAMETERS);
	}
	m_bAutoContrastSection = false;
	m_bAutoContrast = false;
	m_bLDC = false;
	m_bLandscapeMode = false;
	m_bIsAnimationPlaying = true;
	int nNewFrameTime = max(10, m_pCurrentImage->FrameTimeMs());
	::SetTimer(this->m_hWnd, ANIMATION_TIMER_EVENT_ID, nNewFrameTime, NULL);
	m_pNavPanelCtl->EndNavPanelAnimation();
	m_nLastSlideShowImageTickCount = ::GetTickCount();
	m_nLastAnimationOffset = 0;
	m_nExpectedNextAnimationTickCount = ::GetTickCount() + nNewFrameTime;
}

void CMainDlg::AdjustAnimationFrameTime() {
	// restart timer with new frame time
	::KillTimer(this->m_hWnd, ANIMATION_TIMER_EVENT_ID);
	m_nLastAnimationOffset += ::GetTickCount() - m_nExpectedNextAnimationTickCount;
	m_nLastAnimationOffset = min(m_nLastAnimationOffset, max(100, m_pCurrentImage->FrameTimeMs())); // prevent offset from getting too big
	int nNewFrameTime = max(10, m_pCurrentImage->FrameTimeMs() - max(0, m_nLastAnimationOffset));
	m_nExpectedNextAnimationTickCount = ::GetTickCount() + max(10, m_pCurrentImage->FrameTimeMs());
	::SetTimer(this->m_hWnd, ANIMATION_TIMER_EVENT_ID, nNewFrameTime, NULL);
}

void CMainDlg::StopAnimation() {
	if (!m_bIsAnimationPlaying) {
		return;
	}
	if (IsKeepParams() != m_bKeepParametersBeforeAnimation) {
		ExecuteCommand(IDM_KEEP_PARAMETERS);
	}
	if (GetProcessingFlag(m_eProcFlagsBeforeMovie, PFLAG_AutoContrast)) {
		m_bAutoContrast = true;
	}
	if (GetProcessingFlag(m_eProcFlagsBeforeMovie, PFLAG_LDC)) {
		m_bLDC = true;
	}
	if (GetProcessingFlag(m_eProcFlagsBeforeMovie, PFLAG_LandscapeMode)) {
		m_bLandscapeMode = true;
	}
	::KillTimer(this->m_hWnd, ANIMATION_TIMER_EVENT_ID);
	m_bIsAnimationPlaying = false;
}

void CMainDlg::ToggleAlwaysOnTop() {
	// toggle the member variable and call the SetWindowPos to set
	m_bAlwaysOnTop = !m_bAlwaysOnTop;

	// SetWindowPos - https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos
	this->SetWindowPos(
		m_bAlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
		0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE  // causes SetWindowPos to ignore the parameters for top/left/width/height
	);

}

///////////////////////////////////////////////////////////////////////////////////
// Image editing from the side panel, before/after comparison
///////////////////////////////////////////////////////////////////////////////////

void CMainDlg::OnImageProcessingChanged() {
	m_bProcFlagsTouched = true;
	Invalidate(FALSE);
}

CImageProcessingParams CMainDlg::GetDefaultImageProcessingParams() {
	return _SetLandscapeModeParams(m_bLandscapeMode, GetDefaultProcessingParams());
}

void CMainDlg::ResetImageProcessing() {
	if (m_pCurrentImage == NULL) {
		return;
	}
	*m_pImageProcParams = _SetLandscapeModeParams(m_bLandscapeMode, GetDefaultProcessingParams());
	bool bHQResampling, bLandscapeMode;
	InitFromProcessingFlags(GetDefaultProcessingFlags(m_bLandscapeMode), bHQResampling, m_bAutoContrast, m_bAutoContrastSection, m_bLDC, bLandscapeMode);
	m_pChrome->ShowMessage(CNLS::GetString(_T("All values were reset to the defaults.")));
	Invalidate(FALSE);
}

void CMainDlg::SetCompareMode(bool bCompare) {
	m_bCompareMode = bCompare && m_pCurrentImage != NULL;
	m_bDraggingCompareSplit = false;
	if (!m_bCompareMode) {
		std::vector<uint8>().swap(m_compareBuffer);
	}
	Invalidate(FALSE);
	m_pChrome->Paint();
}

void* CMainDlg::ComposeCompareDIB(void* pProcessedDIB, CSize clippedSize) {
	// The unedited image is the resampled DIB before the color, contrast and LDC corrections of the same GetDIB() call.
	// A second GetDIB() call with other parameters is not used, it would invalidate the cached DIBs of the image.
	void* pBeforeDIB = m_pCurrentImage->DIBPixels();
	size_t nBytes = (size_t)clippedSize.cx * clippedSize.cy * 4;
	if (pBeforeDIB == NULL || nBytes == 0 || m_pCurrentImage->DIBWidth() != clippedSize.cx || m_pCurrentImage->DIBHeight() != clippedSize.cy) {
		return pProcessedDIB;
	}
	m_compareBuffer.resize(nBytes);
	// left of the split the unedited image, right of it the processed one, the DIB is centered in the client area
	int nDIBLeft = (m_clientRect.Width() - clippedSize.cx) / 2 + m_DIBOffsets.x;
	int nSplit = max(0, min(clippedSize.cx, GetCompareSplitX() - nDIBLeft));
	for (int y = 0; y < clippedSize.cy; y++) {
		size_t nRow = (size_t)y * clippedSize.cx * 4;
		memcpy(&m_compareBuffer[nRow], (uint8*)pBeforeDIB + nRow, nSplit * 4);
		memcpy(&m_compareBuffer[nRow + nSplit * 4], (uint8*)pProcessedDIB + nRow + nSplit * 4, (clippedSize.cx - nSplit) * 4);
	}
	return &m_compareBuffer[0];
}

int CMainDlg::GetCompareSplitX() {
	return (int)(m_dCompareSplit * m_clientRect.Width() + 0.5);
}

bool CMainDlg::IsOnCompareSplit(int nX) {
	return m_pCurrentImage != NULL && abs(nX - GetCompareSplitX()) <= HelpersGUI::ScaleToScreen(8);
}

void CMainDlg::PaintCompareSplit(CDC& dc) {
	int nX = GetCompareSplitX();
	int nLine = max(1, HelpersGUI::ScaleToScreen(2));
	dc.FillSolidRect(nX - nLine / 2, m_clientRect.top, nLine, m_clientRect.Height(), RGB(255, 255, 255));

	// round handle with arrows in the middle
	int nRadius = HelpersGUI::ScaleToScreen(16);
	int nY = m_clientRect.top + m_clientRect.Height() / 2;
	CBrush brush;
	brush.CreateSolidBrush(RGB(255, 255, 255));
	CPen pen;
	pen.CreatePen(PS_SOLID, 1, RGB(60, 60, 60));
	HBRUSH hOldBrush = dc.SelectBrush(brush);
	HPEN hOldPen = dc.SelectPen(pen);
	dc.Ellipse(nX - nRadius, nY - nRadius, nX + nRadius, nY + nRadius);
	CBrush arrowBrush;
	arrowBrush.CreateSolidBrush(RGB(40, 40, 40));
	dc.SelectBrush(arrowBrush);
	dc.SelectPen((HPEN)::GetStockObject(NULL_PEN));
	int nA = HelpersGUI::ScaleToScreen(5), nGap = HelpersGUI::ScaleToScreen(3);
	POINT left[3] = { { nX - nGap - nA, nY }, { nX - nGap, nY - nA }, { nX - nGap, nY + nA } };
	POINT right[3] = { { nX + nGap + nA, nY }, { nX + nGap, nY - nA }, { nX + nGap, nY + nA } };
	dc.Polygon(left, 3);
	dc.Polygon(right, 3);
	dc.SelectPen(hOldPen);
	dc.SelectBrush(hOldBrush);

	// labels at the top
	HelpersGUI::SelectDefaultFileNameFont(dc);
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(255, 255, 255));
	int nMargin = HelpersGUI::ScaleToScreen(10);
	int nTop = m_clientRect.top + HelpersGUI::ScaleToScreen(8);
	int nHeight = HelpersGUI::ScaleToScreen(30);
	HelpersGUI::DrawTextBordered(dc, CNLS::GetString(_T("Before")), CRect(m_clientRect.left, nTop, nX - nMargin, nTop + nHeight), DT_RIGHT);
	HelpersGUI::DrawTextBordered(dc, CNLS::GetString(_T("After")), CRect(nX + nMargin, nTop, m_clientRect.right, nTop + nHeight), DT_LEFT);
}

///////////////////////////////////////////////////////////////////////////////////
// Drawing on the image
///////////////////////////////////////////////////////////////////////////////////

bool CMainDlg::IsDrawMode() {
	return m_pCurrentImage != NULL && m_pChrome->IsDrawTabShown() && m_pAnnotations->GetTool() != CAnnotations::TOOL_NONE &&
		!m_pCropCtl->IsCropping() && !m_pPanelMgr->IsModalPanelShown();
}

void CMainDlg::PaintAnnotations(CDC& dc) {
	// the image is shown scaled and moved, the mapping is derived from two converted points
	float fX0 = 0.0f, fY0 = 0.0f, fX1 = 1000.0f, fY1 = 1000.0f;
	if (!ImageToScreen(fX0, fY0) || !ImageToScreen(fX1, fY1)) {
		return;
	}
	Gdiplus::Graphics graphics(dc.m_hDC);
	graphics.SetClip(Gdiplus::Rect(m_clientRect.left, m_clientRect.top, m_clientRect.Width(), m_clientRect.Height()));
	graphics.TranslateTransform(fX0, fY0);
	graphics.ScaleTransform((fX1 - fX0) / 1000.0f, (fY1 - fY0) / 1000.0f);
	m_pAnnotations->Render(graphics);
}

void CMainDlg::ApplyAnnotations() {
	if (m_pCurrentImage == NULL || m_pAnnotations->IsEmpty()) {
		return;
	}
	void* pPixels = m_pCurrentImage->GetOriginalPixelsForWriting();
	if (pPixels != NULL && m_pAnnotations->BurnInto(pPixels, m_pCurrentImage->OrigWidth(), m_pCurrentImage->OrigHeight())) {
		m_pAnnotations->Clear();
	}
	m_pImageProcPanelCtl->ShowHideSaveDBButtons();
	Invalidate(FALSE);
	m_pChrome->Paint();
}

///////////////////////////////////////////////////////////////////////////////////
// Star rating (Windows property System.Rating, also shown by Explorer and read by Lightroom)
///////////////////////////////////////////////////////////////////////////////////

// Rating values of the Windows property for 1 to 5 stars
static const UINT RATING_VALUES[6] = { 0, 1, 25, 50, 75, 99 };

int CMainDlg::GetRating() {
	LPCTSTR sFile = (m_pCurrentImage != NULL && !m_pCurrentImage->IsClipboardImage()) ? m_pFileList->Current() : NULL;
	if (sFile == NULL) {
		return -1;
	}
	if (m_sRatingFile.CompareNoCase(sFile) == 0) {
		return m_nRating;
	}
	m_sRatingFile = sFile;
	m_nRating = -1;
	IPropertyStore* pStore = NULL;
	if (SUCCEEDED(::SHGetPropertyStoreFromParsingName(sFile, NULL, GPS_DEFAULT, IID_PPV_ARGS(&pStore)))) {
		PROPVARIANT value;
		::PropVariantInit(&value);
		if (SUCCEEDED(pStore->GetValue(PKEY_Rating, &value))) {
			UINT nValue = (value.vt == VT_UI4) ? value.ulVal : 0;
			m_nRating = (nValue == 0) ? 0 : (nValue < 13) ? 1 : (nValue < 38) ? 2 : (nValue < 63) ? 3 : (nValue < 88) ? 4 : 5;
		}
		::PropVariantClear(&value);
		pStore->Release();
	}
	return m_nRating;
}

void CMainDlg::SetRating(int nStars) {
	LPCTSTR sFile = (m_pCurrentImage != NULL && !m_pCurrentImage->IsClipboardImage()) ? m_pFileList->Current() : NULL;
	if (sFile == NULL || nStars < 0 || nStars > 5) {
		return;
	}
	bool bSuccess = false;
	IPropertyStore* pStore = NULL;
	if (SUCCEEDED(::SHGetPropertyStoreFromParsingName(sFile, NULL, GPS_READWRITE, IID_PPV_ARGS(&pStore)))) {
		PROPVARIANT value;
		::PropVariantInit(&value);
		if (nStars == 0) {
			value.vt = VT_EMPTY;
		} else {
			value.vt = VT_UI4;
			value.ulVal = RATING_VALUES[nStars];
		}
		bSuccess = SUCCEEDED(pStore->SetValue(PKEY_Rating, value)) && SUCCEEDED(pStore->Commit());
		pStore->Release();
	}
	if (bSuccess) {
		m_sRatingFile = sFile;
		m_nRating = nStars;
		CString sMessage;
		if (nStars == 0) {
			sMessage = CNLS::GetString(_T("Rating removed"));
		} else {
			sMessage.Format(CNLS::GetString(_T("Rating: %d stars")), nStars);
		}
		m_pChrome->ShowMessage(sMessage);
	} else {
		m_pChrome->ShowMessage(CNLS::GetString(_T("The rating could not be saved in this file format.")));
	}
	m_pChrome->Paint();
}

///////////////////////////////////////////////////////////////////////////////////
// Remove color tool (transparent background)
///////////////////////////////////////////////////////////////////////////////////

void CMainDlg::ClearTransparency(bool bRestorePixels) {
	if (bRestorePixels && m_pCurrentImage != NULL && !m_transparencyBackup.empty()) {
		uint8* pPixels = (uint8*)m_pCurrentImage->GetOriginalPixelsForWriting();
		size_t nBytes = (size_t)m_pCurrentImage->OrigWidth() * m_pCurrentImage->OrigHeight() * 4;
		if (pPixels != NULL && m_transparencyBackup.size() == nBytes) {
			memcpy(pPixels, &m_transparencyBackup[0], nBytes);
		}
	}
	std::vector<uint8>().swap(m_transparencyMask);
	std::vector<uint8>().swap(m_transparencyBackup);
}

void CMainDlg::RemoveColorAt(float fX, float fY) {
	if (m_pCurrentImage == NULL) {
		return;
	}
	int nWidth = m_pCurrentImage->OrigWidth(), nHeight = m_pCurrentImage->OrigHeight();
	int nStartX = (int)fX, nStartY = (int)fY;
	if (nStartX < 0 || nStartY < 0 || nStartX >= nWidth || nStartY >= nHeight) {
		return;
	}
	HCURSOR hOldCursor = ::SetCursor(::LoadCursor(NULL, IDC_WAIT));
	uint8* pPixels = (uint8*)m_pCurrentImage->GetOriginalPixelsForWriting();
	if (pPixels == NULL) {
		::SetCursor(hOldCursor);
		return;
	}
	size_t nPixels = (size_t)nWidth * nHeight;
	if (m_transparencyMask.size() != nPixels) {
		m_transparencyMask.assign(nPixels, 255);
		m_transparencyBackup.assign(pPixels, pPixels + nPixels * 4);
	}
	// the width buttons of the drawing tools select the tolerance
	int nTolerance = max(6, min(90, m_pAnnotations->GetWidth() * 5));
	const uint8* pSeed = &m_transparencyBackup[((size_t)nStartY * nWidth + nStartX) * 4];
	int nSeedB = pSeed[0], nSeedG = pSeed[1], nSeedR = pSeed[2];
	// flood fill over the connected pixels of similar color, compared with the original colors
	std::vector<int> stack;
	stack.push_back(nStartY * nWidth + nStartX);
	int nRemoved = 0;
	while (!stack.empty()) {
		int nIndex = stack.back();
		stack.pop_back();
		if (m_transparencyMask[nIndex] == 0) continue;
		const uint8* p = &m_transparencyBackup[(size_t)nIndex * 4];
		if (abs(p[0] - nSeedB) > nTolerance || abs(p[1] - nSeedG) > nTolerance || abs(p[2] - nSeedR) > nTolerance) continue;
		m_transparencyMask[nIndex] = 0;
		nRemoved++;
		int x = nIndex % nWidth, y = nIndex / nWidth;
		// checker board for the transparent pixels
		uint8 nChecker = (((x >> 4) + (y >> 4)) & 1) ? 204 : 255;
		pPixels[(size_t)nIndex * 4] = pPixels[(size_t)nIndex * 4 + 1] = pPixels[(size_t)nIndex * 4 + 2] = nChecker;
		if (x > 0) stack.push_back(nIndex - 1);
		if (x < nWidth - 1) stack.push_back(nIndex + 1);
		if (y > 0) stack.push_back(nIndex - nWidth);
		if (y < nHeight - 1) stack.push_back(nIndex + nWidth);
	}
	::SetCursor(hOldCursor);
	CString sMessage;
	sMessage.Format(CNLS::GetString(_T("%d pixels made transparent. Save them with 'Save as transparent PNG'.")), nRemoved);
	m_pChrome->ShowMessage(sMessage);
	Invalidate(FALSE);
	m_pChrome->Paint();
}

void CMainDlg::ApplyTransparencyMask(const std::vector<uint8>& alpha) {
	if (m_pCurrentImage == NULL) {
		return;
	}
	int nWidth = m_pCurrentImage->OrigWidth(), nHeight = m_pCurrentImage->OrigHeight();
	size_t nPixels = (size_t)nWidth * nHeight;
	if (alpha.size() != nPixels) {
		return;
	}
	uint8* pPixels = (uint8*)m_pCurrentImage->GetOriginalPixelsForWriting();
	if (pPixels == NULL) {
		return;
	}
	if (m_transparencyMask.size() != nPixels) {
		m_transparencyMask.assign(nPixels, 255);
		m_transparencyBackup.assign(pPixels, pPixels + nPixels * 4);
	}
	for (size_t i = 0; i < nPixels; i++) {
		uint8 nAlpha = min(m_transparencyMask[i], alpha[i]);
		m_transparencyMask[i] = nAlpha;
		// shown blended over a checker board
		int x = (int)(i % nWidth), y = (int)(i / nWidth);
		int nChecker = (((x >> 4) + (y >> 4)) & 1) ? 204 : 255;
		for (int c = 0; c < 3; c++) {
			pPixels[i * 4 + c] = (uint8)((m_transparencyBackup[i * 4 + c] * nAlpha + nChecker * (255 - nAlpha)) / 255);
		}
	}
	Invalidate(FALSE);
	m_pChrome->Paint();
}
