# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
"""JSON Schema validation helpers for ``perceptra.yaml`` and ``model.yaml``.

* YAML is loaded with ``yaml.safe_load``.
* ``PERCEPTRA__A__B=value`` environment variables override scalars in ``perceptra.yaml``
  (path segments lower-cased, ``__`` as separator, value parsed as a YAML scalar).
* Validation uses JSON Schema 2020-12. Errors carry the field path, e.g.
  ``service.max_streams: 0 is less than the minimum of 1``; unknown keys are rejected.
* Defaults declared in the schema are filled in after validation, so the generator can rely on
  every documented key being present.
"""

from __future__ import annotations

import copy
import json
import os
from collections.abc import Mapping
from importlib import resources
from pathlib import Path
from typing import Any

import jsonschema
import yaml

ENV_OVERRIDE_PREFIX = "PERCEPTRA__"


class ConfigError(Exception):
    """A configuration problem the user must fix. The message names the file and field."""


def _schema_dir() -> Path:
    """schemas/ in the source tree, or the copy installed next to the package."""
    env = os.environ.get("PERCEPTRA_SCHEMA_DIR")
    if env:
        return Path(env)
    packaged = resources.files("perceptra_tools") / "schemas"
    if packaged.is_dir():
        return Path(str(packaged))
    return Path(__file__).resolve().parents[2] / "schemas"


def load_schema(name: str) -> dict[str, Any]:
    path = _schema_dir() / name
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)


def load_yaml(path: Path) -> Any:
    try:
        with open(path, encoding="utf-8") as fh:
            return yaml.safe_load(fh)
    except FileNotFoundError as exc:
        raise ConfigError(f"{path}: file not found") from exc
    except yaml.YAMLError as exc:
        raise ConfigError(f"{path}: invalid YAML: {exc}") from exc


def format_path(path: Any) -> str:
    """['service', 'sources', 'latency_ms'] -> service.sources.latency_ms; list index -> [i]."""
    out = ""
    for part in path:
        if isinstance(part, int):
            out += f"[{part}]"
        else:
            out += ("." if out else "") + str(part)
    return out or "<root>"


def _error_message(err: jsonschema.ValidationError) -> str:
    where = format_path(err.absolute_path)
    if err.validator == "additionalProperties":
        known = set(err.schema.get("properties", {}))
        extra = sorted(k for k in err.instance if k not in known)
        keys = ", ".join(repr(k) for k in extra)
        return f"{where}: unknown key(s) {keys}"
    if err.validator == "required":
        return f"{where}: {err.message}"
    return f"{where}: {err.message}"


def validate(instance: Any, schema: Mapping[str, Any], source: str) -> None:
    """Raises ConfigError listing every violation (sorted by path) with field paths."""
    validator = jsonschema.Draft202012Validator(schema)
    errors = sorted(validator.iter_errors(instance), key=lambda e: list(map(str, e.absolute_path)))
    if errors:
        lines = "\n  ".join(_error_message(e) for e in errors)
        raise ConfigError(f"{source}: invalid configuration:\n  {lines}")


def apply_defaults(instance: Any, schema: Mapping[str, Any]) -> Any:
    """Fills `default` values of object properties (recursively). Returns a new object."""
    if not isinstance(instance, dict) or schema.get("type") not in ("object", ["object", "null"]):
        return instance
    out = copy.deepcopy(instance)
    for key, sub in schema.get("properties", {}).items():
        if key not in out and "default" in sub:
            out[key] = copy.deepcopy(sub["default"])
        if key not in out and sub.get("type") == "object" and "properties" in sub:
            out[key] = {}
        if key in out:
            out[key] = apply_defaults(out[key], sub)
    return out


def apply_env_overrides(data: dict[str, Any], env: Mapping[str, str]) -> list[str]:
    """Applies PERCEPTRA__A__B=value overrides in place. Returns the applied paths (sorted)."""
    applied = []
    for name in sorted(env):
        if not name.startswith(ENV_OVERRIDE_PREFIX):
            continue
        parts = [p.lower() for p in name[len(ENV_OVERRIDE_PREFIX) :].split("__")]
        if not parts or any(p == "" for p in parts):
            raise ConfigError(f"{name}: malformed override variable name")
        try:
            value = yaml.safe_load(env[name]) if env[name] != "" else ""
        except yaml.YAMLError as exc:
            raise ConfigError(f"{name}: value is not a valid YAML scalar: {exc}") from exc
        if isinstance(value, (dict, list)):
            raise ConfigError(f"{name}: only scalar values can be overridden")
        node = data
        for part in parts[:-1]:
            child = node.get(part)
            if child is None:
                child = node[part] = {}
            if not isinstance(child, dict):
                raise ConfigError(f"{name}: '{part}' is not a section")
            node = child
        node[parts[-1]] = value
        applied.append(".".join(parts))
    return applied


def load_service_config(
    path: Path, env: Mapping[str, str] | None = None
) -> tuple[dict[str, Any], list[str]]:
    """Loads, overrides (env), validates and default-fills perceptra.yaml."""
    data = load_yaml(path)
    if data is None:
        data = {}
    if not isinstance(data, dict):
        raise ConfigError(f"{path}: top level must be a mapping")
    applied = apply_env_overrides(data, env if env is not None else os.environ)
    schema = load_schema("service_config.schema.json")
    validate(data, schema, str(path))
    return apply_defaults(data, schema), applied


def load_model_config(path: Path) -> dict[str, Any]:
    """Loads, validates and default-fills model.yaml."""
    data = load_yaml(path)
    if not isinstance(data, dict):
        raise ConfigError(f"{path}: top level must be a mapping")
    schema = load_schema("model.schema.json")
    validate(data, schema, str(path))
    return apply_defaults(data, schema)


def validate_event(event: Any) -> None:
    """Validates one detection event against schemas/detection_event.v1.schema.json."""
    validate(event, load_schema("detection_event.v1.schema.json"), "event")
