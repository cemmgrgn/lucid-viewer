// Version of Lucid Viewer, used by the version resources of LucidViewer.exe and WICLoader.dll.
// Every signed file of a release must carry the same product name and version (SignPath metadata restrictions).
#ifndef LUCID_VERSION_H
#define LUCID_VERSION_H

#define LUCID_VERSION_MAJOR 1
#define LUCID_VERSION_MINOR 1
#define LUCID_VERSION_PATCH 0

#define LUCID_STR2(x) #x
#define LUCID_STR(x) LUCID_STR2(x)
#define LUCID_VERSION_RC LUCID_VERSION_MAJOR,LUCID_VERSION_MINOR,LUCID_VERSION_PATCH,0
#define LUCID_VERSION_STR LUCID_STR(LUCID_VERSION_MAJOR) "." LUCID_STR(LUCID_VERSION_MINOR) "." LUCID_STR(LUCID_VERSION_PATCH) ".0"
#define LUCID_PRODUCT_NAME "Lucid Viewer"
#define LUCID_COMPANY_NAME "Cem Girgin"
// major.minor.patch, shown in the About dialog
#define LUCID_VERSION_SHORT LUCID_STR(LUCID_VERSION_MAJOR) "." LUCID_STR(LUCID_VERSION_MINOR) "." LUCID_STR(LUCID_VERSION_PATCH)

#endif
