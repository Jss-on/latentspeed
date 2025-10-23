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
        # Attach derived address for debugging (non-secret)
        try:
            addr = wallet.address.lower()
        except Exception:
            addr = None
        if isinstance(sig, dict):
            out = dict(sig)
            if addr is not None:
                out["address"] = addr
        else:
            out = {"r": getattr(sig, "r", None), "s": getattr(sig, "s", None), "v": getattr(sig, "v", None)}
            if addr is not None:
                out["address"] = addr
        return {"id": req_id, "result": out}
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
            elif method == "build_payload":
                # Build ready-to-post payload using SDK signature
                if _import_error is not None or Account is None or sign_l1_action is None:
                    _print_line({"id": req_id, "error": {"message": f"ImportError: {_import_error}"}})
                    continue
                try:
                    priv = params.get("privateKey")
                    if not isinstance(priv, str) or len(priv) == 0:
                        _print_line({"id": req_id, "error": {"message": "invalid privateKey"}})
                        continue
                    if not (priv.startswith("0x") or priv.startswith("0X")):
                        priv = "0x" + priv
                    action = params.get("action")
                    if isinstance(action, str):
                        action = json.loads(action)
                    if not isinstance(action, dict):
                        _print_line({"id": req_id, "error": {"message": "invalid action"}})
                        continue
                    nonce = params.get("nonce")
                    if not isinstance(nonce, int):
                        _print_line({"id": req_id, "error": {"message": "invalid nonce"}})
                        continue
                    vault_address = params.get("vaultAddress")
                    if vault_address is not None and not isinstance(vault_address, str):
                        _print_line({"id": req_id, "error": {"message": "invalid vaultAddress"}})
                        continue
                    is_mainnet = params.get("isMainnet")
                    if not isinstance(is_mainnet, bool):
                        _print_line({"id": req_id, "error": {"message": "invalid isMainnet"}})
                        continue
                    wallet = Account.from_key(priv)
                    sig = sign_l1_action(wallet, action, vault_address, nonce, None, is_mainnet)

                    # Normalize signature fields and prefer v (27/28)
                    if isinstance(sig, dict):
                        r = sig.get("r")
                        s = sig.get("s")
                        v_raw = sig.get("v")
                        y_parity = sig.get("yParity")
                    else:
                        # Fallback if SDK returns an object-like
                        r = getattr(sig, "r", None)
                        s = getattr(sig, "s", None)
                        v_raw = getattr(sig, "v", None)
                        y_parity = getattr(sig, "yParity", None)

                    # Use r/s exactly as returned by SDK to avoid altering signature recovery

                    payload = {
                        "action": action,
                        "nonce": nonce,
                        "signature": {"r": r, "s": s},
                    }
                    # Prefer v; if missing, derive from yParity
                    if v_raw is not None:
                        try:
                            v_int = int(v_raw, 16) if isinstance(v_raw, str) and v_raw.startswith("0x") else int(v_raw)
                            payload["signature"]["v"] = v_int
                        except Exception:
                            payload["signature"]["v"] = v_raw
                    elif y_parity is not None:
                        try:
                            yp = int(y_parity) if isinstance(y_parity, str) else y_parity
                        except Exception:
                            yp = 0
                        payload["signature"]["v"] = 27 if yp == 0 else 28
                    if vault_address is not None:
                        payload["vaultAddress"] = vault_address
                    # Do not include isMainnet in HTTP/WS payload; server infers from endpoint. It is only used for signing.
                    _print_line({"id": req_id, "result": {"payload": json.dumps(payload, separators=(",", ":")), "address": wallet.address.lower()}})
                except Exception as e:
                    _print_line({"id": req_id, "error": {"message": f"{type(e).__name__}: {e}"}})
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
