import { randomUUID } from "node:crypto";
import { Pool, type PoolClient, type QueryResultRow } from "pg";

import type {
    DiscordProfile,
    FlowPollResult,
    IdentityStore,
    NeonAccount,
    OAuthFlow,
    RegisteredServer,
    RegisteredServerLink,
} from "./model.js";

interface AccountRow extends QueryResultRow {
    id: string;
    discord_id: string;
    discord_username: string;
    discord_display_name: string | null;
    discord_avatar_hash: string | null;
    created_at: Date;
    updated_at: Date;
}

interface RegisteredServerRow extends QueryResultRow {
    server_id: string;
    endpoint: string;
    registry_protocol: number;
    http_port: number;
    server_version: string;
    name: string;
    tagline: string;
    description: string;
    countries: string[];
    languages: string[];
    links: RegisteredServerLink[];
    accent: string | null;
    first_seen_at: Date;
    last_seen_at: Date;
}

function toAccount(row: AccountRow): NeonAccount {
    return {
        id: row.id,
        discordId: row.discord_id,
        discordUsername: row.discord_username,
        discordDisplayName: row.discord_display_name,
        discordAvatarHash: row.discord_avatar_hash,
        createdAt: row.created_at,
        updatedAt: row.updated_at,
    };
}

function toRegisteredServer(row: RegisteredServerRow): RegisteredServer {
    return {
        id: row.server_id,
        endpoint: row.endpoint,
        registryProtocol: row.registry_protocol,
        httpPort: row.http_port,
        serverVersion: row.server_version,
        name: row.name,
        tagline: row.tagline,
        description: row.description,
        countries: row.countries,
        languages: row.languages,
        links: row.links,
        accent: row.accent,
        firstSeenAt: row.first_seen_at,
        lastSeenAt: row.last_seen_at,
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

export class PostgresIdentityStore implements IdentityStore {
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

    async createFlow(flow: OAuthFlow): Promise<void> {
        await this.#pool.query(
            `INSERT INTO neon_oauth_flows
                (id, poll_token_hash, browser_token_hash, oauth_state_hash, status, account_id, failure_code, created_at, expires_at)
             VALUES ($1, $2, $3, NULL, 'pending', NULL, NULL, $4, $5)`,
            [flow.id, flow.pollTokenHash, flow.browserTokenHash, flow.createdAt, flow.expiresAt],
        );
    }

    async beginFlow(flowId: string, browserTokenHash: Buffer, oauthStateHash: Buffer, now: Date): Promise<boolean> {
        const result = await this.#pool.query(
            `UPDATE neon_oauth_flows
                SET oauth_state_hash = $3
              WHERE id = $1
                AND browser_token_hash = $2
                AND status = 'pending'
                AND oauth_state_hash IS NULL
                AND expires_at > $4`,
            [flowId, browserTokenHash, oauthStateHash, now],
        );
        return result.rowCount === 1;
    }

    async completeFlow(flowId: string, oauthStateHash: Buffer, profile: DiscordProfile, now: Date): Promise<NeonAccount | null> {
        return inTransaction(this.#pool, async (client) => {
            const flowResult = await client.query(
                `SELECT id
                   FROM neon_oauth_flows
                  WHERE id = $1
                    AND oauth_state_hash = $2
                    AND status = 'pending'
                    AND expires_at > $3
                  FOR UPDATE`,
                [flowId, oauthStateHash, now],
            );
            if (flowResult.rowCount !== 1) return null;

            const accountResult = await client.query<AccountRow>(
                `INSERT INTO neon_accounts
                    (id, discord_id, discord_username, discord_display_name, discord_avatar_hash, created_at, updated_at)
                 VALUES ($1, $2, $3, $4, $5, $6, $6)
                 ON CONFLICT (discord_id) DO UPDATE SET
                    discord_username = EXCLUDED.discord_username,
                    discord_display_name = EXCLUDED.discord_display_name,
                    discord_avatar_hash = EXCLUDED.discord_avatar_hash,
                    updated_at = EXCLUDED.updated_at
                 RETURNING *`,
                [randomUUID(), profile.id, profile.username, profile.globalName, profile.avatar, now],
            );
            const accountRow = accountResult.rows[0];
            if (!accountRow) throw new Error("Account upsert did not return a row");

            await client.query(
                `UPDATE neon_oauth_flows
                    SET status = 'authorized', account_id = $2, authorized_at = $3
                  WHERE id = $1`,
                [flowId, accountRow.id, now],
            );
            return toAccount(accountRow);
        });
    }

    async denyFlow(flowId: string, oauthStateHash: Buffer, failureCode: string, now: Date): Promise<boolean> {
        const result = await this.#pool.query(
            `UPDATE neon_oauth_flows
                SET status = 'denied', failure_code = $3, authorized_at = $4
              WHERE id = $1
                AND oauth_state_hash = $2
                AND status = 'pending'
                AND expires_at > $4`,
            [flowId, oauthStateHash, failureCode, now],
        );
        return result.rowCount === 1;
    }

    async inspectFlow(flowId: string, pollTokenHash: Buffer, now: Date): Promise<FlowPollResult | null> {
        const result = await this.#pool.query<AccountRow & { status: string; failure_code: string | null }>(
            `SELECT f.status, f.failure_code,
                    a.id, a.discord_id, a.discord_username, a.discord_display_name,
                    a.discord_avatar_hash, a.created_at, a.updated_at
               FROM neon_oauth_flows f
               LEFT JOIN neon_accounts a ON a.id = f.account_id
              WHERE f.id = $1 AND f.poll_token_hash = $2 AND f.expires_at > $3`,
            [flowId, pollTokenHash, now],
        );
        const row = result.rows[0];
        if (!row) return null;
        if (row.status === "pending") return { status: "pending" };
        if (row.status === "denied") return { status: "denied", failureCode: row.failure_code ?? "oauth_denied" };
        if (row.status === "consumed") return { status: "consumed" };
        if (row.status !== "authorized" || !row.id) return null;
        return { status: "authorized", account: toAccount(row) };
    }

    async consumeFlow(
        flowId: string,
        pollTokenHash: Buffer,
        sessionId: string,
        sessionTokenHash: Buffer,
        sessionExpiresAt: Date,
        now: Date,
    ): Promise<NeonAccount | null> {
        return inTransaction(this.#pool, async (client) => {
            const result = await client.query<AccountRow>(
                `SELECT a.*
                   FROM neon_oauth_flows f
                   JOIN neon_accounts a ON a.id = f.account_id
                  WHERE f.id = $1
                    AND f.poll_token_hash = $2
                    AND f.status = 'authorized'
                    AND f.expires_at > $3
                  FOR UPDATE OF f`,
                [flowId, pollTokenHash, now],
            );
            const accountRow = result.rows[0];
            if (!accountRow) return null;

            await client.query(
                `INSERT INTO neon_sessions (id, account_id, token_hash, created_at, expires_at, revoked_at)
                 VALUES ($1, $2, $3, $4, $5, NULL)`,
                [sessionId, accountRow.id, sessionTokenHash, now, sessionExpiresAt],
            );
            await client.query(
                `UPDATE neon_oauth_flows SET status = 'consumed', consumed_at = $2 WHERE id = $1`,
                [flowId, now],
            );
            return toAccount(accountRow);
        });
    }

    async findAccountBySession(sessionTokenHash: Buffer, now: Date): Promise<NeonAccount | null> {
        const result = await this.#pool.query<AccountRow>(
            `SELECT a.*
               FROM neon_sessions s
               JOIN neon_accounts a ON a.id = s.account_id
              WHERE s.token_hash = $1 AND s.revoked_at IS NULL AND s.expires_at > $2`,
            [sessionTokenHash, now],
        );
        return result.rows[0] ? toAccount(result.rows[0]) : null;
    }

    async upsertRegisteredServer(server: RegisteredServer): Promise<void> {
        await this.#pool.query(
            `INSERT INTO neon_registered_servers
                (server_id, endpoint, registry_protocol, http_port, server_version, name, tagline, description,
                 countries, languages, links, accent, first_seen_at, last_seen_at)
             VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9::jsonb, $10::jsonb, $11::jsonb, $12, $13, $14)
             ON CONFLICT (server_id) DO UPDATE SET
                endpoint = EXCLUDED.endpoint,
                registry_protocol = EXCLUDED.registry_protocol,
                http_port = EXCLUDED.http_port,
                server_version = EXCLUDED.server_version,
                name = EXCLUDED.name,
                tagline = EXCLUDED.tagline,
                description = EXCLUDED.description,
                countries = EXCLUDED.countries,
                languages = EXCLUDED.languages,
                links = EXCLUDED.links,
                accent = EXCLUDED.accent,
                last_seen_at = EXCLUDED.last_seen_at`,
            [
                server.id,
                server.endpoint,
                server.registryProtocol,
                server.httpPort,
                server.serverVersion,
                server.name,
                server.tagline,
                server.description,
                JSON.stringify(server.countries),
                JSON.stringify(server.languages),
                JSON.stringify(server.links),
                server.accent,
                server.firstSeenAt,
                server.lastSeenAt,
            ],
        );
    }

    async listRegisteredServers(activeSince: Date): Promise<RegisteredServer[]> {
        const result = await this.#pool.query<RegisteredServerRow>(
            `SELECT server_id, endpoint, registry_protocol, http_port, server_version, name, tagline, description,
                    countries, languages, links, accent, first_seen_at, last_seen_at
               FROM neon_registered_servers
              WHERE last_seen_at >= $1
              ORDER BY server_id`,
            [activeSince],
        );
        return result.rows.map(toRegisteredServer);
    }

    async close(): Promise<void> {
        await this.#pool.end();
    }
}
