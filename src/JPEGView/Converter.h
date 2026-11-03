#pragma once

#include "ImageProcessingTypes.h"

// Fast image format conversion, used by the Explorer context menu ('JPEGView.exe /convert:<preset> <files>')
// and by the 'Convert to' menu inside JPEGView. Converted files are written next to the source file, existing
// files are never overwritten.
namespace Converter {

	// Output of a conversion preset
	enum EOutputFormat {
		OF_JPEG,
		OF_PNG,
		OF_WEBP,
		OF_WEBP_LOSSLESS,
		OF_BMP,
		OF_TIFF,
		OF_GIF,
		OF_ICO,
		OF_PDF,
		OF_SameAsSource // keeps the source format if it can be written, JPEG otherwise
	};

	struct CPreset {
		LPCTSTR Id; // used on the command line, e.g. 'png' for /convert:png
		LPCTSTR MenuText; // English menu text, translated with CNLS
		EOutputFormat Format;
		int ScalePercent; // 100 for original size
		int Rotation; // additional rotation in degrees, 0, 90 or 270
		bool SeparatorBefore; // menu separator before this entry
	};

	// Gets all presets in menu order
	const CPreset* GetPresets(int& nCount);

	// Finds a preset by its id, NULL if not found
	const CPreset* FindPreset(LPCTSTR sId);

	// Converts a single file. Returns true on success, sOutputFile receives the name of the written file
	// and sError the error message on failure.
	bool ConvertFile(LPCTSTR sSourceFile, const CPreset& preset, CString& sOutputFile, CString& sError);

	// Returns true if the command line requests a converter action (/convert, /registerconvertmenu, /unregisterconvertmenu)
	bool IsConverterCommandLine(LPCTSTR sCommandLine);

	// Runs the converter action given on the command line without showing the main window. Returns the process exit code.
	int RunCommandLine();

	// Adds a popup menu with all presets to hMenu, the command IDs start at nFirstCommandId
	HMENU CreatePresetMenu(int nFirstCommandId);

	// Adds the 'Convert to' submenu to the Explorer context menu of all supported image files (current user only)
	bool RegisterExplorerMenu();

	// Removes the 'Convert to' submenu from the Explorer context menu
	bool UnregisterExplorerMenu();

	// Returns if the Explorer context menu is registered
	bool IsExplorerMenuRegistered();
}
