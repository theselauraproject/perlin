from ..common import upper


def generate(api):
    out = ['#pragma once', '', '#include <cstdint>', '', 'namespace perlin::abi::coverage {', '']
    for host in ('client', 'server'):
        ops = [o for o in sorted(api.ops, key=lambda o: o.id) if host in o.hosts]
        evs = [e for e in sorted(api.events, key=lambda e: e.id) if host in e.hosts]
        out.append(f'inline constexpr std::uint32_t k{host.capitalize()}Ops[] = {{')
        out += [f'    0x{o.id:04X},' for o in ops]
        out.append('};')
        out.append(f'inline constexpr std::uint32_t k{host.capitalize()}Events[] = {{')
        out += [f'    {e.id},' for e in evs]
        out.append('};')
        out.append('')
    out += ['}', '']
    return {'sdk/perlin/gen/coverage.hpp': '\n'.join(out)}
