from __future__ import annotations

from pathlib import Path

from ue_editor_mcp.server_unreal_logs import (
    LogFilter,
    _read_saved_logs,
)


def _make_workspace(tmp_path: Path) -> Path:
    root = tmp_path / "workspace"
    logs_dir = root / "Saved" / "Logs"
    logs_dir.mkdir(parents=True, exist_ok=True)
    return root


def test_saved_tail_reads_latest_file(tmp_path: Path) -> None:
    workspace = _make_workspace(tmp_path)
    log_file = workspace / "Saved" / "Logs" / "p110_2.log"

    lines = [f"[2026.02.20-10:12:{i:02d}:000][  0]LogMCP: line {i}" for i in range(30)]
    log_file.write_text("\n".join(lines), encoding="utf-8")

    result = _read_saved_logs(
        workspace_root=workspace,
        project_name="p110_2",
        tail_lines=10,
        max_bytes=4096,
        cursor=None,
        log_filter=LogFilter(),
        source_label="offline_saved",
        notes=[],
    )

    assert result["success"] is True
    assert result["source"] == "offline_saved"
    assert result["linesReturned"] == 10
    assert "line 29" in result["content"]
    assert result["cursor"].startswith("file:")


def test_saved_cursor_increment_returns_delta_only(tmp_path: Path) -> None:
    workspace = _make_workspace(tmp_path)
    log_file = workspace / "Saved" / "Logs" / "p110_2.log"

    log_file.write_text("a\nb\nc\n", encoding="utf-8")
    first = _read_saved_logs(
        workspace_root=workspace,
        project_name="p110_2",
        tail_lines=50,
        max_bytes=4096,
        cursor=None,
        log_filter=LogFilter(),
        source_label="offline_saved",
        notes=[],
    )
    first_cursor = first["cursor"]

    with log_file.open("a", encoding="utf-8") as handle:
        handle.write("d\ne\n")

    second = _read_saved_logs(
        workspace_root=workspace,
        project_name="p110_2",
        tail_lines=50,
        max_bytes=4096,
        cursor=first_cursor,
        log_filter=LogFilter(),
        source_label="offline_saved",
        notes=[],
    )

    assert second["success"] is True
    assert "d" in second["content"]
    assert "e" in second["content"]
    assert "[SavedLog] a" not in second["content"]


def test_saved_filters_apply_min_verbosity(tmp_path: Path) -> None:
    workspace = _make_workspace(tmp_path)
    log_file = workspace / "Saved" / "Logs" / "p110_2.log"

    log_file.write_text(
        "\n".join([
            "[x]LogMCP: Log: normal",
            "[x]LogMCP: Warning: warn",
            "[x]LogMCP: Error: err",
        ]),
        encoding="utf-8",
    )

    result = _read_saved_logs(
        workspace_root=workspace,
        project_name="p110_2",
        tail_lines=20,
        max_bytes=4096,
        cursor=None,
        log_filter=LogFilter(min_verbosity="Error"),
        source_label="offline_saved",
        notes=[],
    )

    assert result["success"] is True
    assert "err" in result["content"]
    assert "warn" not in result["content"]
    assert "normal" not in result["content"]
