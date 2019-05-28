/***************************************************************************
 *
 *     File: video.cpp
 *
 *     Description:
 *        Controls for the 'Video' tab  in CaptureOEM.
 */

#include <glib.h>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include "video.h"
#include "camera.h"
#include "captureOEM.h"
#include "helpers.h"
#include "videoCaptureDialog.h"

using namespace std;

extern PxLVideo    *gVideoTab;
extern GtkWindow   *gTopLevelWindow;
extern PxLVideoCaptureDialog *gVideoCaptureDialog;


#define COMPRESSION_LOW_QUALITY    2000.0 // 2000:1 compression
#define COMPRESSION_HIGH_QUALITY     10.0 // 10:1 compression
#define COMPRESSION_TARGET          100.0 // 100:1 compression

// Bitrates specified in bits per second
#define BITRATE_FLOOR      10000.0
#define BITRATE_CEILING 50000000.0  // Larger values will crash FFMPEG library


//
// Local prototypes.
//    UI updates can only be done from a gtk thread -- these routines are gtk 'idle' threads
//    and as such are 'UI update safe'. For each 'feature', there there the following functions:
//       . {featureXXX}Deactivate - Makes the controls meaningless (including greying them out)
//       . {featreuXXX}Activate - updates the controls with values from the camera
static gboolean  RefreshComplete (gpointer pData);
static gboolean  FileDeactivate (gpointer pData);
static gboolean  FileActivate (gpointer pData);
static gboolean  CaptureDeactivate (gpointer pData);
static gboolean  CaptureActivate (gpointer pData);
static gboolean  PlaybackDeactivate (gpointer pData);
static gboolean  PlaybackActivate (gpointer pData);
static gboolean  VideoDeactivate (gpointer pData);
static gboolean  VideoActivate (gpointer pData);

extern "C" U32 ClipTermCallback(HANDLE hCamera, U32 uNumFramesCaptured, PXL_RETURN_CODE uRetCode);


/* ---------------------------------------------------------------------------
 * --   Member functions - Public
 * ---------------------------------------------------------------------------
 */
PxLVideo::PxLVideo (GtkBuilder *builder)
: m_captureInProgress (false)
, m_currentDecimation (1)
{
    //
    // Step 1
    //      Find all of the glade controls

    m_fileName = GTK_WIDGET( gtk_builder_get_object( builder, "VideoName_Text" ) );
    m_fileType = GTK_WIDGET( gtk_builder_get_object( builder, "VideoType_Combo" ) );
    m_fileLocation = GTK_WIDGET( gtk_builder_get_object( builder, "VidLocation_Text" ) );
    m_fileLocationBrowser = GTK_WIDGET( gtk_builder_get_object( builder, "VideoLocation_Button" ) );

    m_fileNameIncrement = GTK_WIDGET( gtk_builder_get_object( builder, "VideoNameIncrement_Checkbutton" ) );
    m_captureLaunch = GTK_WIDGET( gtk_builder_get_object( builder, "VideoLaunch_Checkbutton" ) );

    m_encodingType = GTK_WIDGET( gtk_builder_get_object( builder, "EncodingType_Combo" ) );
    m_numFramesToCapture = GTK_WIDGET( gtk_builder_get_object( builder, "VideoNumFrames_Text" ) );
    m_decimation = GTK_WIDGET( gtk_builder_get_object( builder, "VideoDecimation_Text" ) );
    m_keepIntermidiate = GTK_WIDGET( gtk_builder_get_object( builder, "VideoKeepIntermidiate_Checkbutton" ) );

    m_fpsCamera = GTK_WIDGET( gtk_builder_get_object( builder, "VideoCameraFps_Text" ) );
    m_fpsPlayback = GTK_WIDGET( gtk_builder_get_object( builder, "VideoPlaybackFps_Text" ) );
    m_fpsMatch = GTK_WIDGET( gtk_builder_get_object( builder, "VideoMatchFps_Checkbutton" ) );
    m_fpsComment = GTK_WIDGET( gtk_builder_get_object( builder, "VideoComment_Label" ) );

    m_bitrateAuto = GTK_WIDGET( gtk_builder_get_object( builder, "VideoAutoBitrate_Checkbutton" ) );
    m_bitrateSlider =  new PxLSlider (
        GTK_WIDGET( gtk_builder_get_object( builder, "BitrateMin_Label" ) ),
        GTK_WIDGET( gtk_builder_get_object( builder, "BitrateMax_Label" ) ),
        GTK_WIDGET( gtk_builder_get_object( builder, "Bitrate_Scale" ) ),
        GTK_WIDGET( gtk_builder_get_object( builder, "BitrateMax_Text" ) ));

    m_recordTime = GTK_WIDGET( gtk_builder_get_object( builder, "VideoRecordTime_Text" ) );
    m_playbackTime = GTK_WIDGET( gtk_builder_get_object( builder, "VideoPlaybackTime_Text" ) );
    m_fileSize = GTK_WIDGET( gtk_builder_get_object( builder, "VideoFileSize_Text" ) );

    m_captureButton = GTK_WIDGET( gtk_builder_get_object( builder, "VideoCapture_Button" ) );

    //
    // Step 2
    //      Initialize our video defaults
    gtk_entry_set_text (GTK_ENTRY(m_fileName), "clip.mp4");

    // We support two video formats
    gtk_combo_box_text_insert_text (GTK_COMBO_BOX_TEXT(m_fileType),
                                    CLIP_FORMAT_AVI,
                                    "AVI");
    gtk_combo_box_text_insert_text (GTK_COMBO_BOX_TEXT(m_fileType),
                                    CLIP_FORMAT_MP4,
                                    "MP4");
    gtk_combo_box_set_active (GTK_COMBO_BOX(m_fileType),CLIP_FORMAT_MP4);

    // Default to ~/Videos folder
    gchar* videosDir = g_strdup_printf ("%s/Videos", g_get_home_dir());
    gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(m_fileLocationBrowser),
                                        videosDir);
    g_free(videosDir);
    gtk_entry_set_text (GTK_ENTRY(m_fileLocation),
                        gtk_file_chooser_get_current_folder (GTK_FILE_CHOOSER(m_fileLocationBrowser)));

    // We only support 1 encoding format
    gtk_combo_box_text_insert_text (GTK_COMBO_BOX_TEXT(m_encodingType),
                                    0,
                                    "H.264");
    gtk_combo_box_set_active (GTK_COMBO_BOX(m_encodingType),0);

    // By default, 'Match Camera' and 'Auto' bitrate are enabled
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(m_fpsMatch), true);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(m_bitrateAuto), true);

    // Default to a 10 seconds of recording
    gtk_entry_set_text (GTK_ENTRY (m_recordTime), "10.0");

}


