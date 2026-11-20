# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 <PROJECT_OWNER>
"""``perceptra-run``: validate configs, generate DeepStream configs, then exec the binary.

Steps: resolve files (CLI > env > default) -> load + env-override + validate perceptra.yaml ->
resolve + validate model.yaml -> reject unsupported tasks -> licence gate -> inspect the ONNX
against the declared family -> generate ds-main.ini / pgie.ini / parser.ini / app.ini ->
prepare the engine cache -> export PERCEPTRA_PARSER_CONFIG -> ``os.execv`` the binary.

Exit codes: 2 configuration error, 3 model licence not accepted (docs/CONFIGURATION.md).
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from collections.abc import Mapping
from datetime import datetime, timezone
from pathlib import Path

from . import __version__, gen_configs, onnx_inspect
from .schemas import ConfigError, load_model_config, load_service_config

EXIT_CONFIG = 2
EXIT_LICENSE = 3

DEFAULT_CONFIG = "/opt/perceptra/config/perceptra.yaml"
DEFAULT_MODELS_DIR = "/opt/perceptra/models"
DEFAULT_ENGINE_DIR = "/opt/perceptra/engines"
DEFAULT_OUT_DIR = "/tmp/perceptra/generated"
DEFAULT_BINARY = "/opt/perceptra/bin/perceptra"

PERMISSIVE_LICENSES = frozenset(
    {"Apache-2.0", "MIT", "BSD-2-Clause", "BSD-3-Clause", "CC0-1.0", "CC-BY-4.0"}
)
TRT_VERSION_HEADER = "/usr/include/x86_64-linux-gnu/NvInferVersion.h"


def log(level: str, msg: str) -> None:
    ts = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"
    print(f"ts={ts} level={level} comp=run {msg}", file=sys.stderr, flush=True)


def pick(cli: str | None, env: Mapping[str, str], var: str, default: str | None) -> str | None:
    """CLI flag > environment variable > default."""
    if cli:
        return cli
    if env.get(var):
        return env[var]
    return default


def resolve_model(model: str, models_dir: Path) -> tuple[Path, Path]:
    """`model` is a name under models_dir, a model directory, or a path to model.yaml.
    Returns (model.yaml path, model directory)."""
    p = Path(model)
    candidates = []
    if p.suffix in (".yaml", ".yml"):
        candidates.append(p)
    else:
        candidates += [p / "model.yaml", models_dir / model / "model.yaml"]
    for c in candidates:
        if c.is_file():
            return c, c.parent
    raise ConfigError(
        f"model '{model}': no model.yaml found (looked in: {', '.join(map(str, candidates))})"
    )


def detect_trt_version(env: Mapping[str, str]) -> str:
    """TensorRT version for the engine cache name: env override, header, dpkg, else 'unknown'."""
    if env.get("PERCEPTRA_TRT_VERSION"):
        return env["PERCEPTRA_TRT_VERSION"]
    try:
        text = Path(TRT_VERSION_HEADER).read_text(encoding="utf-8")
        parts = [
            re.search(rf"#define\s+TRT_{k}_ENTERPRISE\s+(\d+)", text)
            or re.search(rf"#define\s+NV_TENSORRT_{k}\s+(\d+)", text)
            for k in ("MAJOR", "MINOR", "PATCH")
        ]
        if all(parts):
            return ".".join(m.group(1) for m in parts)  # type: ignore[union-attr]
    except OSError:
        pass
    try:
        out = subprocess.run(
            ["dpkg-query", "-W", "-f=${Version}", "libnvinfer10"],
            capture_output=True,
            text=True,
            check=True,
        ).stdout
        m = re.match(r"(\d+\.\d+\.\d+)", out)
        if m:
            return m.group(1)
    except (OSError, subprocess.CalledProcessError):
        pass
    return "unknown"


def license_gate(spdx: str, env: Mapping[str, str]) -> str | None:
    """Returns the accepted licence id, or None if the model must not start."""
    if spdx in PERMISSIVE_LICENSES:
        return spdx
    accepted = env.get("PERCEPTRA_ACCEPT_MODEL_LICENSE", "")
    lines = [
        f"MODEL LICENCE: {spdx}",
        "The model weights are not under a permissive licence on the allow-list",
        f"({', '.join(sorted(PERMISSIVE_LICENSES))}).",
        "Check that your use complies with the licence (see docs/LICENSING.md).",
    ]
    if accepted == spdx:
        lines.append(f"Accepted via PERCEPTRA_ACCEPT_MODEL_LICENSE={spdx}.")
    else:
        lines.append(f"To start anyway, set PERCEPTRA_ACCEPT_MODEL_LICENSE={spdx}")
    width = max(len(x) for x in lines) + 4
    box = ["*" * width] + [f"* {x.ljust(width - 4)} *" for x in lines] + ["*" * width]
    print("\n".join(box), file=sys.stderr, flush=True)
    return spdx if accepted == spdx else None


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(prog="perceptra-run", description=__doc__.splitlines()[0])
    ap.add_argument(
        "--config", help=f"perceptra.yaml (env PERCEPTRA_CONFIG, default {DEFAULT_CONFIG})"
    )
    ap.add_argument("--model", help="model name under --models-dir, or path (env PERCEPTRA_MODEL)")
    ap.add_argument("--models-dir", help=f"env PERCEPTRA_MODELS_DIR, default {DEFAULT_MODELS_DIR}")
    ap.add_argument("--engine-dir", help=f"env PERCEPTRA_ENGINE_DIR, default {DEFAULT_ENGINE_DIR}")
    ap.add_argument(
        "--out-dir",
        help=f"generated files (env PERCEPTRA_GENERATED_DIR, default {DEFAULT_OUT_DIR})",
    )
    ap.add_argument(
        "--binary", help=f"perceptra binary (env PERCEPTRA_BINARY, default {DEFAULT_BINARY})"
    )
    ap.add_argument("--dry-run", action="store_true", help="validate and generate, do not exec")
    ap.add_argument(
        "--no-check-files",
        action="store_true",
        help="do not require ONNX/labels/DeepStream files to exist (outside the container)",
    )
    ap.add_argument("--version", action="version", version=f"perceptra-run {__version__}")
    return ap


def main(argv: list[str] | None = None, env: Mapping[str, str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    env = dict(os.environ if env is None else env)
    check_files = not args.no_check_files

    try:
        config_path = Path(pick(args.config, env, "PERCEPTRA_CONFIG", DEFAULT_CONFIG))  # type: ignore[arg-type]
        model_arg = pick(args.model, env, "PERCEPTRA_MODEL", None)
        if not model_arg:
            raise ConfigError("no model selected: pass --model or set PERCEPTRA_MODEL")
        models_dir = Path(pick(args.models_dir, env, "PERCEPTRA_MODELS_DIR", DEFAULT_MODELS_DIR))  # type: ignore[arg-type]
        engine_dir = Path(pick(args.engine_dir, env, "PERCEPTRA_ENGINE_DIR", DEFAULT_ENGINE_DIR))  # type: ignore[arg-type]
        out_dir = Path(pick(args.out_dir, env, "PERCEPTRA_GENERATED_DIR", DEFAULT_OUT_DIR))  # type: ignore[arg-type]
        binary = pick(args.binary, env, "PERCEPTRA_BINARY", DEFAULT_BINARY)

        service, overrides = load_service_config(config_path, env)
        for path in overrides:
            log("info", f"event=env_override key={path}")
        model_yaml, model_dir = resolve_model(model_arg, models_dir)
        model = load_model_config(model_yaml)
        log("info", f"event=config_loaded config={config_path} model={model_yaml}")

        if model["task"] != "detect":
            raise ConfigError(
                f"{model_yaml}: task: '{model['task']}' is not supported in this version "
                "(v1 supports: detect)"
            )

        accepted = license_gate(model["license"]["spdx"], env)
        if accepted is None:
            log("error", f"event=license_rejected license={model['license']['spdx']}")
            return EXIT_LICENSE
        log("info", f"event=license_accepted license={accepted}")

        ctx = gen_configs.GenContext(
            service=service,
            model=model,
            model_dir=model_dir.resolve(),
            engine_dir=engine_dir,
            out_dir=out_dir,
            trt_version=detect_trt_version(env),
            check_files=check_files,
            accepted_license=accepted,
        )

        if not gen_configs.is_builtin(model):
            onnx_path = (model_dir / model["files"]["onnx"]).resolve()
            if onnx_path.is_file():
                inputs, outputs = onnx_inspect.load_io(onnx_path)
                out = model["output"]
                rep = onnx_inspect.check(
                    inputs,
                    outputs,
                    out["family"],
                    out["num_classes"],
                    out.get("layers"),
                    out["has_background_class"],
                    service["service"]["max_streams"],
                    (model["input"]["width"], model["input"]["height"]),
                )
                for w in rep.warnings:
                    log("warn", f'event=onnx_check msg="{w}"')
                if rep.errors:
                    raise ConfigError(f"{onnx_path}: " + "; ".join(rep.errors))
            elif check_files:
                raise ConfigError(f"{model_yaml}: files.onnx: '{onnx_path}' not found")

        files = gen_configs.generate(ctx)
        written = gen_configs.write_files(files, out_dir)
        for path in written:
            log("info", f"event=generated file={path}")
        if check_files:
            for name in gen_configs.prepare_engine_cache(ctx):
                log(
                    "info",
                    f"event=engine_cache_copy file={name} dir={gen_configs.engine_cache_dir(ctx)}",
                )
    except ConfigError as exc:
        print(f"perceptra-run: configuration error: {exc}", file=sys.stderr, flush=True)
        return EXIT_CONFIG

    parser_ini = out_dir / "parser.ini"
    if parser_ini.exists():
        os.environ["PERCEPTRA_PARSER_CONFIG"] = str(parser_ini)
    cmd = [binary, "-c", str(out_dir / "ds-main.ini"), "-a", str(out_dir / "app.ini")]
    if not service["service"]["sources"]["force_tcp"]:
        cmd.append("--no-force-tcp")
    if args.dry_run:
        log("info", f'event=dry_run cmd="{" ".join(cmd)}"')
        return 0
    log("info", f'event=exec cmd="{" ".join(cmd)}"')
    try:
        os.execv(cmd[0], cmd)  # noqa: S606 - replaces this process with the service binary
    except OSError as exc:
        print(f"perceptra-run: cannot exec {cmd[0]}: {exc}", file=sys.stderr)
        return EXIT_CONFIG
    return 0  # not reached


if __name__ == "__main__":
    sys.exit(main())
