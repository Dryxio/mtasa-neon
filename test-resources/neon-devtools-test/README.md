# Neon DevTools test resource

This resource produces genuine MTA diagnostics on both sides: Lua runtime
errors from timers and local/remote event handlers, an asynchronous SQLite
callback, invalid native argument warnings, dynamic compilation failures,
interleaved duplicate bursts, custom colors, and long UTF-8 messages.

Connect first, use `debugscript 4`, then restart this resource so the server
suite is emitted while the client is subscribed. The complete client and
server suites run on resource start. Repeat all or individual scenarios with:

- `devtest-server [all|baseline|nilindex|warning|compile|event|database|interleaved]`
- `devtest-client [all|baseline|nilcall|warning|compile|event|remote]`

The default suites also exercise the new diagnostic context field. Timer
failures should show `timer callback`; local and remote handlers should show
their actual event name. The database callback intentionally remains a useful
source/context edge case rather than simulating metadata in its message.

Open or close the console with `Ctrl+Shift+I` or `devtools`. Escape closes it.
