#include "StdAfx.h"
#include "resource.h"
#include "CompareWindow.h"
#include "Converter.h"
#include "JPEGImage.h"
#include "NLS.h"
#include "DarkUI.h"
#include <gdiplus.h>
#include <memory>

using namespace Gdiplus;

namespace CompareWindow {

	static const LPCTSTR COMPARE_CLASS = _T("LucidViewerCompare");

	struct CState {
		std::unique_ptr<Bitmap> Images[2];
		CString Names[2];
		double Zoom;        // 1: the image fits its half of the window
		double CenterX, CenterY; // shown center of the images, 0..1 of the image size
		bool Dragging;
		POINT DragStart;
		double DragCenterX, DragCenterY;
		bool Done;
	};

	// Copy of the original pixels (24 or 32 bpp BGR) as GDI+ bitmap, NULL on error
	static Bitmap* ToBitmap(CJPEGImage* pImage) {
		if (pImage == NULL || pImage->OriginalPixels() == NULL) return NULL;
		int nWidth = pImage->OrigWidth(), nHeight = pImage->OrigHeight();
		int nChannels = pImage->OriginalChannels();
		int nStride = (nChannels == 4) ? nWidth * 4 : ((nWidth * 3 + 3) & ~3);
		Bitmap* pBitmap = new Bitmap(nWidth, nHeight, PixelFormat32bppRGB);
		BitmapData data;
		Rect rect(0, 0, nWidth, nHeight);
		if (pBitmap->GetLastStatus() != Ok || pBitmap->LockBits(&rect, ImageLockModeWrite, PixelFormat32bppRGB, &data) != Ok) {
			delete pBitmap;
			return NULL;
		}
		const BYTE* pSource = (const BYTE*)pImage->OriginalPixels();
		for (int y = 0; y < nHeight; y++) {
			const BYTE* pLine = pSource + (size_t)y * nStride;
			BYTE* pTarget = (BYTE*)data.Scan0 + (size_t)y * data.Stride;
			for (int x = 0; x < nWidth; x++) {
				pTarget[x * 4] = pLine[x * nChannels];
				pTarget[x * 4 + 1] = pLine[x * nChannels + 1];
				pTarget[x * 4 + 2] = pLine[x * nChannels + 2];
				pTarget[x * 4 + 3] = 255;
			}
		}
		pBitmap->UnlockBits(&data);
		return pBitmap;
	}

	// Rectangle of the image in its half (pane), with the common zoom and center
	static RectF ImageRect(const CState& state, int nIndex, const RectF& pane) {
		Bitmap* pBitmap = state.Images[nIndex].get();
		float fWidth = (float)pBitmap->GetWidth(), fHeight = (float)pBitmap->GetHeight();
		float fScale = min(pane.Width / fWidth, pane.Height / fHeight) * (float)state.Zoom;
		float fShownWidth = fWidth * fScale, fShownHeight = fHeight * fScale;
		float x = pane.X + pane.Width / 2 - (float)state.CenterX * fShownWidth;
		float y = pane.Y + pane.Height / 2 - (float)state.CenterY * fShownHeight;
		return RectF(x, y, fShownWidth, fShownHeight);
	}

