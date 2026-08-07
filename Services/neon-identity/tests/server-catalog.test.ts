import { describe, expect, it } from "vitest";

import type { RegisteredServer } from "../src/model.js";
import {
    buildPublicServerCatalog,
    serverHeartbeatSchema,
    type PublicServerCatalog,
    validatePublicServerCatalog,
} from "../src/server-catalog.js";

function testCatalog(): PublicServerCatalog {
    return {
        schema_version: 1,
        servers: [
            {
                id: "community-203-0-113-10-22003",
                endpoints: ["203.0.113.10:22003"],
                name: "Community Neon",
                tagline: "A Neon community server.",
                description: "Configured by the server owner.",
                countries: ["GB", "FR"],
                languages: ["English", "French"],
                links: [],
            },
        ],
    };
}

describe("dynamic Neon server catalogue", () => {
    it("allows an empty catalogue when no heartbeat is currently active", () => {
        expect(validatePublicServerCatalog({ schema_version: 1, servers: [] })).toEqual({ schema_version: 1, servers: [] });
    });

    it("builds public metadata from active registered-server records", () => {
        const now = new Date("2026-08-04T12:00:00.000Z");
        const server: RegisteredServer = {
            id: "community-203-0-113-10-22003",
            endpoint: "203.0.113.10:22003",
            registryProtocol: 1,
            httpPort: 22005,
            serverVersion: "1.7.0-9.99999",
            name: "Community Neon",
            tagline: "A Neon community server.",
            description: "Configured by the server owner.",
            countries: ["GB", "FR"],
            languages: ["English", "French"],
            links: [],
            accent: null,
            logoAssetHash: null,
            bannerAssetHash: null,
            firstSeenAt: now,
            lastSeenAt: now,
        };
        expect(buildPublicServerCatalog([server])).toEqual(testCatalog());
    });

    it("rejects private endpoints and duplicate public identities or metadata", () => {
        const privateEndpoint = testCatalog();
        privateEndpoint.servers[0]!.endpoints = ["192.168.1.20:22003"];
        expect(() => validatePublicServerCatalog(privateEndpoint)).toThrow("cannot publish a private endpoint");

        const duplicateId = testCatalog();
        duplicateId.servers.push({ ...structuredClone(duplicateId.servers[0]!), endpoints: ["203.0.113.11:22003"] });
        expect(() => validatePublicServerCatalog(duplicateId)).toThrow("duplicate server ID");

        const duplicateCountry = testCatalog();
        duplicateCountry.servers[0]!.countries.push("GB");
        expect(() => validatePublicServerCatalog(duplicateCountry)).toThrow("duplicate countries");
    });

    it("validates the versioned heartbeat and HTTPS-only links", () => {
        expect(
            serverHeartbeatSchema.safeParse({
                registry_protocol: 1,
                game_port: 22003,
                http_port: 22005,
                server_version: "1.7.0-9.99999",
                name: "Community Neon",
            }).success,
        ).toBe(true);
        expect(
            serverHeartbeatSchema.safeParse({
                registry_protocol: 1,
                game_port: 22003,
                http_port: 22005,
                server_version: "1.7.0-9.99999",
                name: "Community Neon",
                links: [{ kind: "website", label: "Unsafe", url: "http://example.com" }],
            }).success,
        ).toBe(false);
        expect(
            serverHeartbeatSchema.safeParse({
                registry_protocol: 1,
                game_port: 22003,
                http_port: 22005,
                server_version: "1.7.0-9.99999",
                name: "Community Neon",
                logo_url: "http://example.com/logo.png",
            }).success,
        ).toBe(false);
    });
});
