import { randomUUID } from "node:crypto";

import { Pool, type PoolClient, type QueryResultRow } from "pg";

import type { AccountSyncInput, BlitzAccount, BlitzDataStore, RoundRecordInput, SeriesRecord } from "./model.js";

interface AccountRow extends QueryResultRow {
    neon_id: string;
    public_id: number;
    discord_id: string;
    last_nickname: string;
    locale: string;
    created_at: Date;
    last_seen_at: Date;
}

interface SeriesRow extends QueryResultRow {
    id: string;
    player_one_neon_id: string;
    player_two_neon_id: string;
    status: string;
    created_at: Date;
}

function toAccount(row: AccountRow): BlitzAccount {
    return {
        neonId: row.neon_id,
        publicId: row.public_id,
        discordId: row.discord_id,
        nickname: row.last_nickname,
        locale: row.locale,
        createdAt: row.created_at,
        lastSeenAt: row.last_seen_at,
    };
}

function toSeries(row: SeriesRow): SeriesRecord {
    return {
        id: row.id,
        playerOneNeonId: row.player_one_neon_id,
        playerTwoNeonId: row.player_two_neon_id,
        createdAt: row.created_at,
    };
}

async function inTransaction<T>(pool: Pool, operation: (client: PoolClient) => Promise<T>): Promise<T> {
    const client = await pool.connect();
    try {
        await client.query("BEGIN");
        const result = await operation(client);
        await client.query("COMMIT");
        return result;
    } catch (error) {
        await client.query("ROLLBACK");
        throw error;
    } finally {
        client.release();
    }
}

export class PostgresBlitzDataStore implements BlitzDataStore {
    readonly #pool: Pool;

