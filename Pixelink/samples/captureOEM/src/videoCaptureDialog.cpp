
/***************************************************************************
 *
 *     File: videoCaptureDialog.cpp
 *
 *     Description:
 *       modal dialog used during a video capture
*/

#include <gtk/gtk.h>
#include "videoCaptureDialog.h"
#include "camera.h"
#include "captureOEM.h"

#define  POLL_TIME 1000  // in milliseconds

extern PxLVideoCaptureDialog      *gVideoCaptureDialog;
extern GtkWindow                  *gTopLevelWindow;

// prototypes to allow top down design
static gboolean VideoCapturePoll (gpointer pData);

/* ---------------------------------------------------------------------------
 * --   Member functions - Public
 * ---------------------------------------------------------------------------
 */
PxLVideoCaptureDialog::PxLVideoCaptureDialog (GtkBuilder *builder)
: m_pollNum(0)
, m_stillRunning(NULL)
{
    m_windowVideoCapture = GTK_WIDGET( gtk_builder_get_object( builder, "windowVideoCapture" ) );
    gtk_window_set_transient_for (GTK_WINDOW (m_windowVideoCapture), gTopLevelWindow);  // Bugzilla.1316

    m_progress = GTK_WIDGET( gtk_builder_get_object( builder, "VideoProgress_Label" ) );
}

PxLVideoCaptureDialog::~PxLVideoCaptureDialog ()
{
}

// Note:
//    estimatedCaptureTime is in milliseconds
//    stillRunning is a flag that the main app will set to false when the video capture has completed
void PxLVideoCaptureDialog::begin (int estimatedCaptureTime, bool* stillRunning)
{
    // This can only be done if there isn't a video capture already in progress
    if (!stillRunning || !*stillRunning) return;

    m_captureTime = estimatedCaptureTime;
    m_stillRunning = stillRunning;
    m_pollNum = 0;
    updateDialog (m_captureTime);
    gdk_threads_add_timeout (POLL_TIME, (GSourceFunc)VideoCapturePoll, this); // Run once a second

    gtk_window_present(GTK_WINDOW (m_windowVideoCapture));

}

//
// Note:
//   captureTimeRemaining can be negative.  In fact, it will be negative after the capture
//   has completed, but the API is formatting the video into it's final format (MP4, APIV, etc).
void PxLVideoCaptureDialog::updateDialog(int captureTimeRemaining)
{
    char cValue[40];

    if (captureTimeRemaining >= 0)
    {
        sprintf (cValue, "Capturing time remaining:%d", captureTimeRemaining/1000);
    } else {
        sprintf (cValue, "Formatting time:%d", (-captureTimeRemaining)/1000);
    }
    gtk_label_set_text (GTK_LABEL (m_progress), cValue);
}

/* ---------------------------------------------------------------------------
 * --   gtk thread callbacks - used to update controls
 * ---------------------------------------------------------------------------
 */

static gboolean VideoCapturePoll (gpointer pData)
{
    PxLVideoCaptureDialog *dialog = (PxLVideoCaptureDialog *)pData;
    dialog->m_pollNum++;

    if (!gCamera)
    {
        // The camera is gone!
        if (*(dialog->m_stillRunning))
        {
            gtk_widget_hide (dialog->m_windowVideoCapture);
        }
        return false;
    }

    if (*(dialog->m_stillRunning))
    {
        dialog->updateDialog (dialog->m_captureTime - (dialog->m_pollNum * POLL_TIME));
    } else {
        gtk_widget_hide (dialog->m_windowVideoCapture);
    }

    return *(dialog->m_stillRunning);

}

/* ---------------------------------------------------------------------------
 * --   Control functions from the Glade project
 * ---------------------------------------------------------------------------
 */

extern "C" void CancelVideoCapture
    (GtkWidget* widget, GdkEventExpose* event, gpointer userdata )
{
    if (gVideoCaptureDialog && gCamera)
    {
        // The user wants to quit.  The only way to do this, is to cycle the stream
        PxLAutoLock lock(&gCameraLock);
        gCamera->stopStream();
        gCamera->startStream();

        *gVideoCaptureDialog->m_stillRunning = false;
        gtk_widget_hide (gVideoCaptureDialog->m_windowVideoCapture);
    }
    return;
}



