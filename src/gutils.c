/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * utils.c:  Various utility routines that do not depend on the GUI of Gnumeric
 *
 * Authors:
 *    Miguel de Icaza (miguel@gnu.org)
 *    Jukka-Pekka Iivonen (iivonen@iki.fi)
 *    Zbigniew Chyla (cyba@gnome.pl)
 */
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "gutils.h"

#include "sheet.h"
#include "ranges.h"
#include "mathfunc.h"

#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <gsf/gsf-impl-utils.h>
#ifdef HAVE_FLOATINGPOINT_H
#include <floatingpoint.h>
#endif

/* ------------------------------------------------------------------------- */

static GList *timers_stack = NULL;

void
gnumeric_time_counter_push (void)
{
	GTimer *timer;

	timer = g_timer_new ();
	timers_stack = g_list_prepend (timers_stack, timer);
}

gdouble
gnumeric_time_counter_pop (void)
{
	GTimer *timer;
	gdouble ret_val;

	g_assert (timers_stack != NULL);

	timer = (GTimer *) timers_stack->data;
	timers_stack = g_list_remove (timers_stack, timers_stack->data);
	ret_val = g_timer_elapsed (timer, NULL);
	g_timer_destroy (timer);

	return ret_val;
}

/***************************************************************************/

gunichar const *
g_unichar_strchr (gunichar const *str, gunichar c)
{
	g_return_val_if_fail (str != NULL, NULL);

	for (; *str ; str++)
		if (*str == c)
			return str;
	return NULL;
}

gunichar const *
g_unichar_strstr_utf8 (gunichar const *haystack, gchar const *needle)
{
	gchar const *tmp;
	int i;
	gunichar start;

	g_return_val_if_fail (haystack != NULL, NULL);
	g_return_val_if_fail (haystack[0], NULL);
	g_return_val_if_fail (needle != NULL, NULL);

	start = g_utf8_get_char (needle);
	while (NULL != (haystack = g_unichar_strchr (haystack, start))) {
		i = 0;
		tmp = needle;
		do {
			if (haystack[++i] == 0)
				return haystack;
			tmp = g_utf8_next_char (tmp);
		} while (g_utf8_get_char (tmp) == haystack[i]);
		haystack++;
	}
	return NULL;
}

size_t
g_unichar_strlen (gunichar const *str)
{
	int i = 0;
	while (*str++)
		i++;
	return i;
}

int
g_unichar_strncmp (gunichar const *a, gunichar const *b, size_t n)
{
	while (n > 0 && *a == *b)
		a++, b++, n--;

	if (n > 0)
		return *a - *b;
	return 0;
}

/***************************************************************************/
void
g_ptr_array_insert (GPtrArray *array, gpointer value, int index)
{
	if ((int)array->len != index) {
		int i = array->len - 1;
		gpointer last = g_ptr_array_index (array, i);
		g_ptr_array_add (array, last);

		while (i-- > index) {
			gpointer tmp = g_ptr_array_index (array, i);
			g_ptr_array_index (array, i + 1) = tmp;
		}
		g_ptr_array_index (array, index) = value;
	} else
		g_ptr_array_add (array, value);
}

/**
 * g_create_list:
 * @item1: First item.
 *
 * Creates a GList from NULL-terminated list of arguments.
 *
 * Return value: created list.
 */
GList *
g_create_list (gpointer item1, ...)
{
	va_list args;
	GList *list = NULL;
	gpointer item;

	va_start (args, item1);
	for (item = item1; item != NULL; item = va_arg (args, gpointer)) {
		list = g_list_prepend (list, item);
	}
	va_end (args);

	return g_list_reverse (list);
}

/**
 * g_create_list:
 * @item1: First item.
 *
 * Creates a GList from NULL-terminated list of arguments.
 *
 * Return value: created list.
 */
GSList *
g_create_slist (gpointer item1, ...)
{
	va_list args;
	GSList *list = NULL;
	gpointer item;

	va_start (args, item1);
	for (item = item1; item != NULL; item = va_arg (args, gpointer)) {
		list = g_slist_prepend (list, item);
	}
	va_end (args);

	return g_slist_reverse (list);
}

gint
g_list_index_custom (GList *list, gpointer data, GCompareFunc cmp_func)
{
	GList *l;
	gint i;

	for (l = list, i = 0; l != NULL; l = l->next, i++) {
		if (cmp_func (l->data, data) == 0) {
			return i;
		}
	}

	return -1;
}

