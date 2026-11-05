#include "StdAfx.h"
#include "Clipboard.h"
#include "JPEGImage.h"
#include "Helpers.h"
#include "BasicProcessing.h"
#include "MaxImageDef.h"
#include <gdiplus.h>
#include <shlobj.h>
#include "Annotations.h"

static void CopyOriginalFileNameToClipboard(LPCWSTR filename) {
	int fileNameLength = (int)wcslen(filename);
	DWORD fileNameLengthBytes = sizeof(wchar_t) * (fileNameLength + 1);
	DROPFILES df = {sizeof(DROPFILES), {0, 0}, 0, TRUE};
	HGLOBAL hMem = ::GlobalAlloc(GMEM_ZEROINIT|GMEM_MOVEABLE|GMEM_DDESHARE, sizeof(DROPFILES) + fileNameLengthBytes + sizeof(wchar_t)); // for double NULL char
	int offset = sizeof(DROPFILES) / sizeof(wchar_t);
	WCHAR *pGlobal = (WCHAR *) ::GlobalLock(hMem);
	::CopyMemory(pGlobal, &df, sizeof(DROPFILES));
	::CopyMemory(pGlobal + offset, filename, fileNameLengthBytes); // that's pGlobal + 20 bytes (the size of DROPFILES);
	pGlobal[offset + fileNameLength + 1] = 0; // add additional NULL character
	::GlobalUnlock(hMem);
	::SetClipboardData(CF_HDROP, hMem);
}

static void CopyFileNameTextToClipboard(LPCWSTR filename) {
	int fileNameLength = (int)wcslen(filename);
	DWORD fileNameLengthBytes = sizeof(wchar_t) * (fileNameLength + 1);
	HGLOBAL hMem = ::GlobalAlloc(GMEM_ZEROINIT | GMEM_MOVEABLE | GMEM_DDESHARE, fileNameLengthBytes + sizeof(wchar_t)); // for double NULL char
	WCHAR* pGlobal = (WCHAR*) ::GlobalLock(hMem);
	::CopyMemory(pGlobal, filename, fileNameLengthBytes);
	pGlobal[fileNameLength + 1] = 0; // add additional NULL character
	::GlobalUnlock(hMem);
	::SetClipboardData(CF_UNICODETEXT, hMem);
}

void CClipboard::CopyImageToClipboard(HWND hWnd, CJPEGImage * pImage, LPCTSTR fileName) {
	if (pImage == NULL || pImage->DIBPixelsLastProcessed(true) == NULL) {
		return;
	}

	pImage->EnableDimming(false);
	DoCopy(hWnd, pImage->DIBWidth(), pImage->DIBHeight(), pImage->DIBPixelsLastProcessed(true), pImage->IsClipboardImage() ? NULL : fileName);
	pImage->EnableDimming(true);
}

void CClipboard::CopyFullImageToClipboard(HWND hWnd, CJPEGImage * pImage, const CImageProcessingParams& procParams,
										  EProcessingFlags eFlags, LPCTSTR fileName) {
	CopyFullImageToClipboard(hWnd, pImage, procParams, eFlags, CRect(0, 0, pImage->OrigWidth(), pImage->OrigHeight()), fileName);
}

void CClipboard::CopyFullImageToClipboard(HWND hWnd, CJPEGImage * pImage, const CImageProcessingParams& procParams,
										  EProcessingFlags eFlags, CRect clipRect, LPCTSTR fileName) {
	if (pImage == NULL) {
		return;
	}

	clipRect.left = max(0, clipRect.left);
	clipRect.top = max(0, clipRect.top);
	clipRect.right = min(pImage->OrigWidth(), clipRect.right);
	clipRect.bottom = min(pImage->OrigHeight(), clipRect.bottom);

	pImage->EnableDimming(false);
	void* pDIB = pImage->GetDIB(pImage->OrigSize(), clipRect.Size(), clipRect.TopLeft(), procParams, eFlags);
	DoCopy(hWnd, clipRect.Width(), clipRect.Height(), pDIB, pImage->IsClipboardImage() ? NULL : fileName);
	pImage->EnableDimming(true);
}

