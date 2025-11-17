#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." &> /dev/null && pwd)
VENV_DIR="$ROOT_DIR/.venv-hl-signer"

python3 -m venv "$VENV_DIR"
source "$VENV_DIR/bin/activate"
python -m pip install --upgrade pip
python -m pip install -r "$ROOT_DIR/tools/requirements.txt"

echo "" >&2
echo "[OK] Hyperliquid signer venv ready: $VENV_DIR" >&2
echo "To use it, export:" >&2
echo "  export LATENTSPEED_HL_SIGNER_PYTHON=\"$VENV_DIR/bin/python\"" >&2
echo "  export LATENTSPEED_HL_SIGNER_SCRIPT=\"$ROOT_DIR/tools/hl_signer_bridge.py\"" >&2
echo "" >&2

