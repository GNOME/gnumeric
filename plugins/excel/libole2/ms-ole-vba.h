/**
 * ms-ole-vba.h: MS Office VBA support
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 *
 * Copyright 2000 Helix Code, Inc.
 **/

#ifndef MS_OLE_VBA_H
#define MS_OLE_VBA_H

#include <ms-ole.h>

typedef struct _MsOleVba MsOleVba;

MsOleVba *ms_ole_vba_open  (MsOleStream *s);
void      ms_ole_vba_close (MsOleVba    *vba);

char      ms_ole_vba_getc  (MsOleVba    *vba);
char      ms_ole_vba_peek  (MsOleVba    *vba);
gboolean  ms_ole_vba_eof   (MsOleVba    *vba);

#endif
