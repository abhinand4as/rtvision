# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
"""Render ``rtvision.yaml`` + ``model.yaml`` into the DeepStream and RTVision config files.

Generated files (docs/CONFIGURATION.md "Generated artefacts"):

* ``ds-main.ini``     deepstream-app main config (sources, streammux, PGIE, tracker, sinks)
* ``pgie.ini``        nvinfer config (``family: builtin``: a copy of its own config
                      with paths moved to the engine cache, when it has an onnx-file)
* ``parser.ini``      read by librtvision_parsers.so (not for ``family: builtin``)
* ``app.ini``         read by the ``rtvision`` binary
* ``kafka.properties`` librdkafka passthrough properties (kafka backend only)
* ``tracker.yml``     tracker profile with ReID tensor output (tracker.reid only)

``generate()`` is pure (returns file name -> content) so it can be golden-tested; filesystem side
effects (writing, engine-cache symlinks) live in ``write_files()`` / ``prepare_engine_cache()``.
"""

from __future__ import annotations

import math
import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import jinja2
import yaml

from .schemas import ConfigError

DS_ROOT = "/opt/nvidia/deepstream/deepstream"
PARSER_LIB = "/opt/rtvision/lib/librtvision_parsers.so"

TRACKER_PROFILES = {
    "nvdcf_perf": "config_tracker_NvDCF_perf.yml",
    "nvdcf_accuracy": "config_tracker_NvDCF_accuracy.yml",
    "nvsort": "config_tracker_NvSORT.yml",
    "iou": "config_tracker_IOU.yml",
}
# Profiles whose sample config uses a ReID network (not shipped in the DeepStream image).
REID_PROFILES = {"nvdcf_accuracy"}
# Tracker user-meta pool when ReID tensors are attached to objects (DeepStream default: 32).
REID_USER_META_POOL_SIZE = 128
NETWORK_MODE = {"fp32": 0, "int8": 1, "fp16": 2}
COLOR_FORMAT = {"RGB": 0, "BGR": 1, "GRAY": 2}
# resize -> (maintain-aspect-ratio, symmetric-padding)
RESIZE = {"letterbox_center": (1, 1), "letterbox_topleft": (1, 0), "stretch": (0, 0)}
PARSE_FUNC = {
    "yolo_e2e": "NvDsInferParseRTVision_YoloE2E",
    "yolo_raw": "NvDsInferParseRTVision_YoloRaw",
    "detr": "NvDsInferParseRTVision_Detr",
    "efficient_nms": "NvDsInferParseRTVision_EfficientNms",
}
# nvinfer clustering: yolo_raw needs NMS (2); the others are already de-duplicated (4 = none).
CLUSTER_MODE = {"yolo_raw": 2}
CLUSTER_NONE = 4


@dataclass
class GenContext:
    service: dict[str, Any]  # validated + default-filled rtvision.yaml
    model: dict[str, Any]  # validated + default-filled model.yaml
    model_dir: Path  # directory of model.yaml (relative model files resolve here)
    engine_dir: Path  # TensorRT engine cache root
    out_dir: Path  # where the generated files are written
    trt_version: str = "unknown"
    ds_root: str = DS_ROOT
    parser_lib: str = PARSER_LIB
    check_files: bool = False  # True inside the container: referenced files must exist
    accepted_license: str | None = None


def _resolve(model_dir: Path, p: str) -> Path:
    path = Path(p)
    return path if path.is_absolute() else model_dir / path


def is_builtin(model: dict[str, Any]) -> bool:
    return model["output"]["family"] == "builtin"


def load_labels(ctx: GenContext) -> list[str] | None:
    """Labels (line index = class id), or None if the file is not readable here."""
    path = _resolve(ctx.model_dir, ctx.model["files"]["labels"])
    if not path.is_file():
        if ctx.check_files:
            raise ConfigError(f"model.files.labels: '{path}' not found")
        return None
    lines = path.read_text(encoding="utf-8").splitlines()
    while lines and not lines[-1].strip():
        lines.pop()
    return [line.strip() for line in lines]


