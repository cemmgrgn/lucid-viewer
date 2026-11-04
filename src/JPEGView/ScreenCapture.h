#pragma once

// Screenshot tool: frozen screen with a selection, drawing tools, copy, save, open in the viewer and text recognition (OCR).
// It runs in its own process: once (/capture, /capturetext) or resident in the notification area with global hotkeys (/tray).
namespace ScreenCapture {

	enum EMode { MODE_CAPTURE, MODE_TEXT };

	// True if the command line starts the screenshot tool (/tray, /capture, /capturetext)
	bool IsCommandLine(LPCTSTR sCommandLine);

	// Runs the screenshot tool as given on the command line, returns the exit code
	int RunCommandLine(LPCTSTR sCommandLine);

	// Starts the screenshot tool in a new process (from the viewer)
	void Start(EMode eMode);
	void StartTray();
}
