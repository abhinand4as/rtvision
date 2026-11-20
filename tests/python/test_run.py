# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
"""perceptra-run end to end (dry run): precedence, licence gate, error reporting, ONNX check."""

from __future__ import annotations

import os
from pathlib import Path

import pytest
from conftest import EXAMPLE_CONFIG, EXAMPLES, FIXTURE_MODELS
from perceptra_tools import run
from test_onnx_inspect import CASES, make_onnx

BASE = [
    "--config",
    str(EXAMPLE_CONFIG),
    "--models-dir",
    str(EXAMPLES),
    "--dry-run",
    "--no-check-files",
]
# A test-only model declaring AGPL-3.0-only (no shipped example uses a non-permissive licence).
AGPL_MODEL = ["--models-dir", str(FIXTURE_MODELS), "--model", "yolo26s-people"]


def run_main(tmp_path: Path, extra: list[str], env: dict | None = None) -> int:
    e = {"PERCEPTRA_TRT_VERSION": "10.16.1", **(env or {})}
    return run.main([*BASE, "--out-dir", str(tmp_path / "gen"), *extra], env=e)


def test_permissive_model_runs(tmp_path: Path) -> None:
    assert run_main(tmp_path, ["--model", "rtdetr-r18-coco"]) == 0
    assert {p.name for p in (tmp_path / "gen").iterdir()} == {
        "ds-main.ini",
        "pgie.ini",
        "parser.ini",
        "app.ini",
        "kafka.properties",
    }
    assert os.environ["PERCEPTRA_PARSER_CONFIG"] == str(tmp_path / "gen" / "parser.ini")


def test_license_gate_exit_3(tmp_path: Path, capsys) -> None:
    assert run_main(tmp_path, [*AGPL_MODEL]) == run.EXIT_LICENSE
    err = capsys.readouterr().err
    assert "MODEL LICENCE: AGPL-3.0-only" in err
    assert "PERCEPTRA_ACCEPT_MODEL_LICENSE=AGPL-3.0-only" in err
    assert not (tmp_path / "gen").exists()  # nothing generated
    # A different accepted id does not unlock it.
    assert (
        run_main(tmp_path, [*AGPL_MODEL], {"PERCEPTRA_ACCEPT_MODEL_LICENSE": "MIT"})
        == run.EXIT_LICENSE
    )
    assert (
        run_main(
            tmp_path,
            [*AGPL_MODEL],
            {"PERCEPTRA_ACCEPT_MODEL_LICENSE": "AGPL-3.0-only"},
        )
        == 0
    )
    assert "license=AGPL-3.0-only" in (tmp_path / "gen" / "app.ini").read_text()


def test_model_from_env_and_cli_precedence(tmp_path: Path) -> None:
    assert run_main(tmp_path, [], {"PERCEPTRA_MODEL": "rtdetr-r18-coco"}) == 0
    # CLI wins over env: the env model would fail (unknown).
    assert run_main(tmp_path, ["--model", "rtdetr-r18-coco"], {"PERCEPTRA_MODEL": "nope"}) == 0
    assert run_main(tmp_path, [], {"PERCEPTRA_MODEL": "nope"}) == run.EXIT_CONFIG


def test_no_model(tmp_path: Path, capsys) -> None:
    assert run_main(tmp_path, []) == run.EXIT_CONFIG
    assert "no model selected" in capsys.readouterr().err


def test_invalid_config_reports_field_path(tmp_path: Path, capsys) -> None:
    bad = tmp_path / "bad.yaml"
    bad.write_text(EXAMPLE_CONFIG.read_text().replace("max_streams: 4", "max_streams: -1"))
    rc = run.main(
        [
            "--config",
            str(bad),
            "--models-dir",
            str(EXAMPLES),
            "--model",
            "rtdetr-r18-coco",
            "--dry-run",
            "--no-check-files",
            "--out-dir",
            str(tmp_path / "gen"),
        ],
        env={},
    )
    assert rc == run.EXIT_CONFIG
    err = capsys.readouterr().err
    assert "configuration error" in err and "service.max_streams: -1" in err


def test_env_override_reaches_generated_files(tmp_path: Path) -> None:
    assert (
        run_main(
            tmp_path,
            ["--model", "rtdetr-r18-coco"],
            {"PERCEPTRA__PUBLISH__KAFKA__BROKERS": "broker:29092"},
        )
        == 0
    )
    assert "kafka_brokers=broker:29092" in (tmp_path / "gen" / "app.ini").read_text()


def _model_with_onnx(tmp_path: Path, outputs) -> Path:
    mdir = tmp_path / "models" / "rtdetr-r18-coco"
    mdir.mkdir(parents=True)
    for f in ("model.yaml", "labels.txt"):
        (mdir / f).write_text((EXAMPLES / "rtdetr-r18-coco" / f).read_text())
    make_onnx(mdir / "rtdetr_r18.onnx", outputs)
    return mdir


def test_onnx_checked_against_family(tmp_path: Path, capsys) -> None:
    mdir = _model_with_onnx(tmp_path, CASES["detr"])
    args = [
        "--config",
        str(EXAMPLE_CONFIG),
        "--model",
        str(mdir),
        "--dry-run",
        "--no-check-files",
        "--out-dir",
        str(tmp_path / "gen"),
    ]
    assert run.main(args, env={"PERCEPTRA_TRT_VERSION": "x"}) == 0
    bad = _model_with_onnx(tmp_path / "bad", CASES["yolo_e2e"])
    args[3] = str(bad)
    assert run.main(args, env={}) == run.EXIT_CONFIG
    assert "output.layers ['boxes', 'scores'] not found" in capsys.readouterr().err


def test_missing_onnx_when_checking_files(tmp_path: Path, capsys) -> None:
    rc = run.main(
        [
            "--config",
            str(EXAMPLE_CONFIG),
            "--models-dir",
            str(EXAMPLES),
            "--model",
            "rtdetr-r18-coco",
            "--dry-run",
            "--out-dir",
            str(tmp_path / "gen"),
        ],
        env={},
    )
    assert rc == run.EXIT_CONFIG
    assert "files.onnx" in capsys.readouterr().err


@pytest.mark.parametrize("force_tcp", [True, False])
def test_no_force_tcp_flag(tmp_path: Path, capsys, force_tcp: bool) -> None:
    env = {"PERCEPTRA__SERVICE__SOURCES__FORCE_TCP": str(force_tcp).lower()}
    assert run_main(tmp_path, ["--model", "rtdetr-r18-coco"], env) == 0
    assert ("--no-force-tcp" in capsys.readouterr().err) is (not force_tcp)


def test_detect_trt_version_env() -> None:
    assert run.detect_trt_version({"PERCEPTRA_TRT_VERSION": "9.9.9"}) == "9.9.9"
