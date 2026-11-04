#include "StdAfx.h"
#include "SaveImage.h"
#include "JPEGImage.h"
#include "BasicProcessing.h"
#include "Helpers.h"
#include "SettingsProvider.h"
#include "ParameterDB.h"
#include "EXIFReader.h"
#include "TJPEGWrapper.h"
#include "WEBPWrapper.h"
#include "QOIWrapper.h"
#include <gdiplus.h>
#include <wincodec.h>
#include <string>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")

//////////////////////////////////////////////////////////////////////////////////////////////
// Helpers
//////////////////////////////////////////////////////////////////////////////////////////////

// Removes an existing comment segment (if any) and returns the new JPEG stream, NULL if no comment was found in stream
int CSaveImage::sm_nQualityOverride = -1;

// The quality setting or the quality set by the export window
static int GetQuality(int nSetting) {
	return CSaveImage::GetQualityOverride() >= 0 ? CSaveImage::GetQualityOverride() : nSetting;
}

static uint8* RemoveExistingCommentSegment(void* pJPEGStream, int& nStreamLength) {
	uint8* pCommentSeg = (uint8*)Helpers::FindJPEGMarker(pJPEGStream, nStreamLength, 0xFE);
	if (pCommentSeg != NULL) {
		int nLenSegment = pCommentSeg[2] * 256 + pCommentSeg[3];
		int nHeader = (int)(pCommentSeg - (uint8*)pJPEGStream);
		uint8* pNewBuffer = new uint8[nStreamLength - nLenSegment];
		memcpy(pNewBuffer, pJPEGStream, nHeader);
		memcpy(&(pNewBuffer[nHeader]), pCommentSeg + nLenSegment, nStreamLength - nHeader - nLenSegment);
		nStreamLength = nStreamLength - nLenSegment;
		return pNewBuffer;
	}
	return NULL;
}

// Finds first JPEG marker, returns NULL if none found (invalid JPEG stream?)
static uint8* FindFirstJPEGMarker(void* pJPEGStream, int nStreamLength) {
	uint8* pStream = (uint8*)pJPEGStream;
	if (pStream == NULL || nStreamLength < 3 || pStream[0] != 0xFF || pStream[1] != 0xD8) {
		return NULL; // not a JPEG
	}
	int nIndex = 2;
	do {
		if (pStream[nIndex] == 0xFF) {
			// block header found, skip padding bytes
			while (pStream[nIndex] == 0xFF && nIndex < nStreamLength) nIndex++;
			break;
		}
		else {
			break; // block with pixel data found, start hashing from here
		}
	} while (nIndex < nStreamLength);

	if (pStream[nIndex] != 0) {
		return &(pStream[nIndex - 1]); // place on marker start
	}
	else {
		return NULL;
	}
}

// Inserts the specified string as a JPEG comment into the JPEG stream. Returns NULL in case of errors, a new JPEG stream otherwise
// The string is written as UTF-8
static uint8* InsertCommentBlock(void* pJPEGStream, int& nStreamLength, LPCTSTR comment) {
	const int MAX_COMMENT_LEN = 4096;
	uint8* pFirstJPGMarker = FindFirstJPEGMarker(pJPEGStream, nStreamLength);
	if (pFirstJPGMarker == NULL) {
		return NULL;
	}

	int numberOfCharsToConvert = min(MAX_COMMENT_LEN, (int)_tcslen(comment));
	int utf8Length = ::WideCharToMultiByte(CP_UTF8, 0, comment, numberOfCharsToConvert, NULL, 0, NULL, NULL);
	if (utf8Length == 0) {
		return NULL;
	}

	char* pBuffer = new char[utf8Length];
	::WideCharToMultiByte(CP_UTF8, 0, comment, numberOfCharsToConvert, pBuffer, utf8Length, 0, 0);

	uint8* pNewStream = new uint8[nStreamLength + utf8Length + 4]; // +4: two bytes marker, two bytes segment length
	
	if (pFirstJPGMarker[1] == 0xE0) {
		int nApp0Len = pFirstJPGMarker[2] * 256 + pFirstJPGMarker[3];
		pFirstJPGMarker += 2;
		pFirstJPGMarker += nApp0Len;
	}

	int nSizeToMarker = (int)(pFirstJPGMarker - (uint8*)pJPEGStream);
	memcpy(pNewStream, pJPEGStream, nSizeToMarker);

	// Comment segment marker
	pNewStream[nSizeToMarker] = 0xFF;
	pNewStream[nSizeToMarker + 1] = 0xFE;

	// Segment length
	int nCommentSegLen = 2 + utf8Length;
	pNewStream[nSizeToMarker + 2] = nCommentSegLen >> 8;
	pNewStream[nSizeToMarker + 3] = nCommentSegLen & 0xFF;

	// Comment and the rest of the JPEG stream
	memcpy(&(pNewStream[nSizeToMarker + 4]), pBuffer, utf8Length);
	memcpy(&(pNewStream[nSizeToMarker + 2 + nCommentSegLen]), &(((uint8*)pJPEGStream)[nSizeToMarker]), nStreamLength - nSizeToMarker);

	nStreamLength += nCommentSegLen + 2;

	delete[] pBuffer;

	return pNewStream;
}

