# Task 1.3 prep — TCPConnection refactor

Intermediate report. **Task 1.3 itself (IRC framing + core commands) is not
done** — this covers a foundational refactor of the server's connection
handling that happened while starting on it, once it became clear the
Task 1.2 design (a bare `std::map<int, sockaddr_in>`) wasn't going to carry
per-connection state (a receive buffer, in particular) cleanly. Broadcast,
message framing, and IRC command parsing are still ahead.

## What changed

`server/main.cpp` split into two classes:

- **`TCPConnection`** — everything about *one* socket: creation, `Bind()`,
  `Connect()` (accept), `Handle()` (one receive+echo), `Close()`, and its
  own receive buffer. Both the listening socket (`TCPServer`'s
  `m_EchoServerConnection`) and every connected client are the same type
  now, instead of the listening socket being handled by bespoke code and
  clients being tracked as a bare address.
- **`TCPServer`** — owns the listening `TCPConnection`, the `select()` loop
  (`Update()`), and `std::map<int, TCPConnection>` for connected clients
  (replacing Task 1.2's `std::map<int, sockaddr_in>`).

This matters for what's coming next: broadcast needs to iterate real
per-client objects to send to each one, and framing needs a receive buffer
that persists *per client* across multiple `recv()` calls — neither fits
naturally into "just an address" the way Task 1.2 had it.

## The bug hunt

This turned into the most iterative debugging exchange of the project so
far — `TCPConnection` gets copied and reassigned constantly throughout
`Update()`/`SelectClient()`, and getting the resource-ownership story right
took several wrong turns before landing on a working design. In order:

1. **Missing `Connect()` fd store.** First working version: `accept()`'s
   return value was checked for failure but never actually saved into
   `m_SocketHandle` — the object kept representing the listening socket
   after "accepting" a client. The real client fd was silently dropped,
   never registered with `select()`, so a connected client's messages were
   never picked up. Fixed by storing `accept()`'s result back into
   `m_SocketHandle`.
2. **Destructor-driven double free / premature close.** Added a destructor
   that called `Close()` (`free()` the buffer, `close()` the fd) for
   symmetry — reasonable instinct, wrong for this class. `TCPConnection`
   has no exclusive-ownership semantics (no move constructor, freely copied
   by value all over `Update()`/`SelectClient()`), so *any* temporary copy
   going out of scope closed/freed a resource that a longer-lived copy
   (the one stored in the client map, or the listening connection) still
   needed. Confirmed via `gdb` backtrace: `free(): double free detected in
   tcache 2`, crashing inside `Close()`, called from `Handle()`'s disconnect
   path. Fixed by removing the destructor — `Close()` stays an explicit,
   deliberate call only, never automatic.
3. **Fd/buffer leak from a per-iteration placeholder.** Once the
   destructor was gone, a different cost showed up: `Update()`'s loop
   declared a fresh `TCPConnection clientConnection;` every iteration
   (using the constructor that calls `socket()`) purely as a scratch
   object for `SelectClient()` to fill in — leaking one real fd *and* one
   buffer on every single loop iteration, including idle timeouts.
   Measured directly: server fd count climbed from 10 to 11 over 6 idle
   seconds with the earlier map-based design; unbounded over a longer run.
4. **First fix attempt regressed into the same crash from a new angle.**
   Restructured to use `TCPConnection*` instead of a fresh value each
   iteration — right instinct (stop needing a "real" placeholder object at
   all), wrong first implementation: pointing `clientConnection` directly
   at `&m_EchoServerConnection` for the "new connection ready" case, then
   calling `Connect()` through that pointer, mutated the server's *own*
   persistent listening-socket object in place (`accept()`'s result
   overwrote `m_EchoServerConnection`'s fd). The listening socket then
   became unreachable from its own bookkeeping after the very first client
   connected — confirmed via a second `gdb` crash, plus `"Failed to accept
   a connection: Bad file descriptor"` in the log right before it.
5. **Final design.** `clientConnection` is `nullptr` when the *listening*
   socket is the one ready (a deliberate sentinel, not an alias to the
   real object), and a real client pointer otherwise. Only in the
   `nullptr` branch — meaning only when a connection is actually pending —
   is a short-lived local `TCPConnection` copy-constructed from
   `m_EchoServerConnection` (cheap: the copy constructor just mallocs a
   buffer, no new `socket()` call), `Connect()` is called on *that* local
   copy (never on the original), and a copy of it is inserted into the
   client map. Scoped to just that `if` block, so nothing is allocated on
   iterations where nothing new is connecting.

## Verification

All measured directly, not just reasoned from the code:

- `make clean && make` — clean build, no warnings, throughout every
  iteration of the above.
- Two clients handled sequentially and concurrently, clean send/receive
  both ways.
- Abrupt disconnect (`SIGKILL`, no `EXIT`) — clean removal, no crash.
- `gdb` backtraces captured for both crash regressions (steps 2 and 4
  above) before fixing them, confirming the exact failing line each time
  rather than guessing.
- Server fd count and `VmRSS`, checked after a real client round-trip and
  again after 15 idle seconds (3 `select()` timeout cycles): **flat at 4
  fds / 4496 kB both times** — the per-iteration leak from step 3 is gone.

## Explicitly deferred, not forgotten

- **Message framing** (`\r\n`-terminated frames, accumulation across
  partial `recv()` calls) is still not implemented — `Handle()` still
  treats one `recv()` call as one complete message. `TCPConnection`'s
  per-client buffer is now a real fit for this (it persists for the
  connection's lifetime, unlike Task 1.2's shared scratch buffer), but the
  accumulate-and-scan-for-CRLF logic itself hasn't been written yet — that
  and the actual IRC command set (`NICK`/`USER`/`JOIN`/`PRIVMSG`/etc.,
  broadcast to other clients) are Task 1.3's actual remaining scope.
- **Client stays single-connection/synchronous**, no `select()` of its
  own — confirmed this session as the right call for now: the server's
  `TCPConnection` class solves a *different* problem (juggling N dynamic
  connections with individual lifecycles) than what the client will
  eventually need (multiplexing exactly two fixed fds — stdin and the one
  server socket — so an incoming broadcast doesn't block on the user
  finishing a line of input). Revisit with a small `select()` loop or a
  receiver thread when broadcast actually lands, not by porting
  `TCPConnection` itself to the client.