	static void Paint(HWND hWnd, CState& state) {
		PAINTSTRUCT ps;
		HDC hDC = ::BeginPaint(hWnd, &ps);
		RECT client;
		::GetClientRect(hWnd, &client);
		int nW = client.right, nH = client.bottom;
		if (nW > 0 && nH > 0) {
			HDC hMemDC = ::CreateCompatibleDC(hDC);
			HBITMAP hBitmap = ::CreateCompatibleBitmap(hDC, nW, nH);
			HGDIOBJ hOld = ::SelectObject(hMemDC, hBitmap);
			{
				Graphics g(hMemDC);
				g.SetInterpolationMode(state.Zoom > 1.5 ? InterpolationModeNearestNeighbor : InterpolationModeHighQualityBilinear);
				g.SetPixelOffsetMode(PixelOffsetModeHalf);
				g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
				const DarkUI::CPalette& colors = DarkUI::Colors();
				g.Clear(Color(255, GetRValue(colors.Window), GetGValue(colors.Window), GetBValue(colors.Window)));
				FontFamily family(L"Segoe UI");
				Font font(&family, 13.0f * ::GetDeviceCaps(hDC, LOGPIXELSX) / 96.0f, FontStyleRegular, UnitPixel);
				SolidBrush textBrush(Color(255, GetRValue(colors.Text), GetGValue(colors.Text), GetBValue(colors.Text)));
				SolidBrush labelBack(Color(160, 0, 0, 0));
				SolidBrush white(Color(255, 255, 255, 255));
				int nLabel = ::MulDiv(26, ::GetDeviceCaps(hDC, LOGPIXELSX), 96);
				for (int i = 0; i < 2; i++) {
					RectF pane((REAL)(i * nW / 2), 0, (REAL)(nW / 2), (REAL)nH);
					GraphicsState saved = g.Save();
					g.SetClip(pane);
					if (state.Images[i]) {
						g.DrawImage(state.Images[i].get(), ImageRect(state, i, pane));
					} else {
						StringFormat center;
						center.SetAlignment(StringAlignmentCenter);
						center.SetLineAlignment(StringAlignmentCenter);
						g.DrawString(CNLS::GetString(_T("The image could not be read.")), -1, &font, pane, &center, &textBrush);
					}
					RectF label(pane.X, pane.Y, pane.Width, (REAL)nLabel);
					g.FillRectangle(&labelBack, label);
					StringFormat format;
					format.SetAlignment(StringAlignmentCenter);
					format.SetLineAlignment(StringAlignmentCenter);
					format.SetTrimming(StringTrimmingEllipsisPath);
					format.SetFormatFlags(StringFormatFlagsNoWrap);
					g.DrawString(state.Names[i], -1, &font, label, &format, &white);
					g.Restore(saved);
				}
				Pen divider(Color(255, GetRValue(colors.Border), GetGValue(colors.Border), GetBValue(colors.Border)), 2.0f);
				g.DrawLine(&divider, (REAL)(nW / 2), 0.0f, (REAL)(nW / 2), (REAL)nH);
			}
			::BitBlt(hDC, 0, 0, nW, nH, hMemDC, 0, 0, SRCCOPY);
			::SelectObject(hMemDC, hOld);
			::DeleteObject(hBitmap);
			::DeleteDC(hMemDC);
		}
		::EndPaint(hWnd, &ps);
	}

