import { timingSafeEqual } from "node:crypto";

import helmet from "@fastify/helmet";
import Fastify, { type FastifyInstance } from "fastify";
import { z } from "zod";

import { ROUND_CAUSES, type BlitzAccount, type BlitzDataStore, type SeriesRecord } from "./model.js";

const neonIdSchema = z.uuid();
const discordIdSchema = z.string().regex(/^[0-9]{17,20}$/);
const localeSchema = z.string().regex(/^[A-Za-z][A-Za-z0-9_-]{0,15}$/);
const accountSyncSchema = z.object({
    neon_id: neonIdSchema,
    discord_id: discordIdSchema,
    nickname: z.string().min(1).max(64),
    locale: localeSchema,
});
const localeUpdateSchema = z.object({neon_id: neonIdSchema, locale: localeSchema});
const seriesCreateSchema = z
    .object({
        request_key: z.string().regex(/^[A-Za-z0-9:_-]{16,128}$/),
        player_one_neon_id: neonIdSchema,
        player_two_neon_id: neonIdSchema,
    })
    .refine((value) => value.player_one_neon_id !== value.player_two_neon_id, "Series players must be different");
const roundRecordSchema = z.object({
    round_number: z.number().int().min(1).max(10_000),
    pursuer_neon_id: neonIdSchema,
    fugitive_neon_id: neonIdSchema,
    winner_neon_id: neonIdSchema,
    cause: z.enum(ROUND_CAUSES),
    vehicle_model: z.number().int().min(400).max(611),
    spawn_id: z.string().min(1).max(64),
    duration_ms: z.number().int().min(0).max(3_600_000),
    started: z.boolean(),
});
const seriesCompleteSchema = z.object({reason: z.string().min(1).max(64)});
const seriesParamsSchema = z.object({seriesId: z.uuid()});

function accountResponse(account: BlitzAccount) {
    return {
        neon_id: account.neonId,
        public_id: account.publicId,
        discord_id: account.discordId,
        nickname: account.nickname,
        locale: account.locale,
        created_at: account.createdAt.toISOString(),
        last_seen_at: account.lastSeenAt.toISOString(),
    };
}

function seriesResponse(series: SeriesRecord) {
    return {
        id: series.id,
        player_one_neon_id: series.playerOneNeonId,
        player_two_neon_id: series.playerTwoNeonId,
        created_at: series.createdAt.toISOString(),
    };
}

function tokenMatches(header: string | undefined, expectedToken: string): boolean {
    const prefix = "Bearer ";
    if (!header?.startsWith(prefix)) return false;
    const supplied = Buffer.from(header.slice(prefix.length), "utf8");
    const expected = Buffer.from(expectedToken, "utf8");
    return supplied.length === expected.length && timingSafeEqual(supplied, expected);
}

export async function buildApp(store: BlitzDataStore, apiToken: string): Promise<FastifyInstance> {
    const app = Fastify({logger: false, bodyLimit: 16 * 1024});
    await app.register(helmet);

    app.get("/healthz", async (_request, reply) => {
        await store.ping();
        return reply.send({status: "ok"});
    });

    app.addHook("preHandler", async (request, reply) => {
        if (request.url === "/healthz") return;
        if (!tokenMatches(request.headers.authorization, apiToken)) {
            return reply.code(401).send({error: "unauthorized"});
        }
    });

    app.post("/v1/accounts/sync", async (request, reply) => {
        const input = accountSyncSchema.parse(request.body);
        const account = await store.syncAccount(
            {neonId: input.neon_id, discordId: input.discord_id, nickname: input.nickname, locale: input.locale},
            new Date(),
        );
        return reply.send({account: accountResponse(account)});
    });

    app.post("/v1/accounts/locale", async (request, reply) => {
        const input = localeUpdateSchema.parse(request.body);
        const account = await store.updateLocale(input.neon_id, input.locale, new Date());
        if (!account) return reply.code(404).send({error: "account_not_found"});
        return reply.send({account: accountResponse(account)});
    });

    app.post("/v1/series", async (request, reply) => {
        const input = seriesCreateSchema.parse(request.body);
        const series = await store.createSeries(input.request_key, input.player_one_neon_id, input.player_two_neon_id, new Date());
        return reply.code(201).send({series: seriesResponse(series)});
    });

    app.post("/v1/series/:seriesId/rounds", async (request, reply) => {
        const params = seriesParamsSchema.parse(request.params);
        const input = roundRecordSchema.parse(request.body);
        const recorded = await store.recordRound(
            params.seriesId,
            {
                roundNumber: input.round_number,
                pursuerNeonId: input.pursuer_neon_id,
                fugitiveNeonId: input.fugitive_neon_id,
                winnerNeonId: input.winner_neon_id,
                cause: input.cause,
                vehicleModel: input.vehicle_model,
                spawnId: input.spawn_id,
                durationMs: input.duration_ms,
                started: input.started,
            },
            new Date(),
        );
        return reply.send({recorded});
    });

    app.post("/v1/series/:seriesId/complete", async (request, reply) => {
        const params = seriesParamsSchema.parse(request.params);
        const input = seriesCompleteSchema.parse(request.body);
        const completed = await store.completeSeries(params.seriesId, input.reason, new Date());
        return reply.send({completed});
    });

    app.setErrorHandler((error, request, reply) => {
        if (error instanceof z.ZodError) return reply.code(400).send({error: "invalid_request"});
        request.log.error(error);
        return reply.code(500).send({error: "internal_error"});
    });

    app.addHook("onClose", async () => store.close());
    return app;
}
