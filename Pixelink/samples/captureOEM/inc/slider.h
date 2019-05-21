
/***************************************************************************
 *
 *     File: slider.h
 *
 *     Description: Simple wrapper class for all slider controls
 *
 */

#if !defined(PIXELINK_SLIDER_H)
#define PIXELINK_SLIDER_H

#include <gtk/gtk.h>
#include "PixeLINKApi.h"

class PxLSlider
{
public:
    // Constructor
	PxLSlider (GtkWidget *min, GtkWidget *max, GtkWidget *scale, GtkWidget *value);
	// Destructor
	~PxLSlider ();

    // All slider controls become non-selectable, and their values meaningless
	void  deactivate ();
	// Set the slider controls as selectable or not (and thus controls user write access).
    void  activate(bool writeable);
    // Sets new values for the sliders values (slider can be selectable, or not)
    void  setValue(double value);
    void  setRange(double min, double max);
    bool  rangeChangeInProgress();
    bool  setIsInProgress();
    // Returns the value currently in the 'value' edit box for the control
    double  getEditValue();
    // Returns the value currently specified by the 'scale' control (the actual slider)
    double  getScaleValue();

    GtkWidget    *m_min;
    GtkWidget    *m_max;
    GtkWidget    *m_scale;
    GtkWidget    *m_value;

private:
    bool  m_rangeChangeInProgress;
    bool  m_setIsInProgress;

    double m_minValue;
    double m_maxValue;

};

inline bool PxLSlider::rangeChangeInProgress()
{
    return m_rangeChangeInProgress;
}

inline bool PxLSlider::setIsInProgress()
{
    return m_setIsInProgress;
}


#endif // !defined(PIXELINK_SLIDER_H)
