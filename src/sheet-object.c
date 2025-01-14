/*
 * sheet-object.c: Implements the sheet object manipulation for Gnumeric
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 *   Michael Meeks   (mmeeks@gnu.org)
 *   Jody Goldberg   (jody@gnome.org)
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <sheet-object.h>

#include <sheet.h>
#include <dependent.h>
#include <sheet-view.h>
#include <sheet-control.h>
#include <sheet-control-gui.h>
#include <sheet-private.h>
#include <dialogs/dialogs.h>
#include <sheet-object-impl.h>
#include <expr.h>
#include <ranges.h>
#include <commands.h>
#include <gui-util.h>

#include <gnm-pane-impl.h>
#include <gnm-so-line.h>
#include <gnm-so-filled.h>
#include <sheet-control-gui-priv.h>
#include <sheet-object-cell-comment.h>
#include <sheet-object-widget.h>
#include <sheet-object-graph.h>
#include <sheet-object-image.h>
#include <sheet-filter-combo.h>
#include <wbc-gtk-impl.h>
#include <graph.h>
#include <print.h>
#include <goffice/goffice.h>
#include <application.h>
#include <gutils.h>

#include <libxml/globals.h>
#include <gsf/gsf-impl-utils.h>

#include <string.h>

static gboolean debug_sheet_objects;

static guint so_create_view_src;
static GPtrArray *so_create_view_sos;

/* GType code for SheetObjectAnchor */
static SheetObjectAnchor *
sheet_object_anchor_copy (SheetObjectAnchor * soa)
{
	SheetObjectAnchor *res = g_new (SheetObjectAnchor, 1);
	*res = *soa;
	return res;
}

GType
sheet_object_anchor_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("SheetObjectAnchor",
			 (GBoxedCopyFunc)sheet_object_anchor_copy,
			 (GBoxedFreeFunc)g_free);
	}
	return t;
}

GType
gnm_sheet_object_anchor_mode_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
	  static GEnumValue const values[] = {
		  { GNM_SO_ANCHOR_TWO_CELLS, "GNM_SO_ANCHOR_TWO_CELLS", "two-cells" },
		  { GNM_SO_ANCHOR_ONE_CELL, "GNM_SO_ANCHOR_ONE_CELL", "one-cell" },
		  { GNM_SO_ANCHOR_ABSOLUTE, "GNM_SO_ANCHOR_ABSOLUTE", "absolute" },
		  { 0, NULL, NULL }
	  };
	  etype = g_enum_register_static ("GnmSOAnchorMode", values);
  }
  return etype;
}


/* Returns the class for a SheetObject */
#define SO_CLASS(so) GNM_SO_CLASS(G_OBJECT_GET_CLASS(so))

enum {
	SO_PROP_0 = 0,
	SO_PROP_NAME
};

enum {
	BOUNDS_CHANGED,
	UNREALIZED,
	LAST_SIGNAL
};
static guint	     signals[LAST_SIGNAL] = { 0 };
static GObjectClass *parent_klass;
static GQuark	sov_so_quark;
static GQuark	sov_container_quark;

void
sheet_object_set_print_flag (SheetObject *so, gboolean *print)
{
	g_return_if_fail (GNM_IS_SO (so));

	if (*print)
		so->flags |= SHEET_OBJECT_PRINT;
	else
		so->flags &= ~SHEET_OBJECT_PRINT;
}

gboolean
sheet_object_get_print_flag (SheetObject *so)
{
	return (so->flags & SHEET_OBJECT_PRINT) != 0;
}


static void
cb_so_size_position (SheetObject *so, SheetControl *sc)
{
	WBCGtk *wbcg;

	g_return_if_fail (GNM_IS_SCG (sc));

	wbcg = scg_wbcg ((SheetControlGUI *)sc);

	if (wbcg->edit_line.guru != NULL) {
		GtkWidget *w = wbcg->edit_line.guru;
		wbc_gtk_detach_guru (wbcg);
		gtk_widget_destroy (w);
	}

	dialog_so_size (wbcg, G_OBJECT (so));
}

static void
cb_so_snap_to_grid (SheetObject *so, SheetControl *sc)
{
	SheetObjectAnchor *snapped =
		sheet_object_anchor_dup	(sheet_object_get_anchor (so));
	GnmSOAnchorMode mode = snapped->mode;
	snapped->mode = GNM_SO_ANCHOR_TWO_CELLS;
	snapped->offset[0] = snapped->offset[1] = 0.;
	snapped->offset[2] = snapped->offset[3] = 1.;
	if (mode != GNM_SO_ANCHOR_TWO_CELLS) {
		double pts[4];
		sheet_object_anchor_to_pts (snapped, so->sheet, pts);
		snapped->mode = mode;
		sheet_object_pts_to_anchor (snapped, so->sheet, pts);
	}
	cmd_objects_move (sc_wbc (sc),
		g_slist_prepend (NULL, so),
		g_slist_prepend (NULL, snapped),
		FALSE, _("Snap object to grid"));
}
static void
cb_so_pull_to_front (SheetObject *so, SheetControl *sc)
{
	cmd_object_raise (sc_wbc (sc), so, cmd_object_pull_to_front);
}
static void
cb_so_pull_forward (SheetObject *so, SheetControl *sc)
{
	cmd_object_raise (sc_wbc (sc), so, cmd_object_pull_forward);
}
static void
cb_so_push_backward (SheetObject *so, SheetControl *sc)
{
	cmd_object_raise (sc_wbc (sc), so, cmd_object_push_backward);
}
static void
cb_so_push_to_back (SheetObject *so, SheetControl *sc)
{
	cmd_object_raise (sc_wbc (sc), so, cmd_object_push_to_back);
}
static void
cb_so_delete (SheetObject *so, SheetControl *sc)
{
	cmd_objects_delete (sc_wbc (sc),
		go_slist_create (so, NULL), NULL);
}
static void
cb_so_print (SheetObject *so, SheetControl *sc)
{
	GPtrArray *a = g_ptr_array_new ();
	g_ptr_array_add (a, so);
	gnm_print_so (sc_wbc (sc), a, NULL);
	g_ptr_array_unref (a);
}
void
sheet_object_get_editor (SheetObject *so, SheetControl *sc)
{
	WBCGtk *wbcg;

	g_return_if_fail (GNM_IS_SO (so));
	g_return_if_fail (SO_CLASS (so));
	g_return_if_fail (GNM_IS_SCG (sc));

	wbcg = scg_wbcg ((SheetControlGUI *)sc);

	if (wbcg->edit_line.guru != NULL) {
		GtkWidget *w = wbcg->edit_line.guru;
		wbc_gtk_detach_guru (wbcg);
		gtk_widget_destroy (w);
	}

	if (SO_CLASS(so)->user_config)
		SO_CLASS(so)->user_config (so, sc);
}
static void
cb_so_cut (SheetObject *so, SheetControl *sc)
{
	gnm_app_clipboard_cut_copy_obj (sc_wbc (sc), TRUE, sc_view (sc),
		go_slist_create (so, NULL));
}
static void
cb_so_copy (SheetObject *so, SheetControl *sc)
{
	gnm_app_clipboard_cut_copy_obj (sc_wbc (sc), FALSE, sc_view (sc),
		go_slist_create (so, NULL));
}

gboolean
sheet_object_can_print (SheetObject const *so)
{
	g_return_val_if_fail (GNM_IS_SO (so), FALSE);
	return  (so->flags & SHEET_OBJECT_IS_VISIBLE) &&
		(so->flags & SHEET_OBJECT_PRINT) &&
		SO_CLASS (so)->draw_cairo != NULL;
}

gboolean
sheet_object_can_resize (SheetObject const *so)
{
	g_return_val_if_fail (GNM_IS_SO (so), FALSE);
	return  so->flags & SHEET_OBJECT_CAN_RESIZE;
}

gboolean
sheet_object_can_edit (SheetObject const *so)
{
	g_return_val_if_fail (GNM_IS_SO (so), FALSE);
	return  so->flags & SHEET_OBJECT_CAN_EDIT;
}

static gboolean
sheet_object_can_prop (SheetObject const *so)
{
	g_return_val_if_fail (GNM_IS_SO (so), FALSE);
	return (sheet_object_can_edit (so) && (SO_CLASS(so)->user_config != NULL));
}