PxLVideo::~PxLVideo ()
{
}

void PxLVideo::refreshRequired (bool noCamera)
{
    m_refreshRequired = true;

    if (IsActiveTab (VideoTab))
    {
        if (noCamera)
        {
            // If I am the active tab, then grey out everything
            gdk_threads_add_idle ((GSourceFunc)FileDeactivate, this);
            gdk_threads_add_idle ((GSourceFunc)CaptureDeactivate, this);
            gdk_threads_add_idle ((GSourceFunc)PlaybackDeactivate, this);
            gdk_threads_add_idle ((GSourceFunc)VideoDeactivate, this);
        } else {
            // If I am the active tab, then refresh everything
            gdk_threads_add_idle ((GSourceFunc)FileActivate, this);
            gdk_threads_add_idle ((GSourceFunc)CaptureActivate, this);
            gdk_threads_add_idle ((GSourceFunc)PlaybackActivate, this);
            gdk_threads_add_idle ((GSourceFunc)VideoActivate, this);
        }

        gdk_threads_add_idle ((GSourceFunc)RefreshComplete, this);
    }
}

void PxLVideo::activate()
{
    // I have become the active tab.

    if (gCamera)
    {
        if (m_refreshRequired)
        {
            gdk_threads_add_idle ((GSourceFunc)FileActivate, this);
            gdk_threads_add_idle ((GSourceFunc)CaptureActivate, this);
            gdk_threads_add_idle ((GSourceFunc)PlaybackActivate, this);
            gdk_threads_add_idle ((GSourceFunc)VideoActivate, this);
        }
    } else {
        gdk_threads_add_idle ((GSourceFunc)FileDeactivate, this);
        gdk_threads_add_idle ((GSourceFunc)CaptureDeactivate, this);
        gdk_threads_add_idle ((GSourceFunc)PlaybackDeactivate, this);
        gdk_threads_add_idle ((GSourceFunc)VideoDeactivate, this);
    }
    gdk_threads_add_idle ((GSourceFunc)RefreshComplete, this);
}

void PxLVideo::deactivate()
{
    // I am no longer the active tab.
}


/* ---------------------------------------------------------------------------
 * --   gtk thread callbacks - used to update controls
 * ---------------------------------------------------------------------------
 */

// Indicate that the refresh is no longer outstanding, it has completed.
static gboolean RefreshComplete (gpointer pData)
{
    PxLVideo *pControls = (PxLVideo *)pData;

    pControls->m_refreshRequired = false;
    return false;
}

// Make video file controls unselectable
static gboolean FileDeactivate (gpointer pData)
{
    // These control are always activated

    return false;  //  Only run once....
}

//
// Make video file controls selectable (if appropriate)
static gboolean FileActivate (gpointer pData)
{
    // These control are never deactivated

    return false;  //  Only run once....
}

// Make video capture controls unselectable
static gboolean CaptureDeactivate (gpointer pData)
{
    PxLVideo *pControls = (PxLVideo *)pData;

    gtk_entry_set_text (GTK_ENTRY (pControls->m_numFramesToCapture), "");
    gtk_entry_set_text (GTK_ENTRY (pControls->m_decimation), "");

    gtk_widget_set_sensitive (pControls->m_numFramesToCapture, false);
    gtk_widget_set_sensitive (pControls->m_decimation, false);

    return false;  //  Only run once....
}

