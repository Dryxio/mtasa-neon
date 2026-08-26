CREATE TABLE neon_server_identities (
    server_id TEXT PRIMARY KEY CHECK (server_id ~ '^nsrv_[A-Za-z0-9_-]{43}$'),
    public_key TEXT NOT NULL UNIQUE CHECK (public_key ~ '^[A-Za-z0-9_-]{43}$'),
    status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'suspended')),
    created_at TIMESTAMPTZ NOT NULL,
    last_seen_at TIMESTAMPTZ NOT NULL
);

CREATE TABLE neon_server_endpoint_leases (
    server_id TEXT PRIMARY KEY REFERENCES neon_server_identities(server_id) ON DELETE CASCADE,
    endpoint TEXT NOT NULL UNIQUE,
    verified_at TIMESTAMPTZ NOT NULL,
    expires_at TIMESTAMPTZ NOT NULL,
    auth_enabled BOOLEAN NOT NULL,
    published BOOLEAN NOT NULL
);

CREATE INDEX neon_server_endpoint_leases_expires_at_idx
    ON neon_server_endpoint_leases (expires_at);

CREATE TABLE neon_server_heartbeat_nonces (
    server_id TEXT NOT NULL REFERENCES neon_server_identities(server_id) ON DELETE CASCADE,
    nonce_hash BYTEA NOT NULL,
    expires_at TIMESTAMPTZ NOT NULL,
    PRIMARY KEY (server_id, nonce_hash)
);

CREATE INDEX neon_server_heartbeat_nonces_expires_at_idx
    ON neon_server_heartbeat_nonces (expires_at);

-- Endpoint-level blocks remain effective even if an abusive operator deletes
-- their key and returns with a freshly generated server identity.
CREATE TABLE neon_server_endpoint_blocks (
    endpoint TEXT PRIMARY KEY,
    reason TEXT NOT NULL,
    blocked_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    expires_at TIMESTAMPTZ
);
