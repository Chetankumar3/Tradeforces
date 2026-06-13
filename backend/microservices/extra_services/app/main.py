import asyncio
import json
import os
from dataclasses import dataclass
from typing import Any, AsyncIterator

from fastapi import FastAPI, Request
from fastapi.responses import PlainTextResponse, StreamingResponse
from redis.asyncio import Redis


REDIS_HOST = os.getenv("REDIS_HOST", "10.219.1.4")
REDIS_PORT = int(os.getenv("REDIS_PORT", "6379"))
REDIS_DB = int(os.getenv("REDIS_DB", "0"))
REDIS_KEY = os.getenv("REDIS_KEY", "leaderboard:composite")
POLL_INTERVAL_SECONDS = float(os.getenv("POLL_INTERVAL_SECONDS", "1.0"))
LEADERBOARD_LIMIT = int(os.getenv("LEADERBOARD_LIMIT", "0"))
APP_NAME = os.getenv("APP_NAME", "dash_pusher")


@dataclass(frozen=True)
class LeaderboardRow:
    rank: int
    team_id: str
    composite_score: float
    submission_id: str | None = None

    def as_dict(self) -> dict[str, Any]:
        return {
            "rank": self.rank,
            "team_id": self.team_id,
            "composite_score": self.composite_score,
            "submission_id": self.submission_id,
        }


app = FastAPI(title=APP_NAME)
redis_client = Redis(host=REDIS_HOST, port=REDIS_PORT, db=REDIS_DB, decode_responses=True)


def _score_to_float(value: Any) -> float:
    if value is None:
        return 0.0
    try:
        return float(value)
    except (TypeError, ValueError):
        return 0.0


async def fetch_leaderboard() -> list[LeaderboardRow]:
    if LEADERBOARD_LIMIT > 0:
        members = await redis_client.zrevrange(REDIS_KEY, 0, LEADERBOARD_LIMIT - 1, withscores=True)
    else:
        members = await redis_client.zrevrange(REDIS_KEY, 0, -1, withscores=True)

    leaderboard: list[LeaderboardRow] = []
    for index, (team_id, score) in enumerate(members, start=1):
        leaderboard.append(
            LeaderboardRow(
                rank=index,
                team_id=str(team_id),
                composite_score=_score_to_float(score),
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
    return "\n".join(lines)


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


@app.get("/healthz", response_class=PlainTextResponse)
async def healthz() -> str:
    return "ok"


@app.get("/leaderboard")
async def stream_leaderboard(_: Request) -> StreamingResponse:
    headers = {
        "Cache-Control": "no-cache",
        "Connection": "keep-alive",
        "X-Accel-Buffering": "no",
    }
    return StreamingResponse(leaderboard_stream(), media_type="text/event-stream", headers=headers)