void CClipboard::CopyPathToClipboard(HWND hWnd, CJPEGImage* pImage, LPCTSTR fileName) {
	if (pImage == NULL || pImage->DIBPixelsLastProcessed(true) == NULL) {
		return;
	}
	pImage->EnableDimming(false);
	DoCopyFileNameText(hWnd, fileName);
	pImage->EnableDimming(true);
}

bool CClipboard::CopyTextToClipboard(HWND hWnd, LPCTSTR sText) {
	if (sText == NULL || !::OpenClipboard(hWnd)) {
		return false;
	}
	::EmptyClipboard();
	size_t nBytes = (_tcslen(sText) + 1) * sizeof(TCHAR);
	HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, nBytes);
	bool bOk = false;
	if (hMem != NULL) {
		void* pMem = ::GlobalLock(hMem);
		if (pMem != NULL) {
			memcpy(pMem, sText, nBytes);
			::GlobalUnlock(hMem);
			bOk = ::SetClipboardData(sizeof(TCHAR) == sizeof(WCHAR) ? CF_UNICODETEXT : CF_TEXT, hMem) != NULL;
		}
		if (!bOk) ::GlobalFree(hMem);
	}
	::CloseClipboard();
	return bOk;
}

bool CClipboard::CopyFileToClipboard(HWND hWnd, LPCTSTR fileName) {
	if (fileName == NULL || fileName[0] == 0) {
		return false;
	}
	std::vector<CString> fileNames;
	fileNames.push_back(fileName);
	return CopyFilesToClipboard(hWnd, fileNames);
}

bool CClipboard::CopyFilesToClipboard(HWND hWnd, const std::vector<CString>& fileNames) {
	if (fileNames.empty()) {
		return false;
	}
	// CF_HDROP: DROPFILES followed by the list of zero terminated file names, the list ends with a second zero
	size_t nChars = 0;
	for (const CString& sName : fileNames) nChars += sName.GetLength() + 1;
	HGLOBAL hDrop = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + (nChars + 1) * sizeof(TCHAR));
	if (hDrop == NULL) {
		return false;
	}
	DROPFILES* pDropFiles = (DROPFILES*)::GlobalLock(hDrop);
	if (pDropFiles == NULL) {
		::GlobalFree(hDrop);
		return false;
	}
	pDropFiles->pFiles = sizeof(DROPFILES);
	pDropFiles->fWide = sizeof(TCHAR) == sizeof(WCHAR);
	TCHAR* pTarget = (TCHAR*)((BYTE*)pDropFiles + sizeof(DROPFILES));
	for (const CString& sName : fileNames) {
		memcpy(pTarget, (LPCTSTR)sName, sName.GetLength() * sizeof(TCHAR));
		pTarget += sName.GetLength() + 1; // the zero is there already (GMEM_ZEROINIT)
	}
	::GlobalUnlock(hDrop);

	// Explorer pastes a copy, not a move
	HGLOBAL hEffect = ::GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
	if (hEffect != NULL) {
		DWORD* pEffect = (DWORD*)::GlobalLock(hEffect);
		if (pEffect != NULL) {
			*pEffect = DROPEFFECT_COPY;
			::GlobalUnlock(hEffect);
		}
	}

	if (!::OpenClipboard(hWnd)) {
		::GlobalFree(hDrop);
		if (hEffect != NULL) ::GlobalFree(hEffect);
		return false;
	}
	::EmptyClipboard();
	bool bOk = ::SetClipboardData(CF_HDROP, hDrop) != NULL;
	if (!bOk) {
		::GlobalFree(hDrop);
	}
	if (hEffect != NULL && ::SetClipboardData(::RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT), hEffect) == NULL) {
		::GlobalFree(hEffect);
	}
	if (bOk && fileNames.size() == 1) {
		CopyFileNameTextToClipboard(fileNames[0]);
	}
	::CloseClipboard();
	return bOk;
}