//
// Make video capture controls selectable (if appropriate)
static gboolean CaptureActivate (gpointer pData)
{
    PxLVideo *pControls = (PxLVideo *)pData;

    PxLAutoLock lock(&gCameraLock);

    if (gCamera)
    {
        PXL_RETURN_CODE rc = ApiSuccess;
        float cameraFps = 0.0f;

        rc = gCamera->getValue(FEATURE_ACTUAL_FRAME_RATE, &cameraFps);
        if (!API_SUCCESS(rc)) rc = gCamera->getValue(FEATURE_FRAME_RATE, &cameraFps);

        // Default number of frames based on current record time
        int numberOfFrames = (int)(cameraFps * atof (gtk_entry_get_text (GTK_ENTRY (pControls->m_recordTime))));
        char cActualValue[40];
        sprintf (cActualValue, "%d",numberOfFrames);
        gtk_entry_set_text (GTK_ENTRY (pControls->m_numFramesToCapture), cActualValue);
        gtk_entry_set_text (GTK_ENTRY (pControls->m_decimation), "1");

        gtk_widget_set_sensitive (pControls->m_numFramesToCapture, true);
        gtk_widget_set_sensitive (pControls->m_decimation, true);
    }

    return false;  //  Only run once....
}

// Make video playback controls unselectable
static gboolean PlaybackDeactivate (gpointer pData)
{
    PxLVideo *pControls = (PxLVideo *)pData;

    gtk_entry_set_text (GTK_ENTRY (pControls->m_fpsCamera), "");
    gtk_label_set_text (GTK_LABEL (pControls->m_fpsComment), "");

    gtk_widget_set_sensitive (pControls->m_fpsMatch, false);
    gtk_widget_set_sensitive (pControls->m_bitrateAuto, false);

    pControls->m_bitrateSlider->deactivate();

    return false;  //  Only run once....
}

//
// Make video playback controls selectable (if appropriate)
static gboolean PlaybackActivate (gpointer pData)
{
    PxLVideo *pControls = (PxLVideo *)pData;

    PxLAutoLock lock(&gCameraLock);

    if (gCamera)
    {
        PXL_RETURN_CODE rc = ApiSuccess;
        float cameraFps = 0.0f;
        float playbackFps = 0.0;

        //
        // Step 1
        //      Get the camera's frame rate
        rc = gCamera->getValue(FEATURE_ACTUAL_FRAME_RATE, &cameraFps);
        if (!API_SUCCESS(rc)) rc = gCamera->getValue(FEATURE_FRAME_RATE, &cameraFps);
        char cFrameRate[40];
        sprintf (cFrameRate, "%5.3f",cameraFps);
        gtk_entry_set_text (GTK_ENTRY (pControls->m_fpsCamera), cFrameRate);

        //
        // Step 2
        //      Set the playback framerate and comment according to the users settings.
        bool fpsMatch = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(gVideoTab->m_fpsMatch));
        int decimation = atoi (gtk_entry_get_text (GTK_ENTRY (pControls->m_decimation)));
        if (fpsMatch)
        {
            sprintf (cFrameRate, "%5.3f",cameraFps / decimation);
            gtk_entry_set_text (GTK_ENTRY (pControls->m_fpsPlayback), cFrameRate);
            playbackFps = cameraFps / decimation;
        }  else {
            playbackFps = (atof (gtk_entry_get_text (GTK_ENTRY (pControls->m_fpsPlayback)))) / decimation;
        }

        // figure out if the current settings will produce a video whose playback speed is significantly
        // different than the record speed.  Use a metric of 5% difference
        float speedMultiple = (playbackFps*decimation) / cameraFps;
        if (speedMultiple < 0.95)
        {
            gtk_label_set_text (GTK_LABEL (pControls->m_fpsComment), "Video will appear as\nSLOW motion");
        } else if (speedMultiple > 1.05) {
            gtk_label_set_text (GTK_LABEL (pControls->m_fpsComment), "Video will appear as\nFAST motion");
        } else {
            gtk_label_set_text (GTK_LABEL (pControls->m_fpsComment), "");
        }

        //
        // Step 3
        //      Set our quality limits and target
        double bytesPerFrame = (float)gCamera->imageSizeInBytes();
        double lowQuality = max ( (bytesPerFrame * playbackFps * 8) / COMPRESSION_LOW_QUALITY, BITRATE_FLOOR);
        lowQuality = lowQuality / (1000.0 * 1000.0);  // convert to Mbps (library uses 1000, not 1024)
        double highQuality = min ( (bytesPerFrame * playbackFps * 8) / COMPRESSION_HIGH_QUALITY, BITRATE_CEILING);
        highQuality = highQuality / (1000.0 * 1000.0);  // convert to Mbps (library uses 1000, not 1024)
        // The target is either set by the user, or they have us auto set it (the default)
        double target;
        bool autoBitrate = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(gVideoTab->m_bitrateAuto));
        if (autoBitrate)
        {
            target = min (max ((bytesPerFrame * playbackFps * 8) / COMPRESSION_TARGET, BITRATE_FLOOR), BITRATE_CEILING);
            target = target / (1000.0 * 1000.0);  // convert to Mbps (library uses 1000, not 1024)
        } else {
            target = gVideoTab->m_bitrateSlider->getEditValue();
        }
        if (target < lowQuality) target = lowQuality;
        if (target > highQuality) target = highQuality;
        pControls->m_bitrateSlider->setRange(lowQuality, highQuality);
        pControls->m_bitrateSlider->setValue(target);
        pControls->m_bitrateSlider->activate(! autoBitrate);

        //
        // Step 3
        //      Enable the controls
        gtk_widget_set_sensitive (pControls->m_fpsPlayback, ! fpsMatch);
        gtk_widget_set_sensitive (pControls->m_fpsMatch, true);
        gtk_widget_set_sensitive (pControls->m_bitrateAuto, true);
    }

    return false;  //  Only run once....
}

