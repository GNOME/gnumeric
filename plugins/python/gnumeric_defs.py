# Definitions for the Python plugin for Gnumeric.

import types
class CellRef:
    def __init__(self, column, row, col_relative=0,
                 row_relative=0, sheet=""):
        if ((type (column)       != types.IntType) or
            (type (row)          != types.IntType) or
            (type (col_relative) != types.IntType) or
            (type (row_relative) != types.IntType)):
            #(type (sheet)        != types.StringType)):
            raise TypeError, "Wrong types for a CellRef"

        self.column       = column
        self.row          = row
        self.col_relative = col_relative
        self.row_relative = row_relative
        self.sheet        = sheet 
        
                
class CellRange:
    def __init__ (self, cell_a, cell_b):
        if not ((cell_a.__class__ is CellRef) and
                (cell_b.__class__ is CellRef)):
            raise TypeError, "Wrong types for a CellRange"
        self.range = (cell_a, cell_b)




