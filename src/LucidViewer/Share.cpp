// Compiled without the precompiled header: C++/WinRT needs C++20 and its own Windows includes
#define NOMINMAX
#include <windows.h>
#include <unknwn.h>
#include <shobjidl_core.h>
#include <shlobj_core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.UserProfile.h>
#include <thread>
#include <string>
#include "Share.h"

#pragma comment(lib, "windowsapp")

using namespace winrt::Windows::ApplicationModel::DataTransfer;
using namespace winrt::Windows::Storage;

namespace Share {

	static DataTransferManager s_manager{ nullptr };
	static winrt::event_token s_token;
	static std::wstring s_sFileName;

	bool ShareFile(HWND hWnd, LPCWSTR sFileName) {
		try {
			auto interop = winrt::get_activation_factory<DataTransferManager, IDataTransferManagerInterop>();
			if (s_manager == nullptr) {
				winrt::check_hresult(interop->GetForWindow(hWnd, winrt::guid_of<DataTransferManager>(), winrt::put_abi(s_manager)));
				s_token = s_manager.DataRequested([](DataTransferManager const&, DataRequestedEventArgs const& args) {
					DataRequest request = args.Request();
					DataRequestDeferral deferral = request.GetDeferral();
					std::wstring sFile = s_sFileName;
					size_t nSlash = sFile.find_last_of(L'\\');
					request.Data().Properties().Title(winrt::hstring(nSlash == std::wstring::npos ? sFile : sFile.substr(nSlash + 1)));
					// the request is answered asynchronously, the UI thread must not wait
					StorageFile::GetFileFromPathAsync(sFile).Completed([request, deferral](auto const& operation, winrt::Windows::Foundation::AsyncStatus status) {
						if (status == winrt::Windows::Foundation::AsyncStatus::Completed) {
							request.Data().SetStorageItems({ operation.GetResults() });
						}
						deferral.Complete();
					});
				});
			}
			s_sFileName = sFileName;
			winrt::check_hresult(interop->ShowShareUIForWindow(hWnd));
			return true;
		} catch (...) {
			return false;
		}
	}

	HBITMAP GetThumbnail(LPCWSTR sFileName, int nSize) {
		HBITMAP hBitmap = NULL;
		IShellItemImageFactory* pFactory = NULL;
		if (SUCCEEDED(::SHCreateItemFromParsingName(sFileName, NULL, IID_PPV_ARGS(&pFactory)))) {
			SIZE size = { nSize, nSize };
			if (FAILED(pFactory->GetImage(size, SIIGBF_BIGGERSIZEOK | SIIGBF_THUMBNAILONLY, &hBitmap))) {
				hBitmap = NULL;
			}
			pFactory->Release();
		}
		return hBitmap;
	}

	bool SetLockScreenImage(LPCWSTR sFileName) {
		bool bSuccess = false;
		std::wstring sFile(sFileName);
		// the asynchronous calls are waited for in a thread of the multi-threaded apartment, not in the UI thread
		std::thread worker([&]() {
			try {
				winrt::init_apartment(winrt::apartment_type::multi_threaded);
				StorageFile file = StorageFile::GetFileFromPathAsync(sFile).get();
				winrt::Windows::System::UserProfile::LockScreen::SetImageFileAsync(file).get();
				bSuccess = true;
			} catch (...) {
				bSuccess = false;
			}
			winrt::uninit_apartment();
		});
		worker.join();
		return bSuccess;
	}

	bool DragFile(HWND hWnd, LPCWSTR sFileName) {
		bool bSuccess = false;
		::OleInitialize(NULL); // drag and drop needs OLE, does nothing if already initialized
		IShellItem* pItem = NULL;
		if (SUCCEEDED(::SHCreateItemFromParsingName(sFileName, NULL, IID_PPV_ARGS(&pItem)))) {
			IDataObject* pData = NULL;
			if (SUCCEEDED(pItem->BindToHandler(NULL, BHID_DataObject, IID_PPV_ARGS(&pData)))) {
				DWORD nEffect = 0;
				// with no drop source the shell provides the standard one
				bSuccess = SUCCEEDED(::SHDoDragDrop(hWnd, pData, NULL, DROPEFFECT_COPY | DROPEFFECT_LINK, &nEffect));
				pData->Release();
			}
			pItem->Release();
		}
		return bSuccess;
	}
}
