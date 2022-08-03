#!/usr/bin/env python3
# convert uuid to C structure
# usage
# ./uuidconv.py EC87D643-EBA4-4BB5-A1E5-3F3E36B20DA9

import sys

uuid = sys.argv[1].replace("-","")
print(f"{{0x{uuid[:8]}, 0x{uuid[8:12]}, 0x{uuid[12:16]}, {{0x{uuid[16:18]}, 0x{uuid[18:20]}, 0x{uuid[20:22]}, 0x{uuid[22:24]}, 0x{uuid[24:26]}, 0x{uuid[26:28]}, 0x{uuid[28:30]}, 0x{uuid[30:32]}}}}};")