//
// Make image controls unselectable
static gboolean VideoDeactivate (gpointer pData)
{
    PxLVideo *pControls = (PxLVideo *)pData;

    gtk_widget_set_sensitive (pControls->m_captureButton, false);

    return false;  //  Only run once....
}

//
// Make image controls selectable (if appropriate)
static gboolean VideoActivate (gpointer pData)
{
    PxLVideo *pControls = (PxLVideo *)pData;

    int decimation = atoi (gtk_entry_get_text (GTK_ENTRY (pControls->m_decimation)));

    float effectiveFps = atof (gtk_entry_get_text (GTK_ENTRY (pControls->m_fpsPlayback))) / (float)decimation;
    float numFrames = atof (gtk_entry_get_text (GTK_ENTRY (pControls->m_numFramesToCapture)));
    float bitRate = pControls->m_bitrateSlider->getEditValue();

    char cTextValue[40];
    sprintf (cTextValue, "%8.1f", numFrames / effectiveFps);
    gtk_entry_set_text (GTK_ENTRY (pControls->m_playbackTime), cTextValue);

    // The file size (in KBytes), is playbacktime * bitRate, but in MegaBytes
    float fileSize = ((numFrames / effectiveFps) * bitRate) / 8.0;
    if (fileSize < 10.0)
        sprintf (cTextValue, "%8.3f", fileSize);
    else if (fileSize < 100.0)
        sprintf (cTextValue, "%8.2f", fileSize);
    else
        sprintf (cTextValue, "%8.0f", fileSize);
    gtk_entry_set_text (GTK_ENTRY (pControls->m_fileSize), cTextValue);

    gtk_widget_set_sensitive (pControls->m_captureButton, true);

    return false;  //  Only run once....
}

/* ---------------------------------------------------------------------------
 * --   Control functions from the Glade project
 * ---------------------------------------------------------------------------
 */

extern "C" void NewVideoFormatSelected
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{
    if (! gVideoTab) return;
    if (gVideoTab->m_refreshRequired) return;

    int videoFormatIndex = gtk_combo_box_get_active (GTK_COMBO_BOX(gVideoTab->m_fileType));

    switch (videoFormatIndex)
    {
    case CLIP_FORMAT_MP4:
    default:
        ReplaceFileExtension (GTK_ENTRY(gVideoTab->m_fileName), "mp4");
        break;
    case CLIP_FORMAT_AVI:
        ReplaceFileExtension (GTK_ENTRY(gVideoTab->m_fileName), "avi");
        break;
    }
}

extern "C" void NewVideoLocation
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{
    if (! gVideoTab) return;
    if (gVideoTab->m_refreshRequired) return;

    gchar* newFolder = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER(gVideoTab->m_fileLocationBrowser));

    gtk_entry_set_text (GTK_ENTRY(gVideoTab->m_fileLocation), newFolder);
    g_free(newFolder);
}

extern "C" void VideoIncrementToggled
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{
    if (! gVideoTab) return;
    if (gVideoTab->m_refreshRequired) return;

    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(gVideoTab->m_fileNameIncrement)))
    {
        IncrementFileName (GTK_ENTRY(gVideoTab->m_fileName), "%04d");
    }

}

extern "C" void VideoNumFramesChanged
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{
    if (! gVideoTab) return;
    if (gVideoTab->m_refreshRequired) return;

    int decimation = atoi (gtk_entry_get_text (GTK_ENTRY (gVideoTab->m_decimation)));

    float effectiveCameraFps = atof (gtk_entry_get_text (GTK_ENTRY (gVideoTab->m_fpsCamera))) / (float)decimation;
    // Bugzilla.1597 - playbackFps alreay factors in decimation
    float effectivePlaybackFps = atof (gtk_entry_get_text (GTK_ENTRY (gVideoTab->m_fpsPlayback)));
    float numFrames = atof (gtk_entry_get_text (GTK_ENTRY (gVideoTab->m_numFramesToCapture)));

    char cTextValue[40];
    sprintf (cTextValue, "%8.1f", numFrames / effectiveCameraFps);
    gtk_entry_set_text (GTK_ENTRY (gVideoTab->m_recordTime), cTextValue);
    sprintf (cTextValue, "%8.1f", numFrames / effectivePlaybackFps);
    gtk_entry_set_text (GTK_ENTRY (gVideoTab->m_playbackTime), cTextValue);

    // If the playback time changed, so does the estimated file size
    // The file size (in KBytes), is playbackt ime * bitRate, but in MegaBytes
    float bitRate = gVideoTab->m_bitrateSlider->getEditValue();
    float fileSize = ((numFrames / effectivePlaybackFps) * bitRate) / 8.0;
    if (fileSize < 10.0)
        sprintf (cTextValue, "%8.3f", fileSize);
    else if (fileSize < 100.0)
        sprintf (cTextValue, "%8.2f", fileSize);
    else
        sprintf (cTextValue, "%8.0f", fileSize);
    gtk_entry_set_text (GTK_ENTRY (gVideoTab->m_fileSize), cTextValue);
}

