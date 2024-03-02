import os
import sys
import re
import glob
import subprocess
import itertools

def main():
    dlls = sys.argv[1:]

    export_re = re.compile(r'\s+[0-9a-f]+\s+[0-9a-f]+\s+(?:[0-9a-f]+|        )\s+(.*?)(?:\s+\(.*\))?', re.I)

    for dll in itertools.chain(*[glob.glob(pat) for pat in dlls]):
        with subprocess.Popen(['dumpbin', '/exports', dll], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True) as proc:
            lines = proc.stdout.readlines()

        dll_prefix = os.path.basename(dll).lower() + ':'
        process_imports = False

        for line in lines:
            line = line.rstrip()

            if not process_imports:
                if line.startswith('    ordinal'):
                    process_imports = True
            else:
                if line.startswith('  Summary'):
                    break

                match = export_re.fullmatch(line)
                if match:
                    print(dll_prefix+match.group(1))
                

if __name__ == '__main__':
    main()
