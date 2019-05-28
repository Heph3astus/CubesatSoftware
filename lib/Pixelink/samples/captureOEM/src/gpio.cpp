/***************************************************************************
 *
 *     File: gpio.cpp
 *
 *     Description:
 *        Controls for the 'GPIO' tab  in CaptureOEM.
 */

#include <glib.h>
#include <vector>
#include <algorithm>
#include "gpio.h"
#include "camera.h"
#include "captureOEM.h"
#include "cameraSelect.h"
#include "stream.h"
#include "helpers.h"

using namespace std;

extern PxLGpio         *gGpioTab;
extern PxLCameraSelect *gCameraSelectTab;
extern PxLStream       *gStreamTab;
extern GtkWindow       *gTopLevelWindow;

// TRIGGER_TYPE_FREE_RUNNING really means there is no trigger
#define TRIGGER_TYPE_NONE TRIGGER_TYPE_FREE_RUNNING

//
// Local prototypes.
//    UI updates can only be done from a gtk thread -- these routines are gtk 'idle' threads
//    and as such are 'UI update safe'. For each 'feature', there there the following functions:
//       . {featureXXX}Deactivate - Makes the controls meaningless (including greying them out)
//       . {featreuXXX}Activate - updates the controls with values from the camera
static gboolean  RefreshComplete (gpointer pData);
static gboolean  TriggerDeactivate (gpointer pData);
static gboolean  TriggerActivate (gpointer pData);
static gboolean  GpioDeactivate (gpointer pData);
static gboolean  GpioActivate (gpointer pData);
static void      UpdateTriggerInfo (PxLTriggerInfo& info, vector<int>& supportedHwTriggerModes);
static void      UpdateGpioInfo (PxLGpioInfo& info);

PXL_RETURN_CODE  GetCurrentGpio();
void             UpdateGpiStatus();
const PxLFeaturePollFunctions gpInputPoll (GetCurrentGpio, UpdateGpiStatus);

static const char* const PxLTriggerModeDescriptions[] = {
  "Mode 0\n\n"
  "Start integration at external trigger's\n"
  "leading edge.  Integration time is\n"
  "defined by FEATURE_SHUTTER.",

  "Mode 1\n\n"
  "Start integration at external trigger's\n"
  "leading edge and ends at the trigger's\n"
  "trailing edge.",

  "Mode 14\n\n"
  "The camera will capture Number frames\n"
  "after a trigger at the current\n"
  "integration time and frame rate.  If\n"
  "Number is set to 0 (if supported by\n"
  "the camera), the stream will continue\n"
  "until stopped by the user. "
};

// Indexed by GPIO_MODE_XXXX from PixeLINKTypes.h
static const char * const PxLGpioModeStrings[] =
{
   "Strobe",
   "Normal",
   "Pulse",
   "Busy",
   "Flash",
   "Input"
};

// Indexed by GPIO_MODE_XXXX from PixeLINKTypes.h
static const char* const PxLGpioModeDescriptions[] = {
   "Mode Strobe\n\n"
   "The GPO is set after a trigger occurs.\n"
   "The GPO pulse occurs Delay milliseconds\n"
   "from the trigger and is Duration\n"
   "milliseconds in length.",

   "Mode Normal\n\n"
   "The GPO is set to either low or high,\n"
   "depending on the value of Polarity.",

   "Mode Pulse\n\n"
   "The GPO is pulsed whenever it is turned\n"
   "on. The GPO outputs Number of pulses\n "
   "pulses of Duration milliseconds in\n"
   "length, separated by Interval\n"
   "milliseconds.",

   "Mode Busy\n\n"
   "The GPO is set whenever the camera is\n"
   "unable to respond to a trigger. ",

   "Mode Flash\n\n"
   "The GPO signal is set once the sensor\n"
   "has been reset and starts integrating,\n"
   "and will be deactivated at the end of\n"
   "the exposure time as readout of the\n "
   "array commences.",

   "Mode Input\n\n"
   "Function as a General Purpose Input.\n"
   "The value of the input line is returned\n"
   "as Status.  Note that only GPIO #1 can\n"
   "be configured as a GPI"
};

/* ---------------------------------------------------------------------------
 * --   Member functions - Public
 * ---------------------------------------------------------------------------
 */
