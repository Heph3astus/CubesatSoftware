//
// This demonstrates how to create videos using the PixeLINK API.  Specifically, 
// this applocation will create videos that play back normal motion, to slow motion.
//
// This application showcases how to use:
//    - PxLGetH264Clip
//    - PxLFormatClipEx
//
// NOTE: This application assumes there is at most, one PixeLINK camera connected to the system

// This application is intended to work with very high frame rates.  That is
// the camera is outputting image data at a very high rate.  For most systems
// we will need to do more compression to accomodate this high capture data
// rate.  This takes more procssing power to do the compression, but on most 
// systems, it's the disk access that dictates our ability to sink image
// data at high data rates.  So, we reduce the bitrate (and quality) to do
// more compression, so that we are less likely to loose capture data.

#include <iostream>
#include <stdio.h>
#include <stdexcept>
#include <unistd.h>
#include <stdlib.h>
#include <cassert>
#include <vector>
#include <string.h>
#include "PixeLINKApi.h"
#include "LinuxUtil.h"

using namespace std;

//
// A few useful defines and enums.
//
#define ASSERT(x)	do { assert((x)); } while(0)
#define A_OK            0  // non-zero error codes
#define GENERAL_ERROR   1

#define DEFAULT_PLAYBACK_FRAME_RATE (25) // in frames/second (25 == smooth(ish) video)
#define DEFAULT_RECORD_DURATION     (20) // in seconds
#define CLIP_PLAYBACK_BITRATE       (CLIP_PLAYBACK_BITRATE_DEFAULT/3)

// Prototypes to allow top-down structure
void  usage (char* argv[]);
int   getParameters (int argc, char* argv[], U32* playTime, U32* bitRate, U32* frameRate, char* aviFileName);
float effectiveFrameRate (HANDLE hCamera);
static U32 CaptureDoneCallback(HANDLE hCamera, U32 numFramesCapture, PXL_RETURN_CODE returnCode);

// 'Globals' shared between our main line, and the clip callback
static bool captureFinished = false;
static U32  numImagesStreamed = 0;
static PXL_RETURN_CODE captureRc = ApiSuccess;

int main (int argc, char* argv[])
{
    vector<char> aviFile(256,0);
    U32  recordTime;
    U32  bitRate;
    U32  frameRate;
    
    //
    // Step 1
    //      Validate the user parameters, getting poll period and invert value
    if (A_OK != getParameters(argc, argv, &recordTime, &bitRate, &frameRate, &aviFile[0]))
    {
        usage(argv);
        return GENERAL_ERROR;
    }

    //
    // Step 2
    //		Grab our camera
    HANDLE          hCamera;
    PXL_RETURN_CODE rc = A_OK;
    U32             uNumberOfCameras = 0;

    rc = PxLGetNumberCameras (NULL, &uNumberOfCameras);
    if (!API_SUCCESS(rc) || uNumberOfCameras != 1)
    {
        printf (" Error:  There should be exactly one PixeLINK camera connected.\n");
        return GENERAL_ERROR;
    }
    rc = PxLInitialize (0, &hCamera);
    if (!API_SUCCESS(rc))
    {
        printf (" Error:  Could not initialize the camera.\n");
        return GENERAL_ERROR;
    }

    // 
    // Step 3
    //      Determine the effective frame rate for the camera, and the number of images we will need to
    //      capture the video of the requested length then start the stream
    float cameraFps = effectiveFrameRate(hCamera);
    if (cameraFps < (float)frameRate)
    {
        // Although this will 'work', such a configuration will create a fast motion video, 
        // and a rather poor quality one at that.  For these instances, you should be using 
        // the demo FastMotionVideo
        printf (" Error:  The camera's frame rate is currently %.2f; it should be > %.2f.\n", cameraFps, ((float)frameRate));
        PxLUninitialize (hCamera);
        return GENERAL_ERROR;
    }
    U32   numImages = (U32)(((float)recordTime) * cameraFps);
    if (!API_SUCCESS (PxLSetStreamState (hCamera, START_STREAM)))
    {
        printf (" Error:  Could not start the stream.\n");
        PxLUninitialize (hCamera);
        return GENERAL_ERROR;
    }

    //
    // Step 4
    //      Capture the required images into the clip
    vector<char> h264File(256,0);
    strncpy (&h264File[0],&aviFile[0],256);
    strncat (&h264File[0],".h264",256);
    CLIP_ENCODING_INFO clipInfo;

    clipInfo.uStreamEncoding = CLIP_ENCODING_H264;
    clipInfo.uDecimationFactor = 1;  // No decimation
    clipInfo.playbackFrameRate = (float)frameRate;
    clipInfo.playbackBitRate = bitRate;

    printf (" Recording %d seconds of h264 compressed video (based on %d images).  Press any key to abort...\n\n",
            recordTime, numImages);
    captureFinished = false;
    rc = PxLGetEncodedClip (hCamera, numImages, &h264File[0], &clipInfo, CaptureDoneCallback);
    if (API_SUCCESS(rc))
    {
        while (!captureFinished)
        {
            if (kbhit())
            {
                // User wants to abort.  Tell the API to abort the capture by stopping the stream.  This should call our callback with
                // an error.
                PxLSetStreamState (hCamera, STOP_STREAM);
            } else {
                // No need to steal a bunck of cpu cycles on a loop doing nothing -- sleep for but until it's time to check for keyboard
                // input again.
                usleep (500 * 1000);
            }
        }
    }
    PxLSetStreamState (hCamera, STOP_STREAM);  //already stopped if user aborted, but that's OK

    //
    // Step 5
    //      Clip capture is done.  If it completed OK, create the clip video file (.avi)
    if (API_SUCCESS (rc))
    {
        if (API_SUCCESS (captureRc))
        {
            if (captureRc == ApiSuccessWithFrameLoss)
            {
                printf ("Warning\n %d images had to be streamed to capture %d of them.\n",numImagesStreamed,numImages);  
            } else {
                printf ("Success\n %d images captured.\n",numImages);
            }

            //
            // Step 6
            //      convert the clip capture file, into a .avi video file
            rc = PxLFormatClipEx (&h264File[0], &aviFile[0], CLIP_ENCODING_H264, CLIP_FORMAT_AVI);
        } else {
            rc = captureRc;
        }
    }

    if (!API_SUCCESS(rc))
    {
        printf ("Error\n PxLGetH264Clip/PxLFormatClipEx returned 0x%x\n", rc);
    }
    
    PxLUninitialize (hCamera);
    return rc;

}


