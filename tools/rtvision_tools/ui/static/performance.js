// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Abhinand A S
// Performance page: service figures, cameras and model on top, hardware at the bottom.
import { $, h, num, compact, bytes, duration, notice, table } from "./util.js";
import { LineChart, sparkline } from "./charts.js";

let charts = null;

export function init() {
  charts = {
    fps: new LineChart($("chart-fps"), { unit: " FPS", digits: 1, label: "Total throughput" }),
    detections_s: new LineChart($("chart-det"), { unit: " /s", digits: 1, label: "Detections per second" }),
    gpu: new LineChart($("chart-gpu"), { unit: " %", max: 100, label: "GPU load" }),
    cpu: new LineChart($("chart-cpu"), { unit: " %", max: 100, digits: 1, label: "CPU load" }),
    ram: new LineChart($("chart-ram"), { unit: " %", max: 100, digits: 1, label: "Memory in use" }),
  };
}

const tile = (label, value, note = "") =>
  h("div", { class: "tile" }, h("div", { class: "label" }, label), h("div", { class: "value" }, value), h("div", { class: "note" }, note));

function meter(label, value, percent, { warn = 80, bad = 95 } = {}) {
  const level = percent === null || percent === undefined ? "" : percent >= bad ? " bad" : percent >= warn ? " warn" : "";
  const fill = h("div", { class: "meter-fill" });
  fill.style.width = `${Math.min(Math.max(percent ?? 0, 0), 100)}%`;
  const mark = level === " bad" ? "▲ high · " : level === " warn" ? "▲ " : "";
  return h("div", { class: `meter${level}` },
    h("div", { class: "meter-head" }, h("span", {}, label), h("span", {}, mark + value)),
    h("div", { class: "meter-track", role: "meter", "aria-label": label, "aria-valuemin": "0", "aria-valuemax": "100", "aria-valuenow": String(Math.round(percent ?? 0)) }, fill));
}

const facts = (pairs) => pairs.flatMap(([k, v]) => [h("dt", {}, k), h("dd", {}, v === null || v === undefined || v === "" ? "–" : String(v))]);

function gpuCard(g) {
  const mem = g.memory_total_mib ? (100 * g.memory_used_mib) / g.memory_total_mib : null;
  const pct = (v) => (v === null ? "–" : `${num(v)} %`);
  return h("div", { class: "card" }, h("h2", {}, `GPU ${g.index}`), h("p", { class: "muted small" }, `${g.name} · driver ${g.driver}`),
    meter("Load", pct(g.utilization), g.utilization, { warn: 90, bad: 99 }),
    meter("Memory", `${bytes(g.memory_used_mib * 1048576)} of ${bytes(g.memory_total_mib * 1048576)}`, mem),
    meter("Video decoder", pct(g.decoder), g.decoder),
    meter("Video encoder", pct(g.encoder), g.encoder),
    h("dl", { class: "facts" }, facts([
      ["Temperature", g.temperature_c === null ? null : `${num(g.temperature_c)} °C`],
      ["Power", g.power_w === null ? null : `${num(g.power_w)} W${g.power_limit_w ? ` of ${num(g.power_limit_w)} W` : ""}`]])));
}

function hardware(hw) {
  if (!hw.cpu) return;
  const cores = h("div", { class: "cores", role: "img", "aria-label": `Load of ${hw.cpu.per_core.length} cores` },
    hw.cpu.per_core.map((p, i) => { const b = h("b"); b.style.height = `${p}%`; return h("i", { title: `Core ${i}: ${num(p)} %` }, b); }));
  const gpus = hw.gpus.length ? hw.gpus.map(gpuCard)
    : [h("div", { class: "card" }, h("h2", {}, "GPU"), h("p", { class: "muted" }, hw.gpu_error || "No NVIDIA GPU found"))];
  const m = hw.memory, d = hw.disk;
  $("hw-cards").replaceChildren(...gpus,
    h("div", { class: "card" }, h("h2", {}, "CPU"), h("p", { class: "muted small" }, `${hw.cpu.model || "Unknown"} · ${hw.cpu.cores} threads`),
      meter("Load", `${num(hw.cpu.percent, 1)} %`, hw.cpu.percent), cores,
      h("dl", { class: "facts" }, facts([["Load average", hw.cpu.load.map((v) => num(v, 2)).join(" · ")], ["Uptime", duration(hw.uptime_s)]]))),
    h("div", { class: "card" }, h("h2", {}, "Memory and disk"), h("p", { class: "muted small" }, "Host memory; disk of the container storage"),
      meter("RAM", `${bytes(m.used)} of ${bytes(m.total)}`, (100 * m.used) / m.total),
      m.swap_total ? meter("Swap", `${bytes(m.swap_used)} of ${bytes(m.swap_total)}`, (100 * m.swap_used) / m.swap_total) : null,
      meter("Disk", `${bytes(d.used)} of ${bytes(d.total)}`, (100 * d.used) / d.total, { warn: 85, bad: 95 })));
}