PxLGpio::PxLGpio (GtkBuilder *builder)
: m_gpiLast(false)
{
    //
    // Step 1
    //      Find all of the glade controls

    m_triggerType = GTK_WIDGET( gtk_builder_get_object( builder, "TriggerType_Combo" ) );

    m_swTriggerButton = GTK_WIDGET( gtk_builder_get_object( builder, "SwTrigger_Button" ) );

    m_hwTriggerMode = GTK_WIDGET( gtk_builder_get_object( builder, "HwTriggerMode_Combo" ) );
    m_hwTriggePolarity = GTK_WIDGET( gtk_builder_get_object( builder, "HwTriggerPolarity_Combo" ) );
    m_hwTriggerDelay = GTK_WIDGET( gtk_builder_get_object( builder, "HwTriggerDelay_Text" ) );
    m_hwTriggerParam1Type = GTK_WIDGET( gtk_builder_get_object( builder, "TriggerParam1Type_Label" ) );
    m_hwTriggerNumber = GTK_WIDGET( gtk_builder_get_object( builder, "HwTriggerNumber_Text" ) );
    m_hwTriggerUpdate = GTK_WIDGET( gtk_builder_get_object( builder, "HwTriggerUpdate_Button" ) );
    m_hwTriggerDescription = GTK_WIDGET( gtk_builder_get_object( builder, "HardwareTriggerDesc_Label" ) );

    m_gpioNumber = GTK_WIDGET( gtk_builder_get_object( builder, "GpioNumber_Combo" ) );
    m_gpioEnable = GTK_WIDGET( gtk_builder_get_object( builder, "GpioEnable_Checkbox" ) );
    m_gpioMode = GTK_WIDGET( gtk_builder_get_object( builder, "GpioMode_Combo" ) );
    m_gpioPolarity = GTK_WIDGET( gtk_builder_get_object( builder, "GpioPolarity_Combo" ) );
    m_gpioParam1Type = GTK_WIDGET( gtk_builder_get_object( builder, "GpioParam1Type_Label" ) );
    m_gpioParam1Value = GTK_WIDGET( gtk_builder_get_object( builder, "GpioParam1Value_Text" ) );
    m_gpioParam1Units = GTK_WIDGET( gtk_builder_get_object( builder, "GpioParam1Units_Label" ) );
    m_gpioParam2Type = GTK_WIDGET( gtk_builder_get_object( builder, "GpioParam2Type_Label" ) );
    m_gpioParam2Value = GTK_WIDGET( gtk_builder_get_object( builder, "GpioParam2Value_Text" ) );
    m_gpioParam2Units = GTK_WIDGET( gtk_builder_get_object( builder, "GpioParam2Units_Label" ) );
    m_gpioParam3Type = GTK_WIDGET( gtk_builder_get_object( builder, "GpioParam3Type_Label" ) );
    m_gpioParam3Value = GTK_WIDGET( gtk_builder_get_object( builder, "GpioParam3Value_Text" ) );
    m_gpioParam3Units = GTK_WIDGET( gtk_builder_get_object( builder, "GpioParam3Units_Label" ) );
    m_gpioUpdate = GTK_WIDGET( gtk_builder_get_object( builder, "GpioUpdate_Button" ) );
    m_gpioDescription = GTK_WIDGET( gtk_builder_get_object( builder, "GpioDesc_Label" ) );

}


PxLGpio::~PxLGpio ()
{
}

void PxLGpio::refreshRequired (bool noCamera)
{
    m_refreshRequired = true;

    if (IsActiveTab (GpioTab))
    {
        if (noCamera)
        {
            // If I am the active tab, then grey out everything
            gdk_threads_add_idle ((GSourceFunc)TriggerDeactivate, this);
            gdk_threads_add_idle ((GSourceFunc)GpioDeactivate, this);
        } else {
            // If I am the active tab, then refresh everything
            gdk_threads_add_idle ((GSourceFunc)TriggerActivate, this);
            gdk_threads_add_idle ((GSourceFunc)GpioActivate, this);
        }

        gdk_threads_add_idle ((GSourceFunc)RefreshComplete, this);
    }
}

void PxLGpio::activate()
{
    // I have become the active tab.

    if (gCamera)
    {
        if (m_refreshRequired)
        {
            gdk_threads_add_idle ((GSourceFunc)TriggerActivate, this);
            gdk_threads_add_idle ((GSourceFunc)GpioActivate, this);
        } else {
            // If GP Input is enabled, start it's poller
            int modeIndex = gtk_combo_box_get_active (GTK_COMBO_BOX(gGpioTab->m_gpioMode));
            bool gpInputEnabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(m_gpioEnable)) &&
                                  m_supportedGpioModes[modeIndex] == GPIO_MODE_INPUT;
            if (gpInputEnabled)
            {
                gCamera->m_poller->pollAdd(gpInputPoll);
            }
        }
    } else {
        gdk_threads_add_idle ((GSourceFunc)TriggerDeactivate, this);
        gdk_threads_add_idle ((GSourceFunc)GpioDeactivate, this);
    }
    gdk_threads_add_idle ((GSourceFunc)RefreshComplete, this);
}

void PxLGpio::deactivate()
{
    // I am no longer the active tab.

    // remove the poller (it's OK if it's not there)
    if (gCamera)
    {
        gCamera->m_poller->pollRemove(gpInputPoll);
    }
}


/* ---------------------------------------------------------------------------
 * --   gtk thread callbacks - used to update controls
 * ---------------------------------------------------------------------------
 */

// Indicate that the refresh is no longer outstanding, it has completed.
static gboolean RefreshComplete (gpointer pData)
{
    PxLGpio *pControls = (PxLGpio *)pData;

    pControls->m_refreshRequired = false;
    return false;
}

//
// Make trigger controls unselectable
static gboolean TriggerDeactivate (gpointer pData)
{
    PxLGpio *pControls = (PxLGpio *)pData;

    gtk_widget_set_sensitive (pControls->m_triggerType, false);

    gtk_widget_set_sensitive (pControls->m_swTriggerButton, false);

    gtk_widget_set_sensitive (pControls->m_hwTriggerMode, false);
    gtk_widget_set_sensitive (pControls->m_hwTriggePolarity, false);
    gtk_widget_set_sensitive (pControls->m_hwTriggerDelay, false);
    gtk_widget_set_sensitive (pControls->m_hwTriggerParam1Type, false);
    gtk_widget_set_sensitive (pControls->m_hwTriggerNumber, false);
    gtk_widget_set_sensitive (pControls->m_hwTriggerUpdate, false);
    gtk_label_set_text (GTK_LABEL (pControls->m_hwTriggerDescription), "");

    pControls->m_supportedHwTriggerModes.clear();

    return false;  //  Only run once....
}

