import { z } from "zod";

import type { DiscordProfile } from "./model.js";

const tokenResponseSchema = z.object({
    access_token: z.string().min(1),
    token_type: z.string(),
});

const userResponseSchema = z.object({
    id: z.string().regex(/^\d+$/),
    username: z.string().min(1),
    global_name: z.string().nullable().optional(),
    avatar: z.string().nullable().optional(),
});

const guildMemberSchema = z.object({
    roles: z.array(z.string()),
    pending: z.boolean().optional(),
});

export interface DiscordGuildMember {
    roles: string[];
    pending: boolean;
}

export interface DiscordClient {
    createAuthorizationUrl(state: string, codeChallenge: string): string;
    exchangeCode(code: string, codeVerifier: string): Promise<string>;
    getCurrentUser(accessToken: string): Promise<DiscordProfile>;
    getGuildMember(guildId: string, userId: string, botToken: string): Promise<DiscordGuildMember | null>;
}

export class DiscordApiError extends Error {
    constructor(
        readonly operation: string,
        readonly status: number,
    ) {
        super(`Discord ${operation} failed with HTTP ${status}`);
    }
}

interface DiscordClientConfig {
    clientId: string;
    clientSecret: string;
    redirectUri: string;
    apiBaseUrl: string;
}

export class HttpDiscordClient implements DiscordClient {
    constructor(private readonly config: DiscordClientConfig) {}

    createAuthorizationUrl(state: string, codeChallenge: string): string {
        const url = new URL("https://discord.com/oauth2/authorize");
        url.search = new URLSearchParams({
            client_id: this.config.clientId,
            response_type: "code",
            redirect_uri: this.config.redirectUri,
            scope: "identify",
            state,
            code_challenge: codeChallenge,
            code_challenge_method: "S256",
        }).toString();
        return url.href;
    }

    async exchangeCode(code: string, codeVerifier: string): Promise<string> {
        const body = new URLSearchParams({
            grant_type: "authorization_code",
            code,
            redirect_uri: this.config.redirectUri,
            code_verifier: codeVerifier,
        });
        const response = await this.request("exchange", `${this.config.apiBaseUrl}/oauth2/token`, {
            method: "POST",
            headers: {
                authorization: `Basic ${Buffer.from(`${this.config.clientId}:${this.config.clientSecret}`).toString("base64")}`,
                "content-type": "application/x-www-form-urlencoded",
            },
            body,
        });
        const token = tokenResponseSchema.parse(await response.json());
        if (token.token_type.toLowerCase() !== "bearer") throw new Error("Discord returned an unsupported token type");
        return token.access_token;
    }

    async getCurrentUser(accessToken: string): Promise<DiscordProfile> {
        const response = await this.request("current user lookup", `${this.config.apiBaseUrl}/users/@me`, {
            headers: { authorization: `Bearer ${accessToken}` },
        });
        const user = userResponseSchema.parse(await response.json());
        return {
            id: user.id,
            username: user.username,
            globalName: user.global_name ?? null,
            avatar: user.avatar ?? null,
        };
    }

    async getGuildMember(guildId: string, userId: string, botToken: string): Promise<DiscordGuildMember | null> {
        const url = `${this.config.apiBaseUrl}/guilds/${encodeURIComponent(guildId)}/members/${encodeURIComponent(userId)}`;
        const response = await fetch(url, {
            headers: { authorization: `Bot ${botToken}` },
            signal: AbortSignal.timeout(10_000),
        });
        if (response.status === 404) return null;
        if (!response.ok) throw new DiscordApiError("guild member lookup", response.status);
        const member = guildMemberSchema.parse(await response.json());
        return { roles: member.roles, pending: member.pending ?? false };
    }

    private async request(operation: string, url: string, init: RequestInit): Promise<Response> {
        const response = await fetch(url, { ...init, signal: AbortSignal.timeout(10_000) });
        if (!response.ok) throw new DiscordApiError(operation, response.status);
        return response;
    }
}