static void
sheet_object_populate_menu_real (SheetObject *so, GPtrArray *actions)
{
	unsigned i;
	if (so->sheet->sheet_type == GNM_SHEET_OBJECT) {
		static SheetObjectAction const so_actions[] = {
			{ "gtk-properties",	NULL,		NULL,  0, sheet_object_get_editor, sheet_object_can_prop},
			{ NULL,	NULL, NULL, 0, NULL, NULL },
			{ "edit-copy",		N_("_Copy"),		NULL,  0, cb_so_copy, NULL },
		};
		for (i = 0 ; i < G_N_ELEMENTS (so_actions); i++)
			g_ptr_array_add (actions, (gpointer) (so_actions + i));
	} else {
		static SheetObjectAction const so_actions[] = {
			{ GTK_STOCK_PROPERTIES,	        NULL, NULL,  0, sheet_object_get_editor, sheet_object_can_prop},
			{ NULL,	NULL, NULL, 0, NULL, NULL },
#warning "Two highly dubious icon names here"
			{ GTK_STOCK_LEAVE_FULLSCREEN,   N_("Size _& Position"),	NULL,  0, cb_so_size_position, NULL },
			{ GTK_STOCK_FULLSCREEN,	        N_("_Snap to Grid"),	NULL,  0, cb_so_snap_to_grid, NULL },
			{ NULL,			        N_("_Order"),	        NULL,  1, NULL, NULL },
				{ NULL,			N_("Pul_l to Front"),	NULL,  0, cb_so_pull_to_front, NULL },
				{ NULL,			N_("Pull _Forward"),	NULL,  0, cb_so_pull_forward, NULL },
				{ NULL,			N_("Push _Backward"),	NULL,  0, cb_so_push_backward, NULL },
				{ NULL,			N_("Pus_h to Back"),	NULL,  0, cb_so_push_to_back, NULL },
				{ NULL,			NULL,			NULL, -1, NULL, NULL },
			{ NULL,	NULL, NULL, 0, NULL, NULL },
			{ "edit-cut",			N_("Cu_t"),		NULL,  0, cb_so_cut, NULL },
			{ "edit-copy",			N_("_Copy"),		NULL,  0, cb_so_copy, NULL },
			{ "edit-delete",		N_("_Delete"),		NULL,  0, cb_so_delete, NULL },
			{ NULL,	NULL, NULL, 0, NULL, NULL },
			{ "document-print",		N_("Print"),		NULL,  0, cb_so_print, sheet_object_can_print},
		};
		for (i =  0; i < G_N_ELEMENTS (so_actions); i++)
				g_ptr_array_add (actions, (gpointer) (so_actions + i));
	}
}

/**
 * sheet_object_populate_menu:
 * @so: #SheetObject
 * @actions: (inout) (transfer full) (element-type SheetObjectAction): #GPtrArray
 *
 * Get a list of the actions that can be performed on @so
 **/
void
sheet_object_populate_menu (SheetObject *so, GPtrArray *actions)
{
	g_return_if_fail (NULL != so);

	GNM_SO_CLASS (G_OBJECT_GET_CLASS(so))->populate_menu (so, actions);
}

void
sheet_object_set_name (SheetObject *so, const char *name)
{
	if (name == so->name)
		return;

	g_free (so->name);
	so->name = g_strdup (name);

	g_object_notify (G_OBJECT (so), "name");
}

static void
sheet_object_get_property (GObject *obj, guint param_id,
			   GValue  *value, GParamSpec *pspec)
{
	SheetObject *so = GNM_SO (obj);

	switch (param_id) {
	case SO_PROP_NAME:
		g_value_set_string (value, so->name);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		break;
	}
}

