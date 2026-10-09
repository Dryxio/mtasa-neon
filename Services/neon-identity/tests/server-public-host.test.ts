import { describe, expect, it } from "vitest";
import { publicHostSchema, serverHeartbeatSchema } from "../src/server-catalog.js";
import { resolvePublicEndpoint } from "../src/server-public-host.js";

describe("public relay hostname", () => {
    it.each(["example.com", "server.myroleplay.world", "xn--bcher-kva.example"])("accepts %s", (host) => {
        expect(publicHostSchema.safeParse(host).success).toBe(true);
    });
    it.each(["127.0.0.1", "https://example.com", "example.com:22003", "a..com", "-a.com", "a-.com", "example.com/path", "example.com\u0000", "a.123", "localhost", "a".repeat(64) + ".com"])("rejects %s", (host) => {
        expect(publicHostSchema.safeParse(host).success).toBe(false);
    });
    it("requires signed protocol for public endpoint declarations", () => {
        expect(serverHeartbeatSchema.safeParse({ registry_protocol: 1, game_port: 22003, http_port: 22005,
            server_version: "1.7", name: "test", public_host: "example.com" }).success).toBe(false);
    });
    it("handles DNS failure and deduplicates a single A record", async () => {
        expect(await resolvePublicEndpoint("example.com", async () => { throw new Error("timeout"); })).toBeNull();
        expect(await resolvePublicEndpoint("example.com", async () => ["203.0.113.10", "203.0.113.10"])).toBe("203.0.113.10");
    });
});
