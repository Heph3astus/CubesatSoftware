/***************************************************************************
 *
 *     File: image.h
 *
 *     Description:
 *         Controls for the 'Image' tab  in CaptureOEM.
 *
 */

#if !defined(PIXELINK_IMAGE_H)
#define PIXELINK_IMAGE_H

#include <gtk/gtk.h>
#include <stdio.h>
#include <PixeLINKApi.h>
#include "tab.h"

class PxLImage : public PxLTab
{
public:
    typedef enum _IMAGE_FORMATS
    {
        IMAGE_BITMAP,
        IMAGE_JPEG,
        IMAGE_PNG,
        IMAGE_TIFF,
        IMAGE_ADOBE,
        IMAGE_RAW
    } IMAGE_FORMATS;

    // Constructor
    PxLImage (GtkBuilder *builder);
    // Destructor
    ~PxLImage ();

    void activate ();   // the user has selected this tab
    void deactivate (); // the user has un-selected this tab
    void refreshRequired (bool noCamera);  // Camera status has changed, requiring a refresh of controls

    ULONG toApiImageFormat(IMAGE_FORMATS format);

    //
    // All of the controls

    GtkWidget    *m_fileName;
    GtkWidget    *m_fileType;
    GtkWidget    *m_fileLocation;
    GtkWidget    *m_fileLocationBrowser;
    GtkWidget    *m_fileNameIncrement;
    GtkWidget    *m_captureLaunch;
    GtkWidget    *m_captureButton;

    // m_refreshRequired serves 2 purposes
    //    1. As an indication that some sort of change happened that requires us to refresh of the controls
    //    2. As an indication that the controls are being updated not because the user changed it's value, but because
    //       something else has changed.
    bool  m_refreshRequired;

};

inline ULONG PxLImage::toApiImageFormat(IMAGE_FORMATS format)
{
    switch (format) {
    default:
    case IMAGE_BITMAP: return IMAGE_FORMAT_BMP;
    case IMAGE_JPEG: return IMAGE_FORMAT_JPEG;
    case IMAGE_PNG: return IMAGE_FORMAT_PNG;
    case IMAGE_TIFF: return IMAGE_FORMAT_TIFF;
    case IMAGE_ADOBE: return IMAGE_FORMAT_PSD;
    }
}


#endif // !defined(PIXELINK_IMAGE_H)