static void
sheet_object_set_property (GObject *obj, guint param_id,
			   GValue const *value, GParamSpec *pspec)
{
	SheetObject *so = GNM_SO (obj);

	switch (param_id) {
	case SO_PROP_NAME:
		sheet_object_set_name (so, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		return;
	}
}


static void
sheet_object_finalize (GObject *object)
{
	SheetObject *so = GNM_SO (object);
	if (so->sheet != NULL)
		sheet_object_clear_sheet (so);
	g_free (so->name);
	parent_klass->finalize (object);
}

static void
sheet_object_init (GObject *object)
{
	int i;
	SheetObject *so = GNM_SO (object);

	so->sheet = NULL;
	so->flags = SHEET_OBJECT_IS_VISIBLE | SHEET_OBJECT_PRINT |
		SHEET_OBJECT_CAN_RESIZE | SHEET_OBJECT_CAN_EDIT |
		SHEET_OBJECT_MOVE_WITH_CELLS | SHEET_OBJECT_SIZE_WITH_CELLS;

	/* Store the logical position as A1 */
	so->anchor.cell_bound.start.col = so->anchor.cell_bound.start.row = 0;
	so->anchor.cell_bound.end.col = so->anchor.cell_bound.end.row = 1;
	so->anchor.base.direction = GOD_ANCHOR_DIR_UNKNOWN;

	for (i = 4; i-- > 0 ;)
		so->anchor.offset[i] = 0.;
}

static void
so_default_size (G_GNUC_UNUSED SheetObject const *so, double *width, double *height)
{
	/* Provide some defaults (derived classes may want to override) */
	*width  = 72.;
	*height = 72.;
}

static void
sheet_object_class_init (GObjectClass *klass)
{
	SheetObjectClass *sheet_object_class = GNM_SO_CLASS (klass);

	parent_klass = g_type_class_peek_parent (klass);

	klass->finalize = sheet_object_finalize;
	klass->get_property = sheet_object_get_property;
	klass->set_property = sheet_object_set_property;

	sheet_object_class->populate_menu        = sheet_object_populate_menu_real;
	sheet_object_class->user_config          = NULL;
	sheet_object_class->rubber_band_directly = FALSE;
	sheet_object_class->interactive          = FALSE;
	sheet_object_class->default_size	 = so_default_size;
	sheet_object_class->xml_export_name	 = NULL;
	sheet_object_class->foreach_dep          = NULL;

	g_object_class_install_property
		(klass, SO_PROP_NAME,
		 g_param_spec_string ("name", NULL, NULL, NULL,
				      GSF_PARAM_STATIC | G_PARAM_READWRITE));

	signals[BOUNDS_CHANGED] = g_signal_new ("bounds-changed",
		GNM_SO_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (SheetObjectClass, bounds_changed),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
	signals[UNREALIZED] = g_signal_new ("unrealized",
		GNM_SO_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (SheetObjectClass, unrealized),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
}

GSF_CLASS (SheetObject, sheet_object,
	   sheet_object_class_init, sheet_object_init,
	   G_TYPE_OBJECT)

/**
 * sheet_object_get_view:
 * @so: #SheetObject
 * @container: #SheetObjectViewContainer
 *
 * Returns: (transfer none) (nullable): the found #SheetObjectView or %NULL.
 **/
SheetObjectView *
sheet_object_get_view (SheetObject const *so, SheetObjectViewContainer *container)
{
	GList *l;

	g_return_val_if_fail (GNM_IS_SO (so), NULL);

	for (l = so->realized_list; l; l = l->next) {
		SheetObjectView *view = GNM_SO_VIEW (l->data);
		if (container == g_object_get_qdata (G_OBJECT (view), sov_container_quark))
			return view;
	}

	return NULL;
}

/**
 * sheet_object_update_bounds:
 * @so: The sheet object
 * @p: (nullable): A position marking the top left of the region
 *        needing relocation (default == A1)
 *
 * update the bounds of an object that intersects the region whose top left
 * is @p.  This is used when an objects position is anchored to cols/rows
 * and they change position.
 **/
void
sheet_object_update_bounds (SheetObject *so, GnmCellPos const *pos)
{
	gboolean is_hidden = TRUE;
	int i, end;

	g_return_if_fail (GNM_IS_SO (so));

	if (pos != NULL &&
	    so->anchor.cell_bound.end.col < pos->col &&
	    so->anchor.cell_bound.end.row < pos->row)
		return;

	if (so->anchor.mode != GNM_SO_ANCHOR_TWO_CELLS) {
		double x[4];
		sheet_object_anchor_to_pts (&so->anchor, so->sheet, x);
		sheet_object_pts_to_anchor (&so->anchor, so->sheet, x);
	}

	switch (so->anchor.mode) {
	default:
	case GNM_SO_ANCHOR_TWO_CELLS:
		/* Are all cols hidden ? */
		end = so->anchor.cell_bound.end.col;
		i = so->anchor.cell_bound.start.col;
		while (i <= end && is_hidden)
			is_hidden &= sheet_col_is_hidden (so->sheet, i++);

		/* Are all rows hidden ? */
		if (!is_hidden) {
			is_hidden = TRUE;
			end = so->anchor.cell_bound.end.row;
			i = so->anchor.cell_bound.start.row;
			while (i <= end && is_hidden)
				is_hidden &= sheet_row_is_hidden (so->sheet, i++);
		}
		break;
	case GNM_SO_ANCHOR_ONE_CELL:
		/* Should we really hide if the row or column is hidden? */
		is_hidden = sheet_col_is_hidden (so->sheet, so->anchor.cell_bound.start.col) ||
				sheet_row_is_hidden (so->sheet, so->anchor.cell_bound.start.row);
		break;
	case GNM_SO_ANCHOR_ABSOLUTE:
		is_hidden = FALSE;
		break;
	}
	if (is_hidden)
		so->flags &= ~SHEET_OBJECT_IS_VISIBLE;
	else
		so->flags |= SHEET_OBJECT_IS_VISIBLE;

	g_signal_emit (so, signals[BOUNDS_CHANGED], 0);
}

/**
 * sheet_object_get_sheet:
 * @so: #SheetObject
 *
 * A small utility to help keep the implementation of SheetObjects modular.
 * Returns: (transfer none): the #Sheet owning the object.
 **/
Sheet *
sheet_object_get_sheet (SheetObject const *so)
{
	g_return_val_if_fail (GNM_IS_SO (so), NULL);

	return so->sheet;
}

static void
cb_create_views_helper (GPtrArray *sos, gboolean qfreeze)
{
	Sheet const *last_sheet = NULL;
	unsigned ui, l = sos->len;
	for (ui = 0; ui < l; ui++) {
		SheetObject *so = g_ptr_array_index (sos, ui);
		if (so->sheet == last_sheet) {
			// Avoid excessive duplicate freezes.  This is
			// not essential, but helps with debugging.
			continue;
		}
		last_sheet = so->sheet;
		sheet_freeze_object_views (so->sheet, qfreeze);
	}
}

static gboolean
cb_create_views (void)
{
	unsigned ui, l = so_create_view_sos->len;

	cb_create_views_helper (so_create_view_sos, TRUE);

	for (ui = 0; ui < l; ui++) {
		SheetObject *so = g_ptr_array_index (so_create_view_sos, ui);
		SHEET_FOREACH_CONTROL
			(so->sheet, view, control,
			 sc_object_create_view (control, so););
	}

	cb_create_views_helper (so_create_view_sos, FALSE);

	g_ptr_array_set_size (so_create_view_sos, 0);

	so_create_view_src = 0;
	return FALSE;
}

/**
 * sheet_object_set_sheet:
 * @so:
 * @sheet:
 *
 * Adds a reference to the object.
 **/
void
sheet_object_set_sheet (SheetObject *so, Sheet *sheet)
{
	g_return_if_fail (GNM_IS_SO (so));
	g_return_if_fail (IS_SHEET (sheet));

	if (sheet == so->sheet)
		return;

	g_return_if_fail (so->sheet == NULL);
	if (debug_sheet_objects)
		g_return_if_fail (g_slist_find (sheet->sheet_objects, so) == NULL);

	so->sheet = sheet;
	if (SO_CLASS (so)->assign_to_sheet &&
	    SO_CLASS (so)->assign_to_sheet (so, sheet)) {
		so->sheet = NULL;
		return;
	}

	g_object_ref (so);
	sheet->sheet_objects = g_slist_prepend (sheet->sheet_objects, so);
	/* Update object bounds for absolute and one cell anchored objects */
	if (so->anchor.mode != GNM_SO_ANCHOR_TWO_CELLS) {
		double x[4];
		sheet_object_anchor_to_pts (&so->anchor, sheet, x);
		sheet_object_pts_to_anchor (&so->anchor, sheet, x);
	}

	sheet->priv->objects_changed = TRUE;

	g_ptr_array_add (so_create_view_sos, so);
	if (!so_create_view_src) {
		so_create_view_src =
			g_timeout_add_full (
				G_PRIORITY_DEFAULT_IDLE,
				0,
				(GSourceFunc)cb_create_views,
				NULL,
				NULL);
	}
}

/**
 * sheet_object_clear_sheet:
 * @so: #SheetObject
 *
 * Removes @so from its container, unrealizes all views, disconnects the
 * associated data and unrefs the object
 **/
void
sheet_object_clear_sheet (SheetObject *so)
{
	GSList *ptr;
	unsigned ui;

	g_return_if_fail (GNM_IS_SO (so));

	if (so->sheet == NULL) /* already removed */
		return;

	g_return_if_fail (IS_SHEET (so->sheet));

	ptr = g_slist_find (so->sheet->sheet_objects, so);
	g_return_if_fail (ptr != NULL);

	/* clear any pending attempts to create views */
	for (ui = 0; ui < so_create_view_sos->len; ui++) {
		if (so == g_ptr_array_index (so_create_view_sos, ui)) {
			g_ptr_array_remove_index (so_create_view_sos, ui);
			break;
		}
	}

	while (so->realized_list != NULL) {
		g_object_set_qdata (G_OBJECT (so->realized_list->data), sov_so_quark, NULL);
		g_object_unref (so->realized_list->data);
		so->realized_list = g_list_remove (so->realized_list, so->realized_list->data);

	}
	g_signal_emit (so, signals[UNREALIZED], 0);

	if (SO_CLASS (so)->remove_from_sheet &&
	    SO_CLASS (so)->remove_from_sheet (so))
		return;

	so->sheet->sheet_objects = g_slist_remove_link (so->sheet->sheet_objects, ptr);
	g_slist_free (ptr);

	if (so->anchor.cell_bound.end.col == so->sheet->max_object_extent.col ||
	    so->anchor.cell_bound.end.row == so->sheet->max_object_extent.row)
		so->sheet->priv->objects_changed = TRUE;

	so->sheet = NULL;
	g_object_unref (so);
}

static void
cb_sheet_object_invalidate_sheet (GnmDependent *dep, G_GNUC_UNUSED SheetObject *so, gpointer user)
{
	Sheet *sheet = user;
	GnmExprRelocateInfo rinfo;
	GnmExprTop const *texpr;
	gboolean save_invalidated = sheet->being_invalidated;
	gboolean dep_sheet_invalidated = (dep->sheet == sheet);

	if (!dep->texpr)
		return;

	sheet->being_invalidated = TRUE;
	rinfo.reloc_type = GNM_EXPR_RELOCATE_INVALIDATE_SHEET;
	texpr = gnm_expr_top_relocate (dep->texpr, &rinfo, FALSE);
	if (!texpr && dep_sheet_invalidated) {
		texpr = dep->texpr;
		gnm_expr_top_ref (texpr);
	}

	sheet->being_invalidated = save_invalidated;

	if (texpr) {
		gboolean was_linked = dependent_is_linked (dep);
		dependent_set_expr (dep, texpr);
		gnm_expr_top_unref (texpr);
		if (dep_sheet_invalidated)
			dep->sheet = NULL;
		else if (was_linked)
			dependent_link (dep);
	}
}

void
sheet_object_invalidate_sheet (SheetObject *so, Sheet const *sheet)
{
	sheet_object_foreach_dep (so, cb_sheet_object_invalidate_sheet,
				  (gpointer)sheet);
}

/**
 * sheet_object_foreach_dep:
 * @so: #SheetObject
 * @func: (scope call): #SheetObjectForeachDepFunc
 * @user: user data
 *
 * Loops over each dependent contained in a sheet object and call the handler.
 **/
void
sheet_object_foreach_dep (SheetObject *so,
			  SheetObjectForeachDepFunc func,
			  gpointer user)
{
	if (SO_CLASS (so)->foreach_dep)
		SO_CLASS (so)->foreach_dep (so, func, user);
}

/**
 * sheet_object_new_view:
 * @so:
 * @container:
 *
 * Asks @so to create a view for @container.
 * Returns: (transfer none): the new #SheetObjectView.
 **/
SheetObjectView *
sheet_object_new_view (SheetObject *so, SheetObjectViewContainer *container)
{
	SheetObjectView *view;

	g_return_val_if_fail (GNM_IS_SO (so), NULL);
	g_return_val_if_fail (NULL != container, NULL);

	view = sheet_object_get_view (so, container);
	if (view != NULL)
		return NULL;

	view = SO_CLASS (so)->new_view (so, container);

	if (NULL == view)
		return NULL;

	g_return_val_if_fail (GNM_IS_SO_VIEW (view), NULL);

	/* Store some useful information */
	g_object_set_qdata (G_OBJECT (view), sov_so_quark, so);
	g_object_set_qdata (G_OBJECT (view), sov_container_quark, container);
	so->realized_list = g_list_prepend (so->realized_list, view);
	sheet_object_update_bounds (so, NULL);

	return view;
}

/**
 * sheet_object_draw_cairo:
 *
 * Draw a sheet object using cairo.
 *
 *
 * We are assuming that the cairo context is set up so that the top
 * left of the bounds is (0,0). Note that this
 * is the real top left cell, not necessarily the cell with to which we are
 * anchored.
 *
 **/
void
sheet_object_draw_cairo (SheetObject const *so, cairo_t *cr, gboolean rtl)
{
	if (SO_CLASS (so)->draw_cairo) {
		SheetObjectAnchor const *anchor;
		double x = 0., y = 0., width, height, cell_width, cell_height;
		anchor = sheet_object_get_anchor (so);
		if (anchor->mode == GNM_SO_ANCHOR_ABSOLUTE) {
			x = anchor->offset[0];
			y = anchor->offset[1];
			if (sheet_object_can_resize (so)) {
				width = anchor->offset[2];
				height = anchor->offset[3];
			} else
				sheet_object_default_size ((SheetObject *) so, &width, &height);
			if (rtl)
				x = -x - width;
		} else {
			cell_width = sheet_col_get_distance_pts (so->sheet,
						anchor->cell_bound.start.col,
						anchor->cell_bound.start.col + 1);
			cell_height = sheet_row_get_distance_pts (so->sheet,
						anchor->cell_bound.start.row,
						anchor->cell_bound.start.row + 1);
			x = cell_width * anchor->offset[0];

			y = cell_height * anchor->offset[1];
			if (anchor->mode == GNM_SO_ANCHOR_TWO_CELLS) {
				cell_width = sheet_col_get_distance_pts (so->sheet,
							anchor->cell_bound.end.col,
							anchor->cell_bound.end.col + 1);
				cell_height = sheet_row_get_distance_pts (so->sheet,
							anchor->cell_bound.end.row,
							anchor->cell_bound.end.row + 1);

				if (rtl)
					x = cell_width  * (1 - anchor->offset[2]);

				if (sheet_object_can_resize (so)) {
					width = sheet_col_get_distance_pts (so->sheet,
								anchor->cell_bound.start.col,
								anchor->cell_bound.end.col + 1);
					height = sheet_row_get_distance_pts (so->sheet,
								anchor->cell_bound.start.row,
								anchor->cell_bound.end.row + 1);
					width -= x;
					height -= y;
					width -= cell_width * (1. - ((rtl)? anchor->offset[0]: anchor->offset[2]));
					height -= cell_height * (1 - anchor->offset[3]);
				} else
					sheet_object_default_size ((SheetObject *) so, &width, &height);
			} else {
				if (sheet_object_can_resize (so)) {
					width = anchor->offset[2];
					height = anchor->offset[3];
				} else
					sheet_object_default_size ((SheetObject *) so, &width, &height);
				if (rtl)
					x = cell_width  * (1 - anchor->offset[0]) - width;
			}
		}

		/* we don't need to save/restore cairo, the caller must do it */
		cairo_translate (cr, x, y);
		SO_CLASS (so)->draw_cairo (so, cr, width, height);
	}
}

void
sheet_object_draw_cairo_sized (SheetObject const *so, cairo_t *cr, double width, double height)
{
	SO_CLASS (so)->draw_cairo (so, cr, width, height);
}

/**
 * sheet_object_get_range:
 * @so: the #SheetObject to query
 *
 * Returns: (transfer none): the #GnmRange used for @so.
 */
GnmRange const *
sheet_object_get_range (SheetObject const *so)
{
	g_return_val_if_fail (GNM_IS_SO (so), NULL);

	return &so->anchor.cell_bound;
}

/**
 * sheet_object_get_anchor:
 * @so: #SheetObject
 *
 * Returns: (transfer none): the #SheetObjectAnchor for @so.
 **/
SheetObjectAnchor const *
sheet_object_get_anchor (SheetObject const *so)
{
	g_return_val_if_fail (GNM_IS_SO (so), NULL);

	return &so->anchor;
}

void
sheet_object_set_anchor (SheetObject *so, SheetObjectAnchor const *anchor)
{
	g_return_if_fail (GNM_IS_SO (so));

	so->anchor = *anchor;
	if (so->sheet != NULL) {
		so->sheet->priv->objects_changed = TRUE;
		sheet_object_update_bounds (so, NULL);
	}
}

SheetObjectAnchor *
sheet_object_anchor_dup	(SheetObjectAnchor const *src)
{
	SheetObjectAnchor *res = go_memdup (src, sizeof (SheetObjectAnchor));
	return res;
}

static double
cell_offset_calc_pt (Sheet const *sheet, int i, gboolean is_col, double offset)
{
	ColRowInfo const *cri = sheet_colrow_get_info (sheet, i, is_col);
	return offset * cri->size_pts;
}

/**
 * sheet_object_default_size:
 * @so: The sheet object
 * @w: (out): a ptr into which to store the default_width.
 * @h: (out): a ptr into which to store the default_height.
 *
 * Measurements are in pts.
 **/
void
sheet_object_default_size (SheetObject *so, double *w, double *h)
{
	g_return_if_fail (GNM_IS_SO (so));
	g_return_if_fail (w != NULL);
	g_return_if_fail (h != NULL);

	SO_CLASS (so)->default_size (so, w, h);
}

/**
 * sheet_object_position_pts_get:
 * @so: The sheet object
 * @coords: (out) (array fixed-size=4): coordinates
 *
 * Calculate the position of the object @so in pts from the logical position in
 * the object.
 **/
void
sheet_object_position_pts_get (SheetObject const *so, double *coords)
{
	g_return_if_fail (GNM_IS_SO (so));
	sheet_object_anchor_to_pts (&so->anchor, so->sheet, coords);
}

void
sheet_object_anchor_to_pts (SheetObjectAnchor const *anchor,
			    Sheet const *sheet, double *res_pts)
{
	GnmRange const *r;

	g_return_if_fail (res_pts != NULL);

	r = &anchor->cell_bound;

	if (anchor->mode != GNM_SO_ANCHOR_ABSOLUTE) {
		res_pts[0] = sheet_col_get_distance_pts (sheet, 0,
			r->start.col);
		res_pts[1] = sheet_row_get_distance_pts (sheet, 0,
			r->start.row);
		if (anchor->mode == GNM_SO_ANCHOR_TWO_CELLS) {
			res_pts[2] = res_pts[0] + sheet_col_get_distance_pts (sheet,
				r->start.col, r->end.col);
			res_pts[3] = res_pts[1] + sheet_row_get_distance_pts (sheet,
				r->start.row, r->end.row);

			res_pts[0] += cell_offset_calc_pt (sheet, r->start.col,
				TRUE, anchor->offset[0]);
			res_pts[1] += cell_offset_calc_pt (sheet, r->start.row,
				FALSE, anchor->offset[1]);
			res_pts[2] += cell_offset_calc_pt (sheet, r->end.col,
				TRUE, anchor->offset[2]);
			res_pts[3] += cell_offset_calc_pt (sheet, r->end.row,
				FALSE, anchor->offset[3]);
		} else {
			res_pts[0] += cell_offset_calc_pt (sheet, r->start.col,
				TRUE, anchor->offset[0]);
			res_pts[1] += cell_offset_calc_pt (sheet, r->start.row,
				FALSE, anchor->offset[1]);
			res_pts[2] = res_pts[0] + anchor->offset[2];
			res_pts[3] = res_pts[1] + anchor->offset[3];
		}
	} else {
		res_pts[0] = anchor->offset[0];
		res_pts[1] = anchor->offset[1];
		res_pts[2] = res_pts[0] + anchor->offset[2];
		res_pts[3] = res_pts[1] + anchor->offset[3];
	}
}

void
sheet_object_pts_to_anchor (SheetObjectAnchor *anchor,
			    Sheet const *sheet, double const *res_pts)
{
	int col, row;
	double x, y, tmp = 0;
	ColRowInfo const *ci;
	/* find end column */
	col = x = 0;
	do {
		ci = sheet_col_get_info (sheet, col);
		if (ci->visible) {
			tmp = ci->size_pts;
			if (res_pts[0] <= x + tmp)
				break;
			x += tmp;
		}
	} while (++col < gnm_sheet_get_last_col (sheet));
	if (col == gnm_sheet_get_last_col (sheet)) {
		/* not sure this will occur */
		col--;
		x -= tmp;
	}
	anchor->cell_bound.start.col = col;
	anchor->offset[0] = (anchor->mode == GNM_SO_ANCHOR_ABSOLUTE)?
		res_pts[0]: (res_pts[0] - x) / tmp;
	/* find start row */
	row = y = 0;
	do {
		ci = sheet_row_get_info (sheet, row);
		if (ci->visible) {
			tmp = ci->size_pts;
			if (res_pts[1] <= y + tmp)
				break;
			y += tmp;
		}
	} while (++row < gnm_sheet_get_last_row (sheet));
	if (row == gnm_sheet_get_last_row (sheet)) {
		/* not sure this will occur */
		row--;
		y -= tmp;
	}
	anchor->cell_bound.start.row = row;
	anchor->offset[1] = (anchor->mode == GNM_SO_ANCHOR_ABSOLUTE)?
		res_pts[1]: (res_pts[1] - y) / tmp;

	/* find end column */
	do {
		ci = sheet_col_get_info (sheet, col);
		if (ci->visible) {
			tmp = ci->size_pts;
			if (res_pts[2] <= x + tmp)
				break;
			x += tmp;
		}
	} while (++col < gnm_sheet_get_last_col (sheet));
	if (col == gnm_sheet_get_last_col (sheet)) {
		/* not sure this will occur */
		col--;
		x -= tmp;
	}
	anchor->cell_bound.end.col = col;
	anchor->offset[2] = (anchor->mode == GNM_SO_ANCHOR_TWO_CELLS)?
		(res_pts[2] - x) / tmp: res_pts[2] - res_pts[0];
	/* find end row */
	do {
		ci = sheet_row_get_info (sheet, row);
		if (ci->visible) {
			tmp = ci->size_pts;
			if (res_pts[3] <= y + tmp)
				break;
			y += tmp;
		}
	} while (++row < gnm_sheet_get_last_row (sheet));
	if (row == gnm_sheet_get_last_row (sheet)) {
		/* not sure this will occur */
		row--;
		y -= tmp;
	}
	anchor->cell_bound.end.row = row;
	anchor->offset[3] = (anchor->mode == GNM_SO_ANCHOR_TWO_CELLS)?
		(res_pts[3] - y) / tmp: res_pts[3] - res_pts[1];
}

void
sheet_object_anchor_to_offset_pts (SheetObjectAnchor const *anchor,
				   Sheet const *sheet, double *res_pts)
{
	GnmRange const *r;

	g_return_if_fail (res_pts != NULL);

	r = &anchor->cell_bound;

	if (anchor->mode != GNM_SO_ANCHOR_ABSOLUTE) {
		res_pts[0] = cell_offset_calc_pt (sheet, r->start.col,
						   TRUE, anchor->offset[0]);
		res_pts[1] = cell_offset_calc_pt (sheet, r->start.row,
						   FALSE, anchor->offset[1]);
		if (anchor->mode == GNM_SO_ANCHOR_TWO_CELLS) {
			res_pts[2] = cell_offset_calc_pt (sheet, r->end.col,
							   TRUE, anchor->offset[2]);
			res_pts[3] = cell_offset_calc_pt (sheet, r->end.row,
							   FALSE, anchor->offset[3]);
		}
	}
}

static void
clear_sheet (SheetObject *so, GOUndo **pundo)
{
	if (pundo) {
		GOUndo *u = go_undo_binary_new
			(g_object_ref (so),
			 so->sheet,
			 (GOUndoBinaryFunc)sheet_object_set_sheet,
			 (GFreeFunc) g_object_unref,
			 NULL);
		*pundo = go_undo_combine (*pundo, u);
	}

	sheet_object_clear_sheet (so);
}


/**
 * sheet_objects_relocate:
 * @rinfo: details on what should be moved.
 * @update: Should we do the bound_update now, or leave it for later.
 *		if %FALSE, honour the move_with_cells flag.
 * @pundo: (optional) (out): add dropped objects to ::objects
 *
 * Uses the relocation info and the anchors to decide whether or not, and how
 * to relocate objects when the grid moves (eg ins/del col/row).
 **/
void
sheet_objects_relocate (GnmExprRelocateInfo const *rinfo, gboolean update,
			GOUndo **pundo)
{
	GSList   *ptr, *next;
	GnmRange	 dest;
	gboolean change_sheets;

	g_return_if_fail (rinfo != NULL);
	g_return_if_fail (IS_SHEET (rinfo->origin_sheet));
	g_return_if_fail (IS_SHEET (rinfo->target_sheet));

	dest = rinfo->origin;
	range_translate (&dest, rinfo->target_sheet,
			 rinfo->col_offset, rinfo->row_offset);
	change_sheets = (rinfo->origin_sheet != rinfo->target_sheet);

	/* Clear the destination range on the target sheet */
	if (change_sheets) {
		GSList *copy = g_slist_copy (rinfo->target_sheet->sheet_objects);
		for (ptr = copy; ptr != NULL ; ptr = ptr->next ) {
			SheetObject *so = GNM_SO (ptr->data);
			GnmRange const *r  = &so->anchor.cell_bound;
			if (range_contains (&dest, r->start.col, r->start.row)) {
				clear_sheet (so, pundo);
			}
		}
		g_slist_free (copy);
	}

	ptr = rinfo->origin_sheet->sheet_objects;
	for (; ptr != NULL ; ptr = next ) {
		SheetObject *so = GNM_SO (ptr->data);
		GnmRange r = so->anchor.cell_bound;

		next = ptr->next;
		if ((so->anchor.mode == GNM_SO_ANCHOR_ABSOLUTE) ||
		    (update && 0 == (so->flags & SHEET_OBJECT_MOVE_WITH_CELLS)))
			continue;
		if (range_contains (&rinfo->origin, r.start.col, r.start.row)) {
			/* FIXME : just moving the range is insufficent for all anchor types */
			/* Toss any objects that would be clipped. */
			if (range_translate (&r, rinfo->origin_sheet,
					     rinfo->col_offset, rinfo->row_offset)) {
				clear_sheet (so, pundo);
				continue;
			}
			so->anchor.cell_bound = r;

			if (change_sheets) {
				g_object_ref (so);
				sheet_object_clear_sheet (so);
				sheet_object_set_sheet (so, rinfo->target_sheet);
				g_object_unref (so);
			} else if (update)
				sheet_object_update_bounds (so, NULL);
		} else if (!change_sheets &&
			   range_contains (&dest, r.start.col, r.start.row)) {
			clear_sheet (so, pundo);
			continue;
		}
	}

	rinfo->origin_sheet->priv->objects_changed = TRUE;
	if (change_sheets)
		rinfo->target_sheet->priv->objects_changed = TRUE;
}

/**
 * sheet_objects_get:
 * @sheet: the sheet.
 * @r: (nullable): #GnmRange to look in
 * @t: The type of object to lookup, %G_TYPE_NONE for any.
 *
 * Returns: (element-type SheetObject) (transfer container): a list
 * containing all objects of exactly the specified type (inheritance does
 * not count) that are completely contained in @r.
 **/
GSList *
sheet_objects_get (Sheet const *sheet, GnmRange const *r, GType t)
{
	GSList *res = NULL;
	GSList *ptr;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = ptr->next ) {
		GObject *obj = G_OBJECT (ptr->data);

		if (t == G_TYPE_NONE || t == G_OBJECT_TYPE (obj)) {
			SheetObject *so = GNM_SO (obj);
			if (r == NULL || range_contained (&so->anchor.cell_bound, r))
				res = g_slist_prepend (res, so);
		}
	}
	return g_slist_reverse (res);
}

/**
 * sheet_objects_clear:
 * @sheet: the sheet.
 * @r: (nullable): #GnmRange to look in
 * @t #GType
 * @pundo: (out) (nullable):
 *
 * Removes the objects in the region.
 **/
void
sheet_objects_clear (Sheet const *sheet, GnmRange const *r, GType t,
		     GOUndo **pundo)
{
	GSList *ptr, *next;
	gboolean any = FALSE;

	g_return_if_fail (IS_SHEET (sheet));

	for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = next ) {
		GObject *obj = G_OBJECT (ptr->data);
		next = ptr->next;
		if ((t == G_TYPE_NONE && G_OBJECT_TYPE (obj) != GNM_FILTER_COMBO_TYPE)
		    || t == G_OBJECT_TYPE (obj)) {
			SheetObject *so = GNM_SO (obj);
			if (r == NULL || range_contained (&so->anchor.cell_bound, r)) {
				if (!any) {
					sheet_freeze_object_views (sheet, TRUE);
					any = TRUE;
				}
				clear_sheet (so, pundo);
			}
		}
	}

	if (any)
		sheet_freeze_object_views (sheet, FALSE);
}

/**
 * sheet_object_dup:
 * @so: a #SheetObject to duplicate
 *
 * Returns: (transfer full) (nullable): A copy of @so that is not attached
 * to a sheet.
 **/
SheetObject *
sheet_object_dup (SheetObject const *so)
{
	SheetObject *new_so = NULL;

	if (!SO_CLASS (so)->copy)
		return NULL;

	new_so = g_object_new (G_OBJECT_TYPE (so), NULL);

	g_return_val_if_fail (new_so != NULL, NULL);

	SO_CLASS (so)->copy (new_so, so);
	new_so->flags = so->flags;
	new_so->anchor = so->anchor;

	return new_so;
}

static void
cb_sheet_objects_dup (GnmDependent *dep, SheetObject *so, gpointer user)
{
	Sheet *src = user;
	Sheet *dst = sheet_object_get_sheet (so);
	GnmExprTop const *texpr;

	if (!dep->texpr)
		return;

	texpr = gnm_expr_top_relocate_sheet (dep->texpr, src, dst);
	if (texpr != dep->texpr) {
		gboolean was_linked= dependent_is_linked (dep);
		dependent_set_expr (dep, texpr);
		if (was_linked)
			dependent_link (dep);
	}
	gnm_expr_top_unref (texpr);
}


/**
 * sheet_objects_dup:
 * @src: The source sheet to read the objects from
 * @dst: The destination sheet to attach the objects to
 * @range: (nullable): #GnmRange to look in
 *
 * Clones the objects of the src sheet and attaches them into the dst sheet
 **/
void
sheet_objects_dup (Sheet const *src, Sheet *dst, GnmRange const *range)
{
	GSList *list;

	g_return_if_fail (IS_SHEET (dst));
	g_return_if_fail (dst->sheet_objects == NULL);

	for (list = src->sheet_objects; list != NULL; list = list->next) {
		SheetObject *so = list->data;
		if (range == NULL || range_overlap (range, &so->anchor.cell_bound)) {
			SheetObject *new_so = sheet_object_dup (so);
			if (new_so != NULL) {
				sheet_object_set_sheet (new_so, dst);
				sheet_object_foreach_dep (new_so, cb_sheet_objects_dup,
							  (gpointer)src);
				g_object_unref (new_so);
			}
		}
	}

	dst->sheet_objects = g_slist_reverse (dst->sheet_objects);
}


/**
 * sheet_object_direction_set:
 * @so: The sheet object that we are calculating the direction for
 * @coords: (in) (array fixed-size=4): array of coordinates in L,T,R,B order
 *
 * Sets the object direction from the given the new coordinates
 * The original coordinates are assumed to be normalized (so that top
 * is above bottom and right is at the right of left)
 **/
void
sheet_object_direction_set (SheetObject *so, gdouble const *coords)
{
	if (so->anchor.base.direction == GOD_ANCHOR_DIR_UNKNOWN)
		return;

	so->anchor.base.direction = GOD_ANCHOR_DIR_NONE_MASK;

	if (coords[1] < coords[3])
		so->anchor.base.direction |= GOD_ANCHOR_DIR_DOWN;
	if (coords[0] < coords[2])
		so->anchor.base.direction |= GOD_ANCHOR_DIR_RIGHT;
}

/**
 * sheet_object_rubber_band_directly:
 * @so:
 *
 * Returns: %TRUE if we should draw the object as we are laying it out on
 * an sheet. If %FALSE we draw a rectangle where the object is going to go
 **/
gboolean
sheet_object_rubber_band_directly (G_GNUC_UNUSED SheetObject const *so)
{
	return FALSE;
}

/**
 * sheet_object_anchor_init:
 * @anchor: #SheetObjectAnchor
 * @r: (nullable): #GnmRange to look in
 * @offsets: (in) (array fixed-size=4) (nullable):
 * @direction: #GODrawingAnchorDir
 * @mode: #GnmSOAnchorMode
 *
 * A utility routine to initialize an anchor.  Useful in case we change fields
 * in the future and want to ensure that everything is initialized.
 **/
void
sheet_object_anchor_init (SheetObjectAnchor *anchor,
			  GnmRange const *r, const double *offsets,
			  GODrawingAnchorDir direction,
			  GnmSOAnchorMode mode)
{
	int i;

	if (r == NULL) {
		static GnmRange const defaultVal = { { 0, 0 }, { 1, 1 } };
		r = &defaultVal;
	}
	anchor->cell_bound = *r;

	if (offsets == NULL) {
		static double const defaultVal[4] = { 0., 0., 0., 0. };
		offsets = defaultVal;
	}
	for (i = 4; i-- > 0 ; )
		anchor->offset[i] = offsets[i];

	anchor->base.direction = direction;
	anchor->mode = mode;
	/* TODO : add sanity checking to handle offsets past edges of col/row */
}

/*****************************************************************************/

/**
 * sheet_object_get_stacking:
 * @so: #SheetObject
 *
 * Returns: @so's position in the stack of sheet objects.
 **/
gint
sheet_object_get_stacking (SheetObject *so)
{
	g_return_val_if_fail (so != NULL, 0);
	g_return_val_if_fail (so->sheet != NULL, 0);

	return g_slist_index (so->sheet->sheet_objects, so);
}

gint
sheet_object_adjust_stacking (SheetObject *so, gint offset)
{
	GList	 *l;
	GSList	**ptr, *node = NULL;
	int	  i, target, cur = 0;

	g_return_val_if_fail (so != NULL, 0);
	g_return_val_if_fail (so->sheet != NULL, 0);

	for (ptr = &so->sheet->sheet_objects ; *ptr ; ptr = &(*ptr)->next, cur++)
		if ((*ptr)->data == so) {
			node = *ptr;
			*ptr = (*ptr)->next;
			break;
		}

	g_return_val_if_fail (node != NULL, 0);

	/* Start at the begining when moving things towards the front */
	if (offset > 0) {
		ptr = &so->sheet->sheet_objects;
		i = 0;
	} else
		i = cur;

	for (target = cur - offset; *ptr && i < target ; ptr = &(*ptr)->next)
		i++;

	node->next = *ptr;
	*ptr = node;

	/* TODO : Move this to the container */
	for (l = so->realized_list; l; l = l->next) {
		GocItem *item = GOC_ITEM (l->data);
		if (offset > 0)
			goc_item_raise (item, offset);
		else
			goc_item_lower (item, - offset);
	}
	return cur - i;
}

void
sheet_object_set_anchor_mode (SheetObject *so, GnmSOAnchorMode const *mode)
{
	/* FIXME: adjust offsets according to the old and new modes */
	double pts[4];

	if (so->anchor.mode == *mode)
		return;
	sheet_object_anchor_to_pts (&so->anchor, so->sheet, pts);
	so->anchor.mode = *mode;
	sheet_object_pts_to_anchor (&so->anchor, so->sheet, pts);
}

/*****************************************************************************/

static GObjectClass *view_parent_class;

void
sheet_object_view_set_bounds (SheetObjectView *sov,
			      double const *coords, gboolean visible)
{
	SheetObjectViewClass *klass;

	g_return_if_fail (GNM_IS_SO_VIEW (sov));
	klass = GNM_SO_VIEW_GET_CLASS (sov);
	if (NULL != klass->set_bounds)
		(klass->set_bounds) (sov, coords, visible);
}

/**
 * sheet_object_view_get_item:
 * @sov: #SheetObjectView
 *
 * Returns: (transfer none): the #GocItem implementing @sov
 **/
GocItem *
sheet_object_view_get_item (SheetObjectView *sov)
{
	g_return_val_if_fail (GNM_IS_SO_VIEW (sov), NULL);

	return goc_group_get_child (GOC_GROUP (sov), 0);
}


/**
 * sheet_object_view_get_so:
 * @sov: #SheetObjectView
 *
 * Returns: (transfer none): the #SheetObject owning @view.
 **/
SheetObject *
sheet_object_view_get_so (SheetObjectView *view)
{
	return g_object_get_qdata (G_OBJECT (view), sov_so_quark);
}

static gboolean
sheet_object_view_enter_notify (GocItem *item, double x, double y)
{
	SheetObject *so;

	if (GNM_IS_PANE (item->canvas) && scg_wbcg (GNM_SIMPLE_CANVAS (item->canvas)->scg)->new_object) {
		GnmItemGrid *grid = GNM_PANE (item->canvas)->grid;
		return GOC_ITEM_GET_CLASS (grid)->enter_notify (GOC_ITEM (grid), x, y);
	}

	so = (SheetObject *) g_object_get_qdata (G_OBJECT (item), sov_so_quark);
	gnm_widget_set_cursor_type (GTK_WIDGET (item->canvas),
		(so->flags & SHEET_OBJECT_CAN_PRESS) ? GDK_HAND2 : GDK_ARROW);
	return FALSE;
}

static void
cb_so_menu_activate (GObject *menu, GocItem *view)
{
	SheetObjectAction const *a = g_object_get_data (menu, "action");

	if (a->func) {
		SheetObject *so = sheet_object_view_get_so (GNM_SO_VIEW (view));
		gpointer data = g_object_get_data (G_OBJECT (view->canvas), "sheet-control");

		if (data == NULL)
			data = GNM_SIMPLE_CANVAS (view->canvas)->scg;

		(a->func) (so, GNM_SHEET_CONTROL (data));
	}
}

static void
cb_ptr_array_free (GPtrArray *actions)
{
	g_ptr_array_free (actions, TRUE);
}

/**
 * sheet_object_build_menu:
 * @view: #SheetObjectView
 * @actions: (element-type SheetObjectAction): array of actions.
 * @i: index of first action to add in the array.
 *
 * Builds the contextual menu for @view.
 * Returns: (transfer full): the newly constructed #GtkMenu.
 **/
GtkWidget *
sheet_object_build_menu (SheetObjectView *view,
			 GPtrArray const *actions, unsigned *i)
{
	SheetObjectAction const *a;
	GtkWidget *item, *menu = gtk_menu_new ();

	while (*i < actions->len) {
		a = g_ptr_array_index (actions, *i);
		(*i)++;
		if (a->submenu < 0)
			break;
		if (a->icon != NULL) {
			if (a->label != NULL) {
				item = gtk_image_menu_item_new_with_mnemonic (_(a->label));
				gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item),
					gtk_image_new_from_icon_name (a->icon, GTK_ICON_SIZE_MENU));
			} else
				item = gtk_image_menu_item_new_from_stock (a->icon, NULL);
		} else if (a->label != NULL)
			item = gtk_menu_item_new_with_mnemonic (_(a->label));
		else
			item = gtk_separator_menu_item_new ();
		if (a->submenu > 0)
			gtk_menu_item_set_submenu (GTK_MENU_ITEM (item),
						   sheet_object_build_menu (view, actions, i));
		else if (a->label != NULL || a->icon != NULL) { /* not a separator or menu */
			g_object_set_data (G_OBJECT (item), "action", (gpointer)a);
			g_signal_connect_object (G_OBJECT (item), "activate",
				G_CALLBACK (cb_so_menu_activate), view, 0);
			gtk_widget_set_sensitive (item, a->enable_func == NULL
						  || a->enable_func (sheet_object_view_get_so (view)));
		}
		gtk_menu_shell_append (GTK_MENU_SHELL (menu),  item);
	}
	return menu;
}