//
// Function that's called when PxLGetClip is finished capturing frames, or can't continue
// capturing frames.
//
static U32 CaptureDoneCallback(HANDLE hCamera, U32 numFramesCapture, PXL_RETURN_CODE returnCode)
{
    // Just record the capture information into our shared (global) varaibles so the main line
    // can report/take action on the result.
    numImagesStreamed = numFramesCapture;
    captureRc = returnCode;
    captureFinished = true;
    return ApiSuccess;
}

// 
// Returns the frame rate being used by the camera.  Ideally, this is simply FEAUTURE_ACTUAL_FRAME_RATE, but
// some older cameras do not support that.  If that is the case, use FEATURE_FRAME_RATE, which is 
// always supported.
//
float effectiveFrameRate (HANDLE hCamera)
{
    float frameRate = DEFAULT_PLAYBACK_FRAME_RATE;
    PXL_RETURN_CODE rc;
    
    //
    // Step 1
    //      Determine if the camera supports FEATURE_ACTUAL_FRAME_RATE

    // How big a buffer will we need to hold the information about the trigger feature?
    U32 bufferSize = -1;
    U32 frameRateFeature = FEATURE_FRAME_RATE;
    if (API_SUCCESS (PxLGetCameraFeatures(hCamera, FEATURE_ACTUAL_FRAME_RATE, NULL, &bufferSize)))
    {
	    ASSERT(bufferSize > 0);

	    // Declare a buffer and read the feature information
	    vector<U8> buffer(bufferSize, 0);  // zero-initialized buffer
	    CAMERA_FEATURES* pCameraFeatures = (CAMERA_FEATURES*)&buffer[0];
	    if (API_SUCCESS (PxLGetCameraFeatures(hCamera, FEATURE_ACTUAL_FRAME_RATE, pCameraFeatures, &bufferSize)))
            {
                //
                //  Step 2
                //      Get the 'best available' frame rate of the camera
                if (pCameraFeatures[0].pFeatures->uFlags & FEATURE_FLAG_PRESENCE)
                {
                    frameRateFeature = FEATURE_ACTUAL_FRAME_RATE;
                }
            }
    }
    
    U32 flags;
    U32 numParams = 1;
    rc = PxLGetFeature (hCamera, frameRateFeature, &flags, &numParams, &frameRate);
    ASSERT(API_SUCCESS(rc));

    return frameRate;
}

