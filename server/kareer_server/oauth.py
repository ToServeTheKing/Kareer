# SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""Kareer's own OAuth authorization server, with the login delegated to Authentik.

Grok's custom connector and the desktop app both expect the MCP flavour of
OAuth: discovery, dynamic client registration and PKCE against the server they
talk to. Authentik does not do dynamic registration, so this server plays the
authorization server for its clients and sends the person on to Authentik to
prove who they are. Only the one configured Authentik user gets a token.
"""

from __future__ import annotations

import secrets
import time
from dataclasses import dataclass
from urllib.parse import urlencode

import httpx
from mcp.server.auth.provider import (
    AccessToken,
    AuthorizationCode,
    AuthorizationParams,
    RefreshToken,
    construct_redirect_uri,
)
from mcp.shared.auth import OAuthClientInformationFull, OAuthToken
from starlette.requests import Request
from starlette.responses import HTMLResponse, RedirectResponse, Response

from .store import Store

ACCESS_TTL = 60 * 60
REFRESH_TTL = 90 * 24 * 60 * 60
CODE_TTL = 5 * 60
LOGIN_TTL = 10 * 60
# /register is open to anyone (MCP clients register themselves), so a client
# that never completes a sign-in is forgotten after a day. One that does lives
# as long as its newest refresh token.
UNUSED_CLIENT_TTL = 24 * 60 * 60

CALLBACK_PATH = "/oauth/authentik/callback"


@dataclass
class AuthentikConfig:
    base_url: str  # e.g. https://sso.example.com
    slug: str  # the Authentik application slug
    client_id: str
    client_secret: str
    allowed_user: str

    @property
    def authorize_url(self) -> str:
        return f"{self.base_url}/application/o/authorize/"

    @property
    def token_url(self) -> str:
        return f"{self.base_url}/application/o/token/"

    @property
    def userinfo_url(self) -> str:
        return f"{self.base_url}/application/o/userinfo/"


class KareerOAuthProvider:
    def __init__(self, store: Store, public_url: str, authentik: AuthentikConfig):
        self.store = store
        self.public_url = public_url.rstrip("/")
        self.authentik = authentik

    # --- clients ---------------------------------------------------------------

    async def get_client(self, client_id: str) -> OAuthClientInformationFull | None:
        data = self.store.get("client", client_id)
        return OAuthClientInformationFull.model_validate(data) if data else None

    async def register_client(self, client_info: OAuthClientInformationFull) -> None:
        self.store.put("client", client_info.client_id, client_info.model_dump(mode="json"),
                       time.time() + UNUSED_CLIENT_TTL)

    # --- authorization code, via Authentik -----------------------------------------

    async def authorize(self, client: OAuthClientInformationFull, params: AuthorizationParams) -> str:
        state = secrets.token_urlsafe(32)
        self.store.put(
            "login",
            state,
            {"client_id": client.client_id, "params": params.model_dump(mode="json")},
            time.time() + LOGIN_TTL,
        )
        query = urlencode(
            {
                "response_type": "code",
                "client_id": self.authentik.client_id,
                "redirect_uri": self.public_url + CALLBACK_PATH,
                "scope": "openid profile email",
                "state": state,
            }
        )
        return f"{self.authentik.authorize_url}?{query}"

    async def authentik_callback(self, request: Request) -> Response:
        state = request.query_params.get("state", "")
        pending = self.store.get("login", state)
        if pending is None:
            return HTMLResponse("This sign-in link has expired. Start again from the app.", status_code=400)
        self.store.delete("login", state)
        params = AuthorizationParams.model_validate(pending["params"])
        redirect = str(params.redirect_uri)

        if "error" in request.query_params:
            return RedirectResponse(
                construct_redirect_uri(redirect, error="access_denied", state=params.state), status_code=302
            )

        username = await self._authentik_username(request.query_params.get("code", ""))
        if username != self.authentik.allowed_user:
            return RedirectResponse(
                construct_redirect_uri(
                    redirect,
                    error="access_denied",
                    error_description="This Kareer server belongs to someone else",
                    state=params.state,
                ),
                status_code=302,
            )

        code = secrets.token_urlsafe(32)
        auth_code = AuthorizationCode(
            code=code,
            scopes=params.scopes or [],
            expires_at=time.time() + CODE_TTL,
            client_id=pending["client_id"],
            code_challenge=params.code_challenge,
            redirect_uri=params.redirect_uri,
            redirect_uri_provided_explicitly=params.redirect_uri_provided_explicitly,
            resource=params.resource,
            subject=username,
        )
        self.store.put("code", code, auth_code.model_dump(mode="json"), auth_code.expires_at)
        return RedirectResponse(construct_redirect_uri(redirect, code=code, state=params.state), status_code=302)

    async def _authentik_username(self, code: str) -> str | None:
        if not code:
            return None
        async with httpx.AsyncClient(timeout=15) as http:
            token = await http.post(
                self.authentik.token_url,
                data={
                    "grant_type": "authorization_code",
                    "code": code,
                    "redirect_uri": self.public_url + CALLBACK_PATH,
                    "client_id": self.authentik.client_id,
                    "client_secret": self.authentik.client_secret,
                },
            )
            if token.status_code != 200:
                return None
            info = await http.get(
                self.authentik.userinfo_url,
                headers={"Authorization": f"Bearer {token.json()['access_token']}"},
            )
            if info.status_code != 200:
                return None
            return info.json().get("preferred_username")

    async def load_authorization_code(self, client: OAuthClientInformationFull, authorization_code: str):
        data = self.store.get("code", authorization_code)
        if data is None or data["client_id"] != client.client_id:
            return None
        return AuthorizationCode.model_validate(data)

    async def exchange_authorization_code(
        self, client: OAuthClientInformationFull, authorization_code: AuthorizationCode
    ) -> OAuthToken:
        self.store.delete("code", authorization_code.code)
        return self._issue(client.client_id, authorization_code.scopes, authorization_code.subject,
                           authorization_code.resource)

    # --- tokens ------------------------------------------------------------------

    def _issue(self, client_id: str, scopes: list[str], subject: str | None, resource: str | None) -> OAuthToken:
        access = secrets.token_urlsafe(32)
        refresh = secrets.token_urlsafe(32)
        now = int(time.time())
        client = self.store.get("client", client_id)
        if client is not None:
            self.store.put("client", client_id, client, now + REFRESH_TTL)
        self.store.put(
            "access",
            access,
            AccessToken(token=access, client_id=client_id, scopes=scopes, expires_at=now + ACCESS_TTL,
                        subject=subject, resource=resource).model_dump(mode="json")
            | {"refresh": refresh},
            now + ACCESS_TTL,
        )
        self.store.put(
            "refresh",
            refresh,
            RefreshToken(token=refresh, client_id=client_id, scopes=scopes, expires_at=now + REFRESH_TTL,
                         subject=subject, resource=resource).model_dump(mode="json")
            | {"access": access},
            now + REFRESH_TTL,
        )
        return OAuthToken(access_token=access, expires_in=ACCESS_TTL, scope=" ".join(scopes) or None,
                          refresh_token=refresh)

    async def load_refresh_token(self, client: OAuthClientInformationFull, refresh_token: str):
        data = self.store.get("refresh", refresh_token)
        if data is None or data["client_id"] != client.client_id:
            return None
        return RefreshToken.model_validate(data)

    async def exchange_refresh_token(
        self, client: OAuthClientInformationFull, refresh_token: RefreshToken, scopes: list[str]
    ) -> OAuthToken:
        await self.revoke_token(refresh_token)
        return self._issue(client.client_id, scopes or refresh_token.scopes, refresh_token.subject,
                           refresh_token.resource)

    async def load_access_token(self, token: str) -> AccessToken | None:
        data = self.store.get("access", token)
        if data is None or data.get("subject") != self.authentik.allowed_user:
            return None
        return AccessToken.model_validate(data)

    async def revoke_token(self, token: AccessToken | RefreshToken) -> None:
        kind, other_kind, other_key = (
            ("access", "refresh", "refresh") if isinstance(token, AccessToken) else ("refresh", "access", "access")
        )
        data = self.store.get(kind, token.token)
        self.store.delete(kind, token.token)
        if data and data.get(other_key):
            self.store.delete(other_kind, data[other_key])
