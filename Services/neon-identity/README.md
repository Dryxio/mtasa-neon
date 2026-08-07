# Neon Identity V1

Neon Identity is the platform-owned Discord account link for every Neon server.
It is not a Blitz account system: Blitz can consume the same signed identity as
any other registered Neon server and can add a guild/role policy of its own.

This directory contains the minimal V1 backend contract. It deliberately keeps
the Discord client secret, bot token, OAuth tokens, session hashes, and Ed25519
private key outside the distributed MTA client and server binaries.

## V1 flow

1. The Neon client calls `POST /v1/auth/discord/start`.
2. It stores `flow_id` and `poll_token`, then opens `authorization_url` in the
   system browser. The one-time browser capability is in the URL fragment, so
   it is not sent in HTTP access logs or `Referer` headers.
3. The bridge page posts the capability to the backend. The backend atomically
   starts the flow, sets an encrypted `HttpOnly` OAuth cookie, and redirects to
   Discord with `scope=identify`, a random `state`, and PKCE S256.
4. The callback checks the encrypted cookie, expiry, `state`, and persisted
   state hash before linking the Discord profile. Discord access/refresh tokens
   are never persisted.
5. The client polls `POST /v1/auth/discord/poll`. The successful response
   reveals the opaque Neon session token exactly once.
6. Before joining a server, the client calls `POST /v1/tickets` with that
   session. The backend signs only when `server_id` and endpoint exist in its
   Identity security allowlist. The returned Ed25519 JWT is audience-bound to
   that ID and lives for 30–60 seconds (45 by default).

The `poll_token`, browser capability, and session token are random 256-bit
values. Only SHA-256 hashes of poll and session tokens are persisted. SHA-256 is
appropriate here because these are high-entropy generated secrets, not user
passwords.

