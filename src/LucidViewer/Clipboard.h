#pragma once

#include "ProcessParams.h"

class CJPEGImage;

// Allows to copy an image or text to the clipboard and to paste an image from the clipboard
class CClipboard {
public:
	// Copy the pixels of the currently visible part of the image as a 24 bpp DIB to the clipboard
	// Zoom is as on screen.
	static void CopyImageToClipboard(HWND hWnd, CJPEGImage * pImage, LPCTSTR fileName);

	// Copy processed full size image as a 24 bpp DIB to the clipboard, i.e. in original resolution.
	static void CopyFullImageToClipboard(HWND hWnd, CJPEGImage * pImage, const CImageProcessingParams& procParams,
		EProcessingFlags eFlags, LPCTSTR fileName);

	// Copy section of full size image as a 24 bpp DIB to the clipboard, i.e. in original resolution.
	static void CopyFullImageToClipboard(HWND hWnd, CJPEGImage * pImage, const CImageProcessingParams& procParams,
		EProcessingFlags eFlags, CRect clipRect, LPCTSTR fileName);

	// Copy file path to the clipboard.
	static void CopyPathToClipboard(HWND hWnd, CJPEGImage* pImage, LPCTSTR fileName);

	// Copy the file itself to the clipboard (as Explorer does with Ctrl+C), it can be pasted in Explorer, mails and chats.
	// The path is put on the clipboard as text as well. Returns false on error.
	static bool CopyFileToClipboard(HWND hWnd, LPCTSTR fileName);

	// Paste image from clipboard. Returns NULL if paste failed. The caller gets ownership of the returned image.
	static CJPEGImage* PasteImageFromClipboard(HWND hWnd, const CImageProcessingParams& procParams, EProcessingFlags eFlags);

private:
	CClipboard(void);

	static void DoCopy(HWND hWnd, int nWidth, int nHeight, const void* pSourceImageDIB32, LPCTSTR fileName);
	static void DoCopyFileNameText(HWND hWnd, LPCTSTR fileName);
};
