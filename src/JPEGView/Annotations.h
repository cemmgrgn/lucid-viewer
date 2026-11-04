#pragma once

#include <vector>

namespace Gdiplus { class Graphics; }

// Drawings over the image (pen, highlighter, rectangle, arrow, text, signature).
// The shapes are kept in coordinates of the original image pixels, so they follow zooming and panning.
// They are painted over the image on screen and burnt into the original pixels before saving.
class CAnnotations
{
public:
	enum ETool { TOOL_NONE, TOOL_PEN, TOOL_HIGHLIGHTER, TOOL_RECTANGLE, TOOL_ARROW, TOOL_TEXT, TOOL_REMOVE_COLOR };

	CAnnotations();

	ETool GetTool() { return m_eTool; }
	void SetTool(ETool eTool) { m_eTool = eTool; }
	COLORREF GetColor() { return m_color; }
	void SetColor(COLORREF color) { m_color = color; }
	int GetWidth() { return m_nWidth; }
	void SetWidth(int nWidth) { m_nWidth = nWidth; } // in screen pixels

	bool IsEmpty() { return m_shapes.empty(); }
	bool IsDrawing() { return m_bDrawing; }

	// Mouse input in image coordinates, dZoom is screen pixels per image pixel
	void BeginShape(float fX, float fY, double dZoom);
	void ContinueShape(float fX, float fY);
	void EndShape();
	void AddText(float fX, float fY, double dZoom, LPCTSTR sText);

	void Undo();
	void Clear();

	// Paints all shapes. The graphics transformation must map image coordinates to the target.
	void Render(Gdiplus::Graphics& graphics);
	// Paints the shapes into 32 bpp BGRA pixels of the given size, returns false on error
	bool BurnInto(void* pPixels, int nWidth, int nHeight);

	// Signature: the pen strokes are stored normalized in the user folder and can be inserted into other images
	bool SaveSignature();
	bool HasSignature();
	// Inserts the signature with the given width (image pixels) centered at the given point
	bool InsertSignature(float fCenterX, float fCenterY, float fWidth);

	// Writes 32 bpp BGRA pixels with the alpha values of the mask (0: transparent) as PNG, returns false on error
	static bool SaveTransparentPNG(LPCTSTR sFileName, const void* pBGRA, const unsigned char* pMask, int nWidth, int nHeight);

	// Window to draw the signature with the mouse or pen, saves it. Returns false if cancelled.
	static bool DrawSignature(HWND hParent);

	// Asks for the text of the text tool, returns false if cancelled or empty
	static bool AskForText(HWND hParent, LPCTSTR sTitle, LPCTSTR sOK, LPCTSTR sCancel, CString& sText);

private:
	struct CShape {
		ETool Tool;
		COLORREF Color;
		float Width; // image pixels
		std::vector<float> Points; // x, y pairs in image coordinates
		CString Text;
	};

	ETool m_eTool;
	COLORREF m_color;
	int m_nWidth;
	bool m_bDrawing;
	std::vector<CShape> m_shapes;

	static CString GetSignatureFileName();
};
