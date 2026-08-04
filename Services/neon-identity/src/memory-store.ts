import type { DiscordProfile, FlowPollResult, IdentityStore, NeonAccount, OAuthFlow, RegisteredServer } from "./model.js";

interface MemorySession {
    accountId: string;
    tokenHash: Buffer;
    expiresAt: Date;
}

function hashesEqual(left: Buffer, right: Buffer): boolean {
    return left.length === right.length && left.equals(right);
}

export class MemoryIdentityStore implements IdentityStore {
    readonly flows = new Map<string, OAuthFlow>();
    readonly accounts = new Map<string, NeonAccount>();
    readonly sessions: MemorySession[] = [];
    readonly registeredServers = new Map<string, RegisteredServer>();

    async createFlow(flow: OAuthFlow): Promise<void> {
        this.flows.set(flow.id, {
            ...flow,
            pollTokenHash: Buffer.from(flow.pollTokenHash),
            browserTokenHash: Buffer.from(flow.browserTokenHash),
            oauthStateHash: flow.oauthStateHash ? Buffer.from(flow.oauthStateHash) : null,
            createdAt: new Date(flow.createdAt),
            expiresAt: new Date(flow.expiresAt),
        });
    }

    async beginFlow(flowId: string, browserTokenHash: Buffer, oauthStateHash: Buffer, now: Date): Promise<boolean> {
        const flow = this.flows.get(flowId);
        if (
            !flow ||
            flow.status !== "pending" ||
            flow.oauthStateHash ||
            flow.expiresAt <= now ||
            !hashesEqual(flow.browserTokenHash, browserTokenHash)
        ) {
            return false;
        }
        flow.oauthStateHash = Buffer.from(oauthStateHash);
        return true;
    }

    async completeFlow(flowId: string, oauthStateHash: Buffer, profile: DiscordProfile, now: Date): Promise<NeonAccount | null> {
        const flow = this.flows.get(flowId);
        if (!this.canComplete(flow, oauthStateHash, now)) return null;

        let account = [...this.accounts.values()].find((candidate) => candidate.discordId === profile.id);
        if (!account) {
            account = {
                id: crypto.randomUUID(),
                discordId: profile.id,
                discordUsername: profile.username,
                discordDisplayName: profile.globalName,
                discordAvatarHash: profile.avatar,
                createdAt: now,
                updatedAt: now,
            };
            this.accounts.set(account.id, account);
        } else {
            account.discordUsername = profile.username;
            account.discordDisplayName = profile.globalName;
            account.discordAvatarHash = profile.avatar;
            account.updatedAt = now;
        }
        flow.status = "authorized";
        flow.accountId = account.id;
        return structuredClone(account);
    }

    async denyFlow(flowId: string, oauthStateHash: Buffer, failureCode: string, now: Date): Promise<boolean> {
        const flow = this.flows.get(flowId);
        if (!this.canComplete(flow, oauthStateHash, now)) return false;
        flow.status = "denied";
        flow.failureCode = failureCode;
        return true;
    }

    async inspectFlow(flowId: string, pollTokenHash: Buffer, now: Date): Promise<FlowPollResult | null> {
        const flow = this.flows.get(flowId);
        if (!flow || flow.expiresAt <= now || !hashesEqual(flow.pollTokenHash, pollTokenHash)) return null;
        if (flow.status === "pending") return { status: "pending" };
        if (flow.status === "denied") return { status: "denied", failureCode: flow.failureCode ?? "oauth_denied" };
        if (flow.status === "consumed") return { status: "consumed" };
        const account = flow.accountId ? this.accounts.get(flow.accountId) : null;
        return account ? { status: "authorized", account: structuredClone(account) } : null;
    }

    async consumeFlow(
        flowId: string,
        pollTokenHash: Buffer,
        _sessionId: string,
        sessionTokenHash: Buffer,
        sessionExpiresAt: Date,
        now: Date,
    ): Promise<NeonAccount | null> {
        const flow = this.flows.get(flowId);
        if (
            !flow ||
            flow.status !== "authorized" ||
            flow.expiresAt <= now ||
            !flow.accountId ||
            !hashesEqual(flow.pollTokenHash, pollTokenHash)
        ) {
            return null;
        }
        const account = this.accounts.get(flow.accountId);
        if (!account) return null;
        flow.status = "consumed";
        this.sessions.push({ accountId: account.id, tokenHash: Buffer.from(sessionTokenHash), expiresAt: sessionExpiresAt });
        return structuredClone(account);
    }

    async findAccountBySession(sessionTokenHash: Buffer, now: Date): Promise<NeonAccount | null> {
        const session = this.sessions.find(
            (candidate) => candidate.expiresAt > now && hashesEqual(candidate.tokenHash, sessionTokenHash),
        );
        const account = session ? this.accounts.get(session.accountId) : null;
        return account ? structuredClone(account) : null;
    }

    async upsertRegisteredServer(server: RegisteredServer): Promise<void> {
        const existing = this.registeredServers.get(server.id);
        this.registeredServers.set(server.id, {
            ...structuredClone(server),
            firstSeenAt: existing?.firstSeenAt ?? new Date(server.firstSeenAt),
            lastSeenAt: new Date(server.lastSeenAt),
        });
    }

    async listRegisteredServers(activeSince: Date): Promise<RegisteredServer[]> {
        return [...this.registeredServers.values()]
            .filter((server) => server.lastSeenAt >= activeSince)
            .sort((left, right) => left.id.localeCompare(right.id))
            .map((server) => structuredClone(server));
    }

    async close(): Promise<void> {}

    private canComplete(flow: OAuthFlow | undefined, oauthStateHash: Buffer, now: Date): flow is OAuthFlow {
        return Boolean(
            flow &&
                flow.status === "pending" &&
                flow.expiresAt > now &&
                flow.oauthStateHash &&
                hashesEqual(flow.oauthStateHash, oauthStateHash),
        );
    }
}
