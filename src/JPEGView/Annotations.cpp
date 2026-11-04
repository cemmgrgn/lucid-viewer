#include "StdAfx.h"
#include "Annotations.h"
#include "SettingsProvider.h"
#include "NLS.h"
#include <gdiplus.h>
#include <math.h>

static Gdiplus::Color ToColor(COLORREF color, BYTE nAlpha) {
	return Gdiplus::Color(nAlpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

CAnnotations::CAnnotations() {
	m_eTool = TOOL_PEN;
	m_color = RGB(230, 40, 40);
	m_nWidth = 4;
	m_bDrawing = false;
}

void CAnnotations::BeginShape(float fX, float fY, double dZoom) {
	if (m_eTool == TOOL_NONE || m_eTool == TOOL_TEXT) {
		return;
	}
	CShape shape;
	shape.Tool = m_eTool;
	shape.Color = m_color;
	float fWidth = (float)(m_nWidth / max(0.01, dZoom));
	shape.Width = (m_eTool == TOOL_HIGHLIGHTER) ? fWidth * 4 : fWidth;
	shape.Points.push_back(fX);
	shape.Points.push_back(fY);
	shape.Points.push_back(fX);
	shape.Points.push_back(fY);
	m_shapes.push_back(shape);
	m_bDrawing = true;
}

void CAnnotations::ContinueShape(float fX, float fY) {
	if (!m_bDrawing || m_shapes.empty()) {
		return;
	}
	CShape& shape = m_shapes.back();
	if (shape.Tool == TOOL_PEN || shape.Tool == TOOL_HIGHLIGHTER) {
		size_t n = shape.Points.size();
		// skip points closer than a pixel to the last one
		if (fabs(shape.Points[n - 2] - fX) + fabs(shape.Points[n - 1] - fY) >= 1.0f) {
			shape.Points.push_back(fX);
			shape.Points.push_back(fY);
		}
	} else {
		// rectangle and arrow: start and end point
		shape.Points[2] = fX;
		shape.Points[3] = fY;
	}
}

void CAnnotations::EndShape() {
	m_bDrawing = false;
}

void CAnnotations::AddText(float fX, float fY, double dZoom, LPCTSTR sText) {
	if (sText == NULL || sText[0] == 0) {
		return;
	}
	CShape shape;
	shape.Tool = TOOL_TEXT;
	shape.Color = m_color;
	// the width setting selects the text size: 4 px width gives 24 px text on screen
	shape.Width = (float)(max(12, m_nWidth * 6) / max(0.01, dZoom));
	shape.Points.push_back(fX);
	shape.Points.push_back(fY);
	shape.Text = sText;
	m_shapes.push_back(shape);
}

void CAnnotations::Undo() {
	if (!m_shapes.empty()) {
		m_shapes.pop_back();
	}
	m_bDrawing = false;
}

void CAnnotations::Clear() {
	m_shapes.clear();
	m_bDrawing = false;
}

void CAnnotations::Render(Gdiplus::Graphics& graphics) {
	graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
	graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
	for (const CShape& shape : m_shapes) {
		const std::vector<float>& p = shape.Points;
		if (shape.Tool == TOOL_TEXT) {
			Gdiplus::FontFamily fontFamily(L"Segoe UI");
			Gdiplus::Font font(&fontFamily, shape.Width, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
			Gdiplus::SolidBrush brush(ToColor(shape.Color, 255));
			graphics.DrawString(shape.Text, shape.Text.GetLength(), &font, Gdiplus::PointF(p[0], p[1]), &brush);
			continue;
		}
		Gdiplus::Pen pen(ToColor(shape.Color, shape.Tool == TOOL_HIGHLIGHTER ? 100 : 255), shape.Width);
		pen.SetLineJoin(Gdiplus::LineJoinRound);
		if (shape.Tool == TOOL_HIGHLIGHTER) {
			pen.SetStartCap(Gdiplus::LineCapFlat);
			pen.SetEndCap(Gdiplus::LineCapFlat);
		} else {
			pen.SetStartCap(Gdiplus::LineCapRound);
			pen.SetEndCap(Gdiplus::LineCapRound);
		}
		if (shape.Tool == TOOL_PEN || shape.Tool == TOOL_HIGHLIGHTER) {
			std::vector<Gdiplus::PointF> points;
			for (size_t i = 0; i + 1 < p.size(); i += 2) {
				points.push_back(Gdiplus::PointF(p[i], p[i + 1]));
			}
			if (points.size() == 2 && points[0].Equals(points[1])) {
				points[1].X += 0.5f; // a click draws a dot
			}
			graphics.DrawLines(&pen, &points[0], (INT)points.size());
		} else if (shape.Tool == TOOL_RECTANGLE) {
			graphics.DrawRectangle(&pen, min(p[0], p[2]), min(p[1], p[3]), fabs(p[2] - p[0]), fabs(p[3] - p[1]));
		} else if (shape.Tool == TOOL_ARROW) {
			Gdiplus::AdjustableArrowCap arrowCap(4.0f, 4.0f, TRUE);
			pen.SetCustomEndCap(&arrowCap);
			graphics.DrawLine(&pen, p[0], p[1], p[2], p[3]);
		}
	}
}

bool CAnnotations::BurnInto(void* pPixels, int nWidth, int nHeight) {
	if (pPixels == NULL) {
		return false;
	}
	// 32bppRGB ignores the fourth byte, the image keeps its alpha channel if it has one
	Gdiplus::Bitmap bitmap(nWidth, nHeight, nWidth * 4, PixelFormat32bppRGB, (BYTE*)pPixels);
	if (bitmap.GetLastStatus() != Gdiplus::Ok) {
		return false;
	}
	Gdiplus::Graphics graphics(&bitmap);
	Render(graphics);
	graphics.Flush(Gdiplus::FlushIntentionSync);
	return true;
}

CString CAnnotations::GetSignatureFileName() {
	TCHAR sAppData[MAX_PATH];
	if (!::SHGetSpecialFolderPath(NULL, sAppData, CSIDL_APPDATA, TRUE)) {
		return CString();
	}
	return CString(sAppData) + _T("\\JPEGView\\signature.txt");
}

bool CAnnotations::SaveSignature() {
	// bounding box of all pen strokes
	float fMinX = 1e9f, fMinY = 1e9f, fMaxX = -1e9f, fMaxY = -1e9f;
	for (const CShape& shape : m_shapes) {
		if (shape.Tool != TOOL_PEN) continue;
		for (size_t i = 0; i + 1 < shape.Points.size(); i += 2) {
			fMinX = min(fMinX, shape.Points[i]); fMaxX = max(fMaxX, shape.Points[i]);
			fMinY = min(fMinY, shape.Points[i + 1]); fMaxY = max(fMaxY, shape.Points[i + 1]);
		}
	}
	float fSize = max(fMaxX - fMinX, fMaxY - fMinY);
	CString sFileName = GetSignatureFileName();
	if (fSize <= 0 || sFileName.IsEmpty()) {
		return false;
	}
	// one stroke per line: width, then x y pairs normalized to the width of the signature
	FILE* pFile = NULL;
	if (_tfopen_s(&pFile, sFileName, _T("w")) != 0 || pFile == NULL) {
		return false;
	}
	for (const CShape& shape : m_shapes) {
		if (shape.Tool != TOOL_PEN) continue;
		fprintf(pFile, "%.5f", shape.Width / fSize);
		for (size_t i = 0; i + 1 < shape.Points.size(); i += 2) {
			fprintf(pFile, " %.5f %.5f", (shape.Points[i] - fMinX) / fSize, (shape.Points[i + 1] - fMinY) / fSize);
		}
		fprintf(pFile, "\n");
	}
	fclose(pFile);
	return true;
}

bool CAnnotations::HasSignature() {
	CString sFileName = GetSignatureFileName();
	return !sFileName.IsEmpty() && ::GetFileAttributes(sFileName) != INVALID_FILE_ATTRIBUTES;
}

bool CAnnotations::InsertSignature(float fCenterX, float fCenterY, float fWidth) {
	FILE* pFile = NULL;
	if (_tfopen_s(&pFile, GetSignatureFileName(), _T("r")) != 0 || pFile == NULL) {
		return false;
	}
	std::vector<CShape> strokes;
	float fMaxX = 0, fMaxY = 0;
	char line[65536];
	while (fgets(line, sizeof(line), pFile) != NULL) {
		CShape shape;
		shape.Tool = TOOL_PEN;
		shape.Color = m_color;
		char* pContext = NULL;
		char* pToken = strtok_s(line, " \r\n", &pContext);
		if (pToken == NULL) continue;
		shape.Width = (float)atof(pToken);
		while ((pToken = strtok_s(NULL, " \r\n", &pContext)) != NULL) {
			shape.Points.push_back((float)atof(pToken));
		}
		if (shape.Points.size() < 2) continue;
		for (size_t i = 0; i + 1 < shape.Points.size(); i += 2) {
			fMaxX = max(fMaxX, shape.Points[i]);
			fMaxY = max(fMaxY, shape.Points[i + 1]);
		}
		strokes.push_back(shape);
	}
	fclose(pFile);
	if (strokes.empty()) {
		return false;
	}
	// the stored coordinates have the larger side of the signature as unit
	float fScale = fWidth / max(0.01f, fMaxX);
	for (CShape& stroke : strokes) {
		stroke.Width = max(1.0f, stroke.Width * fScale);
		for (size_t i = 0; i + 1 < stroke.Points.size(); i += 2) {
			stroke.Points[i] = fCenterX + (stroke.Points[i] - fMaxX / 2) * fScale;
			stroke.Points[i + 1] = fCenterY + (stroke.Points[i + 1] - fMaxY / 2) * fScale;
		}
		m_shapes.push_back(stroke);
	}
	return true;
}

// Small modal window with an edit field, used for the text tool
struct CTextInput {
	HWND Edit;
	bool Done, OK;
};

static LRESULT CALLBACK TextInputWndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
	CTextInput* pInput = (CTextInput*)::GetWindowLongPtr(hWnd, GWLP_USERDATA);
	if (nMsg == WM_NCCREATE) {
		::SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCT*)lParam)->lpCreateParams);
	} else if (nMsg == WM_COMMAND && pInput != NULL && (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)) {
		pInput->OK = LOWORD(wParam) == IDOK;
		pInput->Done = true;
		::PostMessage(hWnd, WM_NULL, 0, 0);
		return 0;
	} else if (nMsg == WM_CLOSE && pInput != NULL) {
		pInput->Done = true;
		::PostMessage(hWnd, WM_NULL, 0, 0);
		return 0;
	}
	return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

