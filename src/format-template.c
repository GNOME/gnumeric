/*
 * format-template.c : implementation of the template handling system.
 *
 * Copyright (C) Almer. S. Tigelaar.
 * E-mail: almer1@dds.nl or almer-t@bigfoot.com
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <config.h>
#include "format-template.h"
#include "mstyle.h"
#include "xml-io-autoft.h"
#include "sheet.h"
#include "sheet-style.h"
#include "style-border.h"
#include "command-context.h"
#include "ranges.h"
#include <gnome.h>

/******************************************************************************
 * Hash table related callbacks and functions
 ******************************************************************************
 *
 * These are basically wrapper function around GHashTable ment for use by
 * the FormatTemplate.
 * The hashtable will manage it's own resources, it will copy values instead
 * of using the ones passed.
 */

/**
 * hash_table_destroy_entry_cb:
 * @pkey:
 * @pvalue:
 * @data:
 *
 * Callback for destroying a single key/value pair from a GHashTable
 *
 * Returns : Always TRUE
 **/
static gboolean
hash_table_destroy_entry_cb (gpointer pkey, gpointer pvalue, gpointer data)
{
	g_free ((guint *) pkey);
	mstyle_unref ((MStyle *) pvalue);

	return TRUE;
}

/**
 * hash_table_destroy:
 * @table:
 *
 * Destroy the hashtable (including the keys and values)
 *
 * Returns : Always NULL
 **/
static GHashTable *
hash_table_destroy (GHashTable *table)
{
	int size, removed;

	if (table == NULL)
		return NULL;

	/*
	 * Remove all items from the table, compare the number of
	 * removed items to the number of items in the table
	 * before removal as a sanity check
	 */
	size = g_hash_table_size (table);
	removed = g_hash_table_foreach_remove (table, hash_table_destroy_entry_cb, NULL);

	if (removed != size)
		g_warning ("format-template.c: Not all items removed from hash table!");

	g_hash_table_destroy (table);

	return NULL;
}

/**
 * hash_table_create:
 *
 * Create a new hashtable for the format template
 *
 * Returns : A new hashtable
 **/
static GHashTable *
hash_table_create (void)
{
	return g_hash_table_new (g_int_hash, g_int_equal);
}

/**
 * hash_table_generate_key:
 * @row:
 * @col:
 *
 * Generate a key from row, col coordinates.
 * This key is always unique provided that row and col are
 * both below 65536 (2^16).
 *
 * Return value: The key
 **/
static guint
hash_table_generate_key (int row, int col)
{
	guint key;

	key = (row << 16) + col;

	return key;
}

/**
 * hash_table_lookup:
 * @table:
 * @row:
 * @col:
 *
 * Looks up a the value of a row,col pair in the hash table
 *
 * Return value: The MStyle associated with the coordinates
 **/
static MStyle *
hash_table_lookup (GHashTable *table, int row, int col)
{
	MStyle *result;
	guint key;

	key = hash_table_generate_key (row, col);

	result = g_hash_table_lookup (table, &key);

	return result;
}

/**
 * hash_table_insert:
 * @table:
 * @row:
 * @col:
 * @mstyle:
 * @merge_colors:
 *
 * Insert a new entry into the hashtable for row, col. Note that
 * if there is already an existing entry for those coordinates the
 * existing MStyle and the new one will be merges together to form
 * a new style
 **/
static void
hash_table_insert (GHashTable *table, int row, int col, MStyle *mstyle)
{
	MStyle *orig_value = NULL;
	guint *orig_key = NULL;
	guint key;

	g_return_if_fail (table != NULL);
	g_return_if_fail (mstyle != NULL);

	key = hash_table_generate_key (row, col);

	/*
	 * If an entry for this col/row combination does not
	 * yet exist then simply create a new entry in the hash table
	 */
	if (!g_hash_table_lookup_extended (table, &key, (gpointer *) &orig_key, (gpointer *) &orig_value)) {
		guint *pkey = g_new (guint, 1);

		*pkey = key;

		g_hash_table_insert (table, pkey, mstyle_copy (mstyle));
	} else {

		/*
		 * Overwrite any existing entry in the hashtable
		 * FIXME : Is this the right way to handle this?
		 */
		mstyle_unref (orig_value);
		g_hash_table_insert (table, orig_key, mstyle_copy (mstyle));
	}
}

