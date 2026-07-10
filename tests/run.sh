#!/bin/bash
# Build and run Hans's native (host-side) tests. No Retro68 needed.
set -euo pipefail
cd "$(dirname "$0")"
cc -Wall -Wextra -o /tmp/hans_test_markdown test_markdown.c
/tmp/hans_test_markdown