static gboolean
sheet_object_view_button_pressed (GocItem *item, int button, double x, double y)
{
	GnmPane *pane;
	SheetObject *so;
	if (GNM_IS_PANE (item->canvas)) {
		if (scg_wbcg (GNM_SIMPLE_CANVAS (item->canvas)->scg)->new_object) {
			GnmItemGrid *grid = GNM_PANE (item->canvas)->grid;
			return GOC_ITEM_GET_CLASS (grid)->button_pressed (GOC_ITEM (grid), button, x, y);
		}

		if (button > 3)
			return FALSE;

		pane = GNM_PANE (item->canvas);
		so = (SheetObject *) g_object_get_qdata (G_OBJECT (item), sov_so_quark);

		x *= goc_canvas_get_pixels_per_unit (item->canvas);
		y *= goc_canvas_get_pixels_per_unit (item->canvas);
		/* cb_sheet_object_widget_canvas_event calls even if selected */
		if (NULL == g_hash_table_lookup (pane->drag.ctrl_pts, so)) {
			SheetObjectClass *soc =
				G_TYPE_INSTANCE_GET_CLASS (so, GNM_SO_TYPE, SheetObjectClass);
			GdkEventButton *event = (GdkEventButton *) goc_canvas_get_cur_event (item->canvas);

			if (soc->interactive && button != 3)
				return FALSE;

			if (!(event->state & GDK_SHIFT_MASK))
				scg_object_unselect (pane->simple.scg, NULL);
			scg_object_select (pane->simple.scg, so);
			if (NULL == g_hash_table_lookup (pane->drag.ctrl_pts, so))
				return FALSE;	/* protected ? */
		}

		if (button < 3)
			gnm_pane_object_start_resize (pane, button, x, y, so, 8, FALSE);
		else
			gnm_pane_display_object_menu (pane, so, goc_canvas_get_cur_event (item->canvas));
	} else {
		if (button == 3) {
			GPtrArray *actions = g_ptr_array_new ();
			GtkWidget *menu;
			unsigned i = 0;

			so = (SheetObject *) g_object_get_qdata (G_OBJECT (item), sov_so_quark);
			sheet_object_populate_menu (so, actions);

			if (actions->len == 0) {
				g_ptr_array_free (actions, TRUE);
				return FALSE;
			}

			menu = sheet_object_build_menu
				(sheet_object_get_view (so, (SheetObjectViewContainer *) item->canvas),
				 actions, &i);
			g_object_set_data_full (G_OBJECT (menu), "actions", actions,
				(GDestroyNotify) cb_ptr_array_free);
			gtk_widget_show_all (menu);
			gnumeric_popup_menu (GTK_MENU (menu),
					     goc_canvas_get_cur_event (item->canvas));
		}
	}
	return TRUE;
}