/******************************************************************************
 * FormatColRowInfo - Construction
 ******************************************************************************/

/**
 * format_col_row_info_make:
 * @offset: Desired offset
 * @offset_gravity: Gravity
 * @size: The size
 *
 * This function is simply an easy way to create a FormatColRowInfo
 * instantly
 *
 * Return value: The 3 arguments inside a FormatColRowInfo
 **/
FormatColRowInfo
format_col_row_info_make (int offset, int offset_gravity,
			  int size)
{
	FormatColRowInfo new;

	new.offset = offset;
	new.offset_gravity = offset_gravity;
	new.size = size;

	return new;
}

/******************************************************************************
 * FormatTemplateMember - Getters/setters and creation
 ******************************************************************************/

/**
 * format_template_member_new:
 *
 * Create a new TemplateMember
 *
 * Return value: the new TemplateMember
 **/
TemplateMember *
format_template_member_new (void)
{
	TemplateMember *member;

	member = g_new (TemplateMember, 1);

	format_template_member_set_row_info (member, format_col_row_info_make (0, 1, 1));
	format_template_member_set_col_info (member, format_col_row_info_make (0, 1, 1));
	member->direction = FREQ_DIRECTION_NONE;
	member->repeat    = 0;
	member->skip      = 0;
	member->edge      = 0;
	member->mstyle    = NULL;

	return member;
}

/**
 * format_template_member_clone:
 *
 * Clone a template member
 *
 * Return value: a copy of @member
 **/
TemplateMember *
format_template_member_clone (TemplateMember *member)
{
	TemplateMember *clone;

	clone = format_template_member_new ();

	clone->row = member->row;
	clone->col = member->col;
	clone->direction = member->direction;
	clone->repeat    = member->repeat;
	clone->skip      = member->skip;
	clone->edge      = member->edge;
	clone->mstyle    = member->mstyle;
	mstyle_ref (member->mstyle);

	return clone;
}

/**
 * format_template_member_free:
 * @member: TemplateMember
 *
 * Frees an existing template member
 **/
void
format_template_member_free (TemplateMember *member)
{
	g_return_if_fail (member != NULL);

	if (member->mstyle)
		mstyle_unref (member->mstyle);

	g_free (member);
}


/**
 * format_template_member_get_rect:
 * @member:
 * @x1:
 * @y1:
 * @x2:
 * @y2:
 *
 * Get the rectangular area covered by the TemplateMember @member in the parent
 * rectangle @x1, @y1, @x2, @y2.
 * NOTE : This simply calculates the rectangle, it does not calculate repetitions
 *        or anything. That you'll have to do yourself :-)
 *
 * Return value: a Range containing the effective rectangle of @member
 **/
Range
format_template_member_get_rect (TemplateMember *member, int x1, int y1, int x2, int y2)
{
	Range r;

	r.start.row = r.end.row = 0;
	r.start.col = r.end.col = 0;

	g_return_val_if_fail (member != NULL, r);

	/*
	 * Calculate where the top left of the rectangle will come
	 */
	if (member->row.offset_gravity > 0)
		r.start.row = y1 + member->row.offset;
	else
		r.end.row = y2 - member->row.offset;

	if (member->col.offset_gravity > 0)
		r.start.col = x1 + member->col.offset;
	else
		r.end.col = x2 - member->col.offset;

	/*
	 * Now that we know these coordinates we'll calculate the
	 * bottom right coordinates
	 */
	if (member->row.offset_gravity > 0) {
		if (member->row.size > 0)
			r.end.row = r.start.row + member->row.size - 1;
		else
			r.end.row = y2 + member->row.size;
	} else {
		if (member->row.size > 0)
			r.start.row = r.end.row - member->row.size + 1;
		else
			r.start.row = y1 - member->row.size;
	}

	if (member->col.offset_gravity > 0) {
		if (member->col.size > 0)
			r.end.col = r.start.col + member->col.size - 1;
		else
			r.end.col = x2 + member->col.size;
	} else {
		if (member->col.size > 0)
			r.start.col = r.end.col - member->col.size + 1;
		else
			r.start.col = x1 - member->col.size;
	}

	return r;
}

/******************************************************************************
 * Getters and setters for FormatTemplateMember
 *
 * NOTE : MStyle are taken care of internally, there is no
 *        need to unref or ref mstyle's manually.
 */

