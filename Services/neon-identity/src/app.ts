import { randomUUID } from "node:crypto";

import cookie from "@fastify/cookie";
import helmet from "@fastify/helmet";
import rateLimit from "@fastify/rate-limit";
import Fastify, { type FastifyServerOptions } from "fastify";
import { z } from "zod";

import { isCanonicalIpv4Endpoint, SERVER_ID_PATTERN, type ServiceConfig } from "./config.js";
import { constantTimeStringEqual, hashToken, openJson, pkceChallenge, randomToken, sealJson } from "./crypto.js";
import type { DiscordClient } from "./discord.js";
import type { IdentityStore, NeonAccount } from "./model.js";
import type { DiscordIdentityPolicy } from "./policy.js";
import type { TicketSigner } from "./tickets.js";

const OAUTH_COOKIE_NAME = "neon_oauth_v1";
const serverIdSchema = z.string().regex(SERVER_ID_PATTERN);
const startAuthorizationSchema = z.object({
    flow_id: z.uuid(),
    browser_token: z.string().min(32).max(256),
});
const pollSchema = z.object({
    flow_id: z.uuid(),
    poll_token: z.string().min(32).max(256),
});
const ticketRequestSchema = z.object({
    server_id: serverIdSchema,
    server_endpoint: z.string().refine(isCanonicalIpv4Endpoint),
});

interface OAuthCookieState {
    flowId: string;
    state: string;
    verifier: string;
    expiresAt: number;
}

export interface AppDependencies {
    config: ServiceConfig;
    store: IdentityStore;
    discord: DiscordClient;
    policy: DiscordIdentityPolicy;
    ticketSigner: TicketSigner;
    now?: () => Date;
    logger?: FastifyServerOptions["logger"];
}

function accountResponse(account: NeonAccount) {
    return {
        id: account.id,
        discord_id: account.discordId,
        discord_username: account.discordUsername,
        discord_display_name: account.discordDisplayName,
        discord_avatar_hash: account.discordAvatarHash,
    };
}

function authorizationBridgeHtml(): string {
    const nonce = randomToken(18);
    const script = `
(() => {
  const output = document.getElementById("status");
  const values = new URLSearchParams(window.location.hash.slice(1));
  history.replaceState(null, "", window.location.pathname);
  const flow_id = values.get("flow_id");
  const browser_token = values.get("browser_token");
  if (!flow_id || !browser_token) { output.textContent = "This Neon sign-in link is invalid."; return; }
  fetch(window.location.pathname, {
    method: "POST",
    credentials: "same-origin",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ flow_id, browser_token })
  }).then(async (response) => {
    if (!response.ok) throw new Error("invalid");
    const body = await response.json();
    window.location.replace(body.authorization_url);
  }).catch(() => { output.textContent = "This Neon sign-in link is invalid or expired."; });
})();`;
    return `<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>Neon Identity</title></head><body><main><h1>Neon Identity</h1>
<p id="status">Opening Discord…</p></main><script nonce="${nonce}">${script}</script></body></html>`;
}

function resultHtml(success: boolean): string {
    const title = success ? "Discord linked" : "Sign-in failed";
    const message = success
        ? "Authentication succeeded. You can close this tab and return to Neon."
        : "Authentication could not be completed. Return to Neon and try again.";
    return `<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>${title}</title></head><body><main><h1>${title}</h1><p>${message}</p></main></body></html>`;
}

function extractBearer(value: string | undefined): string | null {
    if (!value) return null;
    const match = /^Bearer ([A-Za-z0-9_-]{32,256})$/.exec(value);
    return match?.[1] ?? null;
}