// Gets the thumbnail as a 24 bpp BGR DIB, returns size of thumbnail in sizeThumb
static void* GetThumbnailDIB(CJPEGImage * pImage, CSize& sizeThumb) {
	EProcessingFlags eFlags = pImage->GetLastProcessFlags();
	eFlags = (EProcessingFlags)(eFlags | PFLAG_HighQualityResampling);
	eFlags = (EProcessingFlags)(eFlags & ~PFLAG_AutoContrastSection);
	eFlags = (EProcessingFlags)(eFlags & ~PFLAG_KeepParams);
	CImageProcessingParams params = pImage->GetLastProcessParams();
	params.Sharpen = 0.0;
	double dZoom;
	sizeThumb = Helpers::GetImageRect(pImage->OrigWidth(), pImage->OrigHeight(), 160, 160, Helpers::ZM_FitToScreenNoZoom, dZoom);
	void* pDIB32bpp = pImage->GetThumbnailDIB(sizeThumb, params, eFlags);
	if (pDIB32bpp == NULL) {
		return NULL;
	}
	uint32 nSizeLinePadded = Helpers::DoPadding(sizeThumb.cx*3, 4);
	uint32 nSizeBytes = nSizeLinePadded*sizeThumb.cy;
	char* pDIB24bpp = new char[nSizeBytes];
	CBasicProcessing::Convert32bppTo24bppDIB(sizeThumb.cx, sizeThumb.cy, pDIB24bpp, pDIB32bpp, false);
	return pDIB24bpp;
}

static int GetJFIFBlockLength(unsigned char* pJPEGStream) {
	int nJFIFLength = 0;
	if (pJPEGStream[2] == 0xFF && pJPEGStream[3] == 0xE0) {
		nJFIFLength = pJPEGStream[4]*256 + pJPEGStream[5] + 2;
	}
	return nJFIFLength;
}

