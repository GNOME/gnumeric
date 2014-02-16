/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * func.c: Function management and utility routines.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *  Michael Meeks   (mmeeks@gnu.org)
 *  Morten Welinder (terra@gnome.org)
 *  Jody Goldberg   (jody@gnome.org)
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include "gnumeric.h"
#include "func.h"

#include "parse-util.h"
#include "dependent.h"
#include "expr.h"
#include "expr-impl.h"
#include "expr-name.h"
#include "cell.h"
#include "workbook-priv.h"
#include "sheet.h"
#include "value.h"
#include "number-match.h"
#include "func-builtin.h"
#include "command-context-stderr.h"
#include "gnm-plugin.h"
#include "gutils.h"

#include <goffice/goffice.h>
#include <glib.h>
#include <string.h>
#include <stdlib.h>

#define UNICODE_ELLIPSIS "\xe2\x80\xa6"
#define F2(func,s) dgettext ((func)->textdomain->str, (s))

static GList	    *categories;
static GnmFuncGroup *unknown_cat;

static GHashTable *functions_by_name;
static GHashTable *functions_by_localized_name;

void
functions_init (void)
{
	functions_by_name =
		g_hash_table_new (go_ascii_strcase_hash, go_ascii_strcase_equal);

	/* FIXME: ascii???  */
	functions_by_localized_name =
		g_hash_table_new (go_ascii_strcase_hash, go_ascii_strcase_equal);

	func_builtin_init ();
}

void
functions_shutdown (void)
{
	while (unknown_cat != NULL && unknown_cat->functions != NULL) {
		GnmFunc *func = unknown_cat->functions->data;
		if (func->usage_count > 0) {
			g_warning ("Function %s still has %d users.\n",
				   gnm_func_get_name (func, FALSE),
				   func->usage_count);
			func->usage_count = 0;
		}
		gnm_func_free (func);
	}
	func_builtin_shutdown ();

	g_hash_table_destroy (functions_by_name);
	functions_by_name = NULL;

	g_hash_table_destroy (functions_by_localized_name);
	functions_by_localized_name = NULL;
}

inline void
gnm_func_load_if_stub (GnmFunc *func)
{
	if (func->fn_type == GNM_FUNC_TYPE_STUB)
		gnm_func_load_stub (func);
}

static void
copy_hash_table_to_ptr_array (gpointer key, gpointer value, gpointer array)
{
	GnmFunc *fd = value;

	if (fd->name == NULL ||
	    strcmp (fd->name, "perl_adder") == 0 ||
	    strcmp (fd->name, "perl_date") == 0 ||
	    strcmp (fd->name, "perl_sed") == 0 ||
	    strcmp (fd->name, "py_capwords") == 0 ||
	    strcmp (fd->name, "py_printf") == 0 ||
	    strcmp (fd->name, "py_bitand") == 0)
		return;

	gnm_func_load_if_stub (fd);
	if (fd->help != NULL)
		g_ptr_array_add (array, fd);
}

static int
func_def_cmp (gconstpointer a, gconstpointer b)
{
	GnmFunc const *fda = *(GnmFunc const **)a ;
	GnmFunc const *fdb = *(GnmFunc const **)b ;

	g_return_val_if_fail (fda->name != NULL, 0);
	g_return_val_if_fail (fdb->name != NULL, 0);

	if (fda->fn_group != NULL && fdb->fn_group != NULL) {
		int res = go_string_cmp (fda->fn_group->display_name,
					 fdb->fn_group->display_name);
		if (res != 0)
			return res;
	}

	return g_ascii_strcasecmp (fda->name, fdb->name);
}

static void
cb_dump_usage (gpointer key, GnmFunc const *fd, FILE *out)
{
	if (fd->usage_count > 0)
		fprintf (out, "%d,%s\n", fd->usage_count, fd->name);
}

static char *
split_at_colon (char const *s, char **rest)
{
	char *dup = g_strdup (s);
	char *colon = strchr (dup, ':');
	if (colon) {
		*colon = 0;
		if (rest) *rest = colon + 1;
	} else {
		if (rest) *rest = NULL;
	}
	return dup;
}

static void
dump_externals (GPtrArray *defs, FILE *out)
{
	unsigned int ui;

	fprintf (out, "<!--#set var=\"title\" value=\"Gnumeric Web Documentation\" -->");
	fprintf (out, "<!--#set var=\"rootdir\" value=\".\" -->");
	fprintf (out, "<!--#include virtual=\"header-begin.shtml\" -->");
	fprintf (out, "<link rel=\"stylesheet\" href=\"style/index.css\" type=\"text/css\"/>");
	fprintf (out, "<!--#include virtual=\"header-end.shtml\" -->");
	fprintf (out, "<!--#set var=\"wolfram\" value=\"none\" -->");
	fprintf (out, "<!--#set var=\"wiki\" value=\"none\" -->");
	fprintf (out, "<!--\n\n-->");

	for (ui = 0; ui < defs->len; ui++) {
		GnmFunc const *fd = g_ptr_array_index (defs, ui);
		gboolean any = FALSE;
		int j;

		for (j = 0; fd->help[j].type != GNM_FUNC_HELP_END; j++) {
			const char *s = F2(fd, fd->help[j].text);

			switch (fd->help[j].type) {
			case GNM_FUNC_HELP_EXTREF:
				if (!any) {
					any = TRUE;
					fprintf (out, "<!--#if expr=\"${QUERY_STRING} = %s\" -->", fd->name);
				}

				if (strncmp (s, "wolfram:", 8) == 0) {
					fprintf (out, "<!--#set var=\"wolfram\" value=\"%s\" -->", s + 8);
				}
				if (strncmp (s, "wiki:", 5) == 0) {
					char *lang, *page;
					lang = split_at_colon (s + 5, &page);
					fprintf (out, "<!--#set var=\"wiki_lang\" value=\"%s\" -->", lang);
					fprintf (out, "<!--#set var=\"wiki\" value=\"%s\" -->", page);
					g_free (lang);
				}
				break;
			default:
				break;
			}
		}

		if (any)
			fprintf (out, "<!--#endif\n\n-->");
	}

	fprintf (out, "<div class=\"floatflush\">\n");
	fprintf (out, "<h1>Online Documentation for \"<!--#echo var=\"QUERY_STRING\" -->\"</h1>\n");
	fprintf (out, "<p>When last checked, these sources provided useful information about\n");
	fprintf (out, "this function.  However, since the links are not controlled by the\n");
	fprintf (out, "Gnumeric Team, we cannot guarantee that the links still work.  If\n");
	fprintf (out, "you find that they do not work, please drop us a line.</p>\n");
	fprintf (out, "<ul>");
	fprintf (out, "<!--#if expr=\"${wolfram} != none\"-->");
	fprintf (out, "<li><a href=\"http://mathworld.wolfram.com/<!--#echo var=\"wolfram\" -->\">Wolfram Mathworld\nentry</a>.</li><!--#endif-->");
	fprintf (out, "<!--#if expr=\"${wiki} != none\"--><li><a href=\"http://<!--#echo var=\"wiki_lang\" -->.wikipedia.org/wiki/<!--#echo var=\"wiki\" -->\">Wikipedia\nentry</a>.</li><!--#endif-->");
	fprintf (out, "<li><a href=\"http://www.google.com/#q=<!--#echo var=\"QUERY_STRING\" -->\">Google Search</a>.</li>");
	fprintf (out, "</ul>");
	fprintf (out, "</div>\n");

	fprintf (out, "<!--#include virtual=\"footer.shtml\" -->\n");
}

/**
 * function_dump_defs :
 * @filename:
 * @dump_type:
 *
 * A generic utility routine to operate on all funtion defs
 * in various ways.  @dump_type will change/extend as needed
 * Right now
 * 0 : www.gnumeric.org's function.shtml page
 * 1 :
 * 2 : generate_po
 * 3 : dump function usage count
 * 4 : external refs
 **/
