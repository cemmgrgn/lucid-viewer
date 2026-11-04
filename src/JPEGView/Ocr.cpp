// Compiled without the precompiled header: C++/WinRT needs C++17 and its own Windows includes
#define NOMINMAX
#include <windows.h>
#include <unknwn.h>
#include <thread>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#include "Ocr.h"

#pragma comment(lib, "windowsapp")

using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Media::Ocr;

namespace Ocr {

	bool RecognizeText(const void* pPixels, int nWidth, int nHeight, int nChannels, std::wstring& sText, std::wstring& sError) {
		bool bSuccess = false;
		// the asynchronous WinRT calls must not block the UI thread (STA), they run in a multithreaded apartment
		std::thread worker([&]() {
			try {
				winrt::init_apartment(winrt::apartment_type::multi_threaded);
				OcrEngine engine = OcrEngine::TryCreateFromUserProfileLanguages();
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
					for (int y = 0; y < nTargetHeight; y++) {
						const uint8_t* pRow = pSource + (size_t)y * nStep * nSourceStride;
						for (int x = 0; x < nTargetWidth; x++) {
							const uint8_t* pPixel = pRow + (size_t)x * nStep * nChannels;
							*pTarget++ = pPixel[0];
							*pTarget++ = pPixel[1];
							*pTarget++ = pPixel[2];
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