// Returns the compressed JPEG stream that must be freed by the caller. NULL in case of error.
static void* CompressAndSave(LPCTSTR sFileName, CJPEGImage * pImage, 
							 void* pData, int nWidth, int nHeight, int nQuality, int& nJPEGStreamLen, 
							 bool& tjFreeNeeded, bool bCopyEXIF, bool bDeleteThumbnail) {
	nJPEGStreamLen = 0;
	tjFreeNeeded = true;
	bool bOutOfMemory;
	unsigned char* pTargetStream = (unsigned char*) TurboJpeg::Compress(pData, nWidth, nHeight, 
		nJPEGStreamLen, bOutOfMemory, nQuality);
	if (pTargetStream == NULL) {
		return NULL;
	}

	FILE *fptr = _tfopen(sFileName, _T("wb"));
	if (fptr == NULL) {
		TurboJpeg::Free(pTargetStream);
		return NULL;
	}

	// If EXIF data is present, replace any JFIF block by this EXIF block to preserve the EXIF information
	if (pImage->GetEXIFData() != NULL && bCopyEXIF) {
		const int cnAdditionalThumbBytes = 32000;
		int nEXIFBlockLenCorrection = 0;
		unsigned char* pNewStream = new unsigned char[nJPEGStreamLen + pImage->GetEXIFDataLength() + cnAdditionalThumbBytes];
		memcpy(pNewStream, pTargetStream, 2); // copy SOI block
		memcpy(pNewStream + 2, pImage->GetEXIFData(), pImage->GetEXIFDataLength()); // copy EXIF block
		
		// Set image orientation back to normal orientation, we save the pixels as displayed
		CEXIFReader exifReader(pNewStream + 2, IF_JPEG);
		exifReader.WriteImageOrientation(1); // 1 means default orientation (unrotated)
		if (bDeleteThumbnail) {
			exifReader.DeleteThumbnail();
		} else if (exifReader.HasJPEGCompressedThumbnail()) {
			// recreate EXIF thumbnail image
			CSize sizeThumb;
			void* pDIBThumb = GetThumbnailDIB(pImage, sizeThumb);
			if (pDIBThumb != NULL) {
				int nJPEGThumbStreamLen;
				unsigned char* pJPEGThumb = (unsigned char*) TurboJpeg::Compress(pDIBThumb, sizeThumb.cx, sizeThumb.cy, nJPEGThumbStreamLen, bOutOfMemory, 70);
				if (pJPEGThumb != NULL) {
					int nThumbJFIFLen = GetJFIFBlockLength(pJPEGThumb);
					nEXIFBlockLenCorrection = nJPEGThumbStreamLen - nThumbJFIFLen - exifReader.GetJPEGThumbStreamLen();
					if (nEXIFBlockLenCorrection <= cnAdditionalThumbBytes && pImage->GetEXIFDataLength() + nEXIFBlockLenCorrection < 65536) {
						exifReader.UpdateJPEGThumbnail(pJPEGThumb + 2 + nThumbJFIFLen, nJPEGThumbStreamLen - 2 - nThumbJFIFLen, nEXIFBlockLenCorrection, sizeThumb);
					} else {
						nEXIFBlockLenCorrection = 0;
					}
					delete[] pJPEGThumb;
				}
				delete[] pDIBThumb;
			}
		}

		int nJFIFLength = GetJFIFBlockLength(pTargetStream);
		memcpy(pNewStream + 2 + pImage->GetEXIFDataLength() + nEXIFBlockLenCorrection, pTargetStream + 2 + nJFIFLength, nJPEGStreamLen - 2 - nJFIFLength);
		TurboJpeg::Free(pTargetStream);
		pTargetStream = pNewStream;
		tjFreeNeeded = false;
		nJPEGStreamLen = nJPEGStreamLen - nJFIFLength + pImage->GetEXIFDataLength() + nEXIFBlockLenCorrection;
	}

	// Take over existing JPEG comment from the old image
	LPCTSTR sComment = pImage->GetJPEGComment();
	if (sComment != NULL && sComment[0] != 0) {
		uint8* pNewStream = RemoveExistingCommentSegment(pTargetStream, nJPEGStreamLen);
		if (pNewStream != NULL) {
			if (tjFreeNeeded) TurboJpeg::Free(pTargetStream); else delete[] pTargetStream;
			pTargetStream = pNewStream;
			tjFreeNeeded = false;
		}
		pNewStream = InsertCommentBlock(pTargetStream, nJPEGStreamLen, sComment);
		if (pNewStream != NULL) {
			if (tjFreeNeeded) TurboJpeg::Free(pTargetStream); else delete[] pTargetStream;
			pTargetStream = pNewStream;
			tjFreeNeeded = false;
		}
	}

	bool bSuccess = fwrite(pTargetStream, 1, nJPEGStreamLen, fptr) == nJPEGStreamLen;
	fclose(fptr);

	// delete partial file if no success
	if (!bSuccess) {
		if (tjFreeNeeded) TurboJpeg::Free(pTargetStream); else delete[] pTargetStream;
		_tunlink(sFileName);
		return NULL;
	}

	// Success, return compressed JPEG stream
	return pTargetStream;
}

// pData must point to 24 bit BGR DIB
static bool SaveWebP(LPCTSTR sFileName, void* pData, int nWidth, int nHeight, bool bUseLosslessWEBP) {
	FILE *fptr = _tfopen(sFileName, _T("wb"));
	if (fptr == NULL) {
		return false;
	}

	bool bSuccess = false;
	try {
		uint8* pOutput;
		size_t nSize;
		int nQuality = GetQuality(CSettingsProvider::This().WEBPSaveQuality());
		pOutput = (uint8*)WebpReaderWriter::Compress((uint8*)pData, nWidth, nHeight, nSize, nQuality, bUseLosslessWEBP);
		bSuccess = fwrite(pOutput, 1, nSize, fptr) == nSize;
		fclose(fptr);
		WebpReaderWriter::FreeMemory(pOutput);
	} catch(...) {
		fclose(fptr);
	}

	// delete partial file if no success
	if (!bSuccess) {
		_tunlink(sFileName);
		return false;
	}

	return true;
}

