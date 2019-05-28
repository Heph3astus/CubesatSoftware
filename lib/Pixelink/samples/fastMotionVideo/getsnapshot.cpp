//
// Sample code to capture an image from a PixeLINK camera and save 
// the encoded image to a file.
//

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <PixeLINKApi.h>

// Local macros for return values
#ifndef SUCCESS
#define SUCCESS (0)
#endif
#ifndef FAILURE
#define FAILURE (1)
#endif

// Local prototypes 
static U32   determineRawImageSize(HANDLE hCamera);
static float getPixelSize(U32 pixelFormat);
static int	 getRawImage(HANDLE  hCamera, char* pRawImage, U32 rawImageSize, FRAME_DESC* pFrameDesc);
static int	 encodeRawImage(const char*, const FRAME_DESC*, U32, char**, U32*);
static int	 saveImageToFile(const char* pFilename, const char* pImage, U32 imageSize);


//
// Get a snapshot from the camera, and save to a file.
// 
//
int getSnapshot(HANDLE hCamera, U32 imageFormat, const char* pFilename)
{
	U32   rawImageSize;
	char* pRawImage;
	FRAME_DESC frameDesc;
	U32   encodedImageSize;
	char* pEncodedImage;
	int   retVal = FAILURE;

	assert(0 != hCamera);
	assert(pFilename);

	// Determine the size of buffer we'll need to hold an 
	// image from the camera
	rawImageSize = determineRawImageSize(hCamera);
	if (0 == rawImageSize) {
		return FAILURE;
	}

	// Malloc the buffer to hold the raw image
	pRawImage = (char*)malloc(rawImageSize);
	if(NULL != pRawImage) {

		// Capture a raw image
        	frameDesc.uSize = sizeof (FRAME_DESC);
		if (getRawImage(hCamera, pRawImage, rawImageSize, &frameDesc) == SUCCESS) {

            		// Encode the raw image into something displayable
			if (encodeRawImage(pRawImage, &frameDesc, imageFormat, &pEncodedImage, &encodedImageSize) == SUCCESS) {
				if (saveImageToFile(pFilename, pEncodedImage, encodedImageSize) == SUCCESS) {
					retVal = SUCCESS;
				}
				free(pEncodedImage);
			}
		}
		free(pRawImage);
	}

	return retVal;
}

//
// Capture an image from the camera.
// 
// NOTE: Assumes the camera is already streaming.
// NOTE: PxLGetNextFrame is a blocking call. 
// i.e. PxLGetNextFrame won't return until an image is captured.
// So, if you're using hardware triggering, it won't return until the camera is triggered.
// 
//
static int
getRawImage(HANDLE hCamera, char* pRawImage, U32 rawImageSize, FRAME_DESC* pFrameDesc)
{
	int retVal;

	assert(0 != hCamera);
	assert(NULL != pRawImage);
	assert(rawImageSize > 0);
	assert(NULL != pFrameDesc);


	// Set up the size of the frame desc - VERY important.
	pFrameDesc->uSize = sizeof(FRAME_DESC);

	// Get an image
	retVal = PxLGetNextFrame(hCamera, rawImageSize, (LPVOID*)pRawImage, pFrameDesc);

	return (ApiSuccess == retVal) ? SUCCESS : FAILURE;
}

//
// Query the camera for region of interest (ROI), decimation, and pixel format
// Using this information, we can calculate the size of a raw image

// Returns 0 on failure
//
static U32 
determineRawImageSize(HANDLE hCamera)
{
	float parms[4];		// reused for each feature query
	U32 roiWidth;
	U32 roiHeight;
	U32 paX, paY;		// integral factor by which the image is reduced
	U32 pixelFormat;
	U32 numPixels;
	float pixelSize;
	U32 flags = FEATURE_FLAG_MANUAL;
	U32 numParams;
	U32 mask;

	int retValue;

    assert(0 != hCamera);

    // Get Region of interest (ROI)
    numParams = 4; // left, top, width, height
    retValue = PxLGetFeature(hCamera, FEATURE_ROI, &flags, &numParams, &parms[0]);
    if (!API_SUCCESS(retValue)) return 0;
    
    roiWidth	= (U32)parms[2];
    roiHeight	= (U32)parms[3];

    // Ask about Decimation
    numParams = 4; // unused, decimation type, x_value, y_value )
    retValue = PxLGetFeature(hCamera, FEATURE_PIXEL_ADDRESSING, &flags, &numParams, &parms[0]);
    if (!API_SUCCESS(retValue)) return 0;
	
    paX = (U32)parms[2];
    paY = (U32)parms[2];

    // Pixel Format
    numParams = 1;
    retValue = PxLGetFeature(hCamera, FEATURE_PIXEL_FORMAT, &flags, &numParams, &parms[0]);
    if (!API_SUCCESS(retValue)) return 0;
	
    pixelFormat = (U32)parms[0];
    pixelSize = getPixelSize(pixelFormat);

    // Assuming the width and height are multiples of the decimation value
    // i.e. 
    // decimation of 1 means height and width can be anything		mask of 0xFFFFFFFF
    // decimation of 2 means height and width are multiples of 2	mask of 0xFFFFFFFE
    // decimation of 4 means height and width are multiples of 4	mask of 0xFFFFFFFC
    // 
    mask = ~(paX - 1);
    if (((roiWidth & mask) != roiWidth) || ((roiHeight & mask) != roiHeight)) {
	return 0;
    }

    numPixels = (roiWidth / paX) * (roiHeight / paY);

    return (U32) ((float)numPixels * pixelSize);
}

