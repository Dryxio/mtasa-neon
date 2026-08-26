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
    ServerIdentityLeaseClaim,
    ServerIdentityLeaseClaimResult,
    ServerAsset,
    ServerAssetSource,
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
    logo_asset_hash: string | null;
    banner_asset_hash: string | null;
    first_seen_at: Date;
    last_seen_at: Date;
}

interface ServerAssetRow extends QueryResultRow {
    hash: string;
    mime_type: ServerAsset["mimeType"];
    width: number;
    height: number;
    bytes: Buffer;
    created_at: Date;
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
        logoAssetHash: row.logo_asset_hash,
        bannerAssetHash: row.banner_asset_hash,
        firstSeenAt: row.first_seen_at,
        lastSeenAt: row.last_seen_at,
    };
}

function toServerAsset(row: ServerAssetRow): ServerAsset {
    return {
        hash: row.hash,
        mimeType: row.mime_type,
        width: row.width,
        height: row.height,
        bytes: row.bytes,
        createdAt: row.created_at,
    };
}

async function inTransaction<T>(pool: Pool, operation: (client: PoolClient) => Promise<T>): Promise<T> {
    for (let attempt = 0; ; attempt += 1) {
        const client = await pool.connect();
        try {
            await client.query("BEGIN");
            const result = await operation(client);
            await client.query("COMMIT");
            return result;
        } catch (error) {
            await client.query("ROLLBACK");
            const code = (error as { code?: string }).code;
            if (attempt < 2 && (code === "40P01" || code === "40001")) continue;
            throw error;
        } finally {
            client.release();
        }
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

    async upsertRegisteredServer(server: RegisteredServer, replaceEndpoint = false): Promise<void> {
        await inTransaction(this.#pool, async (client) => {
            if (replaceEndpoint) {
                await client.query(
                    `DELETE FROM neon_registered_servers WHERE endpoint = $1 AND server_id <> $2`,
                    [server.endpoint, server.id],
                );
            }
            await client.query(
            `INSERT INTO neon_registered_servers
                (server_id, endpoint, registry_protocol, http_port, server_version, name, tagline, description,
                 countries, languages, links, accent, logo_asset_hash, banner_asset_hash, first_seen_at, last_seen_at)
             VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9::jsonb, $10::jsonb, $11::jsonb, $12, $13, $14, $15, $16)
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
                logo_asset_hash = EXCLUDED.logo_asset_hash,
                banner_asset_hash = EXCLUDED.banner_asset_hash,
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
                server.logoAssetHash,
                server.bannerAssetHash,
                server.firstSeenAt,
                server.lastSeenAt,
                ],
            );
        });
    }

    async removeRegisteredServer(serverId: string): Promise<void> {
        await this.#pool.query(`DELETE FROM neon_registered_servers WHERE server_id = $1`, [serverId]);
    }

    async listRegisteredServers(activeSince: Date, now: Date): Promise<RegisteredServer[]> {
        const result = await this.#pool.query<RegisteredServerRow>(
            `SELECT s.server_id, s.endpoint, s.registry_protocol, s.http_port, s.server_version, s.name, s.tagline, s.description,
                    s.countries, s.languages, s.links, s.accent, s.logo_asset_hash, s.banner_asset_hash, s.first_seen_at, s.last_seen_at
               FROM neon_registered_servers s
               LEFT JOIN neon_server_endpoint_leases l ON l.server_id = s.server_id AND l.endpoint = s.endpoint
               LEFT JOIN neon_server_identities i ON i.server_id = l.server_id
              WHERE s.last_seen_at >= $1
                AND ((s.registry_protocol = 1 AND NOT EXISTS (
                        SELECT 1 FROM neon_server_endpoint_leases reserved
                         WHERE reserved.endpoint = s.endpoint AND reserved.expires_at > $2
                     )) OR (l.published AND l.expires_at > $2 AND i.status = 'active'))
              ORDER BY s.server_id`,
            [activeSince, now],
        );
        return result.rows.map(toRegisteredServer);
    }

    async claimServerIdentityLease(claim: ServerIdentityLeaseClaim): Promise<ServerIdentityLeaseClaimResult> {
        return inTransaction(this.#pool, async (client) => {
            // An absent row cannot be protected with FOR UPDATE. Serialize
            // first registration by both identity and endpoint so concurrent
            // claims cannot bypass the lease checks and surface as a unique
            // constraint error (or move one identity twice).
            await client.query(
                `SELECT pg_advisory_xact_lock(hashtextextended('neon-identity:' || $1, 0))`,
                [claim.serverId],
            );
            await client.query(
                `SELECT pg_advisory_xact_lock(hashtextextended('neon-endpoint:' || $1, 0))`,
                [claim.endpoint],
            );
            await client.query(`DELETE FROM neon_server_heartbeat_nonces WHERE expires_at <= $1`, [claim.verifiedAt]);

            const blocked = await client.query(
                `SELECT 1 FROM neon_server_endpoint_blocks
                  WHERE endpoint = $1 AND (expires_at IS NULL OR expires_at > $2)`,
                [claim.endpoint, claim.verifiedAt],
            );
            if (blocked.rowCount) return "endpoint_blocked";

            const identity = await client.query<{ public_key: string; status: string }>(
                `SELECT public_key, status FROM neon_server_identities WHERE server_id = $1 FOR UPDATE`,
                [claim.serverId],
            );
            const identityRow = identity.rows[0];
            if (identityRow && identityRow.public_key !== claim.publicKey) return "identity_in_use";
            if (identityRow?.status === "suspended") return "identity_suspended";

            const endpointOwner = await client.query<{ server_id: string; expires_at: Date; status: string }>(
                `SELECT l.server_id, l.expires_at, i.status
                   FROM neon_server_endpoint_leases l
                   JOIN neon_server_identities i ON i.server_id = l.server_id
                  WHERE l.endpoint = $1
                  FOR UPDATE OF l, i`,
                [claim.endpoint],
            );
            const endpointRow = endpointOwner.rows[0];
            if (endpointRow && endpointRow.server_id !== claim.serverId && endpointRow.status === "suspended") return "endpoint_blocked";
            if (endpointRow && endpointRow.server_id !== claim.serverId && endpointRow.expires_at > claim.verifiedAt) {
                return "endpoint_in_use";
            }

            const currentLease = await client.query<{ endpoint: string; expires_at: Date }>(
                `SELECT endpoint, expires_at FROM neon_server_endpoint_leases WHERE server_id = $1 FOR UPDATE`,
                [claim.serverId],
            );
            const currentLeaseRow = currentLease.rows[0];
            if (currentLeaseRow && currentLeaseRow.endpoint !== claim.endpoint && currentLeaseRow.expires_at > claim.verifiedAt) {
                return "identity_in_use";
            }

            await client.query(
                `INSERT INTO neon_server_identities (server_id, public_key, status, created_at, last_seen_at)
                 VALUES ($1, $2, 'active', $3, $3)
                 ON CONFLICT (server_id) DO UPDATE SET last_seen_at = EXCLUDED.last_seen_at`,
                [claim.serverId, claim.publicKey, claim.verifiedAt],
            );

            const nonce = await client.query(
                `INSERT INTO neon_server_heartbeat_nonces (server_id, nonce_hash, expires_at)
                 VALUES ($1, $2, $3) ON CONFLICT DO NOTHING`,
                [claim.serverId, claim.nonceHash, claim.expiresAt],
            );
            if (nonce.rowCount !== 1) return "replay";

            if (endpointRow && endpointRow.server_id !== claim.serverId) {
                await client.query(`DELETE FROM neon_server_endpoint_leases WHERE server_id = $1`, [endpointRow.server_id]);
            }
            await client.query(
                `INSERT INTO neon_server_endpoint_leases
                    (server_id, endpoint, verified_at, expires_at, auth_enabled, published)
                 VALUES ($1, $2, $3, $4, $5, $6)
                 ON CONFLICT (server_id) DO UPDATE SET
                    endpoint = EXCLUDED.endpoint,
                    verified_at = EXCLUDED.verified_at,
                    expires_at = EXCLUDED.expires_at,
                    auth_enabled = EXCLUDED.auth_enabled,
                    published = EXCLUDED.published`,
                [claim.serverId, claim.endpoint, claim.verifiedAt, claim.expiresAt, claim.authEnabled, claim.published],
            );
            await client.query(
                `DELETE FROM neon_registered_servers WHERE endpoint = $2 AND server_id <> $1`,
                [claim.serverId, claim.endpoint],
            );
            if (!claim.published) {
                await client.query(
                    `DELETE FROM neon_registered_servers WHERE server_id = $1`,
                    [claim.serverId],
                );
            }
            return "accepted";
        });
    }

    async isEndpointReservedByIdentity(endpoint: string, now: Date): Promise<boolean> {
        const result = await this.#pool.query(
            `SELECT 1 FROM neon_server_endpoint_leases WHERE endpoint = $1 AND expires_at > $2`,
            [endpoint, now],
        );
        return result.rowCount === 1;
    }

    async isServerEndpointAuthorized(serverId: string, endpoint: string, now: Date): Promise<boolean> {
        const result = await this.#pool.query(
            `SELECT 1
               FROM neon_server_endpoint_leases l
               JOIN neon_server_identities i ON i.server_id = l.server_id
              WHERE l.server_id = $1
                AND l.endpoint = $2
                AND l.auth_enabled
                AND l.expires_at > $3
                AND i.status = 'active'`,
            [serverId, endpoint, now],
        );
        return result.rowCount === 1;
    }

    async findServerAssetSource(sourceUrl: string, freshSince: Date): Promise<ServerAssetSource | null> {
        const result = await this.#pool.query<{ source_url: string; asset_hash: string; fetched_at: Date }>(
            `SELECT source_url, asset_hash, fetched_at
               FROM neon_server_asset_sources
              WHERE source_url = $1 AND fetched_at >= $2`,
            [sourceUrl, freshSince],
        );
        const row = result.rows[0];
        return row ? { sourceUrl: row.source_url, assetHash: row.asset_hash, fetchedAt: row.fetched_at } : null;
    }

    async putServerAsset(asset: ServerAsset, source: ServerAssetSource): Promise<void> {
        await inTransaction(this.#pool, async (client) => {
            await client.query(
                `INSERT INTO neon_server_assets (hash, mime_type, width, height, bytes, created_at)
                 VALUES ($1, $2, $3, $4, $5, $6)
                 ON CONFLICT (hash) DO NOTHING`,
                [asset.hash, asset.mimeType, asset.width, asset.height, asset.bytes, asset.createdAt],
            );
            await client.query(
                `INSERT INTO neon_server_asset_sources (source_url, asset_hash, fetched_at)
                 VALUES ($1, $2, $3)
                 ON CONFLICT (source_url) DO UPDATE SET asset_hash = EXCLUDED.asset_hash, fetched_at = EXCLUDED.fetched_at`,
                [source.sourceUrl, source.assetHash, source.fetchedAt],
            );
        });
    }

    async findServerAssetByHash(hash: string): Promise<ServerAsset | null> {
        const result = await this.#pool.query<ServerAssetRow>(
            `SELECT hash, mime_type, width, height, bytes, created_at FROM neon_server_assets WHERE hash = $1`,
            [hash],
        );
        return result.rows[0] ? toServerAsset(result.rows[0]) : null;
    }

    async close(): Promise<void> {
        await this.#pool.end();
    }
}