FormatColRowInfo
format_template_member_get_row_info (TemplateMember *member)
{
	return member->row;
}

FormatColRowInfo
format_template_member_get_col_info (TemplateMember *member)
{
	return member->col;
}

FreqDirection
format_template_member_get_direction (TemplateMember *member)
{
	return member->direction;
}

int
format_template_member_get_repeat (TemplateMember *member)
{
	return member->repeat;
}

int
format_template_member_get_skip (TemplateMember *member)
{
	return member->skip;
}

int
format_template_member_get_edge (TemplateMember *member)
{
	return member->edge;
}

MStyle *
format_template_member_get_style (TemplateMember *member)
{
	return member->mstyle;
}

void
format_template_member_set_row_info (TemplateMember *member, FormatColRowInfo row_info)
{
	g_return_if_fail (row_info.offset >= 0);
	g_return_if_fail (row_info.offset_gravity == -1 || row_info.offset_gravity == +1);

	member->row = row_info;
}

void
format_template_member_set_col_info (TemplateMember *member, FormatColRowInfo col_info)
{
	g_return_if_fail (col_info.offset >= 0);
	g_return_if_fail (col_info.offset_gravity == -1 || col_info.offset_gravity == +1);

	member->col = col_info;
}

void
format_template_member_set_direction (TemplateMember *member, FreqDirection direction)
{
	g_return_if_fail (direction == FREQ_DIRECTION_NONE || direction == FREQ_DIRECTION_HORIZONTAL ||
			  direction == FREQ_DIRECTION_VERTICAL);

	member->direction = direction;
}

void
format_template_member_set_repeat (TemplateMember *member, int repeat)
{
	g_return_if_fail (repeat >= -1);

	member->repeat = repeat;
}

void
format_template_member_set_skip (TemplateMember *member, int skip)
{
	g_return_if_fail (skip >= 0);

	member->skip = skip;
}

void
format_template_member_set_edge (TemplateMember *member, int edge)
{
	g_return_if_fail (edge >= 0);

	member->edge = edge;
}

void
format_template_member_set_style (TemplateMember *member, MStyle *mstyle)
{
	MStyle *mstyle_default;
	
	g_return_if_fail (mstyle != NULL);

	if (member->mstyle)
		mstyle_unref (member->mstyle);

	/*
	 * We need to do some magic here. The problem is that the new
	 * mstyle might not have _all_ elements set and we _do_ need it
	 * to have all elements set. We therefore merge with the default
	 * mstyle.
	 */
	mstyle_default = mstyle_new_default ();
	member->mstyle = mstyle_copy_merge (mstyle_default, mstyle);
	mstyle_unref (mstyle_default);
}

/******************************************************************************
 * FormatTemplate - Creation/Destruction
 ******************************************************************************/

/**
 * format_template_new:
 * @context: a WorkbookControl
 *
 * Create a new 'empty' FormatTemplate
 *
 * Return value: the new FormatTemplate
 **/
FormatTemplate *
format_template_new (WorkbookControl *context)
{
	FormatTemplate *ft;

	g_return_val_if_fail (context != NULL, NULL);

	ft = g_new0 (FormatTemplate, 1);

	ft->filename    = g_string_new ("");

	ft->author      = g_string_new (g_get_real_name ());
	ft->name        = g_string_new (_("Name"));
	ft->description = g_string_new ("");

	ft->category = NULL;

	ft->members = NULL;
	ft->context = context;

	ft->number    = TRUE;
	ft->border    = TRUE;
	ft->font      = TRUE;
	ft->patterns  = TRUE;
	ft->alignment = TRUE;

	ft->table     = hash_table_create ();
	ft->invalidate_hash = TRUE;

	ft->x1 = ft->y1 = 0;
	ft->x2 = ft->y2 = 0;

	return ft;
}

/**
 * format_template_free:
 * @ft: FormatTemplate
 *
 * Free @ft
 *
 **/
void
format_template_free (FormatTemplate *ft)
{
	GSList *iterator;

	g_return_if_fail (ft != NULL);

	g_string_free (ft->filename, TRUE);

	g_string_free (ft->author, TRUE);
	g_string_free (ft->name, TRUE);
	g_string_free (ft->description, TRUE);

	iterator = ft->members;
	while (iterator) {
		TemplateMember *member = iterator->data;

		format_template_member_free (member);

		iterator = g_slist_next (iterator);
	}
	g_slist_free (ft->members);

	ft->table = hash_table_destroy (ft->table);

	g_free (ft);
}

