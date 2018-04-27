#!/usr/bin/python
# -----------------------------------------------------------------------------

import gi
gi.require_version('Gnm', '1.12') 
from gi.repository import Gnm
Gnm.init()

import os.path;

def atomize_path(p):
    res=[]
    while 1:
        h,t = os.path.split(p)
        if t != "":
            res.append(t)
        if h == "":
            break
        if h == p:
            res.append(h)
            break;
        p = h
    res.reverse()
    return res

l=atomize_path(os.path.dirname(gi.overrides.Gnm.__file__))
if len(l) > 3 and l[-3] == "introspection":
    print("Using in-tree gi.overrides.Gnm")
else:
    print("Using installed gi.overrides.Gnm at {}"
          .format (gi.overrides.Gnm.__file__))

print Gnm.Value.new_int(12)
