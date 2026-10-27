# SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>
#
# SPDX-License-Identifier: GPL-3.0-or-later

import base64
import hashlib
import json
import secrets
import time
from urllib.parse import parse_qs, urlparse

import pytest
from starlette.testclient import TestClient

from kareer_server.app import create_app
from kareer_server.oauth import REFRESH_TTL, UNUSED_CLIENT_TTL, AuthentikConfig, KareerOAuthProvider
from kareer_server.store import Store

URL = "https://kareer.example"


def job(uuid, fields_at="2026-10-01T00:00:00.000Z", history_at="2026-10-01T00:00:00.000Z", **fields):
    return {
        "uuid": uuid,
        "fields_modified_at": fields_at,
        "history_modified_at": history_at,
        "fields": {"company": "Acme", "title": "Dev"} | fields,
        "history": [{"stage": "Applied", "at": "2026-10-01T00:00:00Z"}],
    }


# --- merge rules ----------------------------------------------------------------


def test_fields_and_history_merge_independently(tmp_path):
    store = Store(tmp_path / "s.sqlite")
    store.sync(0, [job("a")])
    store.set_stage("a", "Interview")  # Grok, while the laptop was offline
    # The laptop edited notes before Grok's move but syncs after it.
    store.sync(0, [job("a", fields_at="2026-10-02T00:00:00.000Z", notes="call went well")])
    record = store.job("a")
    assert record["fields"]["notes"] == "call went well"
    assert [s["stage"] for s in record["history"]] == ["Applied", "Interview"]


def test_older_write_loses_and_is_not_resent(tmp_path):
    store = Store(tmp_path / "s.sqlite")
    first = store.sync(0, [job("a", fields_at="2026-10-05T00:00:00.000Z", notes="new")])
    again = store.sync(first["cursor"], [job("a", fields_at="2026-10-04T00:00:00.000Z", notes="old")])
    assert store.job("a")["fields"]["notes"] == "new"
    assert again["jobs"] == []


def test_delete_wins_and_sticks(tmp_path):
    store = Store(tmp_path / "s.sqlite")
    store.sync(0, [job("a")])
    store.sync(0, [{"uuid": "a", "deleted": True}])
    store.sync(0, [job("a", fields_at="2030-01-01T00:00:00.000Z")])
    assert store.job("a") is None
    assert store.sync(0, [])["jobs"] == [{"uuid": "a", "deleted": True}]


def test_set_stage_canonicalizes_and_skips_repeats(tmp_path):
    store = Store(tmp_path / "s.sqlite")
    store.sync(0, [job("a")])
    cursor = store.sync(0, [])["cursor"]
    store.set_stage("a", "applied")
    assert store.sync(cursor, [])["jobs"] == []
    store.set_stage("a", "rejected")
    assert store.job("a")["history"][-1]["stage"] == "Rejected"
    with pytest.raises(ValueError):
        store.set_stage("a", "Hired")


# --- OAuth, sync and MCP over HTTP ----------------------------------------------------


@pytest.fixture
def client(tmp_path, monkeypatch):
    usernames = iter([])

    async def fake_authentik(self, code):
        return next(usernames)

    monkeypatch.setattr(KareerOAuthProvider, "_authentik_username", fake_authentik)
    store = Store(tmp_path / "s.sqlite")
    app = create_app(
        store,
        URL,
        AuthentikConfig("https://sso.example", "kareer", "cid", "secret", allowed_user="austin"),
    )
    with TestClient(app, base_url=URL) as c:

        def login_as(username):
            nonlocal usernames
            usernames = iter([username])
            return sign_in(c)

        c.login_as = login_as
        c.store = store
        yield c


def sign_in(c):
    redirect = "http://127.0.0.1:53682/callback"
    reg = c.post("/register", json={"redirect_uris": [redirect], "token_endpoint_auth_method": "none"})
    assert reg.status_code == 201, reg.text
    client_id = reg.json()["client_id"]
    verifier = secrets.token_urlsafe(48)
    challenge = base64.urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest()).rstrip(b"=").decode()

    to_authentik = c.get(
        "/authorize",
        params={
            "response_type": "code",
            "client_id": client_id,
            "redirect_uri": redirect,
            "code_challenge": challenge,
            "code_challenge_method": "S256",
            "state": "xyz",
        },
        follow_redirects=False,
    )
    assert to_authentik.status_code == 302, to_authentik.text
    ak_state = parse_qs(urlparse(to_authentik.headers["location"]).query)["state"][0]

    back = c.get("/oauth/authentik/callback", params={"code": "ak", "state": ak_state}, follow_redirects=False)
    query = parse_qs(urlparse(back.headers["location"]).query)
    assert query["state"] == ["xyz"]
    if "error" in query:
        return None
    token = c.post(
        "/token",
        data={
            "grant_type": "authorization_code",
            "code": query["code"][0],
            "redirect_uri": redirect,
            "client_id": client_id,
            "code_verifier": verifier,
        },
    )
    assert token.status_code == 200, token.text
    return token.json()["access_token"]


def test_only_the_allowed_user_gets_a_token(client):
    assert client.login_as("mallory") is None
    assert client.login_as("austin")


def test_sync_requires_a_token(client):
    assert client.post("/api/sync", json={"since": 0, "jobs": []}).status_code == 401
    assert client.post("/api/sync", json={"since": 0}, headers={"Authorization": "Bearer nope"}).status_code == 401


