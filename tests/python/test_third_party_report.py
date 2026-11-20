# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Abhinand A S
"""tools/ci/third_party_report.py: licence classification and THIRD_PARTY_NOTICES.md update.

The Docker part (collecting from the image) runs on a GPU-less host too, but needs the built
image; it is exercised before a release (docs/LICENSING.md §8), not in CI.
"""

from __future__ import annotations

import sys
from pathlib import Path

import pytest
from conftest import REPO

sys.path.insert(0, str(REPO / "tools" / "ci"))
import third_party_report as tpr  # noqa: E402


@pytest.mark.parametrize(
    ("lic", "review"),
    [
        ("MIT", False),
        ("LGPL-2.1-or-later", False),
        ("GPL-3.0-or-later WITH GCC-exception-3.1", False),
        ("BSD-3-Clause OR GPL-2.0-only", False),  # dual: one permissive alternative suffices
        ("LGPL-3.0-or-later OR GPL-2.0-or-later", False),
        ("GPL-2.0-only", True),
        ("GPL-2+", True),
        ("AGPL-3.0-only", True),
        ("CC-BY-NC-4.0", True),
        ("SSPL-1.0", True),
        ("GPL-2.0-only OR AGPL-3.0", True),
    ],
)
def test_needs_review(lic: str, review: bool) -> None:
    assert tpr.needs_review(lic) is review


def test_effective_license_prefers_reviewed_then_files_star() -> None:
    lic, basis = tpr.effective_license("liblz4-1", {"main": "GPL-2+", "all": ["GPL-2+"]})
    assert lic == "BSD-2-Clause" and basis.startswith("reviewed:")
    lic, basis = tpr.effective_license("libfoo1", {"main": "MIT", "all": ["MIT", "GPL-2+"]})
    assert lic == "MIT" and "others: 1" in basis
    assert tpr.effective_license("libbar1", {"main": None, "all": []})[0] == "unknown"


def test_write_notices_replaces_only_the_generated_section(tmp_path: Path, monkeypatch) -> None:
    notices = tmp_path / "THIRD_PARTY_NOTICES.md"
    notices.write_text(f"# Head\nkept\n\n{tpr.BEGIN}\nold\n{tpr.END}\n\nTail kept\n")
    monkeypatch.setattr(tpr, "NOTICES", notices)
    tpr.write_notices(f"{tpr.BEGIN}\nnew\n{tpr.END}\n")
    assert notices.read_text() == f"# Head\nkept\n\n{tpr.BEGIN}\nnew\n{tpr.END}\n\nTail kept\n"


def test_committed_notices_contain_a_generated_report() -> None:
    text = (REPO / "THIRD_PARTY_NOTICES.md").read_text()
    assert tpr.BEGIN in text and tpr.END in text
    assert "⚠" not in text.split(tpr.BEGIN)[1].split("⚠ = needs a review")[0]
