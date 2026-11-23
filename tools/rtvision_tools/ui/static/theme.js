// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Abhinand A S
// Applies the saved theme before the first paint.
try {
  const t = localStorage.getItem("rtvision-theme");
  if (t === "light" || t === "dark") document.documentElement.dataset.theme = t;
} catch (e) { /* storage blocked: follow the system theme */ }