//
// Make image controls selectable (if appropriate)
static gboolean TriggerActivate (gpointer pData)
{
    PxLGpio *pControls = (PxLGpio *)pData;

    bool supportsSwTrigger = false;
    vector<int>supportedHwTriggerModes;

    if (gCamera)
    {
        PXL_RETURN_CODE rc = ApiSuccess;
        PxLTriggerInfo origTrig;

        //
        // Step 0
        //      Clean up old info
        gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT(pControls->m_triggerType));
        gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT(pControls->m_hwTriggerMode));
        gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT(pControls->m_hwTriggePolarity));

        //
        // Step 1
        //      figure out what trigger modes are supported
        if (gCamera->supported(FEATURE_TRIGGER))
        {
            //
            // Step 1a
            //      If a camera supports triggering at all, then it supports software triggering
            supportsSwTrigger = true;

            rc = gCamera->getTriggerValue (origTrig);
            if (API_SUCCESS (rc))
            {
                float minMode = 0.0, maxMode = 0.0;
                float minType = 0.0, maxType = 0.0;
                rc = gCamera->getTriggerRange (&minMode, &maxMode, &minType, &maxType);
                if (API_SUCCESS(rc))
                {
                    if (pControls->InRange (TRIGGER_TYPE_HARDWARE, (int)minType, (int)maxType))
                    {
                        //
                        // Step 1b.
                        //      If TRIGGER_TYPE_HARDWARE in in the camera's trigger range, then it supports
                        //      hardware triggering
                        if (minMode == 0)
                        {
                            supportedHwTriggerModes.push_back(0);
                            //
                            //  Step 1c
                            //      This camera clearly supports hardware mode 0.  But does it also support
                            //      mode 1.  If mode 1 is in it's range, the only way to now for sure is to
                            //      try it.
                            if (pControls->InRange (1, (int)minMode, (int)maxMode))
                            {
                                TEMP_STREAM_STOP();

                                PxLTriggerInfo newTrig = origTrig;
                                newTrig.m_enabled = true;
                                newTrig.m_type = TRIGGER_TYPE_HARDWARE;
                                newTrig.m_mode = 1;
                                rc = gCamera->setTriggerValue (newTrig);

                                if (API_SUCCESS(rc)) supportedHwTriggerModes.push_back(1);
                                gCamera->setTriggerValue (origTrig);
                            }
                        } else if (minMode == 1) {
                            supportedHwTriggerModes.push_back(1);
                        }
                        //
                        // Step 1d
                        //      The only mode left to check it mode 14.  we will simplify this to the range check.
                        //      In other words, if it supports any mode greater than 14 (and a mode of 14 or less),
                        //      then it must also support mode 14.
                        if (pControls->InRange (14, (int)minMode, (int)maxMode)) supportedHwTriggerModes.push_back(14);
                    }
                }
            }
        }

        //
        // Step 2
        //      Set our supported trigger types
        gtk_combo_box_text_insert_text (GTK_COMBO_BOX_TEXT(pControls->m_triggerType),
                                        TRIGGER_TYPE_NONE,
                                        "None");
        if (supportsSwTrigger)
        {
            gtk_combo_box_text_insert_text (GTK_COMBO_BOX_TEXT(pControls->m_triggerType),
                                            TRIGGER_TYPE_SOFTWARE,
                                            "Software");
        }
        if (!supportedHwTriggerModes.empty())
        {
            gtk_combo_box_text_insert_text (GTK_COMBO_BOX_TEXT(pControls->m_triggerType),
                                            TRIGGER_TYPE_HARDWARE,
                                            "Hardware");
        }

        //
        // Step 3
        //      Set our hardware parameters (if supported, and relevant)
        bool supportsMode0 = find (supportedHwTriggerModes.begin(), supportedHwTriggerModes.end(), 0) != supportedHwTriggerModes.end();
        bool supportsMode1 = find (supportedHwTriggerModes.begin(), supportedHwTriggerModes.end(), 1) != supportedHwTriggerModes.end();
        bool supportsMode14 = find (supportedHwTriggerModes.begin(), supportedHwTriggerModes.end(), 14) != supportedHwTriggerModes.end();
        if (supportsMode0) gtk_combo_box_text_insert_text (GTK_COMBO_BOX_TEXT(pControls->m_hwTriggerMode), PxLGpio::MODE_0, "0");
        if (supportsMode1) gtk_combo_box_text_insert_text (GTK_COMBO_BOX_TEXT(pControls->m_hwTriggerMode), PxLGpio::MODE_1, "1");
        if (supportsMode14) gtk_combo_box_text_insert_text (GTK_COMBO_BOX_TEXT(pControls->m_hwTriggerMode),PxLGpio::MODE_14, "14");
        if (! supportedHwTriggerModes.empty())
        {
            gtk_combo_box_text_insert_text (GTK_COMBO_BOX_TEXT(pControls->m_hwTriggePolarity), POLARITY_NEGATIVE, "Negative");
            gtk_combo_box_text_insert_text (GTK_COMBO_BOX_TEXT(pControls->m_hwTriggePolarity), POLARITY_POSITIVE, "Positive");
        }

        // update the fields to the current trigger setting (or defaults).
        gtk_combo_box_set_active (GTK_COMBO_BOX(gGpioTab->m_triggerType), (int)origTrig.m_type);
        UpdateTriggerInfo (origTrig, supportedHwTriggerModes);

    }

    //
    // Step 4
    //      Remember the supported modes for this camera.
    pControls->m_supportedHwTriggerModes = supportedHwTriggerModes;

    return false;  //  Only run once....
}

//
// Make trigger controls unselectable
static gboolean GpioDeactivate (gpointer pData)
{
    PxLGpio *pControls = (PxLGpio *)pData;

    gtk_widget_set_sensitive (pControls->m_gpioNumber, false);
    gtk_widget_set_sensitive (pControls->m_gpioEnable, false);

    gtk_widget_set_sensitive (pControls->m_gpioMode, false);
    gtk_widget_set_sensitive (pControls->m_gpioPolarity, false);
    gtk_label_set_text (GTK_LABEL (pControls->m_gpioParam1Type), "");
    gtk_widget_set_sensitive (pControls->m_gpioParam1Value, false);
    gtk_label_set_text (GTK_LABEL (pControls->m_gpioParam1Units), "");
    gtk_label_set_text (GTK_LABEL (pControls->m_gpioParam2Type), "");
    gtk_widget_set_sensitive (pControls->m_gpioParam2Value, false);
    gtk_label_set_text (GTK_LABEL (pControls->m_gpioParam2Units), "");
    gtk_label_set_text (GTK_LABEL (pControls->m_gpioParam3Type), "");
    gtk_widget_set_sensitive (pControls->m_gpioParam3Value, false);
    gtk_label_set_text (GTK_LABEL (pControls->m_gpioParam3Units), "");
    gtk_widget_set_sensitive (pControls->m_gpioUpdate, false);
    gtk_label_set_text (GTK_LABEL (pControls->m_gpioDescription), "");

    // Remove the GPI poller (it's OK if there isn't one
    if (gCamera) gCamera->m_poller->pollRemove(gpInputPoll);

    return false;  //  Only run once....
}

