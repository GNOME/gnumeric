#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "ms-ole.h"
#include "ms-biff.h"

#define TYPES_FILE "biff-types.h"

typedef struct {
	guint16 opcode;
	char *name;
} BIFF_TYPE;

static GPtrArray *types=NULL;

static void
read_types ()
{
	FILE *file = fopen(TYPES_FILE, "r");
	char buffer[1024];
	types = g_ptr_array_new ();
	while (!feof(file)) {
		char *p;
		fgets(buffer,1023,file);
		for (p=buffer;*p;p++)
			if (*p=='0' && *(p+1)=='x') {
				BIFF_TYPE *bt = g_new (BIFF_TYPE,1);
				char *name, *pt;
				bt->opcode=strtol(p+2,0,16);
				pt = buffer;
				while (*pt && *pt != '#') pt++;      /* # */
				while (*pt && !isspace(*pt)) pt++;  /* define */
				while (*pt &&  isspace(*pt)) pt++;  /* '   ' */
				name = pt;
				while (*pt && !isspace(*pt)) pt++;
				bt->name=g_strndup(name, (pt-name));
				g_ptr_array_add (types, bt);
				break;
			}
	}
	fclose (file);
}

static char*
get_opcode_name (guint16 opcode)
{
	int lp;
	if (!types)
		read_types ();
	for (lp=0;lp<types->len;lp++) {
		BIFF_TYPE *bt = g_ptr_array_index (types, lp);
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

static MS_OLE_DIRECTORY *
get_file_handle (MS_OLE *ole, char *name)
{
	MS_OLE_DIRECTORY *dir;
	if (!name)
		return NULL;
	dir = ms_ole_directory_new (ole);
	while (ms_ole_directory_next(dir)) {
		if (g_strcasecmp(dir->name, name)==0) {
			return dir;
		}
	}	
	printf ("Stream '%s' not found\n", name);
	ms_ole_directory_destroy (dir);
	return NULL;
}

static void
list_files (MS_OLE *ole)
{
	MS_OLE_DIRECTORY *dir = ms_ole_directory_new (ole);
	while (ms_ole_directory_next(dir)) {
		printf ("'%s' : type %d, length %d bytes\n", dir->name, dir->type, dir->length);
	}
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
	printf ("command can be one or all of:\n");
	printf (" * ls:                   list files\n");
	printf (" * dump <stream name>:   dump stream\n");
	printf (" * quit,exit,bye:        exit\n");
	exit(1);
}

int main (int argc, char **argv)
{
	MS_OLE *ole;
	int lp,exit=0,interact=0;
	char *buffer = g_new (char, 1024) ;

	if (argc<2)
		syntax_error(0);

	printf ("Ole file '%s'\n", argv[1]);
	ole = ms_ole_new (argv[1]);
	if (!ole)
		syntax_error ("Can't open file");

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

	do
	{
		char *ptr;
		char delim[]=" \t\n";

		if (interact) {
			fprintf (stdout,"> ");
			fflush (stdout);
			fgets (buffer, 1023, stdin);
		}

		ptr = strtok (buffer, delim);
		printf ("Command : '%s'\n", ptr);
		if (g_strcasecmp(ptr, "ls")==0) {
			list_files (ole);
		} else if (g_strcasecmp(ptr, "dump")==0) {
			MS_OLE_DIRECTORY *dir;
			ptr = strtok (NULL, delim);
			if ((dir = get_file_handle (ole, ptr)))
			{
				MS_OLE_STREAM *stream = ms_ole_stream_open (dir, 'r');
				guint8 *buffer = g_malloc (dir->length);
				stream->read_copy (stream, buffer, dir->length);
				printf ("Stream : '%s' length 0x%x\n", ptr, dir->length);
				if (buffer)
					dump (buffer, dir->length);
				else
					printf ("Failed read\n");
				ms_ole_stream_close (stream);
			} else {
				printf ("Need a stream name\n");
				return 0;
			}
		} else if (g_strcasecmp(ptr, "biff")==0) {
			MS_OLE_DIRECTORY *dir;
			ptr = strtok (NULL, delim);
			if ((dir = get_file_handle (ole, ptr)))
			{
				MS_OLE_STREAM *stream = ms_ole_stream_open (dir, 'r');
				BIFF_QUERY *q = ms_biff_query_new (stream);
				while (ms_biff_query_next(q)) {
					printf ("Opcode 0x%3x : %15s, length %d\n",
						q->opcode, get_opcode_name (q->opcode), q->length);
				}
				ms_ole_stream_close (stream);
			} else {
				printf ("Need a stream name\n");
				return 0;
			}
		} else if (g_strcasecmp(ptr,"exit")==0 ||
			   g_strcasecmp(ptr,"quit")==0 ||
			   g_strcasecmp(ptr,"bye")==0)
			exit = 1;
	}
	while (!exit && interact);

	ms_ole_destroy (ole);
	return 1;
}