/**
 * g_list_free_custom:
 * @list: list of some items
 * @free_func: function freeing list item
 *
 * Clears a list, calling @free_func for each list item.
 *
 */
void
g_list_free_custom (GList *list, GFreeFunc free_func)
{
	GList *l;

	for (l = list; l != NULL; l = l->next) {
		free_func (l->data);
	}
	g_list_free (list);
}

/**
 * g_slist_map:
 * @list        : list of some items
 * @map_func    : mapping function
 */
GSList *
g_slist_map (GSList *list, GnmMapFunc map_func)
{
	GSList *list_copy = NULL;

	GNM_SLIST_FOREACH (list, void, value,
		GNM_SLIST_PREPEND (list_copy, map_func (value))
	);

	return g_slist_reverse (list_copy);
}

/**
 * g_strsplit_to_slist:
 * @string: String to split
 * @delimiter: Token delimiter
 *
 * Splits up string into tokens at delim and returns a string list.
 *
 * Return value: string list which you should free after use using function
 * e_free_string_list().
 *
 */
GSList *
g_strsplit_to_slist (gchar const *string, gchar const *delimiter)
{
	gchar **token_v;
	GSList *string_list = NULL;
	gint i;

	token_v = g_strsplit (string, delimiter, 0);
	if (token_v != NULL) {
		for (i = 0; token_v[i] != NULL; i++) {
			string_list = g_slist_prepend (string_list, token_v[i]);
		}
		string_list = g_slist_reverse (string_list);
		g_free (token_v);
	}

	return string_list;
}

/**
 * g_slist_free_custom:
 * @list: list of some items
 * @free_func: function freeing list item
 *
 * Clears a list, calling @free_func for each list item.
 *
 */
void
g_slist_free_custom (GSList *list, GFreeFunc free_func)
{
	GSList *l;

	for (l = list; l != NULL; l = l->next) {
		free_func (l->data);
	}
	g_slist_free (list);
}

gint
gnumeric_utf8_collate_casefold (const char *a, const char *b)
{
	char *a2 = g_utf8_casefold (a, -1);
	char *b2 = g_utf8_casefold (b, -1);
	int res = g_utf8_collate (a2, b2);
	g_free (a2);
	g_free (b2);
	return res;
}

gint
gnumeric_ascii_strcase_equal (gconstpointer v1, gconstpointer v2)
{
	return g_ascii_strcasecmp ((char const *) v1, (char const *)v2) == 0;
}

/* a char* hash function from ASU */
guint
gnumeric_ascii_strcase_hash (gconstpointer v)
{
	unsigned const char *s = (unsigned const char *)v;
	unsigned const char *p;
	guint h = 0, g;

	for(p = s; *p != '\0'; p += 1) {
		h = ( h << 4 ) + g_ascii_tolower (*p);
		if ( ( g = h & 0xf0000000 ) ) {
			h = h ^ (g >> 24);
			h = h ^ g;
		}
	}

	return h /* % M */;
}

extern char *gnumeric_data_dir;
char *
gnumeric_sys_data_dir (char const *subdir)
{
	if (subdir == NULL)
		return (char *)gnumeric_data_dir;
	return g_strconcat (gnumeric_data_dir, G_DIR_SEPARATOR_S,
			    subdir, G_DIR_SEPARATOR_S, NULL);
}

extern char *gnumeric_lib_dir;
char *
gnumeric_sys_lib_dir (char const *subdir)
{
	return g_strconcat (gnumeric_lib_dir, G_DIR_SEPARATOR_S,
			    subdir, G_DIR_SEPARATOR_S, NULL);
}

#define GLADE_SUFFIX	"glade"
#define PLUGIN_SUFFIX	"plugins"

char *
gnumeric_sys_glade_dir (void)
{
	return gnumeric_sys_data_dir (GLADE_SUFFIX);
}

char *
gnumeric_sys_plugin_dir (void)
{
	return gnumeric_sys_lib_dir (PLUGIN_SUFFIX);
}

