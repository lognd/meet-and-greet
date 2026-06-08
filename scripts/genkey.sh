#!/usr/bin/env bash
# Generate a random 32-character hex XOR key for use in app.toml and CI.
#
# Usage:
#   bash scripts/genkey.sh
#
# The key must be set in two places so they agree:
#   1. app.toml  ->  encryption_key = "<key>"
#   2. GitHub Actions secret XORKEY  (Settings -> Secrets -> Actions -> New)
#
# If the XORKEY secret is absent from GitHub, the release workflow generates
# one automatically and uploads XORKEY.txt alongside the release binaries so
# you can copy the value into app.toml.

set -euo pipefail

python3 -c "import secrets; print(secrets.token_hex(16))"