//
// Make GPIO controls selectable (if appropriate)
static gboolean GpioActivate (gpointer pData)
{
    PxLGpio *pControls = (PxLGpio *)pData;

    int numGpiosSupported = 0;
    vector<int> supportedModes;

    if (gCamera)
    {
        //
        // Step 0
        //      Clean up old info
        gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT(pControls->m_gpioNumber));
        gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT(pControls->m_gpioMode));
        gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT(pControls->m_gpioPolarity));

        PXL_RETURN_CODE rc = ApiSuccess;
        PxLGpioInfo origGpio;
        PxLGpioInfo defaultGpio;

        if (gCamera->supported(FEATURE_GPIO))
        {
            //
            // Step 1
            //      figure out what gpio modes are supported
            bool restoreRequired = false;
            rc = gCamera->getGpioValue (0, origGpio);
            if (API_SUCCESS (rc))
            {
                float minMode = 0.0, maxMode = 0.0;
                rc = gCamera->getGpioRange (&numGpiosSupported, &minMode, &maxMode);
                if (API_SUCCESS(rc) && numGpiosSupported > 0)
                {
                    //
                    // Step 1a
                    //      We know the camera supports the minMode
                    //  Not so fast... Bugzilla.1277 says that we can't trust this information to be
                    //  true for some cameras.  So, rather than rely on this (potentially incorrect) information,
                    //  we not assume the min is supported
                    //supportedModes.push_back((int)minMode);

                    // Step 1b
                    //      For all of the modes between minMode and maxMode, we simply have to try to
                    //      set them to see if it works
                    // Bugzilla.1277 -- test the min value too
                    //for (float trialMode = minMode+1; trialMode < maxMode; trialMode++)
                    for (float trialMode = minMode; trialMode < maxMode; trialMode++)
                    {
                        if (trialMode == GPIO_MODE_NORMAL)
                        {
                            // This mode is always supported, not need to try
                            supportedModes.push_back((int)trialMode);
                            continue;
                        }
                        restoreRequired = true;
                        PxLGpioInfo newGpio = origGpio;
                        newGpio.m_enabled = true;
                        newGpio.m_mode = trialMode;
                        rc = gCamera->setGpioValue (0, newGpio);
                        if (API_SUCCESS (rc)) supportedModes.push_back((int)trialMode);
                    }

                    //
                    // Step 1c
                    //      We know the camera supports the maxMode
                    if (find (supportedModes.begin(), supportedModes.end(), (int)maxMode) == supportedModes.end())
                    {
                        supportedModes.push_back((int)maxMode);
                    }

                    //
                    // Step 1d
                    //      If we changed it, restore the original gpio value
                    if (restoreRequired) gCamera->setGpioValue(0, origGpio);
                }

                //
                // Step 2
                //      Set GPIO nums.  If we support at least one, pick the first one as our 'active' one.
                char cActualValue[40];
                for (int i = 0; i<numGpiosSupported; i++)
                {
                    sprintf (cActualValue, "%d", i+1);
                    gtk_combo_box_text_insert_text (GTK_COMBO_BOX_TEXT(pControls->m_gpioNumber),
                                                    i,
                                                    cActualValue);
                }
                gtk_combo_box_set_active (GTK_COMBO_BOX(pControls->m_gpioNumber), 0);
                gtk_widget_set_sensitive (pControls->m_gpioNumber, true);

                //
                // Step 3
                //      Populate the supported modes dropdown
                for (vector<int>::iterator it = supportedModes.begin(); it != supportedModes.end(); ++it)
                {
                    gtk_combo_box_text_insert_text (GTK_COMBO_BOX_TEXT(pControls->m_gpioMode),
                                                    *it,
                                                    PxLGpioModeStrings[*it]);
                }

                //
                // Step 4
                //      Populate the polarity dropdown
                gtk_combo_box_text_insert_text (GTK_COMBO_BOX_TEXT(pControls->m_gpioPolarity), POLARITY_NEGATIVE, "Negative");
                gtk_combo_box_text_insert_text (GTK_COMBO_BOX_TEXT(pControls->m_gpioPolarity), POLARITY_POSITIVE, "Positive");

                //
                // Step 5
                //      Update the information on the GPO (number 0).
                pControls->m_supportedGpioModes = supportedModes;
                UpdateGpioInfo (origGpio.m_enabled ? origGpio : defaultGpio);

                //
                // Step 6
                //      Updates are only necessary after a user change.
                gtk_widget_set_sensitive (pControls->m_gpioUpdate, false);

                //
                // Step 7
                //      If GP Input is enabled, start it's poller
                if (origGpio.m_enabled && origGpio.m_mode == GPIO_MODE_INPUT)
                {
                    gCamera->m_poller->pollAdd(gpInputPoll);
                }
            }
        }

    }

    //
    // Step 8
    //      Remember the supported modes for this camera.
    pControls->m_supportedGpioModes = supportedModes;

    return false;  //  Only run once....
}

