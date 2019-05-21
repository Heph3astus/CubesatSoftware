/***************************************************************************
 *
 *     File: tab.h
 *
 *     Description:
 *         Common object for all tabs  in CaptureOEM.
 *
 */

#if !defined(PIXELINK_TAB_H)
#define PIXELINK_TAB_H

class PxLTab
{
public:

    // Destructor
    PxLTab () : m_refreshRequired (false) {}
    virtual ~PxLTab () {return;}

    virtual void activate () {return;}   // the user has selected this tab
    virtual void deactivate () {return;} // the user has un-selected this tab
    virtual void refreshRequired (bool noCamera) {return;}  // Camera status has changed, requiring a refresh of controls

    // m_refreshRequired serves 2 purposes
    //    1. As an indication that some sort of change happened that requires us to refresh of the controls
    //    2. As an indication that the controls are being updated not because the user changed it's value, but because
    //       something else has changed.
    bool  m_refreshRequired;

};

#endif // !defined(PIXELINK_TAB_H)