/**
 * format_template_clone:
 * @ft: FormatTemplate
 *
 * Make a copy of @ft.
 *
 * Returns : a copy of @ft
 **/
FormatTemplate *
format_template_clone (FormatTemplate *ft)
{
	FormatTemplate *clone;
	GSList *iterator = NULL;

	g_return_val_if_fail (ft != NULL, NULL);

	clone = format_template_new (ft->context);

	clone->filename    = g_string_new (ft->filename->str);
	clone->author      = g_string_new (ft->author->str);
	clone->name        = g_string_new (ft->name->str);
	clone->description = g_string_new (ft->description->str);

	clone->category    = ft->category;

	iterator = ft->members;
	while (iterator) {
		TemplateMember *member = format_template_member_clone ((TemplateMember *) iterator->data);

		format_template_attach_member (clone, member);
		iterator = g_slist_next (iterator);
	}

	clone->number    = ft->number;
	clone->border    = ft->border;
	clone->font      = ft->font;
	clone->patterns  = ft->patterns;
	clone->alignment = ft->alignment;

	clone->x1        = ft->x1;
	clone->y1        = ft->y1;
	clone->x2        = ft->x2;
	clone->y2        = ft->y2;

	clone->invalidate_hash = TRUE;

	return clone;
}

/**
 * format_template_new_from_file:
 * @context: a WorkbookControl
 * @filename: The filename to load from
 *
 * Create a new FormatTemplate and load a template file
 * into it.
 *
 * Return value: a new FormatTemplate (or NULL on error)
 **/
FormatTemplate *
format_template_new_from_file (WorkbookControl *context, const char *filename)
{
	FormatTemplate *ft;

	g_return_val_if_fail (context != NULL, NULL);
	g_return_val_if_fail (filename != NULL, NULL);

	if (!g_file_exists (filename))
		return NULL;

	ft = format_template_new (context);

	g_string_free (ft->filename, TRUE);
	ft->filename = g_string_new (filename);

	if (gnumeric_xml_read_format_template (ft->context, ft, filename) != 0) {

		format_template_free (ft);
		return NULL;
	}

	return ft;
}

/**
 * format_template_save_to_file:
 * @ft: a FormatTemplate
 *
 * Saves template @ft to a filename set with format_template_set_filename
 *
 * Return value: 0 on success, or -1 on error.
 **/
int
format_template_save (FormatTemplate *ft)
{
	g_return_val_if_fail (ft != NULL, -1);

	return gnumeric_xml_write_format_template (ft->context, ft, ft->filename->str);
}

/**
 * format_template_attach_member:
 * @ft: FormatTemplate
 * @member: the new member to attach
 *
 * Attaches @member to template @ft
 **/
void
format_template_attach_member (FormatTemplate *ft, TemplateMember *member)
{
	g_return_if_fail (ft != NULL);
	g_return_if_fail (member != NULL);

	/*
	 * NOTE : Append is slower, but that's not really an issue
	 *        here, because a FormatTemplate will most likely
	 *        not have 'that many' members anyway
	 */
	ft->members = g_slist_append (ft->members, member);
}

/**
 * format_template_detach_member:
 * @ft: FormatTemplate
 * @member: a TemplateMember
 *
 * Detaches @member from template @ft
 **/
void
format_template_detach_member (FormatTemplate *ft, TemplateMember *member)
{
	g_return_if_fail (ft != NULL);
	g_return_if_fail (member != NULL);

	ft->members = g_slist_remove (ft->members, member);
}

/**
 * format_template_compare_name:
 * @ft_a: First FormatTemplate
 * @ft_b: Second FormatTemplate
 *
 **/
gint
format_template_compare_name (gconstpointer a, gconstpointer b)
{
	FormatTemplate *ft_a = (FormatTemplate *) a,
	               *ft_b = (FormatTemplate *) b;

	return strcmp (ft_a->name->str, ft_b->name->str);
}

/******************************************************************************
 * FormatTemplate - Actual implementation (Filtering and calculating)
 ******************************************************************************/

