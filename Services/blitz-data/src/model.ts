export const ROUND_CAUSES = [
    "timeout",
    "disconnect",
    "vehicle_exit",
    "player_death",
    "forfeit",
    "escape",
    "arrest",
    "vehicle_fire",
    "vehicle_exploded",
    "vehicle_destroyed",
    "water",
    "rollover",
] as const;

export type RoundCause = (typeof ROUND_CAUSES)[number];

export interface BlitzAccount {
    neonId: string;
    publicId: number;
    discordId: string;
    nickname: string;
    locale: string;
    createdAt: Date;
    lastSeenAt: Date;
}

export interface AccountSyncInput {
    neonId: string;
    discordId: string;
    nickname: string;
    locale: string;
}

export interface SeriesRecord {
    id: string;
    playerOneNeonId: string;
    playerTwoNeonId: string;
    createdAt: Date;
}

export interface RoundRecordInput {
    roundNumber: number;
    pursuerNeonId: string;
    fugitiveNeonId: string;
    winnerNeonId: string;
    cause: RoundCause;
    vehicleModel: number;
    spawnId: string;
    durationMs: number;
    started: boolean;
}

export interface BlitzDataStore {
    ping(): Promise<void>;
    syncAccount(input: AccountSyncInput, now: Date): Promise<BlitzAccount>;
    updateLocale(neonId: string, locale: string, now: Date): Promise<BlitzAccount | null>;
    createSeries(requestKey: string, playerOneNeonId: string, playerTwoNeonId: string, now: Date): Promise<SeriesRecord>;
    recordRound(seriesId: string, input: RoundRecordInput, now: Date): Promise<boolean>;
    completeSeries(seriesId: string, reason: string, now: Date): Promise<boolean>;
    close(): Promise<void>;
}
