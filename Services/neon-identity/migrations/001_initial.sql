CREATE TABLE neon_accounts (
    id UUID PRIMARY KEY,
    discord_id TEXT NOT NULL UNIQUE,
    discord_username TEXT NOT NULL,
    discord_display_name TEXT,
    discord_avatar_hash TEXT,
    created_at TIMESTAMPTZ NOT NULL,
    updated_at TIMESTAMPTZ NOT NULL
);

CREATE TABLE neon_oauth_flows (
    id UUID PRIMARY KEY,
    poll_token_hash BYTEA NOT NULL,
    browser_token_hash BYTEA NOT NULL,
    oauth_state_hash BYTEA,
    status TEXT NOT NULL CHECK (status IN ('pending', 'authorized', 'consumed', 'denied')),
    account_id UUID REFERENCES neon_accounts(id),
    failure_code TEXT,
    created_at TIMESTAMPTZ NOT NULL,
    expires_at TIMESTAMPTZ NOT NULL,
    authorized_at TIMESTAMPTZ,
    consumed_at TIMESTAMPTZ
);

CREATE INDEX neon_oauth_flows_expires_at_idx ON neon_oauth_flows (expires_at);

CREATE TABLE neon_sessions (
    id UUID PRIMARY KEY,
    account_id UUID NOT NULL REFERENCES neon_accounts(id) ON DELETE CASCADE,
    token_hash BYTEA NOT NULL UNIQUE,
    created_at TIMESTAMPTZ NOT NULL,
    expires_at TIMESTAMPTZ NOT NULL,
    revoked_at TIMESTAMPTZ
);

CREATE INDEX neon_sessions_account_id_idx ON neon_sessions (account_id);
CREATE INDEX neon_sessions_expires_at_idx ON neon_sessions (expires_at);