/**
 * format_template_filter_style:
 * @ft:
 * @mstyle:
 * @fill_defaults: If set fill in the gaps with the "default" mstyle.
 *
 * Filter an mstyle and strip and replace certain elements
 * based on what the user wants to apply.
 * Basically you should pass FALSE as @fill_defaults, unless you want to have
 * a completely filled style to be returned. If you set @fill_default to TRUE
 * the returned mstyle might have some of it's elements 'not set'
 *
 * Return value: The same mstyle as @mstyle with most likely some modifications
 **/
static MStyle *
format_template_filter_style (FormatTemplate *ft, MStyle *mstyle, gboolean fill_defaults)
{
	g_return_val_if_fail (ft != NULL, NULL);
	g_return_val_if_fail (mstyle != NULL, NULL);

	/*
	 * Don't fill with defaults, this is perfect for when the
	 * mstyles are going to be 'merged' with other mstyles which
	 * have all their elements set
	 */
	if (!fill_defaults) {

		if (!ft->number) {
			mstyle_unset_element (mstyle, MSTYLE_FORMAT);
		}
		if (!ft->border) {
			mstyle_unset_element (mstyle, MSTYLE_BORDER_TOP);
			mstyle_unset_element (mstyle, MSTYLE_BORDER_BOTTOM);
			mstyle_unset_element (mstyle, MSTYLE_BORDER_LEFT);
			mstyle_unset_element (mstyle, MSTYLE_BORDER_RIGHT);
			mstyle_unset_element (mstyle, MSTYLE_BORDER_DIAGONAL);
			mstyle_unset_element (mstyle, MSTYLE_BORDER_REV_DIAGONAL);
		}
		if (!ft->font) {
			mstyle_unset_element (mstyle, MSTYLE_FONT_NAME);
			mstyle_unset_element (mstyle, MSTYLE_FONT_BOLD);
			mstyle_unset_element (mstyle, MSTYLE_FONT_ITALIC);
			mstyle_unset_element (mstyle, MSTYLE_FONT_UNDERLINE);
			mstyle_unset_element (mstyle, MSTYLE_FONT_STRIKETHROUGH);
			mstyle_unset_element (mstyle, MSTYLE_FONT_SIZE);

			mstyle_unset_element (mstyle, MSTYLE_COLOR_FORE);
		}
		if (!ft->patterns) {
			mstyle_unset_element (mstyle, MSTYLE_COLOR_BACK);
			mstyle_unset_element (mstyle, MSTYLE_COLOR_PATTERN);
			mstyle_unset_element (mstyle, MSTYLE_PATTERN);
		}
		if (!ft->alignment) {
			mstyle_unset_element (mstyle, MSTYLE_ALIGN_V);
			mstyle_unset_element (mstyle, MSTYLE_ALIGN_H);
		}
	} else {
		MStyle *mstyle_default = mstyle_new_default ();

		/*
		 * We fill in the gaps with the default mstyle
		 */

		 if (!ft->number) {
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_FORMAT);
		 }
		 if (!ft->border) {
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_BORDER_TOP);
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_BORDER_BOTTOM);
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_BORDER_LEFT);
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_BORDER_RIGHT);
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_BORDER_DIAGONAL);
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_BORDER_REV_DIAGONAL);
		 }
		 if (!ft->font) {
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_FONT_NAME);
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_FONT_BOLD);
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_FONT_ITALIC);
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_FONT_UNDERLINE);
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_FONT_STRIKETHROUGH);
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_FONT_SIZE);

			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_COLOR_FORE);
		 }
		 if (!ft->patterns) {
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_COLOR_BACK);
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_COLOR_PATTERN);
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_PATTERN);
		 }
		 if (!ft->alignment) {
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_ALIGN_V);
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_ALIGN_H);
		 }

		 mstyle_unref (mstyle_default);
	}

	return mstyle;
}

/*
 * Callback used for calculating the styles
 */
typedef void (* PCalcCallback) (FormatTemplate *ft, Range *r, MStyle *mstyle, gpointer data);

/**
 * format_template_range_check:
 * @ft: Format template
 * @s: Target range
 * @display_error: If TRUE will display an error message if @s is not appropriate for @ft.
 *
 * Check wether range @s is big enough to apply format template @ft to it.
 * If this is not the case an error message WILL be displayed if @display_error is TRUE
 *
 * Return value: TRUE if @s is big enough, FALSE if not.
 **/
