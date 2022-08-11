#!/usr/bin/env python3
# convert uuid to C structure. couldn't find any way to generate guid structure on linux so made this uwu
# usage
# ./uuidconv.py a3c5b77a-c88f-4a93-bf1c-4a92a32c65ce

import sys

uuid = sys.argv[1].replace("-","")
print(f"{{0x{uuid[:8]}, 0x{uuid[8:12]}, 0x{uuid[12:16]}, {{0x{uuid[16:18]}, 0x{uuid[18:20]}, 0x{uuid[20:22]}, 0x{uuid[22:24]}, 0x{uuid[24:26]}, 0x{uuid[26:28]}, 0x{uuid[28:30]}, 0x{uuid[30:32]}}}}};")