CREATE TABLE neon_server_assets (
    hash TEXT PRIMARY KEY CHECK (hash ~ '^[0-9a-f]{64}$'),
    mime_type TEXT NOT NULL CHECK (mime_type IN ('image/png', 'image/jpeg', 'image/webp')),
    width INTEGER NOT NULL CHECK (width BETWEEN 16 AND 4096),
    height INTEGER NOT NULL CHECK (height BETWEEN 16 AND 4096),
    bytes BYTEA NOT NULL CHECK (octet_length(bytes) BETWEEN 1 AND 2097152),
    created_at TIMESTAMPTZ NOT NULL,
    CHECK (width::BIGINT * height::BIGINT <= 16777216)
);

CREATE TABLE neon_server_asset_sources (
    source_url TEXT PRIMARY KEY,
    asset_hash TEXT NOT NULL REFERENCES neon_server_assets(hash),
    fetched_at TIMESTAMPTZ NOT NULL
);

ALTER TABLE neon_registered_servers
    ADD COLUMN logo_asset_hash TEXT REFERENCES neon_server_assets(hash),
    ADD COLUMN banner_asset_hash TEXT REFERENCES neon_server_assets(hash);