static gboolean
format_template_range_check (FormatTemplate *ft, Range s, gboolean display_error)
{
	GSList *iterator;
	int diff_col_high = -1;
	int diff_row_high = -1;
	gboolean invalid_range_seen = FALSE;

	g_return_val_if_fail (ft != NULL, FALSE);

	iterator = ft->members;
	while (iterator) {
		TemplateMember *member = iterator->data;
		Range r = format_template_member_get_rect (member, s.start.col, s.start.row,
							   s.end.col, s.end.row);

		if (!range_valid (&r)) {
			int diff_col = (r.start.col - r.end.col);
			int diff_row = (r.start.row - r.end.row);

			if (diff_col > diff_col_high)
				diff_col_high = diff_col;

			if (diff_row > diff_row_high)
				diff_row_high = diff_row;

			invalid_range_seen = TRUE;
		}

		iterator = g_slist_next (iterator);
	}

	if (invalid_range_seen && display_error) {
		int diff_row_high_ft = diff_row_high + (s.end.row - s.start.row) + 1;
		int diff_col_high_ft = diff_col_high + (s.end.col - s.start.col) + 1;
		char *errmsg;

		if (diff_col_high > 0 && diff_row_high > 0) {
			errmsg = g_strdup_printf (_("The target region is too small.  It should be at least %d rows by %d columns"),
							  diff_row_high_ft, diff_col_high_ft);
		} else if (diff_col_high > 0) {
			errmsg = g_strdup_printf (_("The target region is too small.  It should be at least %d columns wide"),
							  diff_col_high_ft);
		} else if (diff_row_high > 0) {
			errmsg = g_strdup_printf (_("The target region is too small.  It should be at least %d rows high"),
							  diff_row_high_ft);
		} else {
			errmsg = NULL;

			g_warning ("Internal error while verifying ranges! (this should not happen!)");
		}

		if (errmsg) {
			gnumeric_error_system (COMMAND_CONTEXT (ft->context), errmsg);
		}

		g_free (errmsg);

		return FALSE;
	} else if (invalid_range_seen) {

		return FALSE;
	}

	return TRUE;
}

/**
 * format_template_calculate:
 * @ft: FormatTemplate
 * @s: Target range
 * @pc: Callback function
 * @cb_data: Data to pass to the callback function
 *
 * Calculate all styles for a range of @s. This routine will invoke the callback function
 * and pass all styles and ranges for those styles to the callback function.
 * The callback function should UNREF the mstyle passed!
 *
 **/
static void
format_template_calculate (FormatTemplate *ft, Range s, PCalcCallback pc, gpointer cb_data)
{
	GSList *iterator;

	g_return_if_fail (ft != NULL);

	/*
	 * Apply all styles
	 */
	iterator = ft->members;
	while (iterator) {
		TemplateMember *member = iterator->data;
		MStyle *mstyle = format_template_member_get_style (member);
		Range r = format_template_member_get_rect (member, s.start.col, s.start.row,
								   s.end.col, s.end.row);

		if (member->direction == FREQ_DIRECTION_NONE) {

			pc (ft, &r, mstyle_copy (mstyle), cb_data);

		} else if (member->direction == FREQ_DIRECTION_HORIZONTAL) {
			int col_repeat = member->repeat;
			Range hr = r;

			while (col_repeat != 0) {
				pc (ft, &hr, mstyle_copy (mstyle), cb_data);

				hr.start.col += member->skip + member->col.size;
				hr.end.col   += member->skip + member->col.size;

				if (member->repeat != -1)
					col_repeat--;
				else {
					if (hr.start.row > s.end.row)
						break;
				}

				if (hr.start.row > s.end.row - member->edge)
					break;
			}
		} else if (member->direction == FREQ_DIRECTION_VERTICAL) {
			int row_repeat = member->repeat;
			Range vr = r;

			while (row_repeat != 0) {
				pc (ft, &vr, mstyle_copy (mstyle), cb_data);

				vr.start.row += member->skip + member->row.size;
				vr.end.row   += member->skip + member->row.size;

				if (member->repeat != -1)
					row_repeat--;
				else {
					if (vr.start.row > s.end.row)
						break;
				}

				if (vr.start.row > s.end.row - member->edge)
					break;
			}
		}


		iterator = g_slist_next (iterator);
	}
}