def _class_ids(ctx: GenContext, labels: list[str] | None) -> tuple[list[int], dict[int, float]]:
    """(keep ids, per-class thresholds by id) from classes.keep / thresholds.per_class."""
    classes = ctx.model.get("classes", {})
    keep = classes.get("keep") or []
    per_class = classes.get("thresholds", {}).get("per_class") or {}
    if (keep or per_class) and labels is None:
        raise ConfigError(
            "model.classes: keep/per_class need the labels file, which is not readable here "
            f"({ctx.model['files']['labels']})"
        )
    index = {name: i for i, name in enumerate(labels or [])}
    keep_ids = []
    for i, name in enumerate(keep):
        if name not in index:
            raise ConfigError(f"model.classes.keep[{i}]: label '{name}' not in labels file")
        keep_ids.append(index[name])
    thr = {}
    for name, value in per_class.items():
        if name not in index:
            raise ConfigError(
                f"model.classes.thresholds.per_class.{name}: label not in labels file"
            )
        thr[index[name]] = value
    return sorted(set(keep_ids)), dict(sorted(thr.items()))


def precision(ctx: GenContext) -> str:
    return ctx.service["inference"].get("precision") or ctx.model["precision"]


def engine_cache_dir(ctx: GenContext) -> Path:
    return ctx.engine_dir / f"{ctx.model['name']}-trt{ctx.trt_version}"


def engine_paths(ctx: GenContext) -> tuple[Path, Path]:
    """(onnx-file as seen by nvinfer, model-engine-file).

    nvinfer always serialises a freshly built engine next to the *real path* of its
    ``onnx-file`` as ``<onnx-file>_b<batch>_gpu<id>_<precision>.engine``
    (nvdsinfer_model_builder.cpp; symlinks are resolved). To cache engines outside the
    (read-only) models volume, nvinfer is pointed at a COPY of the model files inside a
    per-model, per-TensorRT-version cache dir (see prepare_engine_cache()).
    """
    files = ctx.model["files"]
    svc = ctx.service["service"]
    onnx = _resolve(ctx.model_dir, files["onnx"])
    if files.get("engine"):
        return onnx, _resolve(ctx.model_dir, files["engine"])
    cache = engine_cache_dir(ctx)
    onnx_copy = cache / onnx.name
    engine = Path(f"{onnx_copy}_b{svc['max_streams']}_gpu{svc['gpu_id']}_{precision(ctx)}.engine")
    return onnx_copy, engine


# nvinfer [property] keys holding file paths (relative ones resolve against the config's dir).
_NVINFER_PATH_KEYS = (
    "onnx-file",
    "model-engine-file",
    "labelfile-path",
    "int8-calib-file",
    "custom-lib-path",
    "model-file",
    "proto-file",
    "mean-file",
    "tlt-encoded-model",
    "uff-file",
)
_NVINFER_PRECISION = {"0": "fp32", "1": "int8", "2": "fp16"}


def builtin_onnx(ctx: GenContext) -> Path | None:
    """Absolute ``onnx-file`` of a builtin model's nvinfer config, or None if the config is not
    readable here (host tests) or has no onnx-file (e.g. an engine-only config)."""
    cfg = ctx.model["files"].get("nvinfer_config")
    if not is_builtin(ctx.model) or not cfg or not Path(cfg).is_file():
        return None
    for line in Path(cfg).read_text(encoding="utf-8").splitlines():
        key, sep, value = line.partition("=")
        if sep and key.strip() == "onnx-file" and value.strip():
            return (Path(cfg).parent / value.strip()).resolve()
    return None


def builtin_pgie(ctx: GenContext, onnx: Path) -> str:
    """Copy of a builtin nvinfer config with absolute paths, ``onnx-file`` pointing at the copy
    in the engine cache and ``model-engine-file`` at the engine nvinfer will save there.

    Needed because nvinfer saves a built engine next to ``onnx-file``: the DeepStream samples
    directory is not writable for the non-root user, so without this the engine is rebuilt on
    every start. Everything else in the config is kept as is."""
    cfg = Path(ctx.model["files"]["nvinfer_config"])
    s = ctx.service["service"]
    cache = engine_cache_dir(ctx)
    lines = cfg.read_text(encoding="utf-8").splitlines()
    mode = "2"
    for line in lines:
        key, sep, value = line.partition("=")
        if sep and key.strip() == "network-mode":
            mode = value.strip()
    prec = _NVINFER_PRECISION.get(mode, "fp32")
    engine = f"{cache / onnx.name}_b{s['max_streams']}_gpu{s['gpu_id']}_{prec}.engine"
    out = [
        f"# Generated by rtvision-run from {cfg} (paths made absolute, model files and",
        f"# engine in the engine cache {cache}). Do not edit.",
    ]
    seen_engine = False
    section = ""
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            if section == "property" and not seen_engine:
                out.append(f"model-engine-file={engine}")
                seen_engine = True
            section = stripped[1:-1]
            out.append(line)
            continue
        key, sep, value = line.partition("=")
        key, value = key.strip(), value.strip()
        if section != "property" or not sep or stripped.startswith("#"):
            out.append(line)
        elif key == "onnx-file":
            out.append(f"onnx-file={cache / onnx.name}")
        elif key == "model-engine-file":
            out.append(f"model-engine-file={engine}")
            seen_engine = True
        elif key == "batch-size":
            out.append(f"batch-size={s['max_streams']}")
        elif key == "gpu-id":
            out.append(f"gpu-id={s['gpu_id']}")
        elif key in _NVINFER_PATH_KEYS and value and not Path(value).is_absolute():
            out.append(f"{key}={(cfg.parent / value).resolve()}")
        else:
            out.append(line)
    if section == "property" and not seen_engine:
        out.append(f"model-engine-file={engine}")
    return "\n".join(out) + "\n"


