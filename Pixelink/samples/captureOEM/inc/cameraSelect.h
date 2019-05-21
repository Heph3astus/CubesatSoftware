
/***************************************************************************
 *
 *     File: cameraSelect.h
 *
 *     Description:
 *         Controls for the 'Camera Select' and 'Video Preview' controls
 *         in CaptureOEM.
 *
 */

#if !defined(PIXELINK_CAMERA_SELECT_H)
#define PIXELINK_CAMERA_SELECT_H

#include <vector>
#include <gtk/gtk.h>
#include "PixeLINKApi.h"

class PxLCameraSelect
{
public:
    // Constructor
	PxLCameraSelect (GtkBuilder *builder);
	// Destructor
	~PxLCameraSelect ();

    GtkWidget    *m_csCombo;

    GtkWidget    *m_play;
    GtkWidget    *m_pause;
    GtkWidget    *m_stop;

    GtkWidget    *m_resize;

    static void  rebuildCameraSelectCombo (ULONG activeCamera);
    ULONG        getSelectedCamera();

    PXL_RETURN_CODE scanForCameras ();
    bool            isConnected (ULONG serialNum);
    bool            changingCameras ();

    std::vector<ULONG> m_comboCameraList;     // The set of cameras represented in the combo list
    std::vector<ULONG> m_connectedCameraList; // The set of cameras currently connected
    ULONG			   m_selectedCamera;      // The camera currently selected (or 0 for No Camera)
    ULONG			   m_requestedCamera;     // The camera to be selected (or 0 for No Camera)

    bool	m_rebuildInProgress;
    bool    m_cameraChangeInProgress;
    bool    m_scanThreadRunning;

    GThread    		  *m_scanThread;


};

inline bool PxLCameraSelect::changingCameras()
{
    return (m_cameraChangeInProgress);
}

#endif // !defined(PIXELINK_CAMERA_SELECT_H)
