# Blitz data service

This loopback-only service owns durable Blitz accounts, match history, aggregate
statistics, and the schema reserved for future ranked seasons. MTA authenticates
players through Neon Identity, then calls this service with the verified Neon and
Discord IDs. MTA serials are deliberately not account identifiers.

## Runtime boundary

- PostgreSQL is held behind this process rather than exposed to Lua resources.
- The HTTP listener must remain on `127.0.0.1` in production.
- Every mutation requires the private bearer token shared with the server-side
  Blitz resource setting.
- A public player ID begins at `#1000` and remains stable when the MTA nickname
  changes.
- One series contains the initial round and every mutually accepted rematch.
- Round insertion is idempotent on `(series_id, round_number)`, so retrying a
  completed result cannot increment statistics twice.

## Development

```sh
npm install
npm test
npm run build
```

Copy `.env.example`, create the dedicated PostgreSQL database and role, then run:

```sh
npm run migrate
npm start
```

The MTA resource reads `@dataApiUrl` and `@dataApiToken` from its private
resource settings. Production startup must fail closed when either is missing.
