#pragma once

// Windows share window (Nearby Sharing, mail, apps) and dragging files out of the window
namespace Share {
	// Shows the Windows share window for the file, returns false if not available
	bool ShareFile(HWND hWnd, LPCWSTR sFileName);
	// Starts dragging the file to other applications (OLE drag and drop), returns after the drop
	bool DragFile(HWND hWnd, LPCWSTR sFileName);
}
