import { readFile } from "node:fs/promises";

import { describe, expect, it } from "vitest";

import { OFFICIAL_NEON_ISSUER, OFFICIAL_NEON_TICKET_KEY_ID, OFFICIAL_NEON_TICKET_PUBLIC_KEY } from "../src/official-trust.js";

describe("official automatic-server trust bundle", () => {
    it("stays synchronized with the native verifier", async () => {
        const verifierSource = await readFile(
            new URL("../../../Server/mods/deathmatch/logic/CNeonIdentityTicket.cpp", import.meta.url),
            "utf8",
        );
        expect(verifierSource).toContain(`OFFICIAL_ISSUER = "${OFFICIAL_NEON_ISSUER}"`);
        expect(verifierSource).toContain(`{"${OFFICIAL_NEON_TICKET_KEY_ID}", "${OFFICIAL_NEON_TICKET_PUBLIC_KEY}"}`);
    });
});
