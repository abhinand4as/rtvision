<!-- SPDX-License-Identifier: Apache-2.0 -->
# RTSP stream replicator

Plays video files as RTSP cameras, for tests and demos: real-time pacing, endless looping, as many
streams as you configure. H.264 and H.265 are passed through **without re-encoding**, so it needs
no GPU and little CPU.

## In a terminal

```bash
tools/replicator/replicator.sh street.mp4 --streams 4            # rtsp://localhost:8554/cam-1 … cam-4
tools/replicator/replicator.sh street.mp4 lobby.mp4 --streams 4  # videos are used round robin
tools/replicator/replicator.sh --config my-streams.yaml          # config/replicator.example.yaml
tools/replicator/replicator.sh --probe street.mp4                # codec, size, fps, duration
tools/replicator/replicator.sh --help
```

Ctrl-C stops it. The script runs the tool natively if the host has the GStreamer RTSP server
bindings (`python3-gi`, `gir1.2-gst-rtsp-server-1.0`), otherwise in the `rtvision:local` image.
All files must be in one directory.

| Option | Meaning |
|---|---|
| `--streams N` | number of streams (default: one per video) |
| `--stagger S` | start offset between streams that share a video (default 5 s) |
| `--prefix NAME` | stream names `NAME-1`, `NAME-2`, … (default `cam`) |
| `--port P` | RTSP port (default 8554) |
| `--no-loop` | play each video once |
| `REPLICATOR_BIND=0.0.0.0` | let other machines connect (default: this machine only) |
| `REPLICATOR_NETWORK=rtvision_default` | join RTVision's Docker network; add cameras as `rtsp://rtvision-replicator:8554/cam-1` |

## In the web UI

`docker compose -f deploy/compose/compose.yaml -f deploy/compose/compose.ui.yaml up -d`, then
<http://localhost:8080/replicator>: upload videos, choose *Fill from all videos* or edit the
streams, *Save and apply*, *Add all to RTVision*. Videos and configuration are kept in the Docker
volume `rtvision_videos`. Players on this machine can open `rtsp://localhost:8554/<name>`.

## Behaviour

- Every viewer gets its own playback that starts at once, at `start_offset_s`. Two viewers of the
  same stream are therefore not at the same position.
- Start offsets snap to the previous keyframe. Files with rare keyframes (e.g. every 8 s) show the
  same position for close offsets.
- Each loop continues the timeline, so clients see no jump in timestamps.
- Audio is ignored. Other codecs (MJPEG, VP9, AV1) are rejected with a message; convert them first.
- Changing the configuration restarts only the streams that changed.
- The control API (`--api-port`, used by the UI) has no authentication: keep it on a private
  network. Uploads are limited to 4 GiB and must be playable videos.
