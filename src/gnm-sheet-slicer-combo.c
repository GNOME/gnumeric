/*
 * gnm-sheet-slicer-combo.c: Model for in cell combo for data slicers
 *
 * Copyright (C) Jody Goldberg <jody@gnome.org>
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <gnm-sheet-slicer-combo.h>
#include <go-data-slicer-field.h>
#include <widgets/gnm-cell-combo-view.h>
#include <widgets/gnm-sheet-slicer-combo-view.h>

#include <gsf/gsf-impl-utils.h>

enum {
	PROP_0,
	PROP_FIELD
};

static GObjectClass *gssc_parent_klass;

static void
gnm_sheet_slicer_combo_finalize (GObject *object)
{
#if 0
	GnmSheetSlicerCombo *sscombo = GNM_SHEET_SLICER_COMBO (object);
#endif

	gssc_parent_klass->finalize (object);
}

static void
gnm_sheet_slicer_combo_init (SheetObject *so)
{
}

static SheetObjectView *
gnm_sheet_slicer_combo_foo_view_new (SheetObject *so, SheetObjectViewContainer *container)
{
	return gnm_cell_combo_view_new (so,
		gnm_sheet_slicer_combo_view_get_type (), container);
}

static void
gnm_sheet_slicer_combo_set_property (GObject *obj, guint property_id,
				     GValue const *value, GParamSpec *pspec)
{
	GnmSheetSlicerCombo *sscombo = (GnmSheetSlicerCombo *)obj;

	switch (property_id) {
	case PROP_FIELD:
		sscombo->dsf = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
	}
}

static void
gnm_sheet_slicer_combo_get_property (GObject *obj, guint property_id,
				     GValue *value, GParamSpec *pspec)
{
	GnmSheetSlicerCombo const *sscombo = (GnmSheetSlicerCombo const *)obj;
	switch (property_id) {
	case PROP_FIELD:
		g_value_set_object (value, (GObject *) (sscombo->dsf));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
	}
}

static void
gnm_sheet_slicer_combo_class_init (GObjectClass *gobject_class)
{
	SheetObjectClass *so_class = GNM_SO_CLASS (gobject_class);

	gssc_parent_klass = g_type_class_peek_parent (gobject_class);

	gobject_class->set_property	= gnm_sheet_slicer_combo_set_property;
	gobject_class->get_property	= gnm_sheet_slicer_combo_get_property;
	gobject_class->finalize		= gnm_sheet_slicer_combo_finalize;
	so_class->new_view		= gnm_sheet_slicer_combo_foo_view_new;

	g_object_class_install_property (gobject_class, PROP_FIELD,
		 g_param_spec_object ("field", NULL, NULL, GO_DATA_SLICER_FIELD_TYPE,
			GSF_PARAM_STATIC | G_PARAM_READWRITE));
}

typedef GnmCellComboClass GnmSheetSlicerComboClass;
GSF_CLASS (GnmSheetSlicerCombo, gnm_sheet_slicer_combo,
	   gnm_sheet_slicer_combo_class_init, gnm_sheet_slicer_combo_init,
	   gnm_cell_combo_get_type ())
