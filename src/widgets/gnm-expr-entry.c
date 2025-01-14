/*
 * gnm-expr-entry.c: An entry widget specialized to handle expressions
 * and ranges.
 *
 * Author:
 *   Jon Kåre Hellan (hellan@acm.org)
 */

#include <gnumeric-config.h>
#include <gnm-i18n.h>
#include <gnumeric.h>
#include <widgets/gnm-expr-entry.h>

#include <wbc-gtk-impl.h>
#include <sheet-control-gui-priv.h>
#include <gnm-pane.h>
#include <sheet-merge.h>
#include <parse-util.h>
#include <gui-util.h>
#include <ranges.h>
#include <value.h>
#include <expr.h>
#include <func.h>
#include <dependent.h>
#include <sheet.h>
#include <sheet-style.h>
#include <workbook.h>
#include <sheet-view.h>
#include <selection.h>
#include <commands.h>
#include <gnm-format.h>
#include <number-match.h>
#include <gnm-datetime.h>
#include <gnumeric-conf.h>
#include <dead-kittens.h>
#include <dialogs/dialogs.h>
#include <goffice/goffice.h>

#include <gsf/gsf-impl-utils.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>

#define UNICODE_LEFT_TRIANGLE "\xe2\x97\x80"
#define UNICODE_RIGHT_TRIANGLE "\xe2\x96\xb6"
#define UNICODE_CROSS_AND_SKULLBONES "\xe2\x98\xa0"
#define UNICODE_ELLIPSIS "\xe2\x80\xa6"
#define UNICODE_ELLIPSIS_VERT "\xe2\x8b\xae"
#define UNICODE_ARROW_UP "\xe2\x87\xa7"
#define UNICODE_CHECKMARK "\342\234\223"

#warning We should replace these token names with the correct values
   enum yytokentype {
     STRING = 258,
     QUOTED_STRING = 259,
     CONSTANT = 260,
     RANGEREF = 261,
     INTERSECT = 268,
     ARG_SEP = 269,
     INVALID_TOKEN = 273
   };
#define TOKEN_UNMATCHED_APOSTROPHE INVALID_TOKEN

GType
gnm_update_type_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      { GNM_UPDATE_CONTINUOUS, "GNM_UPDATE_CONTINUOUS", "continuous" },
      { GNM_UPDATE_DISCONTINUOUS, "GNM_UPDATE_DISCONTINUOUS", "discontinuous" },
      { GNM_UPDATE_DELAYED, "GNM_UPDATE_DELAYED", "delayed" },
      { 0, NULL, NULL }
    };
    etype = g_enum_register_static (g_intern_static_string ("GnmUpdateType"), values);
  }
  return etype;
}

typedef struct {
	GnmRangeRef ref;
	int	    text_start;
	int	    text_end;
	gboolean    is_valid;
} Rangesel;

struct GnmExprEntry_ {
	GtkBox	parent;

	GtkEntry		*entry;
	GtkWidget               *calendar_combo;
	gulong                   calendar_combo_changed;
	GtkWidget		*icon;
	SheetControlGUI		*scg;	/* the source of the edit */
	Sheet			*sheet;	/* from scg */
	GnmParsePos		 pp;	/* from scg->sv */
	WBCGtk			*wbcg;	/* from scg */
	Rangesel		 rangesel;

	GnmExprEntryFlags	 flags;
	int			 freeze_count;

	GnmUpdateType		 update_policy;
	guint			 update_timeout_id;

	gboolean                 is_cell_renderer;  /* as cell_editable */
	gboolean                 editing_canceled;  /* as cell_editable */
	gboolean                 ignore_changes; /* internal mutex */

	gboolean                 feedback_disabled;
	GnmLexerItem            *lexer_items;
	GnmExprTop const        *texpr;
	struct {
		GtkWidget       *tooltip;
		GnmFunc         *fd;
		gint             args;
		gboolean         had_stuff;
		gulong           handlerid;
		guint            timerid;
		gboolean         enabled;
		gboolean         is_expr;
		gboolean         completion_se_valid;
		gchar           *completion;
		guint            completion_start;
		guint            completion_end;
	}                        tooltip;

	GOFormat const *constant_format;
};

typedef struct _GnmExprEntryClass {
	GtkBoxClass base;

	void (* update)   (GnmExprEntry *gee, gboolean user_requested_update);
	void (* changed)  (GnmExprEntry *gee);
	void (* activate) (GnmExprEntry *gee);
} GnmExprEntryClass;

/* Signals */
enum {
	UPDATE,
	CHANGED,
	ACTIVATE,
	LAST_SIGNAL
};

/* Properties */
enum {
	PROP_0,
	PROP_UPDATE_POLICY,
	PROP_WITH_ICON,
	PROP_TEXT,
	PROP_FLAGS,
	PROP_SCG,
	PROP_WBCG,
	PROP_CONSTANT_FORMAT,
	PROP_EDITING_CANCELED
};

static guint signals[LAST_SIGNAL] = { 0 };

static void gee_set_value_double (GogDataEditor *editor, double val,
				  GODateConventions const *date_conv);

/* Internal routines
 */
static void     gee_rangesel_reset (GnmExprEntry *gee);
static void     gee_rangesel_update_text (GnmExprEntry *gee);
static void     gee_detach_scg (GnmExprEntry *gee);
static void     gee_remove_update_timer (GnmExprEntry *range);
static void     cb_gee_notify_cursor_position (GnmExprEntry *gee);

static gboolean gee_debug;
static GtkWidgetClass *parent_class = NULL;

static gboolean
gee_is_editing (GnmExprEntry *gee)
{
	return (gee != NULL && gee->wbcg != NULL && wbcg_is_editing (gee->wbcg));
}

static GnmConventions const *
gee_convs (const GnmExprEntry *gee)
{
	return sheet_get_conventions (gee->sheet);
}

static inline void
gee_force_abs_rel (GnmExprEntry *gee)
{
	Rangesel *rs = &gee->rangesel;
	rs->is_valid = FALSE;
	if ((gee->flags & GNM_EE_FORCE_ABS_REF))
		rs->ref.a.col_relative = rs->ref.b.col_relative =
			rs->ref.a.row_relative = rs->ref.b.row_relative = FALSE;
        else if ((gee->flags & GNM_EE_FORCE_REL_REF))
		rs->ref.a.col_relative = rs->ref.b.col_relative =
			rs->ref.a.row_relative = rs->ref.b.row_relative = TRUE;
}

static void
gee_rangesel_reset (GnmExprEntry *gee)
{
	Rangesel *rs = &gee->rangesel;

	rs->text_start = 0;
	rs->text_end = 0;
	memset (&rs->ref, 0, sizeof (rs->ref));
	rs->ref.a.col_relative =
	rs->ref.b.col_relative =
	rs->ref.a.row_relative =
	rs->ref.b.row_relative = ((gee->flags & (GNM_EE_FORCE_ABS_REF|GNM_EE_DEFAULT_ABS_REF)) == 0);

	rs->is_valid = FALSE;
}

static void
gee_destroy (GtkWidget *widget)
{
	GnmExprEntry *gee = GNM_EXPR_ENTRY (widget);
	gee_remove_update_timer (gee);
	gee_detach_scg (gee);
	((GtkWidgetClass *)(parent_class))->destroy (widget);
}

static void
cb_icon_clicked (GtkButton *icon,
		 GnmExprEntry *entry)
{
	GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (entry));

	/* TODO special-case GnmExprEntry being directly packed
	 * into a GtkWindow. Currently, we just use it in
	 * GtkDialogs so the current window child widget
	 * is never identical to the entry when it is
	 * not rolled up.
	 */

	if (toplevel != NULL && gtk_widget_is_toplevel (toplevel)) {
		GtkWidget *old_entry_parent;
		GtkWidget *old_toplevel_child;
		GParamSpec **container_props_pspec;
		GArray *container_props;

		g_assert (GTK_IS_WINDOW (toplevel));

		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (icon))) {
			int width, height;
			guint n;

			/* roll-up request */

			old_toplevel_child = gtk_bin_get_child (GTK_BIN (toplevel));
			g_assert (GTK_IS_WIDGET (old_toplevel_child));

			old_entry_parent = gtk_widget_get_parent (GTK_WIDGET (entry));
			g_assert (GTK_IS_CONTAINER (old_entry_parent));

			g_object_set_data_full (G_OBJECT (entry), "old_entry_parent",
						g_object_ref (old_entry_parent),
						(GDestroyNotify) g_object_unref);

			g_return_if_fail ((GtkWidget *) entry != old_toplevel_child);

			g_object_set_data_full (G_OBJECT (entry), "old_toplevel_child",
						g_object_ref (old_toplevel_child),
						(GDestroyNotify) g_object_unref);

			gtk_window_get_size (GTK_WINDOW (toplevel), &width, &height);
			g_object_set_data (G_OBJECT (entry), "old_window_width", GUINT_TO_POINTER (width));
			g_object_set_data (G_OBJECT (entry), "old_window_height", GUINT_TO_POINTER (height));
			g_object_set_data (G_OBJECT (entry), "old_default",
					   gtk_window_get_default_widget (GTK_WINDOW (toplevel)));

			container_props = NULL;

			container_props_pspec = gtk_container_class_list_child_properties
					(G_OBJECT_GET_CLASS (old_entry_parent), &n);

			if (container_props_pspec[0] != NULL) {
				guint ui;

				container_props = g_array_sized_new (FALSE, TRUE, sizeof (GValue), n);

				for (ui = 0; ui < n; ui++) {
					GValue value = G_VALUE_INIT;
					g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (container_props_pspec[ui]));

					gtk_container_child_get_property (GTK_CONTAINER (old_entry_parent), GTK_WIDGET (entry),
									  g_param_spec_get_name (container_props_pspec[ui]),
									  &value);
					g_array_append_val (container_props, value);
				}
			}

			g_object_set_data_full (G_OBJECT (entry), "container_props",
						container_props,
						(GDestroyNotify) g_array_unref);
			g_object_set_data_full (G_OBJECT (entry), "container_props_pspec",
						container_props_pspec,
						(GDestroyNotify) g_free);

			gtk_container_remove (GTK_CONTAINER (toplevel), old_toplevel_child);
			gtk_widget_reparent (GTK_WIDGET (entry), toplevel);

			gtk_widget_grab_focus (GTK_WIDGET (entry->entry));
			gtk_widget_set_can_default (GTK_WIDGET (icon), TRUE);
			gtk_widget_grab_default (GTK_WIDGET (icon));

			gtk_window_resize (GTK_WINDOW (toplevel), 1, 1);

		} else {
			int i;
			gpointer default_widget;

			/* reset rolled-up window */

			old_toplevel_child = g_object_get_data (G_OBJECT (entry), "old_toplevel_child");
			g_assert (GTK_IS_WIDGET (old_toplevel_child));

			old_entry_parent = g_object_get_data (G_OBJECT (entry), "old_entry_parent");
			g_assert (GTK_IS_CONTAINER (old_entry_parent));

			g_object_ref (entry);
			gtk_container_remove (GTK_CONTAINER (toplevel), GTK_WIDGET (entry));
			gtk_container_add (GTK_CONTAINER (toplevel), old_toplevel_child);
			gtk_container_add (GTK_CONTAINER (old_entry_parent), GTK_WIDGET (entry));
			g_object_unref (entry);

			container_props = g_object_get_data (G_OBJECT (entry), "container_props");
			container_props_pspec = g_object_get_data (G_OBJECT (entry), "container_props_pspec");

			for (i = 0; container_props_pspec[i] != NULL; i++) {
				gtk_container_child_set_property (GTK_CONTAINER (old_entry_parent), GTK_WIDGET (entry),
								  g_param_spec_get_name (container_props_pspec[i]),
								  &g_array_index (container_props, GValue, i));
			}

			gtk_window_resize (GTK_WINDOW (toplevel),
					   GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (entry), "old_window_width")),
					   GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (entry), "old_window_height")));
			default_widget = g_object_get_data (G_OBJECT (entry), "old_default");
			if (default_widget != NULL) {
				gtk_window_set_default (GTK_WINDOW (toplevel), GTK_WIDGET (default_widget));
				g_object_set_data (G_OBJECT (entry), "old_default", NULL);
			}

			g_object_set_data (G_OBJECT (entry), "old_entry_parent", NULL);
			g_object_set_data (G_OBJECT (entry), "old_toplevel_child", NULL);
			g_object_set_data (G_OBJECT (entry), "container_props", NULL);
			g_object_set_data (G_OBJECT (entry), "container_props_pspec", NULL);
		}
	} else {
		g_warning ("GnmExprEntry button was clicked, but entry has no toplevel parent.");
	}
}

