# Task 1.2 — Multi-client via select()

Server rewritten from single-blocking-client (Task 1.1) to a `select()`-driven
event loop over the listening socket plus every connected client fd. Still
echo, not broadcast — that's Task 1.3. Client (`client/main.cpp`) was not
touched this session: it stays a single, synchronous connection to one
server, so it has no need for its own `select()`/multiplexing yet.

## What changed

`server/main.cpp`'s `TCPServer` no longer blocks in `accept()` every loop
iteration. Instead:

- A "master" `fd_set` (`m_ServerSocketHandleSet`) is maintained across the
  whole run — the listening socket plus every currently-connected client fd,
  added on connect, cleared on disconnect (`FD_SET`/`FD_CLR`).
- `Update()` runs one loop: a single `select()` call over the master set
  (via a fresh, disposable copy — see bugs below) with a timeout, then
  dispatches based on which fd came back ready — `accept()` only if the
  listening socket is ready, otherwise `HandleClient()` for the one client
  fd that is.
- `HandleClient()` processes exactly one message per call (one `recvfrom()`
  + `sendto()`), then returns control to `Update()`'s loop — it does not
  loop internally waiting for more data from the same client.
- `CloseClient()` erases the client from the bookkeeping map, clears its bit
  from the master fd_set, and closes its fd — all three, every time.

This was a genuinely bug-dense rewrite — `select()`'s destructive-mutation
semantics and fd-reuse-on-reconnect are the two classic gotchas everyone
hits the first time, and both showed up here in sequence, each masking the
next until fixed.

## Bugs found and fixed, in the order they surfaced

1. **Blocking `accept()` on every loop iteration.** The first draft called
   `accept()` unconditionally at the top of each loop, before ever checking
   whether existing clients had anything to say — so after the first client
   connected, the server blocked waiting for a *second* one instead of
   servicing the first.
2. **`select()`'s fd_set only ever contained the listening socket, and its
   first argument (`nfds`) was a single client fd instead of the real
   upper bound.** Readiness checks were structurally checking the wrong
   thing, and a function that computed real per-client readiness (`bool
   clientSocketSelected`) never actually returned it — a hardcoded `return
   false;` at the end made the whole result path dead code, so
   `HandleClient()` was never called at all.
3. **Dangling iterator after `std::map::erase()`.** The original
   `CloseClient()` called `erase(iterator)` and then kept dereferencing that
   same (now-invalidated) iterator to `close()` the fd — undefined
   behavior, the root cause of an earlier "crashes after a while" report.
   Also relied on `erase()`'s return value to decide whether to `close()`
   at all, which is wrong: `erase()` doesn't fail on a valid iterator, it
   just returns `end()` when the erased element was the map's last one —
   so `close()` was being skipped exactly when there was only one client.
4. **`select()` destructively mutates its fd_set argument.** Once the
   `fd_set` bugs above were fixed and a proper master set was introduced,
   it was kept as a class member and passed *by reference* into every
   `select()` call. POSIX `select()` overwrites the set in place to reflect
   only the fds that were actually ready — on a timeout, that means *all*
   bits get cleared. Since the master was never rebuilt before the next
   call, the very first timeout permanently emptied it: no connection could
   ever be accepted again. Fixed by passing the fd_set *by value* into
   `SelectClient()` — a fresh copy per call, so `select()` only ever
   destroys the copy, never the master.
5. **Stale, closed fd left in the master set.** `CloseClient()` closed the
   client's fd and erased it from the bookkeeping map, but never called
   `FD_CLR()` on the master set. A `select()` call given a set containing a
   closed fd fails outright with `EBADF` — confirmed via `strace`
   (`pselect6(..., [3 4], ...) = -1 EBADF`), and once that started
   happening it repeated on every subsequent call forever, since the stale
   bit was never cleared. This meant: connect a client, let it disconnect,
   and the server could never accept *any* further connection — not just
   from that client, from anyone — because the corrupted set poisoned every
   future `select()` call, not just the one checking that specific fd.
