#pragma once

#include <vector>

class CMainDlg;

// Finds similar and duplicate photos of the current folder (Tools > Find similar photos...).
// A perceptual hash (dHash) of the thumbnail of each file is compared, the scan runs in a background thread.
class CDuplicatesDlg
{
public:
	CDuplicatesDlg(CMainDlg* pMainDlg);

	void DoModal();

private:
	CMainDlg* m_pMainDlg;
	HWND m_hWnd, m_hList, m_hStatus;
	HFONT m_hFont;
	bool m_bDone;
	volatile bool m_bCancel;
	HANDLE m_hThread;
	std::vector<CString> m_files;
	std::vector<unsigned __int64> m_hashes;
	std::vector<bool> m_hashValid;
	volatile LONG m_nScanned;

	void CreateControls();
	void ShowResult();
	CString GetSelectedFile();
	static unsigned __stdcall ScanThread(void* pParam);
	static LRESULT CALLBACK WndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam);
};
