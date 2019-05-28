/***************************************************************************
 *
 *     File: link.cpp
 *
 *     Description:
 *        Controls for the 'Link' tab  in CaptureOEM.
 */

#include <glib.h>
#include "link.h"
#include "camera.h"
#include "captureOEM.h"
#include "cameraSelect.h"
#include "helpers.h"
#include "controls.h"

using namespace std;

extern PxLLink         *gLinkTab;
extern PxLCameraSelect *gCameraSelectTab;
extern PxLControls     *gControlsTab;

//
// Local prototypes.
//    UI updates can only be done from a gtk thread -- these routines are gtk 'idle' threads
//    and as such are 'UI update safe'. For each 'feature', there there the following functions:
//       . {featureXXX}Deactivate - Makes the controls meaningless (including greying them out)
//       . {featreuXXX}Activate - updates the controls with values from the camera
static gboolean  RefreshComplete (gpointer pData);
static gboolean  BwLimitDeactivate (gpointer pData);
static gboolean  BwLimitActivate (gpointer pData);

/* ---------------------------------------------------------------------------
 * --   Member functions - Public
 * ---------------------------------------------------------------------------
 */
PxLLink::PxLLink (GtkBuilder *builder)
{
    //
    // Step 1
    //      Find all of the glade controls

    m_bwLimitLabel = GTK_WIDGET( gtk_builder_get_object( builder, "BandwidthLimit_Label" ) );
    m_bwLimitEnable = GTK_WIDGET( gtk_builder_get_object( builder, "BandwidthLimitEnable_Checkbutton" ) );

    m_bwLimitSlider = new PxLSlider (
            GTK_WIDGET( gtk_builder_get_object( builder, "BandwidthLimitMin_Label" ) ),
            GTK_WIDGET( gtk_builder_get_object( builder, "BandwidthLimitMax_Label" ) ),
            GTK_WIDGET( gtk_builder_get_object( builder, "BandwidthLimit_Scale" ) ),
            GTK_WIDGET( gtk_builder_get_object( builder, "BandwidthLimit_Text" ) ));
}


PxLLink::~PxLLink ()
{
}

void PxLLink::refreshRequired (bool noCamera)
{
    m_refreshRequired = true;

    if (IsActiveTab (LinkTab))
    {
        if (noCamera)
        {
            // If I am the active tab, then grey out everything
            gdk_threads_add_idle ((GSourceFunc)BwLimitDeactivate, this);
        } else {
            // If I am the active tab, then refresh everything
            gdk_threads_add_idle ((GSourceFunc)BwLimitActivate, this);
        }

        gdk_threads_add_idle ((GSourceFunc)RefreshComplete, this);
    }
}

void PxLLink::activate()
{
    // I have become the active tab.

    if (gCamera)
    {
        if (m_refreshRequired)
        {
            gdk_threads_add_idle ((GSourceFunc)BwLimitActivate, this);
        }
    } else {
        gdk_threads_add_idle ((GSourceFunc)BwLimitDeactivate, this);
    }
    gdk_threads_add_idle ((GSourceFunc)RefreshComplete, this);
}

void PxLLink::deactivate()
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
    PxLLink *pLink = (PxLLink *)pData;

    pLink->m_refreshRequired = false;
    return false;
}

//
// Make Bandwidth Limit controls unselectable
static gboolean BwLimitDeactivate (gpointer pData)
{
    PxLLink *pLink = (PxLLink *)pData;

    gtk_widget_set_sensitive (pLink->m_bwLimitEnable, false);

    pLink->m_bwLimitSlider->deactivate();

    return false;  //  Only run once....
}

//
// Make Bandwidth Limit controls selectable (if appropriate)
static gboolean BwLimitActivate (gpointer pData)
{
    PxLLink *pLink = (PxLLink *)pData;

    bool supported = false;
    bool enabled = false;

    PxLAutoLock lock(&gCameraLock);

    if (gCamera)
    {
        if (gCamera->supported(FEATURE_BANDWIDTH_LIMIT))
        {
            float min, max, value;

            supported = true;
            enabled = gCamera->enabled (FEATURE_BANDWIDTH_LIMIT);

            gCamera->getRange(FEATURE_BANDWIDTH_LIMIT, &min, &max);
            pLink->m_bwLimitSlider->setRange(min, max);
            gCamera->getValue(FEATURE_BANDWIDTH_LIMIT, &value);
            pLink->m_bwLimitSlider->setValue(value);

            // Update the Bandwidth Limit label if it is limiting the frame rate
            bool warningRequired = gCamera->actualFrameRatelimiter() == FR_LIMITER_BANDWIDTH_LIMIT;
            if (warningRequired)
            {
                gtk_label_set_text (GTK_LABEL (pLink->m_bwLimitLabel), "Bandwidth Limit (Mbps) ** WARNING:Limits Frame Rate ** ");
            } else {
                gtk_label_set_text (GTK_LABEL (pLink->m_bwLimitLabel), "Bandwidth Limit (Mbps)");
            }

        }
    }

    gtk_widget_set_sensitive (pLink->m_bwLimitEnable, supported);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(pLink->m_bwLimitEnable), supported && enabled);
    pLink->m_bwLimitSlider->activate (supported && enabled);

    return false;  //  Only run once....
}

