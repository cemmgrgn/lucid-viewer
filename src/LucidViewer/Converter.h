#pragma once

#include "ImageProcessingTypes.h"
#include <vector>

// Fast image format conversion, used by the Explorer context menu ('LucidViewer.exe /convert:<preset> <files>')
// and by the 'Convert to' menus inside Lucid Viewer. Converted files are written next to the source file, existing
// files are never overwritten.
namespace Converter {

	// Output format of a conversion preset
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
		OF_HEIC, // Windows HEIF encoder (WIC), needs the HEVC video extension
		OF_AVIF, // Windows HEIF encoder with AV1 compression
		OF_SameAsSource // keeps the source format if it can be written, JPEG otherwise
	};

	// A conversion preset is an output format combined with an optional operation (scaling, rotation, metadata removal).
	// Preset ids have the form '<format>' or '<operation>:<format>', e.g. 'png', 'scale50:jpg', 'rotl:same'.
	struct CPreset {
		CString Id;
		CString MenuText; // English menu text of the format, translated with CNLS
		int Operation; // index into the operations, 0 for plain conversion
		EOutputFormat Format;
		int ScalePercent; // 100 for original size
		int MaxLongEdge; // 0 for no limit, otherwise the long image edge is fit into this many pixels
		int Rotation; // additional rotation in degrees, 0, 90 or 270
		bool KeepMetadata; // copies EXIF data for JPEG output
	};

	// An operation groups presets in a sub menu, operation 0 (plain conversion) is the top level of the menu
	struct COperation {
		LPCTSTR Id; // prefix in the preset id
		LPCTSTR MenuText; // English, translated with CNLS
		bool SeparatorBefore;
	};

	// Gets all presets in menu order
	const std::vector<CPreset>& GetPresets();
	// Creates the presets again, after the user defined conversions of the INI file were changed
	void ReloadPresets();

	// Gets the operations (index 0 is the plain conversion without operation)
	const COperation* GetOperations(int& nCount);

	// Finds a preset by its id (old ids like 'scale50' or 'rotate-left' are accepted too), NULL if not found
	const CPreset* FindPreset(LPCTSTR sId);

	// Converts a single file. Returns true on success, sOutputFile receives the name of the written file
	// and sError the error message on failure.
	bool ConvertFile(LPCTSTR sSourceFile, const CPreset& preset, CString& sOutputFile, CString& sError);

	// Returns true if the command line requests a converter action (/convert, /registerconvertmenu, /unregisterconvertmenu)
	bool IsConverterCommandLine(LPCTSTR sCommandLine);

	// Runs the converter action given on the command line without showing the main window. Returns the process exit code.
	int RunCommandLine();

	// Creates a popup menu with all presets (operations as sub menus), the command ID of a preset is nFirstCommandId + preset index
	HMENU CreatePresetMenu(int nFirstCommandId);

	// Adds the 'Convert to' submenu to the Explorer context menu of all supported image files (current user only)
	bool RegisterExplorerMenu();

	// Removes the 'Convert to' submenu from the Explorer context menu
	bool UnregisterExplorerMenu();

	// Returns if the Explorer context menu is registered
	bool IsExplorerMenuRegistered();
}