void
function_dump_defs (char const *filename, int dump_type)
{
	FILE *output_file;
	char *up, *catname;
	unsigned i;
	GPtrArray *ordered;
	GnmFuncGroup const *group = NULL;

	if (dump_type == 2) {
		g_printerr ("generate po is obsolete.\n");
		return;
	}
	g_return_if_fail (filename != NULL);

	if ((output_file = g_fopen (filename, "w")) == NULL){
		g_printerr (_("Cannot create file %s\n"), filename);
		exit (1);
	}

	if (dump_type == 3) {
		g_hash_table_foreach (functions_by_name,
				      (GHFunc) cb_dump_usage,
				      output_file);
		fclose (output_file);
		return;
	}

	/* TODO : Use the translated names and split by fn_group. */
	ordered = g_ptr_array_new ();
	g_hash_table_foreach (functions_by_name,
			      copy_hash_table_to_ptr_array, ordered);

	if (ordered->len > 0)
		qsort (&g_ptr_array_index (ordered, 0),
		       ordered->len, sizeof (gpointer),
		       func_def_cmp);

	if (dump_type == 4) {
		dump_externals (ordered, output_file);
		g_ptr_array_free (ordered, TRUE);
		fclose (output_file);
		return;
	}

	if (dump_type == 0) {
		int unique = 0;
		for (i = 0; i < ordered->len; i++) {
			GnmFunc const *fd = g_ptr_array_index (ordered, i);
			switch (fd->impl_status) {
			case GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC:
				unique++;
				break;
			default: ;
			}
		}

		fprintf (output_file,
			 "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
			 "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n"
			 "<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"en\" lang=\"en\">\n"
			 "<!-- DEFINE current=Home -->\n"
			 "<!-- MARKER: start-header -->\n"
			 "<head>\n"
			 "<title>Gnumeric</title>\n"
			 "<link rel=\"stylesheet\" href=\"style/style.css\" type=\"text/css\" />\n"
			 "<link rel=\"icon\" type=\"image/png\" href=\"logo.png\" />\n"
			 "<style type=\"text/css\"><!--\n"
			 "  div.functiongroup {\n"
			 "    margin-top: 1em;\n"
			 "    margin-bottom: 1em;\n"
			 "  }\n"
			 "  table.functiongroup {\n"
			 "    border-style: solid;\n"
			 "    border-width: 1px;\n"
			 "    border-spacing: 0px;\n"
			 "  }\n"
			 "  tr.header td {\n"
			 "    font-weight: bold;\n"
			 "    font-size: 14pt;\n"
			 "    border-style: solid;\n"
			 "    border-width: 1px;\n"
			 "    text-align: center;\n"
			 "  }\n"
			 "  tr.function td {\n"
			 "    border: solid 1px;\n"
			 "  }\n"
			 "  td.testing-unknown    { background: #ffffff; }\n"
			 "  td.testing-nosuite    { background: #ff7662; }\n"
			 "  td.testing-basic      { background: #fff79d; }\n"
			 "  td.testing-exhaustive { background: #aef8b5; }\n"
			 "  td.testing-devel      { background: #ff6c00; }\n"
			 "  td.imp-exists         { background: #ffffff; }\n"
			 "  td.imp-no             { background: #ff7662; }\n"
			 "  td.imp-subset         { background: #fff79d; }\n"
			 "  td.imp-complete       { background: #aef8b5; }\n"
			 "  td.imp-superset       { background: #16e49e; }\n"
			 "  td.imp-subsetext      { background: #59fff2; }\n"
			 "  td.imp-devel          { background: #ff6c00; }\n"
			 "  td.imp-gnumeric       { background: #44be18; }\n"
			 "--></style>\n"
			 "</head>\n"
			 "<body>\n"
			 "<div id=\"wrap\">\n"
			 "  <a href=\"/\"><div id=\"header\">\n"
			 "    <h1 id=\"logo-text\"><span>Gnumeric</span></h1>\n"
			 "    <p id=\"slogan\">Free, Fast, Accurate &mdash; Pick Any Three!</p>\n"
			 "    <img id=\"logo\" src=\"gnumeric.png\" alt=\"logo\" class=\"float-right\"/>\n"
			 "    </div></a>\n"
			 "\n"
			 "  <div id=\"nav\">\n"
			 "    <ul>\n"
			 "      <li id=\"current\"><a href=\"/\">Home</a></li>\n"
			 "      <li><a href=\"development.html\">Development</a></li>\n"
			 "      <li><a href=\"contact.html\">Contact</a></li>\n"
			 "    </ul>\n"
			 "  </div>\n"
			 "\n"
			 "  <div id=\"content-wrap\">\n"
			 "    <!-- MARKER: start-main -->\n"
			 "    <div id=\"main\">\n"
			 "      <div class=\"generalitem\">\n"
			 "	<h2><span class=\"gnumeric-bullet\"></span>Gnumeric Sheet Functions</h2>\n"
			 "	<p>Gnumeric currently has %d functions for use in spreadsheets.\n"
			 "      %d of these are unique to Gnumeric.</p>\n",
			 ordered->len, unique);
	}

	for (i = 0; i < ordered->len; i++) {
		GnmFunc const *fd = g_ptr_array_index (ordered, i);
		if (dump_type == 1) {
			int i;
			gboolean first_arg = TRUE;
			GString *syntax = g_string_new (NULL);
			GString *arg_desc = g_string_new (NULL);
			GString *desc = g_string_new (NULL);
			GString *odf = g_string_new (NULL);
			GString *excel = g_string_new (NULL);
			GString *note = g_string_new (NULL);
			GString *seealso = g_string_new (NULL);
			gint min, max;

			fprintf (output_file, "@CATEGORY=%s\n",
				 F2(fd, fd->fn_group->display_name->str));
			for (i = 0;
			     fd->help[i].type != GNM_FUNC_HELP_END;
			     i++) {
				switch (fd->help[i].type) {
				case GNM_FUNC_HELP_NAME: {
					char *short_desc;
					char *name = split_at_colon (F2(fd, fd->help[i].text), &short_desc);
					fprintf (output_file,
						 "@FUNCTION=%s\n",
						 name);
					fprintf (output_file,
						 "@SHORTDESC=%s\n",
						 short_desc);
					g_string_append (syntax, name);
					g_string_append_c (syntax, '(');
					g_free (name);
					break;
				}
				case GNM_FUNC_HELP_SEEALSO:
					if (seealso->len > 0)
						g_string_append (seealso, ",");
					g_string_append (seealso, F2(fd, fd->help[i].text));
					break;
				case GNM_FUNC_HELP_DESCRIPTION:
					if (desc->len > 0)
						g_string_append (desc, "\n");
					g_string_append (desc, F2(fd, fd->help[i].text));
					break;
				case GNM_FUNC_HELP_NOTE:
					if (note->len > 0)
						g_string_append (note, " ");
					g_string_append (note, F2(fd, fd->help[i].text));
					break;
				case GNM_FUNC_HELP_ARG: {
					char *argdesc;
					char *name = split_at_colon (F2(fd, fd->help[i].text), &argdesc);
					if (first_arg)
						first_arg = FALSE;
					else
						g_string_append_c (syntax, go_locale_get_arg_sep ());
					g_string_append (syntax, name);
					if (argdesc) {
						g_string_append_printf (arg_desc,
									"@{%s}: %s\n",
									name,
									argdesc);
					}
					g_free (name);
					/* FIXME: Optional args?  */
					break;
				}
				case GNM_FUNC_HELP_ODF:
					if (odf->len > 0)
						g_string_append (odf, " ");
					g_string_append (odf, F2(fd, fd->help[i].text));
					break;
				case GNM_FUNC_HELP_EXCEL:
					if (excel->len > 0)
						g_string_append (excel, " ");
					g_string_append (excel, F2(fd, fd->help[i].text));
					break;

				case GNM_FUNC_HELP_EXTREF:
					/* FIXME! */
				case GNM_FUNC_HELP_EXAMPLES:
					/* FIXME! */
				case GNM_FUNC_HELP_END:
					break;
				}
			}

			function_def_count_args (fd, &min, &max);
			if (max == G_MAXINT)
				fprintf (output_file,
					 "@SYNTAX=%s," UNICODE_ELLIPSIS ")\n",
					 syntax->str);
			else
				fprintf (output_file, "@SYNTAX=%s)\n",
					 syntax->str);

			if (arg_desc->len > 0)
				fprintf (output_file, "@ARGUMENTDESCRIPTION=%s", arg_desc->str);
			if (desc->len > 0)
				fprintf (output_file, "@DESCRIPTION=%s\n", desc->str);
			if (note->len > 0)
				fprintf (output_file, "@NOTE=%s\n", note->str);
			if (excel->len > 0)
				fprintf (output_file, "@EXCEL=%s\n", excel->str);
			if (odf->len > 0)
				fprintf (output_file, "@ODF=%s\n", odf->str);
			if (seealso->len > 0)
				fprintf (output_file, "@SEEALSO=%s\n", seealso->str);

			g_string_free (syntax, TRUE);
			g_string_free (arg_desc, TRUE);
			g_string_free (desc, TRUE);
			g_string_free (odf, TRUE);
			g_string_free (excel, TRUE);
			g_string_free (note, TRUE);
			g_string_free (seealso, TRUE);

			fputc ('\n', output_file);
		} else if (dump_type == 0) {
			static struct {
				char const *name;
				char const *klass;
			} const testing [] = {
				{ "Unknown",		"testing-unknown" },
				{ "No Testsuite",	"testing-nosuite" },
				{ "Basic",		"testing-basic" },
				{ "Exhaustive",		"testing-exhaustive" },
				{ "Under Development",	"testing-devel" }
			};
			static struct {
				char const *name;
				char const *klass;
			} const implementation [] = {
				{ "Exists",			"imp-exists" },
				{ "Unimplemented",		"imp-no" },
				{ "Subset",			"imp-subset" },
				{ "Complete",			"imp-complete" },
				{ "Superset",			"imp-superset" },
				{ "Subset with_extensions",	"imp-subsetext" },
				{ "Under development",		"imp-devel" },
				{ "Unique to Gnumeric",		"imp-gnumeric" },
			};
			if (group != fd->fn_group) {
				if (group) fprintf (output_file, "</table></div>\n");
				group = fd->fn_group;
				fprintf (output_file,
					 "<h2>%s</h2>\n"
					 "<div class=\"functiongroup\"><table class=\"functiongroup\">\n"
					 "<tr class=\"header\">"
					 "<td>Function</td>"
					 "<td>Implementation</td>"
					 "<td>Testing</td>"
					 "</tr>\n",
					 group->display_name->str);
			}
			up = g_ascii_strup (fd->name, -1);
			catname = g_strdup (group->display_name->str);
			while (strchr (catname, ' '))
				*strchr (catname, ' ') = '_';
			fprintf (output_file, "<tr class=\"function\">\n");
			fprintf (output_file,
				 "<td><a href =\"https://help.gnome.org/users/gnumeric/stable/CATEGORY_%s.html.en#gnumeric-function-%s\">%s</a></td>\n",
				 catname, up, fd->name);
			g_free (up);
			g_free (catname);
			fprintf (output_file,
				 "<td class=\"%s\"><a href=\"mailto:gnumeric-list@gnome.org?subject=Re: %s implementation\">%s</a></td>\n",
				 implementation[fd->impl_status].klass,
				 fd->name,
				 implementation[fd->impl_status].name);
			fprintf (output_file,
				 "<td class=\"%s\"><a href=\"mailto:gnumeric-list@gnome.org?subject=Re: %s testing\">%s</a></td>\n",
				 testing[fd->test_status].klass,
				 fd->name,
				 testing[fd->test_status].name);
			fprintf (output_file,"</tr>\n");
		}
	}
	if (dump_type == 0) {
		if (group) fprintf (output_file, "</table></div>\n");
		fprintf (output_file,
			 "      </div>\n"
			 "    </div>\n"
			 "    <!-- MARKER: end-main -->\n"
			 "    <!-- MARKER: start-sidebar -->\n"
			 "    <!-- MARKER: end-sidebar -->\n"
			 "  </div>\n"
			 "</div>\n"
			 "</body>\n"
			 "</html>\n");
	}

	g_ptr_array_free (ordered, TRUE);
	fclose (output_file);
}