bool CAnnotations::AskForText(HWND hParent, LPCTSTR sTitle, LPCTSTR sOK, LPCTSTR sCancel, CString& sText) {
	HINSTANCE hInstance = ::GetModuleHandle(NULL);
	WNDCLASS wndClass{ 0 };
	wndClass.lpfnWndProc = TextInputWndProc;
	wndClass.hInstance = hInstance;
	wndClass.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	wndClass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
	wndClass.lpszClassName = _T("JPEGViewTextInput");
	::RegisterClass(&wndClass);

	HDC hDC = ::GetDC(NULL);
	int nDPI = ::GetDeviceCaps(hDC, LOGPIXELSX);
	::ReleaseDC(NULL, hDC);
	auto S = [nDPI](int n) { return ::MulDiv(n, nDPI, 96); };
	POINT cursor;
	::GetCursorPos(&cursor);
	CTextInput input = { NULL, false, false };
	HWND hWnd = ::CreateWindowEx(WS_EX_DLGMODALFRAME, _T("JPEGViewTextInput"), sTitle, WS_POPUP | WS_CAPTION | WS_SYSMENU,
		cursor.x, cursor.y, S(420), S(130), hParent, NULL, hInstance, &input);
	if (hWnd == NULL) {
		return false;
	}
	NONCLIENTMETRICS metrics{ 0 };
	metrics.cbSize = sizeof(metrics);
	::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
	HFONT hFont = ::CreateFontIndirect(&metrics.lfMessageFont);
	input.Edit = ::CreateWindowEx(WS_EX_CLIENTEDGE, _T("EDIT"), sText, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
		S(12), S(12), S(380), S(26), hWnd, NULL, hInstance, NULL);
	HWND hOK = ::CreateWindow(_T("BUTTON"), sOK, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, S(196), S(50), S(94), S(28), hWnd, (HMENU)IDOK, hInstance, NULL);
	HWND hCancel = ::CreateWindow(_T("BUTTON"), sCancel, WS_CHILD | WS_VISIBLE | WS_TABSTOP, S(298), S(50), S(94), S(28), hWnd, (HMENU)IDCANCEL, hInstance, NULL);
	::SendMessage(input.Edit, WM_SETFONT, (WPARAM)hFont, FALSE);
	::SendMessage(hOK, WM_SETFONT, (WPARAM)hFont, FALSE);
	::SendMessage(hCancel, WM_SETFONT, (WPARAM)hFont, FALSE);
	::EnableWindow(hParent, FALSE);
	::ShowWindow(hWnd, SW_SHOWNORMAL);
	::SetFocus(input.Edit);
	MSG msg;
	while (!input.Done && ::GetMessage(&msg, NULL, 0, 0) > 0) {
		if (!::IsDialogMessage(hWnd, &msg)) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
	}
	if (input.OK) {
		int nLength = ::GetWindowTextLength(input.Edit);
		::GetWindowText(input.Edit, sText.GetBuffer(nLength + 1), nLength + 1);
		sText.ReleaseBuffer();
	}
	::EnableWindow(hParent, TRUE);
	::DestroyWindow(hWnd);
	::SetActiveWindow(hParent);
	::DeleteObject(hFont);
	return input.OK && !sText.IsEmpty();
}

