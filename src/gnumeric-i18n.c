/*
 * gnumeric-i18n.c:
 *
 * Author:
 *   Zbigniew Chyla <cyba@gnome.pl>
 */

#include <gnumeric-config.h>
#include <string.h>
#include <glib.h>
#include "gnumeric-i18n.h"

#define Q_PREFIX_START '!'
#define Q_PREFIX_END   '!'

char *
gnm_i18n_qprefix_gettext (const char *msg)
{
	g_return_val_if_fail (msg != NULL, NULL);

	if (*msg != Q_PREFIX_START) {
		return gettext (msg);
	} else {
		char *translation;

		translation = gettext (msg);
		if (translation != msg) {
			if (*translation != Q_PREFIX_START) {
				return translation;
			} else {
				char *real_translation;

				real_translation = strchr (translation + 1, Q_PREFIX_END);
				if (real_translation != NULL) {
					return real_translation + 1;
				} else {
					g_warning ("Ivalid Q_() translation: \"%s\"", translation);
					return translation;
				}
			}
		} else {
			char *real_msg;

			real_msg = strchr (msg + 1, Q_PREFIX_END);
			if (real_msg != NULL) {
				/* try again, this time without the prefix */
				return gettext (real_msg + 1);
			} else {
				g_warning ("Ivalid Q_() msg: \"%s\"", msg);
				return (char *) msg;
			}
		}
	}
}
