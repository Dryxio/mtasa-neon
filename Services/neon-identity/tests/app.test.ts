import { exportJWK, generateKeyPair, importJWK, jwtVerify, type JWK } from "jose";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import { buildApp } from "../src/app.js";
import type { ServiceConfig } from "../src/config.js";
import type { DiscordClient, DiscordGuildMember } from "../src/discord.js";
import { MemoryIdentityStore } from "../src/memory-store.js";
import type { DiscordProfile } from "../src/model.js";
import { AllowAllDiscordPolicy } from "../src/policy.js";
import { TicketSigner } from "../src/tickets.js";
import { deriveServerId, serverHeartbeatSigningMessage } from "../src/server-identity.js";

class FakeDiscordClient implements DiscordClient {
    readonly profile: DiscordProfile = {
        id: "123456789012345678",
        username: "neon-tester",
        globalName: "Neon Tester",
        avatar: "avatar-hash",
    };
    exchangedCode: string | null = null;
    exchangedVerifier: string | null = null;

    createAuthorizationUrl(state: string, codeChallenge: string): string {
        const url = new URL("https://discord.test/oauth2/authorize");
        url.search = new URLSearchParams({ state, code_challenge: codeChallenge, scope: "identify" }).toString();
        return url.href;
    }

    async exchangeCode(code: string, codeVerifier: string): Promise<string> {
        this.exchangedCode = code;
        this.exchangedVerifier = codeVerifier;
        return "discord-access-token";
    }

    async getCurrentUser(accessToken: string): Promise<DiscordProfile> {
        expect(accessToken).toBe("discord-access-token");
        return this.profile;
    }

    async getGuildMember(): Promise<DiscordGuildMember | null> {
        return null;
    }
}

function testConfig(privateJwk: JsonWebKey): ServiceConfig {
    return {
        nodeEnv: "test",
        host: "127.0.0.1",
        port: 8080,
        publicBaseUrl: new URL("https://identity.test"),
        databaseUrl: "postgres://unused",
        databaseSsl: false,
        discord: {
            clientId: "123456789012345678",
            clientSecret: "not-used-by-the-fake-client",
            redirectUri: "https://identity.test/v1/auth/discord/callback",
            apiBaseUrl: "https://discord.test/api/v10",
            requiredGuildId: null,
            requiredRoleIds: [],
            botToken: null,
            requireCompletedScreening: true,
        },
        issuer: "https://identity.neon.test",
        serverRegistry: new Map([
            ["blitz-production", new Set(["203.0.113.10:22003"])],
            ["neon-test-server", new Set(["127.0.0.1:22003"])],
        ]),
        ticketKeyId: "test-key-v1",
        ticketPrivateJwk: privateJwk,
        oauthCookieKey: Buffer.alloc(32, 7),
        flowTtlSeconds: 300,
        sessionTtlSeconds: 3600,
        ticketTtlSeconds: 45,
        trustProxy: false,
    };
}

function firstCookie(value: string | string[] | undefined): string | undefined {
    const header = Array.isArray(value) ? value[0] : value;
    return header?.split(";", 1)[0];
}