char *
gnumeric_usr_dir (char const *subdir)
{
	char const *home_dir = g_get_home_dir ();

	if (home_dir != NULL) {
		gboolean has_slash = (home_dir[strlen (home_dir) - 1] == G_DIR_SEPARATOR);
		return g_strconcat (home_dir, (has_slash ? "" : G_DIR_SEPARATOR_S),
				    ".gnumeric" G_DIR_SEPARATOR_S GNUMERIC_VERSION G_DIR_SEPARATOR_S,
				    subdir, G_DIR_SEPARATOR_S,
				    NULL);
	}
	return NULL;
}

char *
gnumeric_usr_plugin_dir (void)
{
	return gnumeric_usr_dir (PLUGIN_SUFFIX);
}

/* ------------------------------------------------------------------------- */

/*
 * Escapes all backslashes and quotes in a string. It is based on glib's
 * g_strescape.
 *
 * Also adds quotes around the result.
 */
void
gnm_strescape (GString *target, char const *string)
{
	g_string_append_c (target, '"');
	/* This loop should be UTF-8 safe.  */
	for (; *string; string++) {
		switch (*string) {
		case '"':
		case '\\':
			g_string_append_c (target, '\\');
		default:
			g_string_append_c (target, *string);
		}
	}
	g_string_append_c (target, '"');
}

/*
 * The reverse operation of gnm_strescape.  Returns a pointer to the
 * first char after the string on success or NULL on failure.
 *
 * First character of the string should be an ASCII character used
 * for quoting.
 */
const char *
gnm_strunescape (GString *target, const char *string)
{
	char quote = *string++;
	size_t oldlen = target->len;

	/* This should be UTF-8 safe as long as quote is ASCII.  */
	while (*string != quote) {
		if (*string == 0)
			goto error;
		else if (*string == '\\') {
			string++;
			if (*string == 0)
				goto error;
		}

		g_string_append_c (target, *string);
		string++;
	}

	return ++string;

 error:
	g_string_truncate (target, oldlen);
	return NULL;
}

/* ------------------------------------------------------------------------- */

#ifdef NEED_FAKE_MODFGNUM
gnm_float
modfgnum (gnm_float x, gnm_float *iptr)
{
	double di;
	static gboolean warned = FALSE;

	if (!warned) {
		warned = TRUE;
		g_warning (_("This version of Gnumeric has been compiled with inadequate precision in modfgnum."));
	}

	/* Throw away the fractional part.  Hope the integer part fits.  */
	(void)modf (x, &di);
	*iptr = di;

	return x - di;
}
#endif

/* ------------------------------------------------------------------------- */

#ifdef NEED_FAKE_STRTOGNUM
gnm_float
strtognum (char const *str, char **end)
{
#if defined(HAVE_STRING_TO_DECIMAL) && defined(HAVE_DECIMAL_TO_QUADRUPLE)
	gnm_float res;
	decimal_record dr;
	enum decimal_string_form form;
	decimal_mode dm;
	fp_exception_field_type excp;
	char *echar;

	string_to_decimal ((char **)&str, INT_MAX, 0,
			   &dr, &form, &echar);
	if (end) *end = (char *)str;

	if (form == invalid_form) {
		errno = EINVAL;
		return 0.0;
	}

	dm.rd = fp_nearest;
	decimal_to_quadruple (&res, &dm, &dr, &excp);
        if (excp & ((1 << fp_overflow) | (1 << fp_underflow)))
                errno = ERANGE;
	return res;
#else
	char *myend;
	gnm_float res;
	int count;

	if (end == 0) end = &myend;
	(void) strtod (str, end);
	if (str == *end)
		return 0;

	errno = 0;
	count = sscanf (str, "%Lf", &res);
	if (count == 1)
		return res;

	/* Now what?  */
	*end = (char *)str;
	errno = ERANGE;
	return 0.0;
#endif
}
#endif

/* ------------------------------------------------------------------------- */

#ifdef NEED_FAKE_LDEXPGNUM
gnm_float
ldexpgnum (gnm_float x, int exp)
{
	if (!finitegnum (x) || x == 0)
		return x;
	else {
		gnm_float res = x * gpow2 (exp);
		if (finitegnum (res))
			return res;
		else {
			errno = ERANGE;
			return (x > 0) ? HUGE_VAL : -HUGE_VAL;
		}
	}
}
#endif

/* ------------------------------------------------------------------------- */

