// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Abhinand A S
// Replicator page: upload videos, edit the streams, start/stop, add them to RTVision.
import { $, h, api, toast, notice, num, compact, bytes, duration, table } from "./util.js";

const BASE = "/api/replicator";
const EXT = /\.(mp4|mov|m4v|mkv|ts)$/i;
let status = null, videos = [], rows = [], saved = "", overview = null, port = 8554;

const snapshot = () => JSON.stringify(rows);
const dirty = () => snapshot() !== saved;

function markDirty() {
  $("rep-save").disabled = !dirty();
  $("rep-reset").disabled = !dirty();
}

const localUrl = (s) => `rtsp://${location.hostname}:${status.rtsp_port}/${s.name}`;

async function copy(text) {
  try { await navigator.clipboard.writeText(text); toast(`Copied ${text}`); } catch (e) { prompt("Copy this URL:", text); }
}

async function addToRTVision(s) {
  await api("/api/streams", { method: "POST", body: { id: s.name, name: s.name, url: s.url } });
}

function renderStatus() {
  const added = new Set((overview?.streams || []).map((s) => s.id));
  const stateText = { streaming: ["good", "Streaming"], idle: ["", "Ready, no viewer"], stopped: ["", "Stopped"], error: ["bad", "Error"] };
  const free = (overview?.info?.max_streams ?? 0) - (overview?.streams?.length ?? 0);
  table($("rep-status"), [
    { title: "Stream", cell: (s) => h("strong", {}, s.name) },
    { title: "State", cell: (s) => h("div", {}, h("span", { class: `status ${stateText[s.state][0]}` }, stateText[s.state][1]), s.error ? h("div", { class: "bad-text small" }, s.error) : null) },
    { title: "Video", cell: (s) => h("div", {}, s.video, h("div", { class: "cam-id" }, s.video_info ? `${s.video_info.codec} ${s.video_info.width}×${s.video_info.height} ${s.video_info.fps ?? "?"} FPS` : "")) },
    { title: "URL on this machine", cell: (s) => h("span", {}, h("code", {}, localUrl(s)), " ", h("button", { class: "btn ghost sm", type: "button", onclick: () => copy(localUrl(s)) }, "Copy")) },
    { title: "Viewers", num: true, cell: (s) => num(s.viewers) },
    { title: "Frames", num: true, cell: (s) => compact(s.frames) },
    { title: "Loops", num: true, cell: (s) => num(s.loops) },
    { title: "", cell: (s) => added.has(s.name) ? h("span", { class: "status good" }, "In RTVision")
      : h("button", { class: "btn sm", type: "button", disabled: !overview?.online || free <= 0 || s.state === "error" || s.state === "stopped",
          onclick: async (e) => { e.target.disabled = true; try { await addToRTVision(s); toast(`${s.name} added to RTVision`); } catch (err) { toast(err.message, true); } } }, "Add to RTVision") },
  ], status?.streams || [], "No streams configured. Upload a video, then use Fill from all videos.", true);
  const ready = (status?.streams || []).filter((s) => (s.state === "streaming" || s.state === "idle") && !added.has(s.name));
  $("rep-add-all").disabled = !overview?.online || !ready.length || free <= 0;
  const toggle = $("rep-toggle");
  toggle.disabled = !status;
  toggle.textContent = status?.running ? "Stop all streams" : "Start streams";
}

function renderVideos() {
  table($("videos"), [
    { title: "Video", cell: (v) => h("div", {}, v.name, v.error ? h("div", { class: "bad-text small" }, v.error) : h("div", { class: "cam-id" }, `${v.codec} · ${v.width}×${v.height} · ${v.fps ?? "?"} FPS`)) },
    { title: "Length", num: true, cell: (v) => duration(v.duration_s) },
    { title: "Size", num: true, cell: (v) => bytes(v.size_bytes) },
    { title: "", cell: (v) => h("button", { class: "btn ghost danger sm", type: "button", "aria-label": `Delete ${v.name}`, onclick: () => removeVideo(v.name) }, "Delete") },
  ], videos, "No videos uploaded yet");
}