static GnmValue *
get_matched_value (GnmExprEntry *gee)
{
	GODateConventions const *date_conv =
		sheet_date_conv (gee->sheet);
	const char *text = gnm_expr_entry_get_text (gee);

	return format_match_number (text, gee->constant_format, date_conv);
}


static void
gee_update_calendar (GnmExprEntry *gee)
{
	GDate date;
	GnmValue *v;
	GODateConventions const *date_conv =
		sheet_date_conv (gee->sheet);

	if (!gee->calendar_combo)
		return;

	v = get_matched_value (gee);
	if (!v)
		return;

	if (datetime_value_to_g (&date, v, date_conv)) {
		g_signal_handler_block (gee->calendar_combo,
					gee->calendar_combo_changed);
		go_calendar_button_set_date
			(GO_CALENDAR_BUTTON (gee->calendar_combo),
			 &date);
		g_signal_handler_unblock (gee->calendar_combo,
					  gee->calendar_combo_changed);
	}

	value_release (v);
}

static void
cb_calendar_changed (GOCalendarButton *calb, GnmExprEntry *gee)
{
	GDate date;
	GODateConventions const *date_conv =
		sheet_date_conv (gee->sheet);
	int serial;

	if (!go_calendar_button_get_date (calb, &date))
		return;

	serial = go_date_g_to_serial (&date, date_conv);

	gee_set_value_double (GOG_DATA_EDITOR (gee), serial, date_conv);
}

static void
gee_set_format (GnmExprEntry *gee, GOFormat const *fmt)
{
	if (fmt == gee->constant_format)
		return;

	if (fmt) go_format_ref (fmt);
	go_format_unref (gee->constant_format);
	gee->constant_format = fmt;

	if (gee_debug)
		g_printerr ("Setting format %s\n",
			    fmt ? go_format_as_XL (fmt) : "-");

	if (fmt && go_format_is_date (fmt)) {
		if (!gee->calendar_combo) {
			gee->calendar_combo = go_calendar_button_new ();
			gtk_widget_show (gee->calendar_combo);
			gtk_box_pack_start (GTK_BOX (gee), gee->calendar_combo,
					    FALSE, TRUE, 0);
			gee->calendar_combo_changed =
				g_signal_connect (G_OBJECT (gee->calendar_combo),
						  "changed",
						  G_CALLBACK (cb_calendar_changed),
						  gee);
			gee_update_calendar (gee);
		}
	} else {
		if (gee->calendar_combo) {
			gtk_widget_destroy (gee->calendar_combo);
			gee->calendar_combo = NULL;
			gee->calendar_combo_changed = 0;
		}
	}

	g_object_notify (G_OBJECT (gee), "constant-format");
}

static void
gee_set_with_icon (GnmExprEntry *gee, gboolean with_icon)
{
	gboolean has_icon = (gee->icon != NULL);
	with_icon = !!with_icon;

	if (has_icon == with_icon)
		return;

	if (with_icon) {
		gee->icon = gtk_toggle_button_new ();
		gtk_container_add (GTK_CONTAINER (gee->icon),
				   gtk_image_new_from_icon_name ("gnumeric-exprentry",
								 GTK_ICON_SIZE_MENU));
		gtk_box_pack_end (GTK_BOX (gee), gee->icon, FALSE, FALSE, 0);
		gtk_widget_show_all (gee->icon);
		g_signal_connect (gee->icon, "clicked",
				  G_CALLBACK (cb_icon_clicked), gee);
	} else
		gtk_widget_destroy (gee->icon);
}

