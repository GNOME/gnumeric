/*
 * WidgetPixmapCombo: A pixmap selector combo box
 *
 * Author:
 *   Jody Goldberg <jgoldberg@home.com>
 */
#include <config.h>
#include <gnome.h>
#include "gtk-combo-box.h"
#include "widget-pixmap-combo.h"

#define PIXMAP_PREVIEW_WIDTH 15
#define PIXMAP_PREVIEW_HEIGHT 15

enum {
	CHANGED,
	LAST_SIGNAL
};

static gint pixmap_combo_signals [LAST_SIGNAL] = { 0, };
static GtkObjectClass *pixmap_combo_parent_class;

/***************************************************************************/

static void
pixmap_combo_finalize (GtkObject *object)
{
	PixmapCombo *pc = PIXMAP_COMBO (object);

	g_free (pc->pixmaps);
	(*pixmap_combo_parent_class->finalize) (object);
}

static void
pixmap_combo_class_init (GtkObjectClass *object_class)
{
	object_class->finalize = pixmap_combo_finalize;

	pixmap_combo_parent_class = gtk_type_class (gtk_combo_box_get_type ());

	pixmap_combo_signals [CHANGED] =
		gtk_signal_new (
			"changed",
			GTK_RUN_LAST,
			object_class->type,
			GTK_SIGNAL_OFFSET (PixmapComboClass, changed),
			gtk_marshal_NONE__INT,
			GTK_TYPE_NONE, 1, GTK_TYPE_INT);

	gtk_object_class_add_signals (object_class, pixmap_combo_signals, LAST_SIGNAL);
}

GtkType
pixmap_combo_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"PixmapCombo",
			sizeof (PixmapCombo),
			sizeof (PixmapComboClass),
			(GtkClassInitFunc) pixmap_combo_class_init,
			(GtkObjectInitFunc) NULL,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (gtk_combo_box_get_type (), &info);
	}

	return type;
}

static void
emit_change (GtkWidget *button, PixmapCombo *pc)
{
	g_return_if_fail (pc != NULL);
	g_return_if_fail (0 <= pc->last_index);
	g_return_if_fail (pc->last_index < pc->num_elements);

	gtk_signal_emit (GTK_OBJECT (pc), pixmap_combo_signals [CHANGED],
			 pc->elements[pc->last_index].index);
}

static void
pixmap_clicked (GtkWidget *button, PixmapCombo *pc)
{
	int index = GPOINTER_TO_INT (gtk_object_get_user_data (GTK_OBJECT (button)));
	pixmap_combo_select_pixmap (pc, index);
	emit_change (button, pc);
	gtk_combo_box_popup_hide (GTK_COMBO_BOX (pc));
}

static void
pixmap_table_setup (PixmapCombo *pc)
{
	int row, col, index = 0;

	pc->combo_table = gtk_table_new (pc->cols, pc->rows, 0);
	pc->tool_tip = gtk_tooltips_new ();
	pc->pixmaps = g_malloc (sizeof (GnomePixmap *) * pc->cols * pc->rows);

	for (row = 0; row < pc->rows; row++) {
		for (col = 0; col < pc->cols; ++col, ++index) {
			PixmapComboElement const *element = pc->elements + index;
			GtkWidget *button;

			if (element->xpm_data == NULL) {
				/* exit both loops */
				row = pc->rows;
				break;
			}

			pc->pixmaps[index] = GNOME_PIXMAP (
			       gnome_pixmap_new_from_xpm_d (element->xpm_data));

			button = gtk_button_new ();
			gtk_button_set_relief (GTK_BUTTON (button),
					       GTK_RELIEF_NONE);
			gtk_container_add (GTK_CONTAINER (button),
					   GTK_WIDGET (pc->pixmaps[index]));
			gtk_tooltips_set_tip (pc->tool_tip,
					      button,
					      _(element->untranslated_tooltip),
					      "What goes here ??");

			gtk_table_attach (GTK_TABLE (pc->combo_table), button,
					  col, col+1, row+1, row+2, GTK_FILL, GTK_FILL, 1, 1);

			gtk_signal_connect (GTK_OBJECT (button), "clicked",
					    GTK_SIGNAL_FUNC (pixmap_clicked), pc);
			gtk_object_set_user_data (GTK_OBJECT (button),
						  GINT_TO_POINTER (index));
		}
	}
	pc->num_elements = index;

	gtk_widget_show_all (pc->combo_table);
}

static void
pixmap_combo_construct (PixmapCombo *pc,
			PixmapComboElement const *elements, int ncols, int nrows)
{
	g_return_if_fail (pc != NULL);
	g_return_if_fail (IS_PIXMAP_COMBO (pc));

	/* Our table selector */
	pc->cols = ncols;
	pc->rows = nrows;
	pc->elements = elements;
	pixmap_table_setup (pc);

	pc->preview_button = gtk_button_new ();
	if (!gnome_preferences_get_toolbar_relief_btn ())
		gtk_button_set_relief (GTK_BUTTON (pc->preview_button), GTK_RELIEF_NONE);
	if (!gnome_preferences_get_toolbar_relief_btn ())
		gtk_combo_box_set_arrow_relief (GTK_COMBO_BOX (pc), GTK_RELIEF_NONE);

	pc->preview_pixmap = GTK_PIXMAP (gtk_pixmap_new (pc->pixmaps[0]->pixmap,
							 pc->pixmaps[0]->mask));
	gtk_container_add (GTK_CONTAINER (pc->preview_button), GTK_WIDGET (pc->preview_pixmap));
	gtk_widget_set_usize (GTK_WIDGET (pc->preview_pixmap), 24, 24);
	gtk_signal_connect (GTK_OBJECT (pc->preview_button), "clicked",
			    GTK_SIGNAL_FUNC (emit_change), pc);

	gtk_widget_show_all (pc->preview_button);

	gtk_combo_box_construct (GTK_COMBO_BOX (pc),
				 pc->preview_button,
				 pc->combo_table);
}

GtkWidget *
pixmap_combo_new (PixmapComboElement const *elements, int ncols, int nrows)
{
	PixmapCombo *pc;

	g_return_val_if_fail (elements != NULL, NULL);
	g_return_val_if_fail (elements != NULL, NULL);
	g_return_val_if_fail (ncols > 0, NULL);
	g_return_val_if_fail (nrows > 0, NULL);

	pc = gtk_type_new (pixmap_combo_get_type ());

	pixmap_combo_construct (pc, elements, ncols, nrows);

	return GTK_WIDGET (pc);
}

void
pixmap_combo_select_pixmap (PixmapCombo *pc, int index)
{
	g_return_if_fail (pc != NULL);
	g_return_if_fail (IS_PIXMAP_COMBO (pc));
	g_return_if_fail (0 <= index);
	g_return_if_fail (index < pc->num_elements);

	pc->last_index = index;
	gtk_pixmap_set (pc->preview_pixmap,
			pc->pixmaps[index]->pixmap,
			pc->pixmaps[index]->mask);
}
