#ifndef PLUGIN_APPLIX_H
#define PLUGIN_APPLIX_H

#include <gnumeric.h>
#include <gsf/gsf.h>
#include <stdio.h>

void     applix_read  (IOContext *io_context, WorkbookView *wbv, GsfInput *src);
void     applix_write (IOContext *io_context, WorkbookView const *wbv,
		       GsfOutput *sink);

#endif /* PLUGIN_APPLIX_H */
