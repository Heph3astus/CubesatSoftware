
/***************************************************************************
 *
 *     File: cameraSelect.cpp
 *
 *     Description:
 *         Controls for the 'Camera Select' and 'Video Preview' controls
 *         in CaptureOEM.
 */

#include <stdlib.h>
#include "cameraSelect.h"
#include "camera.h"
#include "captureOEM.h"
#include "preview.h"
#include "controls.h"
#include "stream.h"
#include "image.h"
#include "video.h"
#include "lens.h"
#include "filter.h"
#include "link.h"
#include "info.h"

// prototypes to allow top-down design
static void *scanThread (PxLCameraSelect *cameraSelect);
static gboolean rebuildCameraSelectCombo (gpointer pData);

extern PxLCameraSelect *gCameraSelectTab;
extern PxLPreview      *gVideoPreviewTab;
extern PxLControls     *gControlsTab;
extern PxLStream       *gStreamTab;
extern PxLImage        *gImageTab;
extern PxLVideo        *gVideoTab;
extern PxLLens         *gLensTab;
extern PxLFilter       *gFilterTab;
extern PxLLink         *gLinkTab;
extern PxLInfo         *gInfoTab;

/* ---------------------------------------------------------------------------
 * --   Member functions
 * ---------------------------------------------------------------------------
 */
PxLCameraSelect::PxLCameraSelect (GtkBuilder *builder)
: m_cameraChangeInProgress(false)
{
	//
	// Step 1
	//		Get our GTK control objects from the glade project
	m_csCombo = GTK_WIDGET( gtk_builder_get_object( builder, "CameraSelect_Combo" ) );

	//
	// Step 2
	//		Initialize our camera select data structures to NULL, and start the thread
	//      that will populate it.
	m_comboCameraList.clear();
	m_connectedCameraList.clear();

	m_scanThreadRunning = true;
	m_scanThread = g_thread_new ("cameraScanThread", (GThreadFunc)scanThread, this);
}

PxLCameraSelect::~PxLCameraSelect ()
{
	// Kill the camera scan thread
	m_scanThreadRunning = false;
	g_thread_join(m_scanThread);
    g_thread_unref (m_scanThread);
}

bool PxLCameraSelect::isConnected (ULONG serialNum)
{
    int cameraIndex;
    int numCameras = m_connectedCameraList.size();

    for (cameraIndex=0; cameraIndex<numCameras; cameraIndex++)
    {
        if (m_connectedCameraList[cameraIndex] == serialNum) break;
    }

    return (cameraIndex < numCameras);
}

// Returns the camera the user selected, or 0 if the user selected 'No Camera'
ULONG PxLCameraSelect::getSelectedCamera()
{
    gint cameraIndex = gtk_combo_box_get_active (GTK_COMBO_BOX(m_csCombo));

    if (cameraIndex < 1) return 0;

    return (atoi (gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT(m_csCombo))));
}

PXL_RETURN_CODE PxLCameraSelect::scanForCameras ()
{
    PXL_RETURN_CODE rc = ApiSuccess;
    ULONG numCameras = 0;

    //
    // Step 1
    //      We will construct a new list, so release the old one
    m_connectedCameraList.clear();

    //
    // Step 2
    //      Determine how many cameras are connected, and then get the serial numbers
    rc = PxLGetNumberCameras (NULL, &numCameras);
    if (API_SUCCESS(rc) && numCameras > 0)
    {
    	m_connectedCameraList.resize(numCameras);
        rc = PxLGetNumberCameras (&m_connectedCameraList[0], &numCameras);
        if (!API_SUCCESS(rc))
        {
            //
            // Step 3 (OnError)
            //    Could not get the serial numbers, so empty the list
        	m_connectedCameraList.clear();
        }
    }

    return rc;
}


/* ---------------------------------------------------------------------------
 * --   Control functions from the Glade project
 * ---------------------------------------------------------------------------
 */
