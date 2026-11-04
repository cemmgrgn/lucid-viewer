#pragma once

#include "ProcessParams.h"
#include "ImageProcessingTypes.h"

class CJPEGImage;

// Class that enables to save a processed image to a one of the supported file formats
class CSaveImage
{
public:
	// Save processed image to file. Returns if saving was successful or not.
	// The file format is derived from the file ending of the specified file name.
	// If bFullSize is true, the image in its original size is processed and saved.
	// If bFullSize is false, the image section as shown in the window is saved.
	// Processing parameters and flags are ignored when bFullSize is false, the image is not reprocessed in this case.
	static bool SaveImage(LPCTSTR sFileName, CJPEGImage * pImage, const CImageProcessingParams& procParams,
		EProcessingFlags eFlags, bool bFullSize, bool bUseLosslessWEBP, bool bCreateParameterDBEntry = true);

	// Saves processed image in current window size as displayed on screen. Creates no parameter DB entry for the saved image.
	// The file format is derived from the file ending of the specified file name.
	static bool SaveImage(LPCTSTR sFileName, CJPEGImage * pImage, bool bUseLosslessWEBP);

	// Saves the whole image scaled to targetSize in the given format, used by the converter.
	// Supported formats: IF_JPEG (keeps EXIF data if bKeepMetadata is set), IF_PNG, IF_WindowsBMP, IF_TIFF, IF_GIF, IF_WEBP, IF_QOI.
	// Creates no parameter DB entry.
	static bool SaveImageAs(LPCTSTR sFileName, EImageFormat eFormat, CJPEGImage * pImage, const CImageProcessingParams& procParams,
		EProcessingFlags eFlags, CSize targetSize, bool bUseLosslessWEBP, bool bKeepMetadata = true);

	// Saves the image as Windows icon (PNG compressed entry), scaled to fit into 256 x 256 pixels
	static bool SaveImageAsIcon(LPCTSTR sFileName, CJPEGImage * pImage, const CImageProcessingParams& procParams, EProcessingFlags eFlags);

	// Saves the image as single page PDF document with the image JPEG compressed, the page has the image size at 96 DPI
	static bool SaveImageAsPDF(LPCTSTR sFileName, CJPEGImage * pImage, const CImageProcessingParams& procParams,
		EProcessingFlags eFlags, CSize targetSize);

	// Saves the image as HEIC (HEVC) or AVIF (AV1) with the Windows HEIF encoder (WIC). Needs the HEIF and HEVC video
	// extensions of Windows, AV1 encoding needs Windows 11. Returns false if the encoder is not available.
	static bool SaveImageAsHEIF(LPCTSTR sFileName, CJPEGImage * pImage, const CImageProcessingParams& procParams,
		EProcessingFlags eFlags, CSize targetSize, bool bAV1);

	// Quality (1-100) used instead of the JPEGSaveQuality and WEBPSaveQuality settings, -1 to use the settings again
	static void SetQualityOverride(int nQuality) { sm_nQualityOverride = nQuality; }
	static int GetQualityOverride() { return sm_nQualityOverride; }

private:
	static int sm_nQualityOverride;
	CSaveImage(void);
};
