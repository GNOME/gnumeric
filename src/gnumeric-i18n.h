#ifndef GNUMERIC_I18N_H
#define GNUMERIC_I18N_H

#include <libintl.h>

/* FIXME - replace WITH_GNOME with something real */
#define WITH_GNOME

#ifdef WITH_GNOME
#include <libgnome/gnome-i18n.h>
#else
#define _(String) gettext (String)
#define N_(String) (String)
#endif /* WITH_GNOME */

#define Q_(String) gnm_i18n_qprefix_gettext (String)

char *gnm_i18n_qprefix_gettext (const char *msg);

#endif /* GNUMERIC_I18N_H */
