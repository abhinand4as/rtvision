// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Abhinand A S
// Single-series line chart (crosshair + tooltip, keyboard) and sparkline. No dependencies.
import { h, svg, num } from "./util.js";

function niceMax(v) {
  if (v <= 0) return 1;
  const p = 10 ** Math.floor(Math.log10(v));
  for (const m of [1, 2, 4, 5, 10]) if (v <= m * p) return m * p;
  return 10 * p;
}

// points: [{t, v|null}] -> path segments (gaps where v is null)
function segments(points, x, y) {
  const out = [];
  let cur = null;
  for (const p of points) {
    if (p.v === null || p.v === undefined) { cur = null; continue; }
    if (!cur) out.push((cur = []));
    cur.push([x(p.t), y(p.v)]);
  }
  return out;
}
const toPath = (seg) => "M" + seg.map(([a, b]) => `${a.toFixed(1)},${b.toFixed(1)}`).join("L");

export class LineChart {
  constructor(el, { unit = "", max = null, digits = 0, label }) {
    Object.assign(this, { el, unit, max, digits, points: [], hover: null });
    el.tabIndex = 0;
    el.setAttribute("role", "img");
    this.label = label;
    this.tip = h("div", { class: "tip", hidden: true });
    el.addEventListener("pointermove", (e) => this.pick(e.clientX - el.getBoundingClientRect().left));
    el.addEventListener("pointerleave", () => this.clear());
    el.addEventListener("blur", () => this.clear());
    el.addEventListener("keydown", (e) => {
      if (e.key !== "ArrowLeft" && e.key !== "ArrowRight") return;
      e.preventDefault();
      const n = this.points.length;
      if (!n) return;
      const i = this.hover === null ? n - 1 : this.hover + (e.key === "ArrowLeft" ? -1 : 1);
      this.hover = Math.min(Math.max(i, 0), n - 1);
      this.render();
    });
    new ResizeObserver(() => this.render()).observe(el);
  }

  update(points) {
    // keep the crosshair on the same moment while the window slides
    if (this.hover !== null && this.points[this.hover]) {
      const t = this.points[this.hover].t;
      const i = points.findIndex((p) => p.t >= t);
      this.hover = i < 0 ? null : i;
    }
    this.points = points;
    this.render();
  }

  clear() { this.hover = null; this.render(); }

  pick(px) {
    if (!this.x || !this.points.length) return;
    let best = 0;
    this.points.forEach((p, i) => { if (Math.abs(this.x(p.t) - px) < Math.abs(this.x(this.points[best].t) - px)) best = i; });
    this.hover = best;
    this.render();
  }

  render() {
    const w = this.el.clientWidth, ht = this.el.clientHeight;
    if (!w || !ht) return;
    const L = 36, R = 10, T = 8, B = 20;
    const vals = this.points.map((p) => p.v).filter((v) => v !== null && v !== undefined);
    const root = svg("svg", { viewBox: `0 0 ${w} ${ht}` });
    this.el.setAttribute("aria-label", vals.length ? `${this.label}: now ${num(vals[vals.length - 1], this.digits)}${this.unit}` : `${this.label}: no data`);
    if (vals.length < 2) {
      const t = svg("text", { class: "no-data", x: w / 2, y: ht / 2, "text-anchor": "middle" });
      t.textContent = "Waiting for data";
      root.append(t);
      this.tip.hidden = true;
      this.el.replaceChildren(root, this.tip);
      return;
    }
    const max = this.max ?? niceMax(Math.max(...vals));
    const t0 = this.points[0].t, t1 = this.points[this.points.length - 1].t;
    const x = (this.x = (t) => L + ((t - t0) / Math.max(t1 - t0, 1)) * (w - L - R));
    const y = (v) => T + (1 - Math.min(v, max) / max) * (ht - T - B);
    for (const f of [0, 0.5, 1]) {
      const yy = y(max * f);
      root.append(svg("line", { class: f ? "grid" : "base", x1: L, x2: w - R, y1: yy, y2: yy }));
      const t = svg("text", { class: "tick", x: L - 6, y: yy + 4, "text-anchor": "end" });
      t.textContent = num(max * f, max * f % 1 ? 1 : 0);
      root.append(t);
    }
    const span = t1 - t0;
    for (const [tt, anchor, text] of [[t0, "start", span >= 60 ? `${Math.round(span / 60)} min ago` : `${Math.round(span)} s ago`], [t1, "end", "now"]]) {
      const t = svg("text", { class: "tick", x: x(tt), y: ht - 4, "text-anchor": anchor });
      t.textContent = text;
      root.append(t);
    }
    for (const seg of segments(this.points, x, y)) {
      const base = y(0);
      if (seg.length > 1) {
        root.append(svg("path", { class: "area", d: `${toPath(seg)}L${seg[seg.length - 1][0]},${base}L${seg[0][0]},${base}Z` }));
        root.append(svg("path", { class: "line", d: toPath(seg) }));
      }
    }
    const shown = this.hover === null ? this.points.length - 1 : this.hover;
    const p = this.points[shown];
    if (this.hover !== null) root.append(svg("line", { class: "cross", x1: x(p.t), x2: x(p.t), y1: T, y2: ht - B }));
    if (p.v !== null && p.v !== undefined) root.append(svg("circle", { class: "dot", cx: x(p.t), cy: y(p.v), r: 4 }));
    this.tip.hidden = this.hover === null;
    this.el.replaceChildren(root, this.tip);
    if (this.hover !== null) {
      this.tip.replaceChildren(h("b", {}, num(p.v, this.digits) + (p.v === null ? "" : this.unit)), ` ${new Date(p.t * 1000).toLocaleTimeString()}`);
      const left = x(p.t) + 12 + this.tip.offsetWidth > w ? x(p.t) - 12 - this.tip.offsetWidth : x(p.t) + 12;
      this.tip.style.left = `${Math.max(left, 0)}px`;
      this.tip.style.top = `${T}px`;
    }
  }
}

export function sparkline(values, max, label) {
  const W = 120, H = 26;
  const pts = values.map((v, i) => ({ t: i, v }));
  const root = svg("svg", { class: "spark", viewBox: `0 0 ${W} ${H}`, role: "img", "aria-label": label });
  const x = (t) => 2 + (t / Math.max(values.length - 1, 1)) * (W - 4);
  const y = (v) => 3 + (1 - Math.min(v, max) / max) * (H - 6);
  for (const seg of segments(pts, x, y)) if (seg.length > 1) root.append(svg("path", { d: toPath(seg) }));
  return root;
}
