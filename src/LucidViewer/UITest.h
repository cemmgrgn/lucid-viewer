#pragma once

// Pictures of the windows for testing without the screen:
// LucidViewer.exe /uitest <settings|shortcuts|print|gallery> <dark|light> <output.png> [page]
// The window is opened outside of the visible screen without activating it, saved as PNG and closed.
namespace UITest {
	bool IsCommandLine(LPCTSTR sCommandLine);
	int Run(LPCTSTR sCommandLine);
}