bool CClipboard::CopyTransparentImageToClipboard(HWND hWnd, const void* pBGRA, const unsigned char* pMask, int nWidth, int nHeight) {
	if (pBGRA == NULL || pMask == NULL || nWidth <= 0 || nHeight <= 0) {
		return false;
	}
	HGLOBAL hPNG = CAnnotations::EncodeTransparentPNG(pBGRA, pMask, nWidth, nHeight);
	if (hPNG == NULL) {
		return false;
	}
	// 24 bpp bottom-up DIB, the removed parts white
	uint32 nLineBytes = Helpers::DoPadding(nWidth * 3, 4);
	SIZE_T nDIBBytes = sizeof(BITMAPINFOHEADER) + (SIZE_T)nLineBytes * nHeight;
	HGLOBAL hDIB = ::GlobalAlloc(GMEM_MOVEABLE, nDIBBytes);
	uint8* pDIB = (hDIB != NULL) ? (uint8*)::GlobalLock(hDIB) : NULL;
	if (pDIB != NULL) {
		BITMAPINFOHEADER* pHeader = (BITMAPINFOHEADER*)pDIB;
		memset(pHeader, 0, sizeof(BITMAPINFOHEADER));
		pHeader->biSize = sizeof(BITMAPINFOHEADER);
		pHeader->biWidth = nWidth;
		pHeader->biHeight = nHeight;
		pHeader->biPlanes = 1;
		pHeader->biBitCount = 24;
		pHeader->biCompression = BI_RGB;
		const uint8* pSource = (const uint8*)pBGRA;
		for (int y = 0; y < nHeight; y++) {
			uint8* pTarget = pDIB + sizeof(BITMAPINFOHEADER) + (SIZE_T)(nHeight - 1 - y) * nLineBytes;
			for (int x = 0; x < nWidth; x++) {
				SIZE_T nIndex = (SIZE_T)y * nWidth + x;
				int nAlpha = pMask[nIndex];
				for (int c = 0; c < 3; c++) {
					pTarget[x * 3 + c] = (uint8)((pSource[nIndex * 4 + c] * nAlpha + 255 * (255 - nAlpha) + 127) / 255);
				}
			}
		}
		::GlobalUnlock(hDIB);
	}
	if (!::OpenClipboard(hWnd)) {
		::GlobalFree(hPNG);
		if (hDIB != NULL) ::GlobalFree(hDIB);
		return false;
	}
	::EmptyClipboard();
	bool bOk = ::SetClipboardData(::RegisterClipboardFormat(_T("PNG")), hPNG) != NULL;
	if (!bOk) ::GlobalFree(hPNG);
	if (hDIB != NULL) {
		if (pDIB == NULL || ::SetClipboardData(CF_DIB, hDIB) == NULL) {
			::GlobalFree(hDIB);
		} else {
			bOk = true;
		}
	}
	::CloseClipboard();
	return bOk;
}