/* ---------------------------------------------------------------------------
 * --   Control functions from the Glade project
 * ---------------------------------------------------------------------------
 */

extern "C" void BwLimitEnableToggled
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{
    if (! gCamera || !gLinkTab) return;
    if (gCameraSelectTab->changingCameras()) return;
    if (gLinkTab->m_refreshRequired) return;

    bool enable = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(gLinkTab->m_bwLimitEnable));

    PxLAutoLock lock(&gCameraLock);

    if (enable)
    {
        float currentValue;

        currentValue = gLinkTab->m_bwLimitSlider->getScaleValue();
        gLinkTab->m_bwLimitSlider->activate(true);
        gCamera->setValue (FEATURE_BANDWIDTH_LIMIT, currentValue);
    } else {
        gCamera->disable(FEATURE_BANDWIDTH_LIMIT);
        gLinkTab->m_bwLimitSlider->activate(false);
    }

    gLinkTab->m_bwLimitSlider->activate (enable);

    // Update our Bandwidth, as it may now be limiting the frame rate
    gdk_threads_add_idle ((GSourceFunc)BwLimitActivate, gLinkTab);

    // Notify other tabs that may be affected by this change
    gControlsTab->refreshRequired(false);
}

extern "C" void BwLimitValueChanged
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{
    if (! gCamera || !gLinkTab) return;
    if (gCameraSelectTab->changingCameras()) return;
    if (gLinkTab->m_refreshRequired) return;

    float newValue;

    newValue = gLinkTab->m_bwLimitSlider->getEditValue();
    bool enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(gLinkTab->m_bwLimitEnable));

    if (enabled)
    {
        PxLAutoLock lock(&gCameraLock);

        PXL_RETURN_CODE rc = gCamera->setValue(FEATURE_BANDWIDTH_LIMIT, newValue);

        // Read it back to see if the camera did any 'rounding'
        if (API_SUCCESS(rc))
        {
            gCamera->getValue(FEATURE_BANDWIDTH_LIMIT, &newValue);
        }

        // Notify other tabs that may be affected by this change
        gControlsTab->refreshRequired(false);
    }

    gLinkTab->m_bwLimitSlider->setValue(newValue);

    // Update our Bandwidth, as it may now be limiting the frame rate
    gdk_threads_add_idle ((GSourceFunc)BwLimitActivate, gLinkTab);
}

extern "C" void BwLimitScaleChanged
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{

    if (! gCamera || !gLinkTab) return;
    if (gCameraSelectTab->changingCameras()) return;
    if (gLinkTab->m_refreshRequired) return;

    // we are only interested in changes to the scale from user input
    if (gLinkTab->m_bwLimitSlider->rangeChangeInProgress()) return;
    if (gLinkTab->m_bwLimitSlider->setIsInProgress()) return;

    float newValue;
    bool enabled = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(gLinkTab->m_bwLimitEnable));

    newValue = gLinkTab->m_bwLimitSlider->getScaleValue();

    if (enabled)
    {
        PxLAutoLock lock(&gCameraLock);

        PXL_RETURN_CODE rc = gCamera->setValue(FEATURE_BANDWIDTH_LIMIT, newValue);

        // Read it back to see if the camera did any 'rounding'
        if (API_SUCCESS(rc))
        {
            gCamera->getValue(FEATURE_BANDWIDTH_LIMIT, &newValue);
        }

        // Notify other tabs that may be affected by this change
        gControlsTab->refreshRequired(false);
    }

    gLinkTab->m_bwLimitSlider->setValue(newValue);

    // Update our Bandwidth, as it may now be limiting the frame rate
    gdk_threads_add_idle ((GSourceFunc)BwLimitActivate, gLinkTab);
}



