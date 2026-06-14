import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from shared_core import core as core_module
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


def test_get_db_url_accepts_host_with_explicit_port(monkeypatch):
    monkeypatch.setenv("DB_USER", "appuser")
    monkeypatch.setenv("DB_PASSWORD", "secret")
    monkeypatch.setenv("DB_HOST", "10.219.0.3:5432")
    monkeypatch.setenv("DB_NAME", "tradeforces")
    monkeypatch.delenv("DB_PORT", raising=False)

    url = get_db_url()

    assert url.startswith("postgresql+asyncpg://appuser:secret@10.219.0.3:5432/tradeforces")
    assert "sslmode=disable" in url


def test_get_db_url_uses_cloud_sql_socket_path(monkeypatch):
    monkeypatch.setenv("DB_USER", "appuser")
    monkeypatch.setenv("DB_PASSWORD", "secret")
    monkeypatch.setenv("DB_HOST", "/cloudsql/project:region:instance")
    monkeypatch.setenv("DB_NAME", "tradeforces")
    monkeypatch.delenv("DB_PORT", raising=False)

    url = get_db_url()

    assert url.startswith("postgresql+asyncpg://appuser:secret@/tradeforces")
    assert "host=%2Fcloudsql%2Fproject%3Aregion%3Ainstance" in url


@pytest.mark.asyncio
async def test_init_db_returns_false_when_database_unavailable(monkeypatch):
    def fail_create_engine(*_args, **_kwargs):
        raise OSError("[Errno -2] Name or service not known")

    monkeypatch.setattr(core_module, "create_async_db_engine", fail_create_engine)

    assert await core_module.init_db() is False
