# altirra
Altirra Atari 800 emulator - unofficial mirror

This branch contains my modifications, like this readme. For the branch with no changes see [upstream](https://github.com/joelsgp/altirra/tree/upstream)

## Index of documentation files
* Main build instructions - [src/README.md](src/README.md)
* Extras - listing and license - [dist/extras/README.md](dist/extras/README.md)
* HTML/CHM help file build info - [src/ATHelpFile/README.md](src/ATHelpFile/README.md)
* Altirra BASIC internals detailing - [src/ATBasic/doc/internals.md](src/ATBasic/doc/internals.md)
* `src/Altirra/res/*.txt`
    * Dedication and credits - [src/Altirra/res/about.md](src/Altirra/res/about.md)
    * Changelog - [src/Altirra/res/CHANGELOG.md](src/Altirra/res/CHANGELOG.md)
    * Altirra terminal commands docs - [src/Altirra/res/cmdhelp.txt](src/Altirra/res/cmdhelp.txt)
    * Debugger commands docs - [src/Altirra/res/dbghelp.txt](src/Altirra/res/dbghelp.txt)
    * Replacement ROM info - [src/Altirra/res/romset.md](src/res/romset.md)
* Extensive Hardware and BASIC docs pdfs on the upstream website
    * <https://www.virtualdub.org/downloads/Altirra%20Hardware%20Reference%20Manual.pdf>
    * <https://www.virtualdub.org/downloads/Altirra%20BASIC%20Reference%20Manual.pdf>

Here's the script I used to make the source code downloads on the website into a Git history:
```bash
#!/bin/bash
set -eux

zips=(1.2 1.3 1.4 1.5 1.6 1.7 1.8 1.9
2.00 2.10 2.20 2.30 2.40 2.50 2.60 2.70 2.71 2.80 2.81 2.90
3.00 3.10 3.20 3.90 3.91)

dl_77z=(4.00 4.01 4.10 4.20)

mkdir repo
git -C repo init

do_repo_update () {
	filename="$1"
	wget --no-clobber "https://virtualdub.org/downloads/$filename"
	bsdtar -xf "$filename" --directory repo
	git -C repo add .
	git -C repo commit -m "Altirra $ver"
}

for ver in "${zips[@]}"
do
	do_repo_update "Altirra-$ver-src.zip"
done

for ver in "${dl_77z[@]}"
do
	do_repo_update "Altirra-$ver-src.7z"
done
```
