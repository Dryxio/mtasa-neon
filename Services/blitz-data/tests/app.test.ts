import { randomUUID } from "node:crypto";

import { afterEach, describe, expect, it } from "vitest";

import { buildApp } from "../src/app.js";
import type {
    AccountSyncInput,
    BlitzAccount,
    BlitzDataStore,
    RoundRecordInput,
    SeriesRecord,
} from "../src/model.js";

const API_TOKEN = "test-token-that-is-longer-than-thirty-two-characters";
const PLAYER_ONE = "11111111-1111-4111-8111-111111111111";
const PLAYER_TWO = "22222222-2222-4222-8222-222222222222";

class MemoryStore implements BlitzDataStore {
    readonly accounts = new Map<string, BlitzAccount>();
    readonly series = new Map<string, SeriesRecord & {completed: boolean}>();
    readonly rounds = new Set<string>();
    nextPublicId = 1000;

    async ping(): Promise<void> {}

    async syncAccount(input: AccountSyncInput, now: Date): Promise<BlitzAccount> {
        const existing = this.accounts.get(input.neonId);
        const account: BlitzAccount = {
            neonId: input.neonId,
            publicId: existing?.publicId ?? this.nextPublicId++,
            discordId: input.discordId,
            nickname: input.nickname,
            locale: existing?.locale ?? input.locale,
            createdAt: existing?.createdAt ?? now,
            lastSeenAt: now,
        };
        this.accounts.set(input.neonId, account);
        return account;
    }

    async updateLocale(neonId: string, locale: string, now: Date): Promise<BlitzAccount | null> {
        const account = this.accounts.get(neonId);
        if (!account) return null;
        const updated = {...account, locale, lastSeenAt: now};
        this.accounts.set(neonId, updated);
        return updated;
    }

    async createSeries(_requestKey: string, playerOneNeonId: string, playerTwoNeonId: string, now: Date): Promise<SeriesRecord> {
        if (!this.accounts.has(playerOneNeonId) || !this.accounts.has(playerTwoNeonId)) throw new Error("missing account");
        const series = {id: randomUUID(), playerOneNeonId, playerTwoNeonId, createdAt: now, completed: false};
        this.series.set(series.id, series);
        return series;
    }

    async recordRound(seriesId: string, input: RoundRecordInput): Promise<boolean> {
        const key = `${seriesId}:${input.roundNumber}`;
        if (!this.series.has(seriesId)) throw new Error("missing series");
        if (this.rounds.has(key)) return false;
        this.rounds.add(key);
        return true;
    }

    async completeSeries(seriesId: string): Promise<boolean> {
        const series = this.series.get(seriesId);
        if (!series || series.completed) return false;
        series.completed = true;
        return true;
    }

    async close(): Promise<void> {}
}

const apps: Awaited<ReturnType<typeof buildApp>>[] = [];

async function createTestApp() {
    const store = new MemoryStore();
    const app = await buildApp(store, API_TOKEN);
    apps.push(app);
    return {app, store};
}

async function syncAccount(app: Awaited<ReturnType<typeof buildApp>>, neonId: string, discordId: string) {
    return app.inject({
        method: "POST",
        url: "/v1/accounts/sync",
        headers: {authorization: `Bearer ${API_TOKEN}`},
        payload: {neon_id: neonId, discord_id: discordId, nickname: "Player", locale: "en"},
    });
}

afterEach(async () => {
    await Promise.all(apps.splice(0).map((app) => app.close()));
});

describe("Blitz data API", () => {
    it("does not expose mutation routes without the private server token", async () => {
        const {app} = await createTestApp();
        const response = await app.inject({
            method: "POST",
            url: "/v1/accounts/sync",
            payload: {neon_id: PLAYER_ONE, discord_id: "12345678901234567", nickname: "Player", locale: "en"},
        });
        expect(response.statusCode).toBe(401);
    });

    it("creates a permanent public ID and preserves it on subsequent syncs", async () => {
        const {app} = await createTestApp();
        const first = await syncAccount(app, PLAYER_ONE, "12345678901234567");
        const second = await app.inject({
            method: "POST",
            url: "/v1/accounts/sync",
            headers: {authorization: `Bearer ${API_TOKEN}`},
            payload: {neon_id: PLAYER_ONE, discord_id: "12345678901234567", nickname: "Renamed", locale: "fr"},
        });
        expect(first.statusCode).toBe(200);
        expect(first.json().account.public_id).toBe(1000);
        expect(second.json().account.public_id).toBe(1000);
        expect(second.json().account.nickname).toBe("Renamed");
        expect(second.json().account.locale).toBe("en");

        const localeUpdate = await app.inject({
            method: "POST",
            url: "/v1/accounts/locale",
            headers: {authorization: `Bearer ${API_TOKEN}`},
            payload: {neon_id: PLAYER_ONE, locale: "fr"},
        });
        expect(localeUpdate.json().account.locale).toBe("fr");
    });

    it("groups idempotent rounds under one series", async () => {
        const {app} = await createTestApp();
        await syncAccount(app, PLAYER_ONE, "12345678901234567");
        await syncAccount(app, PLAYER_TWO, "22345678901234567");
        const created = await app.inject({
            method: "POST",
            url: "/v1/series",
            headers: {authorization: `Bearer ${API_TOKEN}`},
            payload: {request_key: "test-series-request-0001", player_one_neon_id: PLAYER_ONE, player_two_neon_id: PLAYER_TWO},
        });
        expect(created.statusCode).toBe(201);
        const seriesId = created.json().series.id as string;
        const payload = {
            round_number: 1,
            pursuer_neon_id: PLAYER_ONE,
            fugitive_neon_id: PLAYER_TWO,
            winner_neon_id: PLAYER_ONE,
            cause: "timeout",
            vehicle_model: 560,
            spawn_id: "grove_01",
            duration_ms: 210000,
            started: true,
        };
        const first = await app.inject({
            method: "POST",
            url: `/v1/series/${seriesId}/rounds`,
            headers: {authorization: `Bearer ${API_TOKEN}`},
            payload,
        });
        const duplicate = await app.inject({
            method: "POST",
            url: `/v1/series/${seriesId}/rounds`,
            headers: {authorization: `Bearer ${API_TOKEN}`},
            payload,
        });
        expect(first.json()).toEqual({recorded: true});
        expect(duplicate.json()).toEqual({recorded: false});
    });

    it("rejects malformed identities before they reach the store", async () => {
        const {app} = await createTestApp();
        const response = await syncAccount(app, "not-a-uuid", "not-a-snowflake");
        expect(response.statusCode).toBe(400);
        expect(response.json()).toEqual({error: "invalid_request"});
    });
});
