/***************************************************************************
 *
 *     File: gpio.h
 *
 *     Description:
 *         Controls for the 'GPIO' tab  in CaptureOEM.
 *
 */

#if !defined(PIXELINK_GPIO_H)
#define PIXELINK_GPIO_H

#include <gtk/gtk.h>
#include <stdio.h>
#include <PixeLINKApi.h>
#include <vector>
#include "tab.h"

class PxLGpio : public PxLTab
{
public:
    typedef enum _HW_TRIGGER_MODES
    {
        MODE_0,
        MODE_1,
        MODE_14
    } HW_TRIGGER_MODES;

    // Constructor
    PxLGpio (GtkBuilder *builder);
    // Destructor
    ~PxLGpio ();

    void activate ();   // the user has selected this tab
    void deactivate (); // the user has un-selected this tab
    void refreshRequired (bool noCamera);  // Camera status has changed, requiring a refresh of controls

    //
    // All of the controls

    GtkWidget    *m_triggerType;

    GtkWidget    *m_swTriggerButton;

    GtkWidget    *m_hwTriggerMode;
    GtkWidget    *m_hwTriggePolarity;
    GtkWidget    *m_hwTriggerDelay;
    GtkWidget    *m_hwTriggerParam1Type;
    GtkWidget    *m_hwTriggerNumber;
    GtkWidget    *m_hwTriggerUpdate;
    GtkWidget    *m_hwTriggerDescription;

    GtkWidget    *m_gpioNumber;
    GtkWidget    *m_gpioEnable;
    GtkWidget    *m_gpioMode;
    GtkWidget    *m_gpioPolarity;
    GtkWidget    *m_gpioParam1Type;
    GtkWidget    *m_gpioParam1Value;
    GtkWidget    *m_gpioParam1Units;
    GtkWidget    *m_gpioParam2Type;
    GtkWidget    *m_gpioParam2Value;
    GtkWidget    *m_gpioParam2Units;
    GtkWidget    *m_gpioParam3Type;
    GtkWidget    *m_gpioParam3Value;
    GtkWidget    *m_gpioParam3Units;
    GtkWidget    *m_gpioUpdate;
    GtkWidget    *m_gpioDescription;

    std::vector<int>   m_supportedHwTriggerModes;
    std::vector<int>   m_supportedGpioModes;

    bool m_gpiLast; // last read state of the GP Input

    bool InRange(int value, int min, int max);
    HW_TRIGGER_MODES ModeToIndex(float trigMode);
    float IndexToMode (HW_TRIGGER_MODES);
};

inline bool PxLGpio::InRange(int value, int min, int max)
{
    return (value >= min && value <= max);
}

inline PxLGpio::HW_TRIGGER_MODES PxLGpio::ModeToIndex(float trigMode)
{
    switch ((int)trigMode)
    {
    case 0:  return PxLGpio::MODE_0;
    case 1:  return PxLGpio::MODE_1;
    case 14: return PxLGpio::MODE_14;
    default: return PxLGpio::MODE_0;
    }
}

inline float PxLGpio::IndexToMode(HW_TRIGGER_MODES index)
{
    switch (index)
    {
    case MODE_0:  return 0.0;
    case MODE_1:  return 1.0;
    case MODE_14: return 14.0;
    default:      return 0.0;
    }
}




#endif // !defined(PIXELINK_GPIO_H)