/******************************************************************************
 * FormatTemplate - Application for the hashtable (previews)
 ******************************************************************************/

static void
cb_format_hash_style (FormatTemplate *ft, Range *r, MStyle *mstyle, GHashTable *table)
{
	int row, col;

	/*
	 * Filter out undesired elements
	 */
	mstyle = format_template_filter_style (ft, mstyle, TRUE);

	for (row = r->start.row; row <= r->end.row; row++) {
		for (col = r->start.col; col <= r->end.col; col++) {

			hash_table_insert (table, row, col, mstyle);
		}
	}

	/*
	 * Unref here, the hashtable will take care of it's own
	 * resources
	 */
	mstyle_unref (mstyle);
}

/**
 * format_template_recalc_hash:
 * @ft: FormatTemplate
 *
 * Refills the hashtable based on new dimensions
 **/
static void
format_template_recalc_hash (FormatTemplate *ft)
{
	Range s;

	g_return_if_fail (ft != NULL);

	ft->table = hash_table_destroy (ft->table);
	ft->table = hash_table_create ();

	g_hash_table_freeze (ft->table);

	s.start.col = ft->x1;
	s.end.col   = ft->x2;
	s.start.row = ft->y1;
	s.end.row   = ft->y2;

	/*
	 * If the range check fails then the template it simply too *huge*
	 * so we don't display an error dialog.
	 */
	if (!format_template_range_check (ft, s, FALSE)) {

		g_warning ("Template %s is too large, hash can't be calculated", ft->name->str);
		g_hash_table_thaw (ft->table);

		return;
	}

	format_template_calculate (ft, s, (PCalcCallback) cb_format_hash_style, ft->table);

	g_hash_table_thaw (ft->table);
}

/**
 * format_template_get_style:
 * @ft:
 * @row:
 * @col:
 *
 * Returns the MStyle associated with coordinates row, col.
 * This routine uses the hash to do this.
 * NOTE : You MAY NOT free the result of this operation,
 *        you may also NOT MODIFY the MStyle returned.
 *        (make a copy first)
 *
 * Return value: an MStyle
 **/
MStyle *
format_template_get_style (FormatTemplate *ft, int row, int col)
{
	MStyle *mstyle;

	g_return_val_if_fail (ft != NULL, NULL);
	g_return_val_if_fail (ft->table != NULL, NULL);

	/*
	 * If the hash isn't filled (as result of resizing) or whatever,
	 * then refill it
	 */
	if (ft->invalidate_hash) {

		ft->invalidate_hash = FALSE;
		format_template_recalc_hash (ft);
	}

	mstyle = hash_table_lookup (ft->table, row, col);

	return mstyle;
}



/******************************************************************************
 * FormatTemplate - Application to Sheet
 ******************************************************************************/

static void
cb_format_sheet_style (FormatTemplate *ft, Range *r, MStyle *mstyle, Sheet *sheet)
{
	g_return_if_fail (ft != NULL);
	g_return_if_fail (r != NULL);
	g_return_if_fail (mstyle != NULL);

	mstyle = format_template_filter_style (ft, mstyle, FALSE);

	/*
	 * We need not unref the mstyle, sheet will
	 * take care of the mstyle
	 */
	sheet_apply_style (sheet, r, mstyle);
}

/**
 * format_template_apply_to_sheet_regions:
 * @ft: FormatTemplate
 * @sheet: the Target sheet
 * @regions: Region list
 *
 * Apply the template to all selected regions in @sheet.
 **/
void
format_template_apply_to_sheet_regions (FormatTemplate *ft, Sheet *sheet, GSList *regions)
{
	GSList *region = NULL;

	/*
	 * First check for range validity
	 */
	region = regions;
	while (region) {
		Range s = *((Range const *) region->data);

		/*
		 * Check if the selected range is valid
		 * if it's not we will abort to avoid a 'spray'
		 * of error dialogs on screen.
		 */
		if (!format_template_range_check (ft, s, TRUE))
			return;

		region = g_slist_next (region);
	}

	/*
	 * Apply the template to all regions
	 */
	for (region = regions; region != NULL ; region = region->next) {
		Range s = *((Range const *) region->data);
		format_template_calculate (ft, s,
			(PCalcCallback) cb_format_sheet_style, sheet);
	}
}

