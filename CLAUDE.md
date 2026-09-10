# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Status

Scaffold only (2026-09-10): directory layout, Makefile, CI mirror wired up.
`server/main.cpp` and `client/main.cpp` are placeholder stubs — no socket code
yet. Task decomposition for the actual implementation lives in a private vault
note outside this repo; ask if you need that context, don't assume it's
summarized correctly here.

## Methodology — who writes what

This repo exists to practice a specific skill (raw socket programming,
`select()`/`poll()` event loops, GNU toolchain/gdb/ASan on Linux) that the
author does not yet have production experience with. Writing the socket and
protocol code for him would defeat the point of the exercise — same rule as
`leetcode-solutions` (review-only) and `gram-engine` ("first time doing
something new → user writes it").

- **User writes**: all socket/networking code, the IRC protocol
  parsing/framing, the event loop itself — anything with real learning value.
- **Claude Code writes/does**: scaffolding (build system, project layout,
  CI wiring), code review/feedback, debugging help, README/docs.

## Commands

- Build: `make` (builds `server/server` and `client/client`, `g++ -std=c++17 -Wall -Wextra -g`)
- Sanitizer build: `make sanitize` (adds `-fsanitize=address`)
- Clean: `make clean`
- Run: `./server/server`, `./client/client`

## Architecture

Single-threaded event loop (`select()`/`poll()`) on the server, not
thread-per-client — deliberately, since that's the pattern relevant to a
low-latency trading-systems context. Server and client are separate binaries
under `server/` and `client/`, no shared build target between them yet. The
wire protocol is a subset of IRC (RFC 2812 / modern.ircdocs.horse), not a
custom framing — validated against a real IRC client, not just the bundled
one.

## Non-goals (current)

- No GUI (console-only).
- No containerization/deployment — no `Dockerfile`, no Kubernetes manifests.
  The `Jenkinsfile` in this repo currently does nothing but check out the
  source and mirror it to GitHub; it does not build or publish anything yet.
- No thread-per-client.
- No TLS/encryption, no authentication, no persistence (DB, disk logging).
- No IRC extensions beyond the basic command set (no MODE/CTCP/DCC/server-linking/IRCv3 capability negotiation).
- Zero external dependencies — POSIX sockets + stdlib only.

## CI/CD

Gitea (source of truth) -> Jenkins multibranch (`Jenkinsfile`): currently just
`Checkout` -> `Mirror to GitHub` (SSH deploy key, `github-mirror-cpp-socket-chat`
Jenkins credential, write-scoped to this GitHub repo only via a GitHub Deploy
Key — not a broader PAT). No build/test/image stages yet — those get added
once there's a `Dockerfile` and CI worth running against real code.
