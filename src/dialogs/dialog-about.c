/*
 * dialog-about.c: Shows the contributors to Gnumeric.
 *
 * Author:
 *  Jody Goldberg <jody@gnome.org>
 *  Morten Welinder <terra@gnome.org>
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
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <dialogs/dialogs.h>
#include <string.h>

#include <gui-util.h>
#include <gnm-random.h>
#include <wbc-gtk.h>
#include <gnm-format.h>
#include <goffice/goffice.h>

#define ABOUT_KEY          "about-dialog"

#define SPEED_FACTOR 1.5

#define EXPANSION_CHAR UNICODE_ZERO_WIDTH_SPACE_C

#define LICENSE_TEXT \
	N_("Gnumeric is available under the GNU General Public License, version 2 or 3 at your option.\n\nSee https://www.gnu.org/licenses/old-licenses/gpl-2.0.html\nor https://www.gnu.org/licenses/gpl-3.0.html.\n\nGnumeric comes with absolutely no warranty.")


typedef enum {
	GNM_CORE		= 1 << 0,	/* All round hacking */
	GNM_FEATURE_HACKER	= 1 << 1,	/* Implement specific feature */
	GNM_ANALYTICS		= 1 << 2,
	GNM_IMPORT_EXPORT	= 1 << 3,
	GNM_SCRIPTING		= 1 << 4,
	GNM_GUI			= 1 << 5,
	GNM_USABILITY		= 1 << 6,
	GNM_DOCUMENTATION	= 1 << 7,
	GNM_TRANSLATION		= 1 << 8,
	GNM_QA			= 1 << 9,
	GNM_ART			= 1 << 10,
	GNM_PACKAGING		= 1 << 11
} ContribTypes;

#if 0
#define GNM_ABOUT_NUM_TYPES	       12
static char const * const about_types[GNM_ABOUT_NUM_TYPES] = {
	N_("Core"),
	N_("Features"),
	N_("Analytics"),
	N_("Import Export"),
	N_("Scripting"),
	N_("UI"),
	N_("Usability"),
	N_("Documentation"),
	N_("Translation"),
	N_("QA"),
	N_("Art"),
	N_("Packaging")
};
#endif

