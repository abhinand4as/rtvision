# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 <PROJECT_OWNER>
"""gen_configs: golden files for the example models and the mapping rules.

Regenerate golden files after an intended change with:
    PERCEPTRA_UPDATE_GOLDEN=1 pytest -q tests/python/test_gen_configs.py
"""

from __future__ import annotations

import configparser
import os
from pathlib import Path

import pytest
import yaml
from conftest import EXAMPLE_CONFIG, EXAMPLES, FIXTURE_MODELS, GOLDEN, REPO, write_model
from perceptra_tools import gen_configs
from perceptra_tools.schemas import ConfigError, load_model_config, load_service_config

OUT_DIR = Path("/tmp/perceptra/generated")
ENGINE_DIR = Path("/opt/perceptra/engines")
TRT = "10.16.1"


def ctx_for(
    model_dir: Path, env: dict | None = None, service_path: Path = EXAMPLE_CONFIG, **kw
) -> gen_configs.GenContext:
    service, _ = load_service_config(service_path, env=env or {})
    model = load_model_config(model_dir / "model.yaml")
    return gen_configs.GenContext(
        service=service,
        model=model,
        model_dir=model_dir,
        engine_dir=ENGINE_DIR,
        out_dir=OUT_DIR,
        trt_version=TRT,
        **kw,
    )


def ini(text: str) -> configparser.ConfigParser:
    cp = configparser.ConfigParser(interpolation=None, strict=False)
    cp.optionxform = str  # keep key case
    cp.read_string(text)
    return cp


@pytest.mark.parametrize("model", sorted(p.name for p in EXAMPLES.iterdir() if p.is_dir()))
def test_golden(model: str) -> None:
    files = gen_configs.generate(ctx_for(EXAMPLES / model))
    gdir = GOLDEN / "generated" / model
    # Golden files must not depend on where the repo is checked out.
    files = {k: v.replace(str(REPO), "<REPO>") for k, v in files.items()}
    if os.environ.get("PERCEPTRA_UPDATE_GOLDEN") == "1":
        gdir.mkdir(parents=True, exist_ok=True)
        for old in gdir.iterdir():
            old.unlink()
        for name, content in files.items():
            (gdir / name).write_text(content)
    expected = {p.name: p.read_text() for p in sorted(gdir.iterdir())}
    assert sorted(files) == sorted(expected)
    for name in files:
        assert files[name] == expected[name], f"{model}/{name} differs from golden"


@pytest.mark.parametrize("n", [1, 3, 16])
def test_max_streams_propagates_to_every_batch_key(n: int) -> None:
    ctx = ctx_for(EXAMPLES / "rtdetr-r18-coco", env={"PERCEPTRA__SERVICE__MAX_STREAMS": str(n)})
    files = gen_configs.generate(ctx)
    ds, pg = ini(files["ds-main.ini"]), ini(files["pgie.ini"])
    assert ds["source-list"]["max-batch-size"] == str(n)
    assert ds["streammux"]["batch-size"] == str(n)
    assert ds["primary-gie"]["batch-size"] == str(n)
    assert pg["property"]["batch-size"] == str(n)
    assert pg["property"]["model-engine-file"].endswith(f"_b{n}_gpu0_fp16.engine")


def test_builtin_overrides_batch_and_gpu_only() -> None:
    ctx = ctx_for(
        EXAMPLES / "trafficcamnet-sample",
        env={"PERCEPTRA__SERVICE__MAX_STREAMS": "6", "PERCEPTRA__SERVICE__GPU_ID": "1"},
    )
    files = gen_configs.generate(ctx)
    assert "pgie.ini" not in files and "parser.ini" not in files
    pg = ini(files["ds-main.ini"])["primary-gie"]
    assert pg["batch-size"] == "6" and pg["gpu-id"] == "1"
    assert pg["config-file"].endswith("samples/configs/deepstream-app/config_infer_primary.txt")


