import json
import os
import sys

def literal(text):
    out = []
    for i in range(0, len(text), 2000):
        piece = text[i:i + 2000].replace('\\', '\\\\').replace('"', '\\"')
        out.append('"' + piece + '"')
    return '\n        '.join(out)

def main():
    if len(sys.argv) != 3:
        print('usage: gen_loot_tables.py <java data/minecraft dir> <output .cpp>')
        return 2
    root, target = sys.argv[1], sys.argv[2]
    entries = []
    for kind in ('blocks', 'entities'):
        base = os.path.join(root, 'loot_table', kind)
        for dirpath, _, files in os.walk(base):
            for f in sorted(files):
                if not f.endswith('.json'):
                    continue
                rel = os.path.relpath(os.path.join(dirpath, f), os.path.join(root, 'loot_table'))[:-5].replace(os.sep, '/')
                with open(os.path.join(dirpath, f), encoding='utf-8') as fh:
                    data = json.load(fh)
                data.pop('random_sequence', None)
                entries.append((rel, json.dumps(data, separators=(',', ':'), sort_keys=True)))
    for name in ('tool/can_silk_touch', 'tool/can_shear'):
        with open(os.path.join(root, 'predicate', name + '.json'), encoding='utf-8') as fh:
            entries.append(('predicate/' + name, json.dumps(json.load(fh), separators=(',', ':'), sort_keys=True)))
    for tag in ('cluster_max_harvestables',):
        with open(os.path.join(root, 'tags', 'item', tag + '.json'), encoding='utf-8') as fh:
            entries.append(('tag/' + tag, json.dumps(json.load(fh), separators=(',', ':'), sort_keys=True)))
    entries.sort()
    lines = ['#include "world/Loot.hpp"', '', 'namespace perlin::server {', '    extern const LootSource kLootSources[] = {']
    for key, text in entries:
        lines.append('        {"' + key + '",')
        lines.append('        ' + literal(text) + '},')
    lines.append('    };')
    lines.append('    extern const int kLootSourceCount = ' + str(len(entries)) + ';')
    lines.append('}')
    lines.append('')
    with open(target, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write('\n'.join(lines))
    print('wrote', len(entries), 'tables to', target)
    return 0

if __name__ == '__main__':
    sys.exit(main())
