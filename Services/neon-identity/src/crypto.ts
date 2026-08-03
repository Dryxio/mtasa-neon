import { createCipheriv, createDecipheriv, createHash, randomBytes, timingSafeEqual } from "node:crypto";

const COOKIE_VERSION = "v1";

export function randomToken(bytes = 32): string {
    return randomBytes(bytes).toString("base64url");
}

export function hashToken(value: string): Buffer {
    return createHash("sha256").update(value, "utf8").digest();
}

export function constantTimeStringEqual(left: string, right: string): boolean {
    const leftHash = hashToken(left);
    const rightHash = hashToken(right);
    return timingSafeEqual(leftHash, rightHash);
}

export function pkceChallenge(verifier: string): string {
    return hashToken(verifier).toString("base64url");
}

export function sealJson(value: unknown, key: Buffer): string {
    const iv = randomBytes(12);
    const cipher = createCipheriv("aes-256-gcm", key, iv);
    cipher.setAAD(Buffer.from(COOKIE_VERSION, "utf8"));
    const ciphertext = Buffer.concat([cipher.update(JSON.stringify(value), "utf8"), cipher.final()]);
    const tag = cipher.getAuthTag();
    return [COOKIE_VERSION, iv.toString("base64url"), ciphertext.toString("base64url"), tag.toString("base64url")].join(".");
}

export function openJson<T>(sealed: string, key: Buffer): T | null {
    const [version, ivValue, ciphertextValue, tagValue, extra] = sealed.split(".");
    if (version !== COOKIE_VERSION || !ivValue || !ciphertextValue || !tagValue || extra !== undefined) return null;

    try {
        const decipher = createDecipheriv("aes-256-gcm", key, Buffer.from(ivValue, "base64url"));
        decipher.setAAD(Buffer.from(COOKIE_VERSION, "utf8"));
        decipher.setAuthTag(Buffer.from(tagValue, "base64url"));
        const plaintext = Buffer.concat([
            decipher.update(Buffer.from(ciphertextValue, "base64url")),
            decipher.final(),
        ]);
        return JSON.parse(plaintext.toString("utf8")) as T;
    } catch {
        return null;
    }
}