/* ------------------------------------------------------------------------- */

static gboolean
check_help_expression (const char *text, GnmFunc const *fd)
{
	GnmConventions const *convs = gnm_conventions_default;
	GnmParsePos pp;
	GnmExprTop const *texpr;
	Workbook *wb;
	GnmParseError perr;

	/* Create a dummy workbook with no sheets for interesting effects.  */
	wb = workbook_new ();
	parse_pos_init (&pp, wb, NULL, 0, 0);

	parse_error_init (&perr);

	texpr = gnm_expr_parse_str (text, &pp,
				    GNM_EXPR_PARSE_DEFAULT,
				    convs,
				    &perr);
	if (perr.err) {
		g_printerr ("Error parsing %s: %s\n",
			    text, perr.err->message);
	}
	parse_error_free (&perr);
	g_object_unref (wb);

	if (!texpr)
		return TRUE;

	gnm_expr_top_unref (texpr);
	return FALSE;
}

static gboolean
check_argument_refs (const char *text, GnmFunc const *fd)
{
	if (fd->fn_type != GNM_FUNC_TYPE_ARGS)
		return FALSE;

	while (1) {
		const char *at = strchr (text, '@');
		char *argname;
		int i;

		if (!at)
			return FALSE;
		if (at[1] != '{')
			return TRUE;
		text = strchr (at + 2, '}');
		if (!text)
			return FALSE;
		argname = g_strndup (at + 2, text - at - 2);

		for (i = 0; TRUE; i++) {
			char *thisarg = function_def_get_arg_name (fd, i);
			gboolean found;
			if (!thisarg) {
				g_free (argname);
				return TRUE;
			}
			found = strcmp (argname, thisarg) == 0;
			g_free (thisarg);
			if (found)
				break;
		}
		g_free (argname);
	}
}


static int
gnm_func_sanity_check1 (GnmFunc const *fd)
{
	GnmFuncHelp const *h;
	int counts[(int)GNM_FUNC_HELP_ODF + 1];
	int res = 0;
	size_t nlen = strlen (fd->name);
	GHashTable *allargs;

	allargs = g_hash_table_new_full
		(g_str_hash, g_str_equal, (GDestroyNotify)g_free, NULL);

	memset (counts, 0, sizeof (counts));
	for (h = fd->help; h->type != GNM_FUNC_HELP_END; h++) {
		g_assert (h->type <= GNM_FUNC_HELP_ODF);
		counts[h->type]++;

		if (!g_utf8_validate (h->text, -1, NULL)) {
			g_printerr ("%s: Invalid UTF-8 in type %i\n",
				    fd->name, h->type);
				res = 1;
				continue;
		}

		switch (h->type) {
		case GNM_FUNC_HELP_NAME:
			if (g_ascii_strncasecmp (fd->name, h->text, nlen) ||
			    h->text[nlen] != ':') {
				g_printerr ("%s: Invalid NAME record\n",
					    fd->name);
				res = 1;
			} else if (h->text[nlen + 1] == ' ') {
				g_printerr ("%s: Unwanted space in NAME record\n",
					    fd->name);
				res = 1;
			} else if (h->text[strlen (h->text) - 1] == '.') {
				g_printerr ("%s: Unwanted period in NAME record\n",
					    fd->name);
				res = 1;
			}
			break;
		case GNM_FUNC_HELP_ARG: {
			const char *aend = strchr (h->text, ':');
			char *argname;

			if (aend == NULL || aend == h->text) {
				g_printerr ("%s: Invalid ARG record\n",
					    fd->name);
				res = 1;
				break;
			}

			if (aend[1] == ' ') {
				g_printerr ("%s: Unwanted space in ARG record\n",
					    fd->name);
				res = 1;
			}
			if (aend[1] == '\0') {
				g_printerr ("%s: Empty ARG record\n",
					    fd->name);
				res = 1;
			}
			if (h->text[strlen (h->text) - 1] == '.') {
				g_printerr ("%s: Unwanted period in ARG record\n",
					    fd->name);
				res = 1;
			}
			if (check_argument_refs (aend + 1, fd)) {
				g_printerr ("%s: Invalid argument reference in argument\n",
					    fd->name);
				res = 1;
			}
			argname = g_strndup (h->text, aend - h->text);
			if (g_hash_table_lookup (allargs, argname)) {
				g_printerr ("%s: Duplicate argument name %s\n",
					    fd->name, argname);
				res = 1;
				g_free (argname);
				g_printerr ("%s\n", h->text);
			} else
				g_hash_table_insert (allargs, argname, argname);
			break;
		}
		case GNM_FUNC_HELP_DESCRIPTION: {
			const char *p;

			if (check_argument_refs (h->text, fd)) {
				g_printerr ("%s: Invalid argument reference in description\n",
					    fd->name);
				res = 1;
			}

			p = h->text;
			while (g_ascii_isupper (*p) ||
			       (p != h->text && (*p == '_' ||
						 *p == '.' ||
						 g_ascii_isdigit (*p))))
				p++;
			if (*p == ' ' &&
			    p - h->text >= 2 &&
			    strncmp (h->text, "CP1252", 6) != 0) {
				if (g_ascii_strncasecmp (h->text, fd->name, nlen)) {
					g_printerr ("%s: Wrong function name in description\n",
						    fd->name);
					res = 1;
				}
			}
			break;
		}

		case GNM_FUNC_HELP_EXAMPLES:
			if (h->text[0] == '=') {
				if (check_help_expression (h->text + 1, fd)) {
					g_printerr ("%s: Invalid EXAMPLES record\n",
						    fd->name);
					res = 1;
				}
			}
			break;
		default:
			; /* Nothing */
		}
	}

	g_hash_table_destroy (allargs);

	if (fd->fn_type == GNM_FUNC_TYPE_ARGS) {
		int n = counts[GNM_FUNC_HELP_ARG];
		if (n != fd->fn.args.max_args) {
			g_printerr ("%s: Help for %d args, but takes %d-%d\n",
				    fd->name, n,
				    fd->fn.args.min_args, fd->fn.args.max_args);
			res = 1;
		}
	}

#if 0
	if (counts[GNM_FUNC_HELP_DESCRIPTION] != 1) {
		g_printerr ("%s: Help has %d descriptions.\n",
			    fd->name, counts[GNM_FUNC_HELP_DESCRIPTION]);
		res = 1;
	}
#endif

	if (counts[GNM_FUNC_HELP_NAME] != 1) {
		g_printerr ("%s: Help has %d NAME records.\n",
			    fd->name, counts[GNM_FUNC_HELP_NAME]);
		res = 1;
	}

	if (counts[GNM_FUNC_HELP_EXCEL] > 1) {
		g_printerr ("%s: Help has %d Excel notes.\n",
			    fd->name, counts[GNM_FUNC_HELP_EXCEL]);
		res = 1;
	}

	if (counts[GNM_FUNC_HELP_ODF] > 1) {
		g_printerr ("%s: Help has %d ODF notes.\n",
			    fd->name, counts[GNM_FUNC_HELP_ODF]);
		res = 1;
	}

	return res;
}

