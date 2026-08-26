import { describe, expect, it } from "vitest";

import { hasNeonRegistryProtocol, hasNeonServerIdentity } from "../src/ase-probe.js";

function rule(name: string, value: string): Buffer {
    const key = Buffer.from(name, "ascii");
    const bytes = Buffer.from(value, "ascii");
    return Buffer.concat([Buffer.from([key.length + 1]), key, Buffer.from([bytes.length + 1]), bytes]);
}

describe("Neon ASE registry marker", () => {
    it("accepts the exact MTA rule encoding", () => {
        const marker = Buffer.from("NeonRegistryProtocol", "ascii");
        expect(hasNeonRegistryProtocol(Buffer.concat([Buffer.from([marker.length + 1]), marker, Buffer.from([2, 0x31])]))).toBe(true);
    });

    it("rejects a server name or malformed rule that merely contains the marker", () => {
        expect(hasNeonRegistryProtocol(Buffer.from("NeonRegistryProtocol", "ascii"))).toBe(false);
        const marker = Buffer.from("NeonRegistryProtocol", "ascii");
        expect(hasNeonRegistryProtocol(Buffer.concat([Buffer.from([marker.length + 1]), marker, Buffer.from([2, 0x30])]))).toBe(false);
    });

    it("binds protocol 2 discovery to the exact signed server identity", () => {
        const expected = "nsrv_0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefgh";
        const response = Buffer.concat([rule("NeonRegistryProtocol", "2"), rule("NeonIdentityServerId", expected)]);

        expect(hasNeonServerIdentity(response, expected)).toBe(true);
        expect(hasNeonServerIdentity(response, `${expected}x`)).toBe(false);
        expect(hasNeonServerIdentity(Buffer.concat([rule("NeonRegistryProtocol", "1"), rule("NeonIdentityServerId", expected)]), expected)).toBe(false);
    });
});