// pData must point to 24 bit BGR DIB
static bool SaveQOI(LPCTSTR sFileName, void* pData, int nWidth, int nHeight) {
	FILE* fptr = _tfopen(sFileName, _T("wb"));
	if (fptr == NULL) {
		return false;
	}

	bool bSuccess = false;
	try {
		uint8* pOutput;
		int nSize;
		pOutput = (uint8*)QoiReaderWriter::Compress((uint8*)pData, nWidth, nHeight, nSize);
		bSuccess = fwrite(pOutput, 1, nSize, fptr) == nSize;
		fclose(fptr);
		QoiReaderWriter::FreeMemory(pOutput);
	}
	catch (...) {
		fclose(fptr);
	}

	// delete partial file if no success
	if (!bSuccess) {
		_tunlink(sFileName);
		return false;
	}

	return true;
}

// Copied from MS sample
static int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
   UINT  num = 0;          // number of image encoders
   UINT  size = 0;         // size of the image encoder array in bytes

   Gdiplus::GetImageEncodersSize(&num, &size);
   if(size == 0)
	  return -1;  // Failure

   Gdiplus::ImageCodecInfo* pImageCodecInfo = (Gdiplus::ImageCodecInfo*)(malloc(size));

   Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);

   for(UINT j = 0; j < num; ++j) {
	  if( wcscmp(pImageCodecInfo[j].MimeType, format) == 0 ) {
		 *pClsid = pImageCodecInfo[j].Clsid;
		 free(pImageCodecInfo);
		 return j;  // Success
	  }    
   }

   free(pImageCodecInfo);
   return -1;  // Failure
}

// Saves the given 24 bpp DIB data to the file name given using GDI+
static bool SaveGDIPlus(LPCTSTR sFileName, EImageFormat eFileFormat, void* pData, int nWidth, int nHeight) {
	Gdiplus::Bitmap* pBitmap = new Gdiplus::Bitmap(nWidth, nHeight, Helpers::DoPadding(nWidth*3, 4), PixelFormat24bppRGB, (BYTE*)pData);
	if (pBitmap->GetLastStatus() != Gdiplus::Ok) {
		delete pBitmap;
		return false;
	}

	const wchar_t* sMIMEType = NULL;
	switch (eFileFormat) {
		case IF_WindowsBMP:
			sMIMEType = L"image/bmp";
			break;
		case IF_PNG:
			sMIMEType = L"image/png";
			break;
		case IF_TIFF:
			sMIMEType = L"image/tiff";
			break;
		case IF_GIF:
			sMIMEType = L"image/gif";
			break;
	}

	CLSID encoderClsid;
	int result = (sMIMEType == NULL) ? -1 : GetEncoderClsid(sMIMEType, &encoderClsid);
	if (result < 0) {
		delete pBitmap;
		return false;
	}

	const wchar_t* sFileNameUnicode;
	sFileNameUnicode = (const wchar_t*)sFileName;

	bool bOk = pBitmap->Save(sFileNameUnicode, &encoderClsid, NULL) == Gdiplus::Ok;

	delete pBitmap;
	return bOk;
}

//////////////////////////////////////////////////////////////////////////////////////////////
// CSaveImage
//////////////////////////////////////////////////////////////////////////////////////////////