int
gnm_func_sanity_check (void)
{
	int res = 0;
	GPtrArray *ordered;
	unsigned ui;

	ordered = g_ptr_array_new ();
	g_hash_table_foreach (functions_by_name,
			      copy_hash_table_to_ptr_array, ordered);
	if (ordered->len > 0)
		qsort (&g_ptr_array_index (ordered, 0),
		       ordered->len, sizeof (gpointer),
		       func_def_cmp);

	for (ui = 0; ui < ordered->len; ui++) {
		GnmFunc const *fd = g_ptr_array_index (ordered, ui);
		if (gnm_func_sanity_check1 (fd))
			res = 1;
	}

	g_ptr_array_free (ordered, TRUE);

	return res;
}

/* ------------------------------------------------------------------------- */

static void
gnm_func_group_free (GnmFuncGroup *fn_group)
{
	g_return_if_fail (fn_group != NULL);
	g_return_if_fail (fn_group->functions == NULL);

	if (fn_group->ref_count-- > 1)
		return;

	go_string_unref (fn_group->internal_name);
	go_string_unref (fn_group->display_name);
	g_free (fn_group);
}

static GnmFuncGroup *
gnm_func_group_ref (GnmFuncGroup *fn_group)
{
	fn_group->ref_count++;
	return fn_group;
}

GType
gnm_func_group_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmFuncGroup",
			 (GBoxedCopyFunc)gnm_func_group_ref,
			 (GBoxedFreeFunc)gnm_func_group_free);
	}
	return t;
}

static gint
function_category_compare (gconstpointer a, gconstpointer b)
{
	GnmFuncGroup const *cat_a = a;
	GnmFuncGroup const *cat_b = b;

	return go_string_cmp (cat_a->display_name, cat_b->display_name);
}

GnmFuncGroup *
gnm_func_group_fetch (char const *name, char const *translation)
{
	GnmFuncGroup *cat = NULL;
	GList *l;

	g_return_val_if_fail (name != NULL, NULL);

	for (l = categories; l != NULL; l = l->next) {
		cat = l->data;
		if (strcmp (cat->internal_name->str, name) == 0) {
			break;
		}
	}

	if (l == NULL) {
		cat = g_new (GnmFuncGroup, 1);
		cat->internal_name = go_string_new (name);
		cat->ref_count = 1;
		if (translation != NULL) {
			cat->display_name = go_string_new (translation);
			cat->has_translation = TRUE;
		} else {
			cat->display_name = go_string_new (name);
			cat->has_translation = FALSE;
		}
		cat->functions = NULL;
		categories = g_list_insert_sorted (
			     categories, cat, &function_category_compare);
	} else if (translation != NULL && translation != name &&
		   !cat->has_translation) {
		go_string_unref (cat->display_name);
		cat->display_name = go_string_new (translation);
		cat->has_translation = TRUE;
		categories = g_list_remove_link (categories, l);
		g_list_free_1 (l);
		categories = g_list_insert_sorted (
			     categories, cat, &function_category_compare);
	}

	return cat;
}

GnmFuncGroup *
gnm_func_group_get_nth (int n)
{
	return g_list_nth_data (categories, n);
}

static void
gnm_func_group_add_func (GnmFuncGroup *fn_group, GnmFunc *fn_def)
{
	g_return_if_fail (fn_group != NULL);
	g_return_if_fail (fn_def != NULL);

	fn_group->functions = g_slist_prepend (fn_group->functions, fn_def);
}

static void
gnm_func_group_remove_func (GnmFuncGroup *fn_group, GnmFunc *fn_def)
{
	g_return_if_fail (fn_group != NULL);
	g_return_if_fail (fn_def != NULL);

	fn_group->functions = g_slist_remove (fn_group->functions, fn_def);
	if (fn_group->functions == NULL) {
		categories = g_list_remove (categories, fn_group);
		if (unknown_cat == fn_group)
			unknown_cat = NULL;
		gnm_func_group_free (fn_group);
	}
}

/******************************************************************************/

static void
extract_arg_types (GnmFunc *def)
{
	int i;

	function_def_count_args (def,
				 &def->fn.args.min_args,
				 &def->fn.args.max_args);
	def->fn.args.arg_types = g_malloc (def->fn.args.max_args + 1);
	for (i = 0; i < def->fn.args.max_args; i++)
		def->fn.args.arg_types[i] = function_def_get_arg_type (def, i);
	def->fn.args.arg_types[i] = 0;
}

static GnmValue *
error_function_no_full_info (GnmFuncEvalInfo *ei,
			     int argc,
			     GnmExprConstPtr const *argv)
{
	return value_new_error (ei->pos, _("Function implementation not available."));
}

static void
gnm_func_clear_arg_names (GnmFunc *fd)
{
	if (fd->arg_names_p) {
		g_ptr_array_foreach (fd->arg_names_p, (GFunc) g_free, NULL);
		g_ptr_array_free (fd->arg_names_p, TRUE);
		fd->arg_names_p = NULL;
	}
}

static void
gnm_func_create_arg_names (GnmFunc *fn_def)
{
	int i;
	GPtrArray *ptr;

	g_return_if_fail (fn_def != NULL);

	ptr = g_ptr_array_new ();
	for (i = 0;
	     fn_def->help && fn_def->help[i].type != GNM_FUNC_HELP_END;
	     i++) {
		if (fn_def->help[i].type != GNM_FUNC_HELP_ARG)
			continue;

		g_ptr_array_add
			(ptr, split_at_colon
			 (F2(fn_def, fn_def->help[i].text), NULL));
	}

	gnm_func_clear_arg_names (fn_def);
	fn_def->arg_names_p = ptr;
}


void
gnm_func_load_stub (GnmFunc *func)
{
	GnmFuncDescriptor desc;

	g_return_if_fail (func->fn_type == GNM_FUNC_TYPE_STUB);

	/* default the content to 0 in case we add new fields
	 * later and the services do not fill them in
	 */
	memset (&desc, 0, sizeof (GnmFuncDescriptor));

	if (func->fn.load_desc (func, &desc)) {
		func->help	 = desc.help ? desc.help : NULL;
		if (desc.fn_args != NULL) {
			func->fn_type		= GNM_FUNC_TYPE_ARGS;
			func->fn.args.func	= desc.fn_args;
			func->fn.args.arg_spec	= desc.arg_spec;
			extract_arg_types (func);
		} else if (desc.fn_nodes != NULL) {
			func->fn_type		= GNM_FUNC_TYPE_NODES;
			func->fn.nodes		= desc.fn_nodes;
		} else {
			g_warning ("Invalid function descriptor with no function");
		}
		func->linker	  = desc.linker;
		func->impl_status = desc.impl_status;
		func->test_status = desc.test_status;
		func->flags	  = desc.flags;
		gnm_func_create_arg_names (func);
	} else {
		func->fn_type = GNM_FUNC_TYPE_NODES;
		func->fn.nodes = &error_function_no_full_info;
		func->linker   = NULL;
	}
}

static void
gnm_func_set_localized_name (GnmFunc *fd, const char *lname)
{
	gboolean in_hashes = !(fd->flags & GNM_FUNC_IS_WORKBOOK_LOCAL);

	if (in_hashes && fd->localized_name)
		g_hash_table_remove (functions_by_localized_name, fd->localized_name);
	g_free (fd->localized_name);

	fd->localized_name = g_strdup (lname);
	if (in_hashes && lname)
		g_hash_table_insert (functions_by_localized_name,
				     fd->localized_name, fd);
}

void
gnm_func_free (GnmFunc *func)
{
	GnmFuncGroup *group;

	g_return_if_fail (func != NULL);
	g_return_if_fail (func->usage_count == 0);

	group = func->fn_group;
	if (group != NULL)
		gnm_func_group_remove_func (group, func);

	gnm_func_set_localized_name (func, NULL);

	if (!(func->flags & GNM_FUNC_IS_WORKBOOK_LOCAL)) {
		g_hash_table_remove (functions_by_name, func->name);
	}

	if (func->fn_type == GNM_FUNC_TYPE_ARGS)
		g_free (func->fn.args.arg_types);

	g_free ((char *)func->name);

	if (func->textdomain)
		go_string_unref (func->textdomain);

	gnm_func_clear_arg_names (func);

	g_free (func);
}

GnmFunc *
gnm_func_ref (GnmFunc *func)
{
	g_return_val_if_fail (func != NULL, NULL);

	func->usage_count++;
	if (func->usage_count == 1 && func->usage_notify != NULL)
		func->usage_notify (func, 1);
	return func;
}

void
gnm_func_unref (GnmFunc *func)
{
	g_return_if_fail (func != NULL);
	g_return_if_fail (func->usage_count > 0);

	func->usage_count--;
	if (func->usage_count == 0 && func->usage_notify != NULL)
		func->usage_notify (func, 0);
}

GType
gnm_func_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmFunc",
			 (GBoxedCopyFunc)gnm_func_ref,
			 (GBoxedFreeFunc)gnm_func_unref);
	}
	return t;
}

