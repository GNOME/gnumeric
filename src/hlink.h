#ifndef GNUMERIC_HLINK_H
#define GNUMERIC_HLINK_H

#include "gnumeric.h"
#include <glib-object.h>

GType gnm_hlink_url_type ();
GType gnm_hlink_gnumeric_type ();
GType gnm_hlink_newbook_type ();
GType gnm_hlink_email_type ();

gboolean  gnm_hlink_activate (GnmHLink *link);
GnmHLink *sheet_hlink_find   (Sheet const *sheet, CellPos const *pos);

#endif /* GNUMERIC_HLINK_H */
