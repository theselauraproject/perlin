#pragma once
#include <array>
#include <cstdint>
#include <expected>
#include <new>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "perlin/abi.hpp"

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
namespace perlin {
    using std::expected;
    using std::unexpected;
}
#else
namespace perlin {
    template <class E> class unexpected {
    public:
        constexpr explicit unexpected(E e) : err(std::move(e)) {}
        constexpr const E& error() const { return err; }

    private:
        E err;
    };
    template <class E> unexpected(E) -> unexpected<E>;

    template <class T, class E> class expected {
    public:
        constexpr expected(T v) : val(std::move(v)), ok(true) {}
        template <class G> constexpr expected(const unexpected<G>& u) : err(u.error()), ok(false) {}
        constexpr bool has_value() const { return ok; }
        constexpr explicit operator bool() const { return ok; }
        constexpr T& operator*() & { return val; }
        constexpr const T& operator*() const& { return val; }
        constexpr T&& operator*() && { return std::move(val); }
        constexpr T* operator->() { return &val; }
        constexpr const T* operator->() const { return &val; }
        constexpr T& value() & { return val; }
        constexpr const T& value() const& { return val; }
        constexpr const E& error() const { return err; }
        template <class U> constexpr T value_or(U&& fallback) const& {
            return ok ? val : static_cast<T>(std::forward<U>(fallback));
        }

    private:
        T val{};
        E err{};
        bool ok;
    };

    template <class E> class expected<void, E> {
    public:
        constexpr expected() : ok(true) {}
        template <class G> constexpr expected(const unexpected<G>& u) : err(u.error()), ok(false) {}
        constexpr bool has_value() const { return ok; }
        constexpr explicit operator bool() const { return ok; }
        constexpr void operator*() const {}
        constexpr const E& error() const { return err; }

    private:
        E err{};
        bool ok;
    };
}
#endif

namespace perlin {
    using abi::Op;
    using abi::Status;

    struct Error {
        Status status = Status::Failed;

        constexpr std::string_view message() const {
            constexpr std::array<std::pair<Status, std::string_view>, 7> table{{
                {Status::Ok, "ok"},
                {Status::UnknownOp, "this game version does not provide that operation"},
                {Status::Denied, "permission denied"},
                {Status::BadArguments, "bad arguments"},
                {Status::Unavailable, "not available right now"},
                {Status::NotFound, "not found"},
                {Status::Failed, "failed"},
            }};
            static_assert(table.size() == meta::enum_count<Status>, "every Status needs a message");
            for (const auto& [code, text] : table)
                if (code == status)
                    return text;
            return "failed";
        }
        constexpr std::string_view name() const { return meta::enum_name(status); }
        constexpr bool is(Status s) const { return status == s; }
        constexpr bool denied() const { return is(Status::Denied); }
        constexpr bool unavailable() const { return is(Status::Unavailable); }
        constexpr bool not_found() const { return is(Status::NotFound); }
        constexpr bool unsupported() const { return is(Status::UnknownOp); }
        friend constexpr bool operator==(Error, Error) = default;
    };

    template <class T> using Result = expected<T, Error>;

    namespace detail {
        extern "C" {
#if defined(__wasm__)
        __attribute__((import_module("perlin"), import_name("call")))
#endif
        int32_t
        perlin_host_call(uint32_t op, const void* in, uint32_t inLen, void* out, uint32_t outCap);
        }

        inline Result<std::vector<uint8_t>> call(Op op, const abi::Writer& args) {
            std::vector<uint8_t> out(2048);
            for (int attempt = 0; attempt < 4; attempt++) {
                const int32_t rc = perlin_host_call(static_cast<uint32_t>(op), args.Data(),
                                                    static_cast<uint32_t>(args.Size()), out.data(),
                                                    static_cast<uint32_t>(out.size()));
                if (rc >= 0) {
                    out.resize(static_cast<size_t>(rc));
                    return out;
                }
                if (rc < -abi::kNeedMoreBase) {
                    out.resize(static_cast<size_t>(-rc - abi::kNeedMoreBase));
                    continue;
                }
                return unexpected(Error{static_cast<Status>(rc)});
            }
            return unexpected(Error{Status::Failed});
        }