export async function buildApp(dependencies: AppDependencies) {
    const { config, discord, policy, store, ticketSigner } = dependencies;
    const now = dependencies.now ?? (() => new Date());
    const app = Fastify({
        logger: dependencies.logger ?? false,
        trustProxy: config.trustProxy,
        bodyLimit: 16 * 1024,
    });

    await app.register(cookie);
    await app.register(helmet, {
        contentSecurityPolicy: false,
        crossOriginEmbedderPolicy: false,
    });
    await app.register(rateLimit, {
        global: true,
        max: 300,
        timeWindow: "1 minute",
    });

    app.addHook("onClose", async () => store.close());
    app.addHook("onSend", async (request, reply, payload) => {
        if (request.url.startsWith("/v1/")) reply.header("cache-control", "no-store");
        return payload;
    });
    app.setErrorHandler((error, request, reply) => {
        request.log.error({ err: error }, "Unhandled Neon Identity request error");
        const statusCode = (error as { statusCode?: number }).statusCode === 429 ? 429 : 500;
        return reply.code(statusCode).send({ error: statusCode === 429 ? "rate_limited" : "internal_error" });
    });

    app.get("/healthz", async () => ({ status: "ok" }));

    app.get("/.well-known/neon-identity", async (_request, reply) => {
        reply.header("cache-control", "public, max-age=300");
        return {
            issuer: config.issuer,
            jwks_uri: new URL("/.well-known/jwks.json", config.publicBaseUrl).href,
            discord_start_endpoint: new URL("/v1/auth/discord/start", config.publicBaseUrl).href,
            discord_poll_endpoint: new URL("/v1/auth/discord/poll", config.publicBaseUrl).href,
            ticket_endpoint: new URL("/v1/tickets", config.publicBaseUrl).href,
            ticket_signing_alg_values_supported: ["EdDSA"],
        };
    });

    app.get("/.well-known/jwks.json", async (_request, reply) => {
        reply.header("cache-control", "public, max-age=300, stale-if-error=86400");
        return { keys: [ticketSigner.publicJwk] };
    });

    app.post(
        "/v1/auth/discord/start",
        { config: { rateLimit: { max: 10, timeWindow: "1 minute" } } },
        async (_request, reply) => {
            const currentTime = now();
            const flowId = randomUUID();
            const pollToken = randomToken();
            const browserToken = randomToken();
            const expiresAt = new Date(currentTime.getTime() + config.flowTtlSeconds * 1000);
            await store.createFlow({
                id: flowId,
                pollTokenHash: hashToken(pollToken),
                browserTokenHash: hashToken(browserToken),
                oauthStateHash: null,
                status: "pending",
                accountId: null,
                failureCode: null,
                createdAt: currentTime,
                expiresAt,
            });

            const authorizationUrl = new URL("/v1/auth/discord/authorize", config.publicBaseUrl);
            // A fragment keeps the one-time browser capability out of HTTP access
            // logs and Referer headers; the bridge page posts it same-origin.
            authorizationUrl.hash = new URLSearchParams({ flow_id: flowId, browser_token: browserToken }).toString();
            return reply.code(201).send({
                flow_id: flowId,
                poll_token: pollToken,
                authorization_url: authorizationUrl.href,
                expires_at: expiresAt.toISOString(),
            });
        },
    );

    app.get("/v1/auth/discord/authorize", async (_request, reply) => {
        const html = authorizationBridgeHtml();
        const nonceMatch = /nonce="([^"]+)"/.exec(html);
        reply
            .header("cache-control", "no-store")
            .header("referrer-policy", "no-referrer")
            .header(
                "content-security-policy",
                `default-src 'none'; script-src 'nonce-${nonceMatch?.[1] ?? "invalid"}'; connect-src 'self'; style-src 'none'; base-uri 'none'; frame-ancestors 'none'`,
            )
            .type("text/html; charset=utf-8");
        return html;
    });

    app.post(
        "/v1/auth/discord/authorize",
        { config: { rateLimit: { max: 20, timeWindow: "1 minute" } } },
        async (request, reply) => {
            const parsed = startAuthorizationSchema.safeParse(request.body);
            if (!parsed.success) return reply.code(400).send({ error: "invalid_authorization_request" });

            const currentTime = now();
            const state = randomToken();
            const verifier = randomToken(48);
            const started = await store.beginFlow(
                parsed.data.flow_id,
                hashToken(parsed.data.browser_token),
                hashToken(state),
                currentTime,
            );
            if (!started) return reply.code(400).send({ error: "invalid_or_expired_flow" });

            const cookieState: OAuthCookieState = {
                flowId: parsed.data.flow_id,
                state,
                verifier,
                expiresAt: currentTime.getTime() + config.flowTtlSeconds * 1000,
            };
            reply
                .setCookie(OAUTH_COOKIE_NAME, sealJson(cookieState, config.oauthCookieKey), {
                    httpOnly: true,
                    secure: config.nodeEnv === "production",
                    sameSite: "lax",
                    path: "/v1/auth/discord/callback",
                    maxAge: config.flowTtlSeconds,
                })
                .header("cache-control", "no-store")
                .header("referrer-policy", "no-referrer");
            return {
                authorization_url: discord.createAuthorizationUrl(state, pkceChallenge(verifier)),
            };
        },
    );

    app.get("/v1/auth/discord/callback", async (request, reply) => {
        const currentTime = now();
        const query = request.query as Record<string, unknown>;
        const cookieValue = request.cookies[OAUTH_COOKIE_NAME];
        const state = cookieValue ? openJson<OAuthCookieState>(cookieValue, config.oauthCookieKey) : null;
        reply
            .clearCookie(OAUTH_COOKIE_NAME, { path: "/v1/auth/discord/callback" })
            .header("cache-control", "no-store")
            .header("referrer-policy", "no-referrer")
            .header("content-security-policy", "default-src 'none'; style-src 'none'; base-uri 'none'; frame-ancestors 'none'")
            .type("text/html; charset=utf-8");

        if (
            !state ||
            typeof state.flowId !== "string" ||
            typeof state.state !== "string" ||
            typeof state.verifier !== "string" ||
            typeof state.expiresAt !== "number" ||
            state.expiresAt <= currentTime.getTime() ||
            typeof query.state !== "string" ||
            !constantTimeStringEqual(query.state, state.state)
        ) {
            return reply.code(400).send(resultHtml(false));
        }

        if (typeof query.error === "string" || typeof query.code !== "string") {
            await store.denyFlow(state.flowId, hashToken(state.state), "discord_oauth_denied", currentTime);
            return reply.code(403).send(resultHtml(false));
        }

        try {
            const accessToken = await discord.exchangeCode(query.code, state.verifier);
            const profile = await discord.getCurrentUser(accessToken);
            const policyResult = await policy.evaluate(profile);
            if (!policyResult.allowed) {
                await store.denyFlow(state.flowId, hashToken(state.state), policyResult.code, currentTime);
                return reply.code(403).send(resultHtml(false));
            }
            const account = await store.completeFlow(state.flowId, hashToken(state.state), profile, currentTime);
            if (!account) return reply.code(400).send(resultHtml(false));
            return reply.code(200).send(resultHtml(true));
        } catch (error) {
            request.log.warn({ err: error, flowId: state.flowId }, "Discord OAuth callback failed");
            await store.denyFlow(state.flowId, hashToken(state.state), "discord_oauth_unavailable", currentTime);
            return reply.code(502).send(resultHtml(false));
        }
    });

    app.post(
        "/v1/auth/discord/poll",
        { config: { rateLimit: { max: 120, timeWindow: "1 minute" } } },
        async (request, reply) => {
            const parsed = pollSchema.safeParse(request.body);
            if (!parsed.success) return reply.code(400).send({ error: "invalid_poll_request" });
            const currentTime = now();
            const pollHash = hashToken(parsed.data.poll_token);
            const flow = await store.inspectFlow(parsed.data.flow_id, pollHash, currentTime);
            if (!flow) return reply.code(404).send({ error: "invalid_or_expired_flow" });
            if (flow.status === "pending") return { status: "pending" };
            if (flow.status === "denied") {
                return reply.code(403).send({ status: "denied", error: flow.failureCode });
            }
            if (flow.status === "consumed") return reply.code(409).send({ error: "flow_already_consumed" });

            const sessionToken = `ns_v1_${randomToken()}`;
            const sessionExpiresAt = new Date(currentTime.getTime() + config.sessionTtlSeconds * 1000);
            const account = await store.consumeFlow(
                parsed.data.flow_id,
                pollHash,
                randomUUID(),
                hashToken(sessionToken),
                sessionExpiresAt,
                currentTime,
            );
            if (!account) return reply.code(409).send({ error: "flow_already_consumed" });
            return {
                session_token: sessionToken,
                session_expires_at: sessionExpiresAt.toISOString(),
                account: accountResponse(account),
            };
        },
    );

    app.post(
        "/v1/tickets",
        { config: { rateLimit: { max: 120, timeWindow: "1 minute" } } },
        async (request, reply) => {
            const sessionToken = extractBearer(request.headers.authorization);
            if (!sessionToken) {
                return reply.header("www-authenticate", "Bearer").code(401).send({ error: "invalid_session" });
            }
            const parsed = ticketRequestSchema.safeParse(request.body);
            if (!parsed.success) return reply.code(400).send({ error: "invalid_ticket_request" });
            const currentTime = now();
            const account = await store.findAccountBySession(hashToken(sessionToken), currentTime);
            if (!account) {
                return reply.header("www-authenticate", "Bearer").code(401).send({ error: "invalid_session" });
            }
            const registeredEndpoints = config.serverRegistry.get(parsed.data.server_id);
            if (!registeredEndpoints?.has(parsed.data.server_endpoint)) {
                return reply.code(403).send({ error: "server_endpoint_not_allowed" });
            }
            const signed = await ticketSigner.sign(
                account,
                parsed.data.server_id,
                parsed.data.server_endpoint,
                currentTime,
            );
            return { ticket: signed.ticket, expires_at: signed.expiresAt.toISOString() };
        },
    );

    return app;
}