async function removeVideo(name) {
  if (!confirm(`Delete "${name}"?`)) return;
  try { await api(`${BASE}/videos/${encodeURIComponent(name)}`, { method: "DELETE" }); await loadVideos(); toast(`${name} deleted`); } catch (e) { toast(e.message, true); }
}

function renderConfig() {
  const usable = videos.filter((v) => !v.error).map((v) => v.name);
  const bind = (row, key, props, conv = (el) => el.value) => {
    const el = h("input", { ...props });
    el.addEventListener("input", () => { row[key] = conv(el); markDirty(); });
    return el;
  };
  table($("rep-config"), [
    { title: "Name", cell: (r) => bind(r, "name", { type: "text", value: r.name, required: true, maxLength: 63, pattern: "[A-Za-z0-9][A-Za-z0-9_\\-]*", "aria-label": "Stream name", title: "Letters, digits, - and _" }) },
    { title: "Video", cell: (r) => {
      const names = usable.includes(r.video) ? usable : [r.video, ...usable];
      const el = h("select", { "aria-label": "Video", required: true }, names.map((n) => h("option", { value: n, selected: n === r.video }, n || "Choose a video")));
      el.addEventListener("change", () => { r.video = el.value; markDirty(); });
      return el;
    } },
    { title: "Start at (s)", cell: (r) => bind(r, "start_offset_s", { type: "number", min: 0, max: 86400, step: "any", value: r.start_offset_s, "aria-label": "Start offset in seconds" }, (el) => Number(el.value) || 0) },
    { title: "Loop", cell: (r) => bind(r, "loop", { type: "checkbox", checked: r.loop, "aria-label": "Loop" }, (el) => el.checked) },
    { title: "On", cell: (r) => bind(r, "enabled", { type: "checkbox", checked: r.enabled, "aria-label": "Enabled" }, (el) => el.checked) },
    { title: "", cell: (r) => h("button", { class: "btn ghost danger sm", type: "button", "aria-label": `Delete stream ${r.name}`, onclick: () => { rows.splice(rows.indexOf(r), 1); renderConfig(); markDirty(); } }, "✕") },
  ], rows, "No streams");
  $("quick-fill").disabled = !usable.length;
  $("rep-add-row").disabled = !usable.length;
}

function newName() {
  for (let i = 1; ; i++) if (!rows.some((r) => r.name === `cam-${i}`)) return `cam-${i}`;
}

async function loadVideos() {
  videos = (await api(`${BASE}/videos`)).videos;
  renderVideos();
  renderConfig();
}

async function loadConfig() {
  const cfg = await api(`${BASE}/config`);
  rows = cfg.streams;
  port = cfg.rtsp_port;
  saved = snapshot();
  renderConfig();
  markDirty();
}

function upload(file) {
  const line = h("li", {}, file.name);
  $("uploads").append(line);
  const fail = (text) => { line.replaceChildren(h("span", { class: "bad-text" }, `${file.name}: ${text}`)); setTimeout(() => line.remove(), 10000); };
  if (!EXT.test(file.name)) { fail("only mp4, mov, m4v, mkv and ts files are supported"); return Promise.resolve(); }
  const name = file.name.replace(/[^A-Za-z0-9._ ()+-]/g, "_").replace(/^[^A-Za-z0-9]+/, "").replace(/\.{2,}/g, ".") || "video.mp4";
  if (videos.some((v) => v.name === name) && !confirm(`"${name}" already exists. Replace it?`)) { line.remove(); return Promise.resolve(); }
  const bar = h("div", { class: "meter-fill" });
  line.append(h("div", { class: "meter-track" }, bar));
  return new Promise((resolve) => {
    const xhr = new XMLHttpRequest(); // fetch() cannot report upload progress
    xhr.open("PUT", `${BASE}/videos/${encodeURIComponent(name)}`);
    xhr.setRequestHeader("X-RTVision", "1");
    xhr.upload.onprogress = (e) => { bar.style.width = `${(100 * e.loaded) / Math.max(e.total, 1)}%`; };
    xhr.onload = () => {
      if (xhr.status === 201) { line.remove(); toast(`${name} uploaded`); } else {
        let msg = `upload failed (HTTP ${xhr.status})`;
        try { msg = JSON.parse(xhr.responseText).error || msg; } catch (e) { /* keep the default */ }
        fail(msg);
      }
      resolve();
    };
    xhr.onerror = () => { fail("upload interrupted"); resolve(); };
    xhr.send(file);
  });
}

