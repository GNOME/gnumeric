#ifndef PLUGIN_APPLIX_H
#define PLUGIN_APPLIX_H

#include <gnumeric.h>

void     applix_read  (GOIOContext *io_context, WorkbookView *wbv, GsfInput *src);
void     applix_write (GOIOContext *io_context, WorkbookView const *wbv,
		       GsfOutput *sink);

#endif /* PLUGIN_APPLIX_H */
