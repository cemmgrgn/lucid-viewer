#pragma once

// Settings window (Tools > Settings...) with all settings of the INI file.
// The settings, their categories, descriptions and possible values are read from the global INI file with its comments,
// changed values are written to the user INI file.
#include <vector>

namespace SettingsEditor {
	struct CResult {
		bool Saved;
		bool KeyMapChanged;
		std::vector<CString> ChangedKeys;
	};

	// Shows the window modal to hParent. Returns true if settings or keyboard shortcuts were saved.
	bool Show(HWND hParent, CResult& result);

	// Window with all commands and their keyboard shortcuts, they can be changed. Returns true if the key map was saved.
	bool EditShortcuts(HWND hParent);

	// UI test: category shown first in the settings window
	void SetSnapshotCategory(int nCategory);

	// Starts Lucid Viewer again with the file and closes the window hParent
	void Restart(HWND hParent, LPCTSTR sCurrentFile);
}