extern "C" void NewCameraSelected
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{
    ULONG selectedCamera;

    // this 'handler' gets called as we rebuild the list -- ignore these ones
    // as we are only intereseted in user input
    if (gCameraSelectTab->m_rebuildInProgress) return;


    selectedCamera = gCameraSelectTab->getSelectedCamera();

    if (0 == selectedCamera)
    {
        if (NULL != gCamera)
        {
            // The user doesn't want this camera anymore
            printf ("Releasing camera %d\n",gCamera->serialNum());
            ReleaseCamera ();
        }
    } else {
        // The user selected a camera
        if (gCamera && (gCamera->serialNum() != selectedCamera))
        {
            // The user selected a different camera -- release the old one
            printf ("Released camera %d\n",gCamera->serialNum());
            ReleaseCamera ();
        }

        try
        {
            GrabCamera (selectedCamera);
            printf ("Grabbed camera %d\n",selectedCamera);
        } catch (PxLError& e) {
            printf ("%s\n", e.showReason());
        }
    }
    gCameraSelectTab->m_requestedCamera = selectedCamera;
    gdk_threads_add_idle ((GSourceFunc)rebuildCameraSelectCombo, gCameraSelectTab);
}

/* ---------------------------------------------------------------------------
 * --   Camera scan thread
 * ---------------------------------------------------------------------------
 */

// Rebuild the camera select list using the values specified in our camera select
// object.
//
// Design Note:
//   Note that this activity can be done as the result of a user action, or as a result
//   of a camera scan.  We do some 'special' handling when creating the camera list, as in
//   the 'typical case' where a camera is already selected (and still there), we want to update
//   the list without doing a COMPLETE list rebuild.  If we empty the list and then rebuild, we
//   get a slight flicker in the control.  So, rather than doing this, we will leave the current
//   camera alone, and only rebuild the rest of the list.
//
//   To accommodate this, we do the treat the list of cameras to choose from (m_comboCameraList)
//   as follows:
//       - If the list is empty, then m_selectedCamera == 0 and 'No Camera' is displayed on
//         the m_csCombo combo.
//       - if it is not empty, the m_comboCameraList[0] is the currently selected camera
//         (m_selectedCamera)
//       - non-selected cameras are ALWAYS at m_comboCameraList index 1 and above.
//
static gboolean rebuildCameraSelectCombo (gpointer pData)
{
    PxLCameraSelect *cameraSelect = (PxLCameraSelect *)pData;
    gchar cameraSerial[40];
    gint  currentListSize = cameraSelect->m_comboCameraList.size();
    gint  i;

    bool changingCameras = cameraSelect->m_selectedCamera != cameraSelect->m_requestedCamera;

    cameraSelect->m_rebuildInProgress = true;

    //
    // Step 1
    //      Make the necessary changes to the list of cameras that will be displayed
    //      when the user selects the camera select drop down.

    // Do NOT do a complete rebuild if the user is not changing cameras.  In other words,
    // do a partial rebuild if we have a camera and we are not changing to a new
    // camera.  See Design Note above for details.
    if (!changingCameras && cameraSelect->m_selectedCamera != 0)
    {
        // Only rebuild the list for non-active cameras

        // First, remove all entries from our list, and all of the old non-active ones from the combo box.
        cameraSelect->m_comboCameraList.clear();
        for (i = currentListSize; i > 1; i--)
        {
            gtk_combo_box_text_remove (GTK_COMBO_BOX_TEXT(cameraSelect->m_csCombo),i);
        }
        // Now, add all of the camera to our list, and all of the non-active ones to our combo box
        cameraSelect->m_comboCameraList.push_back(cameraSelect->m_requestedCamera);
        for (i = 0; i < (gint)cameraSelect->m_connectedCameraList.size(); i++)
        {
            if (cameraSelect->m_connectedCameraList[i] == cameraSelect->m_requestedCamera) continue;
            cameraSelect->m_comboCameraList.push_back (cameraSelect->m_connectedCameraList[i]);
            sprintf (cameraSerial, "%d", cameraSelect->m_connectedCameraList[i]);
            gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT(cameraSelect->m_csCombo), cameraSerial);
        }
     } else {
        // Rebuild the entire list.
         cameraSelect->m_comboCameraList.clear();
        gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT(cameraSelect->m_csCombo));

        // always have 'No Camera' as our first choice
        gtk_combo_box_text_insert_text (GTK_COMBO_BOX_TEXT(cameraSelect->m_csCombo), 0, "No Camera");

        // always have our current camera (if there is one) as our second choice
        if (0 != cameraSelect->m_requestedCamera)
        {
            cameraSelect->m_comboCameraList.push_back (cameraSelect->m_requestedCamera);
            sprintf (cameraSerial, "%d", cameraSelect->m_requestedCamera);
            gtk_combo_box_text_insert_text (GTK_COMBO_BOX_TEXT(cameraSelect->m_csCombo), 1, cameraSerial);
        }

        gtk_combo_box_set_active (GTK_COMBO_BOX(cameraSelect->m_csCombo),(0 == cameraSelect->m_requestedCamera ? 0 : 1));
        cameraSelect->m_selectedCamera = cameraSelect->m_requestedCamera;

        // And finally, add all of our non-active entries
        for (i = 0; i < (gint)cameraSelect->m_connectedCameraList.size(); i++)
        {
            if (cameraSelect->m_requestedCamera == cameraSelect->m_connectedCameraList[i]) continue;
            cameraSelect->m_comboCameraList.push_back (cameraSelect->m_connectedCameraList[i]);
            sprintf (cameraSerial, "%d", cameraSelect->m_connectedCameraList[i]);
            gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT(cameraSelect->m_csCombo), cameraSerial);
        }

     }

     //
    // Step 2
    //      If there has been a change in the currently selected camera, then
    //      update all of the other controls.

    // This is done as camera's are grabbed and released, so it does not need to be done again
    /*
     if (changingCameras)
     {
         cameraSelect->m_cameraChangeInProgress = true;
         // Changing Camera

         gVideoPreviewTab->refreshRequired(cameraSelect->m_selectedCamera == 0);
         gControlsTab->refreshRequired(cameraSelect->m_selectedCamera == 0);
         gStreamTab->refreshRequired(cameraSelect->m_selectedCamera == 0);
         gImageTab->refreshRequired(cameraSelect->m_selectedCamera == 0);
         gVideoTab->refreshRequired(cameraSelect->m_selectedCamera == 0);
         gLensTab->refreshRequired(cameraSelect->m_selectedCamera == 0);

         cameraSelect->m_cameraChangeInProgress = false;
     }
     */

     cameraSelect->m_rebuildInProgress = false;

     return false;  //  Only run once....
}

