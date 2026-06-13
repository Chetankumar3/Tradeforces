import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from shared_core.core import get_db_url


def test_get_db_url_uses_proxy_safe_defaults(monkeypatch):
    monkeypatch.setenv("DB_USER", "appuser")
    monkeypatch.setenv("DB_PASSWORD", "secret")
    monkeypatch.setenv("DB_HOST", "localhost")
    monkeypatch.setenv("DB_PORT", "5433")
    monkeypatch.setenv("DB_NAME", "tradeforces")

    url = get_db_url()

    assert url.startswith("postgresql+asyncpg://appuser:secret@localhost:5433/tradeforces")
    assert "sslmode=disable" in url
    assert "connect_timeout=10" in url


def test_get_db_url_uses_cloud_sql_socket_path(monkeypatch):
    monkeypatch.setenv("DB_USER", "appuser")
    monkeypatch.setenv("DB_PASSWORD", "secret")
    monkeypatch.setenv("DB_HOST", "/cloudsql/project:region:instance")
    monkeypatch.setenv("DB_NAME", "tradeforces")
    monkeypatch.delenv("DB_PORT", raising=False)

    url = get_db_url()

    assert url.startswith("postgresql+asyncpg://appuser:secret@/tradeforces")
    assert "host=%2Fcloudsql%2Fproject%3Aregion%3Ainstance" in url