#ifdef NEED_FAKE_FREXPGNUM
gnm_float
frexpgnum (gnm_float x, int *exp)
{
	static gboolean warned = FALSE;
	double dbl_res;
	gnm_float res;

	if (!warned) {
		warned = TRUE;
		g_warning (_("This version of Gnumeric has been compiled with inadequate precision in frexpgnum."));
	}

	/* This might underflow or overflow in the cast.  */
	dbl_res = frexp ((double)x, exp);
	if (!finitegnum (x) || x == 0)
		return dbl_res;

	/*
	 * Now correct the result and adjust things that might have gotten
	 * off-by-one due to rounding.  This part has all the precision it
	 * needs.
	 */
	res = x / gpow2 (*exp);
	if (gnumabs (res) >= 1.0)
		res /= 2, (*exp)++;
	else if (gnumabs (res) < 0.5)
		res *= 2, (*exp)--;

	return res;
}
#endif

/* ------------------------------------------------------------------------- */

#ifdef NEED_FAKE_ERFGNUM
gnm_float
erfgnum (gnm_float x)
{
	/* FIXME: this looks like it might lack precision for x near zero.  */
	return pnorm (x * M_SQRT2gnum, 0, 1, TRUE, FALSE) * 2 - 1;
}
#endif

/* ------------------------------------------------------------------------- */

#ifdef NEED_FAKE_ERFCGNUM
gnm_float
erfcgnum (gnm_float x)
{
	return 2 * pnorm (x * M_SQRT2gnum, 0, 1, FALSE, FALSE);
}
#endif

/* ------------------------------------------------------------------------- */

#ifdef NEED_FAKE_YNGNUM
gnm_float
yngnum (int n, gnm_float x)
{
	static gboolean warned = FALSE;
	if (!warned) {
		warned = TRUE;
		g_warning (_("This version of Gnumeric has been compiled with inadequate precision in yngnum."));
	}

	return (gnm_float)yn (n, (double)x);
}
#endif

/* ------------------------------------------------------------------------- */
/**
 * gnumeric_utf8_strcapital:
 * @p: pointer to UTF-8 string
 * @len: length in bytes, or -1.
 *
 * Similar to g_utf8_strup and g_utf8_strup, except that this function
 * creates a string "Very Much Like: This, One".
 *
 * Return value: newly allocated string.
 */

char *
gnumeric_utf8_strcapital (const char *p, ssize_t len)
{
	const char *pend = (len < 0 ? NULL : p + len);
	GString *res = g_string_sized_new (len < 0 ? 1 : len + 1);
	gboolean up = TRUE;
	char *result;

	/*
	 * This does a simple character-by-character mapping and probably
	 * is not linguistically correct.
	 */

	for (; (len < 0 || p < pend) && *p; p = g_utf8_next_char (p)) {
		gunichar c = g_utf8_get_char (p);

		if (g_unichar_isalpha (c)) {
			if (up ? g_unichar_isupper (c) : g_unichar_islower (c))
				/* Correct case -- keep the char.  */
				g_string_append_unichar (res, c);
			else {
				char *tmp = up
					? g_utf8_strup (p, 1)
					: g_utf8_strdown (p, 1);
				g_string_append (res, tmp);
				g_free (tmp);
			}
			up = FALSE;
		} else {
			g_string_append_unichar (res, c);
			up = TRUE;
		}
	}

	result = res->str;
	g_string_free (res, FALSE);
	return result;
}

/* ------------------------------------------------------------------------- */

gboolean
gnumeric_valid_filename (const char *filename)
{
	GError *err = NULL;
	char *utf8name;
	gboolean res;

	g_return_val_if_fail (filename != NULL, FALSE);

	utf8name = g_filename_to_utf8 (filename, -1, NULL, NULL, &err);
	res = utf8name && !err;
	g_free (utf8name);
	g_clear_error (&err);

	return res;
}

/* ------------------------------------------------------------------------- */

#undef DEBUG_CHUNK_ALLOCATOR

typedef struct _gnm_mem_chunk_freeblock gnm_mem_chunk_freeblock;
typedef struct _gnm_mem_chunk_block gnm_mem_chunk_block;

struct _gnm_mem_chunk_freeblock {
	gnm_mem_chunk_freeblock *next;
};

struct _gnm_mem_chunk_block {
	gpointer data;
	int freecount, nonalloccount;
	gnm_mem_chunk_freeblock *freelist;
#ifdef DEBUG_CHUNK_ALLOCATOR
	int id;
#endif
};

