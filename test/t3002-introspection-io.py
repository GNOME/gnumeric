#!/usr/bin/python3 -B
# -----------------------------------------------------------------------------

import GnumericTest

import gi
gi.require_version('Gnm', '1.12')
gi.require_version('GOffice', '0.10')
from gi.repository import Gnm
from gi.repository import GOffice
Gnm.init()

import sys
src_uri = GOffice.filename_to_uri (sys.argv[1])
dst_uri = GOffice.filename_to_uri (sys.argv[2])

# A context for reporting errors to stderr
cc = Gnm.CmdContextStderr()

# Load plugins
Gnm.plugins_init(cc)

# A context for io operations
ioc = GOffice.IOContext.new (cc)

# Read a file
wbv = Gnm.WorkbookView.new_from_uri (src_uri, None, ioc, None)
wb = wbv.props.workbook
print("Loaded {}".format(wb.props.uri))

# Save a file
fs = GOffice.FileSaver.for_file_name (dst_uri)
if wbv.save_as (fs, dst_uri, cc):
    print("Saved {}".format(wb.props.uri))
else:
    print("Failed to save to {}".format(dst_uri))


# Remove our references to the objects
wb = None
wbv = None
ioc = None
wbv = None
