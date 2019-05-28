/***************************************************************************
 *
 *     File: info.cpp
 *
 *     Description:
 *        Controls for the 'Info' tab  in CaptureOEM.
 */

#include <stdlib.h>
#include <string.h>
#include "info.h"
#include "camera.h"
#include "captureOEM.h"
#include "helpers.h"
#include "controls.h"
#include "stream.h"
#include "gpio.h"
#include "lens.h"
#include "autoRoi.h"

using namespace std;

extern PxLInfo      *gInfoTab;
extern PxLControls  *gControlsTab;
extern PxLStream    *gStreamTab;
extern PxLGpio      *gGpioTab;
extern PxLLens      *gLensTab;
extern PxLAutoRoi   *gAutoRoiTab;
extern GtkWindow    *gTopLevelWindow;

//
// Local prototypes.
//    UI updates can only be done from a gtk thread -- these routines are gtk 'idle' threads
//    and as such are 'UI update safe'. For each 'feature', there there the following functions:
//       . {featureXXX}Deactivate - Makes the controls meaningless (including greying them out)
//       . {featreuXXX}Activate - updates the controls with values from the camera
static gboolean  RefreshComplete (gpointer pData);
static gboolean  CameraDeactivate (gpointer pData);
static gboolean  CameraActivate (gpointer pData);

// Prototypes for functions used update the temperature.
PXL_RETURN_CODE GetCurrentTemperatures();
void UpdateTemperatureControls();
const PxLFeaturePollFunctions temperatureFuncs (GetCurrentTemperatures, UpdateTemperatureControls);

// Define the thresholds used as user temperature warnings.  These values were taken from Window C-OEM
//static const float sensorWarm = 45.0;
static const float sensorWarm = 38.0;
static const float sensorHot  = 50.0;
//static const float bodyWarm   = 62.0;
static const float bodyWarm   = 43.0;
static const float bodyHot    = 70.0;

/* ---------------------------------------------------------------------------
 * --   Member functions - Public
 * ---------------------------------------------------------------------------
 */
