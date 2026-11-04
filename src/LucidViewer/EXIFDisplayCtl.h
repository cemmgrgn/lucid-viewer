#pragma once

#include "PanelController.h"

class CEXIFDisplay;

// Implements functionality of the EXIF display panel
class CEXIFDisplayCtl : public CPanelController
{
public:
	CEXIFDisplayCtl(CMainDlg* pMainDlg, CPanel* pImageProcPanel);
	virtual ~CEXIFDisplayCtl();

	virtual float DimFactor() { return 0.5f; }

	virtual bool IsVisible();
	virtual bool IsActive() { return m_bVisible; }

	virtual void SetVisible(bool bVisible);
	virtual void SetActive(bool bActive);

	virtual void AfterNewImageLoaded();

	virtual bool OnMouseMove(int nX, int nY);
	virtual void OnPrePaintMainDlg(HDC hPaintDC);

	// Collects the file and EXIF information of the current image, also used by the side panel.
	// Only call when there is a current image.
	void FillEXIFDataDisplay();
	CEXIFDisplay* GetEXIFDisplay() { return m_pEXIFDisplay; }

private:
	bool m_bVisible;
	CEXIFDisplay* m_pEXIFDisplay;
	CPanel* m_pImageProcPanel;
	int m_nFileNameHeight;

	static void OnShowHistogram(void* pContext, int nParameter, CButtonCtrl & sender);
	static void OnClose(void* pContext, int nParameter, CButtonCtrl & sender);
};