/**
 * ole.c: OLE2 file format helper program,
 *        good for dumping OLE streams, and
 * corresponding biff records, and hopefuly
 * some more ...
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 **/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "ms-ole.h"
#include "ms-ole-summary.h"
#include "ms-biff.h"
#include "biff-types.h"

#define BIFF_TYPES_FILE    "biff-types.h"

static char delim[]=" \t\n";

typedef struct {
	guint16 opcode;
	char *name;
} GENERIC_TYPE;

static GPtrArray *biff_types   = NULL;

static char *cur_dir = NULL;

static void dump_vba (MsOle *f);

static void
read_types (char *fname, GPtrArray **types)
{
	FILE *file = fopen(fname, "r");
	char buffer[1024];
	*types = g_ptr_array_new ();
	if (!file) {
		char *newname = g_strconcat ("../", fname, NULL);
		file = fopen (newname, "r");
	}
	if (!file) {
		printf ("Can't find vital file '%s'\n", fname);
		return;
	}
	while (!feof(file)) {
		char *p;
		fgets(buffer,1023,file);
		for (p=buffer;*p;p++)
			if (*p=='0' && *(p+1)=='x') {
				GENERIC_TYPE *bt = g_new (GENERIC_TYPE,1);
				char *name, *pt;
				bt->opcode=strtol(p+2,0,16);
				pt = buffer;
				while (*pt && *pt != '#') pt++;      /* # */
				while (*pt && !isspace(*pt)) pt++;  /* define */
				while (*pt &&  isspace(*pt)) pt++;  /* '   ' */
				while (*pt && *pt != '_') pt++;     /* BIFF_ */
				name = *pt?pt+1:pt;
				while (*pt && !isspace(*pt)) pt++;
				bt->name=g_strndup(name, (pt-name));
				g_ptr_array_add (*types, bt);
				break;
			}
	}
	fclose (file);
}

static char*
get_biff_opcode_name (guint16 opcode)
{
	int lp;
	if (!biff_types)
		read_types (BIFF_TYPES_FILE, &biff_types);
	/* Count backwars to give preference to non-filtered record types */
	for (lp=biff_types->len; --lp >= 0 ;) {
		GENERIC_TYPE *bt = g_ptr_array_index (biff_types, lp);
		if (bt->opcode>0xff) {
			if (bt->opcode == opcode)
				return bt->name;
		} else {
			if (bt->opcode == (opcode&0xff))
				return bt->name;
		}
	}
	return "Unknown";
}

static void
list_files (MsOle *ole)
{
	char     **names;
	MsOleErr   result;
	int        lp;

	result = ms_ole_directory (&names, ole, cur_dir);
	if (result != MS_OLE_ERR_OK) {
		g_warning ("Failed dir");
		return;
	}

	if (!names[0])
		g_warning ("You entered a file !");

	for (lp = 0; names[lp]; lp++) {
		MsOleStat s;
		result = ms_ole_stat (&s, ole, cur_dir, names[lp]);

		if (s.type == MsOleStreamT)
			printf ("'%25s : length %d bytes\n", names[lp], s.size);
		else
			printf ("'[%s] : Storage ( directory )\n", names [lp]);
		
	}
}

static void
list_commands ()
{
	printf ("command can be one or all of:\n");
	printf (" * ls:                   list files\n");
	printf (" * cd:                   enter storage\n");
	printf (" * biff    <stream name>:   dump biff records, merging continues\n");
	printf (" * biffraw <stream name>:   dump biff records no merge + raw data\n");
	printf (" * dump    <stream name>:   dump stream\n");
	printf (" * summary              :   dump document summary info\n");
	printf (" * debug                :   dump internal ole library status\n");
	printf (" * vba                  :   attempt to dump vba \n");
	printf (" Raw transfer commands\n");
	printf (" * get     <stream name> <fname>\n");
	printf (" * put     <fname> <stream name>\n");
	printf (" * copyin  [<fname>,]...\n");
	printf (" * copyout [<fname>,]...\n");
	printf (" * quit,exit,bye:        exit\n");
}

static void
syntax_error(char *err)
{
	if (err) {
		printf("Error; '%s'\n",err);
		exit(1);
	}
		
	printf ("Sytax:\n");
	printf (" ole <ole-file> [-i] [commands...]\n\n");
	printf (" -i: Interactive, queries for fresh commands\n\n");
	list_commands ();
	exit(1);
}

/* ---------------------------- End cut ---------------------------- */

