#include "StdAfx.h"
#include "UpdateCheck.h"
#include "MessageDef.h"
#include <urlmon.h>
#include <process.h>
#include <string>

#pragma comment(lib, "urlmon")

namespace UpdateCheck {

	const LPCTSTR RELEASES_PAGE = _T("https://github.com/cemmgrgn/lucid-viewer/releases/latest");

	static const LPCWSTR LATEST_RELEASE_API = L"https://api.github.com/repos/cemmgrgn/lucid-viewer/releases/latest";

	// Reads the whole response, at most 1 MB
	static bool Download(LPCWSTR sUrl, std::string& sContent) {
		IStream* pStream = NULL;
		if (FAILED(::URLOpenBlockingStreamW(NULL, sUrl, &pStream, 0, NULL)) || pStream == NULL) {
			return false;
		}
		char buffer[4096];
		ULONG nRead = 0;
		HRESULT hr;
		while (SUCCEEDED(hr = pStream->Read(buffer, sizeof(buffer), &nRead)) && nRead > 0 && sContent.size() < 1024 * 1024) {
			sContent.append(buffer, nRead);
			if (hr == S_FALSE) break;
		}
		pStream->Release();
		return !sContent.empty();
	}

	// "tag_name": "v1.2.0" -> 1.2.0
	static CString ParseTagName(const std::string& sJson) {
		size_t nKey = sJson.find("\"tag_name\"");
		if (nKey == std::string::npos) return CString();
		size_t nColon = sJson.find(':', nKey);
		if (nColon == std::string::npos) return CString();
		size_t nStart = sJson.find('"', nColon);
		if (nStart == std::string::npos) return CString();
		size_t nEnd = sJson.find('"', nStart + 1);
		if (nEnd == std::string::npos || nEnd - nStart > 64) return CString();
		CString sTag(sJson.substr(nStart + 1, nEnd - nStart - 1).c_str());
		sTag.TrimLeft(_T("vV"));
		// only digits and dots are accepted as version
		if (sTag.IsEmpty() || sTag.SpanIncluding(_T("0123456789.")) != sTag) return CString();
		return sTag;
	}

	struct CCheckParams {
		HWND hWnd;
	};

	static unsigned __stdcall CheckThread(void* pParam) {
		CCheckParams* pParams = (CCheckParams*)pParam;
		HRESULT hrCom = ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
		// a changing query string, a cached older answer is not used
		WCHAR sUrl[256];
		swprintf_s(sUrl, L"%s?nocache=%u", LATEST_RELEASE_API, ::GetTickCount());
		std::string sJson;
		CString* pVersion = new CString(Download(sUrl, sJson) ? ParseTagName(sJson) : CString());
		if (SUCCEEDED(hrCom)) ::CoUninitialize();
		if (!::PostMessage(pParams->hWnd, WM_UPDATE_CHECKED, 0, (LPARAM)pVersion)) {
			delete pVersion;
		}
		delete pParams;
		return 0;
	}

	void StartCheck(HWND hWnd) {
		CCheckParams* pParams = new CCheckParams;
		pParams->hWnd = hWnd;
		HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, CheckThread, pParams, 0, NULL);
		if (hThread == NULL) {
			delete pParams;
			::PostMessage(hWnd, WM_UPDATE_CHECKED, 0, (LPARAM)new CString());
			return;
		}
		::CloseHandle(hThread);
	}

	int CompareVersions(LPCTSTR sVersion1, LPCTSTR sVersion2) {
		CString s1(sVersion1), s2(sVersion2);
		s1.TrimLeft(_T("vV"));
		s2.TrimLeft(_T("vV"));
		int nPos1 = 0, nPos2 = 0;
		for (int nPart = 0; nPart < 4; nPart++) {
			CString sPart1 = (nPos1 >= 0) ? s1.Tokenize(_T("."), nPos1) : CString();
			CString sPart2 = (nPos2 >= 0) ? s2.Tokenize(_T("."), nPos2) : CString();
			int n1 = _ttoi(sPart1), n2 = _ttoi(sPart2);
			if (n1 != n2) return (n1 < n2) ? -1 : 1;
		}
		return 0;
	}
}
