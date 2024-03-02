# Altirra - Atari 800/800XL/5200 emulator
# Utility script - save state test generator
# Copyright (C) 2009-2021 Avery Lee
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
# You should have received a copy of the GNU General Public License along
# with this program. If not, see <http://www.gnu.org/licenses/>.

import sys
import os
import random

if len(sys.argv) != 4:
    print("Usage: statetest.py <output-dir-path> <interval-count> <interval-len>")
    sys.exit(5)

output_base_path = sys.argv[1]
interval_count = int(sys.argv[2])
interval_len = int(sys.argv[3])

random_seed = 1

def raw_random():
    """
    Generate a deterministic sequence using Marsaglia's xorshift32 algorithm.
    """

    seed = random_seed

    while True:
        seed ^= seed << 13
        seed ^= seed >> 17
        seed ^= seed << 5
        yield seed

def random_gen2(rgen, val_mask, val_start, val_range):
    seq = iter(rgen)

    while True:
        while True:
            v = next(seq) & val_mask
            if v < val_range:
                break

        yield v + val_start

def random_gen(val_start, val_end):
    val_range = val_end - val_start
    limit_bit = 1
    while limit_bit <= val_range:
        limit_bit += limit_bit

    mask = limit_bit - 1

    return random_gen2(raw_random(), mask, val_start, val_range)

print("Using {} intervals of {} cycles".format(interval_count, interval_len))

offsets = [r+x*interval_len for r,x in zip(random_gen(1, interval_len-1),range(0, interval_count))]
delays = [offsets[i] - offsets[i-1] for i in range(1,len(offsets))]

savestate_ref_path = os.path.join(output_base_path, 'ref')
savestate_chk_path = os.path.join(output_base_path, 'chk')

os.makedirs(savestate_ref_path, exist_ok=True)
os.makedirs(savestate_chk_path, exist_ok=True)

with open(os.path.join(output_base_path, 'ref.atdbgscript'), 'w') as f:
    f.write('.logopen "{}"\n'.format(os.path.join(savestate_ref_path, 'ref.txt')))
    f.write('gcr {}\n'.format(8000000))

    for i,delay in enumerate(delays):
        f.write('.echo "=== {:04d} ==="\n'.format(i))
        f.write('gcr {}\n'.format(delay))
        f.write('h\n')
        f.write('.savestate "{}"\n'.format(os.path.join(savestate_ref_path, "{:04d}.atstate2".format(i))))

    f.write('.logclose\n')

with open(os.path.join(output_base_path, 'chk.atdbgscript'), 'w') as f:
    for i,delay in enumerate(delays):
        if i & 1:
            f.write('gcr {}\n'.format(delay))
            f.write('.savestate "{}"\n'.format(os.path.join(savestate_chk_path, "{:04d}.atstate2".format(i))))
            f.write('gcr {}\n'.format(2000000))
        else:
            f.write('.echo "=== {:04d} ==="\n'.format(i))
            f.write('.loadstate "{}"\n'.format(os.path.join(savestate_ref_path, "{:04d}.atstate2".format(i))))