bool CSaveImage::SaveImage(LPCTSTR sFileName, CJPEGImage * pImage, const CImageProcessingParams& procParams,
			 EProcessingFlags eFlags, bool bFullSize, bool bUseLosslessWEBP, bool bCreateParameterDBEntry) {
	pImage->EnableDimming(false);

	CSize imageSize;
	void* pDIB32bpp;

	if (bFullSize) {
		imageSize = pImage->OrigSize();
		pDIB32bpp = pImage->GetDIB(imageSize, imageSize, CPoint(0, 0), procParams, eFlags);
	} else {
		imageSize = CSize(pImage->DIBWidth(), pImage->DIBHeight());
		pDIB32bpp = pImage->DIBPixelsLastProcessed(true);
	}

	if (pDIB32bpp == NULL) {
		pImage->EnableDimming(true);
		return false;
	}

	uint32 nSizeLinePadded = Helpers::DoPadding(imageSize.cx*3, 4);
	uint32 nSizeBytes = nSizeLinePadded*imageSize.cy;
	char* pDIB24bpp = new char[nSizeBytes];
	CBasicProcessing::Convert32bppTo24bppDIB(imageSize.cx, imageSize.cy, pDIB24bpp, pDIB32bpp, false);

	EImageFormat eFileFormat = Helpers::GetImageFormat(sFileName);
	bool bSuccess = false;
	__int64 nPixelHash = 0;
	if (eFileFormat == IF_JPEG || eFileFormat == IF_JPEG_Embedded) {
		// Save JPEG not over GDI+ - we want to keep the meta-data if there is meta-data
		int nJPEGStreamLen;
		bool tjFreeNeeded;
		void* pCompressedJPEG = CompressAndSave(sFileName, pImage, pDIB24bpp, imageSize.cx, imageSize.cy, 
			GetQuality(CSettingsProvider::This().JPEGSaveQuality()), nJPEGStreamLen, tjFreeNeeded, true, !bFullSize);
		bSuccess = pCompressedJPEG != NULL;
		if (bSuccess) {
			nPixelHash = Helpers::CalculateJPEGFileHash(pCompressedJPEG, nJPEGStreamLen);
			if (tjFreeNeeded) {
				TurboJpeg::Free((unsigned char*)pCompressedJPEG);
			} else {
				delete[] pCompressedJPEG;
			}
		}
	} else {
		if (eFileFormat == IF_WEBP) {
			bSuccess = SaveWebP(sFileName, pDIB24bpp, imageSize.cx, imageSize.cy, bUseLosslessWEBP);
		} else if (eFileFormat == IF_QOI) {
			bSuccess = SaveQOI(sFileName, pDIB24bpp, imageSize.cx, imageSize.cy);
		} else {
			bSuccess = SaveGDIPlus(sFileName, eFileFormat, pDIB24bpp, imageSize.cx, imageSize.cy);
		}
		if (bSuccess) {
			CJPEGImage tempImage(imageSize.cx, imageSize.cy, pDIB32bpp, NULL, 4, 0, IF_Unknown, false, 0, 1, 0);
			nPixelHash = tempImage.GetUncompressedPixelHash();
			tempImage.DetachOriginalPixels();
		}
	}

	delete[] pDIB24bpp;
	pDIB24bpp = NULL;

	// Create database entry to avoid processing image again
	if (bSuccess && bCreateParameterDBEntry && CSettingsProvider::This().CreateParamDBEntryOnSave()) {
		if (nPixelHash != 0) {
			CParameterDBEntry newEntry;
			CImageProcessingParams ippNone(0.0, 1.0, 1.0, CSettingsProvider::This().Sharpen(), 0.0, 0.5, 0.5, 0.25, 0.5, 0.0, 0.0, 0.0);
			EProcessingFlags procFlagsNone = GetProcessingFlag(eFlags, PFLAG_HighQualityResampling) ? PFLAG_HighQualityResampling : PFLAG_None;
			newEntry.InitFromProcessParams(ippNone, procFlagsNone, CRotationParams(0));
			newEntry.SetHash(nPixelHash);
			CParameterDB::This().AddEntry(newEntry);
		}
	}
	pImage->EnableDimming(true);

	return bSuccess;
}