extern "C" void VideoDecimationChanged
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{
    int newDecimation = atoi (gtk_entry_get_text (GTK_ENTRY (gVideoTab->m_decimation)));
    float decimationChange = (float)newDecimation / (float)gVideoTab->m_currentDecimation;

    float currententRecordTime = atof (gtk_entry_get_text (GTK_ENTRY (gVideoTab->m_recordTime)));
    float currentPlaybackTime = atof (gtk_entry_get_text (GTK_ENTRY (gVideoTab->m_playbackTime)));
    float currentPlaybackRate = atof (gtk_entry_get_text (GTK_ENTRY (gVideoTab->m_fpsPlayback)));
    float bitRate = gVideoTab->m_bitrateSlider->getEditValue();

    // It will affect the record time and playback time
    char cTextValue[40];
    sprintf (cTextValue, "%8.1f", currententRecordTime * decimationChange);
    gtk_entry_set_text (GTK_ENTRY (gVideoTab->m_recordTime), cTextValue);
    sprintf (cTextValue, "%8.1f", currentPlaybackTime * decimationChange);
    gtk_entry_set_text (GTK_ENTRY (gVideoTab->m_playbackTime), cTextValue);

    // This has the effect of changing the playback frame rate.
    sprintf (cTextValue, "%5.3f", currentPlaybackRate / decimationChange);
    gtk_entry_set_text (GTK_ENTRY (gVideoTab->m_fpsPlayback), cTextValue);

    // And if the playback time changed, so does the file size.
    // The file size (in KBytes), is playbacktime * bitRate, but in MegaBytes
    float fileSize = ((currentPlaybackTime * decimationChange) * bitRate) / 8.0;
    if (fileSize < 10.0)
        sprintf (cTextValue, "%8.3f", fileSize);
    else if (fileSize < 100.0)
        sprintf (cTextValue, "%8.2f", fileSize);
    else
        sprintf (cTextValue, "%8.0f", fileSize);
    gtk_entry_set_text (GTK_ENTRY (gVideoTab->m_fileSize), cTextValue);

    gVideoTab->m_currentDecimation = newDecimation;
}

extern "C" void VideoMatchFpsToggled
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{
    if (! gVideoTab) return;
    if (gVideoTab->m_refreshRequired) return;

    bool bAuto = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(gVideoTab->m_fpsMatch));

    if (bAuto)
    {
        int decimation = atoi (gtk_entry_get_text (GTK_ENTRY (gVideoTab->m_decimation)));
        float cameraFps = atof (gtk_entry_get_text (GTK_ENTRY (gVideoTab->m_fpsCamera)));
        float playbackFps = cameraFps / (float)decimation;
        char cTextValue[40];
        sprintf (cTextValue, "%5.3f", playbackFps);
        gtk_entry_set_text (GTK_ENTRY (gVideoTab->m_fpsPlayback),cTextValue);

        gtk_entry_set_text (GTK_ENTRY (gVideoTab->m_playbackTime),
                                       gtk_entry_get_text (GTK_ENTRY (gVideoTab->m_recordTime)));

        gtk_label_set_text (GTK_LABEL (gVideoTab->m_fpsComment), "");

        // If the playback time changed, so does the file size.
        // The file size (in KBytes), is playbacktime * bitRate, but in MegaBytes
        float currentPlaybackTime = atof (gtk_entry_get_text (GTK_ENTRY (gVideoTab->m_playbackTime)));
        float bitRate = gVideoTab->m_bitrateSlider->getEditValue();
        float fileSize = (currentPlaybackTime * bitRate) / 8.0;
        if (fileSize < 10.0)
            sprintf (cTextValue, "%8.3f", fileSize);
        else if (fileSize < 100.0)
            sprintf (cTextValue, "%8.2f", fileSize);
        else
            sprintf (cTextValue, "%8.0f", fileSize);
        gtk_entry_set_text (GTK_ENTRY (gVideoTab->m_fileSize), cTextValue);
    }
    gtk_widget_set_sensitive (gVideoTab->m_fpsPlayback, ! bAuto);
}

