import asyncio
import json
import os
from dataclasses import dataclass, field
from typing import Any, AsyncIterator

from fastapi import APIRouter, FastAPI, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import PlainTextResponse, StreamingResponse
from redis.asyncio import Redis


REDIS_HOST = os.getenv("REDIS_HOST", "10.219.1.4")
REDIS_PORT = int(os.getenv("REDIS_PORT", "6379"))
REDIS_DB = int(os.getenv("REDIS_DB", "0"))

# ZSET holding {submission_id: composite_score}
REDIS_KEY = os.getenv("REDIS_KEY", "leaderboard2")

# Per-team metadata HASH key pattern. "{id}" is replaced with the submission/team id.
REDIS_HASH_KEY_PATTERN = os.getenv("REDIS_HASH_KEY_PATTERN", "leaderboard2:data:{id}")

POLL_INTERVAL_SECONDS = float(os.getenv("POLL_INTERVAL_SECONDS", "1.0"))
LEADERBOARD_LIMIT = int(os.getenv("LEADERBOARD_LIMIT", "0"))
APP_NAME = os.getenv("APP_NAME", "dash_pusher")

# Maps logical field names -> actual Redis hash field names written by the scorer (Go6).
# Must match the `schemaFields` map passed into RunGo6. Override via env if the
# scorer's schema mapping differs from the defaults below.
DEFAULT_SCHEMA_FIELDS = {
    "ack_p50_ns": "ack_p50_ns",
    "ack_p90_ns": "ack_p90_ns",
    "ack_p99_ns": "ack_p99_ns",
    "exec_p50_ns": "exec_p50_ns",
    "exec_p90_ns": "exec_p90_ns",
    "exec_p99_ns": "exec_p99_ns",
    "max_throughput_rps": "max_throughput_rps",
    "correctness_pct": "correctness_pct",
    "combined_score": "combined_score",
}

try:
    SCHEMA_FIELDS: dict[str, str] = {
        **DEFAULT_SCHEMA_FIELDS,
        **json.loads(os.getenv("SCHEMA_FIELDS_JSON", "{}")),
    }
except (TypeError, ValueError) as exc:
    print(f"Invalid SCHEMA_FIELDS_JSON, falling back to defaults: {exc}")
    SCHEMA_FIELDS = dict(DEFAULT_SCHEMA_FIELDS)

# Reverse lookup: actual Redis hash field name -> logical field name.
REDIS_FIELD_TO_LOGICAL = {v: k for k, v in SCHEMA_FIELDS.items()}

# Logical fields that should be parsed as integers (nanoseconds / counts).
INT_FIELDS = {
    "ack_p50_ns",
    "ack_p90_ns",
    "ack_p99_ns",
    "exec_p50_ns",
    "exec_p90_ns",
    "exec_p99_ns",
    "max_throughput_rps",
}

# Logical fields that should be parsed as floats.
FLOAT_FIELDS = {
    "correctness_pct",
    "combined_score",
}


@dataclass(frozen=True)
class LeaderboardRow:
    rank: int
    team_id: str
    composite_score: float
    submission_id: str | None = None

    # Detailed metrics pulled from the per-team HASH.
    ack_p50_ns: int = 0
    ack_p90_ns: int = 0
    ack_p99_ns: int = 0
    exec_p50_ns: int = 0
    exec_p90_ns: int = 0
    exec_p99_ns: int = 0
    max_throughput_rps: int = 0
    correctness_pct: float = 0.0
    combined_score: float = 0.0

    def as_dict(self) -> dict[str, Any]:
        return {
            "rank": self.rank,
            "team_id": self.team_id,
            "composite_score": self.composite_score,
            "submission_id": self.submission_id,
            "ack_p50_ns": self.ack_p50_ns,
            "ack_p90_ns": self.ack_p90_ns,
            "ack_p99_ns": self.ack_p99_ns,
            "exec_p50_ns": self.exec_p50_ns,
            "exec_p90_ns": self.exec_p90_ns,
            "exec_p99_ns": self.exec_p99_ns,
            "max_throughput_rps": self.max_throughput_rps,
            "correctness_pct": self.correctness_pct,
            "combined_score": self.combined_score,
        }


router = APIRouter(prefix="/dashpusher")
app = FastAPI(title=APP_NAME)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

redis_client = Redis(host=REDIS_HOST, port=REDIS_PORT, db=REDIS_DB, decode_responses=True)


