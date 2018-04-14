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
dst_uri = GOffice.filename_to_uri (sys.argv[2])

ioc = GOffice.IOContext.new (Gnm.CmdContextStderr.new ())

# Read a file
wbv = Gnm.WorkbookView.new_from_uri (src_uri, None, ioc, None)
wb = wbv.props.workbook
print("Loaded {}".format(wb.props.uri))

# Save a file
fs = GOffice.FileSaver.for_file_name (dst_uri)
wbv.save_to_uri (fs, dst_uri, ioc)
print("Saved {}".format(dst_uri))

# Remove our references to the objects
wb = None
wbv = None
ioc = None
wbv = None
