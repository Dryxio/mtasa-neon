import { z } from "zod";

const booleanFromString = z
    .enum(["true", "false"])
    .default("false")
    .transform((value) => value === "true");

const environmentSchema = z.object({
    NODE_ENV: z.enum(["development", "test", "production"]).default("development"),
    HOST: z.string().default("127.0.0.1"),
    PORT: z.coerce.number().int().min(1).max(65_535).default(8091),
    DATABASE_URL: z.string().min(1),
    DATABASE_SSL: booleanFromString,
    BLITZ_API_TOKEN: z.string().min(32),
});

export interface BlitzDataConfig {
    nodeEnv: "development" | "test" | "production";
    host: string;
    port: number;
    databaseUrl: string;
    databaseSsl: boolean;
    apiToken: string;
}

export function loadConfig(environment: NodeJS.ProcessEnv = process.env): BlitzDataConfig {
    const parsed = environmentSchema.parse(environment);
    if (parsed.NODE_ENV === "production" && parsed.HOST !== "127.0.0.1" && parsed.HOST !== "::1") {
        throw new Error("The Blitz data service must bind to a loopback address in production");
    }
    return {
        nodeEnv: parsed.NODE_ENV,
        host: parsed.HOST,
        port: parsed.PORT,
        databaseUrl: parsed.DATABASE_URL,
        databaseSsl: parsed.DATABASE_SSL,
        apiToken: parsed.BLITZ_API_TOKEN,
    };
}
