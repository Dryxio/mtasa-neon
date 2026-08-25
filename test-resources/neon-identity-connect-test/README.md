# Neon Identity connection test

This server-only resource checks the trailing Neon Identity arguments exposed
by `onPlayerConnect` and confirms that they match the established player
getters in `onPlayerJoin`. It logs only value types and comparison results, not
the account identifiers themselves.

## Run

Deploy the resource separately, start it, and connect with the policy under
test. A successful authenticated connection produces both of these lines:

```text
[Neon Identity connect test] PASS onPlayerConnect received neon=string discord=string
[Neon Identity connect test] PASS event arguments match the player getters
```

With optional or disabled authentication, missing identities appear as
`boolean` because the public value is `false`.

To exercise the cancellable pre-join path, run this in the server console or as
an in-game command, then attempt one connection:

```text
neonconnecttest reject-next
```

Use `/neonconnecttest reject-next` in game. Wait for the confirmation that the
next connection will be rejected before reconnecting.

The client must receive `Neon Identity connection test rejection.` and the
server must report that the rejected connection did not reach `onPlayerJoin`.
