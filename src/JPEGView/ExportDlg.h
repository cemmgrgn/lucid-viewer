#pragma once

class CMainDlg;

// Export window (File > Export...): format, quality, size and metadata with an estimate of the file size.
// The processed image (sliders and drawings) is exported, the window is built at runtime.
class CExportDlg
{
public:
	CExportDlg(CMainDlg* pMainDlg);

	// Shows the window modal to the main window, returns true if a file was written
	bool DoModal();

private:
	CMainDlg* m_pMainDlg;
	HWND m_hWnd;
	HFONT m_hFont;
	bool m_bDone, m_bOK;
	HWND m_hFormat, m_hQuality, m_hQualityText, m_hLongEdge, m_hKeepMetadata, m_hEstimate;
	CString m_sEstimateFile;

	void CreateControls();
	HWND AddControl(LPCTSTR sClass, LPCTSTR sText, DWORD nStyle, int nX, int nY, int nWidth, int nHeight, int nId);
	void UpdateControls();
	void UpdateEstimate();
	CSize GetTargetSize();
	bool Export();
public:
	// Writes the image with the current settings, returns false on error
	bool WriteFile(LPCTSTR sFileName, CSize targetSize);
private:
	static LRESULT CALLBACK WndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam);
};
