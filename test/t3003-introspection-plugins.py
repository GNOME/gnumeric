#!/usr/bin/python3 -B
# -----------------------------------------------------------------------------

import GnumericTest

import gi
gi.require_version('Gnm', '1.12')
gi.require_version('GOffice', '0.10')
from gi.repository import Gnm
from gi.repository import GOffice
Gnm.init()

# A context for reporting errors to stderr
cc = Gnm.CmdContextStderr()

# Load plugins
Gnm.plugins_init(cc)

# -----------------------------------------------------------------------------

print("Savers available:")
for fs in GOffice.get_file_savers():
    print("Saver ID: {}".format(fs.props.id))
    print("  Descripton: {}".format(fs.props.description))
    print("  Mime type: {}".format(fs.props.mime_type))
    print("  Extension: {}".format(fs.props.extension))
    print("  Overwrite: {}".format(fs.props.overwrite))
    print("  Interactive-only: {}".format(fs.props.interactive_only))
    print("  Format level: {}".format(fs.props.format_level))
print("")

# -----------------------------------------------------------------------------

print("Loaders available:")
for fo in GOffice.get_file_openers():
    print("Loader ID: {}".format(fo.props.id))
    print("  Descripton: {}".format(fo.props.description))
    print("  Suffixes: {}".format(", ".join (fo.get_suffixes())))
    print("  Mime types: {}".format(", ".join (fo.get_mimes())))
    print("  Interactive-only: {}".format(fo.props.interactive_only))
print("")

# -----------------------------------------------------------------------------

print("Plot families: " + ", ".join (sorted(GOffice.GraphPlot.families())))
print("")

# -----------------------------------------------------------------------------

print("Functions: " + ", ".join (sorted([f.get_name(0) for f in Gnm.Func.enumerate()])))
print("")

# -----------------------------------------------------------------------------
