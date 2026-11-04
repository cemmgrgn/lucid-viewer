#pragma once

#include <vector>

namespace Gdiplus { class Graphics; }

// Drawings over the image (pen, highlighter, rectangle, arrow, text, signature).
// The shapes are kept in coordinates of the original image pixels, so they follow zooming and panning.
// They are painted over the image on screen and burnt into the original pixels before saving.
class CAnnotations
{
public:
	enum ETool { TOOL_NONE, TOOL_PEN, TOOL_HIGHLIGHTER, TOOL_RECTANGLE, TOOL_ARROW, TOOL_TEXT, TOOL_REMOVE_COLOR, TOOL_PIXELATE, TOOL_SELECT };

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

	// Select tool: a click selects a drawing (an inserted signature as a whole), dragging moves it,
	// the corner handles resize it keeping the proportions. Coordinates are image coordinates.
	bool SelectAt(float fX, float fY);
	void SelectLastGroup();
	void ClearSelection() { m_selection.clear(); m_nTransformHandle = 0; }
	bool HasSelection() { return !m_selection.empty(); }
	// 0: outside, 1-4: corner handles (top left, top right, bottom right, bottom left), 5: inside the selection
	int HitTestSelection(float fX, float fY);
	void BeginTransform(int nHandle, float fX, float fY);
	void ContinueTransform(float fX, float fY);
	void EndTransform() { m_nTransformHandle = 0; }
	bool IsTransforming() { return m_nTransformHandle != 0; }
	void DeleteSelection();
	// Screen pixels per image pixel, for the size of the selection handles
	void SetViewScale(float fScale) { m_fViewScale = max(0.0001f, fScale); }

	// Pixelate tool: the rectangles hide what is below them with large blocks
	// Removes the last shape if it is a pixelate rectangle, returns its rectangle (image coordinates)
	bool TakeLastPixelate(CRect& rect);
	bool HasPixelate();
	// Pixelates the rectangles into 32 bpp pixels whose origin is at (nOffsetX, nOffsetY) in image coordinates
	void PixelateInto(void* pPixels, int nWidth, int nHeight, int nOffsetX, int nOffsetY);
	// Block size of the pixelation for the given image size
	static int GetPixelateBlockSize(int nImageWidth, int nImageHeight);
	// Pixelates the rectangle of 32 bpp pixels (stride in pixels)
	static void Pixelate(DWORD* pPixels, int nWidth, int nHeight, int nStride, CRect rect, int nBlock);

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
		ETool Tool = TOOL_PEN;
		COLORREF Color = 0;
		float Width = 1.0f; // image pixels
		std::vector<float> Points; // x, y pairs in image coordinates
		CString Text;
		int Group = -1; // shapes of one group are selected together (the strokes of a signature), -1: no group
	};

	ETool m_eTool;
	COLORREF m_color;
	int m_nWidth;
	bool m_bDrawing;
	std::vector<CShape> m_shapes;
	int m_nNextGroup;
	std::vector<int> m_selection; // indices into m_shapes
	int m_nTransformHandle;
	float m_fStartX, m_fStartY, m_fStartLeft, m_fStartTop, m_fStartRight, m_fStartBottom;
	std::vector<CShape> m_transformStart;
	float m_fViewScale;
	bool m_bRenderSelection;
	static void GetShapeBounds(const CShape& shape, float& fLeft, float& fTop, float& fRight, float& fBottom);
	bool GetSelectionBounds(float& fLeft, float& fTop, float& fRight, float& fBottom);

	static CString GetSignatureFileName();
};
