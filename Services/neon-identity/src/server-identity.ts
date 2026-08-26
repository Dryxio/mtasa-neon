import { createHash, createPublicKey, timingSafeEqual, verify } from "node:crypto";

const SERVER_ID_PREFIX = "nsrv_";
const BASE64URL_32_PATTERN = /^[A-Za-z0-9_-]{43}$/;
const BASE64URL_64_PATTERN = /^[A-Za-z0-9_-]{86}$/;
const NONCE_PATTERN = /^[A-Za-z0-9_-]{22,86}$/;
const MAX_CLOCK_SKEW_SECONDS = 120;

export interface ServerHeartbeatProofHeaders {
    publicKey: string;
    timestamp: string;
    nonce: string;
    signature: string;
}

export type ServerHeartbeatProofResult =
    | { valid: true; serverId: string; publicKey: string; nonce: string }
    | { valid: false };

function decodeCanonicalBase64Url(value: string, expectedBytes: number): Buffer | null {
    try {
        const decoded = Buffer.from(value, "base64url");
        if (decoded.length !== expectedBytes || decoded.toString("base64url") !== value) return null;
        return decoded;
    } catch {
        return null;
    }
}

export function deriveServerId(publicKey: string): string | null {
    if (!BASE64URL_32_PATTERN.test(publicKey) || !decodeCanonicalBase64Url(publicKey, 32)) return null;
    const digest = createHash("sha256").update(Buffer.from(publicKey, "base64url")).digest("base64url");
    return `${SERVER_ID_PREFIX}${digest}`;
}

export function serverHeartbeatSigningMessage(timestamp: string, nonce: string, rawBody: string): string {
    const bodyHash = createHash("sha256").update(rawBody, "utf8").digest("hex");
    return `neon-server-heartbeat-v2\n${timestamp}\n${nonce}\n${bodyHash}`;
}

export function verifyServerHeartbeatProof(
    headers: ServerHeartbeatProofHeaders,
    rawBody: string,
    now: Date,
): ServerHeartbeatProofResult {
    if (
        !BASE64URL_32_PATTERN.test(headers.publicKey) ||
        !BASE64URL_64_PATTERN.test(headers.signature) ||
        !NONCE_PATTERN.test(headers.nonce) ||
        !/^[1-9][0-9]{9}$/.test(headers.timestamp)
    ) {
        return { valid: false };
    }
    const publicKeyBytes = decodeCanonicalBase64Url(headers.publicKey, 32);
    const signature = decodeCanonicalBase64Url(headers.signature, 64);
    if (!publicKeyBytes || !signature) return { valid: false };

    const issuedAt = Number(headers.timestamp);
    const nowSeconds = Math.floor(now.getTime() / 1_000);
    if (!Number.isSafeInteger(issuedAt) || Math.abs(nowSeconds - issuedAt) > MAX_CLOCK_SKEW_SECONDS) {
        return { valid: false };
    }

    try {
        const publicKey = createPublicKey({
            key: { kty: "OKP", crv: "Ed25519", x: headers.publicKey },
            format: "jwk",
        });
        const message = Buffer.from(serverHeartbeatSigningMessage(headers.timestamp, headers.nonce, rawBody), "utf8");
        if (!verify(null, message, publicKey, signature)) return { valid: false };
    } catch {
        return { valid: false };
    }

    const serverId = deriveServerId(headers.publicKey);
    if (!serverId) return { valid: false };
    return { valid: true, serverId, publicKey: headers.publicKey, nonce: headers.nonce };
}

export function constantTimeServerIdEqual(left: string, right: string): boolean {
    const leftBytes = Buffer.from(left);
    const rightBytes = Buffer.from(right);
    return leftBytes.length === rightBytes.length && timingSafeEqual(leftBytes, rightBytes);
}