GnmFunc *
gnm_func_lookup (char const *name, Workbook *scope)
{
	GnmFunc *fd = g_hash_table_lookup (functions_by_name, name);
	if (fd != NULL)
		return fd;
	if (scope == NULL || scope->sheet_local_functions == NULL)
		return NULL;
	return g_hash_table_lookup (scope->sheet_local_functions, (gpointer)name);
}

GnmFunc *
gnm_func_lookup_localized (char const *name, Workbook *scope)
{
	GnmFunc *fd;
	GHashTableIter hiter;
	gpointer value;

	/* Must localize all function names.  */
	g_hash_table_iter_init (&hiter, functions_by_name);
	while (g_hash_table_iter_next (&hiter, NULL, &value)) {
		GnmFunc *fd = value;
		(void)gnm_func_get_name (fd, TRUE);
	}

	fd = g_hash_table_lookup (functions_by_localized_name, name);
	if (fd != NULL)
		return fd;
	if (scope == NULL || scope->sheet_local_functions == NULL)
		return NULL;
	return g_hash_table_lookup (scope->sheet_local_functions, (gpointer)name);
}

/**
 * gnm_func_lookup_prefix:
 * @prefix: prefix to search for
 * @scope:
 * @trans: whether to search translated function names
 *
 * Returns: (element-type GnmFunc*) (transfer full):
 **/
GSList *
gnm_func_lookup_prefix (char const *prefix, Workbook *scope, gboolean trans)
{
	GSList *res = NULL;
	GHashTableIter hiter;
	gpointer value;

	/*
	 * Always iterate over functions_by_name as the localized name
	 * might not be set yet.
	 */
	g_hash_table_iter_init (&hiter, functions_by_name);
	while (g_hash_table_iter_next (&hiter, NULL, &value)) {
		GnmFunc *fd = value;
		if (!(fd->flags & GNM_FUNC_IS_PLACEHOLDER)) {
			const char *name = gnm_func_get_name (fd, trans);
			if (g_str_has_prefix (name, prefix)) {
				gnm_func_ref (fd);
				res = g_slist_prepend (res, fd);
			}
		}
	}

	return res;
}

GnmFunc *
gnm_func_add (GnmFuncGroup *fn_group,
	      GnmFuncDescriptor const *desc,
	      const char *textdomain)
{
	static char const valid_tokens[] = "fsbraAES?|";
	GnmFunc *func;
	char const *ptr;

	g_return_val_if_fail (fn_group != NULL, NULL);
	g_return_val_if_fail (desc != NULL, NULL);

	func = g_new (GnmFunc, 1);

	if (!textdomain)
		textdomain = GETTEXT_PACKAGE;

	func->name		= g_strdup (desc->name);
	func->help		= desc->help ? desc->help : NULL;
	func->textdomain        = go_string_new (textdomain);
	func->linker		= desc->linker;
	func->usage_notify	= desc->usage_notify;
	func->flags		= desc->flags;
	func->impl_status	= desc->impl_status;
	func->test_status	= desc->test_status;
	func->localized_name    = NULL;
	func->arg_names_p       = NULL;

	func->user_data		= NULL;
	func->usage_count	= 0;

	if (desc->fn_args != NULL) {
		/* Check those arguments */
		for (ptr = desc->arg_spec ; *ptr ; ptr++) {
			g_return_val_if_fail (strchr (valid_tokens, *ptr), NULL);
		}

		func->fn_type		= GNM_FUNC_TYPE_ARGS;
		func->fn.args.func	= desc->fn_args;
		func->fn.args.arg_spec	= desc->arg_spec;
		extract_arg_types (func);
	} else if (desc->fn_nodes != NULL) {

		if (desc->arg_spec && *desc->arg_spec) {
			g_warning ("Arg spec for node function -- why?");
		}

		func->fn_type  = GNM_FUNC_TYPE_NODES;
		func->fn.nodes = desc->fn_nodes;
	} else {
		g_warning ("Invalid function has neither args nor nodes handler");
		g_free (func);
		return NULL;
	}

	func->fn_group = fn_group;
	if (fn_group != NULL)
		gnm_func_group_add_func (fn_group, func);
	if (!(func->flags & GNM_FUNC_IS_WORKBOOK_LOCAL))
		g_hash_table_insert (functions_by_name,
				     (gpointer)(func->name), func);

	gnm_func_create_arg_names (func);

	return func;
}

/* Handle unknown functions on import without losing their names */
static GnmValue *
unknownFunctionHandler (GnmFuncEvalInfo *ei,
			G_GNUC_UNUSED int argc,
			G_GNUC_UNUSED GnmExprConstPtr const *argv)
{
	return value_new_error_NAME (ei->pos);
}

/**
 * gnm_func_upgrade_placeholder:
 * @fd:
 * @fn_group:
 * @textdomain:
 * @load_desc: (scope async):
 * @opt_usage_notify: (scope async):
 **/
void
gnm_func_upgrade_placeholder (GnmFunc *fd,
			      GnmFuncGroup *fn_group,
			      const char *textdomain,
			      GnmFuncLoadDesc load_desc,
			      GnmFuncUsageNotify opt_usage_notify)
{
	g_return_if_fail (fd != NULL);
	g_return_if_fail (fd->flags & GNM_FUNC_IS_PLACEHOLDER);
	g_return_if_fail (fn_group != NULL);

	if (!textdomain)
		textdomain = GETTEXT_PACKAGE;

	/* Remove from unknown_cat */
	gnm_func_group_remove_func (fd->fn_group, fd);

	fd->fn_type = GNM_FUNC_TYPE_STUB;
	fd->fn.load_desc = load_desc;
	fd->usage_notify = opt_usage_notify;

	go_string_unref (fd->textdomain);
	fd->textdomain = go_string_new (textdomain);

	/* Clear localized_name so we can deduce the proper name.  */
	gnm_func_set_localized_name (fd, NULL);

	fd->flags &= ~GNM_FUNC_IS_PLACEHOLDER;

	fd->fn_group = fn_group;
	gnm_func_group_add_func (fn_group, fd);
}

static char *
invent_name (const char *pref, GHashTable *h, const char *template)
{
	static int count = 0;
	char *name = g_utf8_strdown (pref, -1);

	while (g_hash_table_lookup (h, name)) {
		count++;
		g_free (name);
		name = g_strdup_printf (template, count);
	}

	return name;
}

static GnmFunc *
gnm_func_add_placeholder_full (Workbook *scope,
			       char const *gname, char const *lname,
			       char const *type)
{
	GnmFuncDescriptor desc;
	GnmFunc *func;
	char const *unknown_cat_name = N_("Unknown Function");
	gboolean copy_gname = TRUE;
	gboolean copy_lname = TRUE;

	g_return_val_if_fail (gname || lname, NULL);
	g_return_val_if_fail (gname == NULL || gnm_func_lookup (gname, scope) == NULL, NULL);
	g_return_val_if_fail (lname == NULL || gnm_func_lookup_localized (lname, scope) == NULL, NULL);

	if (!unknown_cat)
		unknown_cat = gnm_func_group_fetch
			(unknown_cat_name, _(unknown_cat_name));

	if (!gname) {
		/*
		 * This is actually a bit of a problem if we don't end up
		 * with a copy of lname (because there already is a function
		 * with that name).  We're likely to save a template name,
		 * but I don't see what else to do.
		 */
		gname = invent_name (lname, functions_by_name, "unknown%d");
		copy_gname = FALSE;
	}
	if (!lname) {
		/* xgettext: This represents a made-up translated function name.  */
		lname = invent_name (gname, functions_by_localized_name, _("unknown%d"));
		copy_lname = FALSE;
	}

	if (gnm_debug_flag ("func"))
		g_printerr ("Adding placeholder for %s (aka %s)\n", gname, lname);

	memset (&desc, 0, sizeof (GnmFuncDescriptor));
	desc.name	  = gname;
	desc.arg_spec	  = NULL;
	desc.help	  = NULL;
	desc.fn_args	  = NULL;
	desc.fn_nodes	  = &unknownFunctionHandler;
	desc.linker	  = NULL;
	desc.usage_notify = NULL;
	desc.flags	  = GNM_FUNC_IS_PLACEHOLDER;
	desc.impl_status  = GNM_FUNC_IMPL_STATUS_EXISTS;
	desc.test_status  = GNM_FUNC_TEST_STATUS_UNKNOWN;

	if (scope != NULL)
		desc.flags |= GNM_FUNC_IS_WORKBOOK_LOCAL;
	else {
#if 0
		/* WISHLIST : it would be nice to have a log if these. */
		g_warning ("Unknown %s function : %s", type, desc.name);
#endif
	}

	func = gnm_func_add (unknown_cat, &desc, NULL);

	if (lname) {
		gnm_func_set_localized_name (func, lname);
		if (!copy_lname)
			g_free ((char *)lname);
	}

	if (!copy_gname)
		g_free ((char *)gname);

	if (scope != NULL) {
		if (scope->sheet_local_functions == NULL)
			scope->sheet_local_functions = g_hash_table_new_full (
				g_str_hash, g_str_equal,
				NULL, (GDestroyNotify) gnm_func_free);
		g_hash_table_insert (scope->sheet_local_functions,
			(gpointer)func->name, func);
	}

	return func;
}

