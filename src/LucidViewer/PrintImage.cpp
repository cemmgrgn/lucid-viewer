#include "StdAfx.h"
#include "PrintImage.h"
#include "JPEGImage.h"
#include "BasicProcessing.h"
#include "MaxImageDef.h"
#include "Helpers.h"
#include "PrintDlg.h"
#include "PrintParameters.h"
#include <shlwapi.h>
#include <vector>
#pragma comment(lib, "shlwapi.lib")

// Converts 1/10 mm to inch
static double Mm10ToInch(double value) {
	return value * 0.01 / 2.54;
}

void CPrintImage::ClearOffsets() {
	if (m_pPrintParameters != NULL) {
		m_pPrintParameters->OffsetX = m_pPrintParameters->OffsetY = 0;
	}
}

bool CPrintImage::Print(HWND hWnd, CJPEGImage * pImage, const CImageProcessingParams& procParams,
	EProcessingFlags eFlags, LPCTSTR fileName) {
	if (pImage == NULL) {
		return false;
	}

	if (m_pPrintParameters == NULL) {
		m_pPrintParameters = new CPrintParameters(m_defaultMargin, m_defaultPrintWidth);
	}

	if (m_pPrinterSelectionDlg == NULL) {
		m_pPrinterSelectionDlg = new CPrintDialogEx(PD_ALLPAGES | PD_USEDEVMODECOPIES | PD_NOPAGENUMS | PD_NOSELECTION | PD_NOCURRENTPAGE, hWnd);
		HCURSOR hOldCursor = ::SetCursor(::LoadCursor(NULL, IDC_WAIT));
		HRESULT res = m_pPrinterSelectionDlg->GetDefaults();
		::SetCursor(hOldCursor);
		m_pPrinterSelectionDlg->m_pdex.Flags &= ~PD_RETURNDEFAULT;
		if (res != S_OK)
			return false;
	}

	CPrintDlg printDlg(m_pPrintParameters, m_pPrinterSelectionDlg, m_pPrinterSelectionDlg->GetDevMode(), m_pPrinterSelectionDlg->GetDeviceName(),
		m_pPrinterSelectionDlg->GetPortName(), pImage, procParams, eFlags);
	printDlg.SetFileName(fileName);
	bool bPrinted = false;
	if (printDlg.DoModal(hWnd) == IDOK) {
		bPrinted = DoPrint(printDlg.GetPrinterDC(), m_pPrintParameters, pImage, procParams, eFlags, fileName);
	}

	if (printDlg.GetPrinterDC() != NULL) {
		::DeleteDC(printDlg.GetPrinterDC());
	}
	return bPrinted;
}

