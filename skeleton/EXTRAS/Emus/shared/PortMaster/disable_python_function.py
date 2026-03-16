import sys
import re
from pathlib import Path

def disable_function_in_file(filename, funcname):
    with open(filename, "r") as f:
        lines = f.readlines()

    func_def_pattern = re.compile(rf'^(\s*)def\s+{re.escape(funcname)}\s*\(.*\):')
    changed = False
    new_lines = []
    i = 0
    while i < len(lines):
        line = lines[i]
        m = func_def_pattern.match(line)
        if m:
            indent = m.group(1)
            j = i + 1
            while j < len(lines) and (lines[j].strip() == "" or lines[j].lstrip().startswith("#")):
                j += 1
            if j < len(lines):
                next_line = lines[j]
                body_indent = len(next_line) - len(next_line.lstrip())
                expected_indent = len(indent) + 4
                if body_indent < expected_indent:
                    expected_indent = body_indent
                if next_line.lstrip().startswith("return") and (len(next_line) - len(next_line.lstrip()) == expected_indent):
                    pass
                else:
                    new_lines.extend(lines[i:j])
                    new_lines.append(" " * expected_indent + "return\n")
                    changed = True
                    i = j - 1
            else:
                new_lines.append(line)
                new_lines.append(" " * (len(indent) + 4) + "return\n")
                changed = True
        else:
            new_lines.append(line)
        i += 1

    if changed:
        with open(filename, "w") as f:
            f.writelines(new_lines)
        print(f"Function '{funcname}' in '{filename}' has been disabled (return injected).")
    else:
        print(f"No changes made. Function '{funcname}' may already be disabled or not found.")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python disable_function.py <python_file> <function_name>")
        sys.exit(1)

    file_path = sys.argv[1]
    func_name = sys.argv[2]

    if not Path(file_path).is_file():
        print(f"Error: file '{file_path}' not found.")
        sys.exit(1)

    disable_function_in_file(file_path, func_name)