extern "C" void VideoPlaybackFpsChanged
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{
    if (! gVideoTab) return;
    if (gVideoTab->m_refreshRequired) return;

    int decimation = atoi (gtk_entry_get_text (GTK_ENTRY (gVideoTab->m_decimation)));

    float cameraFps = atof (gtk_entry_get_text (GTK_ENTRY (gVideoTab->m_fpsCamera)));
    // The playback video rate already reflects the decimation
    float effectivePlaybackFps = atof (gtk_entry_get_text (GTK_ENTRY (gVideoTab->m_fpsPlayback)));
    float numFrames = atof (gtk_entry_get_text (GTK_ENTRY (gVideoTab->m_numFramesToCapture)));

    char cTextValue[40];
    sprintf (cTextValue, "%8.1f", numFrames / effectivePlaybackFps);
    gtk_entry_set_text (GTK_ENTRY (gVideoTab->m_playbackTime), cTextValue);

    // figure out if the current settings will produce a video whose playback speed is significantly
    // different than the record speed.  Use a metric of 5% difference
    float speedMultiple = (effectivePlaybackFps * (float)decimation) / cameraFps;
    if (speedMultiple < 0.95)
    {
        gtk_label_set_text (GTK_LABEL (gVideoTab->m_fpsComment), "Video will appear as\nSLOW motion");
    } else if (speedMultiple > 1.05) {
        gtk_label_set_text (GTK_LABEL (gVideoTab->m_fpsComment), "Video will appear as\nFAST motion");
    } else {
        gtk_label_set_text (GTK_LABEL (gVideoTab->m_fpsComment), "");
    }

    // If the playback time changed, so does the file size.
    // The file size (in KBytes), is playbacktime * bitRate, but in MegaBytes
    float bitRate = gVideoTab->m_bitrateSlider->getEditValue();
    float fileSize = ((numFrames / effectivePlaybackFps) * bitRate) / 8.0;
    if (fileSize < 10.0)
        sprintf (cTextValue, "%8.3f", fileSize);
    else if (fileSize < 100.0)
        sprintf (cTextValue, "%8.2f", fileSize);
    else
        sprintf (cTextValue, "%8.0f", fileSize);
    gtk_entry_set_text (GTK_ENTRY (gVideoTab->m_fileSize), cTextValue);
}

extern "C" void VideoAutoBitrateToggled
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{
    if (! gVideoTab) return;
    if (gVideoTab->m_refreshRequired) return;

    bool bAuto = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(gVideoTab->m_bitrateAuto));
    if (bAuto && gCamera)
    {
        int decimation = atoi (gtk_entry_get_text (GTK_ENTRY (gVideoTab->m_decimation)));
        float effectivePlaybackFps = atof (gtk_entry_get_text (GTK_ENTRY (gVideoTab->m_fpsPlayback))) / (float)decimation;

        float bytesPerFrame = (float)gCamera->imageSizeInBytes();
        float target = (bytesPerFrame * effectivePlaybackFps * 8) / COMPRESSION_TARGET;
        target = target / (1000.0 * 1000.0);  // convert to Mbps (library uses 1000, not 1024)
        gVideoTab->m_bitrateSlider->setValue(target);
    }
    gVideoTab->m_bitrateSlider->activate(! bAuto);
}

extern "C" void BitrateValueChanged
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{
    if (! gVideoTab) return;
    if (gVideoTab->m_refreshRequired) return;

    float newValue;
    newValue = gVideoTab->m_bitrateSlider->getEditValue();
    gVideoTab->m_bitrateSlider->setValue(newValue);

    // if the bitrate changes, so does the file size.
    // The file size (in KBytes), is playbacktime * bitRate, but in MegaBytes
    float currentPlaybackTime = atof (gtk_entry_get_text (GTK_ENTRY (gVideoTab->m_playbackTime)));
    float fileSize = (currentPlaybackTime * newValue) / 8.0;
    char cTextValue[40];
    if (fileSize < 10.0)
        sprintf (cTextValue, "%8.3f", fileSize);
    else if (fileSize < 100.0)
        sprintf (cTextValue, "%8.2f", fileSize);
    else
        sprintf (cTextValue, "%8.0f", fileSize);
    gtk_entry_set_text (GTK_ENTRY (gVideoTab->m_fileSize), cTextValue);
}

extern "C" void BitrateScaleChanged
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{

    if (! gVideoTab) return;
    if (gVideoTab->m_refreshRequired) return;

    float newValue;
    newValue = gVideoTab->m_bitrateSlider->getScaleValue();
    gVideoTab->m_bitrateSlider->setValue(newValue);

    // if the bitrate changes, so does the file size.
    // The file size (in KBytes), is playbacktime * bitRate, but in MegaBytes
    float currentPlaybackTime = atof (gtk_entry_get_text (GTK_ENTRY (gVideoTab->m_playbackTime)));
    float fileSize = (currentPlaybackTime * newValue) / 8.0;
    char cTextValue[40];
    if (fileSize < 10.0)
        sprintf (cTextValue, "%8.3f", fileSize);
    else if (fileSize < 100.0)
        sprintf (cTextValue, "%8.2f", fileSize);
    else
        sprintf (cTextValue, "%8.0f", fileSize);
    gtk_entry_set_text (GTK_ENTRY (gVideoTab->m_fileSize), cTextValue);
}

extern "C" void VideoRecordTimeChanged
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{
    if (! gVideoTab) return;
    if (gVideoTab->m_refreshRequired) return;

    int decimation = atoi (gtk_entry_get_text (GTK_ENTRY (gVideoTab->m_decimation)));
    float recordTime = atof (gtk_entry_get_text (GTK_ENTRY (gVideoTab->m_recordTime)));

    // Calculate how many frames are necessary to capture for this length of time
    float effectiveCameraFps = atof (gtk_entry_get_text (GTK_ENTRY (gVideoTab->m_fpsCamera))) / (float)decimation;
    float numFrames = recordTime * effectiveCameraFps;
    char cTextValue[40];
    sprintf (cTextValue, "%d", (int)numFrames);
    gtk_entry_set_text (GTK_ENTRY (gVideoTab->m_numFramesToCapture), cTextValue);

    // Now, calculate how long the video playback will be.  Note the playback rate has already
    // been adjusted to accommodate decimation
    float effectivePlaybackFps = atof (gtk_entry_get_text (GTK_ENTRY (gVideoTab->m_fpsPlayback)));
    sprintf (cTextValue, "%8.1f", numFrames / effectivePlaybackFps);
    gtk_entry_set_text (GTK_ENTRY (gVideoTab->m_playbackTime), cTextValue);

    // If the playback time changed, so does the file size.
    // The file size (in KBytes), is playbacktime * bitRate, but in MegaBytes
    float bitRate = gVideoTab->m_bitrateSlider->getEditValue();
    float fileSize = ((numFrames / effectivePlaybackFps) * bitRate) / 8.0;
    if (fileSize < 10.0)
        sprintf (cTextValue, "%8.3f", fileSize);
    else if (fileSize < 100.0)
        sprintf (cTextValue, "%8.2f", fileSize);
    else
        sprintf (cTextValue, "%8.0f", fileSize);
    gtk_entry_set_text (GTK_ENTRY (gVideoTab->m_fileSize), cTextValue);
}

