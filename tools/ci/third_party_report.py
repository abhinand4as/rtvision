# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
"""Third-party report: what the built RTVision image actually contains, with licences.

    python3 tools/ci/third_party_report.py --image rtvision:local            # print Markdown
    python3 tools/ci/third_party_report.py --image rtvision:local --write    # update the
                                        # generated section of THIRD_PARTY_NOTICES.md
    python3 tools/ci/third_party_report.py --image rtvision:local --check    # exit 1 on a
                                        # licence that needs the project owner's review

Runs this same file inside the image (`--inner`, standard library only) to collect:
  1. Python packages installed in /opt/rtvision/python (the rtvision-run tools);
  2. apt packages the Dockerfile adds on top of the DeepStream base image (diff of dpkg lists);
  3. shared libraries that the `rtvision` binary and librtvision_parsers.so load (ldd),
     grouped by owning Debian package, or marked NVIDIA when they come from /opt/nvidia or CUDA.
Licences come from package metadata (Python) and /usr/share/doc/<pkg>/copyright (Debian).
docs/LICENSING.md §6 is the intent; this report is the evidence.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
NOTICES = REPO / "THIRD_PARTY_NOTICES.md"
BEGIN = "<!-- BEGIN GENERATED: tools/ci/third_party_report.py -->"
END = "<!-- END GENERATED -->"
DEFAULT_BASE = "nvcr.io/nvidia/deepstream:9.1-samples-multiarch"
BINARIES = ["/opt/rtvision/bin/rtvision", "/opt/rtvision/lib/librtvision_parsers.so"]
PY_TARGET = "/opt/rtvision/python"
NVIDIA_PREFIXES = (
    "/opt/nvidia/",
    "/usr/local/cuda",
    "/usr/lib/x86_64-linux-gnu/libnv",
    "/usr/lib/x86_64-linux-gnu/libcuda",
    "/usr/lib/aarch64-linux-gnu/libnv",
)

# Effective licence of the LIBRARY a package ships, where the Debian copyright file lists more
# (build scripts, CLI tools, tests, packaging) or is not machine-readable. Reviewed 2026-11-20
# against /usr/share/doc/<pkg>/copyright in the DeepStream 9.1 image; re-check when the base
# image changes. Packages not listed use the licence of their `Files: *` stanza.
KNOWN = {
    "libbz2-1.0": ("bzip2-1.0.6 (BSD-style)", "Files: * BSD-variant; GPL-2 only debian/*"),
    "liblz4-1": ("BSD-2-Clause", "lib/ is BSD-2-Clause; GPL-2+ only programs/ and tests/"),
    "libzstd1": ("BSD-3-Clause OR GPL-2.0-only", "dual licence, used under BSD-3-Clause"),
    "libsasl2-2": ("BSD-3-Clause-Attribution", "Files: *; GPL only in debian/ packaging"),
    "libselinux1": ("public-domain", "library; GPL-2 only utils/avcstat.c and debian/*"),
    "libsqlite3-0": ("blessing (public domain)", "GPL-2+ only debian/*"),
    "libssl3t64": ("Apache-2.0", "OpenSSL 3; Artistic/GPL only a bundled Perl build module"),
    "libunwind8": ("MIT", "Expat"),
    "libyaml-cpp0.8": ("MIT", "X11; GPL-2.0+ only debian/*"),
    "libglvnd-dev": ("MIT", "libGLdispatch, LD_PRELOADed by the base image; GPL only m4 scripts"),
    "libblkid1": ("LGPL-2.1-or-later", "util-linux: libblkid is LGPL; GPL for the CLI tools"),
    "libmount1": ("LGPL-2.1-or-later", "util-linux: libmount is LGPL; GPL for the CLI tools"),
    "libdw1t64": ("LGPL-3.0-or-later OR GPL-2.0-or-later", "elfutils libraries (dual)"),
    "libelf1t64": ("LGPL-3.0-or-later OR GPL-2.0-or-later", "elfutils libraries (dual)"),
    "libidn2-0": ("LGPL-3.0-or-later OR GPL-2.0-or-later", "*.c *.h (dual)"),
    "libunistring5": ("LGPL-3.0-or-later OR GPL-2.0-or-later", "Files: * (dual)"),
    "libkeyutils1": ("LGPL-2.0-or-later", "keyutils.* (the library)"),
    "libcom-err2": ("MIT-US-export", "e2fsprogs lib/et (com_err); GPL for the e2fs tools"),
    "libffi8": ("MIT", "Expat; GPL only build/test scripts"),
    "liblzma5": ("public-domain", "liblzma; GPL-2+ only scripts and lib/ getopt"),
    "libc6": ("LGPL-2.1-or-later", "GNU C Library (copyright file not machine-readable)"),
    "libgcc-s1": ("GPL-3.0-or-later WITH GCC-exception-3.1", "GCC runtime library exception"),
    "libstdc++6": ("GPL-3.0-or-later WITH GCC-exception-3.1", "GCC runtime library exception"),
    "libgomp1": ("GPL-3.0-or-later WITH GCC-exception-3.1", "GCC runtime; LD_PRELOADed by base"),
    "libkrb5-3": ("MIT", "MIT Kerberos"),
    "libk5crypto3": ("MIT", "MIT Kerberos"),
    "libkrb5support0": ("MIT", "MIT Kerberos"),
    "libgssapi-krb5-2": ("MIT", "MIT Kerberos"),
    "liborc-0.4-0t64": ("BSD-2-Clause", "Orc (copyright file not machine-readable)"),
    "libgstreamer1.0-0": ("LGPL-2.1-or-later", "GStreamer core libraries"),
    "libgstreamer-plugins-base1.0-0": ("LGPL-2.1-or-later", "gst-plugins-base libraries"),
    "libgstrtspserver-1.0-0": ("LGPL-2.0-or-later", "gst-rtsp-server"),
    "cuda-cudart-13-2": ("NVIDIA CUDA EULA", "proprietary, from the base image"),
}

# Licences that always need a review (docs/LICENSING.md §6). LGPL and GPL-with-exception are
# handled separately below.
REVIEW = re.compile(r"\b(AGPL|SSPL|BUSL|Commons-Clause|non-?commercial|CC-BY-NC)", re.I)
GPL = re.compile(r"(?<![AL])GPL", re.I)
GPL_OK = re.compile(r"exception|LGPL|GCC|runtime", re.I)


# --------------------------------------------------------------------------------------------
# Inner part: runs inside the image, standard library only.
# --------------------------------------------------------------------------------------------
def _run(cmd: list[str]) -> str:
    return subprocess.run(cmd, capture_output=True, text=True, check=False).stdout


def _dpkg_packages() -> dict[str, str]:
    out = _run(["dpkg-query", "-W", "-f", "${Package}\t${Version}\n"])
    return dict(line.split("\t", 1) for line in out.splitlines() if "\t" in line)


def _deb_license(pkg: str) -> dict:
    """{"main": licence of the `Files: *` stanza (DEP-5) or None, "all": every licence named}."""
    name = pkg.split(":")[0]
    path = Path("/usr/share/doc") / name / "copyright"
    if not path.is_file():
        return {"main": None, "all": []}
    main, found, files = None, [], None
    for line in path.read_text(errors="replace").splitlines():
        if line.startswith("Files:"):
            files = line.split(":", 1)[1].strip()
        elif line.startswith("License:"):
            lic = line.split(":", 1)[1].strip()
            if files == "*" and main is None:
                main = lic
            if lic and lic not in found:
                found.append(lic)
            files = None
    return {"main": main, "all": found}


def _owner(path: str) -> str | None:
    def query(p: str) -> str | None:
        for line in _run(["dpkg", "-S", p]).splitlines():
            if line.startswith("diversion ") or ":" not in line:
                continue
            return line.split(":", 1)[0].split(",")[0].strip()
        return None

    for candidate in dict.fromkeys((os.path.normpath(path), os.path.realpath(path))):
        # usrmerge: /lib/... may be registered as /usr/lib/... or the other way round
        alt = candidate[4:] if candidate.startswith("/usr/") else "/usr" + candidate
        owner = query(candidate) or query(alt)
        if owner:
            return owner
    return None


def _python_packages() -> list[dict]:
    import importlib.metadata as md

    rows = []
    for dist in md.distributions(path=[PY_TARGET]):
        meta = dist.metadata
        lic = meta.get("License-Expression") or ""
        if not lic:
            raw = (meta.get("License") or "").strip()
            lic = raw.splitlines()[0][:80] if raw and len(raw) < 200 else ""
        classifiers = [
            c.split("::")[-1].strip()
            for c in meta.get_all("Classifier") or []
            if c.startswith("License ::")
        ]
        if not lic:
            lic = "; ".join(classifiers) or "unknown"
        url = meta.get("Home-page") or ""
        for entry in meta.get_all("Project-URL") or []:
            label, _, link = entry.partition(",")
            if not url or label.strip().lower() in ("homepage", "source", "repository"):
                url = link.strip()
                if label.strip().lower() == "homepage":
                    break
        rows.append({"name": meta["Name"], "version": dist.version, "license": lic, "url": url})
    return sorted(rows, key=lambda r: r["name"].lower())


def _linked_libraries() -> list[dict]:
    libs: dict[str, dict] = {}
    for binary in BINARIES:
        for line in _run(["ldd", binary]).splitlines():
            m = re.search(r"=>\s+(/\S+)", line) or re.match(r"\s*(/\S+)", line)
            if not m:
                continue
            path = os.path.realpath(m.group(1))
            name = os.path.basename(m.group(1))
            entry = libs.setdefault(name, {"lib": name, "path": path, "used_by": []})
            if os.path.basename(binary) not in entry["used_by"]:
                entry["used_by"].append(os.path.basename(binary))
    for entry in libs.values():
        entry["package"] = _owner(entry["path"])
    return sorted(libs.values(), key=lambda e: e["lib"])


def inner(dpkg_only: bool) -> int:
    data: dict = {"dpkg": _dpkg_packages()}
    if not dpkg_only:
        data["python"] = _python_packages()
        data["libraries"] = _linked_libraries()
        pkgs = {e["package"] for e in data["libraries"] if e["package"]}
        data["deb_licenses"] = {p: _deb_license(p) for p in sorted(pkgs | set(data["dpkg"]))}
    json.dump(data, sys.stdout)
    return 0


# --------------------------------------------------------------------------------------------
# Host part.
# --------------------------------------------------------------------------------------------
def collect(image: str, dpkg_only: bool = False) -> dict:
    cmd = [
        "docker",
        "run",
        "--rm",
        "--entrypoint",
        "python3",
        "-v",
        f"{Path(__file__).resolve()}:/tmp/tpr/tools/ci/third_party_report.py:ro",
        image,
        "/tmp/tpr/tools/ci/third_party_report.py",
        "--inner",
    ]
    if dpkg_only:
        cmd.append("--dpkg-only")
    out = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if out.returncode != 0:
        sys.exit(f"FAIL: inner report in {image} failed: {out.stderr.strip()[-500:]}")
    return json.loads(out.stdout)


def needs_review(lic: str) -> bool:
    """A dual licence ("A OR B") needs review only if every alternative does."""
    alternatives = re.split(r"\s+or\s+", lic, flags=re.I)
    if len(alternatives) > 1:
        return all(needs_review(a) for a in alternatives)
    if REVIEW.search(lic):
        return True
    return bool(GPL.search(lic)) and not GPL_OK.search(lic)


def effective_license(pkg: str, info: dict | None) -> tuple[str, str]:
    """(licence of the library, basis) for a Debian package."""
    if pkg in KNOWN:
        return KNOWN[pkg][0], "reviewed: " + KNOWN[pkg][1]
    if info and info.get("main"):
        others = [x for x in info["all"] if x != info["main"]]
        return info["main"], (
            "copyright `Files: *`" + (f"; others: {len(others)}" if others else "")
        )
    if info and info.get("all"):
        return " / ".join(info["all"]), "copyright (no `Files: *` stanza)"
    return "unknown", "no machine-readable copyright file"


def _image_id(image: str) -> str:
    out = subprocess.run(
        ["docker", "image", "inspect", "-f", "{{.Id}}", image],
        capture_output=True,
        text=True,
        check=False,
    ).stdout.strip()
    return out.split(":")[-1][:12] if out else "?"


def render(image: str, base: str, data: dict, base_data: dict) -> tuple[str, list[str]]:
    review: list[str] = []
    lic_of = data["deb_licenses"]
    lines = [
        BEGIN,
        f"## Generated report: contents of `{image}` (image `{_image_id(image)}`)",
        "",
        f"Base image `{base}`. Regenerate after any change to the Dockerfile or the Python "
        "dependencies: `python3 tools/ci/third_party_report.py --image rtvision:local --write`.",
        "",
        f"### Python packages in `{PY_TARGET}` ({len(data['python'])})",
        "",
        "| Package | Version | Licence (package metadata) | URL |",
        "|---|---|---|---|",
    ]
    for r in data["python"]:
        flag = " ⚠" if needs_review(r["license"]) else ""
        if flag:
            review.append(f"python {r['name']}: {r['license']}")
        lines.append(f"| {r['name']} | {r['version']} | {r['license']}{flag} | {r['url']} |")

    added = sorted(set(data["dpkg"]) - set(base_data["dpkg"]))
    lines += [
        "",
        f"### apt packages added by `docker/Dockerfile` on top of the base image ({len(added)})",
        "",
        "| Package | Version | Licence | Basis |",
        "|---|---|---|---|",
    ]
    for p in added:
        lic, basis = effective_license(p, lic_of.get(p))
        flag = " ⚠" if needs_review(lic) else ""
        if flag:
            review.append(f"apt {p}: {lic}")
        lines.append(f"| {p} | {data['dpkg'][p]} | {lic}{flag} | {basis} |")
    if not added:
        lines.append("| (none: every runtime package already ships in the base image) | | | |")

    by_pkg: dict[str, list[str]] = {}
    nvidia: list[str] = []
    unowned: list[str] = []
    for e in data["libraries"]:
        if e["package"]:
            by_pkg.setdefault(e["package"], []).append(e["lib"])
        elif e["path"].startswith(NVIDIA_PREFIXES):
            nvidia.append(e["lib"])
        else:
            unowned.append(f"{e['lib']} ({e['path']})")
    lines += [
        "",
        f"### Shared libraries loaded by `rtvision` and `librtvision_parsers.so` ({len(data['libraries'])})",  # noqa: E501
        "",
        "Resolved with `ldd`. GStreamer plugins (including DeepStream's) are loaded at runtime "
        "from the base image and are not listed.",
        "",
        "| Debian package | Source | Library licence | Basis | Libraries |",
        "|---|---|---|---|---|",
    ]
    for p in sorted(by_pkg):
        lic, basis = effective_license(p, lic_of.get(p))
        source = "added" if p in added else "base image"
        flag = " ⚠" if needs_review(lic) else ""
        if flag:
            review.append(f"linked {p}: {lic}")
        libs = ", ".join(sorted(by_pkg[p]))
        lines.append(f"| {p} | {source} | {lic}{flag} | {basis} | {libs} |")
    if nvidia:
        lines.append(
            "| (NVIDIA DeepStream / CUDA, not in dpkg) | base image | NVIDIA proprietary (EULA) "
            f"| path under /opt/nvidia or CUDA | {', '.join(sorted(nvidia))} |"
        )
    if unowned:
        lines.append(f"| (no owning package) | ? | review | | {', '.join(sorted(unowned))} |")
        review += [f"unowned {u}" for u in unowned]
    lines += [
        "",
        "⚠ = needs a review by the project owner (copyleft beyond LGPL/GPL-with-exception, or "
        "non-commercial terms); see `docs/LICENSING.md` §6.",
        END,
    ]
    return "\n".join(lines) + "\n", review


def write_notices(section: str) -> None:
    text = NOTICES.read_text()
    if BEGIN in text and END in text:
        head = text[: text.index(BEGIN)]
        tail = text[text.index(END) + len(END) :].lstrip("\n")
        text = head + section + ("\n" + tail if tail else "")
    else:
        text = text.rstrip("\n") + "\n\n" + section
    NOTICES.write_text(text)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--image", default="rtvision:local")
    ap.add_argument("--base-image", default=DEFAULT_BASE)
    ap.add_argument("--write", action="store_true", help=f"update {NOTICES.name}")
    ap.add_argument("--check", action="store_true", help="exit 1 if anything needs review")
    ap.add_argument("--inner", action="store_true", help=argparse.SUPPRESS)
    ap.add_argument("--dpkg-only", action="store_true", help=argparse.SUPPRESS)
    args = ap.parse_args(argv)
    if args.inner:
        return inner(args.dpkg_only)

    data = collect(args.image)
    base_data = collect(args.base_image, dpkg_only=True)
    section, review = render(args.image, args.base_image, data, base_data)
    if args.write:
        write_notices(section)
        print(f"updated {NOTICES.relative_to(REPO)}")
    else:
        print(section)
    if review:
        print(
            f"{'FAIL' if args.check else 'NOTE'}: {len(review)} item(s) need review:",
            file=sys.stderr,
        )
        for r in review:
            print(f"  - {r}", file=sys.stderr)
        return 1 if args.check else 0
    print("PASS: no licence needs review", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