Discord documents `identify` as access to the basic user profile and documents
the server-side authorization-code exchange and PKCE parameters in its
[OAuth2 documentation](https://docs.discord.com/developers/platform/oauth2-and-permissions)
and [account-linking guide](https://docs.discord.com/developers/discord-social-sdk/development-guides/account-linking-with-discord).

## HTTP contract

Start a link:

```http
POST /v1/auth/discord/start
```

```json
{
  "flow_id": "f2d4f89f-8e84-4019-ae52-a8d940e22035",
  "poll_token": "opaque-one-time-poll-secret",
  "authorization_url": "https://identity.example.com/v1/auth/discord/authorize#...",
  "expires_at": "2026-08-03T12:05:00.000Z"
}
```

Poll while the browser flow is pending:

```http
POST /v1/auth/discord/poll
Content-Type: application/json

{"flow_id":"...","poll_token":"..."}
```

```json
{"status":"pending"}
```

The first successful poll returns the session; subsequent polls return `409`:

```json
{
  "session_token": "ns_v1_...",
  "session_expires_at": "2026-09-02T12:00:00.000Z",
  "account": {
    "id": "Neon account UUID",
    "discord_id": "Discord snowflake",
    "discord_username": "name",
    "discord_display_name": "Display Name",
    "discord_avatar_hash": "hash"
  }
}
```

Issue a game ticket:

```http
POST /v1/tickets
Authorization: Bearer ns_v1_...
Content-Type: application/json

{"server_id":"blitz-production","server_endpoint":"203.0.113.10:22003"}
```

```json
{"ticket":"eyJ...","expires_at":"2026-08-03T12:00:45.000Z"}
```

The session is authenticated before registry membership is disclosed. A valid
session requesting an unregistered server/endpoint pair receives:

```http
HTTP/1.1 403 Forbidden
```

```json
{"error":"server_endpoint_not_allowed"}
```

Discovery and keys are public:

- `GET /.well-known/neon-identity`
- `GET /.well-known/neon-server-registry`
- `GET /.well-known/jwks.json`
- `GET /healthz`

The identity discovery document includes `server_registry_uri`, which points to
the public catalogue consumed by the Neon server browser. The catalogue is a
versioned presentation document rather than an authorization source:

```json
{
  "schema_version": 1,
  "servers": [
    {
      "id": "blitz-production",
      "endpoints": ["213.32.90.138:22004"],
      "name": "MTA:SA Neon — Blitz",
      "tagline": "One rival. One city. No route.",
      "description": "Open-world 1v1 vehicle pursuits: escape your rival or stop them before time runs out.",
      "countries": ["BR", "GB", "FR", "RU", "PL", "ID", "ES"],
      "languages": ["Portuguese (Brazil)", "English", "French", "Russian", "Polish", "Indonesian", "Spanish"],
      "links": [],
      "logo_url": "https://identity.example.com/v1/server-registry/assets/0123456789abcdef...",
      "banner_url": "https://identity.example.com/v1/server-registry/assets/fedcba9876543210..."
    }
  ]
}
```

The catalogue is populated by public Neon server heartbeats. A heartbeat is
bound to the TCP source address inserted by the trusted reverse proxy, then the
service probes the corresponding MTA ASE endpoint (`game_port + 123`) and
requires the current `NeonRegistryProtocol` ASE rule before it publishes
anything. It expires after five minutes without renewal. This makes
all public servers running a heartbeat-capable Neon build discoverable without
manual registration while preventing a server from advertising another public
IP. It is service verification, not binary attestation: a determined operator
can modify an open-source build, so abuse still requires moderation controls.

Names and presentation metadata come from `mtaserver.conf`. Live mode, map,
player count, password state, version, and ping remain sourced from ASE. Public
catalogue responses allow cross-origin reads and use
`Cache-Control: public, max-age=30, stale-if-error=300`.

Optional `logo_url` and `banner_url` heartbeat fields are fetched by Identity,
not exposed directly to players. Every hop must resolve exclusively to public
IP addresses and use HTTPS on the standard port. Redirects, timeouts, response
size, MIME magic, and dimensions are validated. PNG, JPEG, and WebP files up to
2 MiB and 4096×4096 (at most 16 megapixels) are accepted. Valid assets are
content-addressed in PostgreSQL and published through immutable, same-origin
HTTPS URLs. A failed refresh retains an older cached copy when possible and
never removes an otherwise healthy server from discovery.

## Ticket validation contract

The compact JWT header is `alg=EdDSA`, `typ=JWT`, plus the published `kid`.
Claims are:

- `iss`: configured Neon Identity issuer;
- `aud`: exact requested Neon `server_id`;
- `sub`: global Neon account UUID;
- `discord_id`: explicit Discord ID exposure for V1;
- `server_endpoint`: actual IPv4 endpoint joined by the native client;
- `iat`, `nbf`, `exp`: short validity window;
- `jti`: unique ticket ID.

A game server must reject every other algorithm, unknown `kid`, wrong issuer,
wrong audience, missing claim, or expired/not-yet-valid ticket. It must also
cache accepted `jti` values until `exp` and reject replays. Offline signature
validation alone cannot make a JWT one-time-use.

V1 intentionally exposes `discord_id` because this is the agreed integration
contract. A future multi-tenant release should gate that claim behind server
registration and player consent, or replace it with a per-organization
pseudonymous identifier.

## MTA Neon server integration

The custom Neon client reads the identity policy and stable `server_id` from
the initial mod-name handshake. Ordinary MTA servers advertise no policy and
never cause an identity ticket request. The client obtains the endpoint from
its real network connection rather than trusting server metadata. Both the
`server_id` and actual `IPv4:port` pair must appear in
`NEON_SERVER_REGISTRY`; this prevents an arbitrary MTA server from claiming a
registered Neon audience and capturing a signed-in player's identity ticket.

Configure a Neon server with the public `x` value from the active Ed25519 JWK:

```xml
<neon_auth>required</neon_auth>
<neon_auth_server_id>blitz-production</neon_auth_server_id>
<neon_auth_issuer>https://identity.example.com</neon_auth_issuer>
<neon_auth_key_id>neon-identity-2026-01</neon_auth_key_id>
<neon_auth_public_key>base64url-JWK-x-value</neon_auth_public_key>
```

`neon_auth` accepts `disabled`, `optional`, or `required`. Ticket validation
happens before the server allocates a player element. Once accepted, resources
can consume the platform identity through:

```lua
isPlayerNeonAuthenticated(player) -- boolean
getPlayerNeonID(player)            -- global Neon UUID or false
getPlayerDiscordID(player)         -- Discord snowflake string or false
```

Equivalent player methods are `player:isNeonAuthenticated()`,
`player:getNeonID()`, and `player:getDiscordID()`.

### Automatic public discovery

Public discovery is enabled by default when ordinary MTA ASE publication is
enabled. An administrator can opt out or provide richer browser metadata:

```xml
<neon_registry>1</neon_registry>
<neon_registry_tagline>One rival. One city. No route.</neon_registry_tagline>
<neon_registry_description>Open-world vehicle pursuits.</neon_registry_description>
<neon_registry_countries>GB, ES, BR, FR</neon_registry_countries>
<neon_registry_languages>English, Spanish, Portuguese (Brazil), French</neon_registry_languages>
<neon_registry_website>https://mta-neon.com</neon_registry_website>
<neon_registry_discord>https://discord.gg/example</neon_registry_discord>
<neon_registry_accent>#9EBCE5</neon_registry_accent>
<neon_registry_logo>https://cdn.example.com/neon/logo.webp</neon_registry_logo>
<neon_registry_banner>https://cdn.example.com/neon/banner.webp</neon_registry_banner>
```

`countries` contains comma-separated ISO 3166-1 alpha-2 codes. Links must use
HTTPS. Artwork is optional and its source URL is replaced by the cached
Identity URL before publication. Empty optional fields receive conservative service defaults. The
official heartbeat URL is compiled as the default; a private deployment can
override it with `<neon_registry_url>`.

## Local setup

Requirements: Node.js 22+ and PostgreSQL 15+.

```sh
cd Services/neon-identity
npm install
npm run generate-key
cp .env.example .env
```

Create a Discord application and register this exact redirect URL:

```text
https://identity.example.com/v1/auth/discord/callback
```

Fill `.env` with the application ID, client secret, generated private JWK,
generated cookie key, PostgreSQL URL, public origin, issuer, and at least one
registered server endpoint. Never commit `.env`. Then apply migrations and start the
service:

```sh
set -a
. ./.env
set +a
npm run migrate
npm run dev
```

Production requires HTTPS at the public origin. Terminate TLS at a trusted
reverse proxy, forward only the expected host, set `TRUST_PROXY=true` only when
requests can arrive exclusively through that proxy, and keep request query,
cookie, and authorization headers out of proxy logs. The service itself omits
request URLs from structured logs and redacts credential-bearing headers.

## Identity security allowlist

`NEON_SERVER_REGISTRY` remains a required JSON object mapping Identity-enabled
official server IDs to canonical endpoints:

```dotenv
NEON_SERVER_REGISTRY='{"blitz-production":["203.0.113.10:22003","203.0.113.11:22003"],"neon-staging":["127.0.0.1:22003"]}'
```

Every ID must match `[A-Za-z0-9][A-Za-z0-9._:-]{0,127}` and every endpoint must
be canonical `IPv4:port` without leading-zero aliases. Empty or invalid
allowlists fail service startup. Matching heartbeats retain their official ID;
other verified public Neon servers receive a stable endpoint-derived community
ID and appear in discovery, but cannot request Discord Identity tickets.
`POST /v1/tickets` authenticates the session, then checks the exact official
pair before calling the signer. Removal stops new tickets immediately; issued
tickets remain valid only until their short `exp`.

## Optional Discord guild/role policy

Global Neon account linking allows every valid Discord account by default.
The `DiscordIdentityPolicy` interface is the hook point for server- or
deployment-specific admission rules. The included policy can require a guild,
completed membership screening, and one or more roles:

```dotenv
DISCORD_REQUIRED_GUILD_ID=123456789012345678
DISCORD_REQUIRED_ROLE_IDS=234567890123456789,345678901234567890
DISCORD_BOT_TOKEN=backend-only-bot-token
DISCORD_REQUIRE_COMPLETED_SCREENING=true
```

The bot must belong to that guild. Discord's
[`Get Guild Member` endpoint](https://docs.discord.com/developers/resources/guild#get-guild-member)
is called with the backend-only bot token. A missing member, pending screening,
or missing configured role denies the flow. Leave these variables blank for the
platform-wide identity service and evaluate Blitz policy at Blitz ticket time.

## Operations and current V1 boundaries

- Run `npm test` and `npm run build` before deployment.
- Rotate signing keys by publishing old and new public keys concurrently until
  every ticket signed by the old key has expired. The current minimal service
  publishes one active key, so overlap needs to be added before rotation.
- Revoke a session by setting `neon_sessions.revoked_at`; a player-facing
  logout/revocation endpoint is not part of this first native-client checkpoint.
- Periodically delete expired flows and sessions after retaining any required
  security audit interval.
- The heartbeat-backed public registry is separate from the static Identity
  allowlist. There is not yet a server-owner portal, moderation console, or
  per-server policy database.
- Public server-browser metadata is validated by `src/server-catalog.ts` and
  persisted from verified heartbeats. Only endpoints in the Identity allowlist
  may receive signed Discord Identity tickets.
- Store the signing key and Discord credentials in a secrets manager in
  production, not an environment file on disk.