static struct {
	char const *name;
	unsigned contributions;
	char const *details; /* optionally NULL */
} const contributors[] = {
	{ N_("Harald Ashburner"),		GNM_ANALYTICS,
		N_("Options pricers") },
	{ N_("Sean Atkinson"),		GNM_ANALYTICS | GNM_IMPORT_EXPORT,
		N_("Functions and X-Base importing.") },
	{ N_("Michel Berkelaar"),		GNM_ANALYTICS,
		N_("Simplex algorithm for Solver (LP Solve).") },
	{ N_("Jean Br\303\251fort"),		GNM_CORE | GNM_FEATURE_HACKER,
		N_("Core charting engine.") },
	{ N_("Grandma Chema Celorio"),	GNM_FEATURE_HACKER|GNM_USABILITY|GNM_QA,
		N_("Quality Assurance and sheet copy.") },
	{ N_("Frank Chiulli"),		GNM_IMPORT_EXPORT,
		N_("OLE2 support.") },
	{ N_("Kenneth Christiansen"),	GNM_TRANSLATION,
		N_("Localization.") },
	{ N_("Zbigniew Chyla"),		GNM_CORE,
		N_("Plugin system, localization.") },
	{ N_("J.H.M. Dassen (Ray)"),	GNM_PACKAGING,
		N_("Debian packaging.") },
	{ N_("Jeroen Dirks"),		GNM_ANALYTICS,
		N_("Simplex algorithm for Solver (LP Solve).") },
	{ N_("Tom Dyas"),			GNM_FEATURE_HACKER,
		N_("Original plugin engine.") },
	{ N_("Kjell Eikland"),            GNM_ANALYTICS,
	        N_("LP-solve") },
	{ N_("Gergo Erdi"),			GNM_GUI,
		N_("Custom UI tools") },
	{ N_("Jody Goldberg"), GNM_CORE, NULL },
	{ N_("John Gotts"),			GNM_PACKAGING,
		N_("RPM packaging") },
	{ N_("Andreas J. G\xc3\xbclzow"),	GNM_CORE|GNM_FEATURE_HACKER|GNM_ANALYTICS|GNM_IMPORT_EXPORT|GNM_GUI|GNM_USABILITY|GNM_DOCUMENTATION|GNM_TRANSLATION|GNM_QA,
		N_("Statistics and GUI guru") },
	{ N_("Jon K\xc3\xa5re Hellan"),	GNM_CORE|GNM_FEATURE_HACKER|GNM_ANALYTICS|GNM_IMPORT_EXPORT|GNM_SCRIPTING|GNM_GUI|GNM_USABILITY|GNM_DOCUMENTATION|GNM_TRANSLATION|GNM_QA,
		N_("UI polish and all round bug fixer") },
	{ N_("Miguel de Icaza"), GNM_CORE, NULL },
	{ N_("Ross Ihaka"),			GNM_ANALYTICS,
		N_("Special functions") },
	{ N_("Jukka-Pekka Iivonen"),	GNM_ANALYTICS|GNM_GUI|GNM_FEATURE_HACKER,
		N_("Solver, lots of worksheet functions, and general trailblazer") },
	{ N_("Jakub Jel\xc3\xadnek"),		GNM_CORE,
		N_("One of the original core contributors") },
	{ N_("Chris Lahey"),		GNM_FEATURE_HACKER,
		N_("The original value format engine and libgoffice work") },
	{ N_("Takashi Matsuda"),		GNM_FEATURE_HACKER,
		N_("The original text plugin") },
	{ N_("Michael Meeks"),		GNM_CORE|GNM_IMPORT_EXPORT,
		N_("Started the MS Excel import/export engine, and 'GnmStyle'") },
	{ N_("Lutz Muller"),		GNM_FEATURE_HACKER,
		N_("SheetObject improvement") },
	{ N_("Yukihiro Nakai"),             GNM_FEATURE_HACKER | GNM_TRANSLATION | GNM_QA,
	        N_("Support for non-Latin languages") },
	{ N_("Peter Notebaert"),            GNM_ANALYTICS,
	        N_("LP-solve") },
	{ N_("Emmanuel Pacaud"),		GNM_CORE | GNM_FEATURE_HACKER,
		N_("Many plot types for charting engine.") },
	{ N_("Federico M. Quintero"),	GNM_CORE,
		N_("canvas support") },
	{ N_("Mark Probst"),		GNM_SCRIPTING,
		N_("Guile support") },
	{ N_("Rasca"),			GNM_IMPORT_EXPORT,
		N_("HTML, troff, LaTeX exporters") },
	{ N_("Vincent Renardias"),		GNM_IMPORT_EXPORT|GNM_TRANSLATION,
		N_("original CSV support, French localization") },
	{ N_("Ariel Rios"),			GNM_SCRIPTING,
		N_("Guile support") },
	{ N_("Jakub Steiner"),		GNM_ART,
		N_("Icons and Images") },
	{ N_("Uwe Steinmann"),		GNM_FEATURE_HACKER|GNM_IMPORT_EXPORT,
		N_("Paradox Importer") },
	{ N_("Arturo Tena"),		GNM_IMPORT_EXPORT,
		N_("Initial work on OLE2 for libgsf") },
	{ N_("Almer S. Tigelaar"),		GNM_FEATURE_HACKER|GNM_IMPORT_EXPORT,
		N_("Consolidation and Structured Text importer") },
	{ N_("Bruno Unna"),			GNM_IMPORT_EXPORT,
		N_("Pieces of MS Excel import") },
	{ N_("Arief Mulya Utama"),              GNM_ANALYTICS,
		N_("Telecommunications functions") },
	{ N_("Daniel Veillard"),		GNM_IMPORT_EXPORT,
		N_("Initial XML support") },
	{ N_("Vladimir Vuksan"),		GNM_ANALYTICS,
		N_("Some financial functions") },
	{ N_("Morten Welinder"),		GNM_CORE|GNM_FEATURE_HACKER|GNM_ANALYTICS|GNM_IMPORT_EXPORT|GNM_SCRIPTING|GNM_GUI|GNM_USABILITY|GNM_TRANSLATION|GNM_QA,
		N_("All round powerhouse") },
	{ N_("Kevin Breit"),		GNM_DOCUMENTATION, NULL },
	{ N_("Thomas Canty"),		GNM_DOCUMENTATION, NULL },
	{ N_("Adrian Custer"),		GNM_DOCUMENTATION, NULL },
	{ N_("Adrian Likins"),		GNM_DOCUMENTATION, NULL },
	{ N_("Aaron Weber"),		GNM_DOCUMENTATION, NULL },
	{ N_("Alexander Kirillov"),		GNM_DOCUMENTATION, NULL },
};

typedef struct AboutRenderer_ AboutRenderer;
typedef struct AboutState_ AboutState;

struct AboutRenderer_ {
	int start_time, duration;

	gboolean (*renderer) (AboutRenderer *, AboutState *);

	PangoLayout *layout;
	PangoAttrList *attrs;
	int natural_width;
	char *text;

