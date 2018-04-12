#!/usr/bin/python
# -----------------------------------------------------------------------------

import gi
gi.require_version('Gnm', '1.12') 
from gi.repository import Gnm
Gnm.init()

wb = Gnm.Workbook.new_with_sheets(1)
sheet = wb.sheet_by_index(0)
sheet.cell_set_value(0,0,Gnm.Value.new_int(10))
sheet.cell_set_value(0,1,Gnm.Value.new_float(101.25))
sheet.cell_set_text(0,2,"=A1+A2")
sheet.cell_set_text(0,3,"'01")
sheet.cell_set_text(0,4,"zzz")
sheet.cell_set_value(0,5,Gnm.Value.new_string("abc"))
sheet.cell_set_value(0,6,Gnm.Value.new_bool(1))
wb.recalc()

print "Peek:"
for i in range(7):
    print sheet.cell_get_value(0,i).peek_string()

print "\nAs int:"
for i in range(7):
    print sheet.cell_get_value(0,i).get_as_int()