PxLInfo::PxLInfo (GtkBuilder *builder)
: m_sensorTempLast(0.0)
, m_bodyTempLast(0.0)
, m_hasSensorTemperature(false)
, m_hasBodyTemperature(false)
{
    //
    // Step 1
    //      Find all of the glade controls

    m_loadSettingsButton = GTK_WIDGET( gtk_builder_get_object( builder, "LoadSettings_Button" ) );
    m_loadFactoryDefaults = GTK_WIDGET( gtk_builder_get_object( builder, "FactoryDefaults_Radio" ) );
    m_loadPowerupDefaults = GTK_WIDGET( gtk_builder_get_object( builder, "PowerupDefaults_Radio" ) );
    m_saveSettingsButton = GTK_WIDGET( gtk_builder_get_object( builder, "SaveSettings_Button" ) );

    m_tempSensor = GTK_WIDGET( gtk_builder_get_object( builder, "SensorTemp_Text" ) );
    m_tempBody = GTK_WIDGET( gtk_builder_get_object( builder, "BodyTemp_Text" ) );

    m_serialNum = GTK_WIDGET( gtk_builder_get_object( builder, "SerialNumber_Text" ) );
    m_vendorName = GTK_WIDGET( gtk_builder_get_object( builder, "VendorName_Text" ) );
    m_productName = GTK_WIDGET( gtk_builder_get_object( builder, "ProductName_Text" ) );
    m_cameraName = GTK_WIDGET( gtk_builder_get_object( builder, "CameraName_Text" ) );

    m_versionFirmware = GTK_WIDGET( gtk_builder_get_object( builder, "FirmwareVersion_Text" ) );
    m_versionFpga = GTK_WIDGET( gtk_builder_get_object( builder, "FpgaVersion_Text" ) );
    m_versionXml = GTK_WIDGET( gtk_builder_get_object( builder, "XmlVersion_Text" ) );

    m_versionPackage = GTK_WIDGET( gtk_builder_get_object( builder, "PackageVersion_Text" ) );
    m_versionCaptureOem = GTK_WIDGET( gtk_builder_get_object( builder, "CaptureOemVersion_Text" ) );

    m_libraryInfo = GTK_WIDGET( gtk_builder_get_object( builder, "LibraryInfo_Textview" ) );

    //
    // Step 2
    //      Initialize all of the host software controls, as they are not going to change
    char defaultVersion[] = "Unknown";

    //
    // Step 2.1
    //      The package version is housed in a file located at ../currentVersion.txt
    size_t   bufLen = 0;
    ssize_t  bytesRead = 0;
    int      numLines = 0;
    int      libNameLen;
    char*    libNameEnd;
    char*    lineRead = NULL;
    FILE *pFile = fopen ("../currentVersion.txt", "r");
    if (pFile)
    {
        bytesRead = getline (&lineRead, &bufLen, pFile);
        // Remove the '\n' from the line
        if (bytesRead > 0 && lineRead[bytesRead-1] == '\n') lineRead[bytesRead-1] = 0;
        fclose (pFile);
    }
    gtk_entry_set_text (GTK_ENTRY(m_versionPackage), bytesRead > 0 ? lineRead : defaultVersion);
    free (lineRead);

    //
    // Step 2.2
    //      Capture OEM version
    gtk_entry_set_text (GTK_ENTRY(m_versionCaptureOem), CaptureOEMVersion);

    //
    // Step 2.3
    //      Get the defendant library information
    GtkTextBuffer *buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (m_libraryInfo));
    GtkTextIter   iter;
    gtk_text_buffer_get_iter_at_offset (buf, &iter, -1);

    bufLen = 0;
    bytesRead = 0;
    
    lineRead = NULL;
    pFile = popen ("/usr/bin/ldd ./captureOEM", "r");
    if (pFile)
    {
        while (bytesRead >= 0)
        {
            bytesRead = getline (&lineRead, &bufLen, pFile);
            if (bytesRead > 0)
            {
                numLines++;
		        // **** BUG ALERT ***
                //      For reasons I don't understand, if the nature of the text that I add
                //      via gtk_text_buffer_insert is sufficiently 'wide' or 'long; such that
                //      gtk needs to add scroll bars, then the top portion of c-OEM will appear in
                //      reverse video (the camera select drop down and the stream control buttons).
                //
                //      Perhaps this is bug with the versions of the gtk libraries I'm using?
                
                //      To that end, we will work around this limitation by only showing the library 
                //      name (which is really all we need anyways), and limit the number of lobraries 
                //      that are displayed.
                //gtk_text_buffer_insert (buf, &iter, lineRead+1, -1);
                libNameEnd = strstr (lineRead+1, "=>");
                libNameLen = libNameEnd ? libNameEnd-lineRead-1 : -1;
                gtk_text_buffer_insert (buf, &iter, lineRead+1, libNameLen);
                gtk_text_buffer_insert (buf, &iter, (numLines % 4 ? " " : "\n"), -1);
                if (numLines >= 50) break;
            }
        }
        fclose (pFile);
    }
    free (lineRead);
}


PxLInfo::~PxLInfo ()
{
}

void PxLInfo::refreshRequired (bool noCamera)
{
    m_refreshRequired = true;

    if (IsActiveTab (InfoTab))
    {
        if (noCamera)
        {
            // If I am the active tab, then grey out everything
            gdk_threads_add_idle ((GSourceFunc)CameraDeactivate, this);
        } else {
            // If I am the active tab, then refresh everything
            gdk_threads_add_idle ((GSourceFunc)CameraActivate, this);
        }

        gdk_threads_add_idle ((GSourceFunc)RefreshComplete, this);
    }
}

void PxLInfo::activate()
{
    // I have become the active tab.

    if (gCamera)
    {
        if (m_refreshRequired)
        {
            gdk_threads_add_idle ((GSourceFunc)CameraActivate, this);
        } else {
            gCamera->m_poller->pollRemove(temperatureFuncs);
        }
    } else {
        gdk_threads_add_idle ((GSourceFunc)CameraDeactivate, this);
    }
    gdk_threads_add_idle ((GSourceFunc)RefreshComplete, this);
}

void PxLInfo::deactivate()
{
    // I am no longer the active tab.
    if (gCamera)
    {
        gCamera->m_poller->pollRemove(temperatureFuncs);
    }
}


/* ---------------------------------------------------------------------------
 * --   gtk thread callbacks - used to update controls
 * ---------------------------------------------------------------------------
 */

// Indicate that the refresh is no longer outstanding, it has completed.
static gboolean RefreshComplete (gpointer pData)
{
    PxLInfo *pInfo = (PxLInfo *)pData;

    pInfo->m_refreshRequired = false;
    return false;
}

