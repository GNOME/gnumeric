#ifndef PLUGIN_APPLIX_H
#define PLUGIN_APPLIX_H

#include <gnumeric.h>
#include <gsf/gsf.h>
#include <stdio.h>

void     applix_read  (IOContext *io_context, Workbook *wb, GsfInput *src);
void     applix_write (IOContext *io_context, Workbook const *wb,
		       GsfOutput *sink);

#endif /* PLUGIN_APPLIX_H */