/******************************************************************************
 * Getters and setters for FormatTemplate
 *
 * NOTE : The caller is always responsible for freeing
 *        return variables and variables passed to the functions.
 */

char *
format_template_get_filename (FormatTemplate *ft)
{
	g_return_val_if_fail (ft != NULL, NULL);

	if (ft->filename)
		return g_strdup (ft->filename->str);
	else
		return NULL;
}

char *
format_template_get_name (FormatTemplate *ft)
{
	g_return_val_if_fail (ft != NULL, NULL);

	if (ft->name)
		return g_strdup (ft->name->str);
	else
		return NULL;
}

char *
format_template_get_author (FormatTemplate *ft)
{
	g_return_val_if_fail (ft != NULL, NULL);

	if (ft->author)
		return g_strdup (ft->author->str);
	else
		return NULL;
}

char *
format_template_get_description (FormatTemplate *ft)
{
	g_return_val_if_fail (ft != NULL, NULL);

	if (ft->description)
		return g_strdup (ft->description->str);
	else
		return NULL;
}

GSList *
format_template_get_members (FormatTemplate *ft)
{
	g_return_val_if_fail (ft != NULL, NULL);

	return ft->members;
}

FormatTemplateCategory *
format_template_get_category (FormatTemplate *ft)
{
	g_return_val_if_fail (ft != NULL, NULL);

	return ft->category;
}

void
format_template_set_filename (FormatTemplate *ft, const char *filename)
{
	g_return_if_fail (ft != NULL);
	g_return_if_fail (filename != NULL);

	if (ft->filename)
		g_string_free (ft->filename, TRUE);

	ft->filename = g_string_new (filename);
}

void
format_template_set_name (FormatTemplate *ft, const char *name)
{
	g_return_if_fail (ft != NULL);
	g_return_if_fail (name != NULL);

	if (ft->name)
		g_string_free (ft->name, TRUE);

	ft->name = g_string_new (name);
}

void
format_template_set_author (FormatTemplate *ft, const char *author)
{
	g_return_if_fail (ft != NULL);
	g_return_if_fail (author != NULL);

	if (ft->author)
		g_string_free (ft->author, TRUE);

	ft->author = g_string_new (author);
}

void
format_template_set_description (FormatTemplate *ft, const char *description)
{
	g_return_if_fail (ft != NULL);
	g_return_if_fail (description != NULL);

	if (ft->description)
		g_string_free (ft->description, TRUE);

	ft->description = g_string_new (description);
}

void
format_template_set_category (FormatTemplate *ft, FormatTemplateCategory *category)
{
	g_return_if_fail (ft != NULL);
	g_return_if_fail (category != NULL);

	ft->category = category;
}


/**
 * format_template_set_filter:
 * @ft: a FormatTemplate
 * @number: Apply number settings?
 * @border: Apply borders?
 * @font: Apply font?
 * @patterns: Apply patterns?
 * @alignment: Apply alignment?
 *
 * Sets the types of elements to filter out of mstyles
 * returned by format_template_calculate_style and applied
 * when using format_template_apply_to_sheet_selection.
 * Every gboolean which is FALSE will be filtered out!
 **/
void
format_template_set_filter (FormatTemplate *ft,
			    gboolean number, gboolean border,
			    gboolean font,   gboolean patterns,
			    gboolean alignment)
{
	g_return_if_fail (ft != NULL);

	ft->number    = number;
	ft->border    = border;
	ft->font      = font;
	ft->patterns  = patterns;
	ft->alignment = alignment;

	ft->invalidate_hash = TRUE;
}

/**
 * format_template_set_size:
 * @ft:
 * @x1:
 * @y1:
 * @x2:
 * @y2:
 *
 * This will set the size of the application area.
 * BIG FAT NOTE : You will need to pass the COORDINATES of top, left and
 * bottom, right. Not the dimensions.
 * So if you want to calculate for an area of 5 x 5, you pass :
 * x1 = 0, y1 = 0, x2 = 4, y2 = 4;
 **/
void
format_template_set_size (FormatTemplate *ft, int x1, int y1, int x2, int y2)
{
	ft->x1 = x1;
	ft->y1 = y1;
	ft->x2 = x2;
	ft->y2 = y2;

	ft->invalidate_hash = TRUE;
}