    constructor(connectionString: string, ssl: boolean) {
        this.#pool = new Pool({
            connectionString,
            max: 10,
            idleTimeoutMillis: 30_000,
            connectionTimeoutMillis: 5_000,
            ssl: ssl ? { rejectUnauthorized: true } : undefined,
        });
    }

    async ping(): Promise<void> {
        await this.#pool.query("SELECT 1");
    }

    async syncAccount(input: AccountSyncInput, now: Date): Promise<BlitzAccount> {
        return inTransaction(this.#pool, async (client) => {
            const result = await client.query<AccountRow>(
                `INSERT INTO blitz_accounts
                    (neon_id, discord_id, last_nickname, locale, created_at, last_seen_at)
                 VALUES ($1, $2, $3, $4, $5, $5)
                 ON CONFLICT (neon_id) DO UPDATE SET
                    discord_id = EXCLUDED.discord_id,
                    last_nickname = EXCLUDED.last_nickname,
                    locale = blitz_accounts.locale,
                    last_seen_at = EXCLUDED.last_seen_at
                 RETURNING *`,
                [input.neonId, input.discordId, input.nickname, input.locale, now],
            );
            const row = result.rows[0];
            if (!row) throw new Error("Account sync did not return an account");
            await client.query(
                `INSERT INTO blitz_player_stats (neon_id, updated_at)
                 VALUES ($1, $2)
                 ON CONFLICT (neon_id) DO NOTHING`,
                [input.neonId, now],
            );
            return toAccount(row);
        });
    }

    async updateLocale(neonId: string, locale: string, now: Date): Promise<BlitzAccount | null> {
        const result = await this.#pool.query<AccountRow>(
            `UPDATE blitz_accounts
                SET locale = $2, last_seen_at = $3
              WHERE neon_id = $1
              RETURNING *`,
            [neonId, locale, now],
        );
        return result.rows[0] ? toAccount(result.rows[0]) : null;
    }

    async createSeries(requestKey: string, playerOneNeonId: string, playerTwoNeonId: string, now: Date): Promise<SeriesRecord> {
        const result = await this.#pool.query<SeriesRow>(
            `INSERT INTO blitz_series (id, request_key, player_one_neon_id, player_two_neon_id, created_at)
             VALUES ($1, $2, $3, $4, $5)
             ON CONFLICT (request_key) DO UPDATE SET request_key = EXCLUDED.request_key
             RETURNING *`,
            [randomUUID(), requestKey, playerOneNeonId, playerTwoNeonId, now],
        );
        const row = result.rows[0];
        if (!row) throw new Error("Series creation did not return a series");
        return toSeries(row);
    }

    async recordRound(seriesId: string, input: RoundRecordInput, now: Date): Promise<boolean> {
        return inTransaction(this.#pool, async (client) => {
            const seriesResult = await client.query<SeriesRow>("SELECT * FROM blitz_series WHERE id = $1 FOR UPDATE", [seriesId]);
            const series = seriesResult.rows[0];
            if (!series) throw new Error("Series does not exist");
            if (series.status !== "active") throw new Error("Series is no longer active");

            const expectedPlayers = new Set([series.player_one_neon_id, series.player_two_neon_id]);
            if (
                !expectedPlayers.has(input.pursuerNeonId) ||
                !expectedPlayers.has(input.fugitiveNeonId) ||
                !expectedPlayers.has(input.winnerNeonId) ||
                input.pursuerNeonId === input.fugitiveNeonId
            ) {
                throw new Error("Round participants do not match the series");
            }

            const insert = await client.query(
                `INSERT INTO blitz_rounds
                    (id, series_id, round_number, pursuer_neon_id, fugitive_neon_id, winner_neon_id, cause,
                     vehicle_model, spawn_id, duration_ms, started, finished_at)
                 VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12)
                 ON CONFLICT (series_id, round_number) DO NOTHING
                 RETURNING id`,
                [
                    randomUUID(),
                    seriesId,
                    input.roundNumber,
                    input.pursuerNeonId,
                    input.fugitiveNeonId,
                    input.winnerNeonId,
                    input.cause,
                    input.vehicleModel,
                    input.spawnId,
                    input.durationMs,
                    input.started,
                    now,
                ],
            );
            if (insert.rowCount !== 1) return false;

            await this.#incrementStats(client, input.pursuerNeonId, "pursuer", input, now);
            await this.#incrementStats(client, input.fugitiveNeonId, "fugitive", input, now);
            return true;
        });
    }

    async #incrementStats(
        client: PoolClient,
        neonId: string,
        role: "pursuer" | "fugitive",
        input: RoundRecordInput,
        now: Date,
    ): Promise<void> {
        const won = input.winnerNeonId === neonId;
        const lostByDisconnect = !won && input.cause === "disconnect";
        const forfeited = !won && input.cause === "forfeit";
        await client.query(
            `INSERT INTO blitz_player_stats
                (neon_id, rounds_played, wins, losses, pursuer_rounds, pursuer_wins, fugitive_rounds, fugitive_wins,
                 disconnect_losses, forfeits, total_duration_ms, updated_at)
             VALUES ($1, 1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11)
             ON CONFLICT (neon_id) DO UPDATE SET
                rounds_played = blitz_player_stats.rounds_played + 1,
                wins = blitz_player_stats.wins + EXCLUDED.wins,
                losses = blitz_player_stats.losses + EXCLUDED.losses,
                pursuer_rounds = blitz_player_stats.pursuer_rounds + EXCLUDED.pursuer_rounds,
                pursuer_wins = blitz_player_stats.pursuer_wins + EXCLUDED.pursuer_wins,
                fugitive_rounds = blitz_player_stats.fugitive_rounds + EXCLUDED.fugitive_rounds,
                fugitive_wins = blitz_player_stats.fugitive_wins + EXCLUDED.fugitive_wins,
                disconnect_losses = blitz_player_stats.disconnect_losses + EXCLUDED.disconnect_losses,
                forfeits = blitz_player_stats.forfeits + EXCLUDED.forfeits,
                total_duration_ms = blitz_player_stats.total_duration_ms + EXCLUDED.total_duration_ms,
                updated_at = EXCLUDED.updated_at`,
            [
                neonId,
                won ? 1 : 0,
                won ? 0 : 1,
                role === "pursuer" ? 1 : 0,
                role === "pursuer" && won ? 1 : 0,
                role === "fugitive" ? 1 : 0,
                role === "fugitive" && won ? 1 : 0,
                lostByDisconnect ? 1 : 0,
                forfeited ? 1 : 0,
                input.durationMs,
                now,
            ],
        );
    }

    async completeSeries(seriesId: string, reason: string, now: Date): Promise<boolean> {
        const result = await this.#pool.query(
            `UPDATE blitz_series s
                SET status = CASE
                        WHEN EXISTS (SELECT 1 FROM blitz_rounds r WHERE r.series_id = s.id) THEN 'completed'
                        ELSE 'abandoned'
                    END,
                    completion_reason = $2,
                    ended_at = $3
              WHERE s.id = $1 AND s.status = 'active'`,
            [seriesId, reason, now],
        );
        return result.rowCount === 1;
    }

    async close(): Promise<void> {
        await this.#pool.end();
    }
}
