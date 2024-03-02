# Altirra - Atari 800/800XL/5200 emulator
# Utility script - Win32 icon/cursor creator
# Copyright (C) 2009-2020 Avery Lee
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

import glob
import os
import sys
import struct
import json
from PIL import Image

PNG_HEADER = b'\x89PNG\x0D\x0A\x1A\x0A'
RT_GROUP_CURSOR = 12
RT_CURSOR = 1

def main():
    desc_name = sys.argv[1]
    root_dir = os.path.dirname(desc_name)

    with open(desc_name, 'r') as f:
        desc = json.load(f)

    output = os.path.join(root_dir, desc['output_path'])
    print('Using output path: {}'.format(output))

    cursor_format = False
    if output.lower()[-4:] == '.cur':
        cursor_format = True

    img_count = len(desc['images'])
    img_offset = 6 + 16*img_count
    img_data = bytearray()

    header = bytearray(struct.pack('<HHH', 0, 2 if cursor_format else 1, img_count))

    for image_desc in desc['images']:
        src_name = os.path.join(root_dir, image_desc['path'])

        with open(src_name, 'rb') as f:
            src_data = f.read()

        if src_data[0:8] != PNG_HEADER or src_data[12:16] != b'IHDR':
            raise Exception('Source file {} not in PNG format'.format(src_name))

        width, height = struct.unpack('>II', src_data[16:24])
        print('{}x{} {}'.format(width, height, src_name))

        start_offset = len(img_data) + img_offset

        img_data += struct.pack('<IIIHHIIIIII', 40, width, height*2, 1, 32, 0, 0, 0, 0, 0, 0)

        and_mask_pitch = ((width + 31) >> 5) * 4
        and_mask = bytearray(b'\xFF' * (and_mask_pitch * height))

        with Image.open(src_name) as img:
            px = img.load()
            for y in range(height-1, -1, -1):
                for x in range(0, width):
                    r,g,b,a = px[x,y]
                    img_data += struct.pack('<BBBB', b, g, r, a)

                    if a >= 128:
                        offset = ((height - 1) - y) * and_mask_pitch + (x >> 3)
                        and_mask[offset] -= (0x80 >> (x & 7))

        # AND mask (not required, but helpful for compatibility)
        img_data += and_mask

        end_offset = len(img_data) + img_offset

        if cursor_format:
            hotspot_x = int(image_desc['hotspot_x'])
            hotspot_y = int(image_desc['hotspot_y'])
            header += struct.pack('<BBBBHHII', width & 0xFF, height & 0xFF, 0, 0, hotspot_x, hotspot_y, end_offset - start_offset, start_offset)
        else:
            header += struct.pack('<BBBBHHII', width & 0xFF, height & 0xFF, 0, 0, 1, 32, end_offset - start_offset, start_offset)

        img_count += 1

    with open(output, 'wb') as f:
        f.write(header)
        f.write(img_data)
        f.close()

if __name__ == "__main__":
    main()
