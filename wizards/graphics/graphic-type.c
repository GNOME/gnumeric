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
graphic_type_selected (GtkCList *clist, gint row, gint column, GdkEvent *event, GraphicContext *gc)
{
	printf ("Row selected: %d\n", row);
}

void
fill_graphic_types (GladeXML *gui, GraphicContext *gc)
{
	GtkCList *clist = GTK_CLIST (glade_xml_get_widget (gui, "graphic-type-clist"));
	int i;
	
	gtk_signal_connect (GTK_OBJECT (clist), "select_row",
			    GTK_SIGNAL_FUNC (graphic_type_selected), gc);
	
	for (i = 0; graphic_types [i].text; i++){
		printf ("Setting text\n");
		gtk_clist_set_text (clist, i, 2, _(graphic_types [i].text));
	}
	gtk_clist_select_row (clist, 1, 0);
}

