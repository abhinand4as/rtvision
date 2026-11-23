// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Abhinand A S
// Live page: one card per camera, cropped from the annotated tiled video.
import { $, h, api, toast, notice, num } from "./util.js";
import { Preview } from "./preview.js";

const cards = new Map(); // camera id -> {root, canvas, ...}
let tiler = { rows: 1, cols: 1 };
let overview = null;
let videoState = "connecting";

function zoom(card, on) {
  for (const c of cards.values()) c.root.classList.remove("big");
  card.root.classList.toggle("big", on);
  document.body.classList.toggle("zoomed", on);
}

async function remove(id) {
  if (!confirm(`Remove camera "${id}" from RTVision?`)) return;
  let body;
  for (;;) {
    try {
      await api(`/api/streams/${encodeURIComponent(id)}`, { method: "DELETE", body });
      toast(`Camera ${id} removed`);
      return;
    } catch (e) {
      if (e.status !== 428) { toast(e.message, true); return; }
      const url = prompt(`Camera "${id}" was not added on this page. Enter its exact URL to remove it:`);
      if (!url) return;
      body = { url };
    }
  }
}

function createCard(s) {
  const canvas = h("canvas", { width: 640, height: 360 });
  const wait = h("span", { class: "cam-wait" }, "Waiting for video");
  const view = h("button", { class: "cam-view", type: "button", "aria-label": `Enlarge ${s.name}` }, canvas, wait);
  const name = h("div", { class: "cam-name" });
  const sub = h("div", { class: "cam-id" });
  const chips = h("div", { class: "chips" });
  const del = h("button", { class: "btn ghost sm remove", type: "button", "aria-label": `Remove ${s.name}`, onclick: () => remove(s.id) }, "Remove");
  const root = h("article", { class: "cam" }, view, h("div", { class: "cam-bar" }, h("div", { class: "cam-text" }, name, sub), chips, del));
  const card = { root, canvas, wait, name, sub, chips, index: s.index, ctx: canvas.getContext("2d") };
  view.addEventListener("click", () => zoom(card, !root.classList.contains("big")));
  return card;
}

function drawTiles(bitmap) {
  const tw = bitmap.width / tiler.cols, th = bitmap.height / tiler.rows;
  for (const c of cards.values()) {
    const col = c.index % tiler.cols, row = Math.floor(c.index / tiler.cols);
    if (c.index < 0 || row >= tiler.rows) continue;
    if (c.canvas.width !== Math.round(tw) || c.canvas.height !== Math.round(th)) {
      c.canvas.width = Math.round(tw);
      c.canvas.height = Math.round(th);
    }
    c.ctx.drawImage(bitmap, col * tw, row * th, tw, th, 0, 0, c.canvas.width, c.canvas.height);
    c.wait.hidden = true;
  }
}

const preview = new Preview(drawTiles, (err) => {
  if (err === videoState) return;
  videoState = err;
  if (err) for (const c of cards.values()) c.wait.hidden = false;
  showNotice();
});

function showNotice() {
  if (!overview) return;
  if (!overview.online) notice($("live-notice"), `RTVision is not reachable: ${overview.error}`, true);
  else if (!overview.ready) notice($("live-notice"), `RTVision is not ready yet: ${overview.ready_reason || "starting"}`);
  else if (!overview.preview) notice($("live-notice"), "Live video is off: start the UI with --preview-url and RTVision with outputs.rtsp.");
  else if (overview.streams.length && videoState) notice($("live-notice"), `Live video: ${videoState}. Retrying.`);
  else notice($("live-notice"), "");
}

export function update(ov) {
  overview = ov;
  const streams = ov.online ? ov.streams : [];
  if (ov.tiler) tiler = ov.tiler;
  const ids = new Set(streams.map((s) => s.id));
  for (const [id, c] of cards) {
    if (ids.has(id)) continue;
    if (c.root.classList.contains("big")) zoom(c, false);
    c.root.remove();
    cards.delete(id);
  }
  let objects = 0, fps = 0;
  for (const s of streams) {
    let c = cards.get(s.id);
    if (!c) cards.set(s.id, (c = createCard(s)));
    c.index = s.index;
    c.name.textContent = s.name;
    c.sub.textContent = `${s.id} · ${num(s.fps, 1)} FPS`;
    const entries = Object.entries(s.objects);
    c.chips.replaceChildren(...(entries.length
      ? entries.slice(0, 5).map(([label, n]) => h("span", { class: "chip" }, h("b", {}, String(n)), ` ${label}`))
      : [h("span", { class: "chip muted" }, "nothing detected")]));
    objects += entries.reduce((a, [, n]) => a + n, 0);
    fps += s.fps;
  }
  const grid = $("live-grid");
  const order = streams.map((s) => cards.get(s.id).root);
  if (order.length !== grid.children.length || order.some((el, i) => grid.children[i] !== el)) grid.replaceChildren(...order);
  grid.style.setProperty("--cols", streams.length <= 1 ? 1 : streams.length <= 4 ? 2 : streams.length <= 9 ? 3 : 4);
  $("live-empty").hidden = !ov.online || streams.length > 0;
  const max = ov.info?.max_streams;
  $("live-summary").textContent = ov.online
    ? `${streams.length} of ${max} cameras · ${num(fps)} FPS in total · ${num(objects)} objects in view` : "";
  $("add-camera").disabled = !ov.online || streams.length >= max;
  showNotice();
  if (active && streams.length && ov.preview) preview.start(); else preview.stop();
}

let active = false;
export function show() { active = true; if (overview) update(overview); }
export function hide() { active = false; preview.stop(); for (const c of cards.values()) zoom(c, false); }

export function init() {
  const dialog = $("camera-dialog"), form = $("camera-form"), error = $("camera-error");
  $("add-camera").addEventListener("click", () => { form.reset(); notice(error, ""); dialog.showModal(); });
  $("camera-cancel").addEventListener("click", () => dialog.close());
  form.addEventListener("submit", async (e) => {
    e.preventDefault();
    const d = new FormData(form);
    try {
      await api("/api/streams", { method: "POST", body: { id: d.get("id"), name: d.get("name"), url: d.get("url") } });
      dialog.close();
      toast(`Camera ${d.get("id")} added`);
    } catch (err) { notice(error, err.message, true); }
  });
  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape") for (const c of cards.values()) if (c.root.classList.contains("big")) zoom(c, false);
  });
}
