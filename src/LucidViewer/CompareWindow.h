#pragma once

// Two photos side by side in a window of their own (light table). The mouse wheel zooms both at the same place,
// dragging moves both. Returns when the window is closed (Esc or the close button).
namespace CompareWindow {
	void Show(HWND hParent, LPCTSTR sFile1, LPCTSTR sFile2);
}
