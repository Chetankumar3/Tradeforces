from fastapi.testclient import TestClient

from app.main import app


client = TestClient(app)


def test_dashpusher_routes_are_registered():
    paths = {route.path for route in app.routes if hasattr(route, 'path')}

    assert '/dashpusher/healthz' in paths
    assert '/dashpusher/leaderboard' in paths


def test_leaderboard_stream_includes_cors_and_sse_headers(monkeypatch):
    async def fake_stream():
        yield 'event: leaderboard\ndata: {"leaderboard":[],"count":0}\n\n'

    monkeypatch.setattr('app.main.leaderboard_stream', fake_stream)

    response = client.get(
        '/dashpusher/leaderboard',
        headers={
            'Accept': 'text/event-stream',
            'Origin': 'http://localhost:5173',
        },
    )

    assert response.status_code == 200
    assert response.headers['content-type'].startswith('text/event-stream')
    assert response.headers.get('access-control-allow-origin') == 'http://localhost:5173'
    assert response.headers.get('access-control-allow-credentials') == 'true'
