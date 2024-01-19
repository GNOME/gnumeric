/*
 * sstest.c: Test code for Gnumeric
 *
 * Copyright (C) 2009,2017 Morten Welinder (terra@gnome.org)
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <libgnumeric.h>
#include <goffice/goffice.h>
#include <command-context-stderr.h>
#include <workbook-view.h>
#include <workbook.h>
#include <gutils.h>
#include <gnm-plugin.h>
#include <parse-util.h>
#include <expr-name.h>
#include <expr.h>
#include <search.h>
#include <sheet.h>
#include <cell.h>
#include <value.h>
#include <func.h>
#include <parse-util.h>
#include <sheet-object-cell-comment.h>
#include <mathfunc.h>
#include <gnm-random.h>
#include <sf-dpq.h>
#include <sf-gamma.h>
#include <rangefunc.h>
#include <gnumeric-conf.h>

#include <gsf/gsf-input-stdio.h>
#include <gsf/gsf-input-textline.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <string.h>
#include <errno.h>

static gboolean sstest_show_version = FALSE;
static gboolean sstest_fast = FALSE;
static gchar *func_def_file = NULL;
static gchar *func_state_file = NULL;
static gchar *ext_refs_file = NULL;
static gchar *samples_file = NULL;

static GOptionEntry const sstest_options [] = {
	{
		"fast", 'f',
		0, G_OPTION_ARG_NONE, &sstest_fast,
		N_("Run fewer iterations"),
		NULL
	},

	{
		"dump-func-defs", 0,
		0, G_OPTION_ARG_FILENAME, &func_def_file,
		N_("Dumps the function definitions"),
		N_("FILE")
	},

	{
		"dump-func-state", 0,
		0, G_OPTION_ARG_FILENAME, &func_state_file,
		N_("Dumps the function definitions"),
		N_("FILE")
	},

	{
		"ext-refs-file", 0,
		0, G_OPTION_ARG_FILENAME, &ext_refs_file,
		N_("Dumps web page for function help"),
		N_("FILE")
	},

	{
		"samples-file", 0,
		0, G_OPTION_ARG_FILENAME, &samples_file,
		N_("Dumps list of samples in function help"),
		N_("FILE")
	},

	{
		"version", 'V',
		0, G_OPTION_ARG_NONE, &sstest_show_version,
		N_("Display program version"),
		NULL
	},

	{ NULL }
};

/* ------------------------------------------------------------------------- */