bool CAnnotations::SaveTransparentPNG(LPCTSTR sFileName, const void* pBGRA, const unsigned char* pMask, int nWidth, int nHeight) {
	Gdiplus::Bitmap bitmap(nWidth, nHeight, PixelFormat32bppARGB);
	Gdiplus::BitmapData data;
	Gdiplus::Rect rect(0, 0, nWidth, nHeight);
	if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
		return false;
	}
	const unsigned char* pSource = (const unsigned char*)pBGRA;
	for (int y = 0; y < nHeight; y++) {
		unsigned char* pTarget = (unsigned char*)data.Scan0 + (size_t)y * data.Stride;
		for (int x = 0; x < nWidth; x++) {
			size_t nIndex = (size_t)y * nWidth + x;
			pTarget[x * 4] = pSource[nIndex * 4];
			pTarget[x * 4 + 1] = pSource[nIndex * 4 + 1];
			pTarget[x * 4 + 2] = pSource[nIndex * 4 + 2];
			pTarget[x * 4 + 3] = pMask[nIndex];
		}
	}
	bitmap.UnlockBits(&data);
	UINT nEncoders = 0, nSize = 0;
	Gdiplus::GetImageEncodersSize(&nEncoders, &nSize);
	std::vector<BYTE> buffer(nSize);
	Gdiplus::ImageCodecInfo* pEncoders = (Gdiplus::ImageCodecInfo*)&buffer[0];
	Gdiplus::GetImageEncoders(nEncoders, nSize, pEncoders);
	for (UINT i = 0; i < nEncoders; i++) {
		if (wcscmp(pEncoders[i].MimeType, L"image/png") == 0) {
			return bitmap.Save(sFileName, &pEncoders[i].Clsid, NULL) == Gdiplus::Ok;
		}
	}
	return false;
}

