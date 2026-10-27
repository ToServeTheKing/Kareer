# Kareer server

A small service that keeps Kareer in sync between computers and lets an AI
assistant (Grok, Cursor, or any client with MCP custom connectors) change
application stages. Each server belongs to one person: only the configured
Authentik user can sign in.

| Path | What |
|---|---|
| `/mcp` | MCP (streamable HTTP). Tools: `list_applications`, `get_application`, `add_application`, `set_stage`, `set_history` |
| `/api/sync` | `POST {since, jobs}` → `{cursor, jobs}`, bearer token. Used by the desktop app |
| `/authorize`, `/token`, `/register`, `/revoke`, `/.well-known/…` | OAuth 2.1 with dynamic client registration and PKCE |
| `/oauth/authentik/callback` | Where Authentik sends the browser back after login |
| `/health` | Liveness, no auth |

The server is its own OAuth authorization server, because MCP clients and the
desktop app register themselves dynamically, which Authentik does not support.
The login itself is Authentik's. Data is one SQLite file.

`/register` has to be open to anyone for that to work, so registrations are
limited per address (10 an hour by default), and a client that never completes
a sign-in is forgotten after a day.

## Running it

You need an [Authentik](https://goauthentik.io) instance and somewhere to serve
HTTPS from.

1. In Authentik, create an OAuth2/OpenID provider (confidential client) with
   the redirect URI `https://<your host>/oauth/authentik/callback`, and an
   application for it. Bind it to your user if other people use your Authentik.
2. Download `kareer-server-compose.yaml` from a
   [release](https://github.com/toservetheking/Kareer/releases), save it as
   `compose.yaml`, and fill in the values it marks. Put the provider's client
   ID and secret in a `.env` beside it.
3. `docker compose up -d`, and point your reverse proxy at port 8000.

The image is `ghcr.io/toservetheking/kareer-server`. `:latest` is the newest
release; each release is also tagged by version (`0.1.7`, `0.1`). To update:
`docker compose pull && docker compose up -d`.

Then in Kareer, open **Settings → Sync**, enter `https://<your host>`, and sign
in. From the command line: `kareer sync --server https://<your host> --sign-in`.

## Connecting an assistant

Add a custom MCP connector with the URL `https://<your host>/mcp`. It opens a
browser window; sign in to Authentik, and the four tools show up.

If the server is behind Cloudflare with Bot Fight Mode on, the assistant's
requests get challenged and the connector never reaches the sign-in. On the
Free plan Bot Fight Mode cannot be skipped for one path, so allow the
assistant's addresses with IP Access rules (find them under Security → Events).

## Configuration

| Variable | |
|---|---|
| `KAREER_PUBLIC_URL` | The server's public URL, e.g. `https://kareer.example.com`. Issuer and redirect base |
| `KAREER_ALLOWED_USER` | The Authentik username allowed in. Required |
| `AUTHENTIK_URL` | Your Authentik, e.g. `https://sso.example.com` |
| `AUTHENTIK_CLIENT_ID`, `AUTHENTIK_CLIENT_SECRET` | From the Authentik provider. Keep them in `.env` |
| `KAREER_SERVER_DB` | SQLite file, default `/data/kareer-server.sqlite` |
| `KAREER_REGISTRATIONS_PER_HOUR` | Client registrations allowed per address per hour, default 10 |
| `KAREER_CLIENT_IP_HEADER` | Header carrying the client's address behind a proxy, e.g. `CF-Connecting-IP`. Only set it if your proxy always sets that header, since clients can send it too |

## Development

```sh
python -m venv .venv && .venv/bin/pip install -e '.[test]'
.venv/bin/python -m pytest
```
