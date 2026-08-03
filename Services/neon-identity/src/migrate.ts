import { readdir, readFile } from "node:fs/promises";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

import { Client } from "pg";

import { loadConfig } from "./config.js";

const config = loadConfig();
const client = new Client({
    connectionString: config.databaseUrl,
    ssl: config.databaseSsl ? { rejectUnauthorized: true } : undefined,
});
const migrationsDirectory = join(dirname(fileURLToPath(import.meta.url)), "../migrations");

await client.connect();
try {
    await client.query("SELECT pg_advisory_lock(hashtext('neon_identity_migrations'))");
    await client.query(
        `CREATE TABLE IF NOT EXISTS neon_schema_migrations (
            name TEXT PRIMARY KEY,
            applied_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
        )`,
    );
    const applied = new Set(
        (await client.query<{ name: string }>("SELECT name FROM neon_schema_migrations")).rows.map((row) => row.name),
    );
    const files = (await readdir(migrationsDirectory)).filter((name) => name.endsWith(".sql")).sort();
    for (const name of files) {
        if (applied.has(name)) continue;
        const sql = await readFile(join(migrationsDirectory, name), "utf8");
        await client.query("BEGIN");
        try {
            await client.query(sql);
            await client.query("INSERT INTO neon_schema_migrations (name) VALUES ($1)", [name]);
            await client.query("COMMIT");
            process.stdout.write(`Applied ${name}\n`);
        } catch (error) {
            await client.query("ROLLBACK");
            throw error;
        }
    }
} finally {
    await client.query("SELECT pg_advisory_unlock(hashtext('neon_identity_migrations'))").catch(() => undefined);
    await client.end();
}
