/*
 * Gnumeric, the GNOME spreadsheet.
 *
 * Graphics Wizard bootstap file
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include <glade/glade.h>
#include "wizard.h"

typedef struct {
	char *description;
	char *icon_name;
	char *guppi_type_name;
} subtype_info_t;

static subtype_info_t bar_subtypes [] = {
	{ N_("Plain bar display"), "icon-bar-plain.png", "bar" },
	{ NULL, NULL, NULL }
};

static struct {
	char *text;
	char *icon_name;
	subtype_info_t *subtypes;
} graphic_types [] = {
	{ N_("Bar plots"), "icon-bar-generic.png", bar_subtypes },
	{ NULL, NULL, NULL }
};

static void
graphic_type_selected (GtkCList *clist, gint row, gint column, GdkEvent *event,
		       WizardGraphicContext *gc)
{
	printf ("Row selected: %d\n", row);
}

void
fill_graphic_types (GladeXML *gui, WizardGraphicContext *gc)
{
	GtkCList *clist = GTK_CLIST (glade_xml_get_widget (gui, "graphic-type-clist"));
	int i;

	gtk_clist_set_column_justification (clist, 1, GTK_JUSTIFY_LEFT);
	gtk_signal_connect (GTK_OBJECT (clist), "select_row",
			    GTK_SIGNAL_FUNC (graphic_type_selected), gc);
	
	for (i = 0; graphic_types [i].text; i++){
		char *clist_text [2];

		clist_text [0] = "";
		clist_text [1] = _(graphic_types [i].text);
		gtk_clist_append (clist, clist_text);
	}
	gtk_clist_select_row (clist, 1, 0);
	gtk_clist_thaw (clist);
}