//
// Make Filter controls unselectable
static gboolean CameraDeactivate (gpointer pData)
{
    PxLInfo *pInfo = (PxLInfo *)pData;

    gtk_widget_set_sensitive (pInfo->m_loadSettingsButton, false);
    gtk_widget_set_sensitive (pInfo->m_loadFactoryDefaults, false);
    gtk_widget_set_sensitive (pInfo->m_loadPowerupDefaults, false);
    gtk_widget_set_sensitive (pInfo->m_saveSettingsButton, false);

    gtk_entry_set_text (GTK_ENTRY (gInfoTab->m_tempSensor), "");
    gtk_entry_set_text (GTK_ENTRY (gInfoTab->m_tempBody), "");

    gtk_entry_set_text (GTK_ENTRY (pInfo->m_serialNum), "");
    gtk_entry_set_text (GTK_ENTRY (pInfo->m_vendorName), "");
    gtk_entry_set_text (GTK_ENTRY (pInfo->m_productName), "");
    gtk_entry_set_text (GTK_ENTRY (pInfo->m_cameraName), "");

    gtk_entry_set_text (GTK_ENTRY (pInfo->m_versionFirmware), "");
    gtk_entry_set_text (GTK_ENTRY (pInfo->m_versionFpga), "");
    gtk_entry_set_text (GTK_ENTRY (pInfo->m_versionXml), "");

    if (gCamera)
    {
        gCamera->m_poller->pollRemove(temperatureFuncs);
    }

    return false;  //  Only run once....
}

//
// Make filter controls selectable (if appropriate)
static gboolean CameraActivate (gpointer pData)
{
    PxLInfo *pInfo = (PxLInfo *)pData;

    if (gCamera)
    {
        PxLAutoLock lock(&gCameraLock);

        gtk_widget_set_sensitive (pInfo->m_loadSettingsButton, true);
        gtk_widget_set_sensitive (pInfo->m_loadFactoryDefaults, true);
        gtk_widget_set_sensitive (pInfo->m_loadPowerupDefaults, true);
        gtk_widget_set_sensitive (pInfo->m_saveSettingsButton, true);

        // add our functions to the continuous poller
        pInfo->m_hasSensorTemperature = gCamera->supported(FEATURE_SENSOR_TEMPERATURE);
        pInfo->m_hasBodyTemperature = gCamera->supported(FEATURE_BODY_TEMPERATURE);
        if (pInfo->m_hasSensorTemperature || pInfo->m_hasBodyTemperature)
        {
            gCamera->m_poller->pollAdd(temperatureFuncs);
        }

        gtk_widget_set_sensitive (pInfo->m_versionFirmware, false);
        gtk_widget_set_sensitive (pInfo->m_versionFpga, false);
        gtk_widget_set_sensitive (pInfo->m_versionXml, false);

        CAMERA_INFO cameraInfo;
        if (API_SUCCESS (gCamera->getCameraInfo(cameraInfo)))
        {
            gtk_entry_set_text (GTK_ENTRY (pInfo->m_serialNum), (char*)cameraInfo.SerialNumber);
            gtk_entry_set_text (GTK_ENTRY (pInfo->m_vendorName), (char*)cameraInfo.VendorName);
            gtk_entry_set_text (GTK_ENTRY (pInfo->m_productName), (char*)cameraInfo.ModelName);
            gtk_entry_set_text (GTK_ENTRY (pInfo->m_cameraName), (char*)cameraInfo.CameraName);

            gtk_entry_set_text (GTK_ENTRY (pInfo->m_versionFirmware), (char*)cameraInfo.FirmwareVersion);
            gtk_entry_set_text (GTK_ENTRY (pInfo->m_versionFpga), (char*)cameraInfo.FPGAVersion);
            gtk_entry_set_text (GTK_ENTRY (pInfo->m_versionXml), (char*)cameraInfo.XMLVersion);
	}
    }

    return false;  //  Only run once....
}

//
// Called periodically -- reads the current temperatures
PXL_RETURN_CODE GetCurrentTemperatures()
{
    PXL_RETURN_CODE rc = ApiSuccess;

    PxLAutoLock lock(&gCameraLock);
    if (gCamera && gInfoTab)
    {
        float temp = 0.0;

        if (gInfoTab->m_hasSensorTemperature)
        {
            rc = gCamera->getValue(FEATURE_SENSOR_TEMPERATURE, &temp);
            if (API_SUCCESS(rc)) gInfoTab->m_sensorTempLast = temp;
        }
        if (gInfoTab->m_hasBodyTemperature)
        {
            rc = gCamera->getValue(FEATURE_BODY_TEMPERATURE, &temp);
            if (API_SUCCESS(rc)) gInfoTab->m_bodyTempLast = temp;
        }
    }

    return rc;
}