///////////////////////////////////////////////////////////////////////////////////
// Signature pad
///////////////////////////////////////////////////////////////////////////////////

struct CSignaturePad {
	CAnnotations Strokes;
	CRect Canvas;
	bool Done, Saved, Drawing;
};

static const int ID_SIGNATURE_CLEAR = 101;

static LRESULT CALLBACK SignatureWndProc(HWND hWnd, UINT nMsg, WPARAM wParam, LPARAM lParam) {
	CSignaturePad* pPad = (CSignaturePad*)::GetWindowLongPtr(hWnd, GWLP_USERDATA);
	switch (nMsg) {
		case WM_CREATE:
			::SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCT*)lParam)->lpCreateParams);
			return 0;
		case WM_PAINT: {
			PAINTSTRUCT ps;
			HDC hDC = ::BeginPaint(hWnd, &ps);
			if (pPad != NULL) {
				CRect client;
				::GetClientRect(hWnd, &client);
				HDC hMemDC = ::CreateCompatibleDC(hDC);
				HBITMAP hBitmap = ::CreateCompatibleBitmap(hDC, client.Width(), client.Height());
				HGDIOBJ hOld = ::SelectObject(hMemDC, hBitmap);
				::FillRect(hMemDC, &client, ::GetSysColorBrush(COLOR_BTNFACE));
				::FillRect(hMemDC, &pPad->Canvas, (HBRUSH)::GetStockObject(WHITE_BRUSH));
				// signature line
				HPEN hPen = ::CreatePen(PS_DOT, 1, RGB(160, 160, 160));
				HGDIOBJ hOldPen = ::SelectObject(hMemDC, hPen);
				int nLineY = pPad->Canvas.bottom - pPad->Canvas.Height() / 4;
				::MoveToEx(hMemDC, pPad->Canvas.left + 20, nLineY, NULL);
				::LineTo(hMemDC, pPad->Canvas.right - 20, nLineY);
				::SelectObject(hMemDC, hOldPen);
				::DeleteObject(hPen);
				{
					Gdiplus::Graphics graphics(hMemDC);
					graphics.SetClip(Gdiplus::Rect(pPad->Canvas.left, pPad->Canvas.top, pPad->Canvas.Width(), pPad->Canvas.Height()));
					pPad->Strokes.Render(graphics);
				}
				::BitBlt(hDC, 0, 0, client.Width(), client.Height(), hMemDC, 0, 0, SRCCOPY);
				::SelectObject(hMemDC, hOld);
				::DeleteObject(hBitmap);
				::DeleteDC(hMemDC);
			}
			::EndPaint(hWnd, &ps);
			return 0;
		}
		case WM_ERASEBKGND:
			return 1;
		case WM_LBUTTONDOWN:
			if (pPad != NULL) {
				CPoint pt((short)LOWORD(lParam), (short)HIWORD(lParam));
				if (pPad->Canvas.PtInRect(pt)) {
					pPad->Strokes.BeginShape((float)pt.x, (float)pt.y, 1.0);
					pPad->Drawing = true;
					::SetCapture(hWnd);
				}
			}
			return 0;
		case WM_MOUSEMOVE:
			if (pPad != NULL && pPad->Drawing) {
				CPoint pt((short)LOWORD(lParam), (short)HIWORD(lParam));
				pt.x = max(pPad->Canvas.left, min(pPad->Canvas.right, pt.x));
				pt.y = max(pPad->Canvas.top, min(pPad->Canvas.bottom, pt.y));
				pPad->Strokes.ContinueShape((float)pt.x, (float)pt.y);
				::InvalidateRect(hWnd, &pPad->Canvas, FALSE);
			}
			return 0;
		case WM_LBUTTONUP:
			if (pPad != NULL && pPad->Drawing) {
				pPad->Strokes.EndShape();
				pPad->Drawing = false;
				::ReleaseCapture();
			}
			return 0;
		case WM_COMMAND:
			if (pPad == NULL) break;
			if (LOWORD(wParam) == ID_SIGNATURE_CLEAR) {
				pPad->Strokes.Clear();
				::InvalidateRect(hWnd, NULL, FALSE);
			} else if (LOWORD(wParam) == IDOK) {
				if (pPad->Strokes.SaveSignature()) {
					pPad->Saved = true;
					pPad->Done = true;
					::PostMessage(hWnd, WM_NULL, 0, 0);
				}
			} else if (LOWORD(wParam) == IDCANCEL) {
				pPad->Done = true;
				::PostMessage(hWnd, WM_NULL, 0, 0);
			}
			return 0;
		case WM_CLOSE:
			if (pPad != NULL) {
				pPad->Done = true;
				::PostMessage(hWnd, WM_NULL, 0, 0);
			}
			return 0;
	}
	return ::DefWindowProc(hWnd, nMsg, wParam, lParam);
}

