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

#include <config.h>
#include "gui-validation.h"
#include "gnumeric.h"
#include "str.h"
#include "style-condition.h"
#include "validation.h"
#include "value.h"

#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <string.h>
#include <locale.h>

static char *
validation_generate_msg (StyleCondition *sc)
{
	StyleCondition *sci;
	GString *s;
	char *t;

	g_return_val_if_fail (sc != NULL, NULL);
	
	s = g_string_new (_("The value you enter :\n"));

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
	 * For "INFORMATION" : "Had best be" ?
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

gboolean
validation_get_accept (GtkWindow *parent, Validation const *v)
{
	GnomeDialog *dialog;
	const char  *title    = v->title ? v->title->str : NULL;
	const char  *msg      = v->msg ? v->msg->str : NULL;
	char        *msg_auto = NULL;
	int          ret;
	gboolean     result = FALSE;

	if (!msg || strlen (msg) == 0)
		msg_auto = validation_generate_msg (v->sc);
	
	switch (v->vs) {
	case VALIDATION_STYLE_NONE :
		result = TRUE;
		break;
	case VALIDATION_STYLE_STOP :
		dialog = GNOME_DIALOG (
			gnome_message_box_new (
				msg_auto ? msg_auto : msg, GNOME_MESSAGE_BOX_ERROR,
				_("Ok"), NULL));
		gnome_dialog_set_parent (dialog, parent);
		if (title)
			gtk_window_set_title (GTK_WINDOW (dialog), title);
		gnome_dialog_run (dialog);
		result = FALSE;
		break;
	case VALIDATION_STYLE_WARNING :
		dialog = GNOME_DIALOG (
			gnome_message_box_new (
				msg_auto ? msg_auto : msg , GNOME_MESSAGE_BOX_WARNING,
				_("Accept"), _("Discard"), NULL));
		/* FIXME: This doesn't seem to have any effect */
		gnome_dialog_set_default (dialog, 0);
		gnome_dialog_set_parent (dialog, parent);
		if (title)
			gtk_window_set_title (GTK_WINDOW (dialog), title);
		ret = gnome_dialog_run (dialog);
		
		if (ret == 0)
			result = TRUE;
		else 
			result = FALSE;
		break;
	case VALIDATION_STYLE_INFO :
		dialog = GNOME_DIALOG (
			gnome_message_box_new (
				msg_auto ? msg_auto : msg, GNOME_MESSAGE_BOX_INFO,
				_("Ok"), NULL));
		gnome_dialog_set_parent (dialog, parent);
		if (title)
			gtk_window_set_title (GTK_WINDOW (dialog), title);
		gnome_dialog_run (dialog);
		result = TRUE;
		break;
	default :
		g_warning ("Unknown validation handler");
		result = FALSE;
	}
	
	if (msg_auto)
		g_free (msg_auto);
	
	return result;
}