// Some static varaibles we use for clip captures
static U32 s_lastClipCaptureNumFrames;
static PXL_RETURN_CODE s_lastClipCaptureRetCode;
static bool s_captureInProgress;

extern "C" void VideoCaptureButtonPressed
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{

    if (! gVideoTab) return;
    if (gVideoTab->m_refreshRequired) return;
    if (! gCamera) return;

    //
    // Step 1
    //      Start the stream (if it's not already running
    PXL_RETURN_CODE rc = ApiSuccess;
    bool wasStreaming = gCamera->streaming();
    if (!wasStreaming) rc = gCamera->startStream();
    if (!API_SUCCESS (rc)) return;

    //
    // Step 2
    //      Figure out the name and path for the intermediate '.h264' file
    string fullName =  gtk_entry_get_text (GTK_ENTRY(gVideoTab->m_fileName));
    size_t dotPos = fullName.rfind('.');
    if (dotPos == string::npos) dotPos = fullName.length();
    string justName = fullName.substr (0, dotPos);

    // These strings will be 'released' when the clip is finished.
    gVideoTab->m_encodedFilename = g_strdup_printf ("%s/%s.h264",
                                                    gtk_entry_get_text (GTK_ENTRY(gVideoTab->m_fileLocation)),
                                                    justName.c_str());
    gVideoTab->m_videoFilename = g_strdup_printf ("%s/%s",
                                                   gtk_entry_get_text (GTK_ENTRY(gVideoTab->m_fileLocation)),
                                                   fullName.c_str());

    //
    // Step 3
    //      Disable the capture button.  It will be re-enabled when the termination callback is made.
    //      We do this so user cannot attempt another capture until this one
    //      is done.
    gtk_widget_set_sensitive (gVideoTab->m_captureButton, false);

    //
    // Step 4
    //      Capture the clip

    s_lastClipCaptureNumFrames = 0;
    s_lastClipCaptureRetCode = ApiSuccess;
    s_captureInProgress = true;

    int   bitRate = (int)(gVideoTab->m_bitrateSlider->getEditValue() * 1000.0 * 1000.0); // library uses 1000, not 1024
    int   decimation = atoi (gtk_entry_get_text (GTK_ENTRY(gVideoTab->m_decimation)));
    float playbackFramerate = atof (gtk_entry_get_text (GTK_ENTRY(gVideoTab->m_fpsPlayback)));
    // TEMP PEC +++
    //     Uncomment to debug
    //printf ("Creating Encoded clip: %s, numFrames:%d decimation:%d bitRate:%d playbackRate:%f\n",
    //         gVideoTab->m_encodedFilename,
    //         atoi (gtk_entry_get_text (GTK_ENTRY(gVideoTab->m_numFramesToCapture))),
    //         decimation,
    //         bitRate,
    //         playbackFramerate);
    // ---
    rc = gCamera->getH264Clip (atoi (gtk_entry_get_text (GTK_ENTRY(gVideoTab->m_numFramesToCapture))),
                               decimation,
                               gVideoTab->m_encodedFilename,
                               playbackFramerate,
                               bitRate,
                               ClipTermCallback);

    //
    // Step 5
    //      Did we in fact start a video capture, or was there a problem.  If there was a problem
    //      that means our termination function will not be called, then we need to re-enable
    //      the capture button
    if (!API_SUCCESS(rc) && // <-- some sort of problem
        (rc != ApiStreamStopped && rc != ApiNoStreamError)) // <-- and was not the user quitting
    {
        gtk_widget_set_sensitive (gVideoTab->m_captureButton, true); // so we better re-enable the button
    }

    if (rc == ApiUnsupportedPixelFormatError)
    {
        // Pop up an error message
        GtkWidget *popupError = gtk_message_dialog_new (gTopLevelWindow,
                                             GTK_DIALOG_DESTROY_WITH_PARENT,
                                             GTK_MESSAGE_ERROR,
                                             GTK_BUTTONS_CLOSE,
                                             "Unsupported Pixel Format.\nTry MONO8 for mono cameras, or YUV for color");
        gtk_dialog_run (GTK_DIALOG (popupError));  // This makes the popup modal
        gtk_widget_destroy (popupError);
    } else if (rc == ApiH264FrameTooLargeError) {
            // Pop up an error message
            GtkWidget *popupError = gtk_message_dialog_new (gTopLevelWindow,
                                                 GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 GTK_MESSAGE_ERROR,
                                                 GTK_BUTTONS_CLOSE,
                                                 "Frame too large for H264 compression.\nReduce the ROI to <= 9 Megapixels");
            gtk_dialog_run (GTK_DIALOG (popupError));  // This makes the popup modal
            gtk_widget_destroy (popupError);
    } else if (API_SUCCESS(rc)) {
        gVideoTab->m_captureInProgress = true;
        gVideoCaptureDialog->begin (atoi (gtk_entry_get_text (GTK_ENTRY(gVideoTab->m_recordTime))) * 1000, // in milliseconds
                                    &gVideoTab->m_captureInProgress);
        // Make the video capture dialog modal
        gtk_dialog_run (GTK_DIALOG(gVideoCaptureDialog->m_windowVideoCapture));
        gVideoTab->m_captureInProgress = false;

        //
        // Step 6
        //      Bump up the file name if we need to
        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(gVideoTab->m_fileNameIncrement)))
        {
            IncrementFileName (GTK_ENTRY(gVideoTab->m_fileName), "%04d");
        }

    }

    //
    // Step 7
    //      Stop the stream if we started it.
    if (!wasStreaming) gCamera->stopStream();

}