//
// Called periodically -- updates the current temperature controls
void UpdateTemperatureControls()
{
    if (gCamera && gInfoTab)
    {
        char cActualValue[40];
        GdkRGBA red = {1.0, 0.0, 0.0, 0.3};
        GdkRGBA yellow = {1.0, 1.0, 0.0, 0.3};

        if (gInfoTab->m_hasSensorTemperature)
        {
            sprintf (cActualValue, "%5.2f", gInfoTab->m_sensorTempLast);
            if (gInfoTab->m_sensorTempLast > sensorHot)
                gtk_widget_override_background_color (gInfoTab->m_tempSensor, GTK_STATE_FLAG_NORMAL, &red);
            else if (gInfoTab->m_sensorTempLast > sensorWarm)
                gtk_widget_override_background_color (gInfoTab->m_tempSensor, GTK_STATE_FLAG_NORMAL, &yellow);
            else
                gtk_widget_override_background_color (gInfoTab->m_tempSensor, GTK_STATE_FLAG_NORMAL, NULL);
            gtk_entry_set_text (GTK_ENTRY (gInfoTab->m_tempSensor), cActualValue);
        }
        if (gInfoTab->m_hasBodyTemperature)
        {
            sprintf (cActualValue, "%5.2f", gInfoTab->m_bodyTempLast);
            if (gInfoTab->m_bodyTempLast > bodyHot)
                gtk_widget_override_background_color (gInfoTab->m_tempBody, GTK_STATE_FLAG_NORMAL, &red);
            else if (gInfoTab->m_bodyTempLast > bodyWarm)
                gtk_widget_override_background_color (gInfoTab->m_tempBody, GTK_STATE_FLAG_NORMAL, &yellow);
            else
                gtk_widget_override_background_color (gInfoTab->m_tempBody, GTK_STATE_FLAG_NORMAL, NULL);
            gtk_entry_set_text (GTK_ENTRY (gInfoTab->m_tempBody), cActualValue);
        }
    }
}

/* ---------------------------------------------------------------------------
 * --   Control functions from the Glade project
 * ---------------------------------------------------------------------------
 */

extern "C" void LoadSettingsButtonPressed
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{
    if (! gCamera || ! gInfoTab) return;
    if (gInfoTab->m_refreshRequired) return;

    bool useFactoryDefault = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(gInfoTab->m_loadFactoryDefaults));

    // Pop up a 'are you sure' message
    GtkWidget *popup = gtk_message_dialog_new (gTopLevelWindow,
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_MESSAGE_QUESTION,
                                         GTK_BUTTONS_OK_CANCEL,
                                         "Loading camera settings to %s values.  OK to proceed?",
                                         useFactoryDefault ? "factory default" : "power-up default");
    int userResult = gtk_dialog_run (GTK_DIALOG (popup));  // This makes the popup modal
    gtk_widget_destroy (popup);

    if (userResult == GTK_RESPONSE_OK)
    {
        PxLInterruptStream hicup(gCamera, STOP_STREAM);

        PXL_RETURN_CODE rc = gCamera->loadSettings(useFactoryDefault);

        if (API_SUCCESS(rc))
        {
            // EVERYTHINGs changed.  let the world know.
            gControlsTab->refreshRequired(false);
            gStreamTab->refreshRequired(false);
            gGpioTab->refreshRequired(false);
            gLensTab->refreshRequired(false);
            gAutoRoiTab->refreshRequired(false);
            gInfoTab->refreshRequired (false);
        }
    }

}

extern "C" void SaveSettingsButtonPressed
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{
    if (! gCamera || ! gInfoTab) return;
    if (gInfoTab->m_refreshRequired) return;

    // Pop up a 'are you sure' message
    GtkWidget *popup = gtk_message_dialog_new (gTopLevelWindow,
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_MESSAGE_QUESTION,
                                         GTK_BUTTONS_OK_CANCEL,
                                         "Saving camera settings to power-up defaults.  OK to proceed?");
    int userResult = gtk_dialog_run (GTK_DIALOG (popup));  // This makes the popup modal
    gtk_widget_destroy (popup);

    if (userResult == GTK_RESPONSE_OK)
    {
        PxLInterruptStream hicup(gCamera, STOP_STREAM);

        gCamera->saveSettings();
    }
}