void usage (char* argv[])
{
        printf("\n This application will capture video for the specified number of seconds.  If the camera's frame\n");
        printf(" rate is playback_framrate, then the generated (a .avi file) will play back at 'normal speed'.\n");
        printf(" However, this application allows you to create 'slow motion' videos by setting the camera up so\n");
        printf(" that it uses frame rates that are higher than playback_framrate.  In so doing, you can create\n");
        printf(" very 'dramatic' slow motion videos, especially when using a very high camera frame rates, \n");
        printf(" several hundred fps or more.  Note however, that very high frame rates are only possible with\n");
        printf(" very short exposure times, which (generally) require a lot of light. In addition to adjusting\n");
        printf(" your lighting accordingly, consider using the following strategies to faclitate very high frame\n");
        printf(" rates:\n");
        printf("    - Adding Pixel Addressing.  Binning in particular will accomodate faster frame rates\n");
        printf("      and help 'brighten' dark images\n");
        printf("    - Adding Gain to brighten the images\n");
        printf("    - Reducing the ROI to accomodate faster frame rates\n\n");
        printf("    Usage: %s [-t capture_duration] [-b playback_bitrate] [-f playback_framerate] video_name \n", argv[0]);
        printf("       where: \n");
        printf("          -t capture_duration   How much time to spend captureing video (in seconds) \n");
        printf("          -b playback_bitrate   Bitrate (b/s) that will be used for playback.  This value\n");
        printf("                                provides guidance on how much compresion to use.  More\n");
        printf("                                compression means lower quality.  Generally, if the video\n");
        printf("                                capture cannot keep pace with the cameras stream (as indicated\n");
        printf("                                by this application issueing a warning), then you will want to\n");
        printf("                                to lower this value.  Its default value is %d \n", CLIP_PLAYBACK_BITRATE);
        printf("          -f playback_framerate Framerate (f/s) that will be used for playback.  This value\n");
        printf("                                determines the duration of the clip. If this value matches the\n");
        printf("                                camera's framerate, then the playback duration will match the\n");
        printf("                                capture_duration.  Its default value is %d \n", DEFAULT_PLAYBACK_FRAME_RATE);
        printf("          video_name            Name of file to generate (it will be postfixed with .avi) \n");
        printf("    Example: \n");
        printf("        %s -t 30 clip \n", argv[0]);
        printf("              This will capture a video, called clip.avi, that records for about 30 seconds. \n");
}

int getParameters (int argc, char* argv[], U32* recordTime, U32* bitRate, U32* frameRate, char* fileName)
{
    
    // Default our local copies to the user supplied values
    U32  uRecordTime = DEFAULT_RECORD_DURATION;
    U32  uBitRate = CLIP_PLAYBACK_BITRATE;
    U32  uFrameRate = DEFAULT_PLAYBACK_FRAME_RATE;
    char*  nameRoot;  
   
    // 
    // Step 1
    //      Simple parameter parameter check
    if (argc < 2 ||  // No play time specified; use the default
        argc > 8)    // User specifies the play time
    {
        printf ("\n ERROR -- Incorrect number of parameters\n");
        return GENERAL_ERROR;
    }

    //
    // Step 2
    //      Parse the command line looking for the optional parameters.
    int parm;
    for (int i=1; i<argc-1; i++)
    {
        if (!strcmp(argv[i],"-t") ||
            !strcmp(argv[i],"-T"))
        {
            if (i+1 >= argc) return GENERAL_ERROR;
            parm = atoi(argv[i+1]);
            if (parm < 1) return  GENERAL_ERROR;
            uRecordTime = (U32) parm;
            i++;
        } else if (!strcmp(argv[i],"-b") ||
                   !strcmp(argv[i],"-B")) {
            if (i+1 >= argc) return GENERAL_ERROR;
            parm = atoi(argv[i+1]);
            if (parm < 1000) return  GENERAL_ERROR;
            uBitRate = (U32) parm;
            i++;
        } else if (!strcmp(argv[i],"-f") ||
                   !strcmp(argv[i],"-F")) {
            if (i+1 >= argc) return GENERAL_ERROR;
            parm = atoi(argv[i+1]);
            if (parm < 1) return  GENERAL_ERROR;
            uFrameRate = (U32) parm;
            i++;
        } else {
            return GENERAL_ERROR;
        }
    }

    //
    // Step 3
    //      The last parameter must be the file name
    if (argv[argc-1][0] == '-') return GENERAL_ERROR;
    nameRoot = argv[argc-1];

    //
    // Step 4
    //      Let the app know the user parameters.  
    *recordTime = uRecordTime;
    *bitRate = uBitRate;
    *frameRate = uFrameRate;
    strncpy (fileName, nameRoot, 256);
    strncat (fileName, ".avi", 256);
    return A_OK;
}



