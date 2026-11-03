# Proxy Client 1.0.0 — native Exit Node controller candidate

- `neebles-proxy-client-platform` is a native executable intended for an **OS-root-owned** Boss platform provider.
- It does **not** receive an IP or arbitrary arguments from the UI. Fixed client/server/tailnet identities are enforced.
- It rejects non-root invocation, the wrong hostname, wrong Tailscale IPv4, wrong tailnet, a stopped daemon, or a preexisting different Exit Node.
- Activation selects `100.71.3.52`; deactivation clears it; both verify the resulting status.
- The executable is **not** installed setuid and is not invoked with sudo from Qt.
- The current `/health` response reports `exit_ready=false` and `authorization_enforced=false`, so the current Qt preflight is *not* a deployable activation gate. Do not pretend those fields represent Tailscale's central policy.
- NOT COMPLETE: the Boss OS provider's root-owned policy and descriptor, surfaces and lifecycle action mapping, Secrets-backed Config, CUSTOM V2 material, installation/rollout, QA on VivoProBook and Git publishing require further gates.
- Security note: this helper intentionally has a pinned IP, not configurable IP. A configurable value needs a new authenticated contract; never accept a user-supplied root argv string.