#define UNICODE_ELLIPSIS "\xe2\x80\xa6"

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
		GnmFunc *fd = g_ptr_array_index (defs, ui);
		gboolean any = FALSE;
		int j, n;
		GnmFuncHelp const *help = gnm_func_get_help (fd, &n);

		for (j = 0; j < n; j++) {
			const char *s = gnm_func_gettext (fd, help[j].text);

			switch (help[j].type) {
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

static void
csv_quoted_print (FILE *out, const char *s)
{
	char quote = '"';
	fputc (quote, out);
	while (*s) {
		if (*s == quote) {
			fputc (quote, out);
			fputc (quote, out);
			s++;
		} else {
			int len = g_utf8_skip[(unsigned char)*s];
			fprintf (out, "%-.*s", len, s);
			s += len;
		}
	}
	fputc ('"', out);
}

static void
dump_samples (GPtrArray *defs, FILE *out)
{
	unsigned ui;
	GnmFuncGroup *last_group = NULL;

	for (ui = 0; ui < defs->len; ui++) {
		GnmFunc *fd = g_ptr_array_index (defs, ui);
		int j, n;
		const char *last = NULL;
		gboolean has_sample = FALSE;
		GnmFuncHelp const *help = gnm_func_get_help (fd, &n);

		if (last_group != gnm_func_get_function_group (fd)) {
			last_group = gnm_func_get_function_group (fd);
			csv_quoted_print (out, last_group->display_name->str);
			fputc ('\n', out);
		}

		for (j = 0; j < n; j++) {
			const char *s = help[j].text;

			if (help[j].type != GNM_FUNC_HELP_EXAMPLES)
				continue;

			has_sample = TRUE;

			/*
			 * Some of the random numbers functions have duplicate
			 * samples.  We don't want the duplicates here.
			 */
			if (s[0] != '=' || (last && strcmp (last, s) == 0))
				continue;

			fputc (',', out);
			if (!last)
				csv_quoted_print (out, fd->name);
			last = s;

			fputc (',', out);
			csv_quoted_print (out, s);
			fputc ('\n', out);
		}

		if (!has_sample)
			g_printerr ("No samples for %s\n", fd->name);
	}
}

static int
func_def_cmp (gconstpointer a, gconstpointer b)
{
	GnmFunc *fda = *(GnmFunc **)a ;
	GnmFunc *fdb = *(GnmFunc **)b ;
	GnmFuncGroup *ga, *gb;

	g_return_val_if_fail (fda->name != NULL, 0);
	g_return_val_if_fail (fdb->name != NULL, 0);

	ga = gnm_func_get_function_group (fda);
	gb = gnm_func_get_function_group (fdb);

	if (ga && gb) {
		int res = go_string_cmp (ga->display_name, gb->display_name);
		if (res != 0)
			return res;
	}

	return g_ascii_strcasecmp (fda->name, fdb->name);
}

static GPtrArray *
enumerate_functions (gboolean filter)
{
	GPtrArray *res = gnm_func_enumerate ();
	unsigned ui;

	for (ui = 0; ui < res->len; ui++) {
		GnmFunc *fd = g_ptr_array_index (res, ui);

		if (filter &&
		    (fd->name == NULL ||
		     strcmp (fd->name, "perl_adder") == 0 ||
		     strcmp (fd->name, "perl_date") == 0 ||
		     strcmp (fd->name, "perl_sed") == 0 ||
		     strcmp (fd->name, "py_capwords") == 0 ||
		     strcmp (fd->name, "py_printf") == 0 ||
		     strcmp (fd->name, "py_bitand") == 0)) {
			g_ptr_array_remove_index_fast (res, ui);
			ui--;
		}

		gnm_func_load_if_stub (fd);
	}

	g_ptr_array_sort (res, func_def_cmp);

	return res;
}

/**
 * function_dump_defs:
 * @filename:
 * @dump_type:
 *
 * A generic utility routine to operate on all function defs
 * in various ways.  @dump_type will change/extend as needed
 * Right now
 * 0 : www.gnumeric.org's function.shtml page
 * 1:
 * 2 : (obsolete)
 * 3 : (obsolete)
 * 4 : external refs
 * 5 : all sample expressions
 **/
static void
function_dump_defs (char const *filename, int dump_type)
{
	FILE *output_file;
	char *up, *catname;
	unsigned i;
	GPtrArray *ordered;
	GnmFuncGroup const *group = NULL;

	g_return_if_fail (filename != NULL);

	if ((output_file = g_fopen (filename, "w")) == NULL){
		g_printerr (_("Cannot create file %s\n"), filename);
		exit (1);
	}

	/* TODO : Use the translated names and split by function group. */
	ordered = enumerate_functions (TRUE);

	if (dump_type == 4) {
		dump_externals (ordered, output_file);
		g_ptr_array_free (ordered, TRUE);
		fclose (output_file);
		return;
	}

	if (dump_type == 5) {
		dump_samples (ordered, output_file);
		g_ptr_array_free (ordered, TRUE);
		fclose (output_file);
		return;
	}

	if (dump_type == 0) {
		int unique = 0;
		for (i = 0; i < ordered->len; i++) {
			GnmFunc *fd = g_ptr_array_index (ordered, i);
			switch (gnm_func_get_impl_status (fd)) {
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
		GnmFunc *fd = g_ptr_array_index (ordered, i);

		// Skip internal-use function
		if (g_ascii_strcasecmp (fd->name, "TABLE") == 0)
			continue;

		// Skip demo function
		if (g_ascii_strcasecmp (fd->name, "ATL_LAST") == 0)
			continue;

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
			GnmFuncGroup *group = gnm_func_get_function_group (fd);
			int n;
			GnmFuncHelp const *help = gnm_func_get_help (fd, &n);

			fprintf (output_file, "@CATEGORY=%s\n",
				 gnm_func_gettext (fd, group->display_name->str));
			for (i = 0; i < n; i++) {
				switch (help[i].type) {
				case GNM_FUNC_HELP_NAME: {
					char *short_desc;
					char *name = split_at_colon (gnm_func_gettext (fd, help[i].text), &short_desc);
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
					g_string_append (seealso, gnm_func_gettext (fd, help[i].text));
					break;
				case GNM_FUNC_HELP_DESCRIPTION:
					if (desc->len > 0)
						g_string_append (desc, "\n");
					g_string_append (desc, gnm_func_gettext (fd, help[i].text));
					break;
				case GNM_FUNC_HELP_NOTE:
					if (note->len > 0)
						g_string_append (note, " ");
					g_string_append (note, gnm_func_gettext (fd, help[i].text));
					break;
				case GNM_FUNC_HELP_ARG: {
					char *argdesc;
					char *name = split_at_colon (gnm_func_gettext (fd, help[i].text), &argdesc);
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
					g_string_append (odf, gnm_func_gettext (fd, help[i].text));
					break;
				case GNM_FUNC_HELP_EXCEL:
					if (excel->len > 0)
						g_string_append (excel, " ");
					g_string_append (excel, gnm_func_gettext (fd, help[i].text));
					break;

				case GNM_FUNC_HELP_EXTREF:
					/* FIXME! */
				case GNM_FUNC_HELP_EXAMPLES:
					/* FIXME! */
				case GNM_FUNC_HELP_END:
					break;
				}
			}

			gnm_func_count_args (fd, &min, &max);
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
			GnmFuncImplStatus imst = gnm_func_get_impl_status (fd);
			GnmFuncTestStatus test = gnm_func_get_test_status (fd);

			if (group != gnm_func_get_function_group (fd)) {
				if (group) fprintf (output_file, "</table></div>\n");
				group = gnm_func_get_function_group (fd);
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
				 "<td><a href =\"https://help.gnome.org/users/gnumeric/stable/gnumeric.html#gnumeric-function-%s\">%s</a></td>\n",
				 up, fd->name);
			g_free (up);
			g_free (catname);
			fprintf (output_file,
				 "<td class=\"%s\"><a href=\"mailto:gnumeric-list@gnome.org?subject=Re: %s implementation\">%s</a></td>\n",
				 implementation[imst].klass,
				 fd->name,
				 implementation[imst].name);
			fprintf (output_file,
				 "<td class=\"%s\"><a href=\"mailto:gnumeric-list@gnome.org?subject=Re: %s testing\">%s</a></td>\n",
				 testing[test].klass,
				 fd->name,
				 testing[test].name);
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

static void
mark_test_start (const char *name)
{
	g_printerr ("-----------------------------------------------------------------------------\nStart: %s\n-----------------------------------------------------------------------------\n\n", name);
}

static void
mark_test_end (const char *name)
{
	g_printerr ("End: %s\n\n", name);
}

static void
cb_collect_names (G_GNUC_UNUSED const char *name, GnmNamedExpr *nexpr, GSList **names)
{
	*names = g_slist_prepend (*names, nexpr);
}

static GnmCell *
fetch_cell (Sheet *sheet, const char *where)
{
	GnmCellPos cp;
	gboolean ok = cellpos_parse (where,
				     gnm_sheet_get_size (sheet),
				     &cp, TRUE) != NULL;
	g_return_val_if_fail (ok, NULL);
	return sheet_cell_fetch (sheet, cp.col, cp.row);
}

static void
set_cell (Sheet *sheet, const char *where, const char *what)
{
	GnmCell *cell = fetch_cell (sheet, where);
	if (cell)
		gnm_cell_set_text (cell, what);
}

static void
dump_sheet (Sheet *sheet, const char *header)
{
	GPtrArray *cells = sheet_cells (sheet, NULL);
	unsigned ui;

	if (header)
		g_printerr ("# %s\n", header);
	for (ui = 0; ui < cells->len; ui++) {
		GnmCell *cell = g_ptr_array_index (cells, ui);
		char *txt = gnm_cell_get_entered_text (cell);
		g_printerr ("%s: %s\n",
			    cellpos_as_string (&cell->pos), txt);
		g_free (txt);
	}
	g_ptr_array_free (cells, TRUE);
}


static void
dump_names (Workbook *wb)
{
	GSList *l, *names = NULL;

	workbook_foreach_name (wb, FALSE, (GHFunc)cb_collect_names, &names);
	names = g_slist_sort (names, (GCompareFunc)expr_name_cmp_by_name);

	g_printerr ("Dumping names...\n");
	for (l = names; l; l = l->next) {
		GnmNamedExpr *nexpr = l->data;
		GnmConventionsOut out;

		out.accum = g_string_new (NULL);
		out.pp = &nexpr->pos;
		out.convs = gnm_conventions_default;

		g_string_append (out.accum, "Scope=");
		if (out.pp->sheet)
			g_string_append (out.accum, out.pp->sheet->name_quoted);
		else
			g_string_append (out.accum, "Global");

		g_string_append (out.accum, " Name=");
		go_strescape (out.accum, expr_name_name (nexpr));

		g_string_append (out.accum, " Expr=");
		gnm_expr_top_as_gstring (nexpr->texpr, &out);

		g_printerr ("%s\n", out.accum->str);
		g_string_free (out.accum, TRUE);
	}
	g_printerr ("Dumping names... Done\n");

	g_slist_free (names);
}

static void
define_name (const char *name, const char *expr_txt, gpointer scope)
{
	GnmParsePos pos;
	GnmExprTop const *texpr;
	GnmNamedExpr const *nexpr;
	GnmConventions const *convs;

	if (IS_SHEET (scope)) {
		parse_pos_init_sheet (&pos, scope);
		convs = sheet_get_conventions (pos.sheet);
	} else {
		parse_pos_init (&pos, WORKBOOK (scope), NULL, 0, 0);
		convs = gnm_conventions_default;
	}

	texpr = gnm_expr_parse_str (expr_txt, &pos,
				    GNM_EXPR_PARSE_DEFAULT,
				    convs, NULL);
	if (!texpr) {
		g_printerr ("Failed to parse %s for name %s\n",
			    expr_txt, name);
		return;
	}

	nexpr = expr_name_add (&pos, name, texpr, NULL, TRUE, NULL);
	if (!nexpr)
		g_printerr ("Failed to add name %s\n", name);
}

static void
test_insdel_rowcol_names (void)
{
	Workbook *wb;
	Sheet *sheet1,*sheet2;
	const char *test_name = "test_insdel_rowcol_names";
	GOUndo *undo;
	int i;

	mark_test_start (test_name);

	wb = workbook_new ();
	sheet1 = workbook_sheet_add (wb, -1,
				     GNM_DEFAULT_COLS, GNM_DEFAULT_ROWS);
	sheet2 = workbook_sheet_add (wb, -1,
				     GNM_DEFAULT_COLS, GNM_DEFAULT_ROWS);

	define_name ("Print_Area", "Sheet1!$A$1:$IV$65536", sheet1);
	define_name ("Print_Area", "Sheet2!$A$1:$IV$65536", sheet2);

	define_name ("NAMEGA1", "A1", wb);
	define_name ("NAMEG2", "$A$14+Sheet1!$A$14+Sheet2!$A$14", wb);

	define_name ("NAMEA1", "A1", sheet1);
	define_name ("NAMEA2", "A2", sheet1);
	define_name ("NAMEA1ABS", "$A$1", sheet1);
	define_name ("NAMEA2ABS", "$A$2", sheet1);

	dump_names (wb);

	for (i = 3; i >= 0; i--) {
		g_printerr ("About to insert before column %s on %s\n",
			    col_name (i), sheet1->name_unquoted);
		sheet_insert_cols (sheet1, i, 12, &undo, NULL);
		dump_names (wb);
		g_printerr ("Undoing.\n");
		go_undo_undo_with_data (undo, NULL);
		g_object_unref (undo);
		g_printerr ("Done.\n");
	}

	for (i = 3; i >= 0; i--) {
		g_printerr ("About to insert before column %s on %s\n",
			    col_name (i), sheet2->name_unquoted);
		sheet_insert_cols (sheet2, i, 12, &undo, NULL);
		dump_names (wb);
		g_printerr ("Undoing.\n");
		go_undo_undo_with_data (undo, NULL);
		g_object_unref (undo);
		g_printerr ("Done.\n");
	}

	for (i = 3; i >= 0; i--) {
		g_printerr ("About to delete column %s on %s\n",
			    col_name (i), sheet1->name_unquoted);
		sheet_delete_cols (sheet1, i, 1, &undo, NULL);
		dump_names (wb);
		g_printerr ("Undoing.\n");
		go_undo_undo_with_data (undo, NULL);
		g_object_unref (undo);
		g_printerr ("Done.\n");
	}

	g_object_unref (wb);

	mark_test_end (test_name);
}

/* ------------------------------------------------------------------------- */

static void
test_insert_delete (void)
{
	const char *test_name = "test_insert_delete";
	Workbook *wb;
	Sheet *sheet1;
	int i;
	GOUndo *u = NULL, *u1;

	mark_test_start (test_name);

	wb = workbook_new ();
	sheet1 = workbook_sheet_add (wb, -1,
				     GNM_DEFAULT_COLS, GNM_DEFAULT_ROWS);
	set_cell (sheet1, "B2", "=D4+1");
	set_cell (sheet1, "D2", "=if(TRUE,B2,2)");

	dump_sheet (sheet1, "Init");

	for (i = 5; i >= 0; i--) {
		g_printerr ("# About to insert column before %s\n",
			    col_name (i));
		sheet_insert_cols (sheet1, i, 1, &u1, NULL);
		u = go_undo_combine (u, u1);
		dump_sheet (sheet1, NULL);
	}

	for (i = 5; i >= 0; i--) {
		g_printerr ("# About to insert row before %s\n",
			    row_name (i));
		sheet_insert_rows (sheet1, i, 1, &u1, NULL);
		u = go_undo_combine (u, u1);
		dump_sheet (sheet1, NULL);
	}

	go_undo_undo (u);
	g_object_unref (u);
	u = NULL;
	dump_sheet (sheet1, "Undo the lot");

	for (i = 5; i >= 0; i--) {
		g_printerr ("# About to delete column %s\n",
			    col_name (i));
		sheet_delete_cols (sheet1, i, 1, &u1, NULL);
		u = go_undo_combine (u, u1);
		dump_sheet (sheet1, NULL);
	}

	for (i = 5; i >= 0; i--) {
		g_printerr ("# About to delete row %s\n",
			    row_name (i));
		sheet_delete_rows (sheet1, i, 1, &u1, NULL);
		u = go_undo_combine (u, u1);
		dump_sheet (sheet1, NULL);
	}

	go_undo_undo (u);
	g_object_unref (u);
	u = NULL;
	dump_sheet (sheet1, "Undo the lot");

	g_object_unref (wb);

	mark_test_end (test_name);
}

/* ------------------------------------------------------------------------- */

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
check_argument_refs (const char *text, GnmFunc *fd)
{
	if (!gnm_func_is_fixargs (fd))
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
			char *thisarg = gnm_func_get_arg_name (fd, i);
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
gnm_func_sanity_check1 (GnmFunc *fd)
{
	GnmFuncHelp const *h;
	int counts[(int)GNM_FUNC_HELP_ODF + 1];
	int res = 0;
	size_t nlen = strlen (fd->name);
	GHashTable *allargs;
	int n;
	GnmFuncHelp const *help = gnm_func_get_help (fd, &n);

	allargs = g_hash_table_new_full
		(g_str_hash, g_str_equal, (GDestroyNotify)g_free, NULL);

	memset (counts, 0, sizeof (counts));
	for (h = help; n-- > 0; h++) {
		unsigned len;

		g_assert (h->type <= GNM_FUNC_HELP_ODF);
		counts[h->type]++;

		if (!g_utf8_validate (h->text, -1, NULL)) {
			g_printerr ("%s: Invalid UTF-8 in type %i\n",
				    fd->name, h->type);
				res = 1;
				continue;
		}

		len = h->text ? strlen (h->text) : 0;
		switch (h->type) {
		case GNM_FUNC_HELP_NAME:
			if (g_ascii_strncasecmp (fd->name, h->text, nlen) ||
			    h->text[nlen] != ':') {
				g_printerr ("%s: Invalid NAME record\n",
					    fd->name);
				res = 1;
			} else if (h->text[nlen + 1] == ' ' ||
				   h->text[len - 1] == ' ') {
				g_printerr ("%s: Unwanted space in NAME record\n",
					    fd->name);
				res = 1;
			} else if (h->text[len - 1] == '.') {
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
				g_printerr ("%s: Invalid argument reference, %s, in argument\n",
					    aend + 1, fd->name);
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

		case GNM_FUNC_HELP_SEEALSO: {
			const char *p = h->text;
			if (len == 0 || strchr (p, ' ')) {
				g_printerr ("%s: Invalid SEEALSO record\n",
					    fd->name);
				res = 1;
				break;
			}

			while (p) {
				char *ref;
				const char *e = strchr (p, ',');
				if (!e) e = p + strlen (p);

				ref = g_strndup (p, e - p);
				if (!gnm_func_lookup (ref, NULL)) {
					g_printerr ("%s: unknown SEEALSO record\reference %s",
						    fd->name, ref);
					res = 1;
				}
				g_free (ref);
				if (*e == 0)
					break;
				else
					p = e + 1;
			}

			break;
		}
		default:
			; /* Nothing */
		}
	}

	g_hash_table_destroy (allargs);

	if (gnm_func_is_fixargs (fd)) {
		int n = counts[GNM_FUNC_HELP_ARG];
		int min, max;
		gnm_func_count_args (fd, &min, &max);
		if (n != max) {
			g_printerr ("%s: Help for %d args, but takes %d-%d\n",
				    fd->name, n, min, max);
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

	if (counts[GNM_FUNC_HELP_SEEALSO] > 1) {
		g_printerr ("%s: Help has %d SEEALSO notes.\n",
			    fd->name, counts[GNM_FUNC_HELP_SEEALSO]);
		res = 1;
	}

	return res;
}

static int
gnm_func_sanity_check (void)
{
	int res = 0;
	GPtrArray *ordered;
	unsigned ui;

	ordered = enumerate_functions (TRUE);

	for (ui = 0; ui < ordered->len; ui++) {
		GnmFunc *fd = g_ptr_array_index (ordered, ui);
		if (gnm_func_sanity_check1 (fd))
			res = 1;
	}

	g_ptr_array_free (ordered, TRUE);

	return res;
}

static void
test_func_help (void)
{
	const char *test_name = "test_func_help";
	int res;

	mark_test_start (test_name);

	res = gnm_func_sanity_check ();
	g_printerr ("Result = %d\n", res);

	mark_test_end (test_name);
}

/* ------------------------------------------------------------------------- */

static int
test_strtol_ok (const char *s, long l, size_t expected_len)
{
	long l2;
	char *end;
	int save_errno;

	l2 = gnm_utf8_strtol (s, &end);
	save_errno = errno;

	if (end != s + expected_len) {
		g_printerr ("Unexpect conversion end of [%s]\n", s);
		return 1;
	}
	if (l != l2) {
		g_printerr ("Unexpect conversion result of [%s]\n", s);
		return 1;
	}
	if (save_errno != 0) {
		g_printerr ("Unexpect conversion errno of [%s]\n", s);
		return 1;
	}

	return 0;
}

static int
test_strtol_noconv (const char *s)
{
	long l;
	char *end;
	int save_errno;

	l = gnm_utf8_strtol (s, &end);
	save_errno = errno;

	if (end != s) {
		g_printerr ("Unexpect conversion end of [%s]\n", s);
		return 1;
	}
	if (l != 0) {
		g_printerr ("Unexpect conversion result of [%s]\n", s);
		return 1;
	}
	if (save_errno != 0) {
		g_printerr ("Unexpect conversion errno of [%s]\n", s);
		return 1;
	}

	return 0;
}

static int
test_strtol_overflow (const char *s, gboolean pos)
{
	long l;
	char *end;
	int save_errno;
	size_t expected_len = strlen (s);

	l = gnm_utf8_strtol (s, &end);
	save_errno = errno;

	if (end != s + expected_len) {
		g_printerr ("Unexpect conversion end of [%s]\n", s);
		return 1;
	}
	if (l != (pos ? LONG_MAX : LONG_MIN)) {
		g_printerr ("Unexpect conversion result of [%s]\n", s);
		return 1;
	}
	if (save_errno != ERANGE) {
		g_printerr ("Unexpect conversion errno of [%s]\n", s);
		return 1;
	}

	return 0;
}

static int
test_strtol_reverse (long l)
{
	char buffer[4*sizeof(l) + 4];
	int res = 0;

	sprintf(buffer, "%ld", l);
	res |= test_strtol_ok (buffer, l, strlen (buffer));

	sprintf(buffer, " %ld", l);
	res |= test_strtol_ok (buffer, l, strlen (buffer));

	sprintf(buffer, "\xc2\xa0\n\t%ld", l);
	res |= test_strtol_ok (buffer, l, strlen (buffer));

	sprintf(buffer, " \t%ldx", l);
	res |= test_strtol_ok (buffer, l, strlen (buffer) - 1);

	return res;
}

static int
test_strtod_ok (const char *s, gnm_float d, size_t expected_len)
{
	gnm_float d2;
	char *end;
	int save_errno;

	d2 = gnm_utf8_strto (s, &end);
	save_errno = errno;

	if (end != s + expected_len) {
		g_printerr ("Unexpect conversion end of [%s]\n", s);
		return 1;
	}
	if (d != d2) {
		g_printerr ("Unexpect conversion result of [%s]\n", s);
		return 1;
	}
	if (save_errno != 0) {
		g_printerr ("Unexpect conversion errno of [%s]\n", s);
		return 1;
	}

	return 0;
}

static void
test_nonascii_numbers (void)
{
	const char *test_name = "test_nonascii_numbers";
	int res = 0;

	mark_test_start (test_name);

	res |= test_strtol_reverse (0);
	res |= test_strtol_reverse (1);
	res |= test_strtol_reverse (-1);
	res |= test_strtol_reverse (LONG_MIN);
	res |= test_strtol_reverse (LONG_MIN + 1);
	res |= test_strtol_reverse (LONG_MAX - 1);

	res |= test_strtol_ok ("\xef\xbc\x8d\xef\xbc\x91", -1, 6);
	res |= test_strtol_ok ("\xc2\xa0+1", 1, 4);

	res |= test_strtol_ok ("000000000000000000000000000000", 0, 30);

	res |= test_strtol_noconv ("");
	res |= test_strtol_noconv (" ");
	res |= test_strtol_noconv (" +");
	res |= test_strtol_noconv (" -");
	res |= test_strtol_noconv (" .00");
	res |= test_strtol_noconv (" e0");
	res |= test_strtol_noconv ("--0");
	res |= test_strtol_noconv ("+-0");
	res |= test_strtol_noconv ("+ 0");
	res |= test_strtol_noconv ("- 0");

	{
		char buffer[4 * sizeof (long) + 2];

		sprintf (buffer, "-%lu", 1 + (unsigned long)LONG_MIN);
		res |= test_strtol_overflow (buffer, FALSE);
		sprintf (buffer, "-%lu", 10 + (unsigned long)LONG_MIN);
		res |= test_strtol_overflow (buffer, FALSE);

		sprintf (buffer, "%lu", 1 + (unsigned long)LONG_MAX);
		res |= test_strtol_overflow (buffer, TRUE);
		sprintf (buffer, "%lu", 10 + (unsigned long)LONG_MAX);
		res |= test_strtol_overflow (buffer, TRUE);
	}

	/* -------------------- */

	res |= test_strtod_ok ("0", 0, 1);
	res |= test_strtod_ok ("1", 1, 1);
	res |= test_strtod_ok ("-1", -1, 2);
	res |= test_strtod_ok ("+1", 1, 2);
	res |= test_strtod_ok (" +1", 1, 3);
	res |= test_strtod_ok ("\xc2\xa0+1", 1, 4);
	res |= test_strtod_ok ("\xc2\xa0+1x", 1, 4);
	res |= test_strtod_ok ("\xc2\xa0+1e", 1, 4);
	res |= test_strtod_ok ("\xc2\xa0+1e+", 1, 4);
	res |= test_strtod_ok ("\xc2\xa0+1e+0", 1, 7);
	res |= test_strtod_ok ("-1e1", -10, 4);
	res |= test_strtod_ok ("100e-2", 1, 6);
	res |= test_strtod_ok ("100e+2", 10000, 6);
	res |= test_strtod_ok ("1x0p0", 1, 1);
	res |= test_strtod_ok ("+inf", gnm_pinf, 4);
	res |= test_strtod_ok ("-inf", gnm_ninf, 4);
	res |= test_strtod_ok ("1.25", 1.25, 4);
	res |= test_strtod_ok ("1.25e1", 12.5, 6);
	res |= test_strtod_ok ("12.5e-1", 1.25, 7);

	g_printerr ("Result = %d\n", res);

	mark_test_end (test_name);
}

/* ------------------------------------------------------------------------- */

static char *random_summary = NULL;

static void
add_random_fail (const char *s)
{
	if (random_summary) {
		char *t = g_strconcat (random_summary, ", ", s, NULL);
		g_free (random_summary);
		random_summary = t;
	} else
		random_summary = g_strdup (s);
}

static void
define_cell (Sheet *sheet, int c, int r, const char *expr)
{
	GnmCell *cell = sheet_cell_fetch (sheet, c, r);
	sheet_cell_set_text (cell, expr, NULL);
}

#define GET_PROB(i_) ((i_) <= 0 ? 0 : ((i_) >= nf ? 1 : probs[(i_)]))

static gboolean
rand_fractile_test (gnm_float const *vals, int N, int nf,
		    gnm_float const *fractiles, gnm_float const *probs)
{
	gnm_float f = 1.0 / nf;
	int *fractilecount = g_new (int, nf + 1);
	int *expected = g_new (int, nf + 1);
	int i;
	gboolean ok = TRUE;
	gboolean debug = TRUE;

	if (debug) {
		g_printerr ("Bin upper limit:");
		for (i = 1; i <= nf; i++) {
			gnm_float U = (i == nf) ? gnm_pinf : fractiles[i];
			g_printerr ("%s%" GNM_FORMAT_g,
				    (i == 1) ? " " : ", ",
				    U);
		}
		g_printerr (".\n");
	}

	if (debug && probs) {
		g_printerr ("Cumulative probabilities:");
		for (i = 1; i <= nf; i++)
			g_printerr ("%s%.1" GNM_FORMAT_f "%%",
				    (i == 1) ? " " : ", ", 100 * GET_PROB (i));
		g_printerr (".\n");
	}

	for (i = 1; i < nf - 1; i++) {
		if (!(fractiles[i] <= fractiles[i + 1])) {
			g_printerr ("Severe fractile ordering problem.\n");
			return FALSE;
		}

		if (probs && !(probs[i] <= probs[i + 1])) {
			g_printerr ("Severe cumulative probabilities ordering problem.\n");
			return FALSE;
		}
	}
	if (probs && (probs[1] < 0 || probs[nf - 1] > 1)) {
		g_printerr ("Severe cumulative probabilities range problem.\n");
		return FALSE;
	}

	for (i = 0; i <= nf; i++)
		fractilecount[i] = 0;

	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		int j;
		for (j = 1; j < nf; j++)
			if (r <= fractiles[j])
				break;
		fractilecount[j]++;
	}
	g_printerr ("Fractile counts:");
	for (i = 1; i <= nf; i++)
		g_printerr ("%s%d", (i == 1) ? " " : ", ", fractilecount[i]);
	g_printerr (".\n");

	if (probs) {
		g_printerr ("Expected counts:");
		for (i = 1; i <= nf; i++) {
			gnm_float p = GET_PROB (i) - GET_PROB (i-1);
			expected[i] = gnm_round (p * N);
			g_printerr ("%s%d", (i == 1) ? " " : ", ", expected[i]);
		}
		g_printerr (".\n");
	} else {
		gnm_float T = f * N;
		g_printerr ("Expected count in each fractile: %.10" GNM_FORMAT_g "\n", T);
		for (i = 0; i <= nf; i++)
			expected[i] = T;
	}

	for (i = 1; i <= nf; i++) {
		gnm_float T = expected[i];
		if (!(gnm_abs (fractilecount[i] - T) <= 4 * gnm_sqrt (T))) {
			g_printerr ("Fractile test failure for bin %d.\n", i);
			ok = FALSE;
		}
	}

	g_free (fractilecount);
	g_free (expected);

	return ok;
}

#undef GET_PROB

static gnm_float *
test_random_1 (int N, const char *expr,
	       gnm_float *mean, gnm_float *var,
	       gnm_float *skew, gnm_float *kurt)
{
	Workbook *wb = workbook_new ();
	Sheet *sheet;
	gnm_float *res = g_new (gnm_float, N);
	int i;
	char *s;
	int cols = 2, rows = N;

	g_printerr ("Testing %s\n", expr);

	gnm_sheet_suggest_size (&cols, &rows);
	sheet = workbook_sheet_add (wb, -1, cols, rows);

	for (i = 0; i < N; i++)
		define_cell (sheet, 0, i, expr);

	s = g_strdup_printf ("=average(a1:a%d)", N);
	define_cell (sheet, 1, 0, s);
	g_free (s);

	s = g_strdup_printf ("=var(a1:a%d)", N);
	define_cell (sheet, 1, 1, s);
	g_free (s);

	s = g_strdup_printf ("=skew(a1:a%d)", N);
	define_cell (sheet, 1, 2, s);
	g_free (s);

	s = g_strdup_printf ("=kurt(a1:a%d)", N);
	define_cell (sheet, 1, 3, s);
	g_free (s);

	/* Force recalc of all dirty cells even in manual mode.  */
	workbook_recalc (sheet->workbook);

	for (i = 0; i < N; i++)
		res[i] = value_get_as_float (sheet_cell_get (sheet, 0, i)->value);
	*mean = value_get_as_float (sheet_cell_get (sheet, 1, 0)->value);
	g_printerr ("Mean: %.10" GNM_FORMAT_g "\n", *mean);

	*var = value_get_as_float (sheet_cell_get (sheet, 1, 1)->value);
	g_printerr ("Var: %.10" GNM_FORMAT_g "\n", *var);

	*skew = value_get_as_float (sheet_cell_get (sheet, 1, 2)->value);
	g_printerr ("Skew: %.10" GNM_FORMAT_g "\n", *skew);

	*kurt = value_get_as_float (sheet_cell_get (sheet, 1, 3)->value);
	g_printerr ("Kurt: %.10" GNM_FORMAT_g "\n", *kurt);

	g_object_unref (wb);
	return res;
}

static gnm_float *
test_random_normality (int N, const char *expr,
		       gnm_float *mean, gnm_float *var,
		       gnm_float *adtest, gnm_float *cvmtest,
		       gnm_float *lkstest, gnm_float *sftest)
{
	Workbook *wb = workbook_new ();
	Sheet *sheet;
	gnm_float *res = g_new (gnm_float, N);
	int i;
	char *s;
	int cols = 2, rows = N;

	g_printerr ("Testing %s\n", expr);

	gnm_sheet_suggest_size (&cols, &rows);
	sheet = workbook_sheet_add (wb, -1, cols, rows);

	for (i = 0; i < N; i++)
		define_cell (sheet, 0, i, expr);

	s = g_strdup_printf ("=average(a1:a%d)", N);
	define_cell (sheet, 1, 0, s);
	g_free (s);

	s = g_strdup_printf ("=var(a1:a%d)", N);
	define_cell (sheet, 1, 1, s);
	g_free (s);

	s = g_strdup_printf ("=adtest(a1:a%d)", N);
	define_cell (sheet, 1, 2, s);
	g_free (s);

	s = g_strdup_printf ("=cvmtest(a1:a%d)", N);
	define_cell (sheet, 1, 3, s);
	g_free (s);

	s = g_strdup_printf ("=lkstest(a1:a%d)", N);
	define_cell (sheet, 1, 4, s);
	g_free (s);

	s = g_strdup_printf ("=sftest(a1:a%d)", N > 5000 ? 5000 : N);
	define_cell (sheet, 1, 5, s);
	g_free (s);

	/* Force recalc of all dirty cells even in manual mode.  */
	workbook_recalc (sheet->workbook);

	for (i = 0; i < N; i++)
		res[i] = value_get_as_float (sheet_cell_get (sheet, 0, i)->value);
	*mean = value_get_as_float (sheet_cell_get (sheet, 1, 0)->value);
	g_printerr ("Mean: %.10" GNM_FORMAT_g "\n", *mean);

	*var = value_get_as_float (sheet_cell_get (sheet, 1, 1)->value);
	g_printerr ("Var: %.10" GNM_FORMAT_g "\n", *var);

	*adtest = value_get_as_float (sheet_cell_get (sheet, 1, 2)->value);
	g_printerr ("ADTest: %.10" GNM_FORMAT_g "\n", *adtest);

	*cvmtest = value_get_as_float (sheet_cell_get (sheet, 1, 3)->value);
	g_printerr ("CVMTest: %.10" GNM_FORMAT_g "\n", *cvmtest);

	*lkstest = value_get_as_float (sheet_cell_get (sheet, 1, 4)->value);
	g_printerr ("LKSTest: %.10" GNM_FORMAT_g "\n", *lkstest);

	*sftest = value_get_as_float (sheet_cell_get (sheet, 1, 5)->value);
	g_printerr ("SFTest: %.10" GNM_FORMAT_g "\n", *sftest);

	g_object_unref (wb);
	return res;
}

static void
test_random_rand (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float mean_target = 0.5;
	gnm_float var_target = 1.0 / 12;
	gnm_float skew_target = 0;
	gnm_float kurt_target = -6.0 / 5;
	gnm_float *vals;
	int i;
	gboolean ok;
	gnm_float T;
	gnm_float fractiles[10];
	const int nf = G_N_ELEMENTS (fractiles);

	vals = test_random_1 (N, "=RAND()", &mean, &var, &skew, &kurt);
	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && r < 1)) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure [%.1" GNM_FORMAT_f " stdev]\n", (mean - T) / gnm_sqrt (var_target / N));
		ok = FALSE;
	}
	T = var_target;
	if (gnm_abs (var - T) > GNM_const(0.01)) {
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}
	T = skew_target;
	if (gnm_abs (skew - T) > GNM_const(0.05)) {
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}
	T = kurt_target;
	if (gnm_abs (kurt - T) > GNM_const(0.05)) {
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++)
		fractiles[i] = i / (double)nf;
	if (!rand_fractile_test (vals, N, nf, fractiles, NULL))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	else
		add_random_fail ("RAND");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randuniform (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float lsign = (random_01 () > GNM_const(0.75) ? 1 : -1);
	gnm_float param_l = lsign * gnm_floor (1 / (GNM_const(0.0001) + gnm_pow (random_01 (), 4)));
	gnm_float param_h = param_l + gnm_floor (1 / (GNM_const(0.0001) + gnm_pow (random_01 () / 2, 4)));
	gnm_float n = param_h - param_l;
	gnm_float mean_target = (param_l + param_h) / 2;
	gnm_float var_target = (n * n) / 12;
	gnm_float skew_target = 0;
	gnm_float kurt_target = -6 / 5.0;
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDUNIFORM(%.10" GNM_FORMAT_g ",%.10" GNM_FORMAT_g ")", param_l, param_h);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= param_l && r < param_h)) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure [%.1" GNM_FORMAT_f " stdev]\n", (mean - T) / gnm_sqrt (var_target / N));
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++)
		fractiles[i] = param_l + n * i / (gnm_float)nf;
	if (!rand_fractile_test (vals, N, nf, fractiles, NULL))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	else
		add_random_fail ("RANDUNIFORM");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randbernoulli (int N)
{
	gnm_float p = 0.3;
	gnm_float q = 1 - p;
	gnm_float mean, var, skew, kurt;
	gnm_float mean_target = p;
	gnm_float var_target = p * (1 - p);
	gnm_float skew_target = (q - p) / gnm_sqrt (p * q);
	gnm_float kurt_target = (1 - 6 * p * q) / (p * q);
	gnm_float *vals;
	int i;
	gboolean ok;
	char *expr;
	gnm_float T;

	expr = g_strdup_printf ("=RANDBERNOULLI(%.10" GNM_FORMAT_g ")", p);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r == 0 || r == 1)) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (gnm_abs (mean - p) > GNM_const(0.01)) {
		g_printerr ("Mean failure [%.1" GNM_FORMAT_f " stdev]\n", (mean - T) / gnm_sqrt (var_target / N));
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (gnm_abs (var - T) > GNM_const(0.01)) {
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (skew - T) <= GNM_const(0.10) * gnm_abs (T))) {
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (kurt - T) <= GNM_const(0.15) * gnm_abs (T))) {
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}
	if (ok)
		g_printerr ("OK\n");
	else
		add_random_fail ("RANDBERNOULLI");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randdiscrete (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	int i;
	gboolean ok;
	gnm_float mean_target = 13;
	gnm_float var_target = 156;
	gnm_float skew_target = 0.6748;
	gnm_float kurt_target = -0.9057;
	char *expr;
	gnm_float T;

	expr = g_strdup_printf ("=RANDDISCRETE({0;1;4;9;16;25;36})");
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && r <= 36 && gnm_sqrt (r) == gnm_floor (gnm_sqrt (r)))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure [%.1" GNM_FORMAT_f " stdev]\n", (mean - T) / gnm_sqrt (var_target / N));
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	if (ok)
		g_printerr ("OK\n");
	else
		add_random_fail ("RANDDISCRETE");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randnorm (int N)
{
	gnm_float mean, var, adtest, cvmtest, lkstest, sftest;
	gnm_float mean_target = 0, var_target = 1;
	gnm_float *vals;
	gboolean ok;
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDNORM(%.10" GNM_FORMAT_g ",%.10" GNM_FORMAT_g ")",
				mean_target, var_target);
	vals = test_random_normality (N, expr, &mean, &var, &adtest, &cvmtest, &lkstest, &sftest);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!gnm_finite (r)) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	if (gnm_abs (mean - T) > GNM_const(0.02)) {
		g_printerr ("Mean failure [%.1" GNM_FORMAT_f " stdev]\n", (mean - T) / gnm_sqrt (var_target / N));
		ok = FALSE;
	}
	T = var_target;
	if (gnm_abs (var - T) > GNM_const(0.02)) {
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++)
		fractiles[i] = qnorm (i / (double)nf, mean_target, gnm_sqrt (var_target), TRUE, FALSE);
	if (!rand_fractile_test (vals, N, nf, fractiles, NULL))
		ok = FALSE;

	if (adtest < GNM_const(0.01)) {
		g_printerr ("Anderson Darling Test rejected [%.10" GNM_FORMAT_g "]\n", adtest);
		ok = FALSE;
	}
	if (cvmtest < GNM_const(0.01)) {
		g_printerr ("Cramér-von Mises Test rejected [%.10" GNM_FORMAT_g "]\n", cvmtest);
		ok = FALSE;
	}
	if (lkstest < GNM_const(0.01)) {
		g_printerr ("Lilliefors (Kolmogorov-Smirnov) Test rejected [%.10" GNM_FORMAT_g "]\n",
			    lkstest);
		ok = FALSE;
	}
	if (sftest < GNM_const(0.01)) {
		g_printerr ("Shapiro-Francia Test rejected [%.10" GNM_FORMAT_g "]\n", sftest);
		ok = FALSE;
	}

	if (ok)
		g_printerr ("OK\n");
	else
		add_random_fail ("RANDNORM");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randsnorm (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float alpha = 5;
	gnm_float delta = alpha/gnm_sqrt(1+alpha*alpha);
	gnm_float mean_target = delta * gnm_sqrt (2/M_PIgnum);
	gnm_float var_target = 1-mean_target*mean_target;
	char *expr;
	gnm_float T;
	int i;

	expr = g_strdup_printf ("=RANDSNORM(%.10" GNM_FORMAT_g ")", alpha);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!gnm_finite (r)) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (gnm_abs (mean - T) > GNM_const(0.01)) {
		g_printerr ("Mean failure [%.1" GNM_FORMAT_f " stdev]\n", (mean - T) / gnm_sqrt (var_target / N));
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (gnm_abs (var - T) > GNM_const(0.01)) {
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = mean_target/gnm_sqrt(var_target);
	T = T*T*T*(4-M_PIgnum)/2;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (gnm_abs (skew - T) > GNM_const(0.05)) {
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = 2*(M_PIgnum - 3)*mean_target*mean_target*mean_target*mean_target/(var_target*var_target);
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (gnm_abs (kurt - T) > GNM_const(0.15)) {
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	if (ok)
		g_printerr ("OK\n");
	else
		add_random_fail ("RANDSNORM");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randexp (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float param_l = 1 / (GNM_const(0.0001) + gnm_pow (random_01 () / 2, 4));
	gnm_float mean_target = param_l;
	gnm_float var_target = mean_target * mean_target;
	gnm_float skew_target = 2;
	gnm_float kurt_target = 6;
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDEXP(%.10" GNM_FORMAT_g ")", param_l);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && gnm_finite (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure [%.1" GNM_FORMAT_f " stdev]\n", (mean - T) / gnm_sqrt (var_target / N));
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++)
		fractiles[i] = qexp (i / (double)nf, param_l, TRUE, FALSE);
	if (!rand_fractile_test (vals, N, nf, fractiles, NULL))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	else
		add_random_fail ("RANDEXP");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randgamma (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float param_shape = gnm_floor (1 / (GNM_const(0.0001) + gnm_pow (random_01 (), 6)));
	gnm_float param_scale = GNM_const(0.001) + gnm_pow (random_01 (), 4) * 1000;
	gnm_float mean_target = param_shape * param_scale;
	gnm_float var_target = mean_target * param_scale;
	gnm_float skew_target = 2 / gnm_sqrt (param_shape);
	gnm_float kurt_target = 6 / param_shape;
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDGAMMA(%.0" GNM_FORMAT_f ",%.10" GNM_FORMAT_g ")", param_shape, param_scale);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r > 0 && gnm_finite (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure [%.1" GNM_FORMAT_f " stdev]\n", (mean - T) / gnm_sqrt (var_target / N));
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++)
		fractiles[i] = qgamma (i / (double)nf, param_shape, param_scale, TRUE, FALSE);
	if (!rand_fractile_test (vals, N, nf, fractiles, NULL))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	else
		add_random_fail ("RANDGAMMA");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randbeta (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float param_a = 1 / (GNM_const(0.0001) + gnm_pow (random_01 (), 6));
	gnm_float param_b = 1 / (GNM_const(0.0001) + gnm_pow (random_01 (), 6));
	gnm_float s = param_a + param_b;
	gnm_float mean_target = param_a / s;
	gnm_float var_target = mean_target * param_b / (s * (s + 1));
	gnm_float skew_target =
		(2 * (param_b - param_a) * gnm_sqrt (s + 1))/
		((s + 2) * gnm_sqrt (param_a * param_b));
	gnm_float kurt_target = gnm_nan; /* Complicated */
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDBETA(%.10" GNM_FORMAT_g ",%.10" GNM_FORMAT_g ")", param_a, param_b);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && r <= 1)) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure [%.1" GNM_FORMAT_f " stdev]\n", (mean - T) / gnm_sqrt (var_target / N));
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++)
		fractiles[i] = qbeta (i / (double)nf, param_a, param_b, TRUE, FALSE);
	if (!rand_fractile_test (vals, N, nf, fractiles, NULL))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	else
		add_random_fail ("RANDBETA");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randtdist (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float param_df = 1 + gnm_floor (1 / (GNM_const(0.01) + gnm_pow (random_01 (), 6)));
	gnm_float mean_target = 0;
	gnm_float var_target = param_df > 2 ? param_df / (param_df - 2) : gnm_nan;
	gnm_float skew_target = param_df > 3 ? 0 : gnm_nan;
	gnm_float kurt_target = param_df > 4 ? 6 / (param_df - 4) : gnm_nan;
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDTDIST(%.0" GNM_FORMAT_f ")", param_df);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(gnm_finite (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (gnm_finite (var_target) && !(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure [%.1" GNM_FORMAT_f " stdev]\n", (mean - T) / gnm_sqrt (var_target / N));
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++)
		fractiles[i] = qt (i / (double)nf, param_df, TRUE, FALSE);
	if (!rand_fractile_test (vals, N, nf, fractiles, NULL))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	else
		add_random_fail ("RANDTDIST");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randfdist (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float param_df1 = 1 + gnm_floor (1 / (GNM_const(0.01) + gnm_pow (random_01 (), 6)));
	gnm_float param_df2 = 1 + gnm_floor (1 / (GNM_const(0.01) + gnm_pow (random_01 (), 6)));
	gnm_float mean_target = param_df2 > 2 ? param_df2 / (param_df2 - 2) : gnm_nan;
	gnm_float var_target = param_df2 > 4
		? (2 * param_df2 * param_df2 * (param_df1 + param_df2 - 2) /
		   (param_df1 * (param_df2 - 2) * (param_df2 - 2) * (param_df2 - 4)))
		: gnm_nan;
	gnm_float skew_target = gnm_nan; /* Complicated */
	gnm_float kurt_target = gnm_nan; /* Complicated */
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDFDIST(%.0" GNM_FORMAT_f ",%.0" GNM_FORMAT_f ")", param_df1, param_df2);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && gnm_finite (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (gnm_finite (var_target) && !(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure [%.1" GNM_FORMAT_f " stdev]\n", (mean - T) / gnm_sqrt (var_target / N));
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++)
		fractiles[i] = qf (i / (double)nf, param_df1, param_df2, TRUE, FALSE);
	if (!rand_fractile_test (vals, N, nf, fractiles, NULL))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	else
		add_random_fail ("RANDFDIST");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randchisq (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float param_df = 1 + gnm_floor (1 / (GNM_const(0.01) + gnm_pow (random_01 (), 6)));
	gnm_float mean_target = param_df;
	gnm_float var_target = param_df * 2;
	gnm_float skew_target = gnm_sqrt (8 / param_df);
	gnm_float kurt_target = 12 / param_df;
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDCHISQ(%.10" GNM_FORMAT_g ")", param_df);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && gnm_finite (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (gnm_finite (var_target) && !(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure [%.1" GNM_FORMAT_f " stdev]\n", (mean - T) / gnm_sqrt (var_target / N));
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++)
		fractiles[i] = qchisq (i / (double)nf, param_df, TRUE, FALSE);
	if (!rand_fractile_test (vals, N, nf, fractiles, NULL))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	else
		add_random_fail ("RANDCHISQ");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randcauchy (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float param_scale = GNM_const(0.001) + gnm_pow (random_01 (), 4) * 1000;
	gnm_float mean_target = gnm_nan;
	gnm_float var_target = gnm_nan;
	gnm_float skew_target = gnm_nan;
	gnm_float kurt_target = gnm_nan;
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10];
	const int nf = G_N_ELEMENTS (fractiles);

	/*
	 * The distribution has no mean, no variance, no skew, and no kurtosis.
	 * The support is all reals.
	 */

	expr = g_strdup_printf ("=RANDCAUCHY(%.10" GNM_FORMAT_g ")", param_scale);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(gnm_finite (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (gnm_finite (var_target) && !(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure [%.1" GNM_FORMAT_f " stdev]\n", (mean - T) / gnm_sqrt (var_target / N));
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++)
		fractiles[i] = qcauchy (i / (double)nf, 0.0, param_scale, TRUE, FALSE);
	if (!rand_fractile_test (vals, N, nf, fractiles, NULL))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	else
		add_random_fail ("RANDCAUCHY");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randbinom (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float param_p = random_01 ();
	gnm_float param_trials = gnm_floor (1 / (GNM_const(0.0001) + gnm_pow (random_01 (), 4)));
	gnm_float mean_target = param_trials * param_p;
	gnm_float var_target = mean_target * (1 - param_p);
	gnm_float skew_target = (1 - 2 * param_p) / gnm_sqrt (var_target);
	gnm_float kurt_target = (1 - 6 * param_p * (1 - param_p)) / var_target;
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10], probs[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDBINOM(%.10" GNM_FORMAT_g ",%.0" GNM_FORMAT_f ")", param_p, param_trials);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && r <= param_trials && r == gnm_floor (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure [%.1" GNM_FORMAT_f " stdev]\n", (mean - T) / gnm_sqrt (var_target / N));
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++) {
		fractiles[i] = qbinom (i / (double)nf, param_trials, param_p, TRUE, FALSE);
		probs[i] = pbinom (fractiles[i], param_trials, param_p, TRUE, FALSE);
	}
	if (!rand_fractile_test (vals, N, nf, fractiles, probs))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	else
		add_random_fail ("RANDBINOM");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randnegbinom (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float param_p = random_01 ();
	gnm_float param_fails = gnm_floor (1 / (GNM_const(0.0001) + gnm_pow (random_01 (), 4)));
	/* Warning: these differ from Wikipedia by swapping p and 1-p.  */
	gnm_float mean_target = param_fails * (1 - param_p) / param_p;
	gnm_float var_target = mean_target / param_p;
	gnm_float skew_target = (2 - param_p) / gnm_sqrt (param_fails * (1 - param_p));
	gnm_float kurt_target = 6 / param_fails + 1 / var_target;
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10], probs[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDNEGBINOM(%.10" GNM_FORMAT_g ",%.0" GNM_FORMAT_f ")", param_p, param_fails);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && gnm_finite (r) && r == gnm_floor (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure [%.1" GNM_FORMAT_f " stdev]\n", (mean - T) / gnm_sqrt (var_target / N));
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++) {
		fractiles[i] = qnbinom (i / (double)nf, param_fails, param_p, TRUE, FALSE);
		probs[i] = pnbinom (fractiles[i], param_fails, param_p, TRUE, FALSE);
	}
	if (!rand_fractile_test (vals, N, nf, fractiles, probs))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	else
		add_random_fail ("RANDNEGBINOM");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randhyperg (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float param_nr = gnm_floor (1 / (GNM_const(0.01) + gnm_pow (random_01 (), 4)));
	gnm_float param_nb = gnm_floor (1 / (GNM_const(0.01) + gnm_pow (random_01 (), 4)));
	gnm_float s = param_nr + param_nb;
	gnm_float param_n = gnm_floor (random_01 () * (s + 1));
	gnm_float mean_target = param_n * param_nr / s;
	gnm_float var_target = s > 1
		? mean_target * (param_nb / s) * (s - param_n) / (s - 1)
		: 0;
	gnm_float skew_target = gnm_nan; /* Complicated */
	gnm_float kurt_target = gnm_nan; /* Complicated */
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10], probs[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDHYPERG(%.10" GNM_FORMAT_g ",%.0" GNM_FORMAT_f ",%.0" GNM_FORMAT_f ")", param_nr, param_nb, param_n);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && r <= param_n && r == gnm_floor (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (gnm_finite (var_target) &&
	    !(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure [%.1" GNM_FORMAT_f " stdev]\n", (mean - T) / gnm_sqrt (var_target / N));
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++) {
		fractiles[i] = qhyper (i / (double)nf, param_nr, param_nb, param_n, TRUE, FALSE);
		probs[i] = phyper (fractiles[i], param_nr, param_nb, param_n, TRUE, FALSE);
	}
	if (!rand_fractile_test (vals, N, nf, fractiles, probs))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	else
		add_random_fail ("RANDHYPERG");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randbetween (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float lsign = (random_01 () > GNM_const(0.75) ? 1 : -1);
	gnm_float param_l = lsign * gnm_floor (1 / (GNM_const(0.0001) + gnm_pow (random_01 (), 4)));
	gnm_float param_h = param_l + gnm_floor (1 / (GNM_const(0.0001) + gnm_pow (random_01 () / 2, 4)));
	gnm_float n = param_h - param_l + 1;
	gnm_float mean_target = (param_l + param_h) / 2;
	gnm_float var_target = (n * n - 1) / 12;
	gnm_float skew_target = 0;
	gnm_float kurt_target = (n * n + 1) / (n * n - 1) * -6 / 5;
	char *expr;
	gnm_float T;
	int i;

	expr = g_strdup_printf ("=RANDBETWEEN(%.0" GNM_FORMAT_f ",%.0" GNM_FORMAT_f ")", param_l, param_h);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= param_l && r <= param_h && r == gnm_floor (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure [%.1" GNM_FORMAT_f " stdev]\n", (mean - T) / gnm_sqrt (var_target / N));
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	if (ok)
		g_printerr ("OK\n");
	else
		add_random_fail ("RANDBETWEEN");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randpoisson (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float param_l = 1 / (GNM_const(0.0001) + gnm_pow (random_01 () / 2, 4));
	gnm_float mean_target = param_l;
	gnm_float var_target = param_l;
	gnm_float skew_target = 1 / gnm_sqrt (param_l);
	gnm_float kurt_target = 1 / param_l;
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10], probs[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDPOISSON(%.10" GNM_FORMAT_g ")", param_l);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && gnm_finite (r) && r == gnm_floor (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure [%.1" GNM_FORMAT_f " stdev]\n", (mean - T) / gnm_sqrt (var_target / N));
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++) {
		fractiles[i] = qpois (i / (double)nf, param_l, TRUE, FALSE);
		probs[i] = ppois (fractiles[i], param_l, TRUE, FALSE);
	}
	if (!rand_fractile_test (vals, N, nf, fractiles, probs))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	else
		add_random_fail ("RANDPOISSON");
	g_printerr ("\n");

	g_free (vals);
}

/*
 * Note: this geometric distribution is the only with support {0,1,2,...}
 */
static void
test_random_randgeom (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float param_p = random_01 ();
	gnm_float mean_target = (1 - param_p) / param_p;
	gnm_float var_target = (1 - param_p) / (param_p * param_p);
	gnm_float skew_target = (2 - param_p) / gnm_sqrt (1 - param_p);
	gnm_float kurt_target = 6 + (param_p * param_p) / (1 - param_p);
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10], probs[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDGEOM(%.10" GNM_FORMAT_g ")", param_p);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && gnm_finite (r) && r == gnm_floor (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure [%.1" GNM_FORMAT_f " stdev]\n", (mean - T) / gnm_sqrt (var_target / N));
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++) {
		fractiles[i] = qgeom (i / (double)nf, param_p, TRUE, FALSE);
		probs[i] = pgeom (fractiles[i], param_p, TRUE, FALSE);
	}
	if (!rand_fractile_test (vals, N, nf, fractiles, probs))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	else
		add_random_fail ("RANDGEOM");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randlog (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float param_p = random_01 ();
	gnm_float p = param_p;
	gnm_float l1mp = gnm_log1p (-p);
	gnm_float mean_target = -p / (1 - p) / l1mp;
	gnm_float var_target = -(p * (p + l1mp)) / gnm_pow ((1 - p) * l1mp, 2);
	/* See http://mathworld.wolfram.com/Log-SeriesDistribution.html */
	gnm_float skew_target =
		-l1mp *
		(2 * p * p + 3 * p * l1mp + (1 + p) * l1mp * l1mp) /
		(l1mp * (p + l1mp) * gnm_sqrt (-p * (p + l1mp)));
	gnm_float kurt_target =
		-(6 * p * p * p +
		  12 * p * p * l1mp +
		  p * (7 + 4 * p) * l1mp * l1mp +
		  (1 + 4 * p + p * p) * l1mp * l1mp * l1mp) /
		(p * gnm_pow (p + l1mp, 2));
	char *expr;
	gnm_float T;
	int i;

	expr = g_strdup_printf ("=RANDLOG(%.10" GNM_FORMAT_g ")", param_p);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 1 && gnm_finite (r) && r == gnm_floor (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure [%.1" GNM_FORMAT_f " stdev]\n", (mean - T) / gnm_sqrt (var_target / N));
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	if (ok)
		g_printerr ("OK\n");
	else
		add_random_fail ("RANDLOG");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randweibull (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float shape = 1 / (GNM_const(0.0001) + gnm_pow (random_01 () / 2, 2));
	gnm_float scale = 2 * random_01 ();
	gnm_float mean_target = scale * gnm_gamma (1 + 1 / shape);
	gnm_float var_target = scale * scale  *
		(gnm_gamma (1 + 2 / shape) -
		 gnm_pow (gnm_gamma (1 + 1 / shape), 2));
	/* See https://en.wikipedia.org/wiki/Weibull_distribution */
	gnm_float skew_target =
		(gnm_gamma (1 + 3 / shape) * gnm_pow (scale, 3) -
		 3 * mean_target * var_target -
		 gnm_pow (mean_target, 3)) /
		gnm_pow (var_target, 1.5);
	gnm_float kurt_target = gnm_nan; /* Complicated */
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDWEIBULL(%.10" GNM_FORMAT_f ",%.10" GNM_FORMAT_f ")", scale, shape);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && gnm_finite (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure [%.1" GNM_FORMAT_f " stdev]\n", (mean - T) / gnm_sqrt (var_target / N));
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++)
		fractiles[i] = qweibull (i / (double)nf, shape, scale, TRUE, FALSE);
	if (!rand_fractile_test (vals, N, nf, fractiles, NULL))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	else
		add_random_fail ("RANDWEIBULL");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randlognorm (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float lm = (random_01() - GNM_const(0.5)) / (GNM_const(0.1) + gnm_pow (random_01 () / 2, 2));
	gnm_float ls = 1 / (1 + gnm_pow (random_01 () / 2, 2));
	gnm_float mean_target = gnm_exp (lm + ls * ls / 2);
	gnm_float var_target = gnm_expm1 (ls * ls) * (mean_target * mean_target);
	/* See https://en.wikipedia.org/wiki/Log-normal_distribution */
	gnm_float skew_target = (gnm_exp (ls * ls) + 2) *
		gnm_sqrt (gnm_expm1 (ls * ls));
	gnm_float kurt_target = gnm_nan; /* Complicated */
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDLOGNORM(%.10" GNM_FORMAT_f ",%.10" GNM_FORMAT_f ")", lm, ls);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && r <= gnm_pinf)) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure [%.1" GNM_FORMAT_f " stdev]\n", (mean - T) / gnm_sqrt (var_target / N));
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++)
		fractiles[i] = qlnorm (i / (double)nf, lm, ls, TRUE, FALSE);
	if (!rand_fractile_test (vals, N, nf, fractiles, NULL))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	else
		add_random_fail ("RANDLOGNORM");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random_randrayleigh (int N)
{
	gnm_float mean, var, skew, kurt;
	gnm_float *vals;
	gboolean ok;
	gnm_float ls = 1 / (1 + gnm_pow (random_01 () / 2, 2));
	gnm_float mean_target = ls * gnm_sqrt (M_PIgnum / 2);
	gnm_float var_target = (4 - M_PIgnum) / 2 * ls * ls;
	gnm_float skew_target = 2 * gnm_sqrt (M_PIgnum) * (M_PIgnum - 3) /
		gnm_pow (4 - M_PIgnum, 1.5);
	gnm_float kurt_target = gnm_nan; /* Complicated */
	char *expr;
	gnm_float T;
	int i;
	gnm_float fractiles[10];
	const int nf = G_N_ELEMENTS (fractiles);

	expr = g_strdup_printf ("=RANDRAYLEIGH(%.10" GNM_FORMAT_f ")", ls);
	vals = test_random_1 (N, expr, &mean, &var, &skew, &kurt);
	g_free (expr);

	ok = TRUE;
	for (i = 0; i < N; i++) {
		gnm_float r = vals[i];
		if (!(r >= 0 && gnm_finite (r))) {
			g_printerr ("Range failure.\n");
			ok = FALSE;
			break;
		}
	}

	T = mean_target;
	g_printerr ("Expected mean: %.10" GNM_FORMAT_g "\n", T);
	if (!(gnm_abs (mean - T) <= 3 * gnm_sqrt (var_target / N))) {
		g_printerr ("Mean failure [%.1" GNM_FORMAT_f " stdev]\n", (mean - T) / gnm_sqrt (var_target / N));
		ok = FALSE;
	}

	T = var_target;
	g_printerr ("Expected var: %.10" GNM_FORMAT_g "\n", T);
	if (!(var >= 0 && gnm_finite (var))) {
		/* That is a very simplistic test! */
		g_printerr ("Var failure.\n");
		ok = FALSE;
	}

	T = skew_target;
	g_printerr ("Expected skew: %.10" GNM_FORMAT_g "\n", T);
	if (!gnm_finite (skew)) {
		/* That is a very simplistic test! */
		g_printerr ("Skew failure.\n");
		ok = FALSE;
	}

	T = kurt_target;
	g_printerr ("Expected kurt: %.10" GNM_FORMAT_g "\n", T);
	if (!(kurt >= -3 && gnm_finite (kurt))) {
		/* That is a very simplistic test! */
		g_printerr ("Kurt failure.\n");
		ok = FALSE;
	}

	/* Fractile test */
	for (i = 1; i < nf; i++)
		fractiles[i] = qrayleigh (i / (double)nf, ls, TRUE, FALSE);
	if (!rand_fractile_test (vals, N, nf, fractiles, NULL))
		ok = FALSE;

	if (ok)
		g_printerr ("OK\n");
	else
		add_random_fail ("RANDRAYLEIGH");
	g_printerr ("\n");

	g_free (vals);
}

static void
test_random (void)
{
	const char *test_name = "test_random";
	const int N = sstest_fast ? 2000 : 20000;
	const int High_N = N * 10;
	const char *single = g_getenv ("SSTEST_RANDOM");

	mark_test_start (test_name);

#define CHECK1(NAME,C) \
	do { if (!single || strcmp(single,#NAME) == 0) test_random_ ## NAME (C); } while (0)

	/* Continuous */
	CHECK1 (rand, N);
	CHECK1 (randuniform, N);
	CHECK1 (randbeta, N);
	CHECK1 (randcauchy, N);
	CHECK1 (randchisq, N);
	CHECK1 (randexp, N);
	CHECK1 (randfdist, N);
	CHECK1 (randgamma, N);
	CHECK1 (randlog, N);
	CHECK1 (randlognorm, N);
	CHECK1 (randnorm, High_N);
	CHECK1 (randsnorm, High_N);
	CHECK1 (randtdist, N);
	CHECK1 (randweibull, N);
	CHECK1 (randrayleigh, N);
#if 0
	CHECK1 (randexppow, N);
	CHECK1 (randgumbel, N);
	CHECK1 (randlandau, N);
	CHECK1 (randlaplace, N);
	CHECK1 (randlevy, N);
	CHECK1 (randlogistic, N);
	CHECK1 (randnormtail, N);
	CHECK1 (randpareto, N);
	CHECK1 (randrayleightail, N);
	CHECK1 (randstdist, N);
#endif

	/* Discrete */
	CHECK1 (randbernoulli, N);
	CHECK1 (randbetween, N);
	CHECK1 (randbinom, N);
	CHECK1 (randdiscrete, N);
	CHECK1 (randgeom, High_N);
	CHECK1 (randhyperg, High_N);
	CHECK1 (randnegbinom, High_N);
	CHECK1 (randpoisson, High_N);

#undef CHECK1

	if (!single) {
		if (random_summary)
			g_printerr ("SUMMARY: FAIL for %s\n\n", random_summary);
		else
			g_printerr ("SUMMARY: OK\n\n");
	}
	g_free (random_summary);
	random_summary = NULL;

	mark_test_end (test_name);
}

static gboolean
almost_eq (gnm_float a, gnm_float b, gnm_float tol)
{
	gnm_float ad = gnm_abs (a - b);
	if (ad == 0)
		return TRUE;
	return ad < MAX (gnm_abs (a), gnm_abs (b)) * tol;
}

#define BARF(what) do { \
	g_printerr("Trouble in %s: %s\n", dist->str, what);	\
	g_printerr("  x=%.12" GNM_FORMAT_g "\n", x);\
	g_printerr("  prev_d=%.12" GNM_FORMAT_g ", prev_p=%.12" GNM_FORMAT_g "\n", prev_d, prev_p); \
	g_printerr("  d=%.12" GNM_FORMAT_g ", p=%.12" GNM_FORMAT_g "\n", d, p); \
} while (0)

#define CHECK_DPQ_DISCRETE() do {					\
	if (i < LEFT || i > RIGHT ? !(d == 0) : !(d >= 0))		\
		BARF("d not non-negative");				\
	if (i < LEFT ? !(p == 0) : (i >= RIGHT ? !(p == 1) : !(p >= prev_p))) \
		BARF("p not increasing from 0");			\
	if (i >= RIGHT ? !(p == 1) : !(p <= 1))				\
		BARF("p not increasing to 1");				\
	if (!almost_eq (prev_p + d, p, tol))				\
		BARF("p does not cummulate");				\
} while(0)

static void
test_dpq_binom (void)
{
	gnm_float param_p = random_01 ();
	gnm_float param_trials = gnm_floor (1 / (GNM_const(0.0001) + gnm_pow (random_01 (), 4)));
	int i;
	gnm_float prev_p = 0;
	gnm_float prev_d = 0;
	GString *dist;
	gnm_float tol = GNM_EPSILON * 1000;
	gnm_float LEFT, RIGHT;

	dist = g_string_new ("binom(");
	go_dtoa (dist, "!^" GNM_FORMAT_g, param_p);
	g_string_append_c (dist, ',');
	go_dtoa (dist, "!^" GNM_FORMAT_g, param_trials);
	g_string_append_c (dist, ')');

	LEFT = 0;
	RIGHT = param_trials;

	for (i = LEFT - 1; i <= RIGHT + 1; i++) {
		gnm_float x = i;
		gnm_float d = dbinom (x, param_trials, param_p, FALSE);
		gnm_float p = pbinom (x, param_trials, param_p, TRUE, FALSE);

		CHECK_DPQ_DISCRETE ();

		prev_d = d;
		prev_p = p;
	}

	g_string_free (dist, TRUE);
}

static void
test_dpq_geom (void)
{
	gnm_float param_p = random_01 ();
	int i;
	gnm_float prev_p = 0;
	gnm_float prev_d = 0;
	GString *dist;
	gnm_float tol = GNM_EPSILON * 10;
	gnm_float LEFT, RIGHT;

	dist = g_string_new ("geom(");
	go_dtoa (dist, "!^" GNM_FORMAT_g, param_p);
	g_string_append_c (dist, ')');

	LEFT = 0;
	RIGHT = gnm_pinf;

	for (i = LEFT - 1; i <= 100 / param_p; i++) {
		gnm_float x = i;
		gnm_float d = dgeom (x, param_p, FALSE);
		gnm_float p = pgeom (x, param_p, TRUE, FALSE);

		CHECK_DPQ_DISCRETE ();

		prev_d = d;
		prev_p = p;
	}

	g_string_free (dist, TRUE);
}

static void
test_dpq_hypergeom (void)
{
	gnm_float param_nr = gnm_floor (1 / (GNM_const(0.01) + gnm_pow (random_01 (), 4)));
	gnm_float param_nb = gnm_floor (1 / (GNM_const(0.01) + gnm_pow (random_01 (), 4)));
	gnm_float param_n = gnm_random_uniform_int (param_nr + param_nb + 1);
	int i;
	gnm_float prev_p = 0;
	gnm_float prev_d = 0;
	GString *dist;
	gnm_float tol = GNM_EPSILON * 1000;
	gnm_float LEFT, RIGHT;

	dist = g_string_new ("hyper(");
	go_dtoa (dist, "!^" GNM_FORMAT_g, param_nr);
	g_string_append_c (dist, ',');
	go_dtoa (dist, "!^" GNM_FORMAT_g, param_nb);
	g_string_append_c (dist, ',');
	go_dtoa (dist, "!^" GNM_FORMAT_g, param_n);
	g_string_append_c (dist, ')');

	LEFT = 0;
	RIGHT = param_n;

	for (i = LEFT - 1; i <= RIGHT + 1; i++) {
		gnm_float x = i;
		gnm_float d = dhyper (x, param_nr, param_nb, param_n, FALSE);
		gnm_float p = phyper (x, param_nr, param_nb, param_n, TRUE, FALSE);

		CHECK_DPQ_DISCRETE ();

		prev_d = d;
		prev_p = p;
	}

	g_string_free (dist, TRUE);
}


static void
test_dpq_poisson (void)
{
	gnm_float param_l = 1 / (GNM_const(0.0001) + gnm_pow (random_01 () / 2, 4));
	int i;
	gnm_float prev_p = 0;
	gnm_float prev_d = 0;
	GString *dist;
	gnm_float tol = GNM_EPSILON * 1000;
	gnm_float LEFT, RIGHT;

	dist = g_string_new ("pois(");
	go_dtoa (dist, "!^" GNM_FORMAT_g, param_l);
	g_string_append_c (dist, ')');

	LEFT = 0;
	RIGHT = gnm_pinf;

	for (i = LEFT - 1; i <= 5 * param_l; i++) {
		gnm_float x = i;
		gnm_float d = dpois (x, param_l, FALSE);
		gnm_float p = ppois (x, param_l, TRUE, FALSE);

		CHECK_DPQ_DISCRETE ();

		prev_d = d;
		prev_p = p;
	}

	g_string_free (dist, TRUE);
}



static void
test_dpq (void)
{
	const char *test_name = "test_random";
	//const int N = sstest_fast ? 2000 : 20000;
	const char *single = g_getenv ("SSTEST_DPQ");

	mark_test_start (test_name);

#define CHECK1(NAME) \
	do { if (!single || strcmp(single,#NAME) == 0) test_dpq_ ## NAME (); } while (0)

	// Discrete
	CHECK1 (binom);
	CHECK1 (geom);
	CHECK1 (hypergeom);
	CHECK1 (poisson);

	mark_test_end (test_name);
}


static GPtrArray *
get_cell_values (GPtrArray *cells)
{
	GPtrArray *values = g_ptr_array_new_with_free_func ((GDestroyNotify)value_release);
	unsigned ui;
	for (ui = 0; ui < cells->len; ui++) {
		GnmCell *cell = g_ptr_array_index (cells, ui);
		g_ptr_array_add (values, value_dup (cell->value));
	}
	return values;
}

static void
test_recalc (GOCmdContext *cc, const char *url)
{
	GOIOContext *io_context = go_io_context_new (cc);
	WorkbookView *wbv = workbook_view_new_from_uri (url, NULL, io_context, NULL);
	Workbook *wb = wb_view_get_workbook (wbv);
	GPtrArray *cells, *base_values;
	unsigned ui;

	workbook_recalc_all (wb);

	cells = g_ptr_array_new ();
	WORKBOOK_FOREACH_SHEET (wb, sheet, {
		GPtrArray *scells = sheet_cells (sheet, NULL);	
		unsigned ui;
		for (ui = 0; ui < scells->len; ui++) {
			GnmCell *cell = g_ptr_array_index (scells, ui);
			g_ptr_array_add (cells, cell);
			if (gnm_cell_has_expr (cell) &&
			    gnm_expr_top_is_volatile (cell->base.texpr))
				g_printerr ("NOTE: %s!%s is volatile.\n",
					    cell->base.sheet->name_unquoted,
					    cell_name (cell));
		}
		g_ptr_array_free (scells, TRUE);
		});
	base_values = get_cell_values (cells);

	g_printerr ("Changing the contents of %d cells, one at a time...\n", cells->len);

	for (ui = 0; ui < cells->len; ui++) {
		GnmCell *cell = g_ptr_array_index (cells, ui);
		char *old = NULL;
		GPtrArray *values;
		unsigned ui2;

		if (gnm_cell_is_array (cell)) {
			// Bail for now
			continue;
		}

		if (gnm_cell_has_expr (cell)) {
			old = gnm_cell_get_entered_text (cell);
			sheet_cell_set_text (cell, "123", NULL);
		} else {
			sheet_cell_set_text (cell, "=2+2", NULL);
		}
		// Forcibly recalc the whole book
		workbook_recalc_all (wb);

		if (old) {
			sheet_cell_set_text (cell, old, NULL);
			g_free (old);
		} else {
			sheet_cell_set_value (cell, value_dup (g_ptr_array_index (base_values, ui)));
		}
		workbook_recalc (wb);

		values = get_cell_values (cells);
		for (ui2 = 0; ui2 < cells->len; ui2++) {
			GnmCell const *cell2 = g_ptr_array_index (cells, ui2);
			GnmValue const *val1 = g_ptr_array_index (base_values, ui2);
			GnmValue const *val2 = g_ptr_array_index (values, ui2);
			if (value_equal (val1, val2))
				continue;

			g_printerr ("When changing %s!%s:\n",
				    cell->base.sheet->name_unquoted, cell_name (cell));
			g_printerr ("  Value of %s!%s before: %s\n",
				    cell2->base.sheet->name_unquoted, cell_name (cell2), value_peek_string (val1));
			g_printerr ("  Value of %s!%s after : %s\n",
				    cell2->base.sheet->name_unquoted, cell_name (cell2), value_peek_string (val2));
			g_printerr ("\n");
		}
		g_ptr_array_unref (values);
	}

	g_ptr_array_free (cells, TRUE);
	g_ptr_array_free (base_values, TRUE);
	g_object_unref (wb);
	g_object_unref (io_context);
}

/* ------------------------------------------------------------------------- */

#define MAYBE_DO(name) if (strcmp (testname, "all") != 0 && strcmp (testname, (name)) != 0) { } else

int
main (int argc, char const **argv)
{
	GOErrorInfo	*plugin_errs;
	GOCmdContext	*cc;
	GOptionContext	*ocontext;
	GError		*error = NULL;
	const char *testname;

	/* No code before here, we need to init threads */
	argv = gnm_pre_parse_init (argc, argv);

	gnm_conf_set_persistence (FALSE);

	ocontext = g_option_context_new (_("[testname]"));
	g_option_context_add_main_entries (ocontext, sstest_options, GETTEXT_PACKAGE);
	g_option_context_add_group	  (ocontext, gnm_get_option_group ());
	g_option_context_parse (ocontext, &argc, (gchar ***)&argv, &error);
	g_option_context_free (ocontext);

	if (error) {
		g_printerr (_("%s\nRun '%s --help' to see a full list of available command line options.\n"),
			    error->message, g_get_prgname ());
		g_error_free (error);
		return 1;
	}

	if (sstest_show_version) {
		g_printerr (_("version '%s'\ndatadir := '%s'\nlibdir := '%s'\n"),
			    GNM_VERSION_FULL, gnm_sys_data_dir (), gnm_sys_lib_dir ());
		return 0;
	}

	gnm_init ();

	cc = gnm_cmd_context_stderr_new ();
	gnm_plugins_init (GO_CMD_CONTEXT (cc));
	go_plugin_db_activate_plugin_list (
		go_plugins_get_available_plugins (), &plugin_errs);
	if (plugin_errs) {
		/* FIXME: What do we want to do here? */
		go_error_info_free (plugin_errs);
	}

	if (func_state_file) {
		function_dump_defs (func_state_file, 0);
		goto done;
	}
	if (func_def_file) {
		function_dump_defs (func_def_file, 1);
		goto done;
	}
	if (ext_refs_file) {
		function_dump_defs (ext_refs_file, 4);
		goto done;
	}
	if (samples_file) {
		function_dump_defs (samples_file, 5);
		goto done;
	}

	testname = argv[1];
	if (!testname) testname = "all";

	/* ---------------------------------------- */

	MAYBE_DO ("test_insdel_rowcol_names") test_insdel_rowcol_names ();
	MAYBE_DO ("test_insert_delete") test_insert_delete ();
	MAYBE_DO ("test_func_help") test_func_help ();
	MAYBE_DO ("test_nonascii_numbers") test_nonascii_numbers ();
	MAYBE_DO ("test_random") test_random ();
	MAYBE_DO ("test_dpq") test_dpq ();
	if (argc > 2) {
		MAYBE_DO ("test_recalc") {
			char *url = go_shell_arg_to_uri (argv[2]);
			test_recalc (cc, url);
			g_free (url);
		}
	}

	/* ---------------------------------------- */

done:
	g_object_unref (cc);
	gnm_shutdown ();
	gnm_pre_parse_shutdown ();

	return 0;
}