// thread to periodically scan the bus for PixeLINK cameras.
static void *scanThread (PxLCameraSelect* cameraSelect)
{
    ULONG rc = ApiSuccess;

    const ULONG sleepTimeUs = 1000 * 500; // 500 ms
    const ULONG pollsBetweenScans = 10;   // 5 seconds between scans

	// Create our initial (empty) camera list
    gCameraSelectTab->m_requestedCamera = 0;
	gdk_threads_add_idle ((GSourceFunc)rebuildCameraSelectCombo, cameraSelect);

    for (ULONG i = pollsBetweenScans; gCameraSelectTab->m_scanThreadRunning; i++)
    {
        if (i >= pollsBetweenScans)
        {
            i = 0; // restart our poll count
			rc = gCameraSelectTab->scanForCameras();
			if (API_SUCCESS(rc))
			{
				if (NULL == gCamera && gCameraSelectTab->m_connectedCameraList.size() > 0)
				{
					// There are some cameras, yet we haven't selected one
					// yet.  Simply pick the first one found.
					try
					{
						GrabCamera (gCameraSelectTab->m_connectedCameraList[0]);
					} catch (PxLError& e) {
						if (e.m_rc == ApiNoCameraError)
						{
							printf ("Could not grab camera %d -- still initializing??\n", gCameraSelectTab->m_connectedCameraList[0]);
							continue;
						}
						printf ("%s\n", e.showReason());
						gCameraSelectTab->m_scanThreadRunning = false;
						break;
					}
					printf ("Grabbed camera %d\n",gCamera->serialNum());
					gCameraSelectTab->m_requestedCamera = gCamera->serialNum();
					gdk_threads_add_idle ((GSourceFunc)rebuildCameraSelectCombo, cameraSelect);
				} else if (NULL != gCamera && ! gCameraSelectTab->isConnected(gCamera->serialNum())) {
					// The camera that we had is gone !!
					printf ("Released camera %d\n",gCamera->serialNum());
					ReleaseCamera();
					gCameraSelectTab->m_requestedCamera = 0;
					gdk_threads_add_idle ((GSourceFunc)rebuildCameraSelectCombo, cameraSelect);
					//continue;  // Try another scan immediately, as there may already be one connected.
				} else if (NULL != gCamera) {
					gCameraSelectTab->m_requestedCamera = gCamera->serialNum();
					gdk_threads_add_idle ((GSourceFunc)rebuildCameraSelectCombo, cameraSelect);
				}
			}
        }
        usleep (sleepTimeUs);  // wait a bit before we wake again.
    }

	// We are about to exit -- release the camera (if we have one)
    if (NULL != gCamera)
    {
    	printf ("Released camera %d\n",gCamera->serialNum());
    	ReleaseCamera();
    }

    return NULL;
}


