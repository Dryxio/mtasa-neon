import { buildApp } from "./app.js";
import { loadConfig } from "./config.js";
import { HttpDiscordClient } from "./discord.js";
import { PostgresIdentityStore } from "./postgres-store.js";
import { AllowAllDiscordPolicy, RequiredGuildDiscordPolicy } from "./policy.js";
import { TicketSigner } from "./tickets.js";

const config = loadConfig();
const store = new PostgresIdentityStore(config.databaseUrl, config.databaseSsl);
const discord = new HttpDiscordClient(config.discord);
const policy =
    config.discord.requiredGuildId && config.discord.botToken
        ? new RequiredGuildDiscordPolicy(discord, {
              guildId: config.discord.requiredGuildId,
              requiredRoleIds: config.discord.requiredRoleIds,
              botToken: config.discord.botToken,
              requireCompletedScreening: config.discord.requireCompletedScreening,
          })
        : new AllowAllDiscordPolicy();
const ticketSigner = await TicketSigner.create(
    config.ticketPrivateJwk,
    config.issuer,
    config.ticketKeyId,
    config.ticketTtlSeconds,
);
const app = await buildApp({
    config,
    store,
    discord,
    policy,
    ticketSigner,
    logger: {
        level: config.nodeEnv === "production" ? "info" : "debug",
        redact: ["req.headers.authorization", "req.headers.cookie", "res.headers.set-cookie"],
        serializers: {
            // Omitting req.url prevents the OAuth bridge capability from ever
            // entering service logs if the route changes in the future.
            req(request) {
                return { method: request.method, host: request.hostname, remoteAddress: request.ip };
            },
        },
    },
});

const stop = async (signal: string) => {
    app.log.info({ signal }, "Stopping Neon Identity");
    await app.close();
    process.exit(0);
};
process.once("SIGINT", () => void stop("SIGINT"));
process.once("SIGTERM", () => void stop("SIGTERM"));

await app.listen({ host: config.host, port: config.port });
