"""Unit tests for scripts/gshhg_loader.download_gshhg.

The real bundle is 150 MB on servers that time out, so urlopen is
swapped for a fake that serves canned bytes (or raises) per URL. The
pinned checksum is swapped for the hash of those canned bytes.
"""
import hashlib
import io
import urllib.error

import pytest

import gshhg_loader as gl

GOOD = b"pretend this is the gshhg zip"
MIRROR, UPSTREAM = "https://mirror.test/g.zip", "https://upstream.test/g.zip"


@pytest.fixture
def fake_net(monkeypatch):
    """Returns (responses, calls). responses maps url → bytes or an
    exception to raise; calls records every url requested."""
    responses: dict = {}
    calls: list[str] = []

    def fake_urlopen(url, timeout):
        calls.append(url)
        r = responses[url]
        if isinstance(r, Exception):
            raise r
        return io.BytesIO(r)

    monkeypatch.setattr(gl.urllib.request, "urlopen", fake_urlopen)
    monkeypatch.setattr(gl.time, "sleep", lambda s: None)
    monkeypatch.setattr(gl, "GSHHG_SOURCES", (MIRROR, UPSTREAM))
    monkeypatch.setattr(gl, "GSHHG_SHA256", hashlib.sha256(GOOD).hexdigest())
    return responses, calls


def test_falls_back_to_second_source_when_first_times_out(fake_net, tmp_path):
    responses, calls = fake_net
    responses[MIRROR] = urllib.error.URLError(TimeoutError("timed out"))
    responses[UPSTREAM] = GOOD
    dest = tmp_path / "g.zip"

    gl.download_gshhg(dest)

    assert dest.read_bytes() == GOOD
    assert calls == [MIRROR] * gl.ATTEMPTS_PER_SOURCE + [UPSTREAM]
    assert not (tmp_path / "g.zip.part").exists()


def test_wrong_checksum_is_discarded_and_next_source_tried(fake_net, tmp_path):
    responses, calls = fake_net
    responses[MIRROR] = b"an html error page"
    responses[UPSTREAM] = GOOD
    dest = tmp_path / "g.zip"

    gl.download_gshhg(dest)

    assert dest.read_bytes() == GOOD
    # No retry against the source that served wrong bytes.
    assert calls == [MIRROR, UPSTREAM]


def test_all_sources_failing_raises_and_leaves_nothing(fake_net, tmp_path):
    responses, _ = fake_net
    responses[MIRROR] = urllib.error.URLError("down")
    responses[UPSTREAM] = b"truncated"
    dest = tmp_path / "g.zip"

    with pytest.raises(RuntimeError, match="any source") as exc:
        gl.download_gshhg(dest)

    assert MIRROR in str(exc.value) and UPSTREAM in str(exc.value)
    assert list(tmp_path.iterdir()) == []


def test_existing_zip_skips_network(fake_net, tmp_path, monkeypatch):
    _, calls = fake_net
    (tmp_path / "gshhg-shp.zip").write_bytes(b"cached")
    unpacked = []

    class FakeZip:
        def __init__(self, path):
            pass

        def __enter__(self):
            return self

        def __exit__(self, *a):
            pass

        def extractall(self, dest):
            unpacked.append(dest)

    monkeypatch.setattr(gl.zipfile, "ZipFile", FakeZip)
    cache = tmp_path

    gl.ensure_gshhg_extracted(cache)

    assert calls == []
    assert unpacked == [cache / "gshhg"]
