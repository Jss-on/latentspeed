#!/usr/bin/env python3
import sys
import json
import traceback

try:
    from eth_account import Account
    from hyperliquid.utils.signing import sign_l1_action
except Exception as e:
    # Defer import error handling until we process first request
    Account = None  # type: ignore
    sign_l1_action = None  # type: ignore
    _import_error = str(e)
else:
    _import_error = None


def _print_line(obj):
    sys.stdout.write(json.dumps(obj, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def _handle_sign_l1(req_id: int, params: dict):
    if _import_error is not None or Account is None or sign_l1_action is None:
        return {"id": req_id, "error": {"message": f"ImportError: {_import_error}"}}
    try:
        priv = params.get("privateKey")
        if not isinstance(priv, str) or len(priv) == 0:
            return {"id": req_id, "error": {"message": "invalid privateKey"}}
        if not (priv.startswith("0x") or priv.startswith("0X")):
            priv = "0x" + priv

        action = params.get("action")
        if isinstance(action, str):
            action = json.loads(action)
        if not isinstance(action, dict):
            return {"id": req_id, "error": {"message": "invalid action"}}

        nonce = params.get("nonce")
        if not isinstance(nonce, int):
            return {"id": req_id, "error": {"message": "invalid nonce"}}

        vault_address = params.get("vaultAddress")
        if vault_address is not None:
            if not isinstance(vault_address, str):
                return {"id": req_id, "error": {"message": "invalid vaultAddress"}}
            vault_address = vault_address.lower()

        expires_after = params.get("expiresAfter")
        if expires_after is not None and not isinstance(expires_after, int):
            return {"id": req_id, "error": {"message": "invalid expiresAfter"}}

        is_mainnet = params.get("isMainnet")
        if not isinstance(is_mainnet, bool):
            return {"id": req_id, "error": {"message": "invalid isMainnet"}}

        wallet = Account.from_key(priv)
        sig = sign_l1_action(wallet, action, vault_address, nonce, expires_after, is_mainnet)
        return {"id": req_id, "result": sig}
    except Exception as e:
        return {"id": req_id, "error": {"message": f"{type(e).__name__}: {e}"}}


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
            req_id = req.get("id")
            method = req.get("method")
            params = req.get("params", {})
            if not isinstance(req_id, int):
                _print_line({"id": None, "error": {"message": "invalid id"}})
                continue
            if method == "ping":
                _print_line({"id": req_id, "result": "pong"})
            elif method == "sign_l1":
                _print_line(_handle_sign_l1(req_id, params))
            else:
                _print_line({"id": req_id, "error": {"message": f"unknown method: {method}"}})
        except Exception as e:
            _print_line({"id": None, "error": {"message": f"{type(e).__name__}: {e}"}})


if __name__ == "__main__":
    try:
        main()
    except Exception:
        sys.stderr.write("hl_signer_bridge fatal error:\n" + traceback.format_exc() + "\n")
        sys.stderr.flush()
        sys.exit(1)

