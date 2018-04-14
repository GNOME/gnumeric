#!/usr/bin/python
# -----------------------------------------------------------------------------

import gi
gi.require_version('Gnm', '1.12') 
gi.require_version('GOffice', '0.10') 
from gi.repository import Gnm
from gi.repository import GOffice
Gnm.init()

import sys
src_uri = GOffice.filename_to_uri (sys.argv[1])
print(src_uri)
dst_uri = GOffice.filename_to_uri (sys.argv[2])
print(dst_uri)

ioc = GOffice.IOContext.new (Gnm.CmdContextStderr.new ())
wbv =  Gnm.WorkbookView.new_from_uri (src_uri, None, ioc, None)

fs = GOffice.FileSaver.for_file_name (dst_uri)
wbv.save_to_uri (fs, dst_uri, ioc)

ioc = None
wbv = None
