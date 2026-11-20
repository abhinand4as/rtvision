# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 <PROJECT_OWNER>
"""Consume events (Kafka or --stdin) and validate each against the v1 event schema.

Placeholder: implemented in milestone M4 (--stdin) / M7 (Kafka).
"""

from __future__ import annotations

import sys


def main() -> int:
    print("FAIL: consume_validate not implemented yet (milestones M4/M7)", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