//
// Called periodically when general purpose inputs -- reads the current value
PXL_RETURN_CODE GetCurrentGpio()
{
    PXL_RETURN_CODE rc = ApiSuccess;

    PxLAutoLock lock(&gCameraLock);
    if (gCamera && gGpioTab)
    {
        // It's safe to assume the camera supports GPIO, as this function will not be called
        // otherwise.  If we were to check via pCamera->supported (FEATURE_GPIO) or
        // pCamera->continuousSupported (FEATURE_GPIO), then that will perform a PxLGetCameraFeatures,
        // which is a lot of work for not.
        PxLGpioInfo currentGpio;
        int requestedGpioNum = gtk_combo_box_get_active (GTK_COMBO_BOX(gGpioTab->m_gpioNumber));  // This is '0' based
        rc = gCamera->getGpioValue(requestedGpioNum, currentGpio);
        if (API_SUCCESS(rc)) gGpioTab->m_gpiLast = currentGpio.m_param1 == 1.0f;
    }

    return rc;
}

//
// Called periodically when doing continuous exposure updates -- updates the user controls
void UpdateGpiStatus()
{
    if (gCamera && gGpioTab)
    {
        PxLAutoLock lock(&gCameraLock);

        gtk_entry_set_text (GTK_ENTRY (gGpioTab->m_gpioParam1Value),
                                       gGpioTab->m_gpiLast ? "Signaled" : "Not signaled");
    }
}

static void UpdateTriggerInfo (PxLTriggerInfo& info, vector<int>& supportedHwTriggerModes)
{
    gtk_combo_box_set_active (GTK_COMBO_BOX(gGpioTab->m_triggerType), info.m_enabled ? (int)info.m_type : TRIGGER_TYPE_NONE);
    gtk_widget_set_sensitive (gGpioTab->m_triggerType, true);

    gtk_widget_set_sensitive (gGpioTab->m_swTriggerButton, info.m_enabled && info.m_type == TRIGGER_TYPE_SOFTWARE);

    if (!supportedHwTriggerModes.empty())
    {
        gtk_combo_box_set_active (GTK_COMBO_BOX(gGpioTab->m_hwTriggerMode), gGpioTab->ModeToIndex(info.m_mode));
        gtk_widget_set_sensitive (gGpioTab->m_hwTriggerMode, true);

        gtk_combo_box_set_active (GTK_COMBO_BOX(gGpioTab->m_hwTriggePolarity), (int)info.m_polarity);
        gtk_widget_set_sensitive (gGpioTab->m_hwTriggePolarity, true);

        char cActualValue[40];
        sprintf (cActualValue, "%8.1f", info.m_delay * 1000.0);
        gtk_entry_set_text (GTK_ENTRY (gGpioTab->m_hwTriggerDelay), cActualValue);
        gtk_widget_set_sensitive (gGpioTab->m_hwTriggerDelay, true);

        bool supportsMode14 = find (supportedHwTriggerModes.begin(), supportedHwTriggerModes.end(), 14) != supportedHwTriggerModes.end();
        if (supportsMode14 && info.m_mode == 14)
        {
            gtk_label_set_text (GTK_LABEL (gGpioTab->m_hwTriggerParam1Type), "Number: ");
            sprintf (cActualValue, "%d", (int)info.m_number);
            gtk_entry_set_text (GTK_ENTRY (gGpioTab->m_hwTriggerNumber), cActualValue);
            gtk_widget_set_sensitive (gGpioTab->m_hwTriggerNumber, true);
        } else {
            gtk_label_set_text (GTK_LABEL (gGpioTab->m_hwTriggerParam1Type), "");
            gtk_widget_set_sensitive (gGpioTab->m_hwTriggerNumber, false);
        }

        int descriptionIndex = max ((int)info.m_mode, 0);
        if (descriptionIndex >= (int)(sizeof(PxLTriggerModeDescriptions) / sizeof (PxLTriggerModeDescriptions[0])))
        {
            descriptionIndex = (int)(sizeof(PxLTriggerModeDescriptions) / sizeof (PxLTriggerModeDescriptions[0])) - 1;
        }
        gtk_label_set_text (GTK_LABEL (gGpioTab->m_hwTriggerDescription), PxLTriggerModeDescriptions[descriptionIndex]);
    }
}

