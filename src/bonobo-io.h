#ifndef _GNM_BONOBO_IO_H
#define _GNM_BONOBO_IO_H

#include <bonobo/bonobo-object.h>

BonoboObject *gnm_persist_stream_new (WorkbookControl *wbc,
				      const char* const iid);
#endif /* _GNM_BONOBO_IO_H */