        inline Result<std::vector<uint8_t>> call(Op op) {
            return call(op, abi::Writer{});
        }

        template <class T> Result<T> call_as(Op op, const abi::Writer& args) {
            auto bytes = call(op, args);
            if (!bytes)
                return unexpected(bytes.error());
            abi::Reader r(bytes->data(), bytes->size());
            T value = r.read<T>();
            if (!r.Ok())
                return unexpected(Error{Status::Failed});
            return value;
        }

        template <class T> Result<T> call_as(Op op) {
            return call_as<T>(op, abi::Writer{});
        }

        template <class T> Result<std::vector<T>> call_list(Op op, const abi::Writer& args) {
            auto bytes = call(op, args);
            if (!bytes)
                return unexpected(bytes.error());
            abi::Reader r(bytes->data(), bytes->size());
            auto list = r.read<std::vector<T>>();
            if (!r.Ok())
                return unexpected(Error{Status::Failed});
            return list;
        }

        template <class T> Result<std::vector<T>> call_list(Op op) {
            return call_list<T>(op, abi::Writer{});
        }

        inline bool call_bool(Op op, const abi::Writer& args) {
            auto bytes = call(op, args);
            return bytes && !bytes->empty() && (*bytes)[0] != 0;
        }

        inline Result<void> call_void(Op op, const abi::Writer& args) {
            auto bytes = call(op, args);
            if (!bytes)
                return unexpected(bytes.error());
            return {};
        }

        inline Result<void> call_void(Op op) {
            return call_void(op, abi::Writer{});
        }
    }

    namespace detail {
        template <class T>
        using call_result_t =
            std::conditional_t<std::is_same_v<T, abi::Nothing>, void,
                               std::conditional_t<std::is_same_v<T, abi::Raw>, std::vector<uint8_t>, T>>;

        template <abi::Op O>
        Result<call_result_t<typename abi::OpSpec<O>::Response>> call_encoded(const abi::Writer& args) {
            using Res = typename abi::OpSpec<O>::Response;
            if constexpr (std::is_same_v<Res, abi::Nothing>)
                return call_void(O, args);
            else if constexpr (std::is_same_v<Res, abi::Raw>)
                return call(O, args);
            else
                return call_as<Res>(O, args);
        }
    }

    // any host function from abi_table.h, typed by its table entry:
    //   call<Op::PlayerState>()                       -> Result<abi::PlayerState>
    //   call<Op::Broadcast>({.text = "hi"})           -> Result<void>
    //   call<Op::Block>(writer)                       raw ops take the bytes they were always given
    template <abi::Op O>
        requires std::is_same_v<typename abi::OpSpec<O>::Request, abi::Nothing>
    auto call() {
        return detail::call_encoded<O>(abi::Writer{});
    }

    template <abi::Op O>
        requires std::is_same_v<typename abi::OpSpec<O>::Request, abi::Raw>
    auto call(const abi::Writer& args) {
        return detail::call_encoded<O>(args);
    }

    template <abi::Op O, class Req = typename abi::OpSpec<O>::Request>
        requires(!std::is_same_v<Req, abi::Nothing> && !std::is_same_v<Req, abi::Raw>)
    auto call(const Req& request) {
        abi::Writer w;
        w.write(request);
        return detail::call_encoded<O>(w);
    }
}

template <> struct std::formatter<perlin::Error> : std::formatter<std::string_view> {
    template <class Ctx> auto format(const perlin::Error& e, Ctx& ctx) const {
        return std::formatter<std::string_view>::format(e.message(), ctx);
    }
};