	gboolean fade_in, fade_out;

	struct {
		double x, y;
	} start, end;

	struct {
		double rate;
		int count;
	} expansion;
	cairo_t *cr;
};

struct AboutState_ {
	GtkWidget *dialog;
	guint timer;

	GtkWidget *anim_area;
	GList *active, *waiting;

	int now;
};

/* ---------------------------------------- */

static void
free_renderer (AboutRenderer *r)
{
	if (r->layout)
		g_object_unref (r->layout);
	if (r->attrs)
		pango_attr_list_unref (r->attrs);
	g_free (r->text);
	g_free (r);
}

static gboolean
text_item_renderer (AboutRenderer *r, AboutState *state)
{
	PangoLayout *layout;
	int age = state->now - r->start_time;
	double rage = CLAMP (age / (double)r->duration, 0.0, 1.0);
	GtkWidget *widget = state->anim_area;
	GtkStyleContext *ctxt;
	const int fade = 500;
	int x, y, width, height;
	cairo_t *cr;
	GtkAllocation wa;
	GdkRGBA color;
	double alpha = 1;

	if (age >= r->duration)
		return FALSE;

	if (r->layout == NULL) {
		r->layout = gtk_widget_create_pango_layout (state->anim_area, r->text);
		pango_layout_set_attributes (r->layout, r->attrs);
		pango_layout_get_size (r->layout, &r->natural_width, NULL);
	}
	layout = r->layout;

	if (r->fade_in && age < fade)
		alpha = age / (double)fade;
	else if (r->fade_out && r->duration - age < fade)
		alpha = (r->duration - age) / (double)fade;

	ctxt = gtk_widget_get_style_context (widget);

	gtk_widget_get_allocation (widget, &wa);
	x = (int)(PANGO_SCALE * wa.width *
		  (r->start.x + rage * (r->end.x - r->start.x)));
	y = (int)(PANGO_SCALE * wa.height *
		  (r->start.y + rage * (r->end.y - r->start.y)));

	if (r->expansion.count) {
		PangoAttrList *attrlist = pango_attr_list_copy (r->attrs);
		const char *p, *text = r->text;
		PangoRectangle ink, logical;

		memset (&logical, 0, sizeof (logical));
		logical.width = (int)(rage * r->expansion.rate * r->natural_width / r->expansion.count);
		ink = logical;

		p = text;
		while (*p) {
			const char *next = g_utf8_next_char (p);
			gunichar uc = g_utf8_get_char (p);

			if (uc == EXPANSION_CHAR) {
				PangoAttribute *attr = pango_attr_shape_new (&ink, &logical);
				attr->start_index = p - text;
				attr->end_index = next - text;
				pango_attr_list_change (attrlist, attr);
			}
			p = next;
		}
		pango_layout_set_attributes (layout, attrlist);
		pango_attr_list_unref (attrlist);
	}

	pango_layout_get_size (layout, &width, &height);
	x -= width / 2;
	y -= height / 2;

	cr = r->cr;
	gnm_style_context_get_color (ctxt, GTK_STATE_FLAG_NORMAL, &color);
	color.alpha = alpha;
	gdk_cairo_set_source_rgba (cr, &color);
	cairo_move_to (cr, x / (double)PANGO_SCALE, y / (double)PANGO_SCALE);
	pango_cairo_show_layout (cr, layout);

	return TRUE;
}

static void
set_text_motion (AboutRenderer *r, double sx, double sy, double ex, double ey)
{
	r->start.x = sx;
	r->start.y = sy;

	r->end.x = ex;
	r->end.y = ey;
}

static void
set_text_expansion (AboutRenderer *r, double er)
{
	char *text = r->text;
	GString *str = g_string_new (NULL);
	char *ntext;
	const char *p;

	r->expansion.rate = er;
	r->expansion.count = 0;

	/* Normalize to make sure diacriticals are combined.  */
	ntext = g_utf8_normalize (text, -1, G_NORMALIZE_DEFAULT_COMPOSE);

	/* Insert inter-letter spaces we can stretch.  */
	for (p = ntext; *p; p = g_utf8_next_char (p)) {
		gunichar uc = g_utf8_get_char (p);

		if (uc == EXPANSION_CHAR)
			continue;

		if (str->len) {
			g_string_append_unichar (str, EXPANSION_CHAR);
			r->expansion.count++;
		}
		g_string_append_unichar (str, uc);
	}

	g_free (ntext);
	g_free (text);
	r->text = g_string_free (str, FALSE);
}