6. **Internal per-client receive loop defeated the whole point of
   `select()`.** `HandleClient()` had its own `while` loop that kept calling
   `recvfrom()` on the same client until it disconnected or sent `EXIT` —
   so once the server started handling client A, it never returned to
   `Update()`'s loop to check whether client B had anything to say. Client
   B's messages sat unread in the kernel's receive buffer, only picked up
   once client A's connection ended. Fixed by having `HandleClient()`
   handle exactly one message per call and return, letting `Update()`'s own
   `select()`-driven loop decide who goes next.
7. **fd reuse on reconnect wasn't re-registered.** The code that adds a new
   client's fd to the master set only called `FD_SET()` when growing
   `m_ServerSocketHandleSetSize` (i.e., only for a fd higher than anything
   seen before). Since the OS reuses the lowest free fd, a client that
   disconnects and reconnects usually gets back the *same* fd it just had
   — which no longer triggered `FD_SET()`, since the set-size bookkeeping
   thought it had already "seen" that fd. The client would show as
   connected (bookkeeping `insert()` isn't gated on this) but its messages
   would never be picked up by `select()` again. Fixed by making `FD_SET()`
   unconditional on every connect, keeping only the `nfds`-growth check
   conditional.
8. **No cleanup path for an abrupt disconnect (no `EXIT`).**
   `HandleClient()` only ever called `CloseClient()` from inside the
   `m_ServerSocketRecieved > 0` branch — a client that disconnected without
   sending `EXIT` (crash, `Ctrl+C`, network drop) made `recvfrom()` return
   `0`, which fell through to a bare `return true;` with no cleanup at all.
   Consequences, all reproduced directly: the dead fd stayed registered
   forever; a closed connection is permanently "readable" for `select()`
   (immediate EOF), so the server's loop stopped blocking on its timeout
   and spun continuously — measured server CPU climbing 33% → 49% → 60%
   over three seconds; and since `std::map` iterates in key order and the
   dead client's fd was numerically lower (it connected first), it was
   always checked before any live client, permanently starving every other
   connection. Fixed by giving `m_ServerSocketRecieved < 1` its own
   explicit branch that calls `CloseClient()` (with `errorCode = 0` — this
   is an expected disconnect, not a failure).

## A smaller cleanup alongside the bug fixes

`CloseClient()`'s messaging was unified: `errorCode > 0` still goes through
`perror()` (a real failure — `errno` is meaningful), while `errorCode == 0`
with a non-empty message prints a plain informational line (`Status: ...`)
instead of misusing `perror()` for an expected, successful disconnect (which
previously printed misleading noise like `"...: Success"` or an unrelated
`"...: Invalid argument"` picked up from unrelated library calls between the
event and the print).

## Verification

All of the following were run directly (build + two real client processes +
`strace` where needed), not just reasoned about from reading the code:

- `make clean && make` — clean build, `-Wall -Wextra`, no warnings.
- Single client: connect, send several messages, `EXIT` — clean round trip
  and shutdown, matches Task 1.1 behavior.
- Two concurrent clients, one idle while the other sends — both served
  correctly, no cross-blocking.
- Disconnect, then reconnect the same client and send a real message —
  confirmed working after fix #7 (previously silently dead: "Client
  connected" would print, but the message would never arrive).
- Abrupt disconnect with no `EXIT` (`SIGKILL`) — confirmed clean removal
  (`Status: The client has been disconected`), server CPU flat at 0% over
  three seconds (previously climbed to 60%+), and a subsequent client
  connects and is served normally (previously starved indefinitely).

## Known limitations (tracked separately, not regressions)

- Still echo, not broadcast to other connected clients — Task 1.3.
- No message framing beyond one `recvfrom()` call — the real IRC `\r\n`
  framing with partial-buffer handling is also Task 1.3.
- Client remains a single blocking connection with no multiplexing of its
  own; revisit only if/when it actually needs to do more than one thing at
  once (e.g. receiving a broadcast while the user is mid-typed-input).
- `README.md` still describes the pre-implementation scaffold state —
  Task 1.6, deliberately deferred as before.
