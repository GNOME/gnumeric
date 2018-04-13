#!/usr/bin/python
# -----------------------------------------------------------------------------

import gi
gi.require_version('Gnm', '1.12') 
from gi.repository import Gnm
Gnm.init()

# Create a workbook with one sheet
wb = Gnm.Workbook.new_with_sheets(1)

# Get sheet.  Index starts at 0
sheet = wb.sheet_by_index(0)
print "Name:", sheet.props.name
print "Number of columns:", sheet.props.columns
print "Number of rows:", sheet.props.rows

# Store values and expressions is some cells.  Coordinates are (col,row)
# both starting at 0.  (So what the gui sees as row 1 is 0 here.)
sheet.cell_set_value(0,0,Gnm.Value.new_int(10))
sheet.cell_set_value(0,1,Gnm.Value.new_float(101.25))
sheet.cell_set_text(0,2,"=A1+A2")
sheet.cell_set_text(0,3,"'01")
sheet.cell_set_text(0,4,"zzz")
sheet.cell_set_value(0,5,Gnm.Value.new_string("abc"))
sheet.cell_set_value(0,6,Gnm.Value.new_bool(1))

# Copy A1:A7, paste to C1:C7
src = Gnm.Range()
src.init(0,0,0,6)
cr = Gnm.clipboard_copy_range(sheet,src)
dst = Gnm.Range()
dst.init(2,0,2,6)
pt = Gnm.PasteTarget.new (sheet,dst,Gnm.PasteFlags.DEFAULT)
Gnm.clipboard_paste_region(cr,pt,None)


# Make A1:A2 bold
st = Gnm.Style.new()
st.set_font_bold(1)
r = Gnm.Range()
r.init(0,0,0,1)
sheet.style_apply_range(r,st)

# Recalculate all cells that need it
wb.recalc()

print "\nAs string:"
for i in range(7):
    print sheet.cell_get_value(0,i).get_as_string()

print "\nAs int:"
for i in range(7):
    print sheet.cell_get_value(0,i).get_as_int()

print "\nList of cells in sheet:"
for c in sheet.cells(None):
    st = sheet.style_get (c.pos.col,c.pos.row)
    bold = st.get_font_bold()
    print("{}: {} {}".format(c.name(), c.get_entered_text(), ("[bold]" if bold else "")))
