#pragma once

// Windows share window (Nearby Sharing, mail, apps) and dragging files out of the window
namespace Share {
	// Shows the Windows share window for the file, returns false if not available
	bool ShareFile(HWND hWnd, LPCWSTR sFileName);
	// Starts dragging the file to other applications (OLE drag and drop), returns after the drop
	bool DragFile(HWND hWnd, LPCWSTR sFileName);
	// Thumbnail of the file from the Windows thumbnail cache (as shown by Explorer), NULL on error. The caller deletes the bitmap.
	HBITMAP GetThumbnail(LPCWSTR sFileName, int nSize);
	// Uses the image file as picture of the Windows lock screen, returns false if Windows refuses it
	bool SetLockScreenImage(LPCWSTR sFileName);
}
