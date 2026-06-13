# dash_pusher

Simple Python SSE service for streaming the leaderboard from Redis.

## What it does

- Reads the current leaderboard from Redis sorted set `leaderboard:composite`
- Sends the full leaderboard snapshot when a client connects
- Keeps the SSE connection alive with a polling loop

## Redis contract

- Key: `leaderboard:composite`
- Type: sorted set
- Member: `team_id`
- Score: `composite_score`

## Local run

```bash
python -m venv .venv
.venv\\Scripts\\pip install -r requirements.txt
.venv\\Scripts\\uvicorn app.main:app --host 0.0.0.0 --port 8000
```

## Environment variables

- `REDIS_HOST` defaults to `10.219.1.4`
- `REDIS_PORT` defaults to `6379`
- `REDIS_DB` defaults to `0`
- `REDIS_KEY` defaults to `leaderboard:composite`
- `POLL_INTERVAL_SECONDS` defaults to `1.0`
- `LEADERBOARD_LIMIT` defaults to `0` which means "all rows"

## Endpoints

- `GET /healthz`
- `GET /leaderboard`