static void UpdateGpioInfo (PxLGpioInfo& info)
{
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(gGpioTab->m_gpioEnable), info.m_enabled);
    gtk_widget_set_sensitive (gGpioTab->m_gpioEnable, true);

    int index = max ((int)gGpioTab->m_supportedGpioModes.size()-1, 0);
    for (; index > 0; index--) if (gGpioTab->m_supportedGpioModes[index] == (int)info.m_mode) break;
    gtk_combo_box_set_active (GTK_COMBO_BOX(gGpioTab->m_gpioMode), index);
    gtk_widget_set_sensitive (gGpioTab->m_gpioMode, true);

    gtk_combo_box_set_active (GTK_COMBO_BOX(gGpioTab->m_gpioPolarity), (int)info.m_polarity);
    gtk_widget_set_sensitive (gGpioTab->m_gpioPolarity, true);

    // Start with all of the optional parameters in a NULL state.  They will be
    // set properly below
    gtk_label_set_text (GTK_LABEL (gGpioTab->m_gpioParam1Type), "");
    gtk_entry_set_text (GTK_ENTRY (gGpioTab->m_gpioParam1Value), "");
    gtk_label_set_text (GTK_LABEL (gGpioTab->m_gpioParam1Units), "");
    gtk_widget_set_sensitive (gGpioTab->m_gpioParam1Value, false);

    gtk_label_set_text (GTK_LABEL (gGpioTab->m_gpioParam2Type), "");
    gtk_entry_set_text (GTK_ENTRY (gGpioTab->m_gpioParam2Value), "");
    gtk_label_set_text (GTK_LABEL (gGpioTab->m_gpioParam2Units), "");
    gtk_widget_set_sensitive (gGpioTab->m_gpioParam2Value, false);

    gtk_label_set_text (GTK_LABEL (gGpioTab->m_gpioParam3Type), "");
    gtk_entry_set_text (GTK_ENTRY (gGpioTab->m_gpioParam3Value), "");
    gtk_label_set_text (GTK_LABEL (gGpioTab->m_gpioParam3Units), "");
    gtk_widget_set_sensitive (gGpioTab->m_gpioParam3Value, false);

    char cActualValue[40];
    switch ((int)info.m_mode)
    {
    // BE sure to convert the times to milliseconds
    case GPIO_MODE_STROBE:
        gtk_label_set_text (GTK_LABEL (gGpioTab->m_gpioParam1Type), "Delay");
        sprintf (cActualValue, "%8.1f", info.m_param1 * 1000.0f);
        gtk_entry_set_text (GTK_ENTRY (gGpioTab->m_gpioParam1Value), cActualValue);
        gtk_label_set_text (GTK_LABEL (gGpioTab->m_gpioParam1Units), "milliseconds");
        gtk_widget_set_sensitive (gGpioTab->m_gpioParam1Value, true);

        gtk_label_set_text (GTK_LABEL (gGpioTab->m_gpioParam2Type), "Duration");
        sprintf (cActualValue, "%8.1f", info.m_param2 * 1000.0f);
        gtk_entry_set_text (GTK_ENTRY (gGpioTab->m_gpioParam2Value), cActualValue);
        gtk_label_set_text (GTK_LABEL (gGpioTab->m_gpioParam2Units), "milliseconds");
        gtk_widget_set_sensitive (gGpioTab->m_gpioParam2Value, true);

        break;
    case GPIO_MODE_PULSE:
        gtk_label_set_text (GTK_LABEL (gGpioTab->m_gpioParam1Type), "Number");
        sprintf (cActualValue, "%d", (int)info.m_param1);
        gtk_entry_set_text (GTK_ENTRY (gGpioTab->m_gpioParam1Value), cActualValue);
        gtk_label_set_text (GTK_LABEL (gGpioTab->m_gpioParam1Units), "");
        gtk_widget_set_sensitive (gGpioTab->m_gpioParam1Value, true);

        gtk_label_set_text (GTK_LABEL (gGpioTab->m_gpioParam2Type), "Duration");
        sprintf (cActualValue, "%8.1f", info.m_param2 * 1000.0f);
        gtk_entry_set_text (GTK_ENTRY (gGpioTab->m_gpioParam2Value), cActualValue);
        gtk_label_set_text (GTK_LABEL (gGpioTab->m_gpioParam2Units), "milliseconds");
        gtk_widget_set_sensitive (gGpioTab->m_gpioParam2Value, true);

        gtk_label_set_text (GTK_LABEL (gGpioTab->m_gpioParam3Type), "Interval");
        sprintf (cActualValue, "%8.1f", info.m_param3 * 1000.0f);
        gtk_entry_set_text (GTK_ENTRY (gGpioTab->m_gpioParam3Value), cActualValue);
        gtk_label_set_text (GTK_LABEL (gGpioTab->m_gpioParam3Units), "milliseconds");
        gtk_widget_set_sensitive (gGpioTab->m_gpioParam3Value, true);
        break;
    case GPIO_MODE_INPUT:
        gtk_label_set_text (GTK_LABEL (gGpioTab->m_gpioParam1Type), "Status");
        gtk_entry_set_text (GTK_ENTRY (gGpioTab->m_gpioParam1Value),
                                       info.m_param1 == 0 ? "Not signaled" : "Signaled");
        gtk_label_set_text (GTK_LABEL (gGpioTab->m_gpioParam1Units), "");
        gtk_widget_set_sensitive (gGpioTab->m_gpioParam1Value, false);

        break;
    default:
        break;
    }

    gtk_label_set_text (GTK_LABEL (gGpioTab->m_gpioDescription), PxLGpioModeDescriptions[(int)info.m_mode]);
}

/* ---------------------------------------------------------------------------
 * --   Control functions from the Glade project
 * ---------------------------------------------------------------------------
 */

extern "C" void NewTriggerSelected
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{
    if (! gCamera || ! gCamera) return;
    if (gCameraSelectTab->changingCameras()) return;
    if (gGpioTab->m_refreshRequired) return;

    //
    // Step 1
    //      Determine the type of trigger the user wants.
    int requestedTriggerType = gtk_combo_box_get_active (GTK_COMBO_BOX(gGpioTab->m_triggerType));
    PxLTriggerInfo requestedTrigger;

    int userModeIndex;
    requestedTrigger.m_enabled = true;
    requestedTrigger.m_type = requestedTriggerType;
    switch (requestedTriggerType)
    {
    case TRIGGER_TYPE_SOFTWARE:
        break;
    case TRIGGER_TYPE_HARDWARE:
        userModeIndex = gtk_combo_box_get_active (GTK_COMBO_BOX(gGpioTab->m_hwTriggerMode));
        requestedTrigger.m_mode = gGpioTab->m_supportedHwTriggerModes[userModeIndex];
        requestedTrigger.m_polarity = (float)gtk_combo_box_get_active (GTK_COMBO_BOX(gGpioTab->m_hwTriggePolarity));
        requestedTrigger.m_delay = atof (gtk_entry_get_text (GTK_ENTRY (gGpioTab->m_hwTriggerDelay))) / 1000.0f;
        requestedTrigger.m_number = atof (gtk_entry_get_text (GTK_ENTRY (gGpioTab->m_hwTriggerNumber)));
        break;
    case TRIGGER_TYPE_NONE:
    default:
        requestedTrigger.m_enabled = false;
        break;
    }

    //
    // Step 2
    //      Attempt to set the trigger
    PxLAutoLock lock(&gCameraLock);
    PXL_RETURN_CODE rc;
    {
        TEMP_STREAM_STOP();
        rc = gCamera->setTriggerValue(requestedTrigger);

        if (!API_SUCCESS (rc))
        {
            //
            // Step 3
            //      If the set didn't work, report the error and then refresh the trigger controls
            // Pop up an error message
            GtkWidget *popupError = gtk_message_dialog_new (gTopLevelWindow,
                                                 GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 GTK_MESSAGE_ERROR,
                                                 GTK_BUTTONS_CLOSE,
                                                 "Setting trigger returned error code - 0x%x", rc);
            gtk_dialog_run (GTK_DIALOG (popupError));  // This makes the popup modal
            gtk_widget_destroy (popupError);

            PxLTriggerInfo actualTrigger;
            rc = gCamera->getTriggerValue(actualTrigger);
            if (API_SUCCESS(rc)) UpdateTriggerInfo (actualTrigger, gGpioTab->m_supportedHwTriggerModes);
        } else {
            //
            // Step 4
            //      We have set the trigger mode.  Update the controls
            gtk_widget_set_sensitive (gGpioTab->m_swTriggerButton, requestedTriggerType == TRIGGER_TYPE_SOFTWARE);
        }
    }

    // Update other tabs the next time they are activated
    gStreamTab->refreshRequired(false);
}