/*
 * When importing it is useful to keep track of unknown function names.
 * We may be missing a plugin or something similar.
 *
 * TODO : Eventully we should be able to keep track of these
 *        and replace them with something else.  Possibly even reordering the
 *        arguments.
 */
GnmFunc *
gnm_func_add_placeholder (Workbook *scope,
			  char const *name, char const *type)
{
	return gnm_func_add_placeholder_full (scope, name, NULL, type);
}

GnmFunc *
gnm_func_add_placeholder_localized (char const *gname, char const *lname)
{
	return gnm_func_add_placeholder_full (NULL, gname, lname, "?");
}

/* Utility routine to be used for import and analysis tools */
GnmFunc	*
gnm_func_lookup_or_add_placeholder (char const *name)
{
	GnmFunc	* f = gnm_func_lookup (name, NULL);
	if (f == NULL)
		f = gnm_func_add_placeholder (NULL, name, "");
	return f;
}

/**
 * gnm_func_get_user_data:
 * @func:
 *
 * Returns: (transfer none):
 **/
gpointer
gnm_func_get_user_data (GnmFunc const *func)
{
	g_return_val_if_fail (func != NULL, NULL);

	return func->user_data;
}

void
gnm_func_set_user_data (GnmFunc *func, gpointer user_data)
{
	g_return_if_fail (func != NULL);

	func->user_data = user_data;
}

char const *
gnm_func_get_name (GnmFunc const *func, gboolean localized_function_names)
{
	int i;
	GnmFunc *fd = (GnmFunc *)func;

	g_return_val_if_fail (func != NULL, NULL);

	if (!localized_function_names)
		return func->name;

	if (func->localized_name)
		return func->localized_name;

	/*
	 * Deduce the translated names from the help texts.  This
	 * code doesn't currently check for clashes in translated
	 * names.
	 */

	gnm_func_load_if_stub (fd);

	for (i = 0;
	     (func->localized_name == NULL &&
	      func->help &&
	      func->help[i].type != GNM_FUNC_HELP_END);
	     i++) {
		const char *s, *sl;
		char *U;
		if (func->help[i].type != GNM_FUNC_HELP_NAME)
			continue;

		s = func->help[i].text;
		sl = F2 (func, s);
		if (s == sl) /* String not actually translated. */
			continue;

		U = split_at_colon (F2 (func, s), NULL);
		if (U) {
			char *lname = g_utf8_strdown (U, -1);
			gnm_func_set_localized_name (fd, lname);
			g_free (lname);
		}
		g_free (U);
	}

	if (!func->localized_name)
		gnm_func_set_localized_name (fd, fd->name);

	return func->localized_name;
}

/**
 * gnm_func_get_description:
 * @fn_def: the fn defintion
 *
 * Return value: the description of the function
 *
 **/
char const*
gnm_func_get_description (GnmFunc const *fn_def)
{
	gint i;
	g_return_val_if_fail (fn_def != NULL, NULL);

	gnm_func_load_if_stub ((GnmFunc *)fn_def);

	for (i = 0;
	     fn_def->help && fn_def->help[i].type != GNM_FUNC_HELP_END;
	     i++) {
		const char *desc;

		if (fn_def->help[i].type != GNM_FUNC_HELP_NAME)
			continue;

		desc = strchr (F2 (fn_def, fn_def->help[i].text), ':');
		return desc ? (desc + 1) : "";
	}
	return "";
}

/**
 * function_def_count_args:
 * @fn_def: pointer to function definition
 * @min: pointer to min. args
 * @max: pointer to max. args
 *
 * This calculates the max and min args that
 * can be passed; NB max can be G_MAXINT for
 * a vararg function.
 * NB. this data is not authoratitive for a
 * 'nodes' function.
 *
 **/
void
function_def_count_args (GnmFunc const *fn_def,
                         int *min, int *max)
{
	char const *ptr;
	int   i;
	int   vararg;

	g_return_if_fail (min != NULL);
	g_return_if_fail (max != NULL);
	g_return_if_fail (fn_def != NULL);

	gnm_func_load_if_stub ((GnmFunc *)fn_def);

	/*
	 * FIXME: clearly for 'nodes' functions many of
	 * the type fields will need to be filled.
	 */
	if (fn_def->fn_type == GNM_FUNC_TYPE_NODES) {
		*min = 0;
		if (g_ascii_strcasecmp ("INDEX",fn_def->name) == 0)
			*max = 4;
		else
			*max = G_MAXINT;
		return;
	}

	ptr = fn_def->fn.args.arg_spec;
	for (i = vararg = 0; ptr && *ptr; ptr++) {
		if (*ptr == '|') {
			vararg = 1;
			*min = i;
		} else
			i++;
	}
	*max = i;
	if (!vararg)
		*min = i;
}

/**
 * function_def_get_arg_type:
 * @fn_def: the fn defintion
 * @arg_idx: zero based argument offset
 *
 * Return value: the type of the argument
 **/
char
function_def_get_arg_type (GnmFunc const *fn_def, int arg_idx)
{
	char const *ptr;

	g_return_val_if_fail (arg_idx >= 0, '?');
	g_return_val_if_fail (fn_def != NULL, '?');

	gnm_func_load_if_stub ((GnmFunc *)fn_def);

	switch (fn_def->fn_type) {
	case GNM_FUNC_TYPE_ARGS:
		for (ptr = fn_def->fn.args.arg_spec; ptr && *ptr; ptr++) {
			if (*ptr == '|')
				continue;
			if (arg_idx-- == 0)
				return *ptr;
		}
		return '?';

	case GNM_FUNC_TYPE_NODES:
		return '?'; /* Close enough for now.  */

	case GNM_FUNC_TYPE_STUB:
#ifndef DEBUG_SWITCH_ENUM
	default:
#endif
		g_assert_not_reached ();
		return '?';
	}
}

/**
 * function_def_get_arg_type_string:
 * @fn_def: the fn defintion
 * @arg_idx: zero based argument offset
 *
 * Return value: the type of the argument as a string
 **/
char const *
function_def_get_arg_type_string (GnmFunc const *fn_def,
				  int arg_idx)
{
	switch (function_def_get_arg_type (fn_def, arg_idx)) {
	case 'f':
		return _("Number");
	case 's':
		return _("String");
	case 'b':
		return _("Boolean");
	case 'r':
		return _("Cell Range");
	case 'A':
		return _("Area");
	case 'E':
		return _("Scalar, Blank, or Error");
	case 'S':
		return _("Scalar");
	case '?':
		/* Missing values will be NULL.  */
		return _("Any");

	default:
		g_warning ("Unknown arg type");
		return "Broken";
	}
}

/**
 * function_def_get_arg_name:
 * @fn_def: the fn defintion
 * @arg_idx: zero based argument offset
 *
 * Return value: the name of the argument (must be freed)
 **/
char *
function_def_get_arg_name (GnmFunc const *fn_def, guint arg_idx)
{
	g_return_val_if_fail (fn_def != NULL, NULL);

	gnm_func_load_if_stub ((GnmFunc *)fn_def);

	if ((fn_def->arg_names_p != NULL)
	    && (arg_idx < fn_def->arg_names_p->len))
		return g_strdup (g_ptr_array_index (fn_def->arg_names_p,
						     arg_idx));
	return NULL;
}

/**
 * gnm_func_get_arg_description:
 * @fn_def: the fn defintion
 * @arg_idx: zero based argument offset
 *
 * Return value: the namedescription of the argument
 **/
char const*
gnm_func_get_arg_description (GnmFunc const *fn_def, guint arg_idx)
{
	gint i;
	g_return_val_if_fail (fn_def != NULL, NULL);

	gnm_func_load_if_stub ((GnmFunc *)fn_def);

	for (i = 0;
	     fn_def->help && fn_def->help[i].type != GNM_FUNC_HELP_END;
	     i++) {
		gchar const *desc;

		if (fn_def->help[i].type != GNM_FUNC_HELP_ARG)
			continue;
		if (arg_idx--)
			continue;

		desc = strchr (F2 (fn_def, fn_def->help[i].text), ':');
		if (!desc)
			return "";

		desc++;
		while (g_unichar_isspace (g_utf8_get_char (desc)))
			desc = g_utf8_next_char (desc);
		return desc;
	}

	return "";
}

/**
 * gnm_func_convert_markup_to_pango:
 * @desc: the fn or arg description string
 * @target: target widget for the markup.
 *
 * Return value: the escaped string with @{} markup converted to
 *               pango markup
 **/
char *
gnm_func_convert_markup_to_pango (char const *desc, GtkWidget *target)
{
	GString *str;
	gchar *markup, *at;
	GdkColor *link_color = NULL;
	char *link_color_text, *span_text;
	size_t span_text_len;

	gtk_widget_style_get (target, "link-color", &link_color, NULL);
	link_color_text = gdk_color_to_string (link_color);
	gdk_color_free (link_color);
	span_text = g_strdup_printf ("<span foreground=\"%s\">",
				     link_color_text);
	span_text_len = strlen (span_text);
	g_free (link_color_text);

	markup = g_markup_escape_text (desc, -1);
	str = g_string_new (markup);
	g_free (markup);

	while ((at = strstr (str->str, "@{"))) {
		gint len = at - str->str;
		go_string_replace (str, len, 2, span_text, -1);
		if ((at = strstr
		     (str->str + len + span_text_len, "}"))) {
			len = at - str->str;
			go_string_replace (str, len, 1, "</span>", -1);
		} else
			g_string_append (str, "</span>");
	}
	g_free (span_text);

	return g_string_free (str, FALSE);
}


