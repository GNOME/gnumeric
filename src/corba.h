#ifndef GNUMERIC_CORBA_H
#define GNUMERIC_CORBA_H

extern   PortableServer_POA gnumeric_poa;
gboolean WorkbookFactory_init (void);

struct _CmdContext * command_context_corba ();

#endif
