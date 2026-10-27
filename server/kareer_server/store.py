# SPDX-FileCopyrightText: 2026 ToServeTheKing <austin@thebennett.net>
#
# SPDX-License-Identifier: GPL-3.0-or-later
"""The server's copy of every application, and the sync merge rules.

A job travels as one record: its fields, its whole stage history, and a
modified-at timestamp for each of those two halves. Each half is
last-writer-wins on its own, so a stage change from Grok and a notes edit
made offline on the laptop both survive. A delete always wins. Every write
that changes a row gives it the next `seq`, and a client pulls everything
with a seq above the cursor it last saw.
"""

from __future__ import annotations

import json
import sqlite3
import threading
from datetime import UTC, datetime
from pathlib import Path
from typing import Any
from uuid import uuid4

STAGES = [
    "Applied",
    "Screening",
    "Interview",
    "Onsite",
    "Offer",
    "Accepted",
    "Rejected",
    "Withdrawn",
    "Ghosted",
]

FIELDS = [
    "company",
    "title",
    "location",
    "remote_type",
    "source",
    "url",
    "date_applied",
    "salary_min",
    "salary_max",
    "salary_expectation",
    "currency",
    "notes",
    "contact",
    "created_at",
]


def canonical_stage(stage: str) -> str | None:
    for s in STAGES:
        if s.lower() == stage.strip().lower():
            return s
    return None


def now_iso() -> str:
    now = datetime.now(UTC)
    return now.strftime("%Y-%m-%dT%H:%M:%S.") + f"{now.microsecond // 1000:03d}Z"


def parse_time(value: str | None) -> datetime:
    """Timestamps compare as instants, not strings: Qt writes some with
    milliseconds and some without."""
    if not value:
        return datetime.min.replace(tzinfo=UTC)
    dt = datetime.fromisoformat(value)
    return dt if dt.tzinfo else dt.replace(tzinfo=UTC)


def normalize_history(steps: list[dict[str, str]]) -> list[dict[str, str]]:
    """Sorted by time, unknown stages dropped, consecutive repeats collapsed:
    the same rules JobsDatabase::replaceStageHistory applies."""
    valid = []
    for step in steps:
        stage = canonical_stage(str(step.get("stage", "")))
        at = step.get("at")
        if stage and at:
            valid.append({"stage": stage, "at": at})
    valid.sort(key=lambda s: parse_time(s["at"]))
    collapsed: list[dict[str, str]] = []
    for step in valid:
        if not collapsed or collapsed[-1]["stage"] != step["stage"]:
            collapsed.append(step)
    return collapsed


