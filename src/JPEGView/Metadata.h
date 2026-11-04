#pragma once

#include <propsys.h>
#include <vector>

// All metadata of an image file as shown by Explorer (Windows property system), the editable ones can be changed
namespace Metadata {

	enum EGroup { GROUP_DESCRIPTION, GROUP_CAMERA, GROUP_IMAGE, GROUP_LOCATION, GROUP_OTHER, GROUP_COUNT };

	struct CEntry {
		PROPERTYKEY Key;
		CString Name;      // display name, translated by Windows
		CString Value;     // formatted for display, empty if the file has no value
		CString EditValue; // text shown in the edit window
		EGroup Group;
		int Order;         // sort order inside the group
		bool Writable;
		bool Present;      // the file has a value
	};

	// English name of the group, translate with CNLS
	LPCTSTR GetGroupName(EGroup eGroup);

	// Reads all metadata of the file, sorted by group, also empty entries for common editable properties
	void Read(LPCTSTR sFileName, std::vector<CEntry>& entries);

	// Shows the edit window for the entry and writes the new value, returns true if the file was changed
	bool Edit(HWND hParent, LPCTSTR sFileName, const CEntry& entry, CString& sError);

	// Removes the editable values (only the location if bLocationOnly), returns the number of removed values or -1 on error
	int Remove(LPCTSTR sFileName, const std::vector<CEntry>& entries, bool bLocationOnly, CString& sError);

	// Writes the values given as edit text (empty removes the value), returns false on error
	bool WriteTexts(LPCTSTR sFileName, const std::vector<std::pair<PROPERTYKEY, CString>>& values);

	// All entries with a value as text, one per line (for the clipboard)
	CString ToText(const std::vector<CEntry>& entries);
}