def prepare_engine_cache(ctx: GenContext) -> list[str]:
    """Copies the model files (ONNX plus external-data files, which must sit next to it) into
    the engine cache dir. A file is copied again only if its size or mtime changed; when any file
    changes, cached engines in that dir are deleted so a stale engine is never used.
    Returns the names of the copied files."""
    if ctx.model["files"].get("engine"):
        return []
    if is_builtin(ctx.model):
        onnx = builtin_onnx(ctx)
        if onnx is None:
            return []
        src_dir = onnx.parent
    else:
        src_dir = _resolve(ctx.model_dir, ctx.model["files"]["onnx"]).parent
    cache = engine_cache_dir(ctx)
    cache.mkdir(parents=True, exist_ok=True)
    copied = []
    for src in sorted(src_dir.iterdir()):
        if not src.is_file() or src.suffix == ".engine" or src.name == "model.yaml":
            continue
        dst = cache / src.name
        st = src.stat()
        if dst.is_file() and not dst.is_symlink():
            dt = dst.stat()
            if dt.st_size == st.st_size and int(dt.st_mtime) == int(st.st_mtime):
                continue
        if dst.is_symlink() or dst.exists():
            dst.unlink()
        tmp = dst.with_name(dst.name + ".tmp")
        shutil.copy2(src, tmp)
        tmp.replace(dst)
        copied.append(src.name)
    if copied:
        for stale in cache.glob("*.engine"):
            stale.unlink()
    return copied


def reid_engine_dir(ctx: GenContext) -> Path:
    return ctx.engine_dir / f"tracker-reid-trt{ctx.trt_version}"


def _reid_model_path(reid: dict[str, Any]) -> str | None:
    return reid.get("onnxFile") or reid.get("tltEncodedModel")


def _reid_tracker_config(ctx: GenContext, sample_file: str | None) -> str | None:
    """For ReID tracker profiles: checks that the ReID network exists (it is not shipped in the
    DeepStream image) and, when tracker.reid is on, renders tracker.yml = the in-image sample
    profile with ReID.outputReidTensor: 1 (features into object user meta) and the ReID engine
    redirected into the engine cache. Returns None when nothing has to be generated.

    The sample profile is read inside the container at runtime; it is never copied into the
    repository (docs/LICENSING.md §3)."""
    trk = ctx.service["tracker"]
    if sample_file is None or not Path(sample_file).is_file():
        if trk["reid"]:
            raise ConfigError(
                f"tracker.reid: needs the DeepStream tracker profile {sample_file} "
                "(run rtvision-run inside the RTVision image)"
            )
        return None
    # DeepStream tracker profiles start with an OpenCV-style "%YAML:1.0" directive that PyYAML
    # rejects; drop it for parsing and write it back in the generated file.
    text = Path(sample_file).read_text(encoding="utf-8")
    body = "\n".join(ln for ln in text.splitlines() if not ln.startswith("%YAML"))
    try:
        data = yaml.safe_load(body) or {}
    except yaml.YAMLError as exc:
        raise ConfigError(f"tracker.profile: cannot parse {sample_file}: {exc}") from exc
    reid = data.get("ReID")
    if not isinstance(reid, dict):
        if trk["reid"]:
            raise ConfigError(f"tracker.reid: {sample_file} has no ReID section")
        return None
    model_path = _reid_model_path(reid)
    if ctx.check_files and model_path and not Path(model_path).is_file():
        raise ConfigError(
            f"tracker.profile: '{trk['profile']}' needs the ReID model {model_path}, which is not "
            "shipped in the DeepStream image. Download NVIDIA's ReIdentificationNet as described "
            f"in {ctx.ds_root}/sources/tracker_ReID/README (NVIDIA model licence) and mount it "
            f"at {Path(model_path).parent}, or use tracker.profile: nvdcf_perf / nvsort / iou"
        )
    if not trk["reid"]:
        return None
    reid["outputReidTensor"] = 1
    if model_path:
        reid["modelEngineFile"] = str(
            reid_engine_dir(ctx) / Path(reid.get("modelEngineFile") or model_path + ".engine").name
        )
    header = (
        "# Generated by rtvision-run from the DeepStream sample profile\n"
        f"# {sample_file} with ReID.outputReidTensor: 1 and the ReID engine cached in\n"
        f"# {reid_engine_dir(ctx)}. Do not edit.\n"
    )
    return header + "%YAML:1.0\n" + yaml.safe_dump(data, sort_keys=False, default_flow_style=None)


