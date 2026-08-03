import { buildApp } from "./app.js";
import { loadConfig } from "./config.js";
import { PostgresBlitzDataStore } from "./postgres-store.js";

const config = loadConfig();
const store = new PostgresBlitzDataStore(config.databaseUrl, config.databaseSsl);
const app = await buildApp(store, config.apiToken);

const shutdown = async () => {
    await app.close();
    process.exit(0);
};

process.on("SIGINT", shutdown);
process.on("SIGTERM", shutdown);

await app.listen({host: config.host, port: config.port});