	static LRESULT CALLBACK WndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
		CState* pState = (CState*)::GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if (nMsg == WM_NCCREATE) {
			::SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCT*)lParam)->lpCreateParams);
			return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
		}
		if (pState == NULL) return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
		switch (nMsg) {
			case WM_PAINT:
				Paint(hWnd, *pState);
				return 0;
			case WM_ERASEBKGND:
				return 1;
			case WM_SIZE:
				::InvalidateRect(hWnd, NULL, FALSE);
				return 0;
			case WM_MOUSEWHEEL: {
				// zoom both images at the point under the mouse
				POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
				::ScreenToClient(hWnd, &point);
				RECT client;
				::GetClientRect(hWnd, &client);
				int nPane = (point.x < client.right / 2) ? 0 : 1;
				RectF pane((REAL)(nPane * client.right / 2), 0, (REAL)(client.right / 2), (REAL)client.bottom);
				if (!pState->Images[nPane]) return 0;
				RectF before = ImageRect(*pState, nPane, pane);
				double dMouseX = (point.x - before.X) / max(1.0f, before.Width), dMouseY = (point.y - before.Y) / max(1.0f, before.Height);
				double dFactor = GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? 1.25 : 0.8;
				double dNewZoom = max(1.0, min(64.0, pState->Zoom * dFactor));
				double dRatio = pState->Zoom / dNewZoom;
				// the point under the mouse stays where it is
				pState->CenterX = dMouseX + (pState->CenterX - dMouseX) * dRatio;
				pState->CenterY = dMouseY + (pState->CenterY - dMouseY) * dRatio;
				pState->Zoom = dNewZoom;
				if (dNewZoom <= 1.0) pState->CenterX = pState->CenterY = 0.5;
				::InvalidateRect(hWnd, NULL, FALSE);
				return 0;
			}
			case WM_LBUTTONDOWN:
				pState->Dragging = true;
				pState->DragStart.x = GET_X_LPARAM(lParam);
				pState->DragStart.y = GET_Y_LPARAM(lParam);
				pState->DragCenterX = pState->CenterX;
				pState->DragCenterY = pState->CenterY;
				::SetCapture(hWnd);
				return 0;
			case WM_MOUSEMOVE:
				if (pState->Dragging && pState->Images[0]) {
					RECT client;
					::GetClientRect(hWnd, &client);
					RectF pane(0, 0, (REAL)(client.right / 2), (REAL)client.bottom);
					RectF shown = ImageRect(*pState, 0, pane);
					pState->CenterX = pState->DragCenterX - (GET_X_LPARAM(lParam) - pState->DragStart.x) / max(1.0f, shown.Width);
					pState->CenterY = pState->DragCenterY - (GET_Y_LPARAM(lParam) - pState->DragStart.y) / max(1.0f, shown.Height);
					::InvalidateRect(hWnd, NULL, FALSE);
				}
				return 0;
			case WM_LBUTTONUP:
				pState->Dragging = false;
				::ReleaseCapture();
				return 0;
			case WM_LBUTTONDBLCLK:
				pState->Zoom = 1.0;
				pState->CenterX = pState->CenterY = 0.5;
				::InvalidateRect(hWnd, NULL, FALSE);
				return 0;
			case WM_KEYDOWN:
				if (wParam == VK_ESCAPE) ::PostMessage(hWnd, WM_CLOSE, 0, 0);
				return 0;
			case WM_CLOSE:
				pState->Done = true;
				return 0;
		}
		return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
	}

	void Show(HWND hParent, LPCTSTR sFile1, LPCTSTR sFile2) {
		CState state;
		state.Zoom = 1.0;
		state.CenterX = state.CenterY = 0.5;
		state.Dragging = false;
		state.Done = false;
		HCURSOR hOldCursor = ::SetCursor(::LoadCursor(NULL, IDC_WAIT));
		LPCTSTR files[2] = { sFile1, sFile2 };
		for (int i = 0; i < 2; i++) {
			CJPEGImage* pImage = Converter::LoadImage(files[i]);
			state.Images[i].reset(ToBitmap(pImage));
			delete pImage;
			LPCTSTR sName = _tcsrchr(files[i], _T('\\'));
			state.Names[i] = (sName != NULL) ? sName + 1 : files[i];
		}
		::SetCursor(hOldCursor);

		HINSTANCE hInstance = ::GetModuleHandle(NULL);
		WNDCLASS wndClass = { 0 };
		wndClass.style = CS_DBLCLKS;
		wndClass.lpfnWndProc = WndProc;
		wndClass.hInstance = hInstance;
		wndClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
		wndClass.hIcon = ::LoadIcon(hInstance, MAKEINTRESOURCE(IDR_MAINFRAME));
		wndClass.lpszClassName = COMPARE_CLASS;
		::RegisterClass(&wndClass);

		RECT parentRect;
		::GetWindowRect(hParent, &parentRect);
		HWND hWnd = ::CreateWindow(COMPARE_CLASS, CNLS::GetString(_T("Compare")), WS_OVERLAPPEDWINDOW,
			parentRect.left, parentRect.top, parentRect.right - parentRect.left, parentRect.bottom - parentRect.top, hParent, NULL, hInstance, &state);
		if (hWnd == NULL) return;
		DarkUI::ApplyToWindow(hWnd);
		::EnableWindow(hParent, FALSE);
		::ShowWindow(hWnd, ::IsZoomed(hParent) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
		MSG msg;
		while (!state.Done && ::GetMessage(&msg, NULL, 0, 0) > 0) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
		::EnableWindow(hParent, TRUE);
		::DestroyWindow(hWnd);
		::SetActiveWindow(hParent);
	}
}
