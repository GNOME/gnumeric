/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * workbook-format-toolbar.c: Format toolbar implementation
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 *   Jody Goldberg (jody@gnome.org)
 *
 * (C) 1998-1999 Miguel de Icaza.
 * (C) 2000-2004 Jody Goldberg
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "workbook-format-toolbar.h"

#include "gui-util.h"
#include "dialogs.h"
#include "selection.h"
#include "global-gnome-font.h"
#include "workbook-control-gui-priv.h"
#include "workbook-view.h"
#include "workbook-priv.h"
#include "sheet.h"
#include "sheet-view.h"
#include "sheet-style.h"
#include "cell.h"
#include "application.h"
#include "commands.h"
#include "format.h"
#include "style-color.h"
#include "style-border.h"
#include "ranges.h"
#include "mstyle.h"
#include "pixmaps/gnumeric-stock-pixbufs.h"
#include "widgets/gnumeric-combo-text.h"

#include <widgets/gnm-combo-box.h>
#include <widgets/widget-color-combo.h>
#include <widgets/widget-pixmap-combo.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkactiongroup.h>

#include <string.h>

/*
 * Removes the GTK_CAN_FOCUS flag from a container and its children.
 */
static void
disable_focus (GtkWidget *base, void *closure)
{
	if (GTK_IS_CONTAINER (base))
		gtk_container_foreach (GTK_CONTAINER (base), disable_focus, NULL);
	GTK_WIDGET_UNSET_FLAGS (base, GTK_CAN_FOCUS);
}

/****************************************************************************/
/* Border combo box */

void
workbook_create_format_toolbar (WorkbookControlGUI *wbcg)
{
	GtkWidget *fontsel, *fontsize, *entry;
	GtkWidget *border_combo, *back_combo, *fore_combo;
	ColorGroup *cg;

	GList *l;
	int i, len;

#ifndef WITH_BONOBO
	GtkWidget *font_button;
	GtkWidget *toolbar = gnumeric_toolbar_new (wbcg,
		workbook_format_toolbar, "FormatToolbar", 2, 0, 0);
#else
	bonobo_ui_component_add_verb_list_with_data (wbcg->uic, verbs, wbcg);
#endif


#ifdef WITH_BONOBO
	gnumeric_inject_widget_into_bonoboui (wbcg, fontsel, "/FormatToolbar/FontName");
	gnumeric_inject_widget_into_bonoboui (wbcg, fontsize, "/FormatToolbar/FontSize");
	gnumeric_inject_widget_into_bonoboui (wbcg, border_combo, "/FormatToolbar/BorderSelector");
	gnumeric_inject_widget_into_bonoboui (wbcg, back_combo, "/FormatToolbar/BackgroundColor");
	gnumeric_inject_widget_into_bonoboui (wbcg, fore_combo, "/FormatToolbar/ForegroundColor");
#else
	gnumeric_toolbar_insert_with_eventbox (
		GTK_TOOLBAR (toolbar), fontsel, _("Font selector"), NULL, 0);
	gnumeric_toolbar_insert_with_eventbox (
		GTK_TOOLBAR (toolbar), fontsize, _("Font size"), NULL, 1);
	gnumeric_toolbar_append_with_eventbox (
		GTK_TOOLBAR (toolbar),
		border_combo, _("Borders"), NULL);
	gnumeric_toolbar_append_with_eventbox (
		GTK_TOOLBAR (toolbar),
		back_combo, _("Background"), NULL);
	gnumeric_toolbar_append_with_eventbox (
		GTK_TOOLBAR (toolbar),
		fore_combo, _("Foreground"), NULL);

	/* Hide font selector button - only shown in vertical mode */
	font_button = gnumeric_toolbar_get_widget (GTK_TOOLBAR (toolbar),
						   TOOLBAR_FONT_BUTTON_INDEX);
	gtk_widget_hide (font_button);

	gtk_widget_show (toolbar);
	wbcg->format_toolbar = toolbar;
#endif
}

