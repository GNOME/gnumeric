#!/usr/bin/env python3
import re
import sys
import os
import argparse

def process_file(filename, fix=False):
    """Checks or fixes a single file for duplicate #include directives."""
    include_pattern = re.compile(r'^\s*#\s*include\s+(["<][^">]+[">])')

    includes_seen = set()
    duplicates = []
    lines = []
    new_lines = []
    changed = False

    try:
        with open(filename, 'r', encoding='utf-8', errors='ignore') as f:
            lines = f.readlines()

        for line_num, line in enumerate(lines, 1):
            match = include_pattern.match(line)
            if match:
                header = match.group(1)
                if header in includes_seen:
                    duplicates.append((header, line_num))
                    changed = True
                    if fix:
                        continue # Skip this line
                else:
                    includes_seen.add(header)

            new_lines.append(line)

    except Exception as e:
        print(f"Error processing {filename}: {e}")
        return False

    if duplicates:
        if not fix:
            print(f"[{filename}] Found {len(duplicates)} duplicate include(s):")
            for header, dup_line in duplicates:
                print(f"  - {header} (duplicate at line {dup_line})")
        else:
            try:
                with open(filename, 'w', encoding='utf-8') as f:
                    f.writelines(new_lines)
                print(f"[{filename}] Fixed: removed {len(duplicates)} duplicate includes.")
            except Exception as e:
                print(f"Error writing to {filename}: {e}")
                return False
        return True
    return False

def main():
    parser = argparse.ArgumentParser(description="Check or fix duplicate #include directives.")
    parser.add_argument("files", nargs="+", help="Files to process")
    parser.add_argument("--fix", action="store_true", help="Remove duplicate includes in-place")

    args = parser.parse_args()

    files_impacted = 0
    for filename in args.files:
        if os.path.isfile(filename):
            if process_file(filename, fix=args.fix):
                files_impacted += 1
        else:
            print(f"Warning: {filename} is not a valid file.")

    if not args.fix and files_impacted > 0:
        sys.exit(1)
    else:
        sys.exit(0)

if __name__ == "__main__":
    main()
