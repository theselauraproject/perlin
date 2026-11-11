#pragma once
#include "perlin/abi.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace perlin::abi::host {
    template <class T> using Result = std::expected<T, Status>;

    template <Op O, class Ctx, class Fn> auto Typed(Fn&& fn) {
        using Req = typename OpSpec<O>::Request;
        using Res = typename OpSpec<O>::Response;
        static_assert(!std::is_same_v<Req, abi::Raw> && !std::is_same_v<Res, abi::Raw>,
                      "a raw op takes a hand-written handler");
        return [fn = std::forward<Fn>(fn)](Ctx& ctx, Reader& in, Writer& out) -> Status {
            [[maybe_unused]] Req request{};
            if constexpr (!std::is_same_v<Req, Nothing>) {
                request = in.read<Req>();
                if (!in.Ok())
                    return Status::BadArguments;
            }
            const auto invoke = [&]() -> decltype(auto) {
                if constexpr (std::is_same_v<Req, Nothing>)
                    return fn(ctx);
                else
                    return fn(ctx, std::as_const(request));
            };
            using Ret = decltype(invoke());
            if constexpr (std::is_void_v<Ret>) {
                invoke();
                return Status::Ok;
            } else if constexpr (std::is_same_v<Ret, Status>) {
                return invoke();
            } else if constexpr (requires(Ret r) {
                                     r.has_value();
                                     r.error();
                                 }) {
                auto r = invoke();
                if (!r)
                    return r.error();
                if constexpr (!std::is_same_v<Res, Nothing>)
                    out.write(*r);
                return Status::Ok;
            } else {
                if constexpr (std::is_same_v<Res, Nothing>)
                    invoke();
                else
                    out.write(invoke());
                return Status::Ok;
            }
        };
    }

    template <class Ctx> class OpTable {
    public:
        using RawHandler = std::function<Status(Ctx&, Reader&, Writer&)>;

        void Raw(Op op, RawHandler handler, uint32_t permission) {
            handlers[static_cast<uint32_t>(op)] = {std::move(handler), permission};
        }
        void Raw(Op op, RawHandler handler) { Raw(op, std::move(handler), PermissionOf(op)); }

        template <Op O, class Fn> void Handle(Fn&& fn) {
            Raw(O, Typed<O, Ctx>(std::forward<Fn>(fn)), OpSpec<O>::kPermission);
        }

        bool Has(uint32_t op) const { return handlers.contains(op); }

        Status Call(Ctx& ctx, uint32_t op, std::span<const uint8_t> in, Writer& out, uint32_t granted) const {
            const auto it = handlers.find(op);
            if (it == handlers.end())
                return Status::UnknownOp;
            if ((it->second.permission & granted) != it->second.permission)
                return Status::Denied;
            Reader reader(in.data(), in.size());
            return it->second.handler(ctx, reader, out);
        }

    private:
        struct Entry {
            RawHandler handler;
            uint32_t permission = 0;
        };
        std::unordered_map<uint32_t, Entry> handlers;
    };

    template <Event E> Writer Encode(const payload_t<E>& payload) {
        Writer w;
        if constexpr (!std::is_same_v<payload_t<E>, EventNothing>)
            w.write(payload);
        return w;
    }

    template <class T> bool DecodesExactly(std::span<const uint8_t> bytes) {
        if constexpr (std::is_same_v<T, Nothing>) {
            return bytes.empty();
        } else {
            Reader r(bytes.data(), bytes.size());
            T value{};
            r.read_into(value);
            return r.Ok() && r.Remaining() == 0;
        }
    }

    namespace detail {
        template <bool Response, size_t... I>
        bool MatchesSchema(Op op, std::span<const uint8_t> bytes, std::index_sequence<I...>) {
            bool known = false, ok = false;
            (void)((kAllOps[I] == op
                        ? (known = true,
                           ok = DecodesExactly<std::conditional_t<Response, typename OpSpec<kAllOps[I]>::Response,
                                                                  typename OpSpec<kAllOps[I]>::Request>>(bytes),
                           true)
                        : false) ||
                   ...);
            return known && ok;
        }
    }

    inline bool RequestMatchesSchema(Op op, std::span<const uint8_t> bytes) {
        return detail::MatchesSchema<false>(op, bytes, std::make_index_sequence<kAllOps.size()>{});
    }
    inline bool ResponseMatchesSchema(Op op, std::span<const uint8_t> bytes) {
        return detail::MatchesSchema<true>(op, bytes, std::make_index_sequence<kAllOps.size()>{});
    }
}
