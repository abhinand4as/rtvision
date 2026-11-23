# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
"""RTSP stream replicator: serves video files as looping, real-time RTSP streams.

``config`` and ``library`` are plain Python (unit-tested without GStreamer); ``engine`` needs
PyGObject with GstRtspServer (in the RTVision image); ``api`` is the HTTP control API used by the
web UI; ``cli`` is the ``rtvision-replicator`` command.
"""
