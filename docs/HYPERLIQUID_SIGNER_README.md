# Hyperliquid Signer Bridge Setup

This guide explains how to set up the Python-based signer bridge used by the Hyperliquid adapter to sign actions (orders, cancels, etc.) with exact parity to the official SDK.

Why a bridge
- Guarantees correct msgpack hashing and EIP-712 signing, which is easy to get subtly wrong in a native reimplementation.
- Keeps private keys in memory of a short-lived, local Python process (no network exposure).
- Overhead is sub-millisecond per sign, negligible relative to network/block cadence.

Requirements
- Python 3.10+ with pip
- Internet access to install the Python SDK (first time only)

Quick setup (recommended)
1) Create a local venv and install the SDK
   - bash latentspeed/tools/setup_hl_signer.sh
2) Export environment variables so the engine uses this signer
   - export LATENTSPEED_HL_SIGNER_PYTHON="latentspeed/.venv-hl-signer/bin/python"
   - export LATENTSPEED_HL_SIGNER_SCRIPT="latentspeed/tools/hl_signer_bridge.py"

Manual setup (alternative)
- python3 -m venv latentspeed/.venv-hl-signor
- source latentspeed/.venv-hl-signer/bin/activate
- python -m pip install --upgrade pip
- python -m pip install hyperliquid-python-sdk
- export LATENTSPEED_HL_SIGNER_PYTHON="latentspeed/.venv-hl-signer/bin/python"
- export LATENTSPEED_HL_SIGNER_SCRIPT="latentspeed/tools/hl_signer_bridge.py"

Verify the bridge
- Ping the bridge (should return {"result":"pong"})
  - echo '{"id":1,"method":"ping"}' | latentspeed/.venv-hl-signer/bin/python latentspeed/tools/hl_signer_bridge.py
- If you see an ImportError mentioning hyperliquid, (re)run the setup script or reinstall the SDK in your venv.

How the engine locates the bridge
- Defaults (if env vars not set):
  - Python: "python3" on PATH
  - Script: "latentspeed/tools/hl_signer_bridge.py" (relative to working dir)
- Recommended: always set the two env vars so paths are explicit, especially when running from outside the repo root.

Security and best practices
- Never log private keys or signatures. The bridge only takes keys via stdin from the engine.
- Keep the venv directory and bridge script only on a trusted, local disk.
- Use separate API wallets (agent wallets) per process/subaccount as advised in Hyperliquid docs.

Troubleshooting
- ImportError: hyperliquid-python-sdk
  - Ensure the venv is created and the SDK installed; re-run the setup script.
- "Script not found"
  - Set LATENTSPEED_HL_SIGNER_SCRIPT to an absolute path, or run from the repo root.
- No output / timeouts
  - Ensure your firewall/AV isn’t blocking local process creation; check that the Python process is running; validate that LATENTSPEED_HL_SIGNER_PYTHON points to the venv’s python.

Notes
- The signer bridge is only used to sign payloads. All network I/O (REST/WS) remains in the C++ adapter.
- The adapter lowers cases of addresses and trims numeric strings before signing per Hyperliquid requirements.