/**
* Function: ClipTermCallback
* Purpose:  This function gets called by the API when a clip capture finishes.
*/
extern "C" U32 ClipTermCallback(HANDLE hCamera, U32 uNumFramesStreamed, PXL_RETURN_CODE uRetCode)
{
    // TEMP PEC +++
    //     Uncomment to debug
    //printf ("Clip Encoding done! rc:0x%x, numFramesStreamed:%d\n", uRetCode, uNumFramesStreamed);
    // ----
    //
    // Step 1
    //      display a warning if we could not capture all streamed images.
    if (uRetCode == ApiSuccessWithFrameLoss)
    {
        //pop-up a warning for the user
        //GtkWidget *popup = gtk_message_dialog_new (GTK_WINDOW(gVideoCaptureDialog->m_windowVideoCapture),
        GtkWidget *popup = gtk_message_dialog_new (gTopLevelWindow,
                                                   GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_WARNING,
                                                   GTK_BUTTONS_CLOSE,
                                                   "The capture could not keep pace with the stream.\nIt took %d images to capture %d of them.",
                                                   uNumFramesStreamed,
                                                   atoi (gtk_entry_get_text (GTK_ENTRY(gVideoTab->m_numFramesToCapture))));
        gtk_dialog_run (GTK_DIALOG (popup));  // This makes the popup modal
        gtk_widget_destroy (popup);
    }

    //
    // Step 2
    //      Format the clip
    if (API_SUCCESS(uRetCode) && gCamera)
    {
        // TEMP PEC +++
        //     Uncomment to debug
        //printf ("Using Encoded clip: %s\n", gVideoTab->m_encodedFilename);
        //printf ("To Create videoclip: %s\n", gVideoTab->m_videoFilename);
        // ---
        gCamera->formatH264Clip(gVideoTab->m_encodedFilename,
                                gVideoTab->m_videoFilename,
                                gtk_combo_box_get_active (GTK_COMBO_BOX(gVideoTab->m_fileType)));
    } else if (uRetCode != ApiStreamStopped &&
               uRetCode != ApiNoStreamError) { // Bugzilla.1338 -- don't report error if the user cancelled
        GtkWidget *popup;
        if (uRetCode == ApiH264InsufficientDataError)
        {
            popup = gtk_message_dialog_new (gTopLevelWindow,
                                            GTK_DIALOG_DESTROY_WITH_PARENT,
                                            GTK_MESSAGE_WARNING,
                                            GTK_BUTTONS_CLOSE,
                                            "The compression engine needs more frames.\nIncrease 'Number of Frames' (or 'Record Time').");
        } else {
            popup = gtk_message_dialog_new (gTopLevelWindow,
                                            GTK_DIALOG_DESTROY_WITH_PARENT,
                                            GTK_MESSAGE_WARNING,
                                            GTK_BUTTONS_CLOSE,
                                            "Capture failed.\nRC: 0x%X", uRetCode);
        }
        gtk_dialog_run (GTK_DIALOG (popup));  // This makes the popup modal
        gtk_widget_destroy (popup);
    }

    gVideoTab->m_captureInProgress = false;
    // We need to re-activate the video capture button.  Do this in a gdk thread.
    //gtk_widget_set_sensitive (gVideoTab->m_captureButton, true);
    gdk_threads_add_idle ((GSourceFunc)VideoActivate, gVideoTab);

    //
    // Step 3
    //      Erase the intermediate file
    if (! gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(gVideoTab->m_keepIntermidiate)))
    {
        remove (gVideoTab->m_encodedFilename);
    }

    //
    // Step 4
    //      Launch a viewer if the user requested it.
    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(gVideoTab->m_captureLaunch)))
    {
        pid_t pid = fork();

        if (pid == 0)
        {
            execl ("/usr/bin/xdg-open", "xdg-open", gVideoTab->m_videoFilename, NULL);
        }
    }

    g_free (gVideoTab->m_encodedFilename);
    g_free (gVideoTab->m_videoFilename);


    return ApiSuccess;
}




