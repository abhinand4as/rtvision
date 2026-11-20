<!-- SPDX-License-Identifier: Apache-2.0 -->
# Troubleshooting

Start with the logs and the ops endpoints:

```bash
docker compose -f deploy/compose/compose.yaml logs --tail 50 perceptra
curl -s localhost:9464/startup; curl -s localhost:9464/ready; curl -s localhost:9464/metrics | grep '^perceptra_'
```

Perceptra's own log lines are `key=value` (`level=error comp=… event=…`). GStreamer and DeepStream
print their own lines in between.

## Exit codes

| Code | From | Meaning |
|---|---|---|
| 0 | binary | clean shutdown (SIGTERM/SIGINT) |
| 1 | binary | runtime error (pipeline failed to start or failed while running) |
| 2 | `perceptra-run`, binary, `entrypoint.sh` | configuration error: the message names the file and field |
| 3 | `perceptra-run` | model licence not on the permissive allow-list and not accepted |
| 137 | Docker | killed by SIGKILL (stop timeout too short, or out of memory) |

With `restart: unless-stopped` (compose) a failing container restarts in a loop; `docker compose
ps` shows `Restarting (<code>)`.

## Startup

**`could not select device driver "nvidia"` / `unsatisfied condition: cuda>=13.2`.** The NVIDIA
Container Toolkit is missing, or the driver is older than 595. Check with
`docker run --rm --gpus all ubuntu nvidia-smi` (the CUDA version shown must be ≥ 13.2).

**Pulling `nvcr.io/nvidia/deepstream` fails with `unauthorized`.** Log in with an NGC API key:
`docker login nvcr.io` (user `$oauthtoken`).

**Exit 3, `MODEL LICENCE: …` box.** The model's weights are not under a permissive licence. Read
the licence, then set `PERCEPTRA_ACCEPT_MODEL_LICENSE=<the id shown>` (in `deploy/compose/.env`).
The default model (RF-DETR) does not need this; `trafficcamnet-sample` does.

**Exit 2, `files.onnx: '…' not found`.** The ONNX has not been exported or is not in the mounted
models directory. For the default models run `tools/export/rfdetr/export.sh`; otherwise see
`docs/MODELS.md`. The compose file mounts `models/examples/` (or `PERCEPTRA_HOST_MODELS_DIR`).

**Exit 2, `event=engine_dir_not_writable`.** The container runs as uid 1000 and the engine cache is
a host directory owned by someone else. Use the named volume from the compose file, or
`sudo chown -R 1000:1000 <dir>`.

**Exit 2, `tracker.profile: … missing from this DeepStream image`.** The tracker profile needs a
file that this base image does not ship. Choose another `tracker.profile`.

**`/startup` stays 503 for a minute or more.** TensorRT is building the engine (first start per
model, batch size, precision and TensorRT version). Later starts load it from the
`perceptra_engines` volume in seconds. `/live` already answers 200 during the build.

**Engine build fails / `ONNX output check` errors.** The export does not match `model.yaml`; run
the dry-run check in `docs/MODELS.md` step 6.

## Streams

**`stream/add` returns an error.** Common causes: more streams than `service.max_streams`; a
malformed payload (compare with `docs/API.md`); an unreachable URL. For files, the path is **inside the
container** (mount it). Check the perceptra logs for the reason.

**RTSP camera never produces frames.** Test the URL from the host (`ffprobe rtsp://…` or VLC). By
default Perceptra forces RTSP over TCP (`sources.force_tcp: true`); some cameras only support UDP.
Credentials go in the URL (`rtsp://user:pass@host/…`).

**`WARNING … No decoder available for type 'audio/mpeg'`.** Harmless: the source has an audio
track and Perceptra only decodes video.

**`GStreamer-WARNING: Failed to load plugin …` at startup.** Harmless: the base image ships
optional GStreamer plugins whose dependencies are not installed. They are not used.

**A file stream stops after a while.** File sources end at end-of-file; they do not loop. The
stream stays listed until you remove it.

## Events

**No events on Kafka.** Check in this order:
1. `curl -s localhost:9464/metrics | grep -E 'frames_processed|events_'`: no frames means the
   stream is not decoding (see Streams); frames but `events_enqueued_total 0` means no frame had
   objects above the thresholds.
2. Frames without objects are not published by default (`publish.include_empty_frames: false`);
   one event is published every `publish.interval_frames` frames (default 30).
3. `/ready` is 503 while Kafka is unreachable; check `publish.kafka.brokers` (inside compose:
   `kafka:9092`; from the host: `localhost:9094`).
4. The topic is `publish.kafka.topic`, else `model.messaging.topic`, else
   `perceptra.<model name>` (e.g. `perceptra.rfdetr-s-coco`, `people-raw`).

**`perceptra_events_dropped_total` grows.** The publish queue is full because Kafka is slower than
the event rate or unreachable. Raise `publish.interval_frames`, check the broker, or tune
`publish.queue`.

**Wrong or missing classes.** `classes.keep` filters labels; `labels.txt` must have exactly one line
per class id the model outputs. See `docs/MODELS.md`.

**Track ids are `null`.** `tracker.profile: none` (no tracker), or the tracker did not assign
that object an id in that frame.

**Timestamps say `"timestamp_source": "system"` although `timestamps.source: ntp`.** File sources
and RTSP streams before their first RTCP sender report have no NTP time; Perceptra falls back to
the system clock for those events.

## Shutdown

**`docker compose stop` takes about 9 seconds.** Expected: DeepStream 9.1's stream muxer can hang
while tearing down, so a watchdog ends the process after `--shutdown-timeout` (8 s) with the
correct exit code (docs/DEVIATIONS.md). The compose file allows 20 s before Docker's SIGKILL.

## Performance

`perceptra_stream_fps` per stream should match the camera frame rate. If it is lower, the GPU is
saturated: use a smaller model, `inference.interval` > 0 (infer every N-th frame, the tracker fills
the gaps), `precision: fp16` or `int8`, or fewer streams per GPU.
