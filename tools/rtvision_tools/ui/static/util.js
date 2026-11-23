// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Abhinand A S
// Small helpers. All text goes through textContent: names come from cameras and uploads.
export const $ = (id) => document.getElementById(id);

export function h(tag, props = {}, ...children) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(props)) {
    if (k === "class") el.className = v;
    else if (k.startsWith("on")) el.addEventListener(k.slice(2), v);
    else if (k in el) el[k] = v;
    else el.setAttribute(k, v);
  }
  for (const c of children.flat()) if (c !== null && c !== undefined) el.append(c);
  return el;
}

export function svg(tag, attrs = {}, ...children) {
  const el = document.createElementNS("http://www.w3.org/2000/svg", tag);
  for (const [k, v] of Object.entries(attrs)) el.setAttribute(k, v);
  el.append(...children);
  return el;
}

export async function api(path, { method = "GET", body } = {}) {
  const headers = method === "GET" ? {} : { "X-RTVision": "1", "Content-Type": "application/json" };
  let resp;
  try {
    resp = await fetch(path, { method, headers, body: body === undefined ? undefined : JSON.stringify(body) });
  } catch (e) {
    throw new Error("The UI server is not reachable");
  }
  const data = await resp.json().catch(() => ({}));
  if (!resp.ok) throw Object.assign(new Error(data.error || `Request failed (HTTP ${resp.status})`), { status: resp.status });
  return data;
}

export function toast(text, bad = false) {
  const el = h("div", { class: bad ? "toast bad" : "toast" }, text);
  $("toasts").append(el);
  setTimeout(() => el.remove(), bad ? 8000 : 4000);
}

export function notice(el, text, bad = false) {
  el.hidden = !text;
  el.textContent = text || "";
  el.classList.toggle("bad", bad);
}

const missing = (v) => v === null || v === undefined || Number.isNaN(v);
export const num = (v, digits = 0) =>
  missing(v) ? "–" : v.toLocaleString("en", { minimumFractionDigits: digits, maximumFractionDigits: digits });
export const compact = (v) =>
  missing(v) ? "–" : v < 10000 ? num(v) : new Intl.NumberFormat("en", { notation: "compact", maximumFractionDigits: 1 }).format(v);
export function bytes(v) {
  if (missing(v)) return "–";
  const units = ["B", "KB", "MB", "GB", "TB"];
  let i = 0;
  while (v >= 1024 && i < units.length - 1) { v /= 1024; i++; }
  return `${v.toFixed(v < 10 && i > 0 ? 1 : 0)} ${units[i]}`;
}
export function duration(s) {
  if (missing(s)) return "–";
  s = Math.round(s);
  const d = Math.floor(s / 86400), hh = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60);
  return d ? `${d} d ${hh} h` : hh ? `${hh} h ${m} min` : m ? `${m} min ${s % 60} s` : `${s} s`;
}

// live: refreshed by polling. Skipped while the user presses or keyboard-focuses something inside,
// so a refresh cannot swallow a click or move the focus.
export function table(el, columns, rows, emptyText, live = false) {
  if (live && el.querySelector(":active, :focus-visible")) return;
  const head = h("thead", {}, h("tr", {}, columns.map((c) => h("th", { class: c.num ? "num" : "", scope: "col" }, c.title))));
  const body = h("tbody", {}, rows.length
    ? rows.map((r) => h("tr", {}, columns.map((c) => h("td", { class: c.num ? "num" : "" }, c.cell(r)))))
    : h("tr", {}, h("td", { class: "empty-row", colSpan: columns.length }, emptyText)));
  el.replaceChildren(head, body);
}
