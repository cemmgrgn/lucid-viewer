#pragma once

enum RegResult {
	Reg_Success,
	Reg_WarningNeedsChangeDACL,  // DACL needs to be changed to write the entry
	Reg_ErrorNeedsWriteDACLRights, // Right to change DACL is not present
	Reg_ErrorWriteKey, // Error writing the key
	Reg_ErrorChangeDACL, // Error changing the DACL
	Reg_ErrorProtectedByHash, // Key is protected by cryptographic hash (Windows 8 only)
	Reg_Internal
};

// Class to support registering file extensions in the Windows registry to be opened by Lucid Viewer
// This class only works for Windows <= Windows 7. For windows 8, the only way to register file extensions
// is to call the 'default programs' dialog from Windows.
class CFileExtensionsRegistry
{
public:
	CFileExtensionsRegistry();

	// Checks if Lucid Viewer is registered in HKEY_CURRENT_USER\Software\Classes\Applications
	bool IsAppRegistered() { return m_bIsAppRegistered; }

	// Register Lucid Viewer in HKEY_CURRENT_USER\Software\Classes\Applications
	bool RegisterApp();
	
	// Checks if all given list of extensions in the form "*.ext1;*.ext2" are opened by Lucid Viewer
	// outRegisteredInHKLM contains true if it's registered to Lucid Viewer but in HKLM registry root, not in HKCU
	bool CheckIfOpenedByApp(LPCTSTR sExtensionList, bool& outRegisteredInHKLM);

	// Registers Lucid Viewer as default viewer for the given extension in the form "*.ext1"
	RegResult RegisterFileExtension(LPCTSTR sExtension, bool bChangeDACLIfRequired);

	// Unregisters Lucid Viewer as default viewer for the given extension in the form "*.ext1"
	RegResult UnregisterFileExtension(LPCTSTR sExtension, bool bChangeDACLIfRequired);

private:
	bool m_bNewRegistryFormat;
	bool m_bIsWindows8;
	bool m_bIsAppRegistered;
};

// Class to register file extensions in windows 8. To do so, Lucid Viewer must be registered in RegisteredApplications
// and its capabilities (i.e. file extensions it supports) must be published in the registry.
// Finally the 'Default programs' dialog from Windows 8 needs to be called to perform the actual registration.
class CFileExtensionsRegistrationWindows8
{
public:
	CFileExtensionsRegistrationWindows8();

	// Register Lucid Viewer in HKEY_CURRENT_USER\Software\RegisteredApplications
	// Also publishes the capabilities and creates a ProgId for Lucid Viewer.
	// Everything is done under HKEY_CURRENT_USER
	bool RegisterApp();

	// Launches the Windows 8 application association dialog (Default programs)
	void LaunchApplicationAssociationDialog();

	// True if the setup has registered Lucid Viewer for all users (HKEY_LOCAL_MACHINE)
	static bool IsRegisteredForAllUsers();

	// Opens the default apps page of the Windows settings, on Windows 11 the page of Lucid Viewer
	void OpenDefaultAppsSettings(bool bForAllUsers);

	// Gets the key name of the last failed key to write
	CString GetLastFailedRegistryKey() { return m_lastFailedRegistryKey; }

private:
	CString m_lastFailedRegistryKey;

	bool WriteStringValue(LPCTSTR path, LPCTSTR keyName, LPCTSTR value);
};