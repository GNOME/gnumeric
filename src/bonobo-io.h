#ifndef _BONOBO_IO_H
#define _BONOBO_IO_H

#include <bonobo/bonobo-persist-stream.h>

void gnumeric_bonobo_read_from_stream (BonoboPersistStream       *ps,
				       Bonobo_Stream              stream,
				       Bonobo_Persist_ContentType type,
				       void                      *data,
				       CORBA_Environment         *ev);
#ifdef GNOME2_CONVERSION_COMPLETE
void gnumeric_bonobo_io_init (void);
#endif
#endif /* _BONOBO_IO_H */
