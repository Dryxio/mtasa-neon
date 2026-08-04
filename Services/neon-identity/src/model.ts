export interface DiscordProfile {
    id: string;
    username: string;
    globalName: string | null;
    avatar: string | null;
}

export interface NeonAccount {
    id: string;
    discordId: string;
    discordUsername: string;
    discordDisplayName: string | null;
    discordAvatarHash: string | null;
    createdAt: Date;
    updatedAt: Date;
}

export type OAuthFlowStatus = "pending" | "authorized" | "consumed" | "denied";

export interface OAuthFlow {
    id: string;
    pollTokenHash: Buffer;
    browserTokenHash: Buffer;
    oauthStateHash: Buffer | null;
    status: OAuthFlowStatus;
    accountId: string | null;
    failureCode: string | null;
    createdAt: Date;
    expiresAt: Date;
}

export type FlowPollResult =
    | { status: "pending" }
    | { status: "denied"; failureCode: string }
    | { status: "consumed" }
    | { status: "authorized"; account: NeonAccount };

export type RegisteredServerLinkKind =
    | "website"
    | "discord"
    | "instagram"
    | "x"
    | "facebook"
    | "vk"
    | "youtube"
    | "tiktok";

export interface RegisteredServerLink {
    kind: RegisteredServerLinkKind;
    label: string;
    url: string;
}

export interface RegisteredServer {
    id: string;
    endpoint: string;
    registryProtocol: number;
    httpPort: number;
    serverVersion: string;
    name: string;
    tagline: string;
    description: string;
    countries: string[];
    languages: string[];
    links: RegisteredServerLink[];
    accent: string | null;
    firstSeenAt: Date;
    lastSeenAt: Date;
}

export interface IdentityStore {
    createFlow(flow: OAuthFlow): Promise<void>;
    beginFlow(flowId: string, browserTokenHash: Buffer, oauthStateHash: Buffer, now: Date): Promise<boolean>;
    completeFlow(flowId: string, oauthStateHash: Buffer, profile: DiscordProfile, now: Date): Promise<NeonAccount | null>;
    denyFlow(flowId: string, oauthStateHash: Buffer, failureCode: string, now: Date): Promise<boolean>;
    inspectFlow(flowId: string, pollTokenHash: Buffer, now: Date): Promise<FlowPollResult | null>;
    consumeFlow(
        flowId: string,
        pollTokenHash: Buffer,
        sessionId: string,
        sessionTokenHash: Buffer,
        sessionExpiresAt: Date,
        now: Date,
    ): Promise<NeonAccount | null>;
    findAccountBySession(sessionTokenHash: Buffer, now: Date): Promise<NeonAccount | null>;
    upsertRegisteredServer(server: RegisteredServer): Promise<void>;
    listRegisteredServers(activeSince: Date): Promise<RegisteredServer[]>;
    close(): Promise<void>;
}
