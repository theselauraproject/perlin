"""Turn a binary file into a c++ source with the bytes as an array: embed_bytes.py <in> <out.cpp> <symbol>"""
import sys

src, dst, symbol = sys.argv[1:4]
data = open(src, 'rb').read()
lines = []
for i in range(0, len(data), 24):
    lines.append(','.join(str(b) for b in data[i:i + 24]))
with open(dst, 'w') as f:
    f.write('#include <cstddef>\n#include <cstdint>\nnamespace perlin::server {\n')
    f.write(f'extern const uint8_t {symbol}[] = {{\n' + ',\n'.join(lines) + '};\n')
    f.write(f'extern const size_t {symbol}Size = {len(data)};\n}}\n')
