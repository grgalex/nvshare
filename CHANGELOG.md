# Changelog

## 0.2.0 (unreleased)

- Fix a deadlock at startup with drivers whose `cuInit()` looks up its own symbols (#27).
- `nvsharectl` no longer prints its usage after successful commands (#3).
- The scheduler exits cleanly on `SIGTERM` and `SIGINT` and removes its socket.
- The scheduler no longer takes the GPU lock away from a client that is alone on the GPU.
- The scheduler checks the socket path length before creating its directory.
- Allow overriding the socket directory with `NVSHARE_SOCK_DIR` (default: `/var/run/nvshare`).
- Update the device plugin's gRPC and x/net dependencies (security fixes) and build it with Go 1.26.
- Base the scheduler image and the libnvshare runtime image on Ubuntu 24.04.
- Add `make test` (no GPU needed) and `make test-gpu`.

## 0.1.1 (2024-01-29)

- Support CUDA 12.

## 0.1 (2023-06-02)

- Initial release.
