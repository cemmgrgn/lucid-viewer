#pragma once

#include <string>
#include <vector>

// Text recognition with the OCR engine of Windows (Windows.Media.Ocr), no download needed.
// The languages are the OCR capable languages of the Windows user profile.
namespace Ocr {
	// Recognizes the text in the given BGR(A) pixels (3 or 4 channels, rows padded to 4 bytes for 3 channels).
	// Returns false on error with sError set, sText has one line per recognized text line.
	// A recognized word with its position in pixels of the given image
	struct CWord {
		std::wstring Text;
		float Left, Top, Width, Height;
		int Line; // index of the text line, words of a line are in reading order
	};

	// Recognizes the words with their positions (for selecting text in the image)
	bool RecognizeWords(const void* pPixels, int nWidth, int nHeight, int nChannels, std::vector<CWord>& words, std::wstring& sError);

	// Language tag (e.g. "tr") of the OCR engine, empty selects it automatically
	void SetPreferredLanguage(const wchar_t* sLanguage);
	bool RecognizeText(const void* pPixels, int nWidth, int nHeight, int nChannels, std::wstring& sText, std::wstring& sError);
}
