/*
 * corba-args.c:  This routine bootstraps Gnumeric with CORBA.
 *
 * The non-CORBA bootstrap code is found on normal-args.c
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "main.h"

#include "sheet.h"
#include "embeddable-grid.h"
#include "corba.h"
#include "workbook-private.h"
#include "ranges.h"
#include "value.h"

#include <libgnome/gnome-i18n.h>
#include <liboaf/liboaf.h>
#include <bonobo.h>

void
gnumeric_arg_parse (int argc, char *argv [])
{
	CORBA_ORB         orb;

	ctx = NULL;

	gnomelib_register_popt_table (oaf_popt_options, _("Oaf options"));
	gnome_init_with_popt_table ("gnumeric", VERSION,
				    argc, argv, gnumeric_popt_options, 0, &ctx);

	orb = oaf_init (argc, argv);

	if (bonobo_init (orb, NULL, NULL) == FALSE)
		g_error ("Failure starting up Bonobo");
}

static void
grid_destroyed (GtkObject *embeddable_grid, Workbook *wb)
{
	wb->priv->workbook_views = g_list_remove (wb->priv->workbook_views, embeddable_grid);
}


Bonobo_Unknown
workbook_container_get_object (BonoboObject *container, CORBA_char *item_name,
			       CORBA_boolean only_if_exists, CORBA_Environment *ev,
			       Workbook *wb);
Bonobo_Unknown
workbook_container_get_object (BonoboObject *container, CORBA_char *item_name,
			       CORBA_boolean only_if_exists, CORBA_Environment *ev,
			       Workbook *wb)
{
	EmbeddableGrid *eg;
	Sheet *sheet;
	char *p;
	Value *range = NULL;

	sheet = workbook_sheet_by_name (wb, item_name);

	if (!sheet)
		return CORBA_OBJECT_NIL;

	p = strchr (item_name, '!');
	if (p) {
		*p++ = 0;

		/* this handles inversions, and relative ranges */
		range = range_parse (sheet, p, TRUE);
		if (range){
			CellRef *a = &range->v_range.cell.a;
			CellRef *b = &range->v_range.cell.b;

			if ((a->col < 0 || a->row < 0) ||
			    (b->col < 0 || b->row < 0) ||
			    (a->col > b->col) ||
			    (a->row > b->row)){
				value_release (range);
				return CORBA_OBJECT_NIL;
			}
		}
	}

	eg = embeddable_grid_new (wb, sheet);
	if (!eg)
		return CORBA_OBJECT_NIL;

	/*
	 * Do we have further configuration information?
	 */
	if (range) {
		CellRef *a = &range->v_range.cell.a;
		CellRef *b = &range->v_range.cell.b;

		embeddable_grid_set_range (eg, a->col, a->row, b->col, b->row);
	}

	gtk_signal_connect (GTK_OBJECT (eg), "destroy",
			    grid_destroyed, wb);

	wb->priv->workbook_views = g_list_prepend (wb->priv->workbook_views, eg);

	return CORBA_Object_duplicate (
		bonobo_object_corba_objref (BONOBO_OBJECT (eg)), ev);
}