CJPEGImage* CClipboard::PasteImageFromClipboard(HWND hWnd, const CImageProcessingParams& procParams, 
												EProcessingFlags eFlags) {
	if (!::OpenClipboard(hWnd)) {
		return NULL;
	}

	CJPEGImage* pImage = NULL;
	HANDLE handle = ::GetClipboardData(CF_DIB);
	if (handle != NULL) {
		BITMAPINFO* pbmInfo = (BITMAPINFO*)::GlobalLock(handle);
		if (pbmInfo != NULL) {
			int nNumColors = pbmInfo->bmiHeader.biClrUsed;
			if (nNumColors == 0 && pbmInfo->bmiHeader.biBitCount <= 8) {
				nNumColors = 1 << pbmInfo->bmiHeader.biBitCount;
			}
			if (pbmInfo->bmiHeader.biCompression == BI_BITFIELDS) {
				nNumColors = 3;
			}
			char* pDIBBits = (char*)pbmInfo + pbmInfo->bmiHeader.biSize + nNumColors*sizeof(RGBQUAD);

			if (pbmInfo->bmiHeader.biWidth <= MAX_IMAGE_DIMENSION && abs(pbmInfo->bmiHeader.biHeight) <= MAX_IMAGE_DIMENSION) {
				Gdiplus::Bitmap* pBitmap = new Gdiplus::Bitmap(pbmInfo, pDIBBits);
				if (pBitmap->GetLastStatus() == Gdiplus::Ok) {
					Gdiplus::Rect bmRect(0, 0, pBitmap->GetWidth(), pBitmap->GetHeight());
					Gdiplus::BitmapData bmData;
					if (pBitmap->LockBits(&bmRect, Gdiplus::ImageLockModeRead, PixelFormat32bppRGB, &bmData) == Gdiplus::Ok) {
						assert(bmData.PixelFormat == PixelFormat32bppRGB);
						void* pDIB = CBasicProcessing::ConvertGdiplus32bppRGB(bmRect.Width, bmRect.Height, bmData.Stride, bmData.Scan0);
						pImage = (pDIB == NULL) ? NULL : new CJPEGImage(bmRect.Width, bmRect.Height, pDIB, NULL, 4, 0, IF_CLIPBOARD, false, 0, 1, 0);
						pBitmap->UnlockBits(&bmData);
					}
				}
				delete pBitmap;
			}

			::GlobalUnlock(handle);
		}
	}

	::CloseClipboard();

	return pImage;
}


void CClipboard::DoCopy(HWND hWnd, int nWidth, int nHeight, const void* pSourceImageDIB32, LPCTSTR fileName) {
	if (!::OpenClipboard(hWnd)) {
		return;
	}
	::EmptyClipboard();

	// get needed size of memory block
	uint32 nSizeLinePadded = Helpers::DoPadding(nWidth*3, 4);
	uint32 nSizeBytes = sizeof(BITMAPINFO) + nSizeLinePadded * nHeight;
	
	// Allocate memory
	HGLOBAL hMem = ::GlobalAlloc(GMEM_MOVEABLE, nSizeBytes);
	if (hMem == NULL) {
		::CloseClipboard();
		return;
	}
	void* pMemory = ::GlobalLock(hMem); 

	BITMAPINFO* pBMInfo = (BITMAPINFO*) pMemory;
	memset(pBMInfo, 0, sizeof(BITMAPINFO));

	pBMInfo->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	pBMInfo->bmiHeader.biWidth = nWidth;
	pBMInfo->bmiHeader.biHeight = nHeight;
	pBMInfo->bmiHeader.biPlanes = 1;
	pBMInfo->bmiHeader.biBitCount = 24;
	pBMInfo->bmiHeader.biCompression = BI_RGB;
	pBMInfo->bmiHeader.biXPelsPerMeter = 10000;
	pBMInfo->bmiHeader.biYPelsPerMeter = 10000;
	pBMInfo->bmiHeader.biClrUsed = 0;

	uint8* pDIBPixelsTarget = (uint8*)pMemory + sizeof(BITMAPINFO) - sizeof(RGBQUAD);
	CBasicProcessing::Convert32bppTo24bppDIB(nWidth, nHeight, pDIBPixelsTarget, pSourceImageDIB32, true);

	::GlobalUnlock(hMem); 
	::SetClipboardData(CF_DIB, hMem);

	if (fileName != NULL) {
		CopyOriginalFileNameToClipboard(fileName);
	}

	::CloseClipboard();
}

void CClipboard::DoCopyFileNameText(HWND hWnd, LPCTSTR fileName) {
	if (!::OpenClipboard(hWnd)) {
		return;
	}
	::EmptyClipboard();

	CopyFileNameTextToClipboard(fileName);

	::CloseClipboard();
}