bool CSaveImage::SaveImage(LPCTSTR sFileName, CJPEGImage * pImage, bool bUseLosslessWEBP)
{
	CImageProcessingParams paramsNotUsed;
	return SaveImage(sFileName, pImage, paramsNotUsed, PFLAG_None, false, bUseLosslessWEBP, false);
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Conversion helpers
//////////////////////////////////////////////////////////////////////////////////////////////

// Gets the processed image scaled to targetSize as 24 bpp BGR DIB, must be freed with delete[]. NULL on error.
static char* GetScaledDIB24bpp(CJPEGImage * pImage, const CImageProcessingParams& procParams, EProcessingFlags eFlags, CSize targetSize) {
	void* pDIB32bpp = pImage->GetDIB(targetSize, targetSize, CPoint(0, 0), procParams, eFlags);
	if (pDIB32bpp == NULL) {
		return NULL;
	}
	uint32 nSizeBytes = Helpers::DoPadding(targetSize.cx*3, 4)*targetSize.cy;
	char* pDIB24bpp = new char[nSizeBytes];
	CBasicProcessing::Convert32bppTo24bppDIB(targetSize.cx, targetSize.cy, pDIB24bpp, pDIB32bpp, false);
	return pDIB24bpp;
}

static bool WriteBufferToFile(LPCTSTR sFileName, const void* pData, size_t nSize) {
	FILE* fptr = _tfopen(sFileName, _T("wb"));
	if (fptr == NULL) {
		return false;
	}
	bool bSuccess = fwrite(pData, 1, nSize, fptr) == nSize;
	fclose(fptr);
	if (!bSuccess) {
		_tunlink(sFileName);
	}
	return bSuccess;
}

bool CSaveImage::SaveImageAs(LPCTSTR sFileName, EImageFormat eFormat, CJPEGImage * pImage, const CImageProcessingParams& procParams,
	EProcessingFlags eFlags, CSize targetSize, bool bUseLosslessWEBP, bool bKeepMetadata) {
	pImage->EnableDimming(false);
	char* pDIB24bpp = GetScaledDIB24bpp(pImage, procParams, eFlags, targetSize);
	if (pDIB24bpp == NULL) {
		pImage->EnableDimming(true);
		return false;
	}

	bool bSuccess = false;
	if (eFormat == IF_JPEG) {
		int nJPEGStreamLen;
		bool tjFreeNeeded;
		void* pCompressedJPEG = CompressAndSave(sFileName, pImage, pDIB24bpp, targetSize.cx, targetSize.cy,
			GetQuality(CSettingsProvider::This().JPEGSaveQuality()), nJPEGStreamLen, tjFreeNeeded, bKeepMetadata, targetSize != pImage->OrigSize());
		bSuccess = pCompressedJPEG != NULL;
		if (bSuccess) {
			if (tjFreeNeeded) {
				TurboJpeg::Free((unsigned char*)pCompressedJPEG);
			} else {
				delete[] pCompressedJPEG;
			}
		}
	} else if (eFormat == IF_WEBP) {
		bSuccess = SaveWebP(sFileName, pDIB24bpp, targetSize.cx, targetSize.cy, bUseLosslessWEBP);
	} else if (eFormat == IF_QOI) {
		bSuccess = SaveQOI(sFileName, pDIB24bpp, targetSize.cx, targetSize.cy);
	} else {
		bSuccess = SaveGDIPlus(sFileName, eFormat, pDIB24bpp, targetSize.cx, targetSize.cy);
	}

	delete[] pDIB24bpp;
	pImage->EnableDimming(true);
	return bSuccess;
}

bool CSaveImage::SaveImageAsIcon(LPCTSTR sFileName, CJPEGImage * pImage, const CImageProcessingParams& procParams, EProcessingFlags eFlags) {
	const int MAX_ICON_SIZE = 256;
	double dZoom;
	CSize iconSize = Helpers::GetImageRect(pImage->OrigWidth(), pImage->OrigHeight(), MAX_ICON_SIZE, MAX_ICON_SIZE, Helpers::ZM_FitToScreenNoZoom, dZoom);

	pImage->EnableDimming(false);
	char* pDIB24bpp = GetScaledDIB24bpp(pImage, procParams, eFlags, iconSize);
	pImage->EnableDimming(true);
	if (pDIB24bpp == NULL) {
		return false;
	}

	// Encode as PNG into memory, icons with PNG compressed entries are supported since Windows Vista
	bool bSuccess = false;
	Gdiplus::Bitmap* pBitmap = new Gdiplus::Bitmap(iconSize.cx, iconSize.cy, Helpers::DoPadding(iconSize.cx*3, 4), PixelFormat24bppRGB, (BYTE*)pDIB24bpp);
	CLSID encoderClsid;
	IStream* pStream = NULL;
	if (pBitmap->GetLastStatus() == Gdiplus::Ok && GetEncoderClsid(L"image/png", &encoderClsid) >= 0 &&
		SUCCEEDED(::CreateStreamOnHGlobal(NULL, TRUE, &pStream)) && pBitmap->Save(pStream, &encoderClsid, NULL) == Gdiplus::Ok) {
		HGLOBAL hGlobal = NULL;
		STATSTG stat{ 0 };
		if (SUCCEEDED(::GetHGlobalFromStream(pStream, &hGlobal)) && SUCCEEDED(pStream->Stat(&stat, STATFLAG_NONAME))) {
			uint32 nPNGSize = (uint32)stat.cbSize.QuadPart;
			const uint8* pPNG = (const uint8*)::GlobalLock(hGlobal);
			// ICONDIR (6 bytes) + one ICONDIRENTRY (16 bytes) + PNG data
			uint32 nTotalSize = 6 + 16 + nPNGSize;
			uint8* pIcon = new uint8[nTotalSize];
			memset(pIcon, 0, 22);
			pIcon[2] = 1; // type: icon
			pIcon[4] = 1; // one image
			pIcon[6] = (uint8)(iconSize.cx >= 256 ? 0 : iconSize.cx); // 0 means 256
			pIcon[7] = (uint8)(iconSize.cy >= 256 ? 0 : iconSize.cy);
			pIcon[10] = 1; // color planes
			pIcon[12] = 32; // bits per pixel
			memcpy(&pIcon[14], &nPNGSize, 4);
			uint32 nOffset = 22;
			memcpy(&pIcon[18], &nOffset, 4);
			memcpy(&pIcon[22], pPNG, nPNGSize);
			::GlobalUnlock(hGlobal);
			bSuccess = WriteBufferToFile(sFileName, pIcon, nTotalSize);
			delete[] pIcon;
		}
	}
	if (pStream != NULL) pStream->Release();
	delete pBitmap;
	delete[] pDIB24bpp;
	return bSuccess;
}

bool CSaveImage::SaveImageAsHEIF(LPCTSTR sFileName, CJPEGImage * pImage, const CImageProcessingParams& procParams,
	EProcessingFlags eFlags, CSize targetSize, bool bAV1) {
	pImage->EnableDimming(false);
	char* pDIB24bpp = GetScaledDIB24bpp(pImage, procParams, eFlags, targetSize);
	pImage->EnableDimming(true);
	if (pDIB24bpp == NULL) {
		return false;
	}

	HRESULT hrInit = ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	bool bSuccess = false;
	IWICImagingFactory* pFactory = NULL;
	IWICStream* pStream = NULL;
	IWICBitmapEncoder* pEncoder = NULL;
	IWICBitmapFrameEncode* pFrame = NULL;
	IPropertyBag2* pOptions = NULL;
	IWICBitmap* pBitmap = NULL;
	UINT nStride = Helpers::DoPadding(targetSize.cx * 3, 4);
	HRESULT hr = ::CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
	if (SUCCEEDED(hr)) hr = pFactory->CreateStream(&pStream);
	if (SUCCEEDED(hr)) hr = pStream->InitializeFromFilename(sFileName, GENERIC_WRITE);
	if (SUCCEEDED(hr)) hr = pFactory->CreateEncoder(GUID_ContainerFormatHeif, NULL, &pEncoder);
	if (SUCCEEDED(hr)) hr = pEncoder->Initialize(pStream, WICBitmapEncoderNoCache);
	if (SUCCEEDED(hr)) hr = pEncoder->CreateNewFrame(&pFrame, &pOptions);
	if (SUCCEEDED(hr) && pOptions != NULL) {
		PROPBAG2 option{ 0 };
		VARIANT value;
		::VariantInit(&value);
		option.pstrName = (LPOLESTR)L"ImageQuality";
		value.vt = VT_R4;
		value.fltVal = max(0.0f, min(1.0f, GetQuality(CSettingsProvider::This().JPEGSaveQuality()) / 100.0f));
		pOptions->Write(1, &option, &value);
		option.pstrName = (LPOLESTR)L"HeifCompressionMethod";
		value.vt = VT_UI1;
		value.bVal = (BYTE)(bAV1 ? WICHeifCompressionAV1 : WICHeifCompressionHEVC);
		// without AV1 support the encoder rejects the option, the frame must not be written with HEVC then
		HRESULT hrOption = pOptions->Write(1, &option, &value);
		if (bAV1 && FAILED(hrOption)) hr = hrOption;
	}
	if (SUCCEEDED(hr)) hr = pFrame->Initialize(pOptions);
	if (SUCCEEDED(hr)) hr = pFrame->SetSize(targetSize.cx, targetSize.cy);
	if (SUCCEEDED(hr)) hr = pFactory->CreateBitmapFromMemory(targetSize.cx, targetSize.cy, GUID_WICPixelFormat24bppBGR,
		nStride, nStride * targetSize.cy, (BYTE*)pDIB24bpp, &pBitmap);
	if (SUCCEEDED(hr)) hr = pFrame->WriteSource(pBitmap, NULL); // converts to the pixel format of the encoder
	if (SUCCEEDED(hr)) hr = pFrame->Commit();
	if (SUCCEEDED(hr)) hr = pEncoder->Commit();
	bSuccess = SUCCEEDED(hr);

	if (pBitmap != NULL) pBitmap->Release();
	if (pOptions != NULL) pOptions->Release();
	if (pFrame != NULL) pFrame->Release();
	if (pEncoder != NULL) pEncoder->Release();
	if (pStream != NULL) pStream->Release();
	if (pFactory != NULL) pFactory->Release();
	if (SUCCEEDED(hrInit)) ::CoUninitialize();
	delete[] pDIB24bpp;
	if (!bSuccess) {
		_tunlink(sFileName);
	}
	return bSuccess;
}

bool CSaveImage::SaveImageAsPDF(LPCTSTR sFileName, CJPEGImage * pImage, const CImageProcessingParams& procParams,
	EProcessingFlags eFlags, CSize targetSize) {
	pImage->EnableDimming(false);
	char* pDIB24bpp = GetScaledDIB24bpp(pImage, procParams, eFlags, targetSize);
	pImage->EnableDimming(true);
	if (pDIB24bpp == NULL) {
		return false;
	}
	int nJPEGStreamLen = 0;
	bool bOutOfMemory;
	unsigned char* pJPEG = (unsigned char*)TurboJpeg::Compress(pDIB24bpp, targetSize.cx, targetSize.cy, nJPEGStreamLen, bOutOfMemory,
		GetQuality(CSettingsProvider::This().JPEGSaveQuality()));
	delete[] pDIB24bpp;
	if (pJPEG == NULL) {
		return false;
	}

	// Page size in points (1/72 inch), image pixels are mapped at 96 DPI
	double dPageWidth = targetSize.cx * 72.0 / 96.0;
	double dPageHeight = targetSize.cy * 72.0 / 96.0;

	std::string pdf;
	std::vector<size_t> offsets;
	char buff[512];
	pdf += "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
	offsets.push_back(pdf.size());
	pdf += "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";
	offsets.push_back(pdf.size());
	pdf += "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n";
	offsets.push_back(pdf.size());
	sprintf_s(buff, sizeof(buff), "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %.2f %.2f] /Resources << /XObject << /Im0 4 0 R >> >> /Contents 5 0 R >>\nendobj\n",
		dPageWidth, dPageHeight);
	pdf += buff;
	offsets.push_back(pdf.size());
	sprintf_s(buff, sizeof(buff), "4 0 obj\n<< /Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length %d >>\nstream\n",
		targetSize.cx, targetSize.cy, nJPEGStreamLen);
	pdf += buff;
	pdf.append((const char*)pJPEG, nJPEGStreamLen);
	pdf += "\nendstream\nendobj\n";
	TurboJpeg::Free(pJPEG);
	char content[128];
	int nContentLen = sprintf_s(content, sizeof(content), "q %.2f 0 0 %.2f 0 0 cm /Im0 Do Q", dPageWidth, dPageHeight);
	offsets.push_back(pdf.size());
	sprintf_s(buff, sizeof(buff), "5 0 obj\n<< /Length %d >>\nstream\n%s\nendstream\nendobj\n", nContentLen, content);
	pdf += buff;
	size_t nXRefOffset = pdf.size();
	sprintf_s(buff, sizeof(buff), "xref\n0 %d\n0000000000 65535 f \n", (int)offsets.size() + 1);
	pdf += buff;
	for (size_t offset : offsets) {
		sprintf_s(buff, sizeof(buff), "%010zu 00000 n \n", offset);
		pdf += buff;
	}
	sprintf_s(buff, sizeof(buff), "trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%zu\n%%%%EOF\n", (int)offsets.size() + 1, nXRefOffset);
	pdf += buff;

	return WriteBufferToFile(sFileName, pdf.data(), pdf.size());
}
