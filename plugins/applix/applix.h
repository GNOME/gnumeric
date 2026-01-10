#ifndef GNM_PLUGIN_APPLIX_H_
#define GNM_PLUGIN_APPLIX_H_

#include <gnumeric.h>

void     applix_read  (GOIOContext *io_context, WorkbookView *wbv, GsfInput *src);
void     applix_write (GOIOContext *io_context, WorkbookView const *wbv,
		       GsfOutput *sink);

#endif /* GNM_PLUGIN_APPLIX_H_ */
