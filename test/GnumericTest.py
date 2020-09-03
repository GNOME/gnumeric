import os

# -----------------------------------------------------------------------------
# Install a hack to make sure we load the in-tree version

if 'GNM_TEST_INTROSPECTION_DIR' in os.environ:
    import importlib.machinery
    class HackFinder:
        def find_spec(name,path,target=None):
            if name == 'gi.overrides.Gnm':
                return importlib.machinery.PathFinder.find_spec (name, [os.environ['GNM_TEST_INTROSPECTION_DIR']])
            return None

    import sys
    sys.meta_path = [HackFinder] + sys.meta_path

# -----------------------------------------------------------------------------
