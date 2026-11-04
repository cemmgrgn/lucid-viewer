#pragma once

// Startup timing for profiling. Set the environment variable LUCID_TRACE=1 and every Mark() appends
// "<ms since process creation> <name>" to %TEMP%\lucid_trace.txt. Costs nothing when the variable is not set.
namespace StartupTrace {

	inline bool IsEnabled() {
		static int s_nEnabled = -1;
		if (s_nEnabled < 0) {
			TCHAR buff[8];
			s_nEnabled = ::GetEnvironmentVariable(_T("LUCID_TRACE"), buff, 8) > 0 ? 1 : 0;
		}
		return s_nEnabled == 1;
	}

	inline void Mark(LPCSTR sName) {
		if (!IsEnabled()) return;
		// first mark: time since process creation from the system clock, after that the performance counter
		static double s_dFirstMs = -1;
		static LARGE_INTEGER s_first, s_freq;
		LARGE_INTEGER counter;
		::QueryPerformanceCounter(&counter);
		if (s_dFirstMs < 0) {
			FILETIME creation, exitTime, kernel, user, now;
			::GetProcessTimes(::GetCurrentProcess(), &creation, &exitTime, &kernel, &user);
			::GetSystemTimeAsFileTime(&now);
			ULONGLONG nCreation = ((ULONGLONG)creation.dwHighDateTime << 32) | creation.dwLowDateTime;
			ULONGLONG nNow = ((ULONGLONG)now.dwHighDateTime << 32) | now.dwLowDateTime;
			s_dFirstMs = (nNow - nCreation) / 10000.0;
			s_first = counter;
			::QueryPerformanceFrequency(&s_freq);
		}
		double dMs = s_dFirstMs + (counter.QuadPart - s_first.QuadPart) * 1000.0 / s_freq.QuadPart;
		char line[160];
		int nLen = sprintf_s(line, "%8.1f  %s\r\n", dMs, sName);
		TCHAR sPath[MAX_PATH];
		DWORD n = ::GetTempPath(MAX_PATH, sPath);
		if (n == 0 || n > MAX_PATH - 20) return;
		_tcscat_s(sPath, _T("lucid_trace.txt"));
		HANDLE hFile = ::CreateFile(sPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, 0, NULL);
		if (hFile == INVALID_HANDLE_VALUE) return;
		DWORD nWritten;
		::WriteFile(hFile, line, nLen, &nWritten, NULL);
		::CloseHandle(hFile);
	}

	// Logs every window message of the thread that takes longer than 3 ms (diagnosis only)
	namespace Detail {
		inline LARGE_INTEGER* Stack() { static LARGE_INTEGER s[64]; return s; }
		inline int& Depth() { static int n = 0; return n; }
		inline LRESULT CALLBACK CallProc(int nCode, WPARAM w, LPARAM l) {
			if (nCode >= 0 && Depth() < 64) ::QueryPerformanceCounter(&Stack()[Depth()]);
			Depth()++;
			return ::CallNextHookEx(NULL, nCode, w, l);
		}
		inline LRESULT CALLBACK RetProc(int nCode, WPARAM w, LPARAM l) {
			Depth()--;
			if (nCode >= 0 && Depth() >= 0 && Depth() < 64) {
				LARGE_INTEGER now, freq;
				::QueryPerformanceCounter(&now);
				::QueryPerformanceFrequency(&freq);
				double dMs = (now.QuadPart - Stack()[Depth()].QuadPart) * 1000.0 / freq.QuadPart;
				if (dMs > 3.0) {
					CWPRETSTRUCT* p = (CWPRETSTRUCT*)l;
					char s[96];
					sprintf_s(s, "      msg 0x%04X depth %d took %.1f ms", p->message, Depth(), dMs);
					Mark(s);
				}
			}
			return ::CallNextHookEx(NULL, nCode, w, l);
		}
	}
	inline void HookMessages() {
		if (!IsEnabled()) return;
		::SetWindowsHookEx(WH_CALLWNDPROC, Detail::CallProc, NULL, ::GetCurrentThreadId());
		::SetWindowsHookEx(WH_CALLWNDPROCRET, Detail::RetProc, NULL, ::GetCurrentThreadId());
	}
}
