# Sign-in relay for the web build

Microsoft sign-in, Xbox Live, Realms and the Minecraft services send no CORS headers, so a browser page cannot
call them directly. The web build sends those requests (and only those) through a relay that forwards them and adds
the headers. `serve.py` has one built in (`/__relay-https/`), so local hosting needs nothing. For a static host
such as GitHub Pages, deploy this Cloudflare Worker once (the free plan is enough):

```sh
cd tools/web/auth-relay
npx wrangler login
npx wrangler deploy            # prints https://perlin-auth-relay.<you>.workers.dev
```

Set `ALLOWED_ORIGINS` in `wrangler.toml` to your site (`https://you.github.io`) so nobody else can use it.

Then tell the page where it is, in any of these ways:

- CI: add the repository variable `WEB_AUTH_RELAY` = the worker URL; every web build then ships a
  `perlin-config.json` with `{"authRelay": "<url>"}`.
- By hand: put that `perlin-config.json` beside `index.html`.
- For a quick try: open the page with `?relay=<url>`.

The relay forwards only to `login.live.com`, `account.xbox.com`, `*.xboxlive.com`, `*.minecraft.net`,
`*.minecraft-services.net` and `*.playfabapi.com`, keeps nothing and drops cookies. Your tokens pass through it, so
use your own deployment rather than someone else's.