def test_mcp_stage_change_reaches_sync(client):
    token = client.login_as("austin")
    auth = {"Authorization": f"Bearer {token}"}
    pushed = client.post("/api/sync", json={"since": 0, "jobs": [job("a")]}, headers=auth).json()

    assert client.post("/mcp", json={}).status_code == 401
    headers = auth | {"Accept": "application/json, text/event-stream", "MCP-Protocol-Version": "2025-06-18"}
    call = client.post(
        "/mcp",
        headers=headers,
        json={
            "jsonrpc": "2.0",
            "id": 1,
            "method": "tools/call",
            "params": {"name": "set_stage", "arguments": {"id": "a", "stage": "interview", "date": "2026-10-20"}},
        },
    )
    assert call.status_code == 200, call.text
    assert "Interview" in call.text

    pulled = client.post("/api/sync", json={"since": pushed["cursor"], "jobs": []}, headers=auth).json()
    assert [s["stage"] for s in pulled["jobs"][0]["history"]] == ["Applied", "Interview"]
    assert pulled["jobs"][0]["history"][-1]["at"] == "2026-10-20T12:00:00.000Z"


def register(c, **headers):
    return c.post("/register", json={"redirect_uris": ["http://127.0.0.1:1/cb"], "token_endpoint_auth_method": "none"},
                  headers=headers)


def client_expiry(c, client_id):
    return c.store._db.execute("SELECT expires_at FROM kv WHERE kind = 'client' AND key = ?", (client_id,)).fetchone()[0]


def test_unused_registrations_expire_and_signed_in_ones_last(client):
    unused = register(client).json()["client_id"]
    assert client_expiry(client, unused) - time.time() == pytest.approx(UNUSED_CLIENT_TTL, abs=60)

    client.login_as("austin")
    signed_in = client.store._db.execute(
        "SELECT key FROM kv WHERE kind = 'client' ORDER BY expires_at DESC LIMIT 1").fetchone()[0]
    assert client_expiry(client, signed_in) - time.time() == pytest.approx(REFRESH_TTL, abs=60)


def test_registrations_are_limited_per_address(tmp_path):
    app = create_app(Store(tmp_path / "s.sqlite"), URL,
                     AuthentikConfig("https://sso.example", "kareer", "cid", "secret", allowed_user="austin"),
                     registrations_per_hour=3, client_ip_header="CF-Connecting-IP")
    with TestClient(app, base_url=URL) as c:
        assert [register(c, **{"CF-Connecting-IP": "203.0.113.1"}).status_code for _ in range(4)] == [201] * 3 + [429]
        assert register(c, **{"CF-Connecting-IP": "203.0.113.2"}).status_code == 201


def call_tool(c, token, name, arguments):
    headers = {"Authorization": f"Bearer {token}", "Accept": "application/json, text/event-stream",
               "MCP-Protocol-Version": "2025-06-18"}
    reply = c.post("/mcp", headers=headers, json={"jsonrpc": "2.0", "id": 1, "method": "tools/call",
                                                  "params": {"name": name, "arguments": arguments}})
    assert reply.status_code == 200, reply.text
    data = next(line[5:] for line in reply.text.splitlines() if line.startswith("data:"))
    return json.loads(data)["result"]


def test_add_application_reaches_sync_and_refuses_duplicates(client):
    token = client.login_as("austin")
    added = call_tool(client, token, "add_application",
                      {"company": "Caterpillar", "title": "Software Engineer", "date_applied": "2026-10-25",
                       "stage": "screening", "remote_type": "hybrid", "notes": "via referral"})
    assert not added.get("isError"), added
    pulled = client.post("/api/sync", json={"since": 0, "jobs": []},
                         headers={"Authorization": f"Bearer {token}"}).json()["jobs"]
    assert len(pulled) == 1
    record = pulled[0]
    assert record["fields"]["company"] == "Caterpillar"
    assert record["fields"]["remote_type"] == "Hybrid"
    assert record["fields"]["date_applied"] == "2026-10-25"
    assert record["fields"]["created_at"].endswith("Z")
    assert [s["stage"] for s in record["history"]] == ["Applied", "Screening"]
    assert record["history"][0]["at"] == "2026-10-25T12:00:00.000Z"

    again = call_tool(client, token, "add_application", {"company": "caterpillar", "title": "software engineer"})
    assert again.get("isError") and "Already logged" in again["content"][0]["text"]
    separate = call_tool(client, token, "add_application",
                         {"company": "Caterpillar", "title": "Software Engineer", "allow_duplicate": True})
    assert not separate.get("isError")
    assert len(client.store.live_jobs()) == 2


def test_tool_errors_reach_the_assistant(client):
    token = client.login_as("austin")
    for name, arguments, expected in [
        ("set_stage", {"id": "nope", "stage": "Interview"}, "No application with id nope"),
        ("get_application", {"id": "nope"}, "No application with id nope"),
        ("add_application", {"company": "Acme", "title": "Dev", "stage": "Hired"}, "Unknown stage 'Hired'"),
        ("add_application", {"company": "Acme", "title": "Dev", "date_applied": "Oct 5"}, "Dates are YYYY-MM-DD"),
    ]:
        result = call_tool(client, token, name, arguments)
        assert result.get("isError") and expected in result["content"][0]["text"], (name, result)