static void
enter_dir (MsOle *ole)
{
	char *newpath, *ptr;

	ptr = strtok (NULL, delim);
	if (!ptr) {
		printf ("Takes a directory argument\n");
		return;
	}

	if (!g_strcasecmp (ptr, "..")) {
		guint lp;
		char **tmp;
		GString *newp = g_string_new ("");

		tmp = g_strsplit (cur_dir, "/", -1);
		lp  = 0;
		if (!tmp[lp])
			return;

		while (tmp[lp+1]) {
			g_string_sprintfa (newp, "%s/", tmp[lp]);
			lp++;
		}
		g_free (cur_dir);
		cur_dir = newp->str;
		g_string_free (newp, FALSE);
	} else {
		MsOleStat s;
		MsOleErr  result;

		newpath = g_strconcat (cur_dir, ptr, "/", NULL);

		result = ms_ole_stat (&s, ole, newpath, "");
		if (result == MS_OLE_ERR_EXIST) {
			printf ("Storage '%s' not found\n", ptr);
			g_free (newpath);
			return;
		}
		if (result != MS_OLE_ERR_OK) {
			g_warning ("internal error");
			g_free (newpath);
			return;
		}
		if (s.type != MsOleStorageT &&
		    s.type != MsOleRootT) {
			printf ("Trying to enter a stream. (%d)\n", s.type);
			g_free (newpath);
			return;
		}

		g_free (cur_dir);
		cur_dir = newpath;
	}
}

static void
do_dump (MsOle *ole)
{
	char        *ptr;
	MsOleStream *stream;
	guint8      *buffer;

	ptr = strtok (NULL, delim);
	if (!ptr) {
		printf ("Need a stream name\n");
		return;
	}
       
	if (ms_ole_stream_open (&stream, ole, cur_dir, ptr, 'r') !=
	    MS_OLE_ERR_OK) {
		printf ("Error opening '%s'\n", ptr);
		return;
	}
	buffer = g_malloc (stream->size);
	stream->read_copy (stream, buffer, stream->size);
	printf ("Stream : '%s' length 0x%x\n", ptr, stream->size);
	if (buffer)
		dump (buffer, stream->size);
	else
		printf ("Failed read\n");
	ms_ole_stream_close (&stream);
}

static void
do_biff (MsOle *ole)
{
	char *ptr;
	MsOleStream *stream;
	
	ptr = strtok (NULL, delim);
	if (!ptr) {
		printf ("Need a stream name\n");
		return;
	}
       
	if (ms_ole_stream_open (&stream, ole, cur_dir, ptr, 'r') !=
	    MS_OLE_ERR_OK) {
		printf ("Error opening '%s'\n", ptr);
		return;
	}
	{
		BiffQuery *q = ms_biff_query_new (stream);
		guint16 last_opcode=0xffff;
		guint32 last_length=0;
		guint32 count=0;
		while (ms_biff_query_next(q)) {
			if (q->opcode == last_opcode &&
			    q->length == last_length)
				count++;
			else {
				if (count>0)
					printf (" x %d\n", count+1);
				else
					printf ("\n");
				count=0;
				printf ("Opcode 0x%3x : %15s, length %d",
					q->opcode, get_biff_opcode_name (q->opcode), q->length);
			}
			last_opcode=q->opcode;
			last_length=q->length;
		}
		printf ("\n");
		ms_ole_stream_close (&stream);
	}
}

static void
do_biff_raw (MsOle *ole)
{
	char *ptr;
	MsOleStream *stream;
	
	ptr = strtok (NULL, delim);
	if (!ptr) {
		printf ("Need a stream name\n");
		return;
	}
       
	if (ms_ole_stream_open (&stream, ole, cur_dir, ptr, 'r') !=
	    MS_OLE_ERR_OK) {
		printf ("Error opening '%s'\n", ptr);
		return;
	}
	{
		guint8 data[4], *buffer;
		
		buffer = g_new (guint8, 65550);
		while (stream->read_copy (stream, data, 4)) {
			guint32 len=MS_OLE_GET_GUINT16(data+2);
			printf ("0x%4x Opcode 0x%3x : %15s, length 0x%x (=%d)\n", stream->position,
				MS_OLE_GET_GUINT16(data), get_biff_opcode_name (MS_OLE_GET_GUINT16(data)),
				len, len);
			stream->read_copy (stream, buffer, len);
			dump (buffer, len);
			buffer[0]=0;
			buffer[len-1]=0;
		}
		ms_ole_stream_close (&stream);
	}
}

