/*
 * gui-validation.c: GUI Utils for the validation feature.
 *
 * Copyright (C) Almer S. Tigelaar <almer@gnome.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "gui-validation.h"

#include "str.h"
#include "style-condition.h"
#include "validation.h"
#include "value.h"
#include "gui-util.h"

#include <libgnome/gnome-i18n.h>
#include <string.h>
#include <locale.h>

#if 0
static char *
validation_generate_msg (StyleCondition *sc)
{
	StyleCondition *sci;
	GString *s;
	char *t;

	g_return_val_if_fail (sc != NULL, NULL);

	s = g_string_new (_("The value you entered :\n"));

	/* FIXME : this should only complain about the contraints that fail, */
	/*
	 * FIXME: Some things need to be done here once we implement
	 * the other validation types.
	 * The current setup works for constraint types Whole Number, Decimal, Date
	 * and Time.
	 * For List :
	 * - First find the constraint
	 * - Generate a string of the form "Must appear in region %s"
	 *   (where %s is the rangeref contained by the expression)
	 * For TextLength :
	 * - Again, first find the constraint
	 * - Generate a string of the form "Text length must ..."
	 *   ("be equal to", "be greater than", etc..)
	 * For Custom :
	 * - Find constraint
	 * - Generate a string like "Formula must be equal to %s"
	 *   (where %s is literally the entered expression, not it's outcome)
	 */

	/*
	 * FIXME2: We can improve the strings by making them dependent
	 * on the validation action to be taken.
	 * For "STOP" : "Must be"
	 * For "WARNING" : "Should be"
	 * For "INFORMATION" : "Are generally" ?
	 */
	for (sci = sc; sci != NULL; sci = sci->next) {
		switch (sci->type) {
		case SCT_EXPR :
			switch (sci->u.expr.op) {
			case SCO_EQUAL         : t = _("Must be equal to %s\n");                 break;
			case SCO_NOT_EQUAL     : t = _("Must not be equal to %s\n");             break;
			case SCO_GREATER       : t = _("Must be greater than %s\n");             break;
			case SCO_LESS          : t = _("Must be less than %s\n");                break;
			case SCO_GREATER_EQUAL : t = _("Must be greater than or equal to %s\n"); break;
			case SCO_LESS_EQUAL    : t = _("Must be less than or equal to %s\n");    break;
			case SCO_BOOLEAN_EXPR  : t = _("The expression must be true.");		 break;
			default :
				t = _("<unknown> <%s>\n");
			}

			/* Force a re-eval if needed */
			if (!sci->u.expr.val) {
				g_return_val_if_fail (dependent_needs_recalc (&sci->u.expr.dep), FALSE);
				dependent_eval (&sci->u.expr.dep);
			}

			if (!sci->u.expr.val)
				g_warning ("Error determining expression value");

			g_string_sprintfa (s, t, value_peek_string (sci->u.expr.val));
			break;
		case SCT_CONSTRAINT :
			switch (sci->u.constraint) {
			case SCC_IS_INT     : t = _("Must be a whole number\n"); break;
			case SCC_IS_FLOAT   : t = _("Must be a decimal\n");      break;
			case SCC_IS_IN_LIST : t = _("Must be in list\n");        break;
			case SCC_IS_DATE    : t = _("Must be a date\n");         break;
			case SCC_IS_TIME    : t = _("Must be a time\n");         break;
			case SCC_IS_TEXTLEN : t = _("Must have textlength\n");   break;
			case SCC_IS_CUSTOM  : t = _("Must equal the formula\n");  break;
			default :
				t = _("<unknown>");
			}

			g_string_append (s, t);
			break;
		case SCT_FLAGS :
			if (sci->u.flags & SCF_ALLOW_BLANK)
				g_string_append (s, _("Can be blank\n"));
			break;
		}
	}

	t = s->str;
	g_string_free (s, FALSE);
	return t;
}
#endif

/**
 * validation_get_accept :
 * 	 1 : ignore invalid and accept result
 * 	 0 : discard invalid and finish editing
 *	-1 : continue editing
 */
int
validation_get_accept (Validation const *v, char const *title, char const *msg,
		       WorkbookControlGUI *wbcg)
{
	int res0, res1, button;

	GtkWidget  *dialog;

	switch (v->vs) {
	case VALIDATION_STYLE_STOP :
		res0 = -1; res1 = 0;
		dialog = gnome_message_box_new ( msg, GNOME_MESSAGE_BOX_ERROR,
			_("Re-Edit"), _("Discard"), NULL);
		break;
	case VALIDATION_STYLE_WARNING :
		res0 = 1; res1 = 0;
		dialog = gnome_message_box_new (msg , GNOME_MESSAGE_BOX_WARNING,
			_("Accept"), _("Discard"), NULL);
		break;
	case VALIDATION_STYLE_INFO :
		res0 = res1 = 1;
		dialog = gnome_message_box_new (msg, GNOME_MESSAGE_BOX_INFO,
			_("Ok"), NULL);
		break;
	default : g_return_val_if_fail (FALSE, 1);
	}

	if (title)
		gtk_window_set_title (GTK_WINDOW (dialog), title);
	gnome_dialog_set_default (GNOME_DIALOG (dialog), 0);
	button = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));

	return (button != 1) ? res0 : res1;
}
