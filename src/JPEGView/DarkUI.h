#pragma once

// Colors and dark mode for the dialogs and tool windows (settings, shortcuts, print).
// Follows the ChromeTheme setting of the main window.
namespace DarkUI {

	struct CPalette {
		COLORREF Window;     // background of the window
		COLORREF Surface;    // background of content areas (settings page, lists)
		COLORREF Control;    // background of edit fields, buttons
		COLORREF ControlHot; // background of buttons under the mouse
		COLORREF Text;
		COLORREF SubText;    // descriptions
		COLORREF Border;
		COLORREF Accent;     // switches, default button, selection mark
		COLORREF AccentText; // text and switch knob on the accent color
		COLORREF Selection;  // selected list item
	};

	bool IsDark();
	// Forces the dark (1) or light (0) colors, -1 follows the setting (used by the UI test)
	void ForceTheme(int nDark);
	const CPalette& Colors();

	// Solid brush of the color, owned by DarkUI
	HBRUSH Brush(COLORREF color);

	// Dark title bar, scroll bars and control themes for the window and all its child windows
	void ApplyToWindow(HWND hWnd);
	// Theme of one control created later (dark scroll bars, edit fields, combo boxes)
	void ApplyToControl(HWND hControl);

	// Background color the buttons of this parent window are drawn on (default: Window color)
	void SetBackground(HWND hParent, COLORREF color);

	// Handles WM_CTLCOLORSTATIC, WM_CTLCOLOREDIT, WM_CTLCOLORLISTBOX, WM_CTLCOLORBTN and WM_CTLCOLORDLG.
	// Returns the brush or NULL if the message is none of them.
	HBRUSH CtlColor(UINT nMsg, WPARAM wParam, LPARAM lParam, COLORREF background, bool bSubText = false);

	// Handles WM_NOTIFY NM_CUSTOMDRAW of buttons: check boxes are drawn as switches, radio buttons and,
	// in dark mode, push buttons in the colors of the palette. Returns true if handled, the result is the return value.
	bool CustomDrawButton(LPARAM lParam, LRESULT& result);

	// UI test: if a snapshot file is set, ShowSnapshot() moves the window out of the screen, shows it without
	// activating it, saves it as PNG after a short time and closes it with the given command (WM_COMMAND)
	void SetSnapshotFile(LPCTSTR sFileName);
	bool IsSnapshotMode();
	void ShowSnapshot(HWND hWnd, int nCloseCommand);
}