/* ------------------------------------------------------------------------- */

static inline void
free_values (GnmValue **values, int top)
{
	int i;

	for (i = 0; i < top; i++)
		if (values [i])
			value_release (values [i]);
}

/* ------------------------------------------------------------------------- */

/**
 * function_call_with_exprs:
 * @ei: EvalInfo containing valid fn_def!
 *
 * Do the guts of calling a function.
 *
 * Returns the result.
 **/
GnmValue *
function_call_with_exprs (GnmFuncEvalInfo *ei)
{
	GnmFunc const *fn_def;
	int	  i, iter_count, iter_width = 0, iter_height = 0;
	char	  arg_type;
	GnmValue	 **args, *tmp = NULL;
	int	 *iter_item = NULL;
	int argc;
	GnmExprConstPtr *argv;
	GnmExprEvalFlags flags, pass_flags;

	g_return_val_if_fail (ei != NULL, NULL);
	g_return_val_if_fail (ei->func_call != NULL, NULL);

	flags = ei->flags;

	argc = ei->func_call->argc;
	argv = ei->func_call->argv;
	fn_def = ei->func_call->func;

	gnm_func_load_if_stub ((GnmFunc *)fn_def);

	/* Functions that deal with ExprNodes */
	if (fn_def->fn_type == GNM_FUNC_TYPE_NODES)
		return fn_def->fn.nodes (ei, argc, argv);

	/* Functions that take pre-computed Values */
	if (argc > fn_def->fn.args.max_args ||
	    argc < fn_def->fn.args.min_args)
		return value_new_error_NA (ei->pos);

	args = g_alloca (sizeof (GnmValue *) * fn_def->fn.args.max_args);
	iter_count = (eval_pos_is_array_context (ei->pos) &&
		      (flags & GNM_EXPR_EVAL_PERMIT_NON_SCALAR))
		? 0 : -1;

	/* Optimization for IF when implicit iteration is not used.  */
	if (ei->func_call->func->fn.args.func == gnumeric_if &&
	    iter_count == -1)
		return gnumeric_if2 (ei, argc, argv, flags);

	pass_flags = (flags &
		      (GNM_EXPR_EVAL_ARRAY_CONTEXT));

	for (i = 0; i < argc; i++) {
		char arg_type = fn_def->fn.args.arg_types[i];
		/* expr is always non-null, missing args are encoded as
		 * const = empty */
		GnmExpr const *expr = argv[i];

		if (arg_type == 'A' || arg_type == 'r') {
			tmp = args[i] = gnm_expr_eval
				(expr, ei->pos,
				 pass_flags |
				 GNM_EXPR_EVAL_PERMIT_NON_SCALAR |
				 GNM_EXPR_EVAL_WANT_REF);
			if (VALUE_IS_ERROR (tmp)) {
				free_values (args, i);
				return tmp;
			}

			if (tmp->type == VALUE_CELLRANGE) {
				gnm_cellref_make_abs (&tmp->v_range.cell.a,
						      &tmp->v_range.cell.a,
						      ei->pos);
				gnm_cellref_make_abs (&tmp->v_range.cell.b,
						      &tmp->v_range.cell.b,
						      ei->pos);
				/* Array args accept scalars */
			} else if (arg_type != 'A' && tmp->type != VALUE_ARRAY) {
				free_values (args, i + 1);
				return value_new_error_VALUE (ei->pos);
			}
			continue;
		}

		/* force scalars whenever we are certain */
		tmp = args[i] = gnm_expr_eval
			(expr, ei->pos,
			 pass_flags |
			 GNM_EXPR_EVAL_PERMIT_EMPTY |
			 (iter_count >= 0 || arg_type == '?'
			  ? GNM_EXPR_EVAL_PERMIT_NON_SCALAR
			  : 0));

		if (arg_type == '?')	/* '?' arguments are unrestriced */
			continue;

		/* optional arguments can be blank */
		if (i >= fn_def->fn.args.min_args && VALUE_IS_EMPTY (tmp)) {
			if (arg_type == 'E' && !gnm_expr_is_empty (expr)) {
				/* An actual argument produced empty.  Make
				   sure function sees that.  */
				args[i] = value_new_empty ();
			}

			continue;
		}

		if (tmp == NULL)
			tmp = args[i] = value_new_empty ();

		/* Handle implicit intersection or iteration depending on flags */
		if (tmp->type == VALUE_CELLRANGE || tmp->type == VALUE_ARRAY) {
			if (iter_count > 0) {
				if (iter_width != value_area_get_width (tmp, ei->pos) ||
				    iter_height != value_area_get_height (tmp, ei->pos)) {
					free_values (args, i + 1);
					return value_new_error_VALUE (ei->pos);
				}
			} else {
				if (iter_count < 0) {
					g_warning ("Damn I thought this was impossible");
					iter_count = 0;
				}
				iter_item = g_alloca (sizeof (int) * argc);
				iter_width = value_area_get_width (tmp, ei->pos);
				iter_height = value_area_get_height (tmp, ei->pos);
			}
			iter_item [iter_count++] = i;

			/* no need to check type, we would fail comparing a range against a "b, f, or s" */
			continue;
		}

		/* All of these argument types must be scalars */
		switch (arg_type) {
		case 'b':
			if (VALUE_IS_STRING (tmp)) {
				gboolean err;
				gboolean b = value_get_as_bool (tmp, &err);
				if (err) {
					free_values (args, i + 1);
					return value_new_error_VALUE (ei->pos);
				}
				value_release (args[i]);
				tmp = args[i] = value_new_bool (b);
				break;
			}
			/* Fall through.  */
		case 'f':
			if (VALUE_IS_STRING (tmp)) {
				tmp = format_match_number (value_peek_string (tmp), NULL,
					workbook_date_conv (ei->pos->sheet->workbook));
				if (tmp == NULL) {
					free_values (args, i + 1);
					return value_new_error_VALUE (ei->pos);
				}
				value_release (args [i]);
				args[i] = tmp;
			} else if (VALUE_IS_ERROR (tmp)) {
				free_values (args, i);
				return tmp;
			} else if (VALUE_IS_EMPTY (tmp)) {
				value_release (args [i]);
				tmp = args[i] = value_new_int (0);
			}

			if (!VALUE_IS_NUMBER (tmp))
				return value_new_error_VALUE (ei->pos);
			break;

		case 's':
		case 'S':
			if (VALUE_IS_ERROR (tmp)) {
				free_values (args, i);
				return tmp;
			}
			break;

		case 'E': /* nothing necessary */
			break;

		/* case '?': handled above */
		default :
			g_warning ("Unknown argument type '%c'", arg_type);
			break;
		}
	}

	while (i < fn_def->fn.args.max_args)
		args [i++] = NULL;

	if (iter_item != NULL) {
		int x, y;
		GnmValue *res = value_new_array_empty (iter_width, iter_height);
		GnmValue const *elem, *err;
		GnmValue **iter_vals = g_alloca (sizeof (GnmValue *) * iter_count);
		GnmValue **iter_args = g_alloca (sizeof (GnmValue *) * iter_count);

		/* collect the args we will iterate on */
		for (i = 0 ; i < iter_count; i++)
			iter_vals[i] = args[iter_item[i]];

		for (x = iter_width; x-- > 0 ; )
			for (y = iter_height; y-- > 0 ; ) {
				/* marshal the args */
				err = NULL;
				for (i = 0 ; i < iter_count; i++) {
					elem = value_area_get_x_y (iter_vals[i], x, y, ei->pos);
					arg_type = fn_def->fn.args.arg_types[iter_item[i]];
					if  (arg_type == 'b' || arg_type == 'f') {
						if (VALUE_IS_EMPTY (elem))
							elem = value_zero;
						else if (VALUE_IS_STRING (elem)) {
							tmp = format_match_number (value_peek_string (elem), NULL,
								workbook_date_conv (ei->pos->sheet->workbook));
							if (tmp != NULL) {
								args [iter_item[i]] = iter_args [i] = tmp;
								continue;
							} else
								break;
						} else if (VALUE_IS_ERROR (elem)) {
							err = elem;
							break;
						} else if (!VALUE_IS_NUMBER (elem))
							break;
					} else if (arg_type == 's') {
						if (VALUE_IS_EMPTY (elem)) {
							args [iter_item[i]] = iter_args [i] = value_new_string ("");
							continue;
						} else if (VALUE_IS_ERROR (elem)) {
							err = elem;
							break;
						} else if (!VALUE_IS_STRING (elem))
							break;
					} else if (elem == NULL) {
						args [iter_item[i]] = iter_args [i] = value_new_empty ();
						continue;
					}
					args [iter_item[i]] = iter_args [i] = value_dup (elem);
				}

				res->v_array.vals[x][y] = (i == iter_count)
					? fn_def->fn.args.func (ei, (GnmValue const * const *)args)
					: ((err != NULL) ? value_dup (err)
							 : value_new_error_VALUE (ei->pos));
				free_values (iter_args, i);
			}

		/* free the primaries, not the already freed iteration */
		for (i = 0 ; i < iter_count; i++)
			args[iter_item[i]] = iter_vals[i];
		tmp = res;
		i = fn_def->fn.args.max_args;
	} else
		tmp = fn_def->fn.args.func (ei, (GnmValue const * const *)args);

	free_values (args, i);
	return tmp;
}

