import { describe, expect, it } from "vitest";

import { MemoryIdentityStore } from "../src/memory-store.js";
import type { RegisteredServer, ServerIdentityLeaseClaim } from "../src/model.js";

function server(id: string, endpoint: string, protocol: number, now: Date): RegisteredServer {
    return {
        id,
        endpoint,
        registryProtocol: protocol,
        httpPort: 22005,
        serverVersion: "1.7.0-9.99999",
        name: id,
        tagline: "Test",
        description: "Test",
        countries: [],
        languages: [],
        links: [],
        accent: null,
        logoAssetHash: null,
        bannerAssetHash: null,
        firstSeenAt: now,
        lastSeenAt: now,
    };
}

describe("automatic server lease catalogue", () => {
    it("reconciles V1/V2 opt-out, expiry, publication and suspension", async () => {
        const store = new MemoryIdentityStore();
        const now = new Date("2026-08-26T10:00:00.000Z");
        const endpoint = "203.0.113.10:22003";
        const identityId = `nsrv_${"a".repeat(43)}`;
        const legacyId = "community-203-0-113-10-22003";
        await store.upsertRegisteredServer(server(legacyId, endpoint, 1, now), true);
        expect(await store.listRegisteredServers(new Date(0), now)).toHaveLength(1);

        const hiddenClaim: ServerIdentityLeaseClaim = {
            serverId: identityId,
            publicKey: "b".repeat(43),
            endpoint,
            nonceHash: Buffer.alloc(32, 1),
            verifiedAt: now,
            expiresAt: new Date(now.getTime() + 300_000),
            authEnabled: true,
            published: false,
        };
        expect(await store.claimServerIdentityLease(hiddenClaim)).toBe("accepted");
        expect(await store.listRegisteredServers(new Date(0), now)).toEqual([]);
        expect(await store.isEndpointReservedByIdentity(endpoint, now)).toBe(true);

        const afterExpiry = new Date(now.getTime() + 301_000);
        expect(await store.isEndpointReservedByIdentity(endpoint, afterExpiry)).toBe(false);
        await store.upsertRegisteredServer(server(legacyId, endpoint, 1, afterExpiry), true);
        expect((await store.listRegisteredServers(new Date(0), afterExpiry))[0]?.id).toBe(legacyId);

        const publishedClaim = {
            ...hiddenClaim,
            nonceHash: Buffer.alloc(32, 2),
            verifiedAt: afterExpiry,
            expiresAt: new Date(afterExpiry.getTime() + 300_000),
            published: true,
        };
        expect(await store.claimServerIdentityLease(publishedClaim)).toBe("accepted");
        await store.upsertRegisteredServer(server(identityId, endpoint, 2, afterExpiry), true);
        expect((await store.listRegisteredServers(new Date(0), afterExpiry))[0]?.id).toBe(identityId);

        store.serverIdentities.get(identityId)!.status = "suspended";
        expect(await store.listRegisteredServers(new Date(0), afterExpiry)).toEqual([]);
    });
});