bool CPrintImage::DoPrint(HDC hPrinterDC, CPrintParameters* pPrintParameters, CJPEGImage * pImage, const CImageProcessingParams& procParams,
	EProcessingFlags eFlags, LPCTSTR fileName) {
	bool bSuccess = false;
	DOCINFO docInfo{ 0 };
	docInfo.cbSize = sizeof(DOCINFO);
	docInfo.lpszDocName = fileName;

	if (::StartDoc(hPrinterDC, &docInfo)) {
		HCURSOR hOldCursor = ::SetCursor(::LoadCursor(NULL, IDC_WAIT));
		int dpiX = ::GetDeviceCaps(hPrinterDC, LOGPIXELSX);
		int dpiY = ::GetDeviceCaps(hPrinterDC, LOGPIXELSY);
		int pixelsX = ::GetDeviceCaps(hPrinterDC, HORZRES);
		int pixelsY = ::GetDeviceCaps(hPrinterDC, VERTRES);
		double dpiRatio = (double)dpiY / dpiX;

		int physicalWidth = ::GetDeviceCaps(hPrinterDC, PHYSICALWIDTH);
		int physicalHeight = ::GetDeviceCaps(hPrinterDC, PHYSICALHEIGHT);
		int physicalOffsetX = ::GetDeviceCaps(hPrinterDC, PHYSICALOFFSETX);
		int physicalOffsetY = ::GetDeviceCaps(hPrinterDC, PHYSICALOFFSETY);

		CRect margins(Helpers::RoundToInt(dpiX * Mm10ToInch(pPrintParameters->MarginLeft)),
			Helpers::RoundToInt(dpiY * Mm10ToInch(pPrintParameters->MarginTop)),
			Helpers::RoundToInt(dpiX * Mm10ToInch(pPrintParameters->MarginRight)),
			Helpers::RoundToInt(dpiY * Mm10ToInch(pPrintParameters->MarginBottom)));

		CRect paperArea(margins.left - physicalOffsetX, margins.top - physicalOffsetY,
			pixelsX - (margins.right - (physicalWidth - physicalOffsetX - pixelsX)),
			pixelsY - (margins.bottom - (physicalHeight - physicalOffsetY - pixelsY)));

		// clip against physical borders
		paperArea = CRect(max(0, paperArea.left), max(0, paperArea.top), min(pixelsX, paperArea.right), min(pixelsY, paperArea.bottom));

		{
			CRect printableRect = paperArea;
			CSize paperSize(physicalWidth, physicalHeight);
			CSize imageSize;
			if (pPrintParameters->FitToPaper) {
				imageSize = Helpers::FitRectIntoRect(CSize(pImage->OrigWidth(), Helpers::RoundToInt(pImage->OrigHeight() * dpiRatio)),
					printableRect).Size();
			} else if (pPrintParameters->FillWithCrop) {
				imageSize = Helpers::FillRectAroundRect(CSize(pImage->OrigWidth(), Helpers::RoundToInt(pImage->OrigHeight() * dpiRatio)),
					printableRect).Size(); 
			} else {
				double imageAR = (double)pImage->OrigHeight() / pImage->OrigWidth();
				imageSize = CSize(Helpers::RoundToInt(dpiX * Mm10ToInch(pPrintParameters->PrintWidth)),
					Helpers::RoundToInt(dpiY * Mm10ToInch(pPrintParameters->PrintWidth) * imageAR));
			}
			
			CRect imageRect = CPrintDlg::Align(imageSize, printableRect, pPrintParameters->AlignmentX, pPrintParameters->AlignmentY);
			CPoint offsets(Helpers::RoundToInt(dpiX * Mm10ToInch(pPrintParameters->OffsetX)), Helpers::RoundToInt(dpiY * Mm10ToInch(pPrintParameters->OffsetY)));
			imageRect.OffsetRect(CPrintDlg::LimitOffset(imageRect, printableRect, offsets));

			CRect clippedRect;
			clippedRect.IntersectRect(imageRect, printableRect);
			CSize clippedImageSize = clippedRect.Size();

			CSize dibSize = pPrintParameters->ScaleByPrinterDriver ? pImage->OrigSize() : clippedImageSize;
			CSize fullTargetSize = pPrintParameters->ScaleByPrinterDriver ? dibSize : imageSize;
			CPoint offset = pPrintParameters->ScaleByPrinterDriver ? CPoint(0, 0) : CPoint(max(0, printableRect.left - imageRect.left), max(0, printableRect.top - imageRect.top));

			pImage->EnableDimming(false);
			void* pDIBData = pImage->GetDIB(fullTargetSize, dibSize, offset, procParams, eFlags);
			pImage->EnableDimming(true);

			std::vector<BYTE> grayPixels;
			if (pDIBData != NULL && pPrintParameters->Grayscale) {
				grayPixels.assign((BYTE*)pDIBData, (BYTE*)pDIBData + (size_t)dibSize.cx * dibSize.cy * 4);
				for (size_t i = 0; i + 3 < grayPixels.size(); i += 4) {
					BYTE nGray = (BYTE)((grayPixels[i] * 29 + grayPixels[i + 1] * 150 + grayPixels[i + 2] * 77) >> 8);
					grayPixels[i] = grayPixels[i + 1] = grayPixels[i + 2] = nGray;
				}
				pDIBData = &grayPixels[0];
			}

			for (int nCopy = 0; pDIBData != NULL && nCopy < max(1, pPrintParameters->Copies); nCopy++) {
				if (::StartPage(hPrinterDC) < 0) break;
				int nSaved = ::SaveDC(hPrinterDC);
				BITMAPINFO bmInfo{ 0 };
				bmInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				bmInfo.bmiHeader.biWidth = dibSize.cx;
				bmInfo.bmiHeader.biHeight = -dibSize.cy;
				bmInfo.bmiHeader.biPlanes = 1;
				bmInfo.bmiHeader.biBitCount = 32;
				bmInfo.bmiHeader.biCompression = BI_RGB;
				::IntersectClipRect(hPrinterDC, printableRect.left, printableRect.top, printableRect.right, printableRect.bottom);
				if (pPrintParameters->ScaleByPrinterDriver) {
					::StretchDIBits(hPrinterDC, imageRect.left, imageRect.top, imageRect.Width(), imageRect.Height(),
						0, 0, pImage->OrigWidth(), pImage->OrigHeight(), pDIBData, &bmInfo, DIB_RGB_COLORS, SRCCOPY);
				} else {
					::SetDIBitsToDevice(hPrinterDC, clippedRect.left, clippedRect.top, clippedRect.Width(), clippedRect.Height(), 0, 0, 0,
						clippedRect.Height(), pDIBData, &bmInfo, DIB_RGB_COLORS);
				}
				if (pPrintParameters->PrintFileName && fileName != NULL) {
					// 3.5 mm high, below the image or at the bottom of the printable area
					int nTextHeight = Helpers::RoundToInt(dpiY * 3.5 / 25.4);
					HFONT hFont = ::CreateFont(-nTextHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, _T("Segoe UI"));
					HGDIOBJ hOldFont = ::SelectObject(hPrinterDC, hFont);
					RECT textRect = { clippedRect.left, 0, clippedRect.right, 0 };
					textRect.top = min((int)clippedRect.bottom + nTextHeight / 2, (int)printableRect.bottom - nTextHeight * 3 / 2);
					textRect.bottom = textRect.top + nTextHeight * 3 / 2;
					::SetBkMode(hPrinterDC, TRANSPARENT);
					::SetTextColor(hPrinterDC, RGB(40, 40, 40));
					::DrawText(hPrinterDC, ::PathFindFileName(fileName), -1, &textRect, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
					::SelectObject(hPrinterDC, hOldFont);
					::DeleteObject(hFont);
				}
				::RestoreDC(hPrinterDC, nSaved);
				::EndPage(hPrinterDC);
				bSuccess = true;
			}
		}

		::EndDoc(hPrinterDC);

		::SetCursor(hOldCursor);
	}
	return bSuccess;
}