def test_keep_to_filter_out_class_ids_and_allow_list(tmp_path: Path) -> None:
    labels = ["person", "car", "dog", "cat"]
    src = (FIXTURE_MODELS / "yolo26s-people" / "model.yaml").read_text()
    src = src.replace("keep: [person]", "keep: [dog, person]").replace(
        "num_classes: 80", "num_classes: 4"
    )
    p = write_model(tmp_path, src, labels)
    files = gen_configs.generate(ctx_for(p.parent))
    assert ini(files["pgie.ini"])["property"]["filter-out-class-ids"] == "1;3"
    assert ini(files["parser.ini"])["parser"]["allowed_class_ids"] == "0,2"


def test_keep_empty_means_all(tmp_path: Path) -> None:
    files = gen_configs.generate(ctx_for(EXAMPLES / "rtdetr-r18-coco"))
    assert "filter-out-class-ids" not in ini(files["pgie.ini"])["property"]
    assert ini(files["parser.ini"])["parser"]["allowed_class_ids"] == ""


@pytest.mark.parametrize(
    ("resize", "maintain", "symmetric"),
    [("letterbox_center", "1", "1"), ("letterbox_topleft", "1", "0"), ("stretch", "0", "0")],
)
def test_resize_mapping(tmp_path: Path, resize: str, maintain: str, symmetric: str) -> None:
    src = (EXAMPLES / "rtdetr-r18-coco" / "model.yaml").read_text()
    p = write_model(
        tmp_path,
        src.replace("resize: stretch", f"resize: {resize}"),
        (EXAMPLES / "rtdetr-r18-coco" / "labels.txt").read_text().splitlines(),
    )
    prop = ini(gen_configs.generate(ctx_for(p.parent))["pgie.ini"])["property"]
    assert prop["maintain-aspect-ratio"] == maintain
    assert prop["symmetric-padding"] == symmetric


def test_precision_override_and_cluster_mode(tmp_path: Path) -> None:
    src = (FIXTURE_MODELS / "yolo26s-people" / "model.yaml").read_text()
    src = src.replace("family: yolo_e2e", "family: yolo_raw").replace(
        "box_format: xyxy_px", "box_format: cxcywh_px"
    )
    src = src.replace(
        "thresholds: { default: 0.35, per_class: {} }",
        "thresholds: { default: 0.35, per_class: { person: 0.5 } }",
    )
    p = write_model(
        tmp_path, src, (FIXTURE_MODELS / "yolo26s-people" / "labels.txt").read_text().splitlines()
    )
    files = gen_configs.generate(ctx_for(p.parent, env={"PERCEPTRA__INFERENCE__PRECISION": "fp32"}))
    pg = ini(files["pgie.ini"])
    assert pg["property"]["network-mode"] == "0"
    assert pg["property"]["model-engine-file"].endswith("_fp32.engine")
    assert pg["property"]["cluster-mode"] == "2"  # NMS for yolo_raw
    assert pg["class-attrs-all"]["nms-iou-threshold"] == "0.45"
    assert pg["class-attrs-0"]["pre-cluster-threshold"] == "0.5"


def test_tracker_profiles_and_reid() -> None:
    for profile, fname in gen_configs.TRACKER_PROFILES.items():
        files = gen_configs.generate(
            ctx_for(EXAMPLES / "rtdetr-r18-coco", env={"PERCEPTRA__TRACKER__PROFILE": profile})
        )
        assert ini(files["ds-main.ini"])["tracker"]["ll-config-file"].endswith(fname)
    files = gen_configs.generate(
        ctx_for(EXAMPLES / "rtdetr-r18-coco", env={"PERCEPTRA__TRACKER__PROFILE": "none"})
    )
    assert ini(files["ds-main.ini"])["tracker"]["enable"] == "0"
    with pytest.raises(ConfigError, match="tracker.reid"):
        gen_configs.generate(
            ctx_for(EXAMPLES / "rtdetr-r18-coco", env={"PERCEPTRA__TRACKER__REID": "true"})
        )


def test_tracker_file_missing_in_image() -> None:
    ctx = ctx_for(EXAMPLES / "rtdetr-r18-coco", check_files=True)
    ctx.ds_root = "/nonexistent/deepstream"
    with pytest.raises(ConfigError, match="missing from this DeepStream image"):
        gen_configs.generate(ctx)


def test_force_tcp_and_outputs() -> None:
    files = gen_configs.generate(
        ctx_for(
            EXAMPLES / "rtdetr-r18-coco",
            env={
                "PERCEPTRA__SERVICE__SOURCES__FORCE_TCP": "false",
                "PERCEPTRA__OUTPUTS__FILE": "/opt/perceptra/output/a.mp4",
            },
        )
    )
    ds = ini(files["ds-main.ini"])
    assert "select-rtp-protocol" not in ds["source-attr-all"]
    assert ds["sink2"]["output-file"] == "/opt/perceptra/output/a.mp4"
    assert ds["tiled-display"]["enable"] == "1" and ds["osd"]["enable"] == "1"


def test_labels_count_and_unknown_keep_label(tmp_path: Path) -> None:
    src = (FIXTURE_MODELS / "yolo26s-people" / "model.yaml").read_text()
    p = write_model(tmp_path, src, ["person", "car"])
    with pytest.raises(ConfigError, match="num_classes: 80 but the labels file has 2 lines"):
        gen_configs.generate(ctx_for(p.parent))
    p = write_model(
        tmp_path,
        src.replace("keep: [person]", "keep: [persn]"),
        (FIXTURE_MODELS / "yolo26s-people" / "labels.txt").read_text().splitlines(),
    )
    with pytest.raises(ConfigError, match=r"classes\.keep\[0\]: label 'persn'"):
        gen_configs.generate(ctx_for(p.parent))


def test_unsupported_task(tmp_path: Path) -> None:
    src = (
        (EXAMPLES / "rtdetr-r18-coco" / "model.yaml")
        .read_text()
        .replace("task: detect", "task: segment")
    )
    p = write_model(
        tmp_path, src, (EXAMPLES / "rtdetr-r18-coco" / "labels.txt").read_text().splitlines()
    )
    with pytest.raises(ConfigError, match="not supported in this version"):
        gen_configs.generate(ctx_for(p.parent))


@pytest.mark.parametrize(
    ("env", "expected"),
    [({"PERCEPTRA__PUBLISH__KAFKA__TOPIC": "custom"}, "custom"), ({}, "perceptra.rtdetr-r18-coco")],
)
def test_kafka_topic_resolution(env: dict, expected: str) -> None:
    files = gen_configs.generate(ctx_for(EXAMPLES / "rtdetr-r18-coco", env=env))
    assert ini(files["app.ini"])["publish"]["kafka_topic"] == expected


def test_stdout_backend_has_no_kafka_files() -> None:
    files = gen_configs.generate(
        ctx_for(EXAMPLES / "rtdetr-r18-coco", env={"PERCEPTRA__PUBLISH__BACKEND": "stdout"})
    )
    assert "kafka.properties" not in files
    pub = ini(files["app.ini"])["publish"]
    assert pub["backend"] == "stdout" and pub["kafka_topic"] == "" and pub["kafka_brokers"] == ""


def test_prepare_engine_cache(tmp_path: Path) -> None:
    mdir = tmp_path / "models" / "m"
    mdir.mkdir(parents=True)
    src = (EXAMPLES / "rtdetr-r18-coco" / "model.yaml").read_text()
    (mdir / "model.yaml").write_text(src)
    (mdir / "labels.txt").write_text((EXAMPLES / "rtdetr-r18-coco" / "labels.txt").read_text())
    (mdir / "rtdetr_r18.onnx").write_bytes(b"x")
    (mdir / "rtdetr_r18.onnx.data").write_bytes(b"y")  # external data must be copied too
    ctx = ctx_for(mdir)
    ctx.engine_dir = tmp_path / "engines"
    copied = gen_configs.prepare_engine_cache(ctx)
    cache = tmp_path / "engines" / f"rtdetr-r18-coco-trt{TRT}"
    assert copied == ["labels.txt", "rtdetr_r18.onnx", "rtdetr_r18.onnx.data"]
    # real copies (nvinfer resolves symlinks and would write next to the read-only original)
    assert not (cache / "rtdetr_r18.onnx").is_symlink()
    assert (cache / "rtdetr_r18.onnx.data").read_bytes() == b"y"
    assert not (cache / "model.yaml").exists()
    onnx_file, engine = gen_configs.engine_paths(ctx)
    assert onnx_file == cache / "rtdetr_r18.onnx"
    assert engine == cache / "rtdetr_r18.onnx_b4_gpu0_fp16.engine"
    # unchanged model: nothing copied, cached engine kept
    engine.write_bytes(b"engine")
    assert gen_configs.prepare_engine_cache(ctx) == []
    assert engine.exists()
    # changed model: re-copied and the stale engine removed
    (mdir / "rtdetr_r18.onnx").write_bytes(b"new model")
    assert gen_configs.prepare_engine_cache(ctx) == ["rtdetr_r18.onnx"]
    assert not engine.exists()
    assert (cache / "rtdetr_r18.onnx").read_bytes() == b"new model"


