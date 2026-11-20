# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 <PROJECT_OWNER>
"""Schemas: examples validate, errors carry field paths, env overrides, event schema."""

from __future__ import annotations

import copy
import json
import re
from pathlib import Path

import pytest
from conftest import EXAMPLE_CONFIG, EXAMPLES, REPO
from perceptra_tools.schemas import (
    ConfigError,
    apply_env_overrides,
    load_model_config,
    load_service_config,
    validate_event,
)


def test_example_service_config_valid() -> None:
    cfg, applied = load_service_config(EXAMPLE_CONFIG, env={})
    assert applied == []
    assert cfg["service"]["max_streams"] == 4
    # defaults filled for keys omitted in the file
    assert cfg["tracker"]["reid"] is False


@pytest.mark.parametrize("model", sorted(p.name for p in EXAMPLES.iterdir() if p.is_dir()))
def test_example_models_valid(model: str) -> None:
    m = load_model_config(EXAMPLES / model / "model.yaml")
    assert m["name"] == model
    assert "thresholds" in m["classes"]


def test_minimal_service_config_gets_defaults(tmp_path: Path) -> None:
    p = tmp_path / "p.yaml"
    p.write_text("service: {max_streams: 2}\npublish: {backend: stdout}\n")
    cfg, _ = load_service_config(p, env={})
    assert cfg["service"]["rest_port"] == 9000
    assert cfg["service"]["sources"]["force_tcp"] is True
    assert cfg["publish"]["queue"]["on_full"] == "drop_oldest"
    assert cfg["observability"]["ops_port"] == 9464
    assert cfg["timestamps"]["source"] == "ntp"


@pytest.mark.parametrize(
    ("yaml_text", "expected"),
    [
        (
            "service: {max_streams: 2, foo: 1}\npublish: {backend: stdout}\n",
            r"service: unknown key\(s\) 'foo'",
        ),
        (
            "service: {max_streams: 0}\npublish: {backend: stdout}\n",
            r"service\.max_streams: 0 is less than the minimum of 1",
        ),
        (
            "service: {max_streams: 2, sources: {latency_ms: fast}}\npublish: {backend: stdout}\n",
            r"service\.sources\.latency_ms: 'fast' is not of type 'integer'",
        ),
        (
            "service: {max_streams: 2}\npublish: {backend: kafka}\n",
            r"publish: 'kafka' is a required property",
        ),
        ("service: {max_streams: 2}\n", r"<root>: 'publish' is a required property"),
        ("service: {max_streams: 2}\npublish: {backend: smoke}\n", r"publish\.backend: 'smoke'"),
    ],
)
def test_invalid_service_config_has_field_path(
    tmp_path: Path, yaml_text: str, expected: str
) -> None:
    p = tmp_path / "p.yaml"
    p.write_text(yaml_text)
    with pytest.raises(ConfigError) as exc:
        load_service_config(p, env={})
    assert re.search(expected, str(exc.value)), str(exc.value)
    assert str(p) in str(exc.value)


def test_invalid_yaml_syntax(tmp_path: Path) -> None:
    p = tmp_path / "p.yaml"
    p.write_text("service: [unclosed\n")
    with pytest.raises(ConfigError, match="invalid YAML"):
        load_service_config(p, env={})


def test_model_unknown_key_and_bad_name(tmp_path: Path) -> None:
    src = (EXAMPLES / "rtdetr-r18-coco" / "model.yaml").read_text()
    p = tmp_path / "model.yaml"
    p.write_text(src.replace("name: rtdetr-r18-coco", "name: Bad_Name\nextra_key: 1"))
    with pytest.raises(ConfigError) as exc:
        load_model_config(p)
    msg = str(exc.value)
    assert "<root>: unknown key(s) 'extra_key'" in msg
    assert "name: 'Bad_Name' does not match" in msg


def test_env_overrides_types_and_paths() -> None:
    data = {"publish": {"backend": "kafka", "kafka": {"brokers": "a:1"}}}
    applied = apply_env_overrides(
        data,
        {
            "PERCEPTRA__PUBLISH__KAFKA__BROKERS": "kafka:9092",
            "PERCEPTRA__SERVICE__MAX_STREAMS": "8",
            "PERCEPTRA__SERVICE__SOURCES__FORCE_TCP": "false",
            "PERCEPTRA__INFERENCE__PRECISION": "null",
            "PERCEPTRA_MODEL": "ignored",  # single underscore: not an override
        },
    )
    assert applied == [
        "inference.precision",
        "publish.kafka.brokers",
        "service.max_streams",
        "service.sources.force_tcp",
    ]
    assert data["publish"]["kafka"]["brokers"] == "kafka:9092"
    assert data["service"]["max_streams"] == 8
    assert data["service"]["sources"]["force_tcp"] is False
    assert data["inference"]["precision"] is None


def test_env_override_validated(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match=r"service\.max_streams"):
        load_service_config(EXAMPLE_CONFIG, env={"PERCEPTRA__SERVICE__MAX_STREAMS": "0"})
    with pytest.raises(ConfigError, match="only scalar"):
        apply_env_overrides({}, {"PERCEPTRA__SERVICE": "{a: 1}"})
    with pytest.raises(ConfigError, match="not a section"):
        apply_env_overrides({"service": 3}, {"PERCEPTRA__SERVICE__X": "1"})


def _doc_event() -> dict:
    text = (REPO / "docs" / "EVENT_SCHEMA.md").read_text()
    block = re.search(r"```json\n(.*?)```", text, re.S)
    assert block
    return json.loads(block.group(1))


def test_event_schema_doc_example_valid() -> None:
    validate_event(_doc_event())


@pytest.mark.parametrize(
    "mutate",
    [
        lambda e: e.pop("sensor"),
        lambda e: e.update(timestamp="20-11-2026"),
        lambda e: e.update(schema="perceptra.detection.v2"),
        lambda e: e["objects"][0].update(confidence=1.5),
        lambda e: e["objects"][0]["bbox"].pop("width"),
    ],
)
def test_event_schema_rejects_mutations(mutate) -> None:
    e = copy.deepcopy(_doc_event())
    mutate(e)
    with pytest.raises(ConfigError):
        validate_event(e)
