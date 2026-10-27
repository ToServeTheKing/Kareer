# SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""The Kareer server: the sync API for the desktop apps and an MCP endpoint
for AI assistants, behind one OAuth server.

    /mcp              MCP (streamable HTTP) for an assistant's custom connector
    /api/sync         POST, bearer token: the desktop apps' push/pull
    /health           GET, no auth
    /authorize, /token, /register, /revoke, /.well-known/...   OAuth (mcp SDK)
"""

from __future__ import annotations

import os
import time as clock
from collections import deque
from datetime import UTC, date, datetime, time, timedelta
from typing import Any

from mcp.server.auth.settings import AuthSettings, ClientRegistrationOptions, RevocationOptions
from mcp.server.mcpserver import MCPServer
from mcp.server.mcpserver.exceptions import ToolError
from starlette.requests import Request
from starlette.responses import JSONResponse, Response

from .oauth import CALLBACK_PATH, AuthentikConfig, KareerOAuthProvider
from .store import STAGES, Store, canonical_stage

REMOTE_TYPES = ["Onsite", "Hybrid", "Remote"]

SCOPE = "kareer"


def _summary(record: dict[str, Any]) -> dict[str, Any]:
    fields, history = record["fields"], record["history"]
    return {
        "id": record["uuid"],
        "company": fields.get("company"),
        "title": fields.get("title"),
        "stage": history[-1]["stage"] if history else None,
        "date_applied": fields.get("date_applied"),
        "last_stage_change": history[-1]["at"] if history else None,
    }


class RegistrationLimit:
    """Caps dynamic client registrations per address per hour, since /register is
    open to anyone. Behind a proxy, `ip_header` names the header carrying the
    client's address (e.g. CF-Connecting-IP); only trust it if the proxy sets it."""

    WINDOW = 60 * 60

    def __init__(self, app, per_hour: int, ip_header: str | None = None):
        self.app = app
        self.per_hour = per_hour
        self.ip_header = ip_header.lower().encode() if ip_header else None
        self.recent: dict[str, deque[float]] = {}

    def _address(self, scope) -> str:
        if self.ip_header:
            for name, value in scope["headers"]:
                if name == self.ip_header:
                    return value.decode("latin-1").split(",")[0].strip()
        client = scope.get("client")
        return client[0] if client else ""

    async def __call__(self, scope, receive, send):
        if scope["type"] == "http" and scope["method"] == "POST" and scope["path"] == "/register":
            now = clock.monotonic()
            if len(self.recent) > 10_000:
                self.recent = {a: h for a, h in self.recent.items() if h and h[-1] > now - self.WINDOW}
            hits = self.recent.setdefault(self._address(scope), deque())
            while hits and hits[0] <= now - self.WINDOW:
                hits.popleft()
            if len(hits) >= self.per_hour:
                retry = int(hits[0] + self.WINDOW - now) + 1
                response = JSONResponse(
                    {"error": "too_many_requests", "error_description": "Too many registrations; try again later"},
                    status_code=429,
                    headers={"Retry-After": str(retry)},
                )
                await response(scope, receive, send)
                return
            hits.append(now)
        await self.app(scope, receive, send)


