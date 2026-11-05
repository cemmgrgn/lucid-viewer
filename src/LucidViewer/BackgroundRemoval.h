#pragma once

#include <string>
#include <vector>

// Removes the background of a photo with the U2-Net model (MIT license) and Windows ML (in Windows, uses the GPU with DirectML).
// The model (176 MB) is downloaded on first use into %LOCALAPPDATA%\LucidViewer\models.
namespace BackgroundRemoval {
	bool IsModelAvailable();
	// Downloads the model with a progress window, returns false if cancelled or failed
	bool DownloadModel(HWND hParent, LPCWSTR sTitle, LPCWSTR sCancel);
	// Computes the foreground mask (255: foreground, 0: background) for BGR(A) pixels (3 or 4 channels, rows padded to 4 bytes for 3 channels)
	bool ComputeMask(const void* pPixels, int nWidth, int nHeight, int nChannels, std::vector<unsigned char>& mask, std::wstring& sError);
}
