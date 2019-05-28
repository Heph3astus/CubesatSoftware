#include <stdio.h>
#include <tclap/CmdLine.h>

#include <PixeLINKApi.h>
#include "getsnapshot.h"

int main(int argc, char** argv) {
	try {
		HANDLE hCamera;
		
		TCLAP::CmdLine cmd("Thing to capture from the PixelLink camera", ' ', "0.1A");
		TCLAP::UnlabeledValueArg<std::string> filetype_arg("type",
																												"File type. One of jpg, bmp, tiff, psd, rgb24, rgb24nondib, rgb48, or mono8.",
																												true, "jpg","type");
		TCLAP::UnlabeledValueArg<std::string> filename_arg("filename",
																												"File name to save",
																												true, "","filename");
		
		if (!API_SUCCESS(PxLInitialize(0, &hCamera))) {
			return 1;
		}
		
		cmd.add(filetype_arg);
		cmd.add(filename_arg);
		cmd.parse(argc, argv);
		
		std::string filetype = filetype_arg.getValue();
		std::string filename = filename_arg.getValue();
		
		int retVal;
		if (filetype == "jpg" || filename == "jpeg") {
			retVal = GetSnapshot(hCamera, IMAGE_FORMAT_JPEG, filename);
		} else if (filetype == "bmp") {
			retVal = GetSnapshot(hCamera, IMAGE_FORMAT_BMP, filename);
		} else if (filetype == "tiff") {
			retVal = GetSnapshot(hCamera, IMAGE_FORMAT_TIFF, filename);
		} else if (filetype == "psd") {
			retVal = GetSnapshot(hCamera, IMAGE_FORMAT_PSD, filename);
		} else if (filetype == "rgb24") {
			retVal = GetSnapshot(hCamera, IMAGE_FORMAT_RAW_RGB24, filename);
		} else if (filetype == "rgb24nondib") {
			retVal = GetSnapshot(hCamera, IMAGE_FORMAT_RAW_RGB24_NON_DIB, filename);
		} else if (filetype == "rgb48") {
			retVal = GetSnapshot(hCamera, IMAGE_FORMAT_RAW_RGB48, filename);
		} else if (filetype == "mono8") {
			retVal = GetSnapshot(hCamera, IMAGE_FORMAT_RAW_MONO8, filename);
		} else {
			retVal = 1;
			printf("error: unknown file type %s\n", filetype.c_str());
		}
		
		if (retVal) {
			printf("error capturing from device.");
		} else {
			printf("Saved successfully to %s\n", filename);
		}
		
		return retVal;
	} catch (TCLAP::ArgException &e) {
		printf("error: %s for arg %s\n", e.error().c_str(), e.argId().c_str());
		return 1;
	}
}
