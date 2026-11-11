// Perlin web sign-in relay: a Cloudflare Worker (free plan is plenty).
//
// Microsoft sign-in, Xbox Live, Realms and Minecraft services answer browsers without CORS headers, so a page on
// GitHub Pages (or any other static host) cannot call them. The page sends those requests here instead, as
//   https://<this worker>/<host>/<path>?<query>
// and the worker forwards them to https://<host>/<path>?<query> and adds the CORS headers on the way back.
// Only the hosts below are forwarded, and only for the origins in ALLOWED_ORIGINS (wrangler.toml), so this is not
// an open proxy. Nothing is stored or logged.

const HOSTS = ["login.live.com", "account.xbox.com", ".xboxlive.com", ".minecraft.net", ".minecraft-services.net",
               ".playfabapi.com"];
const DROP = new Set(["host", "origin", "referer", "cookie", "cf-connecting-ip", "cf-ipcountry", "cf-ray",
                      "cf-visitor", "x-forwarded-for", "x-forwarded-proto", "x-real-ip", "content-length"]);

function allowedHost(host) {
    return HOSTS.some((h) => host === h || (h.startsWith(".") && (host.endsWith(h) || host === h.slice(1))));
}

function allowedOrigin(origin, env) {
    const list = (env.ALLOWED_ORIGINS || "*").split(",").map((s) => s.trim()).filter(Boolean);
    return list.includes("*") || list.includes(origin);
}

function cors(origin, request) {
    return {
        "Access-Control-Allow-Origin": origin || "*",
        "Access-Control-Allow-Methods": "GET, POST, PUT, DELETE, PATCH, OPTIONS",
        "Access-Control-Allow-Headers": request.headers.get("Access-Control-Request-Headers") || "*",
        "Access-Control-Expose-Headers": "*",
        "Access-Control-Max-Age": "86400",
        "Cross-Origin-Resource-Policy": "cross-origin",
        "Vary": "Origin",
    };
}

export default {
    async fetch(request, env) {
        const origin = request.headers.get("Origin") || "";
        if (origin && !allowedOrigin(origin, env))
            return new Response("origin not allowed", { status: 403 });
        if (request.method === "OPTIONS")
            return new Response(null, { status: 204, headers: cors(origin, request) });

        const url = new URL(request.url);
        if (url.pathname === "/ping")
            return new Response("perlin-relay", { headers: cors(origin, request) });
        const slash = url.pathname.indexOf("/", 1);
        const host = url.pathname.slice(1, slash < 0 ? undefined : slash);
        if (!allowedHost(host))
            return new Response("not a sign-in host", { status: 403, headers: cors(origin, request) });
        const target = "https://" + host + (slash < 0 ? "/" : url.pathname.slice(slash)) + url.search;

        const headers = new Headers();
        for (const [k, v] of request.headers)
            if (!DROP.has(k.toLowerCase()))
                headers.set(k, v);
        // browsers cannot set a user agent; some endpoints want one
        if (!headers.has("User-Agent"))
            headers.set("User-Agent", "Perlin");

        const upstream = await fetch(target, {
            method: request.method,
            headers,
            body: request.method === "GET" || request.method === "HEAD" ? undefined : await request.arrayBuffer(),
            redirect: "manual",
        });
        const out = new Headers(upstream.headers);
        out.delete("set-cookie");
        for (const [k, v] of Object.entries(cors(origin, request)))
            out.set(k, v);
        return new Response(upstream.body, { status: upstream.status, statusText: upstream.statusText, headers: out });
    },
};