def test_builtin_config_moves_engine_into_cache(tmp_path: Path) -> None:
    """family: builtin with an onnx-file: nvinfer gets a copy of its config whose onnx/engine live
    in the engine cache (the DeepStream samples dir is not writable for uid 1000)."""
    sample = tmp_path / "ds" / "samples"
    (sample / "models" / "Primary_Detector").mkdir(parents=True)
    (sample / "configs" / "app").mkdir(parents=True)
    pd = sample / "models" / "Primary_Detector"
    (pd / "net.onnx").write_bytes(b"onnx")
    (pd / "labels.txt").write_text("car\nbicycle\nperson\nroad_sign\n")
    (pd / "net.onnx_b30_gpu0_fp16.engine").write_bytes(b"stale")  # never copied
    cfg = sample / "configs" / "app" / "infer.txt"
    cfg.write_text(
        "[property]\n# comment=kept\ngpu-id=0\nonnx-file=../../models/Primary_Detector/net.onnx\n"
        "model-engine-file=../../models/Primary_Detector/net.onnx_b30_gpu0_fp16.engine\n"
        "labelfile-path=../../models/Primary_Detector/labels.txt\nbatch-size=30\n"
        "network-mode=2\nnum-detected-classes=4\n[class-attrs-all]\ntopk=20\n"
    )
    mdir = tmp_path / "models" / "b"
    mdir.mkdir(parents=True)
    src = (EXAMPLES / "trafficcamnet-sample" / "model.yaml").read_text()
    src = src.replace(
        "/opt/nvidia/deepstream/deepstream/samples/configs/deepstream-app/config_infer_primary.txt",
        str(cfg),
    ).replace(
        "/opt/nvidia/deepstream/deepstream/samples/models/Primary_Detector/labels.txt",
        str(pd / "labels.txt"),
    )
    (mdir / "model.yaml").write_text(src)
    ctx = ctx_for(mdir)
    ctx.engine_dir = tmp_path / "engines"
    cache = ctx.engine_dir / f"trafficcamnet-sample-trt{TRT}"

    assert gen_configs.prepare_engine_cache(ctx) == ["labels.txt", "net.onnx"]
    files = gen_configs.generate(ctx)
    pgie = ini(files["pgie.ini"])["property"]
    assert pgie["onnx-file"] == str(cache / "net.onnx")
    assert pgie["model-engine-file"] == str(cache / "net.onnx_b4_gpu0_fp16.engine")
    assert pgie["labelfile-path"] == str(pd / "labels.txt")
    assert pgie["batch-size"] == "4" and pgie["network-mode"] == "2"
    assert "# comment=kept" in files["pgie.ini"]
    assert ini(files["ds-main.ini"])["primary-gie"]["config-file"] == str(ctx.out_dir / "pgie.ini")
    assert "parser.ini" not in files