static void
gee_set_property (GObject      *object,
		  guint         prop_id,
		  GValue const *value,
		  GParamSpec   *pspec)
{
	GnmExprEntry *gee = GNM_EXPR_ENTRY (object);
	switch (prop_id) {
	case PROP_UPDATE_POLICY:
		gnm_expr_entry_set_update_policy (gee, g_value_get_enum (value));
		break;

	case PROP_WITH_ICON:
		gee_set_with_icon (gee, g_value_get_boolean (value));
		break;

	case PROP_TEXT: {
		const char *new_txt = g_value_get_string (value);
		const char *old_txt = gnm_expr_entry_get_text (gee);
		if (go_str_compare (new_txt, old_txt)) {
			gnm_expr_entry_load_from_text (gee, new_txt);
			gnm_expr_entry_signal_update (gee, FALSE);
		}
		break;
	}

	case PROP_FLAGS:
		gnm_expr_entry_set_flags (gee,
			g_value_get_uint (value), GNM_EE_MASK);
		break;
	case PROP_SCG:
		gnm_expr_entry_set_scg (gee,
			GNM_SCG (g_value_get_object (value)));
		break;
	case PROP_WBCG:
		g_return_if_fail (gee->wbcg == NULL);
		gee->wbcg = WBC_GTK (g_value_get_object (value));
		break;
	case PROP_CONSTANT_FORMAT:
		gee_set_format (gee, g_value_get_boxed (value));
		break;
	case PROP_EDITING_CANCELED:
		gee->editing_canceled = g_value_get_boolean (value);
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
gee_get_property (GObject      *object,
		  guint         prop_id,
		  GValue       *value,
		  GParamSpec   *pspec)
{
	GnmExprEntry *gee = GNM_EXPR_ENTRY (object);
	switch (prop_id) {
	case PROP_UPDATE_POLICY:
		g_value_set_enum (value, gee->update_policy);
		break;
	case PROP_WITH_ICON:
		g_value_set_boolean (value, gee->icon != NULL);
		break;
	case PROP_TEXT:
		g_value_set_string (value, gnm_expr_entry_get_text (gee));
		break;
	case PROP_FLAGS:
		g_value_set_uint (value, gee->flags);
		break;
	case PROP_SCG:
		g_value_set_object (value, G_OBJECT (gee->scg));
		break;
	case PROP_WBCG:
		g_value_set_object (value, G_OBJECT (gee->wbcg));
		break;
	case PROP_CONSTANT_FORMAT:
		g_value_set_boxed (value, (gpointer)gee->constant_format);
		break;
	case PROP_EDITING_CANCELED:
		g_value_set_boolean (value, gee->editing_canceled);
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
cb_entry_activate (GnmExprEntry *gee)
{
	g_signal_emit (G_OBJECT (gee), signals[ACTIVATE], 0);
	gnm_expr_entry_signal_update (gee, TRUE);
}

static void
gee_destroy_feedback_range (GnmExprEntry *gee)
{
	WBCGtk *wbcg = scg_wbcg (gee->scg);
	int page, pages = wbcg_get_n_scg (wbcg);

	for (page = 0; page < pages; page++) {
		SheetControlGUI *scg = wbcg_get_nth_scg (wbcg, page);
		SCG_FOREACH_PANE (scg, pane,
				  gnm_pane_expr_cursor_stop (pane););
	}
}

static void
gnm_expr_entry_colour_ranges (GnmExprEntry *gee, int start, int end, GnmRangeRef *rr, int colour,
			      PangoAttrList **attrs, gboolean insert_cursor)
{
	static const GOColor colours[] = {
		GO_COLOR_FROM_RGB (0x00, 0xff, 0x00),
		GO_COLOR_FROM_RGB (0x00, 0x00, 0xff),
		GO_COLOR_FROM_RGB (0xff, 0x00, 0x00),
		GO_COLOR_FROM_RGB (0x00, 0x80, 0x80),
		GO_COLOR_FROM_RGB (0xa0, 0xa0, 0x00),
		GO_COLOR_FROM_RGB (0xa0, 0x00, 0xa0)
	};
	PangoAttribute *at;
	GnmRange r;
	GnmRange const *merge; /*[#127415]*/
	Sheet *start_sheet, *end_sheet;
	Sheet *sheet = scg_sheet (gee->scg);
	SheetControlGUI *scg = NULL;

	if (rr->a.sheet->workbook != gee->sheet->workbook) {
		/* We should show the range in an external workbook! */
		return;
	}

	if (*attrs == NULL)
		*attrs = pango_attr_list_new ();

	colour = colour % G_N_ELEMENTS (colours);

	gnm_rangeref_normalize_pp (rr, &gee->pp,
				   &start_sheet,
				   &end_sheet,
				   &r);
	if (start_sheet != end_sheet)
		return;
	if (insert_cursor) {
		if (range_is_singleton  (&r) &&
		    NULL != (merge = gnm_sheet_merge_is_corner
			     (start_sheet, &r.start)))
			r = *merge;
		if (start_sheet == sheet)
			scg = gee->scg;
		else {
			WBCGtk *wbcg = scg_wbcg (gee->scg);
			scg = wbcg_get_nth_scg (wbcg, start_sheet->index_in_wb);
		}

		SCG_FOREACH_PANE (scg, pane, gnm_pane_expr_cursor_bound_set
				  (pane, &r, colours[colour]););
	}

	at = go_color_to_pango (colours[colour], TRUE);
	at->start_index = start;
	at->end_index = end;

	pango_attr_list_change (*attrs, at);
}

/* WARNING : DO NOT CALL THIS FROM FROM UPDATE.  It may create another
 *           canvas-item which would in turn call update and confuse the
 *           canvas.
 */
static void
gee_scan_for_range (GnmExprEntry *gee)
{
	PangoAttrList *attrs = NULL;

	parse_pos_init_editpos (&gee->pp, scg_view (gee->scg));
	gee_destroy_feedback_range (gee);
	if (!gee->feedback_disabled && gee_is_editing (gee) && gee->lexer_items != NULL) {
		GnmLexerItem *gli = gee->lexer_items;
		int colour = 1; /* We start with 1 since GINT_TO_POINTER (0) == NULL */
		GHashTable *hash = g_hash_table_new_full ((GHashFunc) gnm_rangeref_hash,
							  (GEqualFunc) gnm_rangeref_equal,
							  g_free,
							  NULL);
		do {
			if (gli->token == RANGEREF) {
				char const *text = gtk_entry_get_text (gee->entry);
				char *rtext = g_strndup (text + gli->start,
							 gli->end - gli->start);
				char const *tmp;
				GnmRangeRef rr;
				tmp = rangeref_parse (&rr, rtext,
						      &gee->pp, gee_convs (gee));
				if (tmp != rtext) {
					gpointer val;
					gint this_colour;
					gboolean insert_cursor;
					if (rr.a.sheet == NULL)
						rr.a.sheet = gee->sheet;
					if (rr.b.sheet == NULL)
						rr.b.sheet = rr.a.sheet;
					val = g_hash_table_lookup (hash, &rr);
					if (val == NULL) {
						GnmRangeRef *rrr = gnm_rangeref_dup (&rr);
						this_colour = colour++;
						g_hash_table_insert (hash, rrr, GINT_TO_POINTER (this_colour));
						insert_cursor = TRUE;
					} else {
						this_colour = GPOINTER_TO_INT (val);
						insert_cursor = FALSE;
					}
					gnm_expr_entry_colour_ranges (gee, gli->start, gli->end, &rr,
								      this_colour, &attrs, insert_cursor);
				}
				g_free (rtext);
			}
		} while (gli++->token != 0);
		g_hash_table_destroy (hash);
	}
	if (attrs)
		g_object_set_data_full (G_OBJECT (gee->entry), "gnm:range-attributes", attrs,
					(GDestroyNotify) pango_attr_list_unref);
	else
		g_object_set_data (G_OBJECT (gee->entry), "gnm:range-attributes", NULL);
}

static void
gee_update_env (GnmExprEntry *gee)
{
	if (!gee->ignore_changes) {
		if (NULL != gee->scg &&
#warning why do we want this dichotomy
		    !gee->is_cell_renderer &&
		    !gnm_expr_entry_can_rangesel (gee))
			scg_rangesel_stop (gee->scg, FALSE);

		if (gnm_expr_char_start_p (gtk_entry_get_text (gee->entry)))
			gee_scan_for_range (gee);
	}

}

static gboolean
gee_delete_tooltip (GnmExprEntry *gee, gboolean remove_completion)
{
	gboolean has_tooltip = (gee->tooltip.tooltip != NULL &&
				gee->tooltip.timerid == 0);

	if (gee->tooltip.timerid) {
		g_source_remove (gee->tooltip.timerid);
		gee->tooltip.timerid = 0;
	}
	if (gee->tooltip.tooltip) {
		gtk_widget_destroy (gee->tooltip.tooltip);
		gee->tooltip.tooltip = NULL;
	}
	if (gee->tooltip.fd) {
		gnm_func_dec_usage (gee->tooltip.fd);
		gee->tooltip.fd = NULL;
	}
	if (gee->tooltip.handlerid != 0 && gee->entry != NULL) {
		g_signal_handler_disconnect (gtk_widget_get_toplevel
					     (GTK_WIDGET (gee->entry)),
					     gee->tooltip.handlerid);
		gee->tooltip.handlerid = 0;
	}
	if (remove_completion) {
		g_free (gee->tooltip.completion);
		gee->tooltip.completion = NULL;
		gee->tooltip.completion_se_valid = FALSE;
	}
	return has_tooltip;
}

void
gnm_expr_entry_close_tips  (GnmExprEntry *gee)
{
	if (gee != NULL)
		gee_delete_tooltip (gee, FALSE);
}

static gboolean
cb_gee_focus_out_event (GtkWidget         *widget,
			GdkEventFocus     *event,
			gpointer           user_data);

static gboolean
cb_show_tooltip (gpointer user_data)
{
	GnmExprEntry *gee = GNM_EXPR_ENTRY (user_data);
	gtk_widget_show_all (gee->tooltip.tooltip);
	gee->tooltip.timerid = 0;
	return FALSE;
}


static GtkWidget *
gee_create_tooltip (GnmExprEntry *gee, gchar const *str,
		    gchar const *marked_str, gboolean set_tabs)
{
	GtkWidget *toplevel, *label, *tip;
	gint root_x = 0, root_y = 0;
	GtkAllocation allocation;
	GdkWindow *gdkw;
	gchar *markup = NULL;
	GString *string;
	GtkTextBuffer *buffer;
	PangoAttrList *attr_list = NULL;
	char *text = NULL;

	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (gee->entry));
	gtk_widget_add_events(toplevel, GDK_FOCUS_CHANGE_MASK);
	if (gee->tooltip.handlerid == 0)
		gee->tooltip.handlerid = g_signal_connect
			(G_OBJECT (toplevel), "focus-out-event",
			 G_CALLBACK (cb_gee_focus_out_event), gee);

	label = gnm_convert_to_tooltip (toplevel, gtk_text_view_new ());
	tip = gtk_widget_get_toplevel (label);

	gtk_style_context_add_class (gtk_widget_get_style_context (label),
				     "function-help");

	if (str)
		markup = gnm_func_convert_markup_to_pango (str, label);
	string = g_string_new (markup);
	if (marked_str)
		g_string_append (string, marked_str);
	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (label));

	if (pango_parse_markup (string->str, -1, 0,
				&attr_list, &text,
				NULL, NULL)) {
		go_create_std_tags_for_buffer (buffer);
		gtk_text_buffer_set_text (buffer, text, -1);
		gnm_load_pango_attributes_into_buffer (attr_list, buffer, text);
		g_free (text);
		pango_attr_list_unref (attr_list);
	} else
		gtk_text_buffer_set_text (buffer, string->str, -1);
	g_free (markup);
	g_string_free (string, TRUE);

	if (set_tabs) {
		PangoTabArray *tabs;
		tabs = pango_tab_array_new_with_positions
			(5, TRUE,
			 PANGO_TAB_LEFT, 20,
			 PANGO_TAB_LEFT, 140,
			 PANGO_TAB_LEFT, 160,
			 PANGO_TAB_LEFT, 180,
			 PANGO_TAB_LEFT, 200);
		gtk_text_view_set_tabs (GTK_TEXT_VIEW (label), tabs);
		pango_tab_array_free (tabs);
	}

	gdkw = gtk_widget_get_window (GTK_WIDGET (gee->entry));
	gdk_window_get_origin (gdkw, &root_x, &root_y);
	gtk_widget_get_allocation (GTK_WIDGET (gee->entry), &allocation);

	gtk_window_move (GTK_WINDOW (tip),
			 root_x + allocation.x,
			 root_y + allocation.y + allocation.height);

	return tip;
}

static void
gee_set_tooltip_argument (GString *str, char *arg, gboolean optional)
{
	if (optional)
		g_string_append_c (str, '[');
	g_string_append (str, arg);
	if (optional)
		g_string_append_c (str, ']');
}

static void
gee_set_tooltip (GnmExprEntry *gee, GnmFunc *fd, gint args, gboolean had_stuff)
{
	GString *str;
	gchar sep = go_locale_get_arg_sep ();
	gint min, max, i;
	gboolean first = TRUE;
	char *extra = NULL;
	gboolean localized_function_names = gee->sheet->convs->localized_function_names;
	const char *fdname;

	gnm_func_load_if_stub (fd);
	gnm_func_count_args (fd, &min, &max);

	if ((gee->tooltip.fd)
	    && (gee->tooltip.fd == fd && gee->tooltip.args == args
		&& gee->tooltip.had_stuff == (max == 0 && args == 0 && had_stuff)))
			return;
	gee_delete_tooltip (gee, FALSE);

	gee->tooltip.fd = fd;
	gnm_func_inc_usage (gee->tooltip.fd);

	fdname = gnm_func_get_name (fd, localized_function_names);

	str = g_string_new (fdname);
	g_string_append_c (str, '(');

	for (i = 0; i < max; i++) {
		char *arg_name = gnm_func_get_arg_name
			(fd, i);
		if (arg_name != NULL) {
			if (first)
				first = FALSE;
			else
				g_string_append_c (str, sep);
			if (i == args) {
				extra = g_strdup_printf
					(_("%s: %s"),
					 arg_name,
					 gnm_func_get_arg_description (fd, i));
				g_string_append (str, UNICODE_RIGHT_TRIANGLE);
			}
			gee_set_tooltip_argument (str, arg_name, i >= min);
			if (i == args)
				g_string_append (str, UNICODE_LEFT_TRIANGLE);
			g_free (arg_name);
		} else
			break;
	}
	if (i < max) {
		if (!first)
			g_string_append_c (str, sep);
		g_string_append
			(str, (args >= i && args < max)
			 ? UNICODE_RIGHT_TRIANGLE UNICODE_ELLIPSIS UNICODE_LEFT_TRIANGLE
			 : UNICODE_ELLIPSIS);
	}
	if (max == 0 && args == 0 && !had_stuff) {
		extra = g_strdup_printf (_("%s takes no arguments"),
					 fdname);
	} else if (args >= max) {
		g_string_append (str, UNICODE_RIGHT_TRIANGLE UNICODE_CROSS_AND_SKULLBONES UNICODE_LEFT_TRIANGLE);
		extra = g_strdup_printf (_("Too many arguments for %s"),
					 fdname);
	}
	g_string_append_c (str, ')');
	if (extra) {
		g_string_append_c (str, '\n');
		g_string_append (str, extra);
		g_free (extra);
	}

	gee->tooltip.tooltip = gee_create_tooltip
		(gee, str->str, _("\n\n<i>Ctrl-F4 to close tooltip</i>"), FALSE);
	gtk_widget_show_all (gee->tooltip.tooltip);
	gee->tooltip.args = args;
	gee->tooltip.had_stuff = (max == 0 && args == 0 && had_stuff);

	g_string_free (str, TRUE);
}

static gboolean
gee_set_tooltip_completion (GnmExprEntry *gee, GSList *list, guint start, guint end)
{
	GString *str;
	GString *str_marked;
	gint i = 0;
	gint max = 10;
	GSList *list_c = list;
	gchar const *name = NULL;
	gboolean show_tool_tip, had_tool_tip;
	gboolean localized_function_names = gee->sheet->convs->localized_function_names;

	had_tool_tip = gee_delete_tooltip (gee, TRUE);

	str = g_string_new (NULL);
	for (; list_c != NULL && ++i < max; list_c = list_c->next) {
		GnmFunc *fd = list_c->data;
		name = gnm_func_get_name (fd, localized_function_names);
		if ((end - start) < (guint) g_utf8_strlen (name, -1))
			/* xgettext: the first %s is a function name and */
			/* the second %s the function description */
			g_string_append_printf (str, _("\t%s \t%s\n"), name,
						gnm_func_get_description (fd));
		else {
			/* xgettext: the first %s is a function name and */
			/* the second %s the function description */
			g_string_append_printf (str, _("\342\234\223\t%s \t%s\n"), name,
						gnm_func_get_description (fd));
			i--;
		}
	}

	str_marked = g_string_new (NULL);
	if (i == max)
		g_string_append (str_marked, "\t" UNICODE_ELLIPSIS_VERT "\n");
	if (i == 1) {
		gee->tooltip.completion
			= g_strdup (name);
		/*xgettext: short form for: "type F4-key to complete the name"*/
		g_string_append (str_marked, _("\n\t<i>F4 to complete</i>"));
	} else if (i > 1)
		/*xgettext: short form for: "type shift-F4-keys to select the completion"*/
		g_string_append (str_marked, _("\n\t<i>\342\207\247F4 to select</i>"));
	else
		g_string_truncate (str, str->len - 1);
	gee->tooltip.completion_start = start;
	gee->tooltip.completion_end = end;
	gee->tooltip.completion_se_valid = TRUE;
	show_tool_tip = gnm_conf_get_core_gui_editing_function_name_tooltips ();
	if (show_tool_tip) {
		gee->tooltip.tooltip = gee_create_tooltip
			(gee, str->str, str_marked->str, TRUE);
		if (had_tool_tip)
			gtk_widget_show_all (gee->tooltip.tooltip);
		else
			gee->tooltip.timerid = g_timeout_add_full
				(G_PRIORITY_DEFAULT, 750,
				 cb_show_tooltip,
				 gee,
				 NULL);
	}
	g_string_free (str, TRUE);
	g_string_free (str_marked, TRUE);
	g_slist_free_full (list, (GDestroyNotify) gnm_func_dec_usage);
	return show_tool_tip;
}

static void
gee_dump_lexer (GnmLexerItem *gli) {
	g_printerr ("************\n");
	do {
		g_printerr ("%2" G_GSIZE_FORMAT " to %2" G_GSIZE_FORMAT ": %d\n",
			    gli->start, gli->end, gli->token);
	} while (gli++->token != 0);
	g_printerr ("************\n");

}

static gint
func_def_cmp (gconstpointer a_, gconstpointer b_, gpointer user)
{
	GnmFunc const * const a = (GnmFunc const * const)a_;
	GnmFunc const * const b = (GnmFunc const * const)b_;
	GnmExprEntry *gee = user;
	gboolean localized = gee->sheet->convs->localized_function_names;

	return g_utf8_collate (gnm_func_get_name (a, localized),
			       gnm_func_get_name (b, localized));
}


static void
gee_update_lexer_items (GnmExprEntry *gee)
{
	GtkEditable *editable = GTK_EDITABLE (gee->entry);
	char *str = gtk_editable_get_chars (editable, 0, -1);
	Sheet *sheet = scg_sheet (gee->scg);
	GOFormat const *format;
	gboolean forced_text;

	g_free (gee->lexer_items);
	gee->lexer_items = NULL;

	if (gee->texpr != NULL) {
		gnm_expr_top_unref (gee->texpr);
		gee->texpr = NULL;
	}

	parse_pos_init_editpos (&gee->pp, scg_view (gee->scg));
	format = gnm_style_get_format
		(sheet_style_get (sheet, gee->pp.eval.col, gee->pp.eval.row));
	forced_text = ((format != NULL) && go_format_is_text (format));

	if (!gee->feedback_disabled && !forced_text) {
		gee->texpr = gnm_expr_parse_str
			((str[0] == '=') ? str+1 : str,
			 &gee->pp, GNM_EXPR_PARSE_DEFAULT
			 | GNM_EXPR_PARSE_UNKNOWN_NAMES_ARE_STRINGS,
			 sheet_get_conventions (sheet), NULL);
	}

	gee->tooltip.is_expr =  (!forced_text) &&
		(NULL != gnm_expr_char_start_p (str));
	if (!(gee->flags & GNM_EE_SINGLE_RANGE)) {
		gee->lexer_items = gnm_expr_lex_all
			(str, &gee->pp,
			 GNM_EXPR_PARSE_UNKNOWN_NAMES_ARE_STRINGS,
			 NULL);
		if (gnm_debug_flag ("functooltip"))
			gee_dump_lexer (gee->lexer_items);
	}
	g_free (str);
}

static GnmLexerItem *
gee_duplicate_lexer_items (GnmLexerItem *gli)
{
	int n = 1;
	GnmLexerItem *gli_c = gli;

	while (gli_c->token != 0) {
		gli_c++;
		n++;
	}

	return go_memdup_n (gli, n, sizeof (GnmLexerItem));
}

static void
gee_check_tooltip (GnmExprEntry *gee)
{
	GtkEditable *editable = GTK_EDITABLE (gee->entry);
	gint  end, args = 0;
	guint end_t;
	char *str;
	gboolean stuff = FALSE, completion_se_set = FALSE;
	GnmLexerItem *gli, *gli_c;
	int last_token = 0;

	if (gee->lexer_items == NULL || !gee->tooltip.enabled ||
	    (!gee->tooltip.is_expr && !gee->is_cell_renderer)) {
		gee_delete_tooltip (gee, TRUE);
		return;
	}

	end = gtk_editable_get_position (editable);

	if (end == 0) {
		gee_delete_tooltip (gee, TRUE);
		return;
	}

	str = gtk_editable_get_chars (editable, 0, -1);
	end_t = g_utf8_offset_to_pointer (str, end) - str;


	gli_c = gli = gee_duplicate_lexer_items (gee->lexer_items);

	/*
	 * If we have an open string at the end of the entry, we
	 * need to adjust.
	 */

	for (; gli->token != 0; gli++) {
		if (gli->start >= end_t) {
			gli->token = 0;
			break;
		}
		if (gli->token != TOKEN_UNMATCHED_APOSTROPHE)
			continue;
		if (gli->start == 0)
			goto not_found;
		gli->token = 0;
		stuff = TRUE;
		break;
	}
	if (gli > gli_c)
		gli--;
	if (gli > gli_c)
		last_token = (gli - 1)->token;

	/* This creates the completion tooltip */
	if (!stuff &&
	    gli->token == STRING &&
	    last_token != CONSTANT &&
	    last_token != '$') {
		guint start_t = gli->start;
		char *prefix;
		GSList *list;

		end_t = gli->end;
		prefix = g_strndup (str + start_t, end_t - start_t);
		list = gnm_func_lookup_prefix
			(prefix, gee->sheet->workbook,
			 gee_convs (gee)->localized_function_names);
		g_free (prefix);
		if (list != NULL) {
			list = g_slist_sort_with_data
				(list,
				 func_def_cmp,
				 gee);
			if (gee_set_tooltip_completion
			    (gee, list, start_t, end_t)) {
				g_free (str);
				g_free (gli_c);
				return;
			}
		} else {
			g_free (gee->tooltip.completion);
			gee->tooltip.completion = NULL;
			gee->tooltip.completion_start = start_t;
			gee->tooltip.completion_end = end_t;
			gee->tooltip.completion_se_valid = TRUE;
		}
		completion_se_set = TRUE;
	} else {
		g_free (gee->tooltip.completion);
		gee->tooltip.completion = NULL;
		gee->tooltip.completion_se_valid = FALSE;
	}


	if (!gnm_conf_get_core_gui_editing_function_argument_tooltips ())
		goto not_found;

	if (gnm_debug_flag ("functooltip"))
		g_printerr ("Last token considered is %d from %2"
			    G_GSIZE_FORMAT " to %2" G_GSIZE_FORMAT ".\n",
			    gli->token, gli->start, gli->end);


	while (gli->start > 1) {
		switch (gli->token) {
		case '(':
			if ((gli - 1)->token == STRING) {
				gint start_t = (gli - 1)->start;
				gint end_t = (gli - 1)->end;
				char *name = g_strndup (str + start_t,
							end_t - start_t);
				GnmFunc	*fd = gee_convs (gee)->localized_function_names
					? gnm_func_lookup_localized (name, NULL)
					: gnm_func_lookup (name, NULL);
				g_free (name);
				if (fd != NULL) {
					gee_set_tooltip (gee, fd, args, stuff);
					g_free (str);
					g_free (gli_c);
					return;
				}
			}
			stuff = TRUE;
			args = 0;
			break;
		case '{':
			stuff = (args == 0);
			args = 0;
			break;
		case ')': {
			gint para = 1;
			gli--;
			while (gli->start > 1 && para > 0) {
				switch (gli->token) {
				case ')':
					para++;
					break;
				case '(':
					para--;
					break;
				default:
					break;
				}
				gli--;
			}
			gli++;
			stuff = (args == 0);
			break;
		}
		case '}': {
			gint para = 1;
			gli--;
			while (gli->start > 1 && para > 0) {
				switch (gli->token) {
				case '}':
					para++;
					break;
				case '{':
					para--;
					break;
				default:
					break;
				}
				gli--;
			}
			gli++;
			stuff = (args == 0);
			break;
		}
		case ARG_SEP:
			args++;
			break;
		default:
			stuff = (args == 0);
			break;
		}
		if (gli->start > 1)
			gli--;
	}

 not_found:
	g_free (str);
	g_free (gli_c);
	gee_delete_tooltip (gee, !completion_se_set);
	return;
}

static gboolean
cb_gee_focus_out_event (G_GNUC_UNUSED GtkWidget     *widget,
			G_GNUC_UNUSED GdkEventFocus *event,
			gpointer                     user_data)
{
	gee_delete_tooltip (user_data, FALSE);
	return FALSE;
}

static void
cb_gee_notify_cursor_position (GnmExprEntry *gee)
{
	gee_update_env (gee);
	gee_check_tooltip (gee);
}

static void
cb_entry_changed (GnmExprEntry *gee)
{
	gee_update_lexer_items (gee);
	gee_update_env (gee);
	gee_update_calendar (gee);
	gee_check_tooltip (gee);
	g_signal_emit (G_OBJECT (gee), signals[CHANGED], 0);
}

static gboolean
cb_gee_key_press_event (GtkEntry	*entry,
			GdkEventKey	*event,
			GnmExprEntry	*gee)
{
	WBCGtk *wbcg  = gee->wbcg;
	gboolean is_enter = FALSE;
	int state = gnm_filter_modifiers (event->state);

	switch (event->keyval) {
	case GDK_KEY_Up:	case GDK_KEY_KP_Up:
	case GDK_KEY_Down:	case GDK_KEY_KP_Down:
		if (gee->is_cell_renderer)
			return FALSE;
		/* Ignore these keys */
		return TRUE;
		/* GDK_KEY_F2 starts editing */
		/* GDK_KEY_F3 opens the paste names dialog */
	case GDK_KEY_F4: {
		/* Cycle absolute reference mode through the sequence rel/rel,
		 * abs/abs, rel/abs, abs/rel and back to rel/rel. Update text
		 * displayed in entry.
		 */
		/* Shift F4 provides the paste names dialog based on the current name */
		/* Control F4 closes the tooltips */
		Rangesel *rs = &gee->rangesel;
		gboolean c, r;

		if (state == GDK_SHIFT_MASK) {
			if (gee->tooltip.completion_se_valid)
				dialog_function_select_paste
					(gee->wbcg,
					 gee->tooltip.completion_start,
					 gee->tooltip.completion_end);
			else
				dialog_function_select_paste
					(gee->wbcg, -1, -1);
			return TRUE;
		}
		if (state == GDK_CONTROL_MASK) {
			gnm_expr_entry_close_tips (gee);
			return TRUE;
		}

		if (gee->tooltip.completion != NULL) {
			guint start = gee->tooltip.completion_start;
			guint end = gee->tooltip.completion_end;
			gint new_start = (gint) start;
			GtkEditable *editable = GTK_EDITABLE (gee->entry);

			gtk_editable_insert_text (editable,
						  gee->tooltip.completion,
						  strlen (gee->tooltip.completion),
						  &new_start);
			gtk_editable_delete_text (editable, new_start,
						  end + new_start - start);
			gtk_editable_set_position (editable, new_start);
			return TRUE;
		}

		/* FIXME: since the range can't have changed we should just be able to */
		/*        look it up rather than reparse */

		/* Look for a range */
		if (!rs->is_valid || rs->text_start >= rs->text_end)
			gnm_expr_entry_find_range (gee);

		/* no range found */
		if (!rs->is_valid || rs->text_start >= rs->text_end)
			return TRUE;

		if ((GNM_EE_FORCE_ABS_REF | GNM_EE_FORCE_REL_REF) & gee->flags)
			return TRUE;

		c = rs->ref.a.col_relative;
		r = rs->ref.a.row_relative;
		gnm_cellref_set_col_ar (&rs->ref.a, &gee->pp, !c);
		gnm_cellref_set_col_ar (&rs->ref.b, &gee->pp, !c);
		gnm_cellref_set_row_ar (&rs->ref.a, &gee->pp, c^r);
		gnm_cellref_set_row_ar (&rs->ref.b, &gee->pp, c^r);

		gee_rangesel_update_text (gee);

		return TRUE;
	}

	case GDK_KEY_Escape:
		if (gee->is_cell_renderer) {
			gtk_entry_set_editing_cancelled (entry, TRUE);
			gtk_cell_editable_editing_done (GTK_CELL_EDITABLE (gee));
			gtk_cell_editable_remove_widget (GTK_CELL_EDITABLE (gee));
			return TRUE;
		} else
			wbcg_edit_finish (wbcg, WBC_EDIT_REJECT, NULL);
		return TRUE;

	case GDK_KEY_KP_Enter:
	case GDK_KEY_Return:
		if (gee->is_cell_renderer)
			return FALSE;
		/* Is this the right way to append a newline ?? */
		if (state == GDK_MOD1_MASK) {
			gint pos = gtk_editable_get_position (GTK_EDITABLE (entry));
			gtk_editable_insert_text (GTK_EDITABLE (entry), "\n", 1, &pos);
			gtk_editable_set_position (GTK_EDITABLE (entry), pos);
			return TRUE;
		}

		/* Ctrl-enter is only applicable for the main entry */
		if (!wbcg_is_editing (wbcg))
			break;

		is_enter = TRUE;
		/* fall through */

	case GDK_KEY_Tab:
	case GDK_KEY_ISO_Left_Tab:
	case GDK_KEY_KP_Tab:
		/* Tab is only applicable for the main entry */
		 if (gee->is_cell_renderer || !wbcg_is_editing (wbcg))
			break;
		{
		SheetView *sv;
		WBCEditResult result;

		if (is_enter && (state & GDK_CONTROL_MASK))
			result = (state & GDK_SHIFT_MASK) ? WBC_EDIT_ACCEPT_ARRAY : WBC_EDIT_ACCEPT_RANGE;
		else
			result = WBC_EDIT_ACCEPT;

		/* Be careful to restore the editing sheet if we are editing */
		sv = sheet_get_view (wbcg->editing_sheet,
			wb_control_view (GNM_WBC (wbcg)));

		/* move the edit pos for normal entry */
		if (wbcg_edit_finish (wbcg, result, NULL) && result == WBC_EDIT_ACCEPT) {
			GODirection dir = gnm_conf_get_core_gui_editing_enter_moves_dir ();
			if (!is_enter || dir != GO_DIRECTION_NONE) {
				gboolean forward = TRUE;
				gboolean horizontal = TRUE;
				if (is_enter) {
					horizontal = go_direction_is_horizontal (dir);
					forward = go_direction_is_forward (dir);
				}

				if (event->state & GDK_SHIFT_MASK)
					forward = !forward;

				sv_selection_walk_step (sv, forward, horizontal);

				/* invalidate, in case Enter direction changes */
				if (is_enter)
					sv->first_tab_col = -1;
				gnm_sheet_view_update (sv);
			}
		}
		return TRUE;
	}

	case GDK_KEY_KP_Separator:
	case GDK_KEY_KP_Decimal: {
		GtkEditable *editable = GTK_EDITABLE (entry);
		gint start, end, l;
		GString const* s = go_locale_get_decimal ();
		gchar const* decimal = s->str;
		l = s->len;
		gtk_editable_get_selection_bounds (editable, &start, &end);
		gtk_editable_delete_text (editable, start, end);
		gtk_editable_insert_text (editable, decimal, l, &start);
		gtk_editable_set_position (editable, start);
		return TRUE;
	}

	case GDK_KEY_F9: {
		/* Replace selection by its evaluated result.  */
		GtkEditable *editable = GTK_EDITABLE (entry);
		gint start, end;
		char *str;
		GnmExprTop const *texpr;
		Sheet *sheet = gee->pp.sheet;

		gtk_editable_get_selection_bounds (editable, &start, &end);
		if (end <= start)
			return FALSE;
		str = gtk_editable_get_chars (editable, start, end);

		texpr = gnm_expr_parse_str (str, &gee->pp,
					    GNM_EXPR_PARSE_DEFAULT,
					    gee_convs (gee),
					    NULL);
		if (texpr) {
			GnmValue *v;
			GnmEvalPos ep;
			char *cst;
			GnmExpr const *expr;

			eval_pos_init_pos (&ep, sheet, &gee->pp.eval);
			v = gnm_expr_top_eval (texpr, &ep, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
			gnm_expr_top_unref (texpr);

			/*
			 * Turn the value into an expression so we get
			 * the right syntax.
			 */
			expr = gnm_expr_new_constant (v);
			cst = gnm_expr_as_string (expr, &gee->pp,
						  gee_convs (gee));
			gnm_expr_free (expr);

			gtk_editable_delete_text (editable, start, end);
			gtk_editable_insert_text (editable, cst, -1, &start);
			gtk_editable_set_position (editable, start);

			g_free (cst);
		}

		g_free (str);
		return TRUE;
	}

	default:
		break;
	}

	return FALSE;
}

static gboolean
cb_gee_button_press_event (G_GNUC_UNUSED GtkEntry *entry,
			   G_GNUC_UNUSED GdkEventButton *event,
			   GnmExprEntry *gee)
{
	g_return_val_if_fail (GNM_EXPR_ENTRY_IS (gee), FALSE);

	if (gee->scg) {
		scg_rangesel_stop (gee->scg, FALSE);
		gnm_expr_entry_find_range (gee);
		g_signal_emit (G_OBJECT (gee), signals[CHANGED], 0);
	}

	return FALSE;
}

static gboolean
gee_mnemonic_activate (GtkWidget *w, G_GNUC_UNUSED gboolean group_cycling)
{
	GnmExprEntry *gee = GNM_EXPR_ENTRY (w);
	gtk_widget_grab_focus (GTK_WIDGET (gee->entry));
	return TRUE;
}

static void
gee_init (GnmExprEntry *gee)
{
	gee->editing_canceled = FALSE;
	gee->is_cell_renderer = FALSE;
	gee->ignore_changes = FALSE;
	gee->flags = 0;
	gee->scg = NULL;
	gee->sheet = NULL;
	gee->wbcg = NULL;
	gee->freeze_count = 0;
	gee->update_timeout_id = 0;
	gee->update_policy = GNM_UPDATE_CONTINUOUS;
	gee->feedback_disabled = FALSE;
	gee->lexer_items = NULL;
	gee->texpr = NULL;
	gee->tooltip.tooltip = NULL;
	gee->tooltip.fd = NULL;
	gee->tooltip.handlerid = 0;
	gee->tooltip.enabled = TRUE;
	gee_rangesel_reset (gee);

	gee->entry = GTK_ENTRY (gtk_entry_new ());

	/* Disable selecting the entire content when the widget gets focus */
	g_object_set (gtk_widget_get_settings (GTK_WIDGET (gee->entry)),
		      "gtk-entry-select-on-focus", FALSE,
		      NULL);

	g_signal_connect_swapped (G_OBJECT (gee->entry), "activate",
		G_CALLBACK (cb_entry_activate), gee);
	g_signal_connect_swapped (G_OBJECT (gee->entry), "changed",
		G_CALLBACK (cb_entry_changed), gee);
	g_signal_connect (G_OBJECT (gee->entry), "key_press_event",
		G_CALLBACK (cb_gee_key_press_event), gee);
	g_signal_connect (G_OBJECT (gee->entry), "button_press_event",
		G_CALLBACK (cb_gee_button_press_event), gee);
	g_signal_connect_swapped (G_OBJECT (gee->entry), "notify::cursor-position",
		G_CALLBACK (cb_gee_notify_cursor_position), gee);
	gtk_box_pack_start (GTK_BOX (gee), GTK_WIDGET (gee->entry),
		TRUE, TRUE, 0);
	gtk_widget_show (GTK_WIDGET (gee->entry));
}

static void
gee_finalize (GObject *obj)
{
	GnmExprEntry *gee = (GnmExprEntry *)obj;

	go_format_unref (gee->constant_format);
	gee_delete_tooltip (gee, TRUE);
	g_free (gee->lexer_items);
	if (gee->texpr != NULL)
		gnm_expr_top_unref (gee->texpr);

	((GObjectClass *)parent_class)->finalize (obj);
}

static void
gee_set_value_double (GogDataEditor *editor, double val,
		      GODateConventions const *date_conv)
{
	GnmExprEntry *gee = GNM_EXPR_ENTRY (editor);
	GnmValue *v = value_new_float (val);
	char *txt = format_value (gee->constant_format, v, -1, date_conv);

	value_release (v);

	if (*txt == 0) {
		g_free (txt);
		txt = g_strdup_printf ("%g", val);
	}

	if (gee_debug)
		g_printerr ("Setting text %s\n", txt);

	g_object_set (G_OBJECT (editor), "text", txt, NULL);

	g_free (txt);
}

static void
gee_data_editor_set_format (GogDataEditor *deditor, GOFormat const *fmt)
{
	GnmExprEntry *gee = (GnmExprEntry *)deditor;
	GnmValue *v;
	GODateConventions const *date_conv =
		sheet_date_conv (gee->sheet);

	if (fmt == gee->constant_format)
		return;

	v = get_matched_value (gee);

	gee_set_format (gee, fmt);

	if (v && VALUE_IS_FLOAT (v)) {
		char *txt = format_value (gee->constant_format, v,
					  -1, date_conv);
		gtk_entry_set_text (gee->entry, txt);
		g_free (txt);
	}

	value_release (v);
}

static void
gee_go_plot_data_editor_init (GogDataEditorClass *iface)
{
	iface->set_format = gee_data_editor_set_format;
	iface->set_value_double = gee_set_value_double;
}


static void
gee_class_init (GObjectClass *gobject_class)
{
	GtkWidgetClass *widget_class = (GtkWidgetClass *)gobject_class;

	parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->set_property	= gee_set_property;
	gobject_class->get_property	= gee_get_property;
	gobject_class->finalize		= gee_finalize;
	widget_class->destroy		= gee_destroy;
	widget_class->mnemonic_activate = gee_mnemonic_activate;

	signals[UPDATE] = g_signal_new ("update",
		GNM_EXPR_ENTRY_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmExprEntryClass, update),
		NULL, NULL,
		g_cclosure_marshal_VOID__BOOLEAN,
		G_TYPE_NONE,
		1, G_TYPE_BOOLEAN);
	signals[CHANGED] = g_signal_new ("changed",
		GNM_EXPR_ENTRY_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (GnmExprEntryClass, changed),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);
	signals[ACTIVATE] =
		g_signal_new ("activate",
		G_OBJECT_CLASS_TYPE (gobject_class),
		G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		G_STRUCT_OFFSET (GnmExprEntryClass, activate),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);


	g_object_class_override_property
		(gobject_class, PROP_EDITING_CANCELED, "editing-canceled");

	g_object_class_install_property
		(gobject_class, PROP_UPDATE_POLICY,
		 g_param_spec_enum ("update-policy",
				    P_("Update policy"),
				    P_("How frequently changes to the entry should be applied"),
				    GNM_TYPE_UPDATE_TYPE, GNM_UPDATE_CONTINUOUS,
				    GSF_PARAM_STATIC | G_PARAM_READWRITE));

	g_object_class_install_property
		(gobject_class, PROP_WITH_ICON,
		 g_param_spec_boolean ("with-icon",
				       P_("With icon"),
				       P_("Should there be an icon to the right of the entry?"),
				       TRUE,
				       GSF_PARAM_STATIC | G_PARAM_READWRITE));

	g_object_class_install_property
		(gobject_class, PROP_TEXT,
		 g_param_spec_string ("text",
				      P_("Text"),
				      P_("The contents of the entry"),
				      "",
				      GSF_PARAM_STATIC | G_PARAM_READWRITE));

	g_object_class_install_property
		(gobject_class, PROP_FLAGS,
		 g_param_spec_uint ("flags", NULL, NULL,
				    0, GNM_EE_MASK, 0,
				    GSF_PARAM_STATIC | G_PARAM_READWRITE));

	g_object_class_install_property
		(gobject_class, PROP_SCG,
		 g_param_spec_object ("scg",
				      P_("SheetControlGUI"),
				      P_("The GUI container associated with the entry."),
				      GNM_SCG_TYPE,
				      GSF_PARAM_STATIC | G_PARAM_READWRITE));

	g_object_class_install_property
		(gobject_class, PROP_WBCG,
		 g_param_spec_object ("wbcg",
				      P_("WBCGtk"),
				      P_("The toplevel GUI container associated with the entry."),
				      GNM_WBC_GTK_TYPE,
				      GSF_PARAM_STATIC | G_PARAM_READWRITE));

	g_object_class_install_property
		(gobject_class, PROP_CONSTANT_FORMAT,
		 g_param_spec_boxed ("constant-format",
				     P_("Constant Format"),
				     P_("Format for constants"),
				     go_format_get_type (),
				     GSF_PARAM_STATIC | G_PARAM_READWRITE));

	gee_debug = gnm_debug_flag ("gee");
}

/***************************************************************************/

static void
gee_editable_start_editing (GtkCellEditable *cell_editable,
			    G_GNUC_UNUSED GdkEvent *event)
{
	GtkEntry *entry = gnm_expr_entry_get_entry (GNM_EXPR_ENTRY (cell_editable));
	GNM_EXPR_ENTRY (cell_editable)->is_cell_renderer = TRUE;
	g_signal_connect_swapped (G_OBJECT (entry), "activate",
		G_CALLBACK (gtk_cell_editable_editing_done), cell_editable);
	gtk_widget_grab_focus (GTK_WIDGET (entry));
}

static void
gee_cell_editable_init (GtkCellEditableIface *iface)
{
	iface->start_editing = gee_editable_start_editing;
}
/***************************************************************************/

GSF_CLASS_FULL (GnmExprEntry, gnm_expr_entry,
		NULL, NULL, gee_class_init, NULL,
		gee_init, GTK_TYPE_BOX, 0,
		GSF_INTERFACE (gee_cell_editable_init, GTK_TYPE_CELL_EDITABLE);
		GSF_INTERFACE (gee_go_plot_data_editor_init, GOG_TYPE_DATA_EDITOR))

/**
 * gee_prepare_range :
 * @gee:
 * @dst:
 *
 * Adjust @dst as necessary to conform to @gee's requirements
 * Produces the _logical_ range, a merge is displayed as only the topleft.
 **/
static void
gee_prepare_range (GnmExprEntry const *gee, GnmRangeRef *dst)
{
	Rangesel const *rs = &gee->rangesel;

	*dst = rs->ref;

	if (dst->a.sheet == NULL && !(gee->flags & GNM_EE_SHEET_OPTIONAL))
		dst->a.sheet = gee->sheet;
	if (gee->flags & GNM_EE_FULL_ROW) {
		dst->a.col = 0;
		dst->b.col = gnm_sheet_get_last_col (gee->sheet);
	}
	if (gee->flags & GNM_EE_FULL_COL) {
		dst->a.row = 0;
		dst->b.row = gnm_sheet_get_last_row (gee->sheet);
	}

	/* special case a single merge to be only corner */
	if (!(gee->flags & (GNM_EE_FULL_ROW|GNM_EE_FULL_COL))) {
		GnmEvalPos ep;
		GnmRange r;
		GnmRange const *merge;
		Sheet *start_sheet, *end_sheet;
		gnm_rangeref_normalize(dst,
			eval_pos_init_pos (&ep, gee->sheet, &gee->pp.eval),
			&start_sheet, &end_sheet,
			&r);
		merge = gnm_sheet_merge_is_corner (gee->sheet, &r.start);
		if (merge != NULL && range_equal (merge, &r))
			dst->b = dst->a;
	}
}

static char *
gee_rangesel_make_text (GnmExprEntry const *gee)
{
	GnmRangeRef ref;
	GnmConventionsOut out;

	gee_prepare_range (gee, &ref);

	out.accum = g_string_new (NULL);
	out.pp    = &gee->pp;
	out.convs = gee_convs (gee);
	rangeref_as_string (&out, &ref);
	return g_string_free (out.accum, FALSE);
}

static void
gee_rangesel_update_text (GnmExprEntry *gee)
{
	GtkEditable *editable = GTK_EDITABLE (gee->entry);
	Rangesel *rs = &gee->rangesel;
	int len;
	char *text = gee_rangesel_make_text (gee);

	g_return_if_fail (!gee->ignore_changes);

	gee->ignore_changes = TRUE;
	if (rs->text_end > rs->text_start) {
		if (text == NULL)
			gtk_editable_delete_text (editable,
						  rs->text_start,
						  rs->text_end);
		else
			/* We don't call gtk_editable_delete_text since we don't want */
			/* to emit a signal yet */
			GTK_EDITABLE_GET_IFACE (gee->entry)->delete_text (editable,
									  rs->text_start,
									  rs->text_end);
		rs->text_end = rs->text_start;
		gtk_editable_set_position (GTK_EDITABLE (gee->entry), rs->text_end);
	} else
		rs->text_start = rs->text_end =
			gtk_editable_get_position (GTK_EDITABLE (gee->entry));

	if (text != NULL) {
		/* Set the cursor at the end.  It looks nicer */
		len = strlen (text);

		gtk_editable_insert_text (editable, text, len, &rs->text_end);
		gtk_editable_set_position (editable, rs->text_end);
		g_free (text);
	}

	gee->ignore_changes = FALSE;
}

static void
gee_find_lexer_token (GnmLexerItem const *gli, guint token_pos,
		      GnmLexerItem const **gli_before, GnmLexerItem const **gli_after)
{
	*gli_before = *gli_after = NULL;
	if (gli->token == 0)
		return;
	if (gli->start == token_pos) {
		*gli_after = gli;
		return;
	}
	while (gli->token != 0) {
		if (gli->start < token_pos && token_pos < gli->end) {
			*gli_before = *gli_after = gli;
			return;
		}
		if (gli->start == token_pos) {
			*gli_before = gli - 1;
			*gli_after = gli;
			return;
		}
		if (gli->end == token_pos) {
			*gli_before = gli;
			*gli_after = ((gli + 1)->token != 0) ? (gli + 1) : NULL;
			return;
		}
		gli++;
	}
	*gli_before = gli - 1;
	return;
}

/**
 * gnm_expr_entry_find_range:
 * @gee:   a #GnmExprEntry
 *
 * Look at the current selection to see how much of it needs to be changed when
 * selecting a range.
 **/
gboolean
gnm_expr_entry_find_range (GnmExprEntry *gee)
{
	gboolean  single, formula_only;
	char const *text, *cursor, *tmp, *ptr;
	char *rs_text;
	GnmRangeRef  range;
	Rangesel *rs;
	int len, token_pos;
	GnmLexerItem const *gli, *gli_before, *gli_after;

	g_return_val_if_fail (gee != NULL, FALSE);

	single = (gee->flags & GNM_EE_SINGLE_RANGE) != 0;
	rs = &gee->rangesel;
	memset (rs, 0, sizeof (*rs));
	rs->ref.a.col_relative = rs->ref.a.row_relative = TRUE;
	rs->ref.b.col_relative = rs->ref.b.row_relative = TRUE;
	gee_force_abs_rel (gee);

	text = gtk_entry_get_text (gee->entry);
	if (text == NULL)
		return TRUE;

	formula_only = (gee->flags & GNM_EE_FORMULA_ONLY) != 0;
	if (formula_only && !gnm_expr_char_start_p (text))
		return FALSE;

	len = g_utf8_strlen (text, -1);

	if (single) {
		GnmRangeRef range;
		rs->text_start = 0;
		rs->text_end = len;
		tmp = rangeref_parse (&range, text, &gee->pp, gee_convs (gee));
		if (tmp != text) {
			rs->is_valid = TRUE;
			rs->ref = range;
		}
		return TRUE;
	}

	cursor = g_utf8_offset_to_pointer
		(text, gtk_editable_get_position (GTK_EDITABLE (gee->entry)));

	ptr = gnm_expr_char_start_p (text);
	if (ptr == NULL)
		ptr = text;

	if (gnm_debug_flag ("rangeselection"))
		g_printerr ("text: >%s< -- cursor: >%s<\n", text, cursor);

	if (ptr[0] == '\0') {
		rs->text_end = rs->text_start =
			g_utf8_pointer_to_offset
			(text, ptr);
		return TRUE;
	}

	if (gee->lexer_items == NULL)
		gee_update_lexer_items (gee);
	g_return_val_if_fail (gee->lexer_items != NULL, FALSE);

	gli = gee->lexer_items;
	while (gli->token != 0 && gli->start < (guint) (ptr - text))
		gli++;

	if (gli->token == 0) {
		rs->text_start = g_utf8_pointer_to_offset
			(text, ptr);
		rs->text_end = len;
		return TRUE;
	}

	token_pos = cursor - text;

	gee_find_lexer_token (gli, (guint)token_pos, &gli_before, &gli_after);

	if (gnm_debug_flag ("rangeselection")) {
		g_printerr ("before: %p -- after: %p\n", gli_before, gli_after);
		if (gli_before)
			g_printerr ("before token: %d\n", gli_before->token);
		if (gli_after)
			g_printerr ("after token: %d\n", gli_after->token);
	}

	if (gli_before == NULL && gli_after == NULL)
		return FALSE;

	if (gli_before == gli_after) {
		if ((gli_after + 1)->token == '(' ||
		    (gli_after + 1)->token == '{')
			return FALSE;
		if (gli < gli_before &&
		    ((gli_before - 1)->token == ')' ||
		     (gli_before - 1)->token == '}'))
			return FALSE;
		rs->text_start = g_utf8_pointer_to_offset
			(text, text + gli_before->start);
		rs->text_end   = g_utf8_pointer_to_offset
			(text, text + gli_before->end);
	} else if (gli_before != NULL && gli_after != NULL) {
		switch (gli_before->token) {
		case STRING:
		case QUOTED_STRING:
		case CONSTANT:
		case RANGEREF:
		case INVALID_TOKEN:
			if (gli_after->token == '(' ||
			    gli_after->token == '{')
				return FALSE;
			rs->text_start = g_utf8_pointer_to_offset
				(text, text + gli_before->start);
			rs->text_end   = g_utf8_pointer_to_offset
				(text, text + gli_before->end);
			break;
		default:
			switch (gli_after->token) {
			case STRING:
			case QUOTED_STRING:
			case CONSTANT:
			case RANGEREF:
			case INVALID_TOKEN:
				rs->text_start = g_utf8_pointer_to_offset
					(text, text + gli_after->start);
				rs->text_end   = g_utf8_pointer_to_offset
					(text, text + gli_after->end);
				break;
			default:
				rs->text_start = g_utf8_pointer_to_offset
					(text, text + gli_before->end);
				rs->text_end   = g_utf8_pointer_to_offset
					(text, text + gli_after->start);
				break;
			}
		}
	} else if (gli_before == NULL)
		switch (gli_after->token) {
		case STRING:
		case QUOTED_STRING:
		case CONSTANT:
		case RANGEREF:
		case INVALID_TOKEN:
			if ((gli_after + 1)->token == '(' ||
			    (gli_after + 1)->token == '{')
				return FALSE;
			rs->text_start = g_utf8_pointer_to_offset
				(text, text + gli_after->start);
			rs->text_end   = g_utf8_pointer_to_offset
				(text, text + gli_after->end);
			break;
		default:
			rs->text_end = rs->text_start =
				g_utf8_pointer_to_offset
				(text, text + gli_after->start);
			break;
		}
	else switch (gli_before->token) {
		case STRING:
		case QUOTED_STRING:
		case CONSTANT:
		case RANGEREF:
		case INVALID_TOKEN:
			if (gli < gli_before &&
			    ((gli_before - 1)->token == ')' ||
			     (gli_before - 1)->token == '}'))
				return FALSE;
			rs->text_start = g_utf8_pointer_to_offset
				(text, text + gli_before->start);
			rs->text_end   = g_utf8_pointer_to_offset
				(text, text + gli_before->end);
			break;
		case ')':
		case '}':
			return FALSE;
		default:
			rs->text_end = rs->text_start =
				g_utf8_pointer_to_offset
				(text, text + gli_before->start);
			break;
		}

	if (gnm_debug_flag ("rangeselection"))
		g_printerr ("characters from %d to %d\n",
			    rs->text_start, rs->text_end);

	rs_text = gtk_editable_get_chars (GTK_EDITABLE (gee->entry),
					  rs->text_start, rs->text_end);
	tmp = rangeref_parse (&range, rs_text, &gee->pp, gee_convs (gee));
	g_free (rs_text);
	if (tmp != rs_text) {
		rs->is_valid = TRUE;
		rs->ref = range;
	}
	return TRUE;
}

/**
 * gnm_expr_entry_rangesel_stop:
 * @gee:   a #GnmExprEntry
 * @clear_string: clear string flag
 *
 * Perform the appropriate action when a range selection has been completed.
 **/
void
gnm_expr_entry_rangesel_stop (GnmExprEntry *gee,
			      gboolean clear_string)
{
	Rangesel *rs;

	g_return_if_fail (GNM_EXPR_ENTRY_IS (gee));

	rs = &gee->rangesel;
	if (clear_string && rs->text_end > rs->text_start)
		gtk_editable_delete_text (GTK_EDITABLE (gee->entry),
					  rs->text_start, rs->text_end);

	if (!(gee->flags & GNM_EE_SINGLE_RANGE) || clear_string)
		gee_rangesel_reset (gee);
}

/***************************************************************************/

static void
cb_scg_destroy (GnmExprEntry *gee, SheetControlGUI *scg)
{
	g_return_if_fail (scg == gee->scg);

	gee_rangesel_reset (gee);
	gee->scg = NULL;
	gee->sheet = NULL;
}

static void
gee_detach_scg (GnmExprEntry *gee)
{
	if (gee->scg != NULL) {
		g_object_weak_unref (G_OBJECT (gee->scg),
				     (GWeakNotify) cb_scg_destroy, gee);
		gee->scg = NULL;
		gee->sheet = NULL;
	}
}

/***************************************************************************/

typedef struct {
	GnmExprEntry *gee;
	gboolean user_requested;
} GEETimerClosure;

static gboolean
cb_gee_update_timeout (GEETimerClosure const *info)
{
	info->gee->update_timeout_id = 0;
	g_signal_emit (G_OBJECT (info->gee), signals[UPDATE], 0,
		       info->user_requested);
	return FALSE;
}

static void
gee_remove_update_timer (GnmExprEntry *gee)
{
	if (gee->update_timeout_id != 0) {
		g_source_remove (gee->update_timeout_id);
		gee->update_timeout_id = 0;
	}
}

static void
gee_reset_update_timer (GnmExprEntry *gee, gboolean user_requested)
{
	GEETimerClosure *dat = g_new (GEETimerClosure, 1);
	gee_remove_update_timer (gee);
	dat->gee = gee;
	dat->user_requested = user_requested;
	gee->update_timeout_id = g_timeout_add_full (G_PRIORITY_DEFAULT, 300,
		(GSourceFunc) cb_gee_update_timeout, dat, g_free);
}

/**
 * gnm_expr_entry_signal_update:
 * @gee:
 * @user_requested: is the update requested by the user (eg activation)
 *
 * Higher level operations know when they are logically complete and can notify
 * GnmExprEntry clients.  For example, button-up after a drag selection
 * indicates a logical end to the change and offers a good time to update.
 **/
void
gnm_expr_entry_signal_update (GnmExprEntry *gee, gboolean user_requested)
{
	gee_reset_update_timer (gee, user_requested);
}

/**
 * gnm_expr_entry_set_update_policy:
 * @gee: a #GnmExprEntry
 * @policy: update policy
 *
 * Sets the update policy for the expr-entry. #GNM_UPDATE_CONTINUOUS means that
 * anytime the entry's content changes, the update signal will be emitted.
 * #GNM_UPDATE_DELAYED means that the signal will be emitted after a brief
 * timeout when no changes occur, so updates are spaced by a short time rather
 * than continuous. #GNM_UPDATE_DISCONTINUOUS means that the signal will only
 * be emitted when the user releases the button and ends the rangeselection.
 *
 **/
void
gnm_expr_entry_set_update_policy (GnmExprEntry *gee,
				       GnmUpdateType  policy)
{
	g_return_if_fail (GNM_EXPR_ENTRY_IS (gee));

	if (gee->update_policy == policy)
		return;
	gee->update_policy = policy;
	g_object_notify (G_OBJECT (gee), "update-policy");
}

/**
 * gnm_expr_entry_new:
 * @wbcg: #WBCGtk
 * @with_icon: append a rollup icon to the end of the entry
 *
 * Creates a new #GnmExprEntry, which is an entry widget with support
 * for range selections.
 * The entry is created with default flag settings which are suitable for use
 * in many dialogs, but see #gnm_expr_entry_set_flags.
 *
 * Return value: a new #GnmExprEntry.
 **/
GnmExprEntry *
gnm_expr_entry_new (WBCGtk *wbcg, gboolean with_icon)
{
	return g_object_new (GNM_EXPR_ENTRY_TYPE,
			     "scg",	  wbcg_cur_scg (wbcg),
			     "with-icon", with_icon,
			     NULL);
}

void
gnm_expr_entry_freeze (GnmExprEntry *gee)
{
	g_return_if_fail (GNM_EXPR_ENTRY_IS (gee));

	gee->freeze_count++;
}

void
gnm_expr_entry_thaw (GnmExprEntry *gee)
{
	g_return_if_fail (GNM_EXPR_ENTRY_IS (gee));

	if (gee->freeze_count > 0 && (--gee->freeze_count) == 0) {
		gee_rangesel_update_text (gee);
		switch (gee->update_policy) {
		case GNM_UPDATE_DELAYED :
			gee_reset_update_timer (gee, FALSE);
			break;

		default :
		case GNM_UPDATE_DISCONTINUOUS :
			if (gee->scg->rangesel.active)
				break;
		case GNM_UPDATE_CONTINUOUS:
			g_signal_emit (G_OBJECT (gee), signals[UPDATE], 0, FALSE);
		}
	}
}

/**
 * gnm_expr_entry_set_flags:
 * @gee: a #GnmExprEntry
 * @flags:      bitmap of flag values
 * @mask:       bitmap with ones for flags to be changed
 *
 * Changes the flags specified in @mask to values given in @flags.
 *
 * Flags (%FALSE by default):
 * %GNM_EE_SINGLE_RANGE      Entry will only hold a single range.
 * %GNM_EE_ABS_COL           Column reference must be absolute.
 * %GNM_EE_ABS_ROW           Row reference must be absolute.
 * %GNM_EE_FULL_COL          GnmRange consists of full columns.
 * %GNM_EE_FULL_ROW          GnmRange consists of full rows.
 * %GNM_EE_SHEET_OPTIONAL    Current sheet name not auto-added.
 **/
void
gnm_expr_entry_set_flags (GnmExprEntry *gee,
			  GnmExprEntryFlags flags,
			  GnmExprEntryFlags mask)
{
	GnmExprEntryFlags newflags;
	g_return_if_fail (GNM_EXPR_ENTRY_IS (gee));

	newflags = (gee->flags & ~mask) | (flags & mask);
	if (gee->flags == newflags)
		return;

	gee->flags = newflags;
	gee_rangesel_reset (gee);
}

/**
 * gnm_expr_entry_set_scg:
 * @gee: a #GnmExprEntry
 * @scg: a #SheetControlGUI
 *
 * Associates the entry with a SheetControlGUI. The entry widget
 * automatically removes the association when the SheetControlGUI is
 * destroyed.
 **/
void
gnm_expr_entry_set_scg (GnmExprEntry *gee, SheetControlGUI *scg)
{
	g_return_if_fail (GNM_EXPR_ENTRY_IS (gee));
	g_return_if_fail (scg == NULL || GNM_IS_SCG (scg));

	if ((gee->flags & GNM_EE_SINGLE_RANGE) || scg != gee->scg)
		gee_rangesel_reset (gee);

	gee_detach_scg (gee);
	gee->scg = scg;
	if (scg) {
		g_object_weak_ref (G_OBJECT (gee->scg),
				   (GWeakNotify) cb_scg_destroy, gee);
		gee->sheet = sc_sheet (GNM_SHEET_CONTROL (scg));
		parse_pos_init_editpos (&gee->pp, scg_view (gee->scg));
		gee->wbcg = scg_wbcg (gee->scg);
	} else
		gee->sheet = NULL;

	if (gee_debug)
		g_printerr ("Setting gee (%p)->sheet = %s\n",
			    gee, gee->sheet->name_unquoted);
}

/**
 * gnm_expr_entry_get_scg:
 * @gee:
 *
 * Returns: (transfer none): the associated #SheetControlGUI.
 **/
SheetControlGUI *
gnm_expr_entry_get_scg (GnmExprEntry *gee)
{
	return gee->scg;
}

/**
 * gnm_expr_entry_load_from_text:
 * @gee:
 * @txt:
 */
void
gnm_expr_entry_load_from_text (GnmExprEntry *gee, char const *txt)
{
	g_return_if_fail (GNM_EXPR_ENTRY_IS (gee));
	/* We have nowhere to store the text while frozen. */
	g_return_if_fail (gee->freeze_count == 0);

	gee_rangesel_reset (gee);

	if (gee_debug)
		g_printerr ("Setting entry text: [%s]\n", txt);

	gtk_entry_set_text (gee->entry, txt);
	gee_delete_tooltip (gee, TRUE);
}

/**
 * gnm_expr_entry_load_from_dep:
 * @gee: a #GnmExprEntry
 * @dep: A dependent
 *
 * Sets the text of the entry, and removes saved information about earlier
 * range selections.
 **/
void
gnm_expr_entry_load_from_dep (GnmExprEntry *gee, GnmDependent const *dep)
{
	g_return_if_fail (GNM_EXPR_ENTRY_IS (gee));
	g_return_if_fail (dep != NULL);
	/* We have nowhere to store the text while frozen. */
	g_return_if_fail (gee->freeze_count == 0);

	if (dep->texpr != NULL) {
		char *text;
		GnmParsePos pp;

		parse_pos_init_dep (&pp, dep);
		text = gnm_expr_top_as_string (dep->texpr, &pp,
					       gee_convs (gee));

		gee_rangesel_reset (gee);
		gtk_entry_set_text (gee->entry, text);
		gee->rangesel.text_end = strlen (text);

		g_free (text);
		gee_delete_tooltip (gee, TRUE);
	} else
		gnm_expr_entry_load_from_text (gee, "");
}

/**
 * gnm_expr_entry_load_from_expr:
 * @gee: a #GnmExprEntry
 * @texpr: An expression
 * @pp: The parse position
 *
 * Sets the text of the entry, and removes saved information about earlier
 * range selections.
 **/
void
gnm_expr_entry_load_from_expr (GnmExprEntry *gee,
			       GnmExprTop const *texpr,
			       GnmParsePos const *pp)
{
	g_return_if_fail (GNM_EXPR_ENTRY_IS (gee));
	/* We have nowhere to store the text while frozen. */
	g_return_if_fail (gee->freeze_count == 0);

	if (texpr != NULL) {
		char *text = gnm_expr_top_as_string
			(texpr, pp, gee_convs (gee));
		gee_rangesel_reset (gee);
		if (gee_debug)
			g_printerr ("Setting entry text: [%s]\n", text);
		gtk_entry_set_text (gee->entry, text);
		gee->rangesel.text_end = strlen (text);
		g_free (text);
		gee_delete_tooltip (gee, TRUE);
	} else
		gnm_expr_entry_load_from_text (gee, "");
}

/**
 * gnm_expr_entry_load_from_range:
 * @gee: a #GnmExprEntry
 * @r:          a #GnmRange
 * @sheet:      a #sheet
 *
 * Returns: true if displayed range is different from input range. false
 * otherwise.
 *
 * Sets the range selection and displays it in the entry text. If the widget
 * already contains a range selection, the new text replaces the
 * old. Otherwise, it is inserted at @pos.
 **/
gboolean
gnm_expr_entry_load_from_range (GnmExprEntry *gee,
				Sheet *sheet, GnmRange const *r)
{
	Rangesel *rs;
	GnmRangeRef ref;
	gboolean needs_change = FALSE;

	g_return_val_if_fail (GNM_EXPR_ENTRY_IS (gee), FALSE);
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);
	g_return_val_if_fail (r != NULL, FALSE);

	needs_change =  (gee->flags & GNM_EE_FULL_COL &&
			 !range_is_full (r, sheet, TRUE)) ||
			(gee->flags & GNM_EE_FULL_ROW &&
			 !range_is_full (r, sheet, FALSE));

	rs = &gee->rangesel;
	ref = rs->ref;
	ref.a.col = r->start.col; if (rs->ref.a.col_relative) ref.a.col -= gee->pp.eval.col;
	ref.b.col = r->end.col;   if (rs->ref.b.col_relative) ref.b.col -= gee->pp.eval.col;
	ref.a.row = r->start.row; if (rs->ref.a.row_relative) ref.a.row -= gee->pp.eval.row;
	ref.b.row = r->end.row;   if (rs->ref.b.row_relative) ref.b.row -= gee->pp.eval.row;

	if (rs->ref.a.col == ref.a.col &&
	    rs->ref.b.col == ref.b.col &&
	    rs->ref.a.row == ref.a.row &&
	    rs->ref.b.row == ref.b.row &&
	    rs->ref.a.sheet == sheet &&
	    (rs->ref.b.sheet == NULL || rs->ref.b.sheet == sheet))
		return needs_change; /* FIXME ??? */

	rs->ref.a.col = ref.a.col;
	rs->ref.b.col = ref.b.col;
	rs->ref.a.row = ref.a.row;
	rs->ref.b.row = ref.b.row;
	rs->ref.a.sheet =
		(sheet != gee->sheet || !(gee->flags & GNM_EE_SHEET_OPTIONAL)) ? sheet : NULL;
	rs->ref.b.sheet = NULL;

	if (gee->freeze_count == 0)
		gee_rangesel_update_text (gee);

	rs->is_valid = TRUE; /* we just loaded it up */

	return needs_change;
}

/**
 * gnm_expr_entry_get_rangesel:
 * @gee: a #GnmExprEntry
 * @r: (out): address to receive #GnmRange
 * @sheet: (out) (optional) (transfer none): address to receive #sheet
 *
 * Get the range selection. GnmRange is copied, Sheet is not. If sheet
 * argument is NULL, the corresponding value is not returned.
 *
 * Returns: %TRUE if the returned range is indeed valid.
 * The resulting range is normalized.
 **/
gboolean
gnm_expr_entry_get_rangesel (GnmExprEntry const *gee,
			     GnmRange *r, Sheet **sheet)
{
	GnmRangeRef ref;
	Rangesel const *rs = &gee->rangesel;

	g_return_val_if_fail (GNM_EXPR_ENTRY_IS (gee), FALSE);

	gee_prepare_range (gee, &ref);

	ref.a.sheet = eval_sheet (rs->ref.a.sheet, gee->sheet);
	ref.b.sheet = eval_sheet (rs->ref.b.sheet, ref.a.sheet);

	/* TODO : does not handle 3d, neither does this interface
	 * should probably scrap the interface in favour of returning a
	 * rangeref.
	 */
	if (sheet)
		*sheet = ref.a.sheet;

	if (r != NULL) {
		gnm_cellpos_init_cellref (&r->start, &ref.a, &gee->pp.eval, ref.a.sheet);
		gnm_cellpos_init_cellref (&r->end, &ref.b, &gee->pp.eval, ref.b.sheet);
		range_normalize (r);
	}

	return rs->is_valid;
}

/**
 * gnm_expr_entry_can_rangesel:
 * @gee:   a #GnmExprEntry
 *
 * Returns: %TRUE if a range selection is meaningful at current position.
 **/
gboolean
gnm_expr_entry_can_rangesel (GnmExprEntry *gee)
{
	char const *text;

	g_return_val_if_fail (GNM_EXPR_ENTRY_IS (gee), FALSE);

	if (wbc_gtk_get_guru (gee->wbcg) != NULL &&
	    gee == gee->wbcg->edit_line.entry)
		return FALSE;

	text = gtk_entry_get_text (gee->entry);

	/* We need to be editing an expression */
	if (wbc_gtk_get_guru (gee->wbcg) == NULL &&
	    gnm_expr_char_start_p (text) == NULL)
		return FALSE;

	return (gnm_expr_entry_find_range (gee));
}

/**
 * gnm_expr_entry_parse:
 * @gee: the entry
 * @pp: a parse position
 * @start_sel: start range selection when things change.
 * @flags:
 *
 * Attempts to parse the content of the entry line honouring
 * the flags.
 */
GnmExprTop const *
gnm_expr_entry_parse (GnmExprEntry *gee, GnmParsePos const *pp,
		      GnmParseError *perr, gboolean start_sel,
		      GnmExprParseFlags flags)
{
	char const *text;
	char *str;
	GnmExprTop const *texpr;

	g_return_val_if_fail (GNM_EXPR_ENTRY_IS (gee), NULL);

	text = gtk_entry_get_text (gee->entry);

	if (text == NULL || text[0] == '\0')
		return NULL;

	if (gee_debug)
		g_printerr ("Parsing %s\n", text);

	if ((gee->flags & GNM_EE_FORCE_ABS_REF))
		flags |= GNM_EXPR_PARSE_FORCE_ABSOLUTE_REFERENCES;
	else if ((gee->flags & GNM_EE_FORCE_REL_REF))
		flags |= GNM_EXPR_PARSE_FORCE_RELATIVE_REFERENCES;
	if (!(gee->flags & GNM_EE_SHEET_OPTIONAL))
		flags |= GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES;

	/* First try parsing as a value.  */
	{
		GnmValue *v = get_matched_value (gee);
		if (v) {
			GODateConventions const *date_conv =
				sheet_date_conv (gee->sheet);
			GnmExprTop const *texpr = gnm_expr_top_new_constant (v);
			char *str = format_value (gee->constant_format, v, -1, date_conv);
			if (gee_debug)
				g_printerr ("Setting entry text: [%s]\n", str);
			gtk_entry_set_text (gee->entry, str);
			g_free (str);
			return texpr;
		}
	}

	/* Failing that, try as an expression.  */
	texpr = gnm_expr_parse_str (text, pp, flags,
				    gee_convs (gee), perr);

	if (texpr == NULL)
		return NULL;

	if (gee->flags & GNM_EE_SINGLE_RANGE) {
		GnmValue *range = gnm_expr_top_get_range (texpr);
		if (range == NULL) {
			if (perr != NULL) {
				perr->err = g_error_new (1, PERR_SINGLE_RANGE,
					_("Expecting a single range"));
				perr->begin_char = perr->end_char   = 0;
			}
			gnm_expr_top_unref (texpr);
			return NULL;
		}
		value_release (range);
	}

	/* Reset the entry in case something changed */
	str = (flags & GNM_EXPR_PARSE_PERMIT_MULTIPLE_EXPRESSIONS)
		? gnm_expr_top_multiple_as_string (texpr, pp, gee_convs (gee))
		: gnm_expr_top_as_string (texpr, pp, gee_convs (gee));

	if (strcmp (str, text)) {
		SheetControlGUI *scg = wbcg_cur_scg (gee->wbcg);
		Rangesel const *rs = &gee->rangesel;
		if (gee == wbcg_get_entry_logical (gee->wbcg) &&
		    start_sel && sc_sheet (GNM_SHEET_CONTROL (scg)) == rs->ref.a.sheet) {
			scg_rangesel_bound (scg,
				rs->ref.a.col, rs->ref.a.row,
				rs->ref.b.col, rs->ref.b.row);
		} else {
			if (gee_debug)
				g_printerr ("Setting entry text: [%s]\n", str);
			gtk_entry_set_text (gee->entry, str);
		}
	}
	g_free (str);

	return texpr;
}

/**
 * gnm_expr_entry_get_text:
 * @gee:
 *
 * A small convenience routine.  Think long and hard before using this.
 * There are lots of parse routines that serve the common case.
 *
 * Returns: The content of the entry.  Caller should not modify the result.
 **/
char const *
gnm_expr_entry_get_text	(GnmExprEntry const *gee)
{
	g_return_val_if_fail (GNM_EXPR_ENTRY_IS (gee), NULL);
	return gtk_entry_get_text (gee->entry);
}

/**
 * gnm_expr_entry_parse_as_value:
 * @gee: GnmExprEntry
 * @sheet: the sheet where the cell range is evaluated.
 *
 * Returns a (GnmValue *) of type VALUE_CELLRANGE if the @range was
 *	successfully parsed or %NULL on failure.
 */
GnmValue *
gnm_expr_entry_parse_as_value (GnmExprEntry *gee, Sheet *sheet)
{
	GnmParsePos pp;
	GnmExprParseFlags flags = GNM_EXPR_PARSE_UNKNOWN_NAMES_ARE_STRINGS;
	GnmValue *v;
	const char *txt;

	g_return_val_if_fail (GNM_EXPR_ENTRY_IS (gee), NULL);

	if ((gee->flags & GNM_EE_FORCE_ABS_REF))
		flags |= GNM_EXPR_PARSE_FORCE_ABSOLUTE_REFERENCES;
	else if ((gee->flags & GNM_EE_FORCE_REL_REF))
		flags |= GNM_EXPR_PARSE_FORCE_RELATIVE_REFERENCES;
	if (!(gee->flags & GNM_EE_SHEET_OPTIONAL))
		flags |= GNM_EXPR_PARSE_FORCE_EXPLICIT_SHEET_REFERENCES;

	txt = gtk_entry_get_text (gnm_expr_entry_get_entry (gee));

	parse_pos_init_sheet (&pp, sheet);
	v = value_new_cellrange_parsepos_str (&pp, txt, flags);

	if (!v && (gee->flags & GNM_EE_CONSTANT_ALLOWED)) {
		GODateConventions const *date_conv =
			sheet ? sheet_date_conv (sheet) : NULL;
		v = format_match_number (txt, NULL, date_conv);
	}

	return v;
}

/**
 * gnm_expr_entry_parse_as_list:
 * @gee: GnmExprEntry
 * @sheet: the sheet where the cell range is evaluated. This really only needed if
 *         the range given does not include a sheet specification.
 *
 * Returns: (element-type GnmValue) (transfer full): a list of ranges
 * (as #GnmValue).
 */
GSList *
gnm_expr_entry_parse_as_list (GnmExprEntry *gee, Sheet *sheet)
{
	g_return_val_if_fail (GNM_EXPR_ENTRY_IS (gee), NULL);

	return global_range_list_parse (sheet,
		gtk_entry_get_text (gnm_expr_entry_get_entry (gee)));
}

/**
 * gnm_expr_entry_get_entry:
 * @gee: #GnmExprEntry
 *
 * Returns: (transfer none): the associated #GtkEntry.
 **/
GtkEntry *
gnm_expr_entry_get_entry (GnmExprEntry *gee)
{
	g_return_val_if_fail (GNM_EXPR_ENTRY_IS (gee), NULL);

	return gee->entry;
}

gboolean
gnm_expr_entry_is_cell_ref (GnmExprEntry *gee, Sheet *sheet,
			    gboolean allow_multiple_cell)
{
        GnmValue *val;
	gboolean res;

	g_return_val_if_fail (GNM_EXPR_ENTRY_IS (gee), FALSE);

	val = gnm_expr_entry_parse_as_value (gee, sheet);
        if (val == NULL)
		return FALSE;

	res = ((VALUE_IS_CELLRANGE (val)) &&
	       (allow_multiple_cell ||
		((val->v_range.cell.a.col == val->v_range.cell.b.col) &&
		 (val->v_range.cell.a.row == val->v_range.cell.b.row))));
	value_release (val);
	return res;

}

gboolean
gnm_expr_entry_is_blank	(GnmExprEntry *gee)
{
	GtkEntry *entry;
	char const *text;

	g_return_val_if_fail (GNM_EXPR_ENTRY_IS (gee), FALSE);

	entry = gnm_expr_entry_get_entry (gee);

	text = gtk_entry_get_text (entry);
	if (text == NULL)
		return TRUE;

	while (*text) {
		if (!g_unichar_isspace (g_utf8_get_char (text)))
			return FALSE;
		text = g_utf8_next_char (text);
	}

	return TRUE;
}

char *
gnm_expr_entry_global_range_name (GnmExprEntry *gee, Sheet *sheet)
{
	GnmValue *val;
	char *text = NULL;

	g_return_val_if_fail (GNM_EXPR_ENTRY_IS (gee), NULL);

	val = gnm_expr_entry_parse_as_value (gee, sheet);
	if (val != NULL) {
		if (VALUE_IS_CELLRANGE (val))
			text = value_get_as_string (val);
		value_release (val);
	}

	return text;
}

void
gnm_expr_entry_grab_focus (GnmExprEntry *gee, gboolean select_all)
{
	g_return_if_fail (GNM_EXPR_ENTRY_IS (gee));

	gtk_widget_grab_focus (GTK_WIDGET (gee->entry));
	if (select_all) {
		gtk_editable_set_position (GTK_EDITABLE (gee->entry), -1);
		gtk_editable_select_region (GTK_EDITABLE (gee->entry), 0, -1);
	}
}

gboolean
gnm_expr_entry_editing_canceled (GnmExprEntry *gee)
{
	g_return_val_if_fail (GNM_EXPR_ENTRY_IS (gee), TRUE);

	return gee->editing_canceled;
}

/*****************************************************************************/

void
gnm_expr_entry_disable_tips (GnmExprEntry *gee)
{
	g_return_if_fail (gee != NULL);
	gee_delete_tooltip (gee, TRUE);
	gee->tooltip.enabled = FALSE;
}

void
gnm_expr_entry_enable_tips (GnmExprEntry *gee)
{
	g_return_if_fail (gee != NULL);
	gee->tooltip.enabled = TRUE;
}