bool CAnnotations::DrawSignature(HWND hParent) {
	HINSTANCE hInstance = ::GetModuleHandle(NULL);
	WNDCLASS wndClass{ 0 };
	wndClass.lpfnWndProc = SignatureWndProc;
	wndClass.hInstance = hInstance;
	wndClass.hCursor = ::LoadCursor(NULL, IDC_CROSS);
	wndClass.lpszClassName = _T("JPEGViewSignaturePad");
	::RegisterClass(&wndClass);
	HDC hDC = ::GetDC(NULL);
	int nDPI = ::GetDeviceCaps(hDC, LOGPIXELSX);
	::ReleaseDC(NULL, hDC);
	auto S = [nDPI](int n) { return ::MulDiv(n, nDPI, 96); };

	CSignaturePad pad;
	pad.Strokes.SetTool(TOOL_PEN);
	pad.Strokes.SetColor(RGB(20, 20, 60));
	pad.Strokes.SetWidth(3);
	pad.Done = pad.Saved = pad.Drawing = false;
	int nWidth = S(620), nHeight = S(330);
	pad.Canvas = CRect(S(12), S(40), nWidth - S(12), nHeight - S(56));
	RECT windowRect = { 0, 0, nWidth, nHeight };
	DWORD nStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU;
	::AdjustWindowRectEx(&windowRect, nStyle, FALSE, WS_EX_DLGMODALFRAME);
	RECT parentRect;
	::GetWindowRect(hParent, &parentRect);
	int nWindowWidth = windowRect.right - windowRect.left, nWindowHeight = windowRect.bottom - windowRect.top;
	HWND hWnd = ::CreateWindowEx(WS_EX_DLGMODALFRAME, _T("JPEGViewSignaturePad"), CNLS::GetString(_T("Draw your signature")), nStyle,
		(parentRect.left + parentRect.right - nWindowWidth) / 2, (parentRect.top + parentRect.bottom - nWindowHeight) / 2,
		nWindowWidth, nWindowHeight, hParent, NULL, hInstance, &pad);
	if (hWnd == NULL) return false;
	NONCLIENTMETRICS metrics{ 0 };
	metrics.cbSize = sizeof(metrics);
	::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
	HFONT hFont = ::CreateFontIndirect(&metrics.lfMessageFont);
	HWND hHint = ::CreateWindow(_T("STATIC"), CNLS::GetString(_T("Write your signature in the white area with the mouse or a pen.")),
		WS_CHILD | WS_VISIBLE, S(12), S(12), nWidth - S(24), S(22), hWnd, NULL, hInstance, NULL);
	int nButton = S(110), nButtonY = nHeight - S(44);
	HWND hClear = ::CreateWindow(_T("BUTTON"), CNLS::GetString(_T("Clear")), WS_CHILD | WS_VISIBLE | WS_TABSTOP,
		S(12), nButtonY, nButton, S(30), hWnd, (HMENU)(INT_PTR)ID_SIGNATURE_CLEAR, hInstance, NULL);
	HWND hSave = ::CreateWindow(_T("BUTTON"), CNLS::GetString(_T("Save")), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
		nWidth - S(12) - 2 * nButton - S(8), nButtonY, nButton, S(30), hWnd, (HMENU)IDOK, hInstance, NULL);
	HWND hCancel = ::CreateWindow(_T("BUTTON"), CNLS::GetString(_T("Cancel")), WS_CHILD | WS_VISIBLE | WS_TABSTOP,
		nWidth - S(12) - nButton, nButtonY, nButton, S(30), hWnd, (HMENU)IDCANCEL, hInstance, NULL);
	for (HWND hControl : { hHint, hClear, hSave, hCancel }) ::SendMessage(hControl, WM_SETFONT, (WPARAM)hFont, FALSE);
	::EnableWindow(hParent, FALSE);
	::ShowWindow(hWnd, SW_SHOWNORMAL);
	MSG msg;
	while (!pad.Done && ::GetMessage(&msg, NULL, 0, 0) > 0) {
		if (!::IsDialogMessage(hWnd, &msg)) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
	}
	::EnableWindow(hParent, TRUE);
	::DestroyWindow(hWnd);
	::SetActiveWindow(hParent);
	::DeleteObject(hFont);
	return pad.Saved;
}
