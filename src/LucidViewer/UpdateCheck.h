#pragma once

// Checks on request (Help menu) if a newer version of Lucid Viewer is published on GitHub.
// Nothing is checked automatically and nothing but the request for the latest release is sent.
namespace UpdateCheck {

	// Page with the latest release, opened when a newer version is found
	extern const LPCTSTR RELEASES_PAGE;

	// Starts the check in a background thread. When done, WM_UPDATE_CHECKED is posted to hWnd with a heap allocated
	// CString* in LPARAM (freed by the receiver): the version of the latest release, e.g. "1.2.0", empty on error.
	void StartCheck(HWND hWnd);

	// Compares two versions 'major.minor.patch' (a leading 'v' is ignored), returns <0, 0 or >0
	int CompareVersions(LPCTSTR sVersion1, LPCTSTR sVersion2);
}