def _score_to_float(value: Any) -> float:
    if value is None:
        return 0.0
    try:
        return float(value)
    except (TypeError, ValueError):
        return 0.0


def _to_int(value: Any) -> int:
    if value is None:
        return 0
    try:
        return int(float(value))
    except (TypeError, ValueError):
        return 0


def _parse_hash(raw_hash: dict[str, str]) -> dict[str, Any]:
    """Map raw Redis hash fields (schema names) -> logical field names with proper types."""
    parsed: dict[str, Any] = {}
    for redis_field, value in raw_hash.items():
        logical_name = REDIS_FIELD_TO_LOGICAL.get(redis_field)
        if logical_name is None:
            continue
        if logical_name in INT_FIELDS:
            parsed[logical_name] = _to_int(value)
        elif logical_name in FLOAT_FIELDS:
            parsed[logical_name] = _score_to_float(value)
        else:
            parsed[logical_name] = value
    return parsed


async def fetch_leaderboard() -> list[LeaderboardRow]:
    try:
        if LEADERBOARD_LIMIT > 0:
            members = await redis_client.zrevrange(REDIS_KEY, 0, LEADERBOARD_LIMIT - 1, withscores=True)
        else:
            members = await redis_client.zrevrange(REDIS_KEY, 0, -1, withscores=True)
    except Exception as exc:
        print(f"Redis leaderboard ZSET query failed: {exc}")
        return []

    if not members:
        return []

    # Batch-fetch each team's metadata HASH in a single round-trip.
    pipe = redis_client.pipeline()
    for team_id, _score in members:
        pipe.hgetall(REDIS_HASH_KEY_PATTERN.format(id=team_id))

    try:
        hashes = await pipe.execute()
    except Exception as exc:
        print(f"Redis leaderboard HASH query failed: {exc}")
        hashes = [{} for _ in members]

    leaderboard: list[LeaderboardRow] = []
    for index, ((team_id, score), raw_hash) in enumerate(zip(members, hashes), start=1):
        team_id_str = str(team_id)
        details = _parse_hash(raw_hash or {})

        leaderboard.append(
            LeaderboardRow(
                rank=index,
                team_id=team_id_str,
                composite_score=_score_to_float(score),
                submission_id=team_id_str,
                ack_p50_ns=details.get("ack_p50_ns", 0),
                ack_p90_ns=details.get("ack_p90_ns", 0),
                ack_p99_ns=details.get("ack_p99_ns", 0),
                exec_p50_ns=details.get("exec_p50_ns", 0),
                exec_p90_ns=details.get("exec_p90_ns", 0),
                exec_p99_ns=details.get("exec_p99_ns", 0),
                max_throughput_rps=details.get("max_throughput_rps", 0),
                correctness_pct=details.get("correctness_pct", 0.0),
                combined_score=details.get("combined_score", 0.0),
            )
        )
    return leaderboard


def snapshot_payload(rows: list[LeaderboardRow]) -> str:
    payload = {
        "leaderboard": [row.as_dict() for row in rows],
        "count": len(rows),
    }
    return json.dumps(payload, separators=(",", ":"), ensure_ascii=True)


def sse_event(event: str, data: str, event_id: str | None = None) -> str:
    lines = []
    if event_id is not None:
        lines.append(f"id: {event_id}")
    lines.append(f"event: {event}")
    for line in data.splitlines() or [""]:
        lines.append(f"data: {line}")
    lines.append("")
    return "\n".join(lines) + "\n\n"


async def leaderboard_stream() -> AsyncIterator[str]:
    previous_snapshot = None
    heartbeat_count = 0

    while True:
        rows = await fetch_leaderboard()
        payload = snapshot_payload(rows)

        if payload != previous_snapshot:
            previous_snapshot = payload
            yield sse_event("leaderboard", payload, event_id=str(heartbeat_count))

        heartbeat_count += 1
        yield ": keep-alive\n\n"
        await asyncio.sleep(POLL_INTERVAL_SECONDS)


@router.get("/healthz", response_class=PlainTextResponse)
async def healthz() -> str:
    return "ok"


@router.get("/leaderboard")
async def stream_leaderboard(_: Request) -> StreamingResponse:
    headers = {
        "Cache-Control": "no-cache",
        "Connection": "keep-alive",
        "X-Accel-Buffering": "no",
    }
    return StreamingResponse(leaderboard_stream(), media_type="text/event-stream", headers=headers)


app.include_router(router)