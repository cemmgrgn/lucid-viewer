// Compiled without the precompiled header: C++/WinRT needs C++20 and its own Windows includes
#define NOMINMAX
#include <windows.h>
#include <unknwn.h>
#include <urlmon.h>
#include <shlobj_core.h>
#include <commctrl.h>
#include <thread>
#include <atomic>
#include <algorithm>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.AI.MachineLearning.h>
#include "BackgroundRemoval.h"

#pragma comment(lib, "windowsapp")
#pragma comment(lib, "urlmon")

using namespace winrt::Windows::AI::MachineLearning;

namespace BackgroundRemoval {

	static const wchar_t* MODEL_URL = L"https://github.com/danielgatis/rembg/releases/download/v0.0.0/u2net.onnx";
	static const int MODEL_SIZE = 320; // input size of U2-Net

	static std::wstring GetModelPath() {
		// The installer puts the model next to the program (models\u2net.onnx), use that copy when it is there
		wchar_t sExePath[MAX_PATH];
		DWORD nLen = ::GetModuleFileNameW(NULL, sExePath, MAX_PATH);
		if (nLen > 0 && nLen < MAX_PATH) {
			std::wstring sInstalled(sExePath);
			sInstalled = sInstalled.substr(0, sInstalled.find_last_of(L'\\') + 1) + L"models\\u2net.onnx";
			WIN32_FILE_ATTRIBUTE_DATA data;
			if (::GetFileAttributesExW(sInstalled.c_str(), GetFileExInfoStandard, &data) && (data.nFileSizeLow > 1000000 || data.nFileSizeHigh > 0)) {
				return sInstalled;
			}
		}
		wchar_t* sLocalAppData = NULL;
		std::wstring sPath;
		if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &sLocalAppData))) {
			sPath = std::wstring(sLocalAppData) + L"\\JPEGView\\models\\u2net.onnx";
			::CoTaskMemFree(sLocalAppData);
		}
		return sPath;
	}

	bool IsModelAvailable() {
		std::wstring sPath = GetModelPath();
		WIN32_FILE_ATTRIBUTE_DATA data;
		return !sPath.empty() && ::GetFileAttributesExW(sPath.c_str(), GetFileExInfoStandard, &data) && data.nFileSizeLow + ((ULONGLONG)data.nFileSizeHigh << 32) > 1000000;
	}

	// Download progress, the UI thread shows it
	class CDownloadCallback : public IBindStatusCallback {
	public:
		std::atomic<ULONG> Progress{ 0 }, Total{ 0 };
		std::atomic<bool> Cancel{ false };
		STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
			if (riid == IID_IUnknown || riid == IID_IBindStatusCallback) { *ppv = this; return S_OK; }
			*ppv = NULL;
			return E_NOINTERFACE;
		}
		STDMETHODIMP_(ULONG) AddRef() override { return 1; }
		STDMETHODIMP_(ULONG) Release() override { return 1; }
		STDMETHODIMP OnStartBinding(DWORD, IBinding*) override { return S_OK; }
		STDMETHODIMP GetPriority(LONG*) override { return E_NOTIMPL; }
		STDMETHODIMP OnLowResource(DWORD) override { return S_OK; }
		STDMETHODIMP OnProgress(ULONG ulProgress, ULONG ulProgressMax, ULONG, LPCWSTR) override {
			Progress = ulProgress;
			Total = ulProgressMax;
			return Cancel ? E_ABORT : S_OK;
		}
		STDMETHODIMP OnStopBinding(HRESULT, LPCWSTR) override { return S_OK; }
		STDMETHODIMP GetBindInfo(DWORD*, BINDINFO*) override { return S_OK; }
		STDMETHODIMP OnDataAvailable(DWORD, DWORD, FORMATETC*, STGMEDIUM*) override { return S_OK; }
		STDMETHODIMP OnObjectAvailable(REFIID, IUnknown*) override { return S_OK; }
	};

	static LRESULT CALLBACK ProgressWndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
		CDownloadCallback* pCallback = (CDownloadCallback*)::GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if (nMsg == WM_NCCREATE) {
			::SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCT*)lParam)->lpCreateParams);
		} else if ((nMsg == WM_COMMAND && LOWORD(wParam) == IDCANCEL) || nMsg == WM_CLOSE) {
			if (pCallback != NULL) pCallback->Cancel = true;
			return 0;
		}
		return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
	}

	bool DownloadModel(HWND hParent, LPCWSTR sTitle, LPCWSTR sCancel) {
		std::wstring sPath = GetModelPath();
		if (sPath.empty()) return false;
		std::wstring sFolder = sPath.substr(0, sPath.find_last_of(L'\\'));
		::SHCreateDirectoryExW(NULL, sFolder.c_str(), NULL);
		std::wstring sTemp = sPath + L".part";

		HINSTANCE hInstance = ::GetModuleHandle(NULL);
		WNDCLASSW wndClass{ 0 };
		wndClass.lpfnWndProc = ProgressWndProc;
		wndClass.hInstance = hInstance;
		wndClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
		wndClass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
		wndClass.lpszClassName = L"JPEGViewModelDownload";
		::RegisterClassW(&wndClass);
		INITCOMMONCONTROLSEX initControls = { sizeof(INITCOMMONCONTROLSEX), ICC_PROGRESS_CLASS };
		::InitCommonControlsEx(&initControls);

		CDownloadCallback callback;
		HDC hDC = ::GetDC(NULL);
		int nDPI = ::GetDeviceCaps(hDC, LOGPIXELSX);
		::ReleaseDC(NULL, hDC);
		auto S = [nDPI](int n) { return ::MulDiv(n, nDPI, 96); };
		RECT parentRect;
		::GetWindowRect(hParent, &parentRect);
		int nW = S(420), nH = S(150);
		HWND hWnd = ::CreateWindowExW(WS_EX_DLGMODALFRAME, L"JPEGViewModelDownload", sTitle, WS_POPUP | WS_CAPTION | WS_SYSMENU,
			(parentRect.left + parentRect.right - nW) / 2, (parentRect.top + parentRect.bottom - nH) / 2, nW, nH, hParent, NULL, hInstance, &callback);
		NONCLIENTMETRICSW metrics{ 0 };
		metrics.cbSize = sizeof(metrics);
		::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
		HFONT hFont = ::CreateFontIndirectW(&metrics.lfMessageFont);
		HWND hText = ::CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, S(12), S(12), S(390), S(20), hWnd, NULL, hInstance, NULL);
		HWND hBar = ::CreateWindowW(PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE, S(12), S(40), S(390), S(18), hWnd, NULL, hInstance, NULL);
		HWND hButton = ::CreateWindowW(L"BUTTON", sCancel, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, S(308), S(72), S(94), S(28), hWnd, (HMENU)IDCANCEL, hInstance, NULL);
		::SendMessage(hText, WM_SETFONT, (WPARAM)hFont, FALSE);
		::SendMessage(hButton, WM_SETFONT, (WPARAM)hFont, FALSE);
		::SendMessage(hBar, PBM_SETRANGE32, 0, 1000);
		::EnableWindow(hParent, FALSE);
		::ShowWindow(hWnd, SW_SHOWNORMAL);

		std::atomic<bool> bFinished{ false };
		HRESULT hrDownload = E_FAIL;
		std::thread worker([&]() {
			hrDownload = ::URLDownloadToFileW(NULL, MODEL_URL, sTemp.c_str(), 0, &callback);
			bFinished = true;
		});
		while (!bFinished) {
			MSG msg;
			while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
				if (!::IsDialogMessage(hWnd, &msg)) {
					::TranslateMessage(&msg);
					::DispatchMessage(&msg);
				}
			}
			ULONG nTotal = callback.Total, nProgress = callback.Progress;
			wchar_t sText[128];
			swprintf_s(sText, L"%.0f / %.0f MB", nProgress / 1048576.0, nTotal / 1048576.0);
			::SetWindowTextW(hText, sText);
			::SendMessage(hBar, PBM_SETPOS, nTotal > 0 ? (WPARAM)((ULONGLONG)nProgress * 1000 / nTotal) : 0, 0);
			::MsgWaitForMultipleObjects(0, NULL, FALSE, 100, QS_ALLINPUT);
		}
		worker.join();
		::EnableWindow(hParent, TRUE);
		::DestroyWindow(hWnd);
		::DeleteObject(hFont);
		::SetActiveWindow(hParent);
		if (FAILED(hrDownload) || callback.Cancel) {
			::DeleteFileW(sTemp.c_str());
			return false;
		}
		::DeleteFileW(sPath.c_str());
		return ::MoveFileW(sTemp.c_str(), sPath.c_str()) != FALSE;
	}

	bool ComputeMask(const void* pPixels, int nWidth, int nHeight, int nChannels, std::vector<unsigned char>& mask, std::wstring& sError) {
		bool bSuccess = false;
		std::thread worker([&]() {
			try {
				winrt::init_apartment(winrt::apartment_type::multi_threaded);
				LearningModel model = LearningModel::LoadFromFilePath(GetModelPath());
				LearningModelSession session{ nullptr };
				try {
					session = LearningModelSession(model, LearningModelDevice(LearningModelDeviceKind::DirectX));
				} catch (...) {
					session = LearningModelSession(model, LearningModelDevice(LearningModelDeviceKind::Cpu));
				}
				// input: RGB, 320 x 320, normalized with the ImageNet mean and standard deviation (as rembg does)
				const float mean[3] = { 0.485f, 0.456f, 0.406f }, stddev[3] = { 0.229f, 0.224f, 0.225f };
				int nStride = (nChannels == 4) ? nWidth * 4 : ((nWidth * 3 + 3) & ~3);
				const unsigned char* pSource = (const unsigned char*)pPixels;
				std::vector<float> input(3 * MODEL_SIZE * MODEL_SIZE);
				for (int y = 0; y < MODEL_SIZE; y++) {
					int nSourceY = std::min(nHeight - 1, (int)((y + 0.5) * nHeight / MODEL_SIZE));
					for (int x = 0; x < MODEL_SIZE; x++) {
						int nSourceX = std::min(nWidth - 1, (int)((x + 0.5) * nWidth / MODEL_SIZE));
						const unsigned char* p = pSource + (size_t)nSourceY * nStride + (size_t)nSourceX * nChannels;
						float rgb[3] = { p[2] / 255.0f, p[1] / 255.0f, p[0] / 255.0f };
						for (int c = 0; c < 3; c++) {
							input[(size_t)c * MODEL_SIZE * MODEL_SIZE + (size_t)y * MODEL_SIZE + x] = (rgb[c] - mean[c]) / stddev[c];
						}
					}
				}
				std::vector<int64_t> shape = { 1, 3, MODEL_SIZE, MODEL_SIZE };
				TensorFloat tensor = TensorFloat::CreateFromArray(shape, input);
				LearningModelBinding binding(session);
				binding.Bind(model.InputFeatures().GetAt(0).Name(), tensor);
				LearningModelEvaluationResult result = session.Evaluate(binding, L"");
				TensorFloat output = result.Outputs().Lookup(model.OutputFeatures().GetAt(0).Name()).as<TensorFloat>();
				auto values = output.GetAsVectorView();
				if (values.Size() < (uint32_t)(MODEL_SIZE * MODEL_SIZE)) {
					sError = L"Unexpected model output.";
					return;
				}
				std::vector<float> prediction(MODEL_SIZE * MODEL_SIZE);
				float fMin = 1e9f, fMax = -1e9f;
				for (int i = 0; i < MODEL_SIZE * MODEL_SIZE; i++) {
					prediction[i] = values.GetAt(i);
					fMin = std::min(fMin, prediction[i]);
					fMax = std::max(fMax, prediction[i]);
				}
				float fRange = std::max(1e-6f, fMax - fMin);
				// bilinear scaling of the prediction to the image size
				mask.resize((size_t)nWidth * nHeight);
				for (int y = 0; y < nHeight; y++) {
					float fY = std::max(0.0f, (y + 0.5f) * MODEL_SIZE / nHeight - 0.5f);
					int y0 = std::min(MODEL_SIZE - 1, (int)fY), y1 = std::min(MODEL_SIZE - 1, y0 + 1);
					float wy = fY - y0;
					for (int x = 0; x < nWidth; x++) {
						float fX = std::max(0.0f, (x + 0.5f) * MODEL_SIZE / nWidth - 0.5f);
						int x0 = std::min(MODEL_SIZE - 1, (int)fX), x1 = std::min(MODEL_SIZE - 1, x0 + 1);
						float wx = fX - x0;
						float v = (prediction[y0 * MODEL_SIZE + x0] * (1 - wx) + prediction[y0 * MODEL_SIZE + x1] * wx) * (1 - wy) +
							(prediction[y1 * MODEL_SIZE + x0] * (1 - wx) + prediction[y1 * MODEL_SIZE + x1] * wx) * wy;
						mask[(size_t)y * nWidth + x] = (unsigned char)std::clamp((v - fMin) / fRange * 255.0f + 0.5f, 0.0f, 255.0f);
					}
				}
				bSuccess = true;
			} catch (winrt::hresult_error const& error) {
				sError = error.message();
			} catch (...) {
				sError = L"Background removal failed.";
			}
			winrt::uninit_apartment();
		});
		worker.join();
		return bSuccess;
	}
}
