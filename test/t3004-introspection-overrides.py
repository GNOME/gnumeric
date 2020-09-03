#!/usr/bin/python3 -B
# -----------------------------------------------------------------------------

import GnumericTest

import gi
gi.require_version('Gnm', '1.12')
from gi.repository import Gnm
from gi.repository import GOffice as Go
Gnm.init()

if Gnm.in_tree:
    print("Using in-tree gi.overrides.Gnm")
else:
    print("Using installed gi.overrides.Gnm at {}"
          .format (gi.overrides.Gnm.__file__))

# -----------------------------------------------------------------------------

print("\nTesting GnmValue overrides:")
# __str__
print(Gnm.Value.new_empty())
print(Gnm.Value.new_bool(0))
print(Gnm.Value.new_bool(1))
print(Gnm.Value.new_int(12))
print(Gnm.Value.new_float(12.5))
print(Gnm.Value.new_string("howdy"))
v=Gnm.Value.new_float(12.5)
v.set_fmt(Go.Format.new_from_XL("0.00"))
print(v)

# -----------------------------------------------------------------------------

print("\nTesting GnmRange overrides:")
# __new__
r=Gnm.Range(1,2,3,4)
# __str__
print(r)
