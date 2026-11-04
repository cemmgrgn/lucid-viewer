#pragma once

// Settings window (Tools > Settings...) with all settings of the INI file.
// The settings, their categories, descriptions and possible values are read from the global INI file with its comments,
// changed values are written to the user INI file.
namespace SettingsEditor {
	// Shows the window modal to hParent. sCurrentFile is opened again if JPEGView is restarted.
	// Returns true if settings were saved.
	bool Show(HWND hParent, LPCTSTR sCurrentFile);
}