struct _gnm_mem_chunk {
	char *name;
	size_t atom_size, user_atom_size, chunk_size, alignment;
	int atoms_per_block;

	/* List of all blocks.  */
	GSList *blocklist;

	/* List of blocks that are not full.  */
	GList *freeblocks;

#ifdef DEBUG_CHUNK_ALLOCATOR
	int blockid;
#endif
};


gnm_mem_chunk *
gnm_mem_chunk_new (char const *name, size_t user_atom_size, size_t chunk_size)
{
	int atoms_per_block;
	gnm_mem_chunk *res;
	size_t user_alignment, alignment, atom_size;
	size_t maxalign = 1 + ((sizeof (void *) - 1) |
			       (sizeof (long) - 1) |
			       (sizeof (double) - 1) |
			       (sizeof (gnm_float) - 1));

	/*
	 * The alignment that the caller can expect is 2^(lowest_bit_in_size).
	 * The fancy bit math computes this.  Think it over.
	 *
	 * We don't go lower than pointer-size, so this always comes out as
	 * 4 or 8.  (Or 16, if gnm_float is long double.)  Sometimes, when
	 * user_atom_size is a multiple of 8, this alignment is bigger than
	 * really needed, but we don't know if the structure has elements
	 * with 8-byte alignment.  In those cases we waste memory.
	 */
	user_alignment = ((user_atom_size ^ (user_atom_size - 1)) + 1) / 2;
	alignment = MIN (MAX (user_alignment, sizeof (gnm_mem_chunk_block *)), maxalign);
	atom_size = alignment + MAX (user_atom_size, sizeof (gnm_mem_chunk_freeblock));
	atoms_per_block = MAX (1, chunk_size / atom_size);
	chunk_size = atoms_per_block * atom_size;

#ifdef DEBUG_CHUNK_ALLOCATOR
	g_print ("Created %s with alignment=%d, atom_size=%d (%d), chunk_size=%d.\n",
		 name, alignment, atom_size, user_atom_size,
		 chunk_size);
#endif

	res = g_new (gnm_mem_chunk, 1);
	res->alignment = alignment;
	res->name = g_strdup (name);
	res->user_atom_size = user_atom_size;
	res->atom_size = atom_size;
	res->chunk_size = chunk_size;
	res->atoms_per_block = atoms_per_block;
	res->blocklist = NULL;
	res->freeblocks = NULL;
#ifdef DEBUG_CHUNK_ALLOCATOR
	res->blockid = 0;
#endif

	return res;
}

void
gnm_mem_chunk_destroy (gnm_mem_chunk *chunk, gboolean expect_leaks)
{
	GSList *l;

	g_return_if_fail (chunk != NULL);

#ifdef DEBUG_CHUNK_ALLOCATOR
	g_print ("Destroying %s.\n", chunk->name);
#endif
	/*
	 * Since this routine frees all memory allocated for the pool,
	 * it is sometimes convenient not to free at all.  For such
	 * cases, don't report leaks.
	 */
	if (!expect_leaks) {
		GSList *l;
		int leaked = 0;

		for (l = chunk->blocklist; l; l = l->next) {
			gnm_mem_chunk_block *block = l->data;
			leaked += chunk->atoms_per_block - (block->freecount + block->nonalloccount);
		}
		if (leaked) {
			g_warning ("Leaked %d nodes from %s.",
				   leaked, chunk->name);
		}
	}

	for (l = chunk->blocklist; l; l = l->next) {
		gnm_mem_chunk_block *block = l->data;
		g_free (block->data);
		g_free (block);
	}
	g_slist_free (chunk->blocklist);
	g_list_free (chunk->freeblocks);
	g_free (chunk->name);
	g_free (chunk);
}

