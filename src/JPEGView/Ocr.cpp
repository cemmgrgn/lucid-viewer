// Compiled without the precompiled header: C++/WinRT needs C++17 and its own Windows includes
#define NOMINMAX
#include <windows.h>
#include <unknwn.h>
#include <thread>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#include "Ocr.h"

#pragma comment(lib, "windowsapp")

using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Media::Ocr;

namespace Ocr {

	static std::wstring s_sPreferredLanguage;

	void SetPreferredLanguage(const wchar_t* sLanguage) {
		s_sPreferredLanguage = (sLanguage != nullptr) ? sLanguage : L"";
	}

	static OcrEngine TryCreate(const std::wstring& sTag) {
		try {
			winrt::Windows::Globalization::Language language(sTag);
			if (OcrEngine::IsLanguageSupported(language)) {
				return OcrEngine::TryCreateFromLanguage(language);
			}
		} catch (...) {
		}
		return nullptr;
	}

	// Engine of the language from the INI file, else of a non English keyboard layout (the language the user writes),
	// else of the Windows display languages. Each engine knows only one language, the Turkish one also reads English text.
	static OcrEngine CreateEngine() {
		if (!s_sPreferredLanguage.empty()) {
			OcrEngine engine = TryCreate(s_sPreferredLanguage);
			if (engine != nullptr) return engine;
		}
		HKL layouts[32];
		int nLayouts = ::GetKeyboardLayoutList(32, layouts);
		for (int i = 0; i < nLayouts; i++) {
			wchar_t sName[LOCALE_NAME_MAX_LENGTH] = { 0 };
			if (::LCIDToLocaleName(MAKELCID(LOWORD((ULONG_PTR)layouts[i]), SORT_DEFAULT), sName, LOCALE_NAME_MAX_LENGTH, 0) == 0) continue;
			std::wstring sTag(sName);
			if (sTag.compare(0, 2, L"en") == 0) continue;
			OcrEngine engine = TryCreate(sTag);
			if (engine == nullptr && sTag.find(L'-') != std::wstring::npos) engine = TryCreate(sTag.substr(0, sTag.find(L'-')));
			if (engine != nullptr) return engine;
		}
		return OcrEngine::TryCreateFromUserProfileLanguages();
	}

	bool RecognizeText(const void* pPixels, int nWidth, int nHeight, int nChannels, std::wstring& sText, std::wstring& sError) {
		bool bSuccess = false;
		// the asynchronous WinRT calls must not block the UI thread (STA), they run in a multithreaded apartment
		std::thread worker([&]() {
			try {
				winrt::init_apartment(winrt::apartment_type::multi_threaded);
				OcrEngine engine = CreateEngine();
				if (engine == nullptr) {
					sError = L"No OCR language is installed in Windows.";
				} else {
					// the engine has a maximal image size, larger images are reduced by an integer factor
					int nMaxDimension = (int)OcrEngine::MaxImageDimension();
					int nStep = 1;
					while (nWidth / nStep > nMaxDimension || nHeight / nStep > nMaxDimension) {
						nStep++;
					}
					int nTargetWidth = nWidth / nStep, nTargetHeight = nHeight / nStep;
					int nSourceStride = (nChannels == 4) ? nWidth * 4 : ((nWidth * 3 + 3) & ~3);
					winrt::Windows::Storage::Streams::Buffer buffer((uint32_t)nTargetWidth * nTargetHeight * 4);
					buffer.Length(buffer.Capacity());
					uint8_t* pTarget = buffer.data();
					const uint8_t* pSource = (const uint8_t*)pPixels;
					// light text on a dark background (dark themes) is recognized better inverted
					uint64_t nSum = 0, nCount = 0;
					for (int y = 0; y < nHeight; y += 4) {
						const uint8_t* pRow = pSource + (size_t)y * nSourceStride;
						for (int x = 0; x < nWidth; x += 4) {
							const uint8_t* pPixel = pRow + (size_t)x * nChannels;
							nSum += (pPixel[0] * 29 + pPixel[1] * 150 + pPixel[2] * 77) >> 8;
							nCount++;
						}
					}
					uint8_t nInvert = (nCount > 0 && nSum / nCount < 110) ? 255 : 0;
					for (int y = 0; y < nTargetHeight; y++) {
						const uint8_t* pRow = pSource + (size_t)y * nStep * nSourceStride;
						for (int x = 0; x < nTargetWidth; x++) {
							const uint8_t* pPixel = pRow + (size_t)x * nStep * nChannels;
							*pTarget++ = pPixel[0] ^ nInvert;
							*pTarget++ = pPixel[1] ^ nInvert;
							*pTarget++ = pPixel[2] ^ nInvert;
							*pTarget++ = 255;
						}
					}
					SoftwareBitmap bitmap = SoftwareBitmap::CreateCopyFromBuffer(buffer, BitmapPixelFormat::Bgra8, nTargetWidth, nTargetHeight, BitmapAlphaMode::Premultiplied);
					OcrResult result = engine.RecognizeAsync(bitmap).get();
					for (OcrLine const& line : result.Lines()) {
						sText += line.Text();
						sText += L"\r\n";
					}
					bSuccess = true;
				}
			} catch (winrt::hresult_error const& error) {
				sError = error.message();
			} catch (...) {
				sError = L"Text recognition failed.";
			}
			winrt::uninit_apartment();
		});
		worker.join();
		return bSuccess;
	}
}
