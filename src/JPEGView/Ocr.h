#pragma once

#include <string>

// Text recognition with the OCR engine of Windows (Windows.Media.Ocr), no download needed.
// The languages are the OCR capable languages of the Windows user profile.
namespace Ocr {
	// Recognizes the text in the given BGR(A) pixels (3 or 4 channels, rows padded to 4 bytes for 3 channels).
	// Returns false on error with sError set, sText has one line per recognized text line.
	// Language tag (e.g. "tr") of the OCR engine, empty selects it automatically
	void SetPreferredLanguage(const wchar_t* sLanguage);
	bool RecognizeText(const void* pPixels, int nWidth, int nHeight, int nChannels, std::wstring& sText, std::wstring& sError);
}