/*
 * Use this to invoke a register function: the only drawback is that
 * you have to compute/expand all of the values to use this
 */
GnmValue *
function_call_with_values (GnmEvalPos const *ep, char const *fn_name,
			   int argc, GnmValue const * const *values)
{
	GnmFunc *fn_def;

	g_return_val_if_fail (ep != NULL, NULL);
	g_return_val_if_fail (fn_name != NULL, NULL);
	g_return_val_if_fail (ep->sheet != NULL, NULL);

	/* FIXME : support workbook local functions */
	fn_def = gnm_func_lookup (fn_name, NULL);
	if (fn_def == NULL)
		return value_new_error_NAME (ep);
	return function_def_call_with_values (ep, fn_def, argc, values);
}

GnmValue *
function_def_call_with_values (GnmEvalPos const *ep, GnmFunc const *fn_def,
                               int argc, GnmValue const * const *values)
{
	GnmValue *retval;
	GnmExprFunction	ef;
	GnmFuncEvalInfo fs;

	fs.pos = ep;
	fs.func_call = &ef;
	ef.func = (GnmFunc *)fn_def;

	gnm_func_load_if_stub ((GnmFunc *)fn_def);

	if (fn_def->fn_type == GNM_FUNC_TYPE_NODES) {
		/*
		 * If function deals with ExprNodes, create some
		 * temporary ExprNodes with constants.
		 */
		GnmExprConstant *expr = g_new (GnmExprConstant, argc);
		GnmExprConstPtr *argv = g_new (GnmExprConstPtr, argc);
		int i;

		for (i = 0; i < argc; i++) {
			gnm_expr_constant_init (expr + i, values[i]);
			argv[i] = (GnmExprConstPtr)(expr + i);
		}
		retval = fn_def->fn.nodes (&fs, argc, argv);
		g_free (argv);
		g_free (expr);
	} else
		retval = fn_def->fn.args.func (&fs, values);

	return retval;
}

/* ------------------------------------------------------------------------- */

typedef struct {
	FunctionIterateCB  callback;
	void              *closure;
	gboolean           strict;
	gboolean           ignore_subtotal;
} IterateCallbackClosure;

/**
 * cb_iterate_cellrange:
 *
 * Helper routine used by the function_iterate_do_value routine.
 * Invoked by the sheet cell range iterator.
 **/
static GnmValue *
cb_iterate_cellrange (GnmCellIter const *iter, gpointer user)

{
	IterateCallbackClosure *data = user;
	GnmCell  *cell;
	GnmValue *res;
	GnmEvalPos ep;

	if (NULL == (cell = iter->cell)) {
		ep.sheet = iter->pp.sheet;
		ep.dep = NULL;
		ep.eval.col = iter->pp.eval.col;
		ep.eval.row = iter->pp.eval.row;
		return (*data->callback)(&ep, NULL, data->closure);
	}

	if (data->ignore_subtotal && gnm_cell_has_expr (cell) &&
	    gnm_expr_top_contains_subtotal (cell->base.texpr))
		return NULL;

	gnm_cell_eval (cell);
	eval_pos_init_cell (&ep, cell);

	/* If we encounter an error for the strict case, short-circuit here.  */
	if (data->strict && (NULL != (res = gnm_cell_is_error (cell))))
		return value_new_error_str (&ep, res->v_err.mesg);

	/* All other cases -- including error -- just call the handler.  */
	return (*data->callback)(&ep, cell->value, data->closure);
}

/*
 * function_iterate_do_value:
 *
 * Helper routine for function_iterate_argument_values.
 */
static GnmValue *
function_iterate_do_value (GnmEvalPos const  *ep,
			   FunctionIterateCB  callback,
			   gpointer	      closure,
			   GnmValue const    *value,
			   gboolean           strict,
			   CellIterFlags      iter_flags)
{
	GnmValue *res = NULL;

	switch (value->type){
	case VALUE_ERROR:
		if (strict) {
			res = value_dup (value);
			break;
		}
		/* Fall through.  */

	case VALUE_EMPTY:
	case VALUE_BOOLEAN:
	case VALUE_FLOAT:
	case VALUE_STRING:
		res = (*callback)(ep, value, closure);
		break;

	case VALUE_ARRAY: {
		int x, y;

		/* Note the order here.  */
		for (y = 0; y < value->v_array.y; y++) {
			  for (x = 0; x < value->v_array.x; x++) {
				res = function_iterate_do_value (
					ep, callback, closure,
					value->v_array.vals [x][y],
					strict, CELL_ITER_IGNORE_BLANK);
				if (res != NULL)
					return res;
			}
		}
		break;
	}
	case VALUE_CELLRANGE: {
		IterateCallbackClosure data;

		data.callback = callback;
		data.closure  = closure;
		data.strict   = strict;
		data.ignore_subtotal = (iter_flags & CELL_ITER_IGNORE_SUBTOTAL) != 0;

		res = workbook_foreach_cell_in_range (ep, value, iter_flags,
						      cb_iterate_cellrange,
						      &data);
	}
	}
	return res;
}

/**
 * function_iterate_argument_values:
 * @ep:               The position in a workbook at which to evaluate
 * @callback: (scope call): The routine to be invoked for every value computed
 * @callback_closure: Closure for the callback.
 * @argc:
 * @argv:
 * @strict:           If TRUE, the function is considered "strict".  This means
 *                   that if an error value occurs as an argument, the iteration
 *                   will stop and that error will be returned.  If FALSE, an
 *                   error will be passed on to the callback (as a GnmValue *
 *                   of type VALUE_ERROR).
 * @iter_flags:
 *
 * Return value:
 *    NULL            : if no errors were reported.
 *    GnmValue *         : if an error was found during strict evaluation
 *    VALUE_TERMINATE : if the callback requested termination of the iteration.
 *
 * This routine provides a simple way for internal functions with variable
 * number of arguments to be written: this would iterate over a list of
 * expressions (expr_node_list) and will invoke the callback for every
 * GnmValue found on the list (this means that ranges get properly expaned).
 **/
GnmValue *
function_iterate_argument_values (GnmEvalPos const	*ep,
				  FunctionIterateCB	 callback,
				  void			*callback_closure,
				  int                    argc,
				  GnmExprConstPtr const *argv,
				  gboolean		 strict,
				  CellIterFlags		 iter_flags)
{
	GnmValue *result = NULL;
	int a;

	for (a = 0; result == NULL && a < argc; a++) {
		GnmExpr const *expr = argv[a];
		GnmValue *val;

		if (iter_flags & CELL_ITER_IGNORE_SUBTOTAL &&
		    gnm_expr_contains_subtotal (expr))
			continue;

		/* need to drill down into names to handle things like
		 * sum(name)  with name := (A:A,B:B) */
		while (GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_NAME) {
			expr = expr->name.name->texpr->expr;
			if (expr == NULL) {
				if (strict)
					return value_new_error_REF (ep);
				continue;
			}
		}

		/* Handle sets as a special case */
		if (GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_SET) {
			result = function_iterate_argument_values
				(ep, callback, callback_closure,
				 expr->set.argc, expr->set.argv,
				 strict, iter_flags);
			continue;
		}

		/* We need a cleaner model of what to do here.
		 * In non-array mode
		 *	SUM(Range)
		 * will obviously return Range
		 *
		 *	SUM(INDIRECT(Range))
		 *	SUM(INDIRECT(Range):....)
		 * will do implicit intersection on Range (in non-array mode),
		 * but allow non-scalar results from indirect (no intersection)
		 *
		 *	SUM(Range=3)
		 * will do implicit intersection in non-array mode */
		if (GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_CONSTANT)
			val = value_dup (expr->constant.value);
		else if (eval_pos_is_array_context (ep) ||
			 GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_FUNCALL ||
			 GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_RANGE_CTOR ||
			 GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_INTERSECT)
			val = gnm_expr_eval (expr, ep,
				GNM_EXPR_EVAL_PERMIT_EMPTY | GNM_EXPR_EVAL_PERMIT_NON_SCALAR);
		else
			val = gnm_expr_eval (expr, ep,
				GNM_EXPR_EVAL_PERMIT_EMPTY);

		if (val == NULL)
			continue;

		if (strict && VALUE_IS_ERROR (val)) {
			/* Be careful not to make VALUE_TERMINATE into a real value */
			return val;
		}

		result = function_iterate_do_value (ep, callback, callback_closure,
						    val, strict, iter_flags);
		value_release (val);
	}
	return result;
}
