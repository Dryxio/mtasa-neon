CREATE TABLE blitz_accounts (
    neon_id UUID PRIMARY KEY,
    public_id INTEGER GENERATED ALWAYS AS IDENTITY (START WITH 1000) UNIQUE NOT NULL,
    discord_id TEXT UNIQUE NOT NULL CHECK (discord_id ~ '^[0-9]{17,20}$'),
    last_nickname TEXT NOT NULL CHECK (char_length(last_nickname) BETWEEN 1 AND 64),
    locale TEXT NOT NULL CHECK (locale ~ '^[A-Za-z][A-Za-z0-9_-]{0,15}$'),
    created_at TIMESTAMPTZ NOT NULL,
    last_seen_at TIMESTAMPTZ NOT NULL
);

CREATE TABLE blitz_series (
    id UUID PRIMARY KEY,
    request_key TEXT UNIQUE NOT NULL CHECK (char_length(request_key) BETWEEN 16 AND 128),
    player_one_neon_id UUID NOT NULL REFERENCES blitz_accounts(neon_id),
    player_two_neon_id UUID NOT NULL REFERENCES blitz_accounts(neon_id),
    status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'completed', 'abandoned')),
    completion_reason TEXT,
    created_at TIMESTAMPTZ NOT NULL,
    ended_at TIMESTAMPTZ,
    CHECK (player_one_neon_id <> player_two_neon_id)
);

CREATE INDEX blitz_series_player_one_created_idx ON blitz_series (player_one_neon_id, created_at DESC);
CREATE INDEX blitz_series_player_two_created_idx ON blitz_series (player_two_neon_id, created_at DESC);

CREATE TABLE blitz_rounds (
    id UUID PRIMARY KEY,
    series_id UUID NOT NULL REFERENCES blitz_series(id),
    round_number INTEGER NOT NULL CHECK (round_number >= 1),
    pursuer_neon_id UUID NOT NULL REFERENCES blitz_accounts(neon_id),
    fugitive_neon_id UUID NOT NULL REFERENCES blitz_accounts(neon_id),
    winner_neon_id UUID NOT NULL REFERENCES blitz_accounts(neon_id),
    cause TEXT NOT NULL CHECK (cause IN (
        'timeout', 'disconnect', 'vehicle_exit', 'player_death', 'forfeit', 'escape', 'arrest',
        'vehicle_fire', 'vehicle_exploded', 'vehicle_destroyed', 'water', 'rollover'
    )),
    vehicle_model INTEGER NOT NULL CHECK (vehicle_model BETWEEN 400 AND 611),
    spawn_id TEXT NOT NULL CHECK (char_length(spawn_id) BETWEEN 1 AND 64),
    duration_ms INTEGER NOT NULL CHECK (duration_ms BETWEEN 0 AND 3600000),
    started BOOLEAN NOT NULL,
    finished_at TIMESTAMPTZ NOT NULL,
    UNIQUE (series_id, round_number),
    CHECK (pursuer_neon_id <> fugitive_neon_id),
    CHECK (winner_neon_id IN (pursuer_neon_id, fugitive_neon_id))
);

CREATE INDEX blitz_rounds_pursuer_finished_idx ON blitz_rounds (pursuer_neon_id, finished_at DESC);
CREATE INDEX blitz_rounds_fugitive_finished_idx ON blitz_rounds (fugitive_neon_id, finished_at DESC);

CREATE TABLE blitz_player_stats (
    neon_id UUID PRIMARY KEY REFERENCES blitz_accounts(neon_id),
    rounds_played INTEGER NOT NULL DEFAULT 0 CHECK (rounds_played >= 0),
    wins INTEGER NOT NULL DEFAULT 0 CHECK (wins >= 0),
    losses INTEGER NOT NULL DEFAULT 0 CHECK (losses >= 0),
    pursuer_rounds INTEGER NOT NULL DEFAULT 0 CHECK (pursuer_rounds >= 0),
    pursuer_wins INTEGER NOT NULL DEFAULT 0 CHECK (pursuer_wins >= 0),
    fugitive_rounds INTEGER NOT NULL DEFAULT 0 CHECK (fugitive_rounds >= 0),
    fugitive_wins INTEGER NOT NULL DEFAULT 0 CHECK (fugitive_wins >= 0),
    disconnect_losses INTEGER NOT NULL DEFAULT 0 CHECK (disconnect_losses >= 0),
    forfeits INTEGER NOT NULL DEFAULT 0 CHECK (forfeits >= 0),
    total_duration_ms BIGINT NOT NULL DEFAULT 0 CHECK (total_duration_ms >= 0),
    updated_at TIMESTAMPTZ NOT NULL
);

CREATE TABLE blitz_seasons (
    id UUID PRIMARY KEY,
    name TEXT NOT NULL,
    starts_at TIMESTAMPTZ NOT NULL,
    ends_at TIMESTAMPTZ,
    is_ranked BOOLEAN NOT NULL DEFAULT FALSE,
    CHECK (ends_at IS NULL OR ends_at > starts_at)
);

CREATE TABLE blitz_ratings (
    season_id UUID NOT NULL REFERENCES blitz_seasons(id),
    neon_id UUID NOT NULL REFERENCES blitz_accounts(neon_id),
    rating INTEGER NOT NULL DEFAULT 1000,
    games_played INTEGER NOT NULL DEFAULT 0 CHECK (games_played >= 0),
    wins INTEGER NOT NULL DEFAULT 0 CHECK (wins >= 0),
    losses INTEGER NOT NULL DEFAULT 0 CHECK (losses >= 0),
    updated_at TIMESTAMPTZ NOT NULL,
    PRIMARY KEY (season_id, neon_id)
);