def _tiler(n: int) -> tuple[int, int]:
    cols = math.ceil(math.sqrt(n))
    return math.ceil(n / cols), cols


def kafka_topic(service: dict[str, Any], model: dict[str, Any]) -> str:
    topic = service["publish"].get("kafka", {}).get("topic")
    if topic:
        return topic
    return model.get("messaging", {}).get("topic") or f"rtvision.{model['name']}"


def _env() -> jinja2.Environment:
    return jinja2.Environment(
        loader=jinja2.PackageLoader("rtvision_tools", "templates"),
        undefined=jinja2.StrictUndefined,
        keep_trailing_newline=True,
        trim_blocks=True,
        lstrip_blocks=True,
        autoescape=False,
    )


def _ini_bool(v: bool) -> int:
    return 1 if v else 0


def generate(ctx: GenContext) -> dict[str, str]:
    """Returns {file name: content}. Raises ConfigError on inconsistent configuration."""
    svc, model = ctx.service, ctx.model
    s = svc["service"]
    batch = s["max_streams"]
    family = model["output"]["family"]
    builtin = is_builtin(model)
    out_dir = ctx.out_dir

    if model["task"] != "detect":
        raise ConfigError(
            f"model.task: '{model['task']}' is not supported in this version (v1 supports: detect)"
        )

    labels = load_labels(ctx)
    nc = model["output"]["num_classes"]
    if labels is not None and len(labels) != nc:
        raise ConfigError(
            f"model.output.num_classes: {nc} but the labels file has {len(labels)} lines"
        )
    keep_ids, per_class_thr = _class_ids(ctx, labels)

    trk = svc["tracker"]
    tracker_file = None
    if trk["profile"] != "none":
        tracker_file = (
            f"{ctx.ds_root}/samples/configs/deepstream-app/" + TRACKER_PROFILES[trk["profile"]]
        )
        if ctx.check_files and not Path(tracker_file).is_file():
            raise ConfigError(
                f"tracker.profile: '{trk['profile']}' needs {tracker_file}, which is missing "
                "from this DeepStream image"
            )
    if trk["reid"] and trk["profile"] != "nvdcf_accuracy":
        raise ConfigError("tracker.reid: true requires tracker.profile: nvdcf_accuracy")
    if svc["publish"]["include_embeddings"] and not trk["reid"]:
        raise ConfigError("publish.include_embeddings: true requires tracker.reid: true")

    files: dict[str, str] = {}
    if trk["profile"] in REID_PROFILES:
        reid_yaml = _reid_tracker_config(ctx, tracker_file)
        if reid_yaml is not None:
            files["tracker.yml"] = reid_yaml
            tracker_file = str(out_dir / "tracker.yml")

    outputs = svc["outputs"]
    visual = bool(outputs["display"] or outputs["file"] or outputs["rtsp"])
    rows, cols = _tiler(batch)

    env = _env()
    pgie_path = out_dir / "pgie.ini"
    common = {"service": svc, "model": model, "s": s, "batch": batch}

    if builtin:
        nvinfer_config = model["files"].get("nvinfer_config")
        if not nvinfer_config:
            raise ConfigError("model.files.nvinfer_config: required for output.family 'builtin'")
        if ctx.check_files and not Path(nvinfer_config).is_file():
            raise ConfigError(f"model.files.nvinfer_config: '{nvinfer_config}' not found")
        pgie_config = nvinfer_config
        onnx = builtin_onnx(ctx)
        if onnx is not None:
            files["pgie.ini"] = builtin_pgie(ctx, onnx)
            pgie_config = str(pgie_path)
    else:
        pgie_config = str(pgie_path)
        onnx_file, engine_file = engine_paths(ctx)
        maintain, symmetric = RESIZE[model["input"]["resize"]]
        inp = model["input"]
        prec = precision(ctx)
        calib = model["files"].get("int8_calibration")
        filter_out = [i for i in range(nc) if i not in keep_ids] if keep_ids else []
        files["pgie.ini"] = env.get_template("pgie.ini.j2").render(
            **common,
            onnx_file=onnx_file,
            engine_file=engine_file,
            labels_file=_resolve(ctx.model_dir, model["files"]["labels"]),
            network_mode=NETWORK_MODE[prec],
            precision=prec,
            calib_file=_resolve(ctx.model_dir, calib) if calib else None,
            offsets=";".join(_num(m) for m in inp["mean"]),
            scale=_num(inp["scale"]),
            color_format=COLOR_FORMAT[inp["color"]],
            maintain_aspect_ratio=maintain,
            symmetric_padding=symmetric,
            num_classes=nc,
            parse_func=PARSE_FUNC[family],
            parser_lib=ctx.parser_lib,
            cluster_mode=CLUSTER_MODE.get(family, CLUSTER_NONE),
            filter_out=";".join(map(str, filter_out)),
            classes=model.get("classes", {}),
            per_class_thr=per_class_thr,
            nms=family in CLUSTER_MODE,
            num=_num,
        )
        out = model["output"]
        files["parser.ini"] = env.get_template("parser.ini.j2").render(
            family=family,
            layers=",".join(out.get("layers") or []),
            box_format=out.get("box_format"),
            activation=out.get("activation"),
            has_background_class=str(bool(out["has_background_class"])).lower(),
            num_classes=nc,
            allowed=",".join(map(str, keep_ids)),
            topk=model.get("classes", {}).get("topk", 300),
        )

    files["ds-main.ini"] = env.get_template("ds_main.ini.j2").render(
        **common,
        obs=svc["observability"],
        src=s["sources"],
        mux=s["streammux"],
        pgie_config=pgie_config,
        interval=svc["inference"]["interval"],
        tracker=trk,
        tracker_file=tracker_file,
        reid_meta_pool=REID_USER_META_POOL_SIZE,
        ts_source=svc["timestamps"]["source"],
        ds_root=ctx.ds_root,
        visual=visual,
        rows=rows,
        cols=cols,
        outputs=outputs,
        b=_ini_bool,
    )

    pub = svc["publish"]
    kafka = pub.get("kafka") or {}
    kafka_props_path = out_dir / "kafka.properties" if pub["backend"] == "kafka" else None
    files["app.ini"] = env.get_template("app.ini.j2").render(
        model=model,
        family=family,
        license=ctx.accepted_license or model["license"]["spdx"],
        precision=precision(ctx),
        hot_swap_file=svc["inference"].get("hot_swap_file") or "",
        pub=pub,
        queue=pub["queue"],
        kafka=kafka,
        topic=kafka_topic(svc, model) if pub["backend"] == "kafka" else "",
        kafka_props_path=kafka_props_path or "",
        file_path=(pub.get("file") or {}).get("path", ""),
        ts=svc["timestamps"],
        tracker=trk,
        obs=svc["observability"],
        service_name=s["name"],
        max_streams=batch,
        b=lambda v: "true" if v else "false",
    )
    if kafka_props_path:
        props = kafka.get("properties") or {}
        lines = ["# Generated by rtvision-run: librdkafka properties (publish.kafka.properties)."]
        lines += [f"{k}={v}" for k, v in sorted(props.items())]
        files["kafka.properties"] = "\n".join(lines) + "\n"
    return files


def _num(v: float | int) -> str:
    """Stable number formatting for INI files (no exponent, no trailing zeros)."""
    if isinstance(v, bool):
        return str(int(v))
    if isinstance(v, int) or float(v).is_integer():
        return str(int(v))
    return repr(float(v))


def write_files(files: dict[str, str], out_dir: Path) -> list[Path]:
    out_dir.mkdir(parents=True, exist_ok=True)
    written = []
    for name, content in sorted(files.items()):
        path = out_dir / name
        tmp = path.with_suffix(path.suffix + ".tmp")
        tmp.write_text(content, encoding="utf-8")
        tmp.replace(path)
        written.append(path)
    return written