def _fake_ds_root(tmp_path: Path, with_model: bool) -> Path:
    """A minimal DeepStream tree with an accuracy tracker profile referencing a ReID model."""
    root = tmp_path / "ds"
    cfg = root / "samples" / "configs" / "deepstream-app"
    cfg.mkdir(parents=True)
    model = root / "samples" / "models" / "Tracker" / "resnet50_market1501.etlt"
    for name in gen_configs.TRACKER_PROFILES.values():
        (cfg / name).write_text("BaseConfig: {minDetectorConfidence: 0}\n")
    (cfg / "config_tracker_NvDCF_accuracy.yml").write_text(
        "# NVIDIA licence header\n%YAML:1.0\n"
        "BaseConfig:\n  minDetectorConfidence: 0.1\n"
        "ReID:\n  reidType: 2\n  reidFeatureSize: 256\n"
        f'  tltEncodedModel: "{model}"\n'
        f'  modelEngineFile: "{model}_b100_gpu0_fp16.engine"\n'
    )
    if with_model:
        model.parent.mkdir(parents=True)
        model.write_bytes(b"x")
    return root


def test_timestamps_source_sets_attach_sys_ts() -> None:
    for source, expected in (("ntp", "0"), ("uri_utc", "1"), ("system", "1")):
        files = gen_configs.generate(
            ctx_for(EXAMPLES / "rtdetr-r18-coco", env={"PERCEPTRA__TIMESTAMPS__SOURCE": source})
        )
        assert ini(files["ds-main.ini"])["streammux"]["attach-sys-ts-as-ntp"] == expected
        assert ini(files["app.ini"])["timestamps"]["source"] == source


def test_reid_tracker_config_generated(tmp_path: Path) -> None:
    env = {
        "PERCEPTRA__TRACKER__PROFILE": "nvdcf_accuracy",
        "PERCEPTRA__TRACKER__REID": "true",
        "PERCEPTRA__TRACKER__REID_STORE_AGE": "30",
        "PERCEPTRA__PUBLISH__INCLUDE_EMBEDDINGS": "true",
    }
    ctx = ctx_for(EXAMPLES / "rtdetr-r18-coco", env=env, check_files=False)
    ctx.ds_root = str(_fake_ds_root(tmp_path, with_model=True))
    files = gen_configs.generate(ctx)
    assert "\n%YAML:1.0\n" in files["tracker.yml"]  # directive kept for the tracker library
    trk = yaml.safe_load(files["tracker.yml"].replace("%YAML:1.0", ""))
    assert trk["ReID"]["outputReidTensor"] == 1
    assert trk["ReID"]["reidType"] == 2 and trk["BaseConfig"]["minDetectorConfidence"] == 0.1
    assert trk["ReID"]["modelEngineFile"] == str(
        ENGINE_DIR / f"tracker-reid-trt{TRT}" / "resnet50_market1501.etlt_b100_gpu0_fp16.engine"
    )
    ds = ini(files["ds-main.ini"])["tracker"]
    assert ds["ll-config-file"] == str(OUT_DIR / "tracker.yml")
    assert ds["user-meta-pool-size"] == str(gen_configs.REID_USER_META_POOL_SIZE)
    app = ini(files["app.ini"])
    assert app["tracker"]["reid"] == "true" and app["tracker"]["reid_store_age"] == "30"
    assert app["publish"]["include_embeddings"] == "true"


def test_accuracy_profile_without_reid_model(tmp_path: Path) -> None:
    env = {"PERCEPTRA__TRACKER__PROFILE": "nvdcf_accuracy"}
    ctx = ctx_for(EXAMPLES / "rtdetr-r18-coco", env=env, check_files=True)
    ctx.ds_root = str(_fake_ds_root(tmp_path, with_model=False))
    with pytest.raises(ConfigError, match="needs the ReID model .*not shipped"):
        gen_configs.generate(ctx)
    # with the model present, the plain accuracy profile is referenced unchanged
    ctx.ds_root = str(_fake_ds_root(tmp_path / "ok", with_model=True))
    files = gen_configs.generate(ctx)
    assert "tracker.yml" not in files
    assert ini(files["ds-main.ini"])["tracker"]["ll-config-file"].endswith(
        "config_tracker_NvDCF_accuracy.yml"
    )


def test_embeddings_require_reid() -> None:
    with pytest.raises(ConfigError, match="include_embeddings: true requires tracker.reid"):
        gen_configs.generate(
            ctx_for(
                EXAMPLES / "rtdetr-r18-coco", env={"PERCEPTRA__PUBLISH__INCLUDE_EMBEDDINGS": "true"}
            )
        )
