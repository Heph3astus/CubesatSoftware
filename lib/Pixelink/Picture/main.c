//
// main.c
//
// Sample code to capture an image from a PixeLINK camera and save
// the encoded image to a file.
//

#include <stdio.h>
#include <PixeLINKApi.h>
#include "getsnapshot.h"
#include "../../../main.h"
int visSnapshot(const char* fileName)
{
	HANDLE hCamera;
	const char* pFilenameJpg = strCat(fileName, ".jpg");
	int retVal;

	// Tell the camera we want to start using it.
	// NOTE: We're assuming there's only one camera.
	if (!API_SUCCESS(PxLInitialize(0, &hCamera))) {
		return 1;
	}

	// Get the snapshots and save it to a file
	retVal = GetSnapshot(hCamera, IMAGE_FORMAT_JPEG, pFilenameJpg);
	if (SUCCESS == retVal) {
		printf("Saved image to '%s'\n", pFilenameJpg);
	}
	if (SUCCESS != retVal) {
		printf("ERROR: Unable to capture an image\n");
	}

	// Tell the camera we're done with it.
	PxLUninitialize(hCamera);

	return (SUCCESS == retVal) ? 0 : 1;
}
