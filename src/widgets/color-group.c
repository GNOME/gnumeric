/*
 * color-group.c : Utility to keep a shered memory of custom and
 *                  current colors between arbitrary widgets
 *
 * Author:
 * 	Michael Levy (mlevy@genoscope.cns.fr)
 *
 * (C) 2000 Michael Levy
 */

#include <string.h>
#include "gnumeric-type-util.h"
#include "color-group.h"

#define PARENT_TYPE gtk_object_get_type ()
#define PARENT_CLASS gtk_type_class ( gtk_object_get_type ())

enum {
        COLOR_CHANGE,
        LAST_SIGNAL
};

struct _ColorGroup {
	GtkObject  parent;

        gchar     *name;
        GdkColor **color_history; /* custom color history */
	GdkColor  *current_color ;
	gint       history_size;  /* length of color_history */
	gint       history_iterator; /* used to cycle through the colors */
};

typedef struct {
	GtkObjectClass parent_class;

	/* Signals emited by this object */
	void (*color_change) (ColorGroup *color_group, GdkColor *color, gboolean custom);
} ColorGroupClass;


static gint color_group_signals [LAST_SIGNAL] = { 0 };

typedef void (*GtkSignal_NONE__POINTER_BOOL) (GtkObject * object,
					      gpointer arg1,
					      gboolean arg2,
					      gpointer user_data);
static void
marshal_NONE__POINTER_BOOL (GtkObject * object,
			    GtkSignalFunc func,
			    gpointer func_data,
			    GtkArg * args)
{
	GtkSignal_NONE__POINTER_BOOL rfunc;
	rfunc = (GtkSignal_NONE__POINTER_BOOL) func;
	(*rfunc) (object,
		  GTK_VALUE_POINTER (args[0]),
		  GTK_VALUE_BOOL    (args[1]),
		  func_data);
}

static void color_group_destroy(GtkObject *obj);

static void
color_group_class_init (ColorGroupClass *class)
{
	GtkObjectClass *object_class;

	object_class = (GtkObjectClass*) class;

	object_class->destroy = &color_group_destroy;

	color_group_signals[COLOR_CHANGE] =
		gtk_signal_new("color_change",
			       GTK_RUN_LAST,
			       object_class->type,
			       GTK_SIGNAL_OFFSET (ColorGroupClass, color_change),
			       marshal_NONE__POINTER_BOOL,
			       GTK_TYPE_NONE, 2, GTK_TYPE_POINTER, GTK_TYPE_BOOL);
	gtk_object_class_add_signals (object_class, color_group_signals, LAST_SIGNAL);
	/* No default handler */
	class->color_change = NULL;
}

static void
color_group_init (ColorGroup *cg)
{
	cg->name = NULL;
	cg->color_history= NULL;
	cg->current_color = NULL;
	cg->history_size = 0;
	cg->history_iterator = 0;
}


GNUMERIC_MAKE_TYPE(color_group,
		   "ColorGroup",
		   ColorGroup,
		   color_group_class_init,
		   color_group_init,
		   PARENT_TYPE);


/* Hash table used to ensure unicity in newly created names*/
static GHashTable *group_names = NULL;

static void
initialize_group_names(void)
{
	g_assert(group_names == NULL);
	group_names = g_hash_table_new ((GHashFunc) g_str_hash, (GCompareFunc) g_str_equal);
}

GtkObject *
color_group_from_name (const gchar * name)
{
	gpointer res;

	g_assert(group_names);

	g_return_val_if_fail(name != NULL, NULL);
	res = g_hash_table_lookup(group_names, name);

	if(res != NULL)
		return GTK_OBJECT(res);
	else
		return NULL;
}

static gchar *
create_unique_name(void)
{
	const gchar *prefix = "__cg_autogen_name__";
	static gint latest_suff = 0;
	gchar *new_name;

	for(;;latest_suff++) {
		new_name = g_strdup_printf("%s%i", prefix, latest_suff);
		if(color_group_from_name (new_name) == NULL)
			return new_name;
		else
			g_free(new_name);
	}
	g_assert_not_reached();
}

static void
color_group_destroy(GtkObject *obj)
{
	ColorGroup *cg;

	g_return_if_fail(obj != NULL);
	g_return_if_fail(IS_COLOR_GROUP(obj));
	g_assert(group_names != NULL);

	cg = COLOR_GROUP(obj);

	/* make this name available */
	g_hash_table_remove (group_names, cg->name);
	g_free (cg->name);

	if(cg->color_history != NULL)
		g_free(cg->color_history);
	cg->color_history = NULL;
	if(cg->current_color)
		g_free(cg->current_color);
	cg->history_size = 0;
	cg->history_iterator = 0;
	/* call the base class destructor */
	if (GTK_OBJECT_CLASS (PARENT_CLASS)->destroy)
		(* GTK_OBJECT_CLASS (PARENT_CLASS)->destroy) (obj);
}

/*
  get the size of the custom color history
*/
gint
color_group_get_history_size (ColorGroup *cg)
{
	g_return_val_if_fail(cg != NULL, 0);
	return cg->history_size;
}

