/*
 * gnm-cell-combo.c: Base class the models of in cell combos (e.g. validation and sheetslicer)
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
#include <gnm-cell-combo.h>
#include <sheet-view.h>

#include <gsf/gsf-impl-utils.h>

enum {
	PROP_0,
	PROP_SV,
};

static GObjectClass *gcc_parent_klass;

static void
gnm_cell_combo_set_sv (GnmCellCombo *ccombo, SheetView *sv)
{
	if (ccombo->sv == sv)
		return;

	if (NULL != ccombo->sv)
		gnm_sheet_view_weak_unref (&ccombo->sv);

	ccombo->sv = sv;
	if (sv)
		gnm_sheet_view_weak_ref (sv, &ccombo->sv);
}

static void
gnm_cell_combo_finalize (GObject *object)
{
	GnmCellCombo *ccombo = GNM_CELL_COMBO (object);
	gnm_cell_combo_set_sv (ccombo, NULL);
	gcc_parent_klass->finalize (object);
}

static void
gnm_cell_combo_dispose (GObject *object)
{
	GnmCellCombo *ccombo = GNM_CELL_COMBO (object);
	gnm_cell_combo_set_sv (ccombo, NULL);
	gcc_parent_klass->dispose (object);
}

static void
gnm_cell_combo_set_property (GObject *obj, guint property_id,
			     GValue const *value, GParamSpec *pspec)
{
	GnmCellCombo *ccombo = (GnmCellCombo *)obj;

	switch (property_id) {
	case PROP_SV: {
		SheetView *sv = g_value_get_object (value);
		gnm_cell_combo_set_sv (ccombo, sv);
		break;
	}

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
	}
}

static void
gnm_cell_combo_get_property (GObject *obj, guint property_id,
			     GValue *value, GParamSpec *pspec)
{
	GnmCellCombo const *ccombo = (GnmCellCombo const *)obj;

	switch (property_id) {
	case PROP_SV:
		g_value_set_object (value, ccombo->sv);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, property_id, pspec);
	}
}

static void
gnm_cell_combo_init (SheetObject *so)
{
	/* keep the arrows from wandering with their cells */
	so->flags &= ~SHEET_OBJECT_MOVE_WITH_CELLS;
}

static void
gnm_cell_combo_class_init (GObjectClass *gobject_class)
{
	SheetObjectClass *so_class = GNM_SO_CLASS (gobject_class);

	gcc_parent_klass = g_type_class_peek_parent (gobject_class);

	gobject_class->dispose		= gnm_cell_combo_dispose;
	gobject_class->finalize		= gnm_cell_combo_finalize;
	gobject_class->get_property	= gnm_cell_combo_get_property;
	gobject_class->set_property	= gnm_cell_combo_set_property;
	so_class->write_xml_sax		= NULL;
	so_class->prep_sax_parser	= NULL;
	so_class->copy			= NULL;

	g_object_class_install_property (gobject_class, PROP_SV,
		 g_param_spec_object ("sheet-view", NULL, NULL,
			GNM_SHEET_VIEW_TYPE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
}

GSF_CLASS_ABSTRACT (GnmCellCombo, gnm_cell_combo,
		    gnm_cell_combo_class_init, gnm_cell_combo_init,
		    GNM_SO_TYPE)
