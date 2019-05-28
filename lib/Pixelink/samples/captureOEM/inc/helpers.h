/***************************************************************************
 *
 *     File: helpers.h
 *
 *     Description: Common helper functions used throughout captureOEM
 *
 */

#ifndef PIXELINK_HELPERS_H_
#define PIXELINK_HELPERS_H_

#include <gtk/gtk.h>


void IncrementFileName(GtkEntry *entry, const char* format);
void ReplaceFileExtension(GtkEntry *entry, const char* newExtension);



#endif /* PIXELINK_HELPERS_H_ */