extern "C" void SwTriggerButtonPressed
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{
    if (! gCamera || ! gCamera) return;
    if (gCameraSelectTab->changingCameras()) return;
    if (gGpioTab->m_refreshRequired) return;

    PxLAutoLock lock(&gCameraLock);

    // simply capture a throw away frame.
    std::vector<U8> frameBuf (gCamera->imageSizeInBytes());
    FRAME_DESC     frameDesc;

    gCamera->getNextFrame (frameBuf.size(), &frameBuf[0], &frameDesc);
}

extern "C" void NewTriggerModeSelected
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{
    if (! gCamera || ! gCamera) return;
    if (gCameraSelectTab->changingCameras()) return;
    if (gGpioTab->m_refreshRequired) return;

    bool hwTriggering = gtk_combo_box_get_active (GTK_COMBO_BOX(gGpioTab->m_triggerType)) == TRIGGER_TYPE_HARDWARE;
    PxLGpio::HW_TRIGGER_MODES triggerMode = (PxLGpio::HW_TRIGGER_MODES)gtk_combo_box_get_active (GTK_COMBO_BOX(gGpioTab->m_hwTriggerMode));
    gtk_label_set_text (GTK_LABEL (gGpioTab->m_hwTriggerDescription), PxLTriggerModeDescriptions[triggerMode]);

    if (triggerMode == PxLGpio::MODE_14)
    {
        gtk_label_set_text (GTK_LABEL (gGpioTab->m_hwTriggerParam1Type), "Number: ");
        char cActualValue[40];
        sprintf (cActualValue, "%d", 1);  // default to just one frame
        gtk_entry_set_text (GTK_ENTRY (gGpioTab->m_hwTriggerNumber), cActualValue);
        gtk_widget_set_sensitive (gGpioTab->m_hwTriggerNumber, true);
    } else {
        gtk_label_set_text (GTK_LABEL (gGpioTab->m_hwTriggerParam1Type), "");
        gtk_widget_set_sensitive (gGpioTab->m_hwTriggerNumber, false);
    }

    gtk_widget_set_sensitive (gGpioTab->m_hwTriggerUpdate, hwTriggering);
}

extern "C" void TriggerParamChanged
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{
    if (! gCamera || ! gCamera) return;
    if (gCameraSelectTab->changingCameras()) return;
    if (gGpioTab->m_refreshRequired) return;

    bool hwTriggering = gtk_combo_box_get_active (GTK_COMBO_BOX(gGpioTab->m_triggerType)) == TRIGGER_TYPE_HARDWARE;

    gtk_widget_set_sensitive (gGpioTab->m_hwTriggerUpdate, hwTriggering);
}

extern "C" void TriggerUpdateButtonPressed
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{
    if (! gCamera || ! gCamera) return;
    if (gCameraSelectTab->changingCameras()) return;
    if (gGpioTab->m_refreshRequired) return;

    PxLTriggerInfo requestedTrigger;
    PxLAutoLock lock(&gCameraLock);
    PXL_RETURN_CODE rc;
    {
        requestedTrigger.m_enabled = true;
        requestedTrigger.m_type = TRIGGER_TYPE_HARDWARE;
        int userModeIndex = gtk_combo_box_get_active (GTK_COMBO_BOX(gGpioTab->m_hwTriggerMode));
        requestedTrigger.m_mode = gGpioTab->m_supportedHwTriggerModes[userModeIndex];
        requestedTrigger.m_polarity = gtk_combo_box_get_active (GTK_COMBO_BOX(gGpioTab->m_hwTriggePolarity));
        requestedTrigger.m_delay = atof (gtk_entry_get_text (GTK_ENTRY (gGpioTab->m_hwTriggerDelay))) / 1000.0f;
        requestedTrigger.m_number = atof (gtk_entry_get_text (GTK_ENTRY (gGpioTab->m_hwTriggerNumber)));
        TEMP_STREAM_STOP();
        rc = gCamera->setTriggerValue(requestedTrigger);

        if (!API_SUCCESS (rc))
        {
            GtkWidget *popupError = gtk_message_dialog_new (gTopLevelWindow,
                                                 GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 GTK_MESSAGE_ERROR,
                                                 GTK_BUTTONS_CLOSE,
                                                 "Setting trigger returned error code - 0x%x", rc);
            gtk_dialog_run (GTK_DIALOG (popupError));  // This makes the popup modal
            gtk_widget_destroy (popupError);

            PxLTriggerInfo actualTrigger;
            rc = gCamera->getTriggerValue(actualTrigger);
            if (API_SUCCESS(rc)) UpdateTriggerInfo (actualTrigger, gGpioTab->m_supportedHwTriggerModes);
        }
    }

    gtk_widget_set_sensitive (gGpioTab->m_hwTriggerUpdate, false);

    // Update other tabs the next time they are activated
    gStreamTab->refreshRequired(false);
}

