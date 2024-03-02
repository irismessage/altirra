#! python3

# Altirra build script
# Copyright (C) Avery Lee 2014-2022
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

import sys
import subprocess
import re
import os
import time
import zipfile
import shutil
import marshal

EXPECTED_MSVC_VERSION = '19.29.30140'
EXPECTED_MSVC_VERSION_DESC = 'Visual Studio 2019 v16.11.10'

DIAGNOSTIC_PATTERN = re.compile(r'(?:[0-9]+\>|)(?:[a-zA-Z0-9:\\/.]* *\([0-9,]+\).*(?:warning|error)|.*fatal error LNK[0-9]+:).*', re.I)

class Options():
    def __init__(self):
        super().__init__()

    version_id = None
    enable_banner = True
    enable_vccheck = True
    enable_clean = True
    enable_build = True
    enable_arm64 = False
    enable_checkimports = True
    enable_package = True
    enable_packopt = True
    override_changelist = None

opts = Options()

class Log():
    def __init__(self):
        self.last_len = 0
        self.start_time = time.time()

    def log(self, *args) -> None:
        print(self._format(*args).ljust(self.last_len))
        self.last_len = 0

    def log_status(self, *args) -> None:
        line = self._format(*args)[0:100]
        line_output = line.ljust(self.last_len)
        self.last_len = len(line)
        
        sys.stdout.write(line_output + '\r')

    def line_clear(self, ) -> None:
        if self.last_len:
            sys.stdout.write(' ' * self.last_len + '\r')

    def _format(self, *args) -> str:
        if len(args) == 0:
            line = ''
        elif len(args) > 1:
            line = args[0].format(*args[1:])
        else:
            line = args[0]

        delta = int(time.time() - self.start_time)
        return '[{:2}:{:02}] {}'.format(delta//60, delta%60, line.expandtabs(8))

log = Log()

class BuildException(Exception):
    pass

def try_remove(path:str) -> bool:
    try:
        os.remove(path)
    except FileNotFoundError:
        return False

    return True

def try_invoke_program(*args, diagnostic_pattern = None) -> int:
    with subprocess.Popen(list(args), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True) as proc:
        try:
            last_len = 0
            phase = 0

            while True:
                line = proc.stdout.readline()

                if not line:
                    break

                line = line.rstrip()

                if not line:
                    continue

                if diagnostic_pattern is not None and diagnostic_pattern.fullmatch(line):
                    log.log('>' + line)
                else:
                    log.log_status('-\\|/'[phase&3] + line)
                    phase += 1

            log.line_clear()

            return proc.wait()
        except KeyboardInterrupt:
            proc.terminate()
            raise

def invoke_program(*args, diagnostic_pattern = None):
    return_code = try_invoke_program(*args, diagnostic_pattern = diagnostic_pattern)
    if return_code != 0:
        raise BuildException('Command failed with return code: {}'.format(return_code))

def invoke_vs(*args) -> None:
    invoke_program('devenv.com', *args, diagnostic_pattern = DIAGNOSTIC_PATTERN)

def get_vs_version() -> str:
    with subprocess.Popen('cl', stdout=subprocess.PIPE, stderr=subprocess.STDOUT) as proc:
        banner = proc.stdout.read().decode('utf-8')

    r = re.search('Optimizing Compiler Version ([0-9.]+)', banner)
    if not r:
        raise BuildException('Unable to determine VC++ compiler version.')

    return r[1]

def query_p4(*args):
    with subprocess.Popen(['p4', '-G'] + list(args), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=False) as proc:
        data = proc.stdout.read()

    return marshal.loads(data)

def precheck_program(exe_name:str) -> bool:
    return try_invoke_program('where', '/q', exe_name) == 0

def simple_pattern_to_re(wcpat:str) -> str:
    last_star = False

    i = 0
    ln = len(wcpat)
    restr = ''
    while i < ln:
        ch = wcpat[i]
        i += 1

        if ch == '/' and wcpat[i:i+3] == '**/':
            i += 3

            restr += r'(?:/.*|)/'
        elif ch == '*':
            if i < ln and wcpat[i] == '*':
                restr += '.*'
                i += 1
            else:
                restr += r'[^/\:]*'
        else:
            restr += re.escape(ch)

    return restr

def common_suffix_len(s:str, t:str) -> int:
    n = min(len(s), len(t))

    for i in range(1, n + 1):
        if s[-i] != t[-i]:
            return i - 1

    return n

def pattern_to_re(ewcpat, id) -> (str,str):
    if isinstance(ewcpat, tuple):
        pat, rep = ewcpat
        suffix_len = common_suffix_len(pat, rep)

        if '*' in pat[0:-suffix_len] or '*' in rep[0:-suffix_len]:
            raise Exception('Wildcards found outside of common suffix: {} -> {}'.format(pat, rep))

        return (
            re.escape(pat[:-suffix_len]) + '(?P<r{}>'.format(id) + simple_pattern_to_re(pat[-suffix_len:]) + ')',
            rep[:-suffix_len]
        )
    else:
        return (simple_pattern_to_re(ewcpat), None)

def package_as_zip(output_path:str, selected_files:[(str, str)]) -> None:
    zipdirs = set()

    with zipfile.ZipFile(output_path, 'w', zipfile.ZIP_DEFLATED, compresslevel=1) as zf:
        t = time.time()

        file_num = 1
        for relpath, arcpath in selected_files:
            t2 = time.time()
            if t2 - t >= 0.1:
                t = t2
                log.log_status('{} - {} -> {}', file_num, relpath, arcpath)

            pos = 0
            while True:
                pos2 = arcpath.find('/', pos)
                if pos2 < 0:
                    break

                pos = pos2+1
                arcdir = arcpath[:pos]
                if arcdir not in zipdirs:
                    zipdirs.add(arcdir)

                    zf.writestr(arcdir, bytes())

            zf.write(relpath, arcpath)

            file_num += 1

        uncompressed_bytes = sum([info.file_size for info in zf.infolist()])
        zf.close()

    intermediate_size = os.path.getsize(output_path)
    log.log('Packaged {} bytes to {} bytes using .zip format: {}', uncompressed_bytes, intermediate_size, output_path)

    if opts.enable_packopt:
        log.log('Optimizing package with advzip')

        invoke_program('advzip', '-z', '-2', output_path)
        log.log('Optimized to {} bytes', os.path.getsize(output_path))

def package_as_7z(output_path:str, selected_files:[(str, str)]) -> None:
    log.log('Packaging {} files using 7-zip', len(selected_files))

    # 7-zip doesn't have a facility to rename files during add, so that's out of the
    # question. Even worse, 18.05 and 19.00 have a bug where any file symlinks on
    # windows are added as 0 byte files, so we can't work around this by symlinking
    # to a staging folder. We don't want to actually copy to staging if we can avoid
    # it, so we do this instead:
    #
    # - Archive using the -spf option so the archive contains absolute paths. This
    #   is awful, but it ensures that every source path has a unique archive path.
    #
    # - Use the 'rn' rename command to rename the absolute paths to the archive paths
    #   that we want. Happily, this does work with listfiles, taking the old and
    #   new paths on separate lines and avoiding a ridiculous command line or multiple
    #   passes.

    stage_path = os.path.join('publish', 'stage.txt')

    with open(stage_path, 'w') as f:
        for src_path, arc_path in selected_files:
            f.write(os.path.abspath(os.path.normpath(src_path))+'\n')

        f.close()

    try_remove(output_path)
    invoke_program(
        '7z',
        'a',
        '-sse',     # fail if there are errors on any files
        '-spf',     # store absolute paths in archive
        '-bso1',    # output messages to stdout
        '-bse1',    # error messages to stdout
        '-bsp1',    # progress messages to stdout
        '-mx=7',    # use high-ish compression settings
        '-mtr=off', # do not store attribute flags
        '-r-',      # do not recurse, we've already enumerated files individually
        output_path, '@'+stage_path)

    log.log('Renaming {} files', len(selected_files))

    # rename the files in the .7z directory to what we actually wanted them to be
    with open(stage_path, 'w') as f:
        for src_path, arc_path in selected_files:
            f.write(os.path.abspath(os.path.normpath(src_path))+'\n')
            f.write(arc_path+'\n')

        f.close()

    invoke_program('7z', 'rn', output_path, '@'+stage_path)

    log.log('Packaged to {} bytes using .7z format: {}', os.path.getsize(output_path), output_path)

    os.remove(stage_path)

def do_package(output_path:str, include_patterns:[str]) -> None:
    regexps = []
    rpats = []

    for ipat in include_patterns:
        pat, rep = pattern_to_re(ipat, len(rpats))

        if rep is not None:
            rpats.append(rep)

        regexps.append(pat)

    regexp = re.compile('|'.join(regexps), re.I)

    selected_files = list()

    scanned = 0

    for root, dirs, files in os.walk('.'):
        rinsed_root = root[2:].replace('\\', '/')

        scanned += len(files)

        for file in files:
            relpath = rinsed_root + '/' + file if rinsed_root else file
            match_res = regexp.fullmatch(relpath)
            if match_res:
                mgroup = match_res.lastgroup
                if mgroup:
                    selected_files.append((relpath, rpats[int(match_res.lastgroup[1:])] + match_res.group(match_res.lastgroup)))
                else:
                    selected_files.append((relpath, relpath))

    selected_files = sorted(selected_files, key=lambda sfile : str.casefold(sfile[1]))

    log.log('Selected {} files from {} files scanned', len(selected_files), scanned)

    if output_path.endswith('.7z'):
        package_as_7z(output_path, selected_files)
    else:
        package_as_zip(output_path, selected_files)

class DLLImportDatabase():
    def __init__(self):
        super().__init__()

    @staticmethod
    def load(path:str):
        dlls = {}

        with open(path, 'r') as f:
            for line in f.readlines():
                line = line.strip()

                if line:
                    dll, import_name = line.split(':')
                    dll = dll.lower()

                    dll_imports = dlls.get(dll)
                    if not dll_imports:
                        dll_imports = set()
                        dlls[dll] = dll_imports

                    dll_imports.add(import_name)

        db = DLLImportDatabase()
        db.dlls = dlls
        return db

def do_checkimports(bin_path:str, dllinfo_path:str) -> None:
    db = DLLImportDatabase.load(dllinfo_path)

    with subprocess.Popen(['dumpbin', '/imports', bin_path], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True) as proc:
        imports = proc.stdout.readlines()

    PARSE_START = 0
    PARSE_DLL_NAME = 1
    PARSE_FIND_IMPORTS = 2
    PARSE_IMPORTS = 3

    parse_state = PARSE_START
    dll_name = None
    dll_name:str

    dll_imports = None
    dll_imports:set

    import_re = re.compile(r'\s*[0-9a-f]+\s([^\s]+)', re.I)

    found_imports = False

    database_name = os.path.basename(dllinfo_path)

    for line in imports:
        line = line.rstrip()

        if parse_state == PARSE_START:
            if line.startswith('  Section contains the following imports:'):
                parse_state = PARSE_DLL_NAME
        elif parse_state == PARSE_DLL_NAME:
            if len(line) >= 5 and line.startswith('    ') and line[4] != ' ':
                dll_name = line[4:].strip().lower()

                dll_imports = db.dlls.get(dll_name)
                if dll_imports is None:
                    raise BuildException('{} imports DLL not in allowed DLL database: {} (using database {})'.format(os.path.basename(bin_path), dll_name, database_name))

                parse_state = PARSE_FIND_IMPORTS
        elif parse_state == PARSE_FIND_IMPORTS:
            if len(line.strip()) == 0:
                parse_state = PARSE_IMPORTS
        elif parse_state == PARSE_IMPORTS:
            match = import_re.fullmatch(line)
            if match:
                import_name = match.group(1)

                if import_name not in dll_imports:
                    raise BuildException('{} imports symbol not in allowed DLL database: {}:{} (using database {})'.format(os.path.basename(bin_path), dll_name, import_name, database_name))

                found_imports = True
            elif len(line.strip()) == 0:
                parse_state = PARSE_DLL_NAME

    if not found_imports:
        raise BuildException('No imports found while parsing dumpbin /exports output from: {}'.format(os.path.basename(bin_path)))

    log.log('Import check succeeded for: {}'.format(os.path.basename(bin_path)))

def banner() -> None:
    global opts

    if opts.enable_banner:
        opts.enable_banner = False

        print("Altirra Build Release Utility Version 4.00")
        print("Copyright (C) Avery Lee 2014-2021. Licensed under GNU General Public License, v2 or later")
        print()

def usage_error(*args) -> None:
    if len(args) > 1:
        args = args[0].format(*args[1:])

    banner()
    print(args)
    sys.exit(10)

def usage_exit() -> None:
    banner()

    print("""Usage: release.py [options] <version-id>

Options:
    /inc            Build incrementally
    /packonly       Pack only, do not build
    /nopackopt      Skip package optimization
    /anyvc          Disable VC++ version check
    /checkvc        Only do VC++ version check
    /importsonly    Only do DLL imports check
    /changelist     Override P4 changelist number
    /arm64          Enable ARM64 build""")

    sys.exit(5)

def main() -> None:
    global opts

    swchars = '/-'

    args = iter(sys.argv[1:])
    while True:
        try:
            arg = next(args)
        except StopIteration:
            break

        if arg[0] in swchars:
            if arg == '--':
                swchars = ''
                continue

            swname = arg[1:]

            try:
                if swname == 'inc':
                    opts.enable_clean = False
                elif swname == 'check':
                    opts.enable_package = False
                elif swname == 'packonly':
                    opts.enable_clean = False
                    opts.enable_build = False
                    opts.enable_checkimports = False
                elif swname == 'nopackopt':
                    opts.enable_packopt = False
                elif swname == 'anyvc':
                    opts.enable_vccheck = False
                elif swname == 'checkvc':
                    opts.enable_clean = False
                    opts.enable_build = False
                    opts.enable_checkimports = False
                    opts.enable_package = False
                    opts.enable_vccheck = True
                elif swname == 'importsonly':
                    opts.enable_clean = False
                    opts.enable_build = False
                    opts.enable_checkimports = True
                    opts.enable_package = False
                    opts.enable_vccheck = False
                elif swname == 'arm64':
                    opts.enable_arm64 = True
                elif swname == 'nologo':
                    opts.enable_banner = False
                elif swname == 'changelist':
                    opts.override_changelist = int(next(args))
                else:
                    usage_error('Unknown switch: {}', arg)
            except StopIteration:
                usage_error('Expected argument for switch: {}', arg)
        elif opts.version_id is None:
            opts.version_id = arg
        else:
            usage_error('Unexpected extra argument: {}', arg)

    banner()

    if opts.enable_package:
        if opts.version_id is None:
            usage_exit();

    try:
        if opts.enable_build:
            if not precheck_program('devenv.com'):
                raise BuildException('Unable to find Visual C++ IDE (devenv.com) in current path.')

        if opts.enable_package:
            if opts.enable_packopt:
                if not precheck_program('advzip.exe'):
                    raise BuildException('Unable to find AdvanceCOMP (advzip.exe) in current path.')

        if opts.enable_build:
            if opts.enable_vccheck:
                vs_ver = get_vs_version()

                if vs_ver != EXPECTED_MSVC_VERSION:
                    log.log('Error: Unexpected version of Visual C/C++ compiler.')
                    log.log()
                    log.log(' Expected: {} ({})', EXPECTED_MSVC_VERSION, EXPECTED_MSVC_VERSION_DESC)
                    log.log(' Detected: {}', vs_ver)
                    log.log()
                    log.log('If this is expected, use the /anyvc switch to override the version check.')
                    sys.exit(20)

                log.log('Using Visual C/C++ {}', vs_ver)

            try_remove(os.path.join('publish', 'build-x86.log'))
            try_remove(os.path.join('publish', 'build-x64.log'))
            try_remove(os.path.join('publish', 'build-arm64.log'))
            try_remove(os.path.join('publish', 'build-help.log'))

            if opts.enable_clean:
                log.log('Cleaning x86')
                invoke_vs('src\\Altirra.sln', '/Clean', 'Release|Win32')

                log.log('Cleaning x64')
                invoke_vs('src\\Altirra.sln', '/Clean', 'Release|x64')

                if opts.enable_arm64:
                    log.log('Cleaning ARM64')
                    invoke_vs('src\\Altirra.sln', '/Clean', 'Release|x64')
            else:
                log.log('Skipping clean due to /inc')

            if opts.override_changelist is not None:
                change_counter = opts.override_changelist
                log.log('Overriding changelist counter to {}', change_counter)
            else:
                log.log('Querying Perforce changelist number')

                change_counter = int(query_p4('counter', 'change')[b'value'].decode('utf-8'))

            # Compute version number.
            #
            # x.yy.z.w => x.yy comes from main version
            #            z is 101-199 for test releases, 200 for final release
            #            w is changelist number

            version_match = re.fullmatch(r'([0-9]+)\.([0-9]{2})(?:-test([0-9]+))?(.*)', opts.version_id)

            version_major = 0
            version_minor = 0
            version_branch = 0
            version_changelist = change_counter
            is_prerelease = False
            is_special = False

            if version_match:
                version_major = int(version_match.group(1))
                version_minor = int(version_match.group(2))

                test_number = version_match.group(3)
                if test_number:
                    version_branch = int(test_number) + 100
                    is_prerelease = True
                    version_type = "test"
                elif not version_match.group(4):
                    version_branch = 200
                    version_type = "release"
                else:
                    is_special = True
                    version_type = "special"
            else:
                is_special = True
                version_type = "special"

            string_version = '{}.{:02}.{}.{}'.format(version_major, version_minor, version_branch, version_changelist)
            numeric_version = '{},{},{},{}'.format(version_major, version_minor, version_branch, version_changelist)

            log.log('Stamping build as version {} ({})', string_version, version_type)

            os.makedirs(os.path.join('src', 'Altirra', 'autobuild'), exist_ok = True)

            version_file = os.path.join('src', 'Altirra', 'autobuild', 'version.h')
            with open(version_file, 'w') as f:
                f.write('#ifndef f_AT_VERSION_H\n')
                f.write('#define f_AT_VERSION_H\n')
                f.write('#define AT_VERSION "{}"\n'.format(opts.version_id))

                if is_prerelease or is_special:
                    f.write('#define AT_VERSION_PRERELEASE 1\n')

                f.write('#define AT_VERSION_COMPARE_VALUE (UINT64_C(0x{:04X}{:04X}{:04X}{:04X}))\n'.format(
                    version_major,
                    version_minor,
                    version_branch,
                    version_changelist)
                )

                f.write('#endif\n')
                f.close()

            atversionrc_file = os.path.join('src', 'Altirra', 'autobuild', 'atversionrc.h')
            with open(atversionrc_file, 'w') as f:
                f.write("""#ifndef f_AT_ATVERSIONRC_H
#define f_AT_ATVERSIONRC_H

#define AT_WIN_VERSIONBLOCK_FILEVERSION {numeric_version}
#define AT_WIN_VERSIONBLOCK_FILEVERSION_STR "{string_version}"
#define AT_WIN_VERSIONBLOCK_PRODUCTVERSION {numeric_version}
#define AT_WIN_VERSIONBLOCK_PRODUCTVERSION_STR "{string_version}"

#endif
""".format(numeric_version = numeric_version, string_version = string_version))

            try:
                build_switch = '/Rebuild' if opts.enable_clean else '/Build'

                log.log('Building x86')
                invoke_vs('src\\Altirra.sln', build_switch, 'Release|Win32', '/Out', 'publish\\build-x86.log')

                log.log('Building x64')
                invoke_vs('src\\Altirra.sln', build_switch, 'Release|x64', '/Out', 'publish\\build-x64.log')

                if opts.enable_arm64:
                    log.log('Building ARM64')
                    invoke_vs('src\\Altirra.sln', build_switch, 'Release|ARM64', '/Out', 'publish\\build-arm64.log')

                log.log('Building help file')
                invoke_vs('src\\ATHelpFile.sln', build_switch, 'Release', '/Out', 'publish\\build-help.log')
            finally:
                os.remove(version_file)
                os.remove(atversionrc_file)
        else:
            log.log('Skipping build due to /packonly')

        if opts.enable_checkimports:
            log.log('Checking DLL imports')

            do_checkimports(os.path.normpath('out/Release/Altirra.exe'), os.path.normpath('scripts/dllinfo/win7x64_x86.exports'))
            do_checkimports(os.path.normpath('out/ReleaseAMD64/Altirra64.exe'), os.path.normpath('scripts/dllinfo/win7x64_x64.exports'))

        if opts.enable_package:
            log.log('Packaging source')

            src_filename_patterns = [
                '*.vcxproj',
                '*.vcxproj.filters',
                '*.sln',
                '*.cpp',
                '*.h',
                '*.fx',
                '*.props',
                '*.xml',
                '*.targets',
                '*.asm',
                '*.xasm',
                '*.rc',
                '*.asm64',
                '*.inl',
                '*.fxh',
                '*.vdfx',
                '*.inc',
                '*.k',
                '*.txt',
                '*.bmp',
                '*.png',
                '*.jpg',
                '*.ico',
                '*.cur',
                '*.manifest',
                '*.s',
                '*.pcm',
                '*.bas',
                '*.html',
                '*.natvis',
                '*.vs',
                '*.ps',
                '*.vsh',
                '*.psh',
                '*.cmd',
                '*.svg',
                '*.atcpengine',
                '*.py',
                '*.json',
                '*.ps1',
                '*.flac',
                'Makefile'
            ]

            src_patterns = []

            for dir in ['assets', 'scripts', 'src', 'testdata']:
                src_patterns.extend([dir + '/**/' + pat for pat in src_filename_patterns])

            src_patterns.extend([
                'Copying',
                'release.py',
                'src/.editorconfig',
                'src/BUILD-HOWTO.html',
                'src/Kasumi/data/Tuffy.*',
                'src/Kernel/source/shared/atarifont.bin',
                'src/Kernel/source/shared/atariifont.bin',
                'src/ATHelpFile/source/*.xml',
                'src/ATHelpFile/source/*.xsl',
                'src/ATHelpFile/source/*.css',
                'src/ATHelpFile/source/*.hhp',
                'src/ATHelpFile/source/*.hhw',
                'src/ATHelpFile/source/*.hhc',
                'src/Altirra/res/altirraexticons.res',
                'localconfig/example/*.props',
                'dist/**',
                'testdata/**'
            ])

            do_package(os.path.join('publish', 'Altirra-{}-src.7z'.format(opts.version_id)),
                src_patterns
            )

            bin_common_patterns = [
                'Copying',
                ('out/Helpfile/Altirra.chm', 'Altirra.chm'),
                ('out/Release/Additions.atr', 'Additions.atr')
            ]

            do_package(os.path.join('publish', 'Altirra-{}.zip'.format(opts.version_id)),
                [
                    ('out/Release/Altirra.exe', 'Altirra.exe'),
                    ('out/ReleaseAMD64/Altirra64.exe', 'Altirra64.exe'),
                    ('dist/**', '**')
                ] + bin_common_patterns
            )

            if opts.enable_arm64:
                do_package(os.path.join('publish', 'Altirra-{}-ARM64.zip'.format(opts.version_id)),
                    [
                        ('out/releasearm64/AltirraARM64.exe', 'AltirraARM64.exe'),
                    ] + bin_common_patterns
                )

            log.log('Copying symbols')

            shutil.copyfile(os.path.join('out', 'Release', 'Altirra.pdb'), os.path.join('publish', 'Altirra-{}.pdb'.format(opts.version_id)))
            shutil.copyfile(os.path.join('out', 'ReleaseAMD64', 'Altirra64.pdb'), os.path.join('publish', 'Altirra64-{}.pdb'.format(opts.version_id)))

            if opts.enable_arm64:
                shutil.copyfile(os.path.join('out', 'ReleaseARM64', 'AltirraARM64.pdb'), os.path.join('publish', 'AltirraARM64-{}.pdb'.format(opts.version_id)))

        log.log('Build succeeded')
    except BuildException as e:
        log.log('Build failed: ' + str(e))
        sys.exit(20)

if __name__ == '__main__':
    main()