static void
really_get (MsOle *ole, char *from, char *to)
{
	MsOleStream *stream;
	
	if (ms_ole_stream_open (&stream, ole, cur_dir, from, 'r') !=
	    MS_OLE_ERR_OK) {
		printf ("Error opening '%s'\n", from);
		return;
	} else {
		guint8 *buffer = g_malloc (stream->size);
		FILE *f = fopen (to, "w");
		stream->read_copy (stream, buffer, stream->size);
		printf ("Stream : '%s' length 0x%x\n", from, stream->size);
		if (f && buffer) {
			fwrite (buffer, 1, stream->size, f);
			fclose (f);
		} else
			printf ("Failed write to '%s'\n", to);
		ms_ole_stream_close (&stream);

	}
}

static void
do_get (MsOle *ole)
{
	char *from, *to;

	from = strtok (NULL, delim);
	to   = strtok (NULL, delim);
	really_get (ole, from, to);
}

static void
really_put (MsOle *ole, char *from, char *to)
{
	MsOleStream *stream;
	char buffer[8200];

	if (!from || !to) {
		printf ("Null name\n");
		return;
	}

	if (ms_ole_stream_open (&stream, ole, cur_dir, to, 'w') !=
	    MS_OLE_ERR_OK) {
		printf ("Error opening '%s'\n", to);
		return;
	} else {
		FILE *f = fopen (from, "r");
		size_t len;
		int block=0;

		if (!f || !stream) {
			printf ("Failed write\n");
			return;
		}

		stream->lseek (stream, 0, MsOleSeekSet);
	       
		do {
			guint32 lenr = 1+ (int)(8192.0*rand()/(RAND_MAX+1.0));
			len = fread (buffer, 1, lenr, f);
			printf ("Transfering block %d = %d bytes\n", block++, len); 
			stream->write (stream, buffer, len);
		} while (!feof(f) && len>0);

		fclose (f);
		ms_ole_stream_close (&stream);
	}
}

static void
do_summary (MsOle *ole)
{
	MsOleSummary        *si;
	MsOleSummaryPreview  preview;
	gboolean             ok;
	gchar               *txt;
	guint32              num;

	si = ms_ole_summary_open (ole);
	if (!si) {
		printf ("No summary information\n");
		return;
	}

	txt = ms_ole_summary_get_string (si, MS_OLE_SUMMARY_TITLE, &ok);
	if (ok)
		printf ("The title is %s\n", txt);
	else
		printf ("no title found\n");
	g_free (txt);

	txt = ms_ole_summary_get_string (si, MS_OLE_SUMMARY_SUBJECT, &ok);
	if (ok)
		printf ("The subject is %s\n", txt);
	else
		printf ("no subject found\n");
	g_free (txt);

	txt = ms_ole_summary_get_string (si, MS_OLE_SUMMARY_AUTHOR, &ok);
	if (ok)
		printf ("The author is %s\n", txt);
	else
		printf ("no author found\n");
	g_free (txt);

	txt = ms_ole_summary_get_string (si, MS_OLE_SUMMARY_KEYWORDS, &ok);
	if (ok)
		printf ("The keywords are %s\n", txt);
	else
		printf ("no keywords found\n");
	g_free (txt);

	txt = ms_ole_summary_get_string (si, MS_OLE_SUMMARY_COMMENTS, &ok);
	if (ok)
		printf ("The comments are %s\n", txt);
	else
		printf ("no comments found\n");
	g_free (txt);

	txt = ms_ole_summary_get_string (si, MS_OLE_SUMMARY_TEMPLATE, &ok);
	if (ok)
		printf ("The template was %s\n", txt);
	else
		printf ("no template found\n");
	g_free (txt);

	txt = ms_ole_summary_get_string (si, MS_OLE_SUMMARY_LASTAUTHOR, &ok);
	if (ok)
		printf ("The last author was %s\n", txt);
	else
		printf ("no last author found\n");
	g_free (txt);

	txt = ms_ole_summary_get_string (si, MS_OLE_SUMMARY_REVNUMBER, &ok);
	if (ok)
		printf ("The rev no was %s\n", txt);
	else
		printf ("no rev no found\n");
	g_free (txt);

	txt = ms_ole_summary_get_string (si, MS_OLE_SUMMARY_APPNAME, &ok);
	if (ok)
		printf ("The app name was %s\n", txt);
	else
		printf ("no app name found\n");
	g_free (txt);

/*	txt = wvSumInfoGetTime(&yr, &mon, &day, &hr, &min, &sec,PID_TOTAL_EDITTIME,&si);
	if (ok)
		printf ("Total edit time was %d/%d/%d %d:%d:%d\n",day,mon,yr,hr,min,sec);
	else
		printf ("no total edit time found\n");
	g_free (txt);

	txt = wvSumInfoGetTime(&yr, &mon, &day, &hr, &min, &sec,PID_LASTPRINTED,&si);
	if (ok)
	    printf ("Last printed on %d/%d/%d %d:%d:%d\n",day,mon,yr,hr,min,sec);
	else
		printf ("no last printed time found\n");

	txt = wvSumInfoGetTime(&yr, &mon, &day, &hr, &min, &sec,PID_CREATED,&si);
	if (ok)
	    printf ("Created on %d/%d/%d %d:%d:%d\n",day,mon,yr,hr,min,sec);
	else
		printf ("no creation time found\n");

	txt = wvSumInfoGetTime(&yr, &mon, &day, &hr, &min, &sec,PID_LASTSAVED,&si);
	if (ok)
	    printf ("Last Saved on %d/%d/%d %d:%d:%d\n",day,mon,yr,hr,min,sec);
	else
	printf ("no lastsaved date found\n");*/

	num = ms_ole_summary_get_long (si, MS_OLE_SUMMARY_PAGECOUNT, &ok);

	if (ok)
		printf ("PageCount is %d\n", num);
	else
		printf ("no pagecount\n");

	num = ms_ole_summary_get_long (si, MS_OLE_SUMMARY_WORDCOUNT, &ok);
	if (ok)
		printf ("WordCount is %d\n", num);
	else
		printf ("no wordcount\n");

	num = ms_ole_summary_get_long (si, MS_OLE_SUMMARY_CHARCOUNT, &ok);

	if (ok)
		printf ("CharCount is %d\n", num);
	else
		printf ("no charcount\n");

	num = ms_ole_summary_get_long (si, MS_OLE_SUMMARY_SECURITY, &ok);
	if (ok)
		printf ("Security is %d\n", num);
	else
		printf ("no security\n");

	preview = ms_ole_summary_get_preview (si, MS_OLE_SUMMARY_THUMBNAIL, &ok);
	if (ok) {
		printf ("preview is %d bytes long\n", preview.len);
		ms_ole_summary_preview_destroy (preview);
	} else
		printf ("no preview found\n");

	ms_ole_summary_close (si);
}