extern "C" void NewGpioNumSelected
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{
    if (! gCamera || ! gCamera) return;
    if (gCameraSelectTab->changingCameras()) return;
    if (gGpioTab->m_refreshRequired) return;

    int requestedGpioNum = gtk_combo_box_get_active (GTK_COMBO_BOX(gGpioTab->m_gpioNumber));  // This is '0' based
    PxLGpioInfo requestedGpio;

    PxLAutoLock lock(&gCameraLock);
    PXL_RETURN_CODE rc;
    {
        rc = gCamera->getGpioValue(requestedGpioNum, requestedGpio);

        if (API_SUCCESS (rc))
        {
            //If the new GPO we just selected is disabled, then we don't actually know the mode (it's impossible
            // to tell.  So, in these circumstances, use the default mode of GPIO_MODE_NORMAL
            if (! requestedGpio.m_enabled) requestedGpio.m_mode = GPIO_MODE_NORMAL;
            UpdateGpioInfo (requestedGpio);
        }
    }
}

extern "C" void GpioEnableToggled
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{
    if (! gCamera || ! gCamera) return;
    if (gCameraSelectTab->changingCameras()) return;
    if (gGpioTab->m_refreshRequired) return;

    bool gpioEnable = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(gGpioTab->m_gpioEnable));
    int requestedGpioNum = gtk_combo_box_get_active (GTK_COMBO_BOX(gGpioTab->m_gpioNumber));  // This is '0' based

    PxLGpioInfo requestedGpio;
    requestedGpio.m_enabled = gpioEnable;
    int modeIndex = gtk_combo_box_get_active (GTK_COMBO_BOX(gGpioTab->m_gpioMode));
    requestedGpio.m_mode = (float)gGpioTab->m_supportedGpioModes[modeIndex];
    requestedGpio.m_polarity = gtk_combo_box_get_active (GTK_COMBO_BOX(gGpioTab->m_gpioPolarity));
    // Set the optional parameters.  The lack of breaks is intentional, as they are sequenced accordingly
    switch ((int)requestedGpio.m_mode)
    {
    // Be sure to convert the time quantities from milliseconds to seconds
    case GPIO_MODE_PULSE:
        requestedGpio.m_param3 = atof (gtk_entry_get_text (GTK_ENTRY (gGpioTab->m_gpioParam3Value))) / 1000.0f;
    case GPIO_MODE_STROBE:
        requestedGpio.m_param2 = atof (gtk_entry_get_text (GTK_ENTRY (gGpioTab->m_gpioParam2Value))) / 1000.0f;
        requestedGpio.m_param1 = atof (gtk_entry_get_text (GTK_ENTRY (gGpioTab->m_gpioParam1Value)));
        if ((int)requestedGpio.m_mode == GPIO_MODE_STROBE) requestedGpio.m_param1 /= 1000.0f;
    default:
        break;
    }

    PxLAutoLock lock(&gCameraLock);
    PXL_RETURN_CODE rc;
    {
        TEMP_STREAM_STOP();

        rc = gCamera->setGpioValue(requestedGpioNum, requestedGpio);

        if (!API_SUCCESS (rc))
        {
            GtkWidget *popupError = gtk_message_dialog_new (gTopLevelWindow,
                                                 GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 GTK_MESSAGE_ERROR,
                                                 GTK_BUTTONS_CLOSE,
                                                 "%s GPIO returned error code - 0x%x",
                                                     gpioEnable ? "Enabling" : "Disabling",
                                                     rc);
            gtk_dialog_run (GTK_DIALOG (popupError));  // This makes the popup modal
            gtk_widget_destroy (popupError);

            PxLGpioInfo actualGpio;
            rc = gCamera->getGpioValue(requestedGpioNum, actualGpio);
            if (API_SUCCESS(rc)) UpdateGpioInfo (actualGpio);
        } else {
            // If GP Input is enabled, start it's poller
            if (requestedGpio.m_enabled && requestedGpio.m_mode == GPIO_MODE_INPUT)
            {
                gCamera->m_poller->pollAdd(gpInputPoll);
            } else {
                // This will safely do nothing if there is no poller
                gCamera->m_poller->pollRemove(gpInputPoll);
            }
        }
    }
}

extern "C" void NewGpioModeSelected
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{
    if (! gCamera || ! gCamera) return;
    if (gCameraSelectTab->changingCameras()) return;
    if (gGpioTab->m_refreshRequired) return;

    // the user has changed the 'mode' of the GPIO, reset all of the 'parameters back
    // to their default state.
    PxLGpioInfo defaultInfo;
    int modeIndex = gtk_combo_box_get_active (GTK_COMBO_BOX(gGpioTab->m_gpioMode));
    defaultInfo.m_mode = (float)gGpioTab->m_supportedGpioModes[modeIndex];
    UpdateGpioInfo (defaultInfo);

    gtk_widget_set_sensitive (gGpioTab->m_gpioUpdate, true);
}

extern "C" void GpioParamChanged
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{
    if (! gCamera || ! gCamera) return;
    if (gCameraSelectTab->changingCameras()) return;
    if (gGpioTab->m_refreshRequired) return;

    gtk_widget_set_sensitive (gGpioTab->m_gpioUpdate, true);
}

extern "C" void GpioUpdateButtonPressed
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{
    if (! gCamera || ! gCamera) return;
    if (gCameraSelectTab->changingCameras()) return;
    if (gGpioTab->m_refreshRequired) return;

    // Disable the update button once it has been pressed
    gtk_widget_set_sensitive (gGpioTab->m_gpioUpdate, false);

    GpioEnableToggled  (widget, event, userdata);
}


