#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ms-ole.h"

static
MS_OLE_STREAM *
get_file_handle (char *name)
{
	return NULL ;
}

static
void list_files (MS_OLE *ole)
{
	MS_OLE_DIRECTORY *dir = ms_ole_directory_new (ole) ;
	while (ms_ole_directory_next(dir)) {
		printf ("'%s' : type %d, length %d bytes\n", dir->name, dir->type, dir->length) ;
	}
}

int main (int argc, char **argv)
{
	MS_OLE *ole ;
	int lp ;
	for (lp=0;lp<argc;lp++)
		printf ("Arg %d = '%s'\n", lp, argv[lp]) ;
	if (argc<2)
	       exit (1) ;
	printf ("Ole file '%s'\n", argv[1]) ;
	ole = ms_ole_new (argv[1]) ;
	for (lp=2;lp<argc;lp++) {
		if (strcasecmp(argv[lp], "ls")==0) {
			list_files (ole) ;
		} else if (strcasecmp(argv[lp], "dump")==0) {
			MS_OLE_STREAM *stream ;
			if (lp+1<argc) {
				stream = get_file_handle (argv[lp+1]) ;
				
			} else {
				printf ("Need a stream name\n") ;
				return 0 ;
			}
		}
	}
	ms_ole_destroy (ole) ;
	return 1 ;
}