static void
do_put (MsOle *ole)
{
	char *from, *to;

	from = strtok (NULL, delim);
	to   = strtok (NULL, delim);

	if (!from || !to) {
		printf ("put <filename> <stream>\n");
		return;
	}

	really_put (ole, from, to);
}

static void
do_copyin (MsOle *ole)
{
	char *from;

	do {
		from = strtok (NULL, delim);
		if (from)
			really_put (ole, from, from);
	} while (from);
}

static void
do_copyout (MsOle *ole)
{
	char *from;

	do {
		from = strtok (NULL, delim);
		if (from)
			really_get (ole, from, from);
	} while (from);
}

int main (int argc, char **argv)
{
	MsOle *ole;
	int lp,exit=0,interact=0;
	char *buffer = g_new (char, 1024) ;

	if (argc<2)
		syntax_error(0);

	printf ("Ole file '%s'\n", argv[1]);
	if (ms_ole_open (&ole, argv[1]) != MS_OLE_ERR_OK) {
		printf ("Creating new file '%s'\n", argv[1]);
		if (ms_ole_create (&ole, argv[1]) != MS_OLE_ERR_OK)
			syntax_error ("Can't open file or create new one");
	}

	if (argc<=2)
		syntax_error ("Need command or -i");

	if (argc>2 && argv[argc-1][0]=='-'
	    && argv[argc-1][1]=='i') 
		interact=1;
	else {
		char *str=g_strdup(argv[2]) ;
		for (lp=3;lp<argc;lp++)
			str=g_strconcat(str," ",argv[lp],NULL); /* Mega leak :-) */
		buffer = str; /* and again */
	}

	cur_dir = g_strdup ("/");

	do
	{
		char *ptr;

		if (interact) {
			fprintf (stdout,"> ");
			fflush (stdout);
			fgets (buffer, 1023, stdin);
		}

		ptr = strtok (buffer, delim);
		if (!ptr && interact) continue;
		if (!interact)
			printf ("Command : '%s'\n", ptr);
		if (g_strcasecmp(ptr, "ls")==0) {
			list_files (ole);
		} else if (g_strcasecmp(ptr, "cd")==0)
			enter_dir (ole);
		else if (g_strcasecmp(ptr, "dump")==0)
			do_dump (ole);
		else if (g_strcasecmp(ptr, "biff")==0)
			do_biff (ole);
		else if (g_strcasecmp(ptr, "biffraw")==0)
			do_biff_raw (ole);
		else if (g_strcasecmp(ptr, "get")==0)
			do_get (ole);
		else if (g_strcasecmp(ptr, "put")==0)
			do_put (ole);
		else if (g_strcasecmp(ptr, "copyin")==0)
			do_copyin (ole);
		else if (g_strcasecmp(ptr, "copyout")==0)
			do_copyout (ole);
		else if (g_strcasecmp(ptr, "summary")==0)
			do_summary (ole);
		else if (g_strcasecmp(ptr, "debug")==0)
			ms_ole_debug (ole, 1);
		else if (g_strcasecmp(ptr, "vba")==0)
			dump_vba (ole);
		else if (g_strcasecmp(ptr,"help")==0 ||
			   g_strcasecmp(ptr,"?")==0 ||
			   g_strcasecmp(ptr,"info")==0 ||
			   g_strcasecmp(ptr,"man")==0)
			list_commands ();
		else if (g_strcasecmp(ptr,"exit")==0 ||
			   g_strcasecmp(ptr,"quit")==0 ||
			   g_strcasecmp(ptr,"q")==0 ||
			   g_strcasecmp(ptr,"bye")==0)
			exit = 1;
	}
	while (!exit && interact);

	ms_ole_destroy (&ole);
	return 1;
}