static gboolean
sheet_object_view_button2_pressed (GocItem *item, int button, double x, double y)
{
	if (button == 1 && !GNM_IS_PANE (item->canvas)) {
		SheetControl *sc = GNM_SHEET_CONTROL (g_object_get_data (G_OBJECT (item->canvas), "sheet-control"));
		SheetObject *so = (SheetObject *) g_object_get_qdata (G_OBJECT (item), sov_so_quark);

		if (sc && sheet_object_can_edit (so))
			sheet_object_get_editor (so, sc);
	}
	return TRUE;
}

static void
sheet_object_view_finalize (GObject *obj)
{
	SheetObject *so = (SheetObject *) g_object_get_qdata (obj, sov_so_quark);
	if (so)
		so->realized_list = g_list_remove (so->realized_list, obj);
	view_parent_class->finalize (obj);
}

static void
sheet_object_view_class_init (GocItemClass *item_klass)
{
	GObjectClass *obj_klass = (GObjectClass *) item_klass;
	view_parent_class = g_type_class_peek_parent (item_klass);

	obj_klass->finalize = sheet_object_view_finalize;

	item_klass->enter_notify = sheet_object_view_enter_notify;
	item_klass->button_pressed = sheet_object_view_button_pressed;
	item_klass->button2_pressed = sheet_object_view_button2_pressed;
}