class Store:
    def __init__(self, path: str | Path):
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        self._db = sqlite3.connect(str(path), check_same_thread=False, isolation_level=None)
        self._db.row_factory = sqlite3.Row
        self._lock = threading.Lock()
        self._db.executescript(
            """
            PRAGMA journal_mode = WAL;
            CREATE TABLE IF NOT EXISTS jobs (
                uuid TEXT PRIMARY KEY,
                seq INTEGER NOT NULL,
                deleted INTEGER NOT NULL DEFAULT 0,
                fields_modified_at TEXT,
                history_modified_at TEXT,
                fields TEXT NOT NULL DEFAULT '{}',
                history TEXT NOT NULL DEFAULT '[]'
            );
            CREATE INDEX IF NOT EXISTS jobs_seq ON jobs(seq);
            CREATE TABLE IF NOT EXISTS kv (
                kind TEXT NOT NULL,
                key TEXT NOT NULL,
                value TEXT NOT NULL,
                expires_at REAL,
                PRIMARY KEY (kind, key)
            );
            """
        )

    # --- sync ----------------------------------------------------------------

    def _next_seq(self) -> int:
        return self._db.execute("SELECT COALESCE(MAX(seq), 0) + 1 FROM jobs").fetchone()[0]

    def _row(self, uuid: str) -> sqlite3.Row | None:
        return self._db.execute("SELECT * FROM jobs WHERE uuid = ?", (uuid,)).fetchone()

    @staticmethod
    def _to_record(row: sqlite3.Row) -> dict[str, Any]:
        record: dict[str, Any] = {"uuid": row["uuid"], "deleted": bool(row["deleted"])}
        if not row["deleted"]:
            record.update(
                fields_modified_at=row["fields_modified_at"],
                history_modified_at=row["history_modified_at"],
                fields=json.loads(row["fields"]),
                history=json.loads(row["history"]),
            )
        return record

    def _merge(self, incoming: dict[str, Any]) -> None:
        uuid = str(incoming["uuid"])
        existing = self._row(uuid)
        if existing is not None and existing["deleted"]:
            return
        if incoming.get("deleted"):
            self._db.execute(
                "INSERT INTO jobs (uuid, seq, deleted) VALUES (?, ?, 1) "
                "ON CONFLICT(uuid) DO UPDATE SET seq = excluded.seq, deleted = 1, fields = '{}', history = '[]', "
                "fields_modified_at = NULL, history_modified_at = NULL",
                (uuid, self._next_seq()),
            )
            return

        fields_at = incoming.get("fields_modified_at")
        history_at = incoming.get("history_modified_at")
        fields = {k: incoming.get("fields", {}).get(k) for k in FIELDS}
        history = normalize_history(incoming.get("history", []))
        if existing is None:
            self._db.execute(
                "INSERT INTO jobs (uuid, seq, fields_modified_at, history_modified_at, fields, history) "
                "VALUES (?, ?, ?, ?, ?, ?)",
                (uuid, self._next_seq(), fields_at, history_at, json.dumps(fields), json.dumps(history)),
            )
            return

        changed = False
        if parse_time(fields_at) > parse_time(existing["fields_modified_at"]):
            self._db.execute(
                "UPDATE jobs SET fields = ?, fields_modified_at = ? WHERE uuid = ?",
                (json.dumps(fields), fields_at, uuid),
            )
            changed = True
        if history and parse_time(history_at) > parse_time(existing["history_modified_at"]):
            self._db.execute(
                "UPDATE jobs SET history = ?, history_modified_at = ? WHERE uuid = ?",
                (json.dumps(history), history_at, uuid),
            )
            changed = True
        if changed:
            self._db.execute("UPDATE jobs SET seq = ? WHERE uuid = ?", (self._next_seq(), uuid))

    def sync(self, since: int, pushed: list[dict[str, Any]]) -> dict[str, Any]:
        """Merges what a client pushed, then returns every row it has not seen.
        Rows it just pushed come back too when they won; applying them is a
        no-op on the client because the timestamps match."""
        with self._lock:
            self._db.execute("BEGIN IMMEDIATE")
            try:
                for record in pushed:
                    if record.get("uuid"):
                        self._merge(record)
                self._db.execute("COMMIT")
            except BaseException:
                self._db.execute("ROLLBACK")
                raise
            rows = self._db.execute("SELECT * FROM jobs WHERE seq > ? ORDER BY seq", (since,)).fetchall()
            cursor = self._db.execute("SELECT COALESCE(MAX(seq), 0) FROM jobs").fetchone()[0]
        return {"cursor": cursor, "jobs": [self._to_record(r) for r in rows]}

    # --- what the MCP tools use -----------------------------------------------

    def live_jobs(self) -> list[dict[str, Any]]:
        with self._lock:
            rows = self._db.execute("SELECT * FROM jobs WHERE deleted = 0").fetchall()
        return [self._to_record(r) for r in rows]

    def job(self, uuid: str) -> dict[str, Any] | None:
        with self._lock:
            row = self._row(uuid)
        if row is None or row["deleted"]:
            return None
        return self._to_record(row)

    def add_job(self, fields: dict[str, Any], history: list[dict[str, str]]) -> dict[str, Any]:
        """Creates an application the way JobsDatabase::addJob does, minus the
        synthetic Start step, which never travels in sync records."""
        stamp = now_iso()
        record = {
            "uuid": uuid4().hex,
            "fields_modified_at": stamp,
            "history_modified_at": stamp,
            "fields": {"created_at": stamp[:19] + "Z"} | fields,
            "history": history,
        }
        with self._lock:
            self._db.execute("BEGIN IMMEDIATE")
            try:
                self._merge(record)
                self._db.execute("COMMIT")
            except BaseException:
                self._db.execute("ROLLBACK")
                raise
            return self._to_record(self._row(record["uuid"]))

    def set_stage(self, uuid: str, stage: str, at: str | None = None) -> dict[str, Any]:
        """Records a move the way JobsDatabase::setStage does: a no-op if the job
        is already there, otherwise a new step on the end of its history."""
        canonical = canonical_stage(stage)
        if canonical is None:
            raise ValueError(f"Unknown stage '{stage}'. Valid stages: {', '.join(STAGES)}")
        with self._lock:
            row = self._row(uuid)
            if row is None or row["deleted"]:
                raise KeyError(uuid)
            history = json.loads(row["history"])
            if history and history[-1]["stage"] == canonical and at is None:
                return self._to_record(row)
            history = normalize_history([*history, {"stage": canonical, "at": at or now_iso()}])
            self._db.execute(
                "UPDATE jobs SET history = ?, history_modified_at = ?, seq = ? WHERE uuid = ?",
                (json.dumps(history), now_iso(), self._next_seq(), uuid),
            )
            return self._to_record(self._row(uuid))

    def replace_history(self, uuid: str, steps: list[dict[str, str]]) -> dict[str, Any]:
        """Rewrites a job's whole history the way JobsDatabase::replaceStageHistory
        does: normalized, with date_applied set to the first step's date, so both
        halves count as modified."""
        history = normalize_history(steps)
        if not history:
            raise ValueError("A history needs at least one valid step")
        with self._lock:
            row = self._row(uuid)
            if row is None or row["deleted"]:
                raise KeyError(uuid)
            fields = json.loads(row["fields"]) | {"date_applied": history[0]["at"][:10]}
            stamp = now_iso()
            self._db.execute(
                "UPDATE jobs SET fields = ?, history = ?, fields_modified_at = ?, history_modified_at = ?, "
                "seq = ? WHERE uuid = ?",
                (json.dumps(fields), json.dumps(history), stamp, stamp, self._next_seq(), uuid),
            )
            return self._to_record(self._row(uuid))

    # --- small expiring key/value store for OAuth state -----------------------

    def put(self, kind: str, key: str, value: dict[str, Any], expires_at: float | None) -> None:
        with self._lock:
            self._db.execute(
                "DELETE FROM kv WHERE expires_at IS NOT NULL AND expires_at < strftime('%s', 'now')"
            )
            self._db.execute(
                "INSERT OR REPLACE INTO kv (kind, key, value, expires_at) VALUES (?, ?, ?, ?)",
                (kind, key, json.dumps(value), expires_at),
            )

    def get(self, kind: str, key: str) -> dict[str, Any] | None:
        with self._lock:
            row = self._db.execute(
                "SELECT value, expires_at FROM kv WHERE kind = ? AND key = ?", (kind, key)
            ).fetchone()
        if row is None:
            return None
        if row["expires_at"] is not None and row["expires_at"] < datetime.now(UTC).timestamp():
            return None
        return json.loads(row["value"])

    def delete(self, kind: str, key: str) -> None:
        with self._lock:
            self._db.execute("DELETE FROM kv WHERE kind = ? AND key = ?", (kind, key))
