#pragma once

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/socket.h>
#define PERLIN_HAS_IFADDRS 1
#else
struct sockaddr;
#endif

namespace perlin::net {
    struct InterfaceAddress {
        const sockaddr* address = nullptr;
        const sockaddr* broadcast = nullptr;
        bool up = false;
        bool loopback = false;
        bool canBroadcast = false;
    };

    template <class Fn> void ForEachInterface(Fn&& fn) {
#ifdef PERLIN_HAS_IFADDRS
        ifaddrs* list = nullptr;
        if (getifaddrs(&list) != 0)
            return;
        for (const ifaddrs* a = list; a; a = a->ifa_next)
            fn(InterfaceAddress{a->ifa_addr, a->ifa_broadaddr, (a->ifa_flags & IFF_UP) != 0,
                                (a->ifa_flags & IFF_LOOPBACK) != 0, (a->ifa_flags & IFF_BROADCAST) != 0});
        freeifaddrs(list);
#else
        (void)fn;
#endif
    }
}
