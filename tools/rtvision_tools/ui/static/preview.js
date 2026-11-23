// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Abhinand A S
// Reads the multipart JPEG stream once (browsers allow few connections per host) and hands each
// decoded frame to a callback, which crops the camera tiles out of it. Reconnects on errors.
const RETRY_MS = 1000;
const text = new TextDecoder();

function find(buf, pattern, from) {
  outer: for (let i = from; i <= buf.length - pattern.length; i++) {
    for (let j = 0; j < pattern.length; j++) if (buf[i + j] !== pattern[j]) continue outer;
    return i;
  }
  return -1;
}

export class Preview {
  constructor(onFrame, onState) {
    this.onFrame = onFrame;
    this.onState = onState;
    this.abort = null;
    this.busy = false;
  }

  start() {
    if (this.abort) return;
    this.abort = new AbortController();
    this.run(this.abort.signal);
  }

  stop() {
    this.abort?.abort();
    this.abort = null;
  }

  async run(signal) {
    while (!signal.aborted) {
      try {
        const resp = await fetch("/preview.mjpg", { signal, cache: "no-store" });
        if (!resp.ok) throw new Error((await resp.json().catch(() => ({}))).error || "no video");
        await this.read(resp.body.getReader(), signal);
        throw new Error("the video stream ended");
      } catch (e) {
        if (signal.aborted) return;
        this.onState(e.message);
        await new Promise((r) => setTimeout(r, RETRY_MS));
      }
    }
  }

  async read(reader, signal) {
    const CRLF2 = [13, 10, 13, 10];
    let buf = new Uint8Array(0);
    while (!signal.aborted) {
      const { done, value } = await reader.read();
      if (done) return;
      const next = new Uint8Array(buf.length + value.length);
      next.set(buf);
      next.set(value, buf.length);
      buf = next;
      for (;;) {
        const end = find(buf, CRLF2, 0);
        if (end < 0) break;
        const m = /content-length:\s*(\d+)/i.exec(text.decode(buf.subarray(0, end)));
        if (!m) throw new Error("unexpected video stream");
        const start = end + 4, len = Number(m[1]);
        if (buf.length < start + len + 2) break;
        const jpeg = buf.slice(start, start + len);
        buf = buf.slice(start + len + 2);
        if (!this.busy) this.draw(jpeg); // decoding slower than the stream: skip frames
      }
    }
  }

  async draw(jpeg) {
    this.busy = true;
    try {
      const bitmap = await createImageBitmap(new Blob([jpeg], { type: "image/jpeg" }));
      this.onState(null);
      this.onFrame(bitmap);
      bitmap.close();
    } catch (e) { /* damaged frame: wait for the next one */ } finally {
      this.busy = false;
    }
  }
}