static AboutRenderer *
make_text_item (AboutState *state, const char *text, int duration)
{
	AboutRenderer *r = g_new0 (AboutRenderer, 1);
	PangoAttribute *attr;

	duration = (int)(duration / SPEED_FACTOR);

	r->start_time = state->now;
	r->duration = duration;
	r->layout = NULL;
	r->renderer = text_item_renderer;
	r->fade_in = r->fade_out = TRUE;
	r->text = g_strdup (text);
	set_text_motion (r, 0.5, 0.5, 0.5, 0.5);

	r->attrs = pango_attr_list_new ();
	attr = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
	pango_attr_list_change (r->attrs, attr);

	state->now += duration;

	return r;
}

/* ---------------------------------------- */


#define TIME_SLICE 20 /* ms */


static void
free_state (AboutState *state)
{
	if (state->timer) {
		g_source_remove (state->timer);
		state->timer = 0;
	}

	g_list_free_full (state->active, (GDestroyNotify)free_renderer);
	state->active = NULL;

	g_list_free_full (state->waiting, (GDestroyNotify)free_renderer);
	state->waiting = NULL;

	g_free (state);
}

static gboolean
about_dialog_timer (gpointer state_)
{
	AboutState *state = state_;

	while (state->waiting) {
		AboutRenderer *r = state->waiting->data;
		if (r->start_time > state->now)
			break;
		state->active = g_list_append (state->active, r);
		state->waiting = g_list_remove (state->waiting, r);
	}

	if (state->active)
		gtk_widget_queue_draw (state->anim_area);

	state->now += TIME_SLICE;

	return TRUE;
}

static gboolean
about_dialog_anim_draw (G_GNUC_UNUSED GtkWidget *widget,
			cairo_t *cr,
			AboutState *state)
{
	GList *l;

	l = state->active;
	while (l) {
		GList *next = l->next;
		AboutRenderer *r = l->data;
		gboolean keep;
		r->cr = cr;
		keep = r->renderer (r, state);
		if (!keep) {
			free_renderer (r);
			state->active = g_list_remove_link (state->active, l);
		}
		l = next;
	}

	return FALSE;
}

#define APPENDR(r_) do {			\
  tail->next = g_list_prepend (NULL, (r_));	\
  tail->next->prev = tail;			\
  tail = tail->next;				\
} while (0)

#define OVERLAP(ms) do {			\
  state->now -= (int)((ms) / SPEED_FACTOR);	\
} while (0)

static void
create_animation (AboutState *state)
{
	AboutRenderer *r;
	GList *tail;
	unsigned ui;
	unsigned N = G_N_ELEMENTS (contributors);
	unsigned *permutation;

	OVERLAP (-500);

	r = make_text_item (state, _("Gnumeric is the result of"), 3000);
	set_text_motion (r, 0.5, 0.9, 0.5, 0.1);
	tail = state->waiting = g_list_prepend (NULL, r);

	OVERLAP (2000);

	r = make_text_item (state, _("the efforts of many people."), 3000);
	set_text_motion (r, 0.5, 0.9, 0.5, 0.1);
	APPENDR (r);

	OVERLAP (2000);

	r = make_text_item (state, _("Your help is much appreciated!"), 3000);
	set_text_motion (r, 0.5, 0.9, 0.5, 0.1);
	APPENDR (r);

	permutation = g_new (unsigned, N);
	for (ui = 0; ui < N; ui++)
		permutation[ui] = ui;

	for (ui = 0; ui < N; ui++) {
		unsigned pui = gnm_random_uniform_int (N);
		unsigned A = permutation[ui];
		permutation[ui] = permutation[pui];
		permutation[pui] = A;
	}

	for (ui = 0; ui < N; ui++) {
		unsigned pui = permutation[ui];
		const char *name = contributors[pui].name;
		int style = ui % 2;

		if (ui != 0)
			OVERLAP (1900);

		r = make_text_item (state, name, 3000);
		switch (style) {
		case 0:
			set_text_motion (r, 0.5, 0.1, 0.1, 0.9);
			if (0) set_text_expansion (r, 1);
			break;
		case 1:
			set_text_motion (r, 0.5, 0.1, 0.9, 0.9);
			if (0) set_text_expansion (r, 1);
			break;
#if 0
		case 2:
			set_text_motion (r, 0.5, 0.1, 0.5, 0.9);
			set_text_expansion (r, 3);
			break;
#endif
		}

		APPENDR (r);
	}

	g_free (permutation);

	OVERLAP (-1000);

	r = make_text_item (state, _("We apologize if anyone was left out."),
			    3000);
	set_text_motion (r, 0.5, 0.9, 0.5, 0.1);
	APPENDR (r);

	OVERLAP (2000);

	r = make_text_item (state, _("Please contact us to correct mistakes."),
			    3000);
	set_text_motion (r, 0.5, 0.9, 0.5, 0.1);
	APPENDR (r);

	OVERLAP (2000);

	r = make_text_item (state, _("Report problems at"), 3000);
	set_text_motion (r, 0.5, 0.9, 0.5, 0.1);
	APPENDR (r);

	OVERLAP (2000);

	r = make_text_item (state, PACKAGE_BUGREPORT, 3000);
	set_text_motion (r, 0.5, 0.9, 0.5, 0.1);
	APPENDR (r);

	r = make_text_item (state, _("We aim to please!"), 3000);
	r->fade_out = FALSE;
	APPENDR (r);

	OVERLAP (-100);

	r = make_text_item (state, _("We aim to please!"), 2500);
	r->fade_in = FALSE;
	set_text_expansion (r, 3);
	APPENDR (r);

	state->now = 0;
}

