#ifndef GNUMERIC_SHEET_PRIVATE_H
#define GNUMERIC_SHEET_PRIVATE_H

struct _SheetPrivate {
#ifdef ENABLE_BONOBO
	void            *corba_server;

	GSList          *sheet_vectors;
#endif
};

#endif /* GNUMERIC_SHEET_PRIVATE_H */
