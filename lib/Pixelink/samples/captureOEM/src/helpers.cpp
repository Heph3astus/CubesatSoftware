/***************************************************************************
 *
 *     File: image.cpp
 *
 *     Description:
 *        Controls for the 'Image' tab  in CaptureOEM.
 */

#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include "helpers.h"

using namespace std;


/**
* Function: IncrementFileName
* Purpose:  Read the file name from the provided window, parse out whatever
*           number immediately precedes the file extension, increment the
*           number by one, and change the file name in the control to contain
*           the new number.
*           Eg: Changes "C:\image_3.bmp" to"C:\image_4.bmp".
*/
void IncrementFileName(GtkEntry *entry, const char* format)
{
    //
    // Step 1
    //      Find where the '.' is in the file name (if any)
    string name = gtk_entry_get_text (entry);
    size_t dotPos = name.rfind('.');
    if (dotPos == string::npos) dotPos = name.length();

    //
    // Step 2
    //      Find the number immediately preceeding the '.' (if there is one)
    size_t numPos = name.find_last_not_of ("0123456789", dotPos-1);
    if (numPos == string::npos)numPos = dotPos;
    else numPos++;
    int fileNum;
    if (numPos == dotPos)
    {
        // there is no current fileNum, so, start with 1
        fileNum = 1;
    } else {
        fileNum = (atoi ((name.substr (numPos, dotPos-numPos)).c_str())) + 1;
    }

    //
    // Step 3
    //      Build a new string using the 3 components
    gchar* newFileNum = g_strdup_printf (format, fileNum);
    string newName = name.substr (0, numPos) + newFileNum + name.substr (dotPos, name.length() - dotPos);
    g_free (newFileNum);

    //
    // Step 4
    //      Put this new name into the control
    gtk_entry_set_text (entry, newName.c_str());

}

/**
* Function: IncrementFileName
* Purpose:  Read the file name from the provided window, parse the file name,
*           replacing the extension with the requested one.
*           Eg: Changes "image_3.bmp" to"image_3.jpeg".
*/
void ReplaceFileExtension(GtkEntry *entry, const char* newExtension)
{
    //
    // Step 1
    //      Find where the '.' is in the file name (if any)
    string name = gtk_entry_get_text (entry);
    size_t dotPos = name.rfind('.');
    if (dotPos == string::npos) dotPos = name.length();


    //
    // Step 2
    //      Build a new string using the 2 components
    name.resize (dotPos);
    name = name + '.' + newExtension;

    //
    // Step 3
    //      Put this new name into the control
    gtk_entry_set_text (entry, name.c_str());

}