#undef APPENDR

void
dialog_about (WBCGtk *wbcg)
{
	GtkWidget *w, *c;
	GList *children;
	AboutState *state;
	GPtrArray *authors;
	GPtrArray *documenters;
	GPtrArray *artists;
	unsigned ui;

	if (gnm_dialog_raise_if_exists (wbcg, ABOUT_KEY))
		return;

	state = g_new0 (AboutState, 1);

	authors = g_ptr_array_new_with_free_func (g_free);
	documenters = g_ptr_array_new_with_free_func (g_free);
	artists = g_ptr_array_new_with_free_func (g_free);
	for (ui = 0; ui < G_N_ELEMENTS (contributors); ui++) {
		const char *name = contributors[ui].name;
		const char *details = contributors[ui].details;
		unsigned flags = contributors[ui].contributions;
		if (flags & GNM_ART) {
			g_ptr_array_add (artists, g_strdup (name));
		}
		if (flags & GNM_DOCUMENTATION) {
			g_ptr_array_add (documenters, g_strdup (name));
		}
		if (flags & ~(GNM_ART | GNM_DOCUMENTATION)) {
			char *text = details
				? g_strdup_printf ("%s (%s)", name, details)
				: g_strdup (name);
			g_ptr_array_add (authors, text);
		}
	}
	g_ptr_array_add (authors, NULL);
	g_ptr_array_add (documenters, NULL);
	g_ptr_array_add (artists, NULL);
	w = g_object_new (GTK_TYPE_ABOUT_DIALOG,
			  "title", _("About Gnumeric"),
			  "version", GNM_VERSION_FULL,
			  "website", PACKAGE_URL,
			  "website-label", _("Visit the Gnumeric website"),
			  "logo-icon-name", "org.gnumeric.gnumeric",
			  "copyright", _("Copyright \xc2\xa9 1998-2024"),
			  "comments", _("Free, Fast, Accurate - Pick Any Three!"),
			  "license", _(LICENSE_TEXT),
			  "wrap-license", TRUE,
			  "authors", authors->pdata,
			  "documenters", documenters->pdata,
			  "artists", artists->pdata,
			  NULL);
	g_ptr_array_free (authors, TRUE);
	g_ptr_array_free (documenters, TRUE);
	g_ptr_array_free (artists, TRUE);
	state->dialog = w;

	g_signal_connect (w, "response",
			  G_CALLBACK (gtk_widget_destroy), NULL);
	g_signal_connect_swapped (w, "destroy",
				  G_CALLBACK (free_state), state);

	c = gtk_dialog_get_content_area (GTK_DIALOG (w));
	children = gtk_container_get_children (GTK_CONTAINER (c));

	if (children && GTK_IS_BOX (children->data)) {
		GtkWidget *vbox = children->data;
		int width, height;
		PangoLayout *layout;

		state->anim_area = gtk_drawing_area_new ();
		layout = gtk_widget_create_pango_layout (state->anim_area, "x");
		pango_layout_get_pixel_size (layout, &width, &height);
		gtk_widget_set_size_request (state->anim_area,
					     60 * width, 8 * height);
		g_object_unref (layout);

		g_signal_connect (state->anim_area, "draw",
				  G_CALLBACK (about_dialog_anim_draw),
				  state);

		gtk_box_pack_end (GTK_BOX (vbox), state->anim_area, TRUE, TRUE, 0);

		create_animation (state);

		state->timer = g_timeout_add (TIME_SLICE, about_dialog_timer, state);
	}
	g_list_free (children);

	gnm_keyed_dialog (wbcg, GTK_WINDOW (w), ABOUT_KEY);
	gtk_widget_show_all (w);
}
