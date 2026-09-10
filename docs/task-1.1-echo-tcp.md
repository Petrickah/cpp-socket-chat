# Task 1.1 — Setup + echo TCP

Real socket code replacing the placeholder stubs, implemented and debugged in
a single interactive session. Bidirectional TCP echo between one server and
one client on localhost — blocking I/O, single client at a time, no
`select()`/`poll()` yet (that's Task 1.2).

## What was built

Both `server/main.cpp` and `client/main.cpp` moved from scaffold stubs to a
small class wrapping the POSIX socket calls:

- **`TCPClient`** (`client/main.cpp`): `socket()` in the constructor,
  `Connect()` (`connect()`), `Send()` (`send()` + a `recv()` loop that
  accumulates until the full echo is back), `Close()`.
- **`TCPServer`** (`server/main.cpp`): `socket()` in the constructor,
  `Bind()` (`setsockopt(SO_REUSEADDR)` + `bind()` + `listen()`),
  `ConnectClient()` (`accept()`), a private `HandleClient()` running the
  per-connection echo loop, and two separate close paths — `CloseServer()`
  for the listening socket, `CloseClient()` for one client's socket.

Protocol: the client reads a line via `std::getline`, sends it, and blocks
until the full echo is received back before prompting again. Typing `EXIT`
sends the literal string; the server echoes it back once (so the client sees
its own "EXIT" confirmed) and then closes that client's connection. The
client recognizes its own echoed "EXIT" and closes on that signal rather than
on a local check of what it typed.

## Bugs found and fixed during the session

All caught through code review + reasoning about TCP's byte-stream semantics
(no message framing), not through a debugger — `gdb`/sanitizer work is
scoped to Task 1.4, once there's more logic worth sanitizing.

1. **Client never reset its received-byte counter between messages** — a
   member variable accumulated across calls instead of being reset per
   `Send()`, so a short message following a longer one could skip the
   receive loop entirely.
2. **`Close()` closed the wrong socket** — the server originally had one
   `Close()` that always closed the *listening* socket, but was called from
   per-client error paths. Any single client error killed the server's
   ability to accept future clients. Split into `CloseServer()` (listening
   socket, fatal setup errors only) and `CloseClient()` (per-connection
   socket, always returns `0` so a client's own error doesn't propagate and
   stop the server's accept loop).
3. **Received-length validation bugs, twice** — first on an original `HELLO`
   handshake (since dropped), then reintroduced on the `EXIT` check: using
   the just-received byte count as the `strncmp()` compare length is
   trivially satisfied by a partial/fragmented `recv()` (TCP doesn't
   preserve message boundaries). Also hit the degenerate form of this bug on
   the client side — comparing with a stale `0`-length counter (read before
   it was updated for the current `recv()`) meant `strncmp(..., 0)` matched
   *any* echo, unconditionally, since a zero-length compare always returns
   equal.
4. **Missing `SO_REUSEADDR`** — restarting the server shortly after a
   previous run failed to bind (`EADDRINUSE` from the just-closed socket's
   `TIME_WAIT`).
5. **Unvalidated `inet_addr()`** — an invalid IP argument silently became
   `255.255.255.255` instead of a clear usage error.
6. **`std::cin >> inputMessage` truncated messages at the first space** —
   `operator>>` reads one whitespace-delimited token, not a full line.
   Switched to `std::getline(std::cin, inputMessage)`.
7. **Server-side receive/print ordering** — the received message was
   originally echoed back and only printed to the server's own console after
   blocking on the *next* `recv()`, so the current message's content never
   showed up until another round-trip happened (and the very first message
   never printed at all). Fixed by printing immediately after each `recv()`,
   before deciding what to do with the data.
8. **`EXIT` never reaching the server** — the client's loop condition
   (`while (inputMessage != "EXIT" && ...)`) checked the freshly-typed
   string before calling `Send()`, so typing `EXIT` exited the loop locally
   without ever transmitting it. The server was left blocked in `recv()`
   until the client process terminated and the socket closed out from under
   it.
9. **Double `close()` on the client socket** — the `EXIT` branch originally
   called `Close()` internally (closing the fd) *and* returned a value that
   let control reach `main()`'s own closing call, closing the same fd twice.
   Resolved by having the `EXIT` branch only signal (return non-zero)
   without closing anything itself — the socket is now closed exactly once,
   in `main()`, after the loop exits.

## Verification

- `make clean && make` — builds both binaries with `-Wall -Wextra`, no
  warnings.
- Manual two-terminal session: `./server/server <port>` and
  `./client/client 127.0.0.1 <port>`, exchanged several multi-word messages
  (full lines, not truncated), confirmed each was echoed back and printed on
  both sides, then typed `EXIT` and confirmed clean shutdown on both ends
  (no hang, no double-close, no server crash) and that the server's accept
  loop survives a client disconnect and stays up for the next connection.

## Known limitations (tracked separately, not regressions)

- Single client at a time, blocking I/O — `select()`/multi-client is Task
  1.2.
- No message framing beyond "whatever one `recv()` call returns" — fine for
  an echo test, not yet the real IRC `\r\n` framing (Task 1.3).
- `README.md` still describes the pre-implementation scaffold state;
  updating it is Task 1.6, deliberately deferred rather than done here.