GSF_CLASS (SheetObjectView, sheet_object_view,
	   sheet_object_view_class_init, NULL,
	   GOC_TYPE_GROUP)

/*****************************************************************************/

GType
sheet_object_imageable_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static GTypeInfo const type_info = {
			sizeof (SheetObjectImageableIface), /* class_size */
			NULL,				/* base_init */
			NULL,				/* base_finalize */
			NULL, NULL, NULL, 0, 0, NULL, NULL
		};

		type = g_type_register_static (G_TYPE_INTERFACE,
			"SheetObjectImageable", &type_info, 0);
	}

	return type;
}

#define GNM_SO_IMAGEABLE_CLASS(o)	(G_TYPE_INSTANCE_GET_INTERFACE ((o), GNM_SO_IMAGEABLE_TYPE, SheetObjectImageableIface))

GtkTargetList *
sheet_object_get_target_list (SheetObject const *so)
{
	if (!GNM_IS_SO_IMAGEABLE (so))
		return NULL;

	return GNM_SO_IMAGEABLE_CLASS (so)->get_target_list (so);
}

/**
 * sheet_object_write_image:
 * @so: #SheetObject
 * @format: (nullable): image format to use
 * @resolution: export resolution
 * @output: destination
 * @err: (out) (optional) (nullable): error indication
 *
 * Saves a sheet object as an image to @output.  If an error occurs, @err will
 * be set.
 **/