describe("Neon Identity HTTP contract", () => {
    let app: Awaited<ReturnType<typeof buildApp>>;
    let store: MemoryIdentityStore;
    let discord: FakeDiscordClient;
    let signer: TicketSigner;
    let publicKey: CryptoKey;

    beforeEach(async () => {
        const keys = await generateKeyPair("EdDSA", { crv: "Ed25519", extractable: true });
        const privateJwk = await exportJWK(keys.privateKey);
        publicKey = await importJWK((await exportJWK(keys.publicKey)) as JWK, "EdDSA") as CryptoKey;
        const config = testConfig(privateJwk);
        signer = await TicketSigner.create(privateJwk, config.issuer, config.ticketKeyId, config.ticketTtlSeconds);
        store = new MemoryIdentityStore();
        discord = new FakeDiscordClient();
        app = await buildApp({
            config,
            store,
            discord,
            policy: new AllowAllDiscordPolicy(),
            ticketSigner: signer,
            aseProbe: async (address, port, version) =>
                address === "203.0.113.10" && (port === 22003 || port === 22004) && version === "1.7.0-9.99999",
            serverAssetFetcher: async (sourceUrl, createdAt) => ({
                hash: sourceUrl.includes("banner") ? "b".repeat(64) : "a".repeat(64),
                mimeType: "image/png",
                width: sourceUrl.includes("banner") ? 1920 : 512,
                height: sourceUrl.includes("banner") ? 1080 : 512,
                bytes: Buffer.from(sourceUrl.includes("banner") ? "fake-banner" : "fake-logo"),
                createdAt,
            }),
            now: () => new Date("2026-08-03T12:00:00.000Z"),
        });
    });

    afterEach(async () => app?.close());

    it("registers an ASE-verified server and publishes its configured metadata", async () => {
        const discovery = await app.inject({ method: "GET", url: "/.well-known/neon-identity" });
        expect(discovery.statusCode).toBe(200);
        expect(discovery.json()).toMatchObject({
            server_registry_uri: "https://identity.test/.well-known/neon-server-registry",
        });

        const empty = await app.inject({ method: "GET", url: "/.well-known/neon-server-registry" });
        expect(empty.json()).toEqual({ schema_version: 1, servers: [] });

        const heartbeat = await app.inject({
            method: "POST",
            url: "/v1/server-registry/heartbeat",
            headers: { "x-real-ip": "203.0.113.10" },
            payload: {
                registry_protocol: 1,
                game_port: 22003,
                http_port: 22005,
                server_version: "1.7.0-9.99999",
                name: "MTA:SA Neon — Test",
                tagline: "Test server",
                description: "Metadata loaded from mtaserver.conf.",
                countries: ["GB", "FR"],
                languages: ["English", "French"],
                links: [{ kind: "website", label: "Website", url: "https://mta-neon.com" }],
                logo_url: "https://assets.example.com/logo.png",
                banner_url: "https://assets.example.com/banner.png",
            },
        });
        expect(heartbeat.statusCode).toBe(202);
        expect(heartbeat.json()).toEqual({
            status: "registered",
            server_id: "blitz-production",
            endpoint: "203.0.113.10:22003",
            expires_in: 300,
        });

        const response = await app.inject({ method: "GET", url: "/.well-known/neon-server-registry" });
        expect(response.statusCode).toBe(200);
        expect(response.headers["cache-control"]).toBe("public, max-age=30, stale-if-error=300");
        expect(response.headers["access-control-allow-origin"]).toBe("*");
        expect(response.headers["cross-origin-resource-policy"]).toBe("cross-origin");
        expect(response.json()).toEqual({
            schema_version: 1,
            servers: [
                {
                    id: "blitz-production",
                    endpoints: ["203.0.113.10:22003"],
                    name: "MTA:SA Neon — Test",
                    tagline: "Test server",
                    description: "Metadata loaded from mtaserver.conf.",
                    countries: ["GB", "FR"],
                    languages: ["English", "French"],
                    links: [{ kind: "website", label: "Website", url: "https://mta-neon.com" }],
                    logo_url: `https://identity.test/v1/server-registry/assets/${"a".repeat(64)}`,
                    banner_url: `https://identity.test/v1/server-registry/assets/${"b".repeat(64)}`,
                },
            ],
        });

        const logo = await app.inject({
            method: "GET",
            url: `/v1/server-registry/assets/${"a".repeat(64)}`,
        });
        expect(logo.statusCode).toBe(200);
        expect(logo.headers["content-type"]).toBe("image/png");
        expect(logo.headers["cache-control"]).toBe("public, max-age=31536000, immutable");
        expect(logo.headers["cross-origin-resource-policy"]).toBe("cross-origin");
        expect(logo.rawPayload).toEqual(Buffer.from("fake-logo"));
    });

    it("refuses registry claims without the proxy-bound public source address", async () => {
        const response = await app.inject({
            method: "POST",
            url: "/v1/server-registry/heartbeat",
            payload: {
                registry_protocol: 1,
                game_port: 22003,
                http_port: 22005,
                server_version: "1.7.0-9.99999",
                name: "Spoofed server",
            },
        });
        expect(response.statusCode).toBe(403);
        expect(response.json()).toEqual({ error: "public_ipv4_required" });
    });

    it("reports malformed signed JSON as a client error", async () => {
        const response = await app.inject({
            method: "POST",
            url: "/v1/server-registry/heartbeat",
            headers: { "content-type": "application/json", "x-real-ip": "203.0.113.10" },
            payload: "{",
        });

        expect(response.statusCode).toBe(400);
        expect(response.json()).toEqual({ error: "invalid_json" });
    });

    it("publishes a verified community server without granting an official Identity ID", async () => {
        const heartbeat = await app.inject({
            method: "POST",
            url: "/v1/server-registry/heartbeat",
            headers: { "x-real-ip": "203.0.113.10" },
            payload: {
                registry_protocol: 1,
                game_port: 22004,
                http_port: 22006,
                server_version: "1.7.0-9.99999",
                name: "Community Neon",
            },
        });
        expect(heartbeat.statusCode).toBe(202);
        expect(heartbeat.json()).toMatchObject({
            server_id: "community-203-0-113-10-22004",
            endpoint: "203.0.113.10:22004",
        });

        const registry = await app.inject({ method: "GET", url: "/.well-known/neon-server-registry" });
        expect(registry.json()).toMatchObject({
            servers: [
                {
                    id: "community-203-0-113-10-22004",
                    name: "Community Neon",
                },
            ],
        });
    });

    it("auto-enrolls a signed server identity only after the V2 proof and ASE marker agree", async () => {
        const serverKeys = await generateKeyPair("EdDSA", { crv: "Ed25519", extractable: true });
        const serverPublicJwk = await exportJWK(serverKeys.publicKey);
        const publicKey = serverPublicJwk.x!;
        const serverId = deriveServerId(publicKey)!;
        const timestamp = `${Math.floor(new Date("2026-08-03T12:00:00.000Z").getTime() / 1_000)}`;
        const nonce = Buffer.alloc(16, 9).toString("base64url");
        const rawBody = JSON.stringify({
            registry_protocol: 2,
            game_port: 22003,
            http_port: 22005,
            server_version: "1.7.0-9.99999",
            name: "Automatic Neon server",
            auth_enabled: true,
            published: false,
        });
        const signature = Buffer.from(await crypto.subtle.sign(
            "Ed25519",
            serverKeys.privateKey,
            Buffer.from(serverHeartbeatSigningMessage(timestamp, nonce, rawBody)),
        )).toString("base64url");

        const response = await app.inject({
            method: "POST",
            url: "/v1/server-registry/heartbeat",
            headers: {
                "content-type": "application/json",
                "x-real-ip": "203.0.113.10",
                "x-neon-server-key": publicKey,
                "x-neon-server-timestamp": timestamp,
                "x-neon-server-nonce": nonce,
                "x-neon-server-signature": signature,
            },
            payload: rawBody,
        });
        expect(response.statusCode).toBe(202);
        expect(response.json()).toEqual({
            status: "registered",
            server_id: serverId,
            endpoint: "203.0.113.10:22003",
            expires_in: 300,
        });
        expect(await store.isServerEndpointAuthorized(serverId, "203.0.113.10:22003", new Date("2026-08-03T12:00:01.000Z"))).toBe(true);
        expect((await app.inject({ method: "GET", url: "/.well-known/neon-server-registry" })).json()).toEqual({ schema_version: 1, servers: [] });

        const replay = await app.inject({
            method: "POST",
            url: "/v1/server-registry/heartbeat",
            headers: {
                "content-type": "application/json",
                "x-real-ip": "203.0.113.10",
                "x-neon-server-key": publicKey,
                "x-neon-server-timestamp": timestamp,
                "x-neon-server-nonce": nonce,
                "x-neon-server-signature": signature,
            },
            payload: rawBody,
        });
        expect(replay.statusCode).toBe(401);
        expect(replay.json()).toEqual({ error: "replay" });
    });

    it("completes OAuth once and issues a server-bound Ed25519 ticket", async () => {
        const start = await app.inject({ method: "POST", url: "/v1/auth/discord/start" });
        expect(start.statusCode).toBe(201);
        const started = start.json<{
            flow_id: string;
            poll_token: string;
            authorization_url: string;
            expires_at: string;
        }>();

        const bridgeUrl = new URL(started.authorization_url);
        expect(bridgeUrl.search).toBe("");
        const bridgeSecrets = new URLSearchParams(bridgeUrl.hash.slice(1));
        expect(bridgeSecrets.get("flow_id")).toBe(started.flow_id);
        expect(bridgeSecrets.get("browser_token")).toBeTruthy();
        const persistedFlow = store.flows.get(started.flow_id);
        expect(persistedFlow?.pollTokenHash.toString("utf8")).not.toContain(started.poll_token);

        const pending = await app.inject({
            method: "POST",
            url: "/v1/auth/discord/poll",
            payload: { flow_id: started.flow_id, poll_token: started.poll_token },
        });
        expect(pending.json()).toEqual({ status: "pending" });

        const authorization = await app.inject({
            method: "POST",
            url: "/v1/auth/discord/authorize",
            payload: {
                flow_id: started.flow_id,
                browser_token: bridgeSecrets.get("browser_token"),
            },
        });
        expect(authorization.statusCode).toBe(200);
        const cookie = firstCookie(authorization.headers["set-cookie"]);
        expect(cookie).toBeTruthy();
        const discordUrl = new URL(authorization.json<{ authorization_url: string }>().authorization_url);
        const oauthState = discordUrl.searchParams.get("state");
        expect(oauthState).toBeTruthy();
        expect(discordUrl.searchParams.get("scope")).toBe("identify");
        expect(discordUrl.searchParams.get("code_challenge")).toBeTruthy();

        const callback = await app.inject({
            method: "GET",
            url: `/v1/auth/discord/callback?code=discord-code&state=${encodeURIComponent(oauthState!)}`,
            headers: { cookie: cookie! },
        });
        expect(callback.statusCode).toBe(200);
        expect(discord.exchangedCode).toBe("discord-code");
        expect(discord.exchangedVerifier).toBeTruthy();

        const poll = await app.inject({
            method: "POST",
            url: "/v1/auth/discord/poll",
            payload: { flow_id: started.flow_id, poll_token: started.poll_token },
        });
        expect(poll.statusCode).toBe(200);
        const completed = poll.json<{ session_token: string; account: { id: string; discord_id: string } }>();
        expect(completed.session_token).toMatch(/^ns_v1_/);
        expect(completed.account.discord_id).toBe(discord.profile.id);
        expect(store.sessions[0]?.tokenHash.toString("utf8")).not.toContain(completed.session_token);

        const replayedPoll = await app.inject({
            method: "POST",
            url: "/v1/auth/discord/poll",
            payload: { flow_id: started.flow_id, poll_token: started.poll_token },
        });
        expect(replayedPoll.statusCode).toBe(409);

        const signSpy = vi.spyOn(signer, "sign");
        const ticketResponse = await app.inject({
            method: "POST",
            url: "/v1/tickets",
            headers: { authorization: `Bearer ${completed.session_token}` },
            payload: { server_id: "blitz-production", server_endpoint: "203.0.113.10:22003" },
        });
        expect(ticketResponse.statusCode).toBe(200);
        expect(signSpy).toHaveBeenCalledTimes(1);
        const ticketBody = ticketResponse.json<{ ticket: string; expires_at: string }>();
        const verified = await jwtVerify(ticketBody.ticket, publicKey, {
            algorithms: ["EdDSA"],
            issuer: "https://identity.neon.test",
            audience: "blitz-production",
            currentDate: new Date("2026-08-03T12:00:00.000Z"),
        });
        expect(verified.protectedHeader).toMatchObject({ alg: "EdDSA", typ: "JWT", kid: "test-key-v1" });
        expect(verified.payload.sub).toBe(completed.account.id);
        expect(verified.payload.discord_id).toBe(discord.profile.id);
        expect(verified.payload.server_endpoint).toBe("203.0.113.10:22003");
        expect(verified.payload.jti).toBeTruthy();
        expect(verified.payload.exp! - verified.payload.iat!).toBe(45);

        const unknownServer = await app.inject({
            method: "POST",
            url: "/v1/tickets",
            headers: { authorization: `Bearer ${completed.session_token}` },
            payload: { server_id: "unregistered-server", server_endpoint: "203.0.113.10:22003" },
        });
        expect(unknownServer.statusCode).toBe(403);
        expect(unknownServer.json()).toEqual({ error: "server_endpoint_not_allowed" });
        expect(signSpy).toHaveBeenCalledTimes(1);

        const impersonatedServer = await app.inject({
            method: "POST",
            url: "/v1/tickets",
            headers: { authorization: `Bearer ${completed.session_token}` },
            payload: { server_id: "blitz-production", server_endpoint: "198.51.100.20:22003" },
        });
        expect(impersonatedServer.statusCode).toBe(403);
        expect(impersonatedServer.json()).toEqual({ error: "server_endpoint_not_allowed" });
        expect(signSpy).toHaveBeenCalledTimes(1);
    });

    it("rejects a callback whose state does not match the encrypted cookie", async () => {
        const started = (await app.inject({ method: "POST", url: "/v1/auth/discord/start" })).json<{
            flow_id: string;
            poll_token: string;
            authorization_url: string;
        }>();
        const secrets = new URLSearchParams(new URL(started.authorization_url).hash.slice(1));
        const authorization = await app.inject({
            method: "POST",
            url: "/v1/auth/discord/authorize",
            payload: { flow_id: started.flow_id, browser_token: secrets.get("browser_token") },
        });
        const cookie = firstCookie(authorization.headers["set-cookie"]);
        const callback = await app.inject({
            method: "GET",
            url: "/v1/auth/discord/callback?code=attacker-code&state=wrong-state",
            headers: { cookie: cookie! },
        });
        expect(callback.statusCode).toBe(400);
        expect(discord.exchangedCode).toBeNull();

        const poll = await app.inject({
            method: "POST",
            url: "/v1/auth/discord/poll",
            payload: { flow_id: started.flow_id, poll_token: started.poll_token },
        });
        expect(poll.json()).toEqual({ status: "pending" });
    });

    it("does not issue tickets for missing or invalid sessions", async () => {
        const missing = await app.inject({
            method: "POST",
            url: "/v1/tickets",
            payload: { server_id: "blitz", server_endpoint: "127.0.0.1:22003" },
        });
        expect(missing.statusCode).toBe(401);
        const invalid = await app.inject({
            method: "POST",
            url: "/v1/tickets",
            headers: { authorization: `Bearer ns_v1_${"x".repeat(43)}` },
            payload: { server_id: "blitz", server_endpoint: "127.0.0.1:22003" },
        });
        expect(invalid.statusCode).toBe(401);
    });
});