export function update(ov, hw, history) {
  const pts = history.points;
  notice($("perf-notice"), ov.online ? "" : `RTVision is not reachable: ${ov.error}`, true);
  const t = ov.totals || {}, r = ov.rates || {}, streams = ov.online ? ov.streams : [];
  $("perf-tiles").replaceChildren(
    tile("Throughput", ov.online ? `${num(r.fps, 1)}` : "–", "FPS, all cameras"),
    tile("Cameras", ov.online ? `${streams.length}` : "–", ov.online ? `of ${ov.info.max_streams}` : ""),
    tile("Detections", num(r.detections_s, 1), "objects per second"),
    tile("Events sent", compact(t.rtvision_events_published_total), `${num(r.events_s, 1)} per second`),
    tile("Events dropped", compact(t.rtvision_events_dropped_total), `${compact(t.rtvision_events_failed_total)} failed`),
    tile("Publish queue", compact(t.rtvision_publish_queue_depth), "events waiting"));
  for (const [key, chart] of Object.entries(charts)) chart.update(pts.map((p) => ({ t: p.t, v: p[key] })));

  const peak = Math.max(30, ...pts.flatMap((p) => Object.values(p.streams)));
  table($("perf-cameras"), [
    { title: "Camera", cell: (s) => h("div", {}, h("div", {}, s.name), h("div", { class: "cam-id" }, s.id)) },
    { title: "FPS", num: true, cell: (s) => num(s.fps, 1) },
    { title: "Last 5 minutes", cell: (s) => sparkline(pts.map((p) => p.streams[s.id] ?? null), peak, `FPS of ${s.name}`) },
    { title: "In view", num: true, cell: (s) => num(Object.values(s.objects).reduce((a, b) => a + b, 0)) },
    { title: "Frames", num: true, cell: (s) => compact(s.frames) },
    { title: "Detections", num: true, cell: (s) => compact(s.detections_total) },
    { title: "Clock", cell: (s) => s.timestamp_source || "–" },
  ], streams, "No cameras");

  const info = ov.info || {}, m = info.model || {};
  $("perf-model").replaceChildren(...facts([
    ["Name", m.name], ["Version", m.version], ["Task", m.task], ["Output family", m.family], ["Precision", m.precision], ["Licence", m.license],
    ["Max cameras", info.max_streams], ["Hot swaps", `${num(t.rtvision_model_swaps_total_ok ?? 0)} ok, ${num(t.rtvision_model_swaps_total_failed ?? 0)} failed`],
    ["Event schema", info.schema], ["RTVision", info.version], ["Web UI", ov.ui_version]]));
  hardware(hw);

  table($("perf-table"), [
    { title: "Time", cell: (p) => new Date(p.t * 1000).toLocaleTimeString() },
    { title: "FPS", num: true, cell: (p) => num(p.fps, 1) },
    { title: "Detections/s", num: true, cell: (p) => num(p.detections_s, 1) },
    { title: "GPU %", num: true, cell: (p) => num(p.gpu) },
    { title: "CPU %", num: true, cell: (p) => num(p.cpu, 1) },
    { title: "Memory %", num: true, cell: (p) => num(p.ram, 1) },
  ], pts.slice(-150).filter((_, i, a) => (a.length - 1 - i) % 5 === 0).reverse(), "No data yet");
}