/*
   Change the size of the custom color history.
*/
void
color_group_set_history_size (ColorGroup *cg, gint size)
{
	g_return_if_fail(cg != NULL);
	g_return_if_fail(size >= 0);

	if(size == 0) {
		if(cg->color_history != NULL)
			g_free(cg->color_history);
		cg->color_history = NULL;
		cg->history_size = 0;
		cg->history_iterator = 0;
		return;
	}
	cg->color_history = (GdkColor**) g_realloc(cg->color_history,
						   size * sizeof(GdkColor *));

	/* Initialize to NULL */
	for (; cg->history_size < size; (cg->history_size)++)
		cg->color_history[cg->history_size] = NULL;
	/*
	  Need to do the following in case this function was called to
	  reduce the size of the historyh
	*/
	cg->history_size  = size;

	if(cg->history_iterator >= cg->history_size)
		cg->history_iterator = 0;
}

/*
 * Create a new color group.
 * if name is NULL or a name not currently in use by another group
 * then a new group is created and returned. If name was NULL
 * then the new group is given a unique name prefixed by "__cg_autogen_name__"
 * (thereby insuring namespace separation).
 * If name was already used by a group then the reference count is
 * incremented and a pointer to the group is returned.
 */
GtkObject *
color_group_new_named (const gchar *name)
{
	GtkObject *obj;
	ColorGroup *cg;
	gchar *new_name;

	if (group_names == NULL)
		initialize_group_names();

	if (name == NULL)
		new_name = create_unique_name();
	else
		new_name = g_strdup (name);

	obj = color_group_from_name (new_name);
	if (obj) {
		g_free (new_name);
		gtk_object_ref(GTK_OBJECT(obj));
		return obj;
	}

	/* Take care of creating the new object */
	cg = gtk_type_new(color_group_get_type ());
	g_return_val_if_fail(cg != NULL, NULL);

	cg->name = new_name;

	/* give the group a default size */
	color_group_set_history_size (cg, 16);

	/* lastly register this name */
	g_hash_table_insert(group_names, new_name, cg);

	return GTK_OBJECT(cg);
}

/*
  retuns the next most recent custom color
  NULL is returned when there are no more custom colors
*/
GdkColor *
color_group_next_color (ColorGroup *cg)
{
	g_return_val_if_fail(cg != NULL, NULL);

	if((cg->history_iterator >= cg->history_size) ||
	   (cg->history_iterator < 0))
		return NULL;
	cg->history_iterator++;

	return cg->color_history[cg->history_iterator - 1];
}

/*
  retuns the next oldest custom color
  NULL is returned when there are no more custom colors
*/
GdkColor *
color_group_previous_color (ColorGroup *cg)
{
	g_return_val_if_fail(cg != NULL, NULL);

	if((cg->history_iterator <= -1) ||
	   (cg->history_iterator >= cg->history_size))
		return NULL;
	cg->history_iterator--;

	return cg->color_history[cg->history_iterator + 1];
}

/*
   returns the most recent custom color
   NULL is returned if there are no custom colors
*/
GdkColor *
color_group_most_recent_color (ColorGroup *cg)
{
	g_return_val_if_fail(cg != NULL, NULL);

	cg->history_iterator = 0;
	return color_group_next_color(cg);
}

/*
  returns the oldest color
  NULL is returned if there are no custom colors
*/
GdkColor *
color_group_oldest_color (ColorGroup *cg)
{
	g_return_val_if_fail(cg != NULL, NULL);

	cg->history_iterator = cg->history_size - 1;

	return color_group_previous_color(cg);
}

/*
 * color_group_add_color:
 * Changes the colors. If the color is a custom color *it adds a new custom
 * color to the color_history 
 * This function emits the signal "color_change"
 */
void
color_group_add_color (ColorGroup *cg, GdkColor *color, gboolean custom_color)
{
	gint pos;

	g_return_if_fail(cg != NULL);

	color_group_set_current_color(cg, color);

	if(custom_color == TRUE) {
		pos = cg->history_size - 2;
		for (;pos >= 0; pos--)
			cg->color_history[pos + 1] = cg->color_history[pos];

		if(cg->history_size > 0)
			cg->color_history[0] = color;
	}

	gtk_signal_emit(GTK_OBJECT(cg),
			color_group_signals[COLOR_CHANGE],
			color,
			custom_color);
}

/*
 * color_group_get_current_color:
 * returns the current color for the group. The return value may be NULL
 */
GdkColor *
color_group_get_current_color (ColorGroup *cg)
{
	GdkColor *new;

	g_return_val_if_fail(cg != NULL, NULL);

	if(cg->current_color == NULL)
		return NULL;

	new = g_new (GdkColor, 1);
	memcpy (new, cg->current_color, sizeof (GdkColor));

	return new;
}

/*
 * color_group_set_current_color:
 * Changes the current color
 */
void
color_group_set_current_color (ColorGroup *cg,  GdkColor *color)
{
	GdkColor *new;
	g_return_if_fail (cg != NULL);

	if(color == NULL)
		return;

	new = g_new (GdkColor, 1);
	memcpy (new, color, sizeof (GdkColor));

	if(cg->current_color != NULL)
		g_free(cg->current_color);

	cg->current_color = new;
}