gpointer
gnm_mem_chunk_alloc (gnm_mem_chunk *chunk)
{
	gnm_mem_chunk_block *block;
	char *res;

	/* First try the freelist.  */
	if (chunk->freeblocks) {
		gnm_mem_chunk_freeblock *res;

		block = chunk->freeblocks->data;
		res = block->freelist;
		if (res) {
			block->freelist = res->next;

			block->freecount--;
			if (block->freecount == 0 && block->nonalloccount == 0) {
				/* Block turned full -- remove it from freeblocks.  */
				chunk->freeblocks = g_list_delete_link (chunk->freeblocks,
									chunk->freeblocks);
			}
			return res;
		}
		/*
		 * If we get here, the block has free space that was never
		 * allocated.
		 */
	} else {
		block = g_new (gnm_mem_chunk_block, 1);
#ifdef DEBUG_CHUNK_ALLOCATOR
		block->id = chunk->blockid++;
		g_print ("Allocating new chunk %d for %s.\n", block->id, chunk->name);
#endif
		block->nonalloccount = chunk->atoms_per_block;
		block->freecount = 0;
		block->data = g_malloc (chunk->chunk_size);
		block->freelist = NULL;

		chunk->blocklist = g_slist_prepend (chunk->blocklist, block);
		chunk->freeblocks = g_list_prepend (chunk->freeblocks, block);
	}

	res = (char *)block->data +
		(chunk->atoms_per_block - block->nonalloccount--) * chunk->atom_size;
	*((gnm_mem_chunk_block **)res) = block;

	if (block->nonalloccount == 0 && block->freecount == 0) {
		/* Block turned full -- remove it from freeblocks.  */
		chunk->freeblocks = g_list_delete_link (chunk->freeblocks, chunk->freeblocks);
	}

	return res + chunk->alignment;
}

gpointer
gnm_mem_chunk_alloc0 (gnm_mem_chunk *chunk)
{
	gpointer res = gnm_mem_chunk_alloc (chunk);
	memset (res, 0, chunk->user_atom_size);
	return res;
}

void
gnm_mem_chunk_free (gnm_mem_chunk *chunk, gpointer mem)
{
	gnm_mem_chunk_freeblock *fb = mem;
	gnm_mem_chunk_block *block =
		*((gnm_mem_chunk_block **)((char *)mem - chunk->alignment));

#if 0
	/*
	 * This is useful when the exact location of a leak needs to be
	 * pin-pointed.
	 */
	memset (mem, 0, chunk->user_atom_size);
#endif

	fb->next = block->freelist;
	block->freelist = fb;
	block->freecount++;

	if (block->freecount == 1 && block->nonalloccount == 0) {
		/* Block turned non-full.  */
		chunk->freeblocks = g_list_prepend (chunk->freeblocks, block);
	} else if (block->freecount == chunk->atoms_per_block) {
		/* Block turned all-free.  */

#ifdef DEBUG_CHUNK_ALLOCATOR
		g_print ("Releasing chunk %d for %s.\n", block->id, chunk->name);
#endif
		/*
		 * FIXME -- this could be faster if we rolled our own lists.
		 * Hopefully, however, (a) the number of blocks is small,
		 * and (b) the freed block might be near the beginning ("top")
		 * of the stacks.
		 */
		chunk->blocklist = g_slist_remove (chunk->blocklist, block);
		chunk->freeblocks = g_list_remove (chunk->freeblocks, block);

		g_free (block->data);
		g_free (block);
	}
}

/*
 * Loop over all non-freed memory in the chunk.  It's safe to allocate or free
 * from the chunk in the callback.
 */
void
gnm_mem_chunk_foreach_leak (gnm_mem_chunk *chunk, GFunc cb, gpointer user)
{
	GSList *l, *leaks = NULL;

	for (l = chunk->blocklist; l; l = l->next) {
		gnm_mem_chunk_block *block = l->data;
		if (chunk->atoms_per_block - (block->freecount + block->nonalloccount) > 0) {
			char *freed = g_new0 (char, chunk->atoms_per_block);
			gnm_mem_chunk_freeblock *fb = block->freelist;
			int i;

			while (fb) {
				char *atom = (char *)fb - chunk->alignment;
				int no = (atom - (char *)block->data) / chunk->atom_size;
				freed[no] = 1;
				fb = fb->next;
			}

			for (i = chunk->atoms_per_block - block->nonalloccount - 1; i >= 0; i--) {
				if (!freed[i]) {
					char *atom = (char *)block->data + i * chunk->atom_size;
					leaks = g_slist_prepend (leaks, atom + chunk->alignment);
				}
			}
			g_free (freed);
		}
	}

	g_slist_foreach (leaks, cb, user);
	g_slist_free (leaks);
}

int
g_str_compare (void const *x, void const *y)
{
	if (x == NULL || y == NULL) {
		if (x == y)
			return 0;
		else
			return x ? -1 : 1;
	} 

	return strcmp (x, y);
}