def create_app(store: Store, public_url: str, authentik: AuthentikConfig, registrations_per_hour: int = 10,
               client_ip_header: str | None = None):
    provider = KareerOAuthProvider(store, public_url, authentik)
    mcp = MCPServer(
        name="Kareer",
        instructions=(
            "Kareer is the user's job application tracker. Use list_applications to find an "
            "application by company or title, then set_stage to record that it moved, or "
            "add_application to log a new one. "
            f"Stages: {', '.join(STAGES)}. Moves are recorded in the application's stage "
            "history, which the desktop app syncs."
        ),
        auth_server_provider=provider,
        auth=AuthSettings(
            issuer_url=public_url,
            resource_server_url=public_url.rstrip("/") + "/mcp",
            validate_token_resource=False,
            client_registration_options=ClientRegistrationOptions(
                enabled=True, valid_scopes=[SCOPE], default_scopes=[SCOPE]
            ),
            revocation_options=RevocationOptions(enabled=True),
            required_scopes=[],
        ),
    )

    @mcp.tool()
    def list_applications(stage: str | None = None, search: str | None = None) -> list[dict[str, Any]]:
        """List job applications, newest first. Optionally filter by current stage
        (e.g. "Applied") and/or a case-insensitive search over company and title."""
        jobs = [_summary(r) for r in store.live_jobs()]
        if stage:
            jobs = [j for j in jobs if (j["stage"] or "").lower() == stage.lower()]
        if search:
            needle = search.lower()
            jobs = [j for j in jobs if needle in f"{j['company']} {j['title']}".lower()]
        jobs.sort(key=lambda j: (j["date_applied"] or "", j["last_stage_change"] or ""), reverse=True)
        return jobs

    @mcp.tool()
    def get_application(id: str) -> dict[str, Any]:
        """Everything recorded about one application, including its stage history."""
        record = store.job(id)
        if record is None:
            raise ToolError(f"No application with id {id}")
        return _summary(record) | {"fields": record["fields"], "history": record["history"]}

    @mcp.tool()
    def add_application(
        company: str,
        title: str,
        date_applied: str | None = None,
        stage: str = "Applied",
        location: str | None = None,
        remote_type: str | None = None,
        source: str | None = None,
        url: str | None = None,
        notes: str | None = None,
        contact: str | None = None,
        salary_min: int | None = None,
        salary_max: int | None = None,
        currency: str | None = None,
        allow_duplicate: bool = False,
    ) -> dict[str, Any]:
        """Log a new job application. `date_applied` is YYYY-MM-DD (default today).
        `stage` is where it stands now (default Applied); anything else is recorded as
        a move on from Applied. `remote_type` is Onsite, Hybrid or Remote. Refuses if an
        application with the same company and title already exists (check with
        list_applications, or pass allow_duplicate=true for a genuinely separate one)."""
        company, title = company.strip(), title.strip()
        if not company or not title:
            raise ToolError("company and title are required")
        if not allow_duplicate:
            same = [j for j in map(_summary, store.live_jobs())
                    if (j["company"] or "").lower() == company.lower() and (j["title"] or "").lower() == title.lower()]
            if same:
                raise ToolError(f"Already logged as {same[0]['id']} (stage {same[0]['stage']}). "
                                 "Use set_stage to update it, or allow_duplicate=true for a separate application.")
        canonical = canonical_stage(stage)
        if canonical is None:
            raise ToolError(f"Unknown stage '{stage}'. Valid stages: {', '.join(STAGES)}")
        if remote_type:
            remote_type = next((r for r in REMOTE_TYPES if r.lower() == remote_type.strip().lower()), None)
            if remote_type is None:
                raise ToolError(f"remote_type is one of {', '.join(REMOTE_TYPES)}")
        now = datetime.now(UTC)
        day = _parse_day(date_applied) if date_applied else now.date()
        # Never in the future, and a second ahead of any later stage so the
        # two steps can't swap places when a client sorts them.
        applied_at = min(datetime.combine(day, time(12), UTC), now - timedelta(seconds=1))
        history = [{"stage": "Applied", "at": _stamp(applied_at)}]
        if canonical != "Applied":
            history.append({"stage": canonical, "at": _stamp(now)})
        fields = {
            "company": company, "title": title, "date_applied": day.isoformat(), "location": location,
            "remote_type": remote_type, "source": source, "url": url, "notes": notes, "contact": contact,
            "salary_min": salary_min, "salary_max": salary_max, "currency": currency,
        }
        record = store.add_job(fields, history)
        return _summary(record) | {"fields": record["fields"], "history": record["history"]}

    @mcp.tool()
    def set_stage(id: str, stage: str, date: str | None = None) -> dict[str, Any]:
        """Move an application to a new stage. `stage` is one of Applied, Screening,
        Interview, Onsite, Offer, Accepted, Rejected, Withdrawn, Ghosted. `date`
        (YYYY-MM-DD) backdates the move; omit it to record the move as happening now."""
        at = None
        if date:
            at = _noon(_parse_day(date))
        try:
            record = store.set_stage(id, stage, at)
        except KeyError:
            raise ToolError(f"No application with id {id}") from None
        except ValueError as error:
            raise ToolError(str(error)) from None
        return _summary(record) | {"history": record["history"]}

    @mcp.custom_route("/health", methods=["GET"])
    async def health(_: Request) -> Response:
        return JSONResponse({"ok": True})

    @mcp.custom_route(CALLBACK_PATH, methods=["GET"])
    async def authentik_callback(request: Request) -> Response:
        return await provider.authentik_callback(request)

    @mcp.custom_route("/api/sync", methods=["POST"])
    async def sync(request: Request) -> Response:
        header = request.headers.get("authorization", "")
        token = header[7:] if header.lower().startswith("bearer ") else ""
        if not token or await provider.load_access_token(token) is None:
            return JSONResponse(
                {"error": "invalid_token"},
                status_code=401,
                headers={"WWW-Authenticate": 'Bearer error="invalid_token"'},
            )
        body = await request.json()
        jobs = body.get("jobs", [])
        if not isinstance(jobs, list):
            return JSONResponse({"error": "jobs must be a list"}, status_code=400)
        return JSONResponse(store.sync(int(body.get("since", 0)), jobs))

    return RegistrationLimit(mcp.streamable_http_app(host="0.0.0.0", stateless_http=True),
                             registrations_per_hour, client_ip_header)


def _stamp(moment: datetime) -> str:
    return moment.strftime("%Y-%m-%dT%H:%M:%S.") + f"{moment.microsecond // 1000:03d}Z"


def _noon(day: date) -> str:
    return _stamp(datetime.combine(day, time(12), UTC))


def _parse_day(value: str) -> date:
    try:
        return date.fromisoformat(value)
    except ValueError:
        raise ToolError(f"Dates are YYYY-MM-DD, got '{value}'") from None


def main() -> None:
    import uvicorn

    env = os.environ
    store = Store(env.get("KAREER_SERVER_DB", "/data/kareer-server.sqlite"))
    app = create_app(
        store,
        env["KAREER_PUBLIC_URL"],
        AuthentikConfig(
            base_url=env["AUTHENTIK_URL"].rstrip("/"),
            slug=env.get("AUTHENTIK_SLUG", "kareer"),
            client_id=env["AUTHENTIK_CLIENT_ID"],
            client_secret=env["AUTHENTIK_CLIENT_SECRET"],
            allowed_user=env["KAREER_ALLOWED_USER"],
        ),
        registrations_per_hour=int(env.get("KAREER_REGISTRATIONS_PER_HOUR", "10")),
        client_ip_header=env.get("KAREER_CLIENT_IP_HEADER") or None,
    )
    uvicorn.run(app, host="0.0.0.0", port=int(env.get("PORT", "8000")), proxy_headers=True,
                forwarded_allow_ips="*")


if __name__ == "__main__":
    main()