void
sheet_object_write_image (SheetObject const *so, char const *format, double resolution,
			  GsfOutput *output, GError **err)
{
	g_return_if_fail (GNM_IS_SO_IMAGEABLE (so));
	g_return_if_fail (GSF_IS_OUTPUT (output));

	GNM_SO_IMAGEABLE_CLASS (so)->write_image (so, format, resolution,
							output, err);
}

/**
 * sheet_object_save_as_image:
 * @so: #SheetObject
 * @format: (nullable): image format to use
 * @resolution: export resolution
 * @url: destination url
 * @err: (out) (optional) (nullable): error indication
 *
 * Saves a sheet object as an image to @url.  If an error occurs, @err
 * will be set.
 **/
void
sheet_object_save_as_image (SheetObject const *so,
			    char const *format,
			    double resolution,
			    const char *url,
			    GError **err)
{
	GsfOutput *dst;

	g_return_if_fail (GNM_IS_SO_IMAGEABLE (so));
	g_return_if_fail (url != NULL);

	dst = go_file_create (url, err);
	if (!dst)
		return;

	sheet_object_write_image (so, format, resolution, dst, err);
	gsf_output_close (dst);
	g_object_unref (dst);
}

/*****************************************************************************/

GType
sheet_object_exportable_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static GTypeInfo const type_info = {
			sizeof (SheetObjectExportableIface), /* class_size */
			NULL,				/* base_init */
			NULL,				/* base_finalize */
			NULL, NULL, NULL, 0, 0, NULL, NULL
		};

		type = g_type_register_static (G_TYPE_INTERFACE,
			"SheetObjectExportable", &type_info, 0);
	}

	return type;
}

