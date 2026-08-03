import { randomUUID } from "node:crypto";
import { importJWK, SignJWT, type JWK } from "jose";

import type { NeonAccount } from "./model.js";

export class TicketSigner {
    private constructor(
        private readonly privateKey: CryptoKey,
        readonly publicJwk: JWK,
        private readonly issuer: string,
        private readonly keyId: string,
        private readonly ttlSeconds: number,
    ) {}

    static async create(privateJwk: JsonWebKey, issuer: string, keyId: string, ttlSeconds: number): Promise<TicketSigner> {
        const privateKey = await importJWK(privateJwk as JWK, "EdDSA");
        if (privateKey instanceof Uint8Array) throw new Error("Ed25519 import unexpectedly produced a symmetric key");
        if (!privateJwk.x) throw new Error("Ed25519 private JWK is missing its public x coordinate");
        const publicJwk: JWK = { kty: "OKP", crv: "Ed25519", x: privateJwk.x };
        return new TicketSigner(
            privateKey,
            { ...publicJwk, kid: keyId, alg: "EdDSA", use: "sig" },
            issuer,
            keyId,
            ttlSeconds,
        );
    }

    async sign(
        account: NeonAccount,
        serverId: string,
        serverEndpoint: string,
        now: Date,
    ): Promise<{ ticket: string; expiresAt: Date }> {
        const issuedAt = Math.floor(now.getTime() / 1000);
        const expiresAt = new Date((issuedAt + this.ttlSeconds) * 1000);
        const ticket = await new SignJWT({ discord_id: account.discordId, server_endpoint: serverEndpoint })
            .setProtectedHeader({ alg: "EdDSA", typ: "JWT", kid: this.keyId })
            .setIssuer(this.issuer)
            .setAudience(serverId)
            .setSubject(account.id)
            .setIssuedAt(issuedAt)
            .setNotBefore(issuedAt - 2)
            .setExpirationTime(Math.floor(expiresAt.getTime() / 1000))
            .setJti(randomUUID())
            .sign(this.privateKey);
        return { ticket, expiresAt };
    }
}