async function uploadAll(files) {
  for (const f of files) await upload(f);
  try { await loadVideos(); } catch (e) { toast(e.message, true); }
}

export function init() {
  const drop = $("drop"), file = $("file");
  file.addEventListener("change", () => { uploadAll([...file.files]); file.value = ""; });
  drop.addEventListener("keydown", (e) => { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); file.click(); } });
  for (const ev of ["dragover", "dragleave", "drop"]) drop.addEventListener(ev, (e) => { e.preventDefault(); drop.classList.toggle("over", ev === "dragover"); });
  drop.addEventListener("drop", (e) => uploadAll([...e.dataTransfer.files]));
  $("rep-add-row").addEventListener("click", () => {
    rows.push({ name: newName(), video: videos.find((v) => !v.error).name, loop: true, start_offset_s: 0, enabled: true });
    renderConfig();
    markDirty();
  });
  $("quick-fill").addEventListener("click", () => {
    const usable = videos.filter((v) => !v.error);
    const n = Math.min(Math.max(parseInt($("quick-count").value, 10) || 1, 1), 64);
    if (rows.length && !confirm(`Replace the ${rows.length} configured streams with ${n} new ones?`)) return;
    rows = Array.from({ length: n }, (_, i) => ({ name: `cam-${i + 1}`, video: usable[i % usable.length].name, loop: true, start_offset_s: 5 * Math.floor(i / usable.length), enabled: true }));
    renderConfig();
    markDirty();
  });
  $("rep-reset").addEventListener("click", () => loadConfig().catch((e) => toast(e.message, true)));
  $("rep-form").addEventListener("submit", async (e) => {
    e.preventDefault();
    try {
      status = await api(`${BASE}/config`, { method: "PUT", body: { rtsp_port: port, ...(await keepHost()), streams: rows } });
      await loadConfig();
      renderStatus();
      toast("Configuration applied");
    } catch (err) { toast(err.message, true); }
  });
  $("rep-toggle").addEventListener("click", async () => {
    try { status = await api(`${BASE}/${status.running ? "stop" : "start"}`, { method: "POST" }); renderStatus(); } catch (e) { toast(e.message, true); }
  });
  $("rep-add-all").addEventListener("click", async () => {
    const added = new Set((overview?.streams || []).map((s) => s.id));
    let n = 0;
    $("rep-add-all").disabled = true;
    for (const s of status.streams) {
      if (added.has(s.name) || (s.state !== "streaming" && s.state !== "idle")) continue;
      try { await addToRTVision(s); n++; } catch (e) { toast(`${s.name}: ${e.message}`, true); break; }
    }
    if (n) toast(`${n} ${n === 1 ? "camera" : "cameras"} added to RTVision`);
  });
}

// the form does not edit public_host: send back what the replicator has
async function keepHost() {
  const cfg = await api(`${BASE}/config`);
  return { public_host: cfg.public_host };
}

let loaded = false;
export async function update(ov) {
  overview = ov;
  if (!ov.replicator) {
    notice($("rep-notice"), "The replicator is not configured. Start the stack with deploy/compose/compose.ui.yaml.");
    return;
  }
  try {
    status = await api(`${BASE}/status`);
    if (!loaded) { await loadConfig(); await loadVideos(); loaded = true; }
    notice($("rep-notice"), "");
    renderStatus();
  } catch (e) {
    loaded = false;
    notice($("rep-notice"), `Replicator: ${e.message}`, true);
  }
}