#define GNM_SO_EXPORTABLE_CLASS(o)	(G_TYPE_INSTANCE_GET_INTERFACE ((o), GNM_SO_EXPORTABLE_TYPE, SheetObjectExportableIface))

GtkTargetList *
sheet_object_exportable_get_target_list (SheetObject const *so)
{
	if (!GNM_IS_SO_EXPORTABLE (so))
		return NULL;

	return GNM_SO_EXPORTABLE_CLASS (so)->get_target_list (so);
}

void
sheet_object_write_object (SheetObject const *so, char const *format,
			   GsfOutput *output, GError **err,
			   GnmConventions const *convs)
{
	GnmLocale *locale;

	g_return_if_fail (GNM_IS_SO_EXPORTABLE (so));

	locale = gnm_push_C_locale ();
	GNM_SO_EXPORTABLE_CLASS (so)->
		write_object (so, format, output, err, convs);
	gnm_pop_C_locale (locale);
}

/**
 * sheet_object_move_undo:
 * @objects: (element-type SheetObject):
 * @objects_created:
 *
 * Returns: (transfer full): the newly allocated #GOUndo.
 **/
GOUndo *
sheet_object_move_undo (GSList *objects, gboolean objects_created)
{
	GOUndo *undo = NULL;
	GSList *objs = objects;

	g_return_val_if_fail (NULL != objects, NULL);

	for (; objs; objs = objs->next) {
		SheetObject *obj = objs->data;
		SheetObjectAnchor *tmp;

		if (objects_created) {
			undo = go_undo_combine
				(undo,
				 go_undo_unary_new
				 (g_object_ref (obj),
				  (GOUndoUnaryFunc) sheet_object_clear_sheet,
				  (GFreeFunc) g_object_unref));
		}

		tmp = g_new (SheetObjectAnchor, 1);
		*tmp = *sheet_object_get_anchor (obj);
		undo = go_undo_combine
			(undo, go_undo_binary_new
			 (g_object_ref (obj), tmp,
			  (GOUndoBinaryFunc) sheet_object_set_anchor,
			  (GFreeFunc) g_object_unref,
			  (GFreeFunc) g_free));
	}
	return undo;
}

/**
 * sheet_object_move_do:
 * @objects: (element-type SheetObject):
 * @anchors: (element-type SheetObjectAnchor):
 * @objects_created:
 *
 * Returns: (transfer full): the newly allocated #GOUndo.
 **/
GOUndo *
sheet_object_move_do (GSList *objects, GSList *anchors,
		      gboolean objects_created)
{
	GOUndo *undo = NULL;
	GSList *objs = objects, *anchs = anchors;

	g_return_val_if_fail (NULL != objects, NULL);
	g_return_val_if_fail (NULL != anchors, NULL);
	g_return_val_if_fail (g_slist_length (objects)
			      == g_slist_length (anchors), NULL);

	for (; objs && anchs; objs = objs->next, anchs = anchs->next) {
		SheetObject *obj = objs->data;
		SheetObjectAnchor *anch = anchs->data;
		SheetObjectAnchor *tmp;

		if (objects_created) {
			undo = go_undo_combine
				(undo,
				 go_undo_binary_new
				 (g_object_ref (obj),
				  sheet_object_get_sheet (obj),
				  (GOUndoBinaryFunc) sheet_object_set_sheet,
				  (GFreeFunc) g_object_unref,
				  NULL));
		}
		tmp = g_new (SheetObjectAnchor, 1);
		*tmp = *anch;
		undo = go_undo_combine
			(go_undo_binary_new
			 (g_object_ref (obj), tmp,
			  (GOUndoBinaryFunc) sheet_object_set_anchor,
			  (GFreeFunc) g_object_unref,
			  (GFreeFunc) g_free), undo);
	}
	return undo;
}


/*****************************************************************************/

/**
 * sheet_objects_init: (skip)
 */
void
sheet_objects_init (void)
{
	debug_sheet_objects = gnm_debug_flag ("sheet-objects");
	so_create_view_sos = g_ptr_array_new ();

	GNM_SO_LINE_TYPE;
	GNM_SO_FILLED_TYPE;
	GNM_SO_GRAPH_TYPE;
	GNM_SO_IMAGE_TYPE;
	GNM_GO_DATA_SCALAR_TYPE;
	GNM_GO_DATA_VECTOR_TYPE;
	GNM_GO_DATA_MATRIX_TYPE;
	GNM_CELL_COMMENT_TYPE;

	sheet_object_widget_register ();
	sov_so_quark = g_quark_from_static_string ("SheetObject");
	sov_container_quark = g_quark_from_static_string ("SheetObjectViewContainer");
}

/**
 * sheet_objects_shutdown: (skip)
 */
void
sheet_objects_shutdown (void)
{
	if (so_create_view_src != 0) {
		g_source_remove (so_create_view_src);
		so_create_view_src = 0;
	}

	g_ptr_array_free (so_create_view_sos, TRUE);
	so_create_view_sos = NULL;
}
