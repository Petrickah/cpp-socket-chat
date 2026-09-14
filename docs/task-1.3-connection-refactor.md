# Task 1.3 prep — TCPConnection refactor

Intermediate report, covering two sessions (2026-09-13, 2026-09-14).
**Task 1.3 itself (IRC framing + core commands) is still not done** — both
sessions were foundational refactoring of the server's connection handling,
once it became clear the Task 1.2 design (a bare `std::map<int,
sockaddr_in>`) wasn't going to carry per-connection state (a receive
buffer, in particular) cleanly. Broadcast, message framing, and IRC command
parsing are still ahead.

## Session 1 (2026-09-13) — TCPConnection/TCPServer split

### What changed

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

### The bug hunt

One of the most iterative debugging exchanges of the project so far —
`TCPConnection` gets copied and reassigned constantly throughout
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

### Verification

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

## Session 2 (2026-09-14) — select() moved into TCPConnection

### What changed

Continued the same refactor: the `select()`/dispatch logic that session 1
left split across `TCPServer`'s `SelectClient()`/`UpdateSocketHandleSet()`
helpers moved entirely onto `TCPConnection` itself (`Connect()`, `Update()`,
`Select()`), so `TCPServer::Update()` is now a thin loop that just asks the
listening connection "what's ready?" and dispatches on the answer.
`Close()` was also redesigned to do its own complete cleanup in one place
(find the client in the map, erase it, print/`perror` the status, `close()`
the fd, `FD_CLR()` it) instead of reporting success/failure back through a
return value for the caller to act on — session 1's error-flag approach
kept getting lost across copies, so this sidesteps that whole problem
rather than continuing to chase it. The receive buffer also switched from
a raw `malloc()`'d `char*` to `std::string`.

### The bug hunt

Just as iterative as session 1, four bugs in sequence:

1. **Wrong IP / port 0 logged for new clients.** The `Client connected: ...`
   print read `m_SocketAddress` (the member) before the `memcpy()` that
   actually populates it from `accept()`'s output — printed whatever
   garbage was already sitting in that memory. Fixed by reordering: the
   print now happens after the address is actually copied in (and later,
   after `Connect()` absorbed this logic directly, the ordering carried
   over correctly).
2. **Communication breaking after the very first message.** Root cause was
   two compounding bugs: `operator=` didn't copy the connection's error
   flag, so a "this connection just closed" signal set by the special
   close-constructor never survived being assigned into another copy — and
   separately, the caller's condition for removing a client from the map
   had the polarity backwards (erasing on `Handle()` returning "no error"
   instead of "error"). Together: **every successful message immediately,
   silently erased that client from the registry**, right after its own
   echo went out — so a client's first message worked, and nothing after
   it ever did. Rather than fix the polarity and the missing copy
   separately, the whole error-flag/return-value approach was dropped in
   favor of `Close()`'s self-contained cleanup (above) — a message being
   handled either results in an explicit `Close()` call or it doesn't,
   with no intermediate signal that needs to survive being copied.
3. **A phantom client on fd 0.** `Select()`'s timeout and error cases
   (`select()` returning `0` or `-1`) returned `true` — meaning "something
   is ready" — instead of `false`. On every idle timeout, this pushed the
   loop's default-constructed placeholder `TCPConnection` (fd `0`,
   never actually connected to anything) through the same path as a real
   new connection, inserting a bogus entry into the client map keyed by
   `0`. The next timeout then tried to `Handle()` it — `recv()` on fd `0`,
   the server process's own **stdin** — which failed and triggered
   `Close()`, which `close()`d fd `0` outright. Confirmed via the server
   log: an extra `"Status: The client has been disconected"` with no
   matching `"Client connected"` ever printed for it, immediately followed
   by a real second client that could no longer connect at all (its
   `accept()`-assigned fd very plausibly landed on the just-closed `0`).
   Fixed by making the timeout/error cases return `false` again — "nothing
   to report" has to actually mean nothing gets dispatched.
4. **Server freezing on the first client, starving everyone else.**
   `Select()` correctly built a disposable working copy of the fd_set
   before calling `select()` (the master-vs-copy split discussed across
   several earlier sessions), but then forwarded the *original, untouched
   master* into `Update()` for the actual readiness checks — not the copy
   `select()` had just populated. Since the master has every
   historically-registered fd's bit permanently set (only ever cleared on
   `Close()`), `IsReady()` against it was true for *every* connected
   client regardless of what `select()` had actually just reported.
   `Update()`'s loop always matched the first client in the map (lowest
   fd, map iteration order) and called blocking `recv()` on it — which hung
   indefinitely whenever that client had nothing new to say, freezing the
   single-threaded loop and starving both other clients and new
   connections. Confirmed directly: client A connects and goes idle,
   client B's connection attempt never even gets `"Client connected"`
   logged while A stays open. Fixed by threading the `select()`-populated
   working copy through to `Update()`'s readiness checks specifically,
   while `Connect()`'s permanent `FD_SET()` still goes through the real
   master — the two fd_sets finally used for the purpose each was actually
   built for.

### Verification

- Two clients, one connected-and-idle while the other connects and
  exchanges messages — both logged correctly, no freeze, confirmed with a
  dedicated two-client test after fix 4.
- Multiple sequential and overlapping client sessions in a single run
  (mixed English/Romanian test messages), clean connect/echo/`EXIT`/
  disconnect logging throughout, no stray or missing `Status` lines.
- `make clean && make` clean throughout.

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
  own — confirmed across both sessions as the right call for now: the server's
  `TCPConnection` class solves a *different* problem (juggling N dynamic
  connections with individual lifecycles) than what the client will
  eventually need (multiplexing exactly two fixed fds — stdin and the one
  server socket — so an incoming broadcast doesn't block on the user
  finishing a line of input). Revisit with a small `select()` loop or a
  receiver thread when broadcast actually lands, not by porting
  `TCPConnection` itself to the client.