static void
dump_vba_module (MsOle *f, const char *path, const char *name)
{
	MsOleStream *s;

	if (ms_ole_stream_open (&s, f, path, name, 'r') !=
	    MS_OLE_ERR_OK) {
		printf ("Strange: can't open '%s%s'\n", path, name);
		return;
	}

	/* Very, very, very, very, very crude :-) */
	{
		guint8  *data, *ptr;
		guint32  i;

		data = g_new (guint8, s->size);
		if (!s->read_copy (s, data, s->size)) {
			printf ("Strange: failed read of module '%s%s'\n", path, name);
			return;
		}
		
		ptr = data;
		i   = 0;
		do {
			if (!g_strncasecmp (ptr, "Attribut", 8)) {
				guint8 *txt = g_new (guint8, s->size);
				guint8 *p;
				guint  j;

				printf ("Possibly found the text !\n");
				p = txt;
				while (i < s->size) {
					for (j = 0; j < 8 && i + j < s->size; j++, i++)
						*p++ = *ptr++;
					i++;
					ptr++;
				}
				*p = '\0';
				printf ("Text is '%s'\n", txt);
				g_free (txt);
				break;
			}
			ptr++; i++;
		} while (i < s->size - 10);

		g_free (data);
	}
	
	ms_ole_stream_close (&s);
}

/* Hack - leave it here for now */
static void
dump_vba (MsOle *f)
{
	const char *vbapath = "/_VBA_PROJECT_CUR";
	char **dir;
	MsOleStream *s;
	MsOleStat    st;
	char *txt;
	int   i;
	int   module_count;

	if (ms_ole_stat (&st, f, vbapath, "") != MS_OLE_ERR_OK ||
	    st.type == MsOleStreamT) {
		printf ("No valid VBA found\n");
		return;
	}

	if (ms_ole_stream_open (&s, f, vbapath, "PROJECT", 'r') !=
	    MS_OLE_ERR_OK) {
		printf ("No project file... wierd\n");
	} else {
		txt = g_new (guint8, s->size);
		if (!s->read_copy (s, txt, s->size))
			printf ("Failed to read project stream\n");
		else {
			printf ("----------\n");
			printf ("Project file:\n");
			printf ("%s", txt);
			printf ("----------\n");
		}
		ms_ole_stream_close (&s);
	}

	txt = g_strconcat (vbapath, "/VBA", NULL);
	if (ms_ole_directory (&dir, f, txt) != MS_OLE_ERR_OK) {
		printf ("No VBA subdirectory found");
		g_free (txt);
		return;
	}

	module_count = 0;
	for (i = 0; dir[i]; i++) {

		if (!g_strncasecmp (dir[i], "Module", 6)) {
			printf ("Module : %d = '%s'\n", module_count, dir[i]);
			printf ("----------\n");
			dump_vba_module (f, txt, dir[i]);
			printf ("----------\n");
			module_count++;
		}	
	}
	if (!module_count)
		printf ("Strange no modules found\n");

	g_free (txt);
}