//
// Given the pixel format, return the size of a individual pixel (in bytes)
//
// Returns 0 on failure.
//
static float 
getPixelSize(U32 pixelFormat)
{
	float retVal = 0.0f;

	switch(pixelFormat) {
	
		case PIXEL_FORMAT_MONO8:
		case PIXEL_FORMAT_BAYER8_GRBG:
		case PIXEL_FORMAT_BAYER8_RGGB:
		case PIXEL_FORMAT_BAYER8_GBRG:
		case PIXEL_FORMAT_BAYER8_BGGR:
			retVal = 1.0f;
			break;

		case PIXEL_FORMAT_YUV422:
		case PIXEL_FORMAT_MONO16:
		case PIXEL_FORMAT_BAYER16_GRBG:
		case PIXEL_FORMAT_BAYER16_RGGB:
		case PIXEL_FORMAT_BAYER16_GBRG:
		case PIXEL_FORMAT_BAYER16_BGGR:
			retVal = 2.0f;
			break;

		case PIXEL_FORMAT_MONO12_PACKED:
		case PIXEL_FORMAT_BAYER12_GRBG_PACKED:
		case PIXEL_FORMAT_BAYER12_RGGB_PACKED:
		case PIXEL_FORMAT_BAYER12_GBRG_PACKED:
		case PIXEL_FORMAT_BAYER12_BGGR_PACKED:
			retVal = 1.5f;
			break;

		case PIXEL_FORMAT_RGB24:
			retVal = 3.0f;
			break;

		case PIXEL_FORMAT_RGB48:
			retVal = 6.0f;
			break;

		default:
			assert(0);
			break;
	}
	return retVal;
}

//
// Given a buffer with a raw image, create and return a 
// pointer to a new buffer with the encoded image. 
//
// NOTE: The caller becomes the owner of the buffer containing the 
//		 encoded image, and therefore must free the 
//		 buffer when done with it.
//
// Returns SUCCESS or FAILURE
//
static int
encodeRawImage(const char* pRawImage,
			   const FRAME_DESC* pFrameDesc, 
			   U32    encodedImageFormat, 
			   char** ppEncodedImage, 
			   U32*   pEncodedImageSize)
{
	U32 encodedImageSize = 0;
	char* pEncodedImage;

	assert(NULL != pRawImage);
	assert(NULL != pFrameDesc);
	assert(NULL != ppEncodedImage);
	assert(NULL != pEncodedImageSize);

	// How big is the encoded image going to be?
	// Pass in NULL for the encoded image pointer, and the result is
	// returned in encodedImageSize
	if (PxLFormatImage((LPVOID)pRawImage, (FRAME_DESC*)pFrameDesc, encodedImageFormat, NULL, &encodedImageSize) == ApiSuccess) {
		assert(encodedImageSize > 0);
		pEncodedImage = (char*)malloc(encodedImageSize);
		// Now that we have a buffer for the encoded image, ask for it to be converted.
		// NOTE: encodedImageSize is an IN param here because we're telling PxLFormatImage two things:
		//       1) pointer to the buffer
		//       2) the size of the buffer
		if (NULL != pEncodedImage) {
			if (PxLFormatImage((LPVOID)pRawImage, (FRAME_DESC*)pFrameDesc, encodedImageFormat, pEncodedImage, &encodedImageSize) == ApiSuccess) {
				*ppEncodedImage = pEncodedImage; // handing over ownership of buffer to caller
				*pEncodedImageSize = encodedImageSize;
				return SUCCESS;
			}
			free(pEncodedImage);
    		
		}
	}

	return FAILURE;
}

//
// Save a buffer to a file
// This overwrites any existing file
//
// Returns SUCCESS or FAILURE
//
static int
saveImageToFile(const char* pFilename, const char* pImage, U32 imageSize)
{
	size_t numBytesWritten;
	FILE* pFile;

	assert(NULL != pFilename);
	assert(NULL != pImage);
	assert(imageSize > 0);

	// Open our file for binary write
	pFile = fopen(pFilename, "wb");
	if (NULL == pFile) {
		return FAILURE;
	}

	numBytesWritten = fwrite((void*)pImage, sizeof(char), imageSize, pFile);

	fclose(pFile);
	
	return ((U32)numBytesWritten == imageSize) ? SUCCESS : FAILURE;
}

