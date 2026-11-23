// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Abhinand A S
// Page switching and polling.
import { $, api } from "./util.js";
import * as live from "./live.js";
import * as performance from "./performance.js";
import * as replicator from "./replicator.js";

const POLL_MS = 1000;
const pages = { live, performance, replicator };
let current = null;

function page() {
  const name = location.pathname.slice(1);
  return name in pages ? name : "live";
}

function route() {
  const next = page();
  if (next !== current) {
    pages[current]?.hide?.();
    current = next;
    for (const name of Object.keys(pages)) $(`page-${name}`).hidden = name !== next;
    for (const a of document.querySelectorAll("nav a")) {
      if (a.getAttribute("href") === `/${next}`) a.setAttribute("aria-current", "page"); else a.removeAttribute("aria-current");
    }
    document.title = `${next[0].toUpperCase()}${next.slice(1)} · RTVision`;
    pages[next].show?.();
    poll();
  }
}

function setStatus(ov) {
  const el = $("service-status");
  const [cls, text] = !ov ? ["bad", "UI server offline"] : !ov.online ? ["bad", "RTVision offline"] : !ov.ready ? ["warn", "Starting"] : ["good", `Running · ${ov.info.model.name}`];
  el.className = `status ${cls}`;
  if (el.textContent !== text) el.textContent = text;
}

let polling = false;
async function poll() {
  if (polling || document.hidden) return;
  polling = true;
  try {
    const ov = await api("/api/overview");
    setStatus(ov);
    if (current === "live") live.update(ov);
    else if (current === "performance") {
      const [hw, history] = await Promise.all([api("/api/hardware"), api("/api/history")]);
      performance.update(ov, hw, history);
    } else await replicator.update(ov);
  } catch (e) {
    setStatus(null);
    console.error(e);
  } finally {
    polling = false;
  }
}

document.addEventListener("click", (e) => {
  const a = e.target.closest("a[data-nav]");
  if (!a || e.metaKey || e.ctrlKey || e.shiftKey || e.button) return;
  e.preventDefault();
  history.pushState(null, "", a.getAttribute("href"));
  route();
});
window.addEventListener("popstate", route);
document.addEventListener("visibilitychange", () => { if (!document.hidden) poll(); });
$("theme").addEventListener("click", () => {
  const root = document.documentElement;
  const dark = root.dataset.theme ? root.dataset.theme === "dark" : matchMedia("(prefers-color-scheme: dark)").matches;
  root.dataset.theme = dark ? "light" : "dark";
  try { localStorage.setItem("rtvision-theme", root.dataset.theme); } catch (e) { /* storage blocked */ }
});

for (const p of Object.values(pages)) p.init();
route();
setInterval(poll, POLL_MS);
