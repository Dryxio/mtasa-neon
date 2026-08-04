CREATE TABLE neon_registered_servers (
    server_id TEXT PRIMARY KEY,
    endpoint TEXT NOT NULL UNIQUE,
    registry_protocol INTEGER NOT NULL,
    http_port INTEGER NOT NULL,
    server_version TEXT NOT NULL,
    name TEXT NOT NULL,
    tagline TEXT NOT NULL,
    description TEXT NOT NULL,
    countries JSONB NOT NULL,
    languages JSONB NOT NULL,
    links JSONB NOT NULL,
    accent TEXT,
    first_seen_at TIMESTAMPTZ NOT NULL,
    last_seen_at TIMESTAMPTZ NOT NULL
);

CREATE INDEX neon_registered_servers_last_seen_at_idx ON neon_registered_servers (last_seen_at);
