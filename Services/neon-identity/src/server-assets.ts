import { createHash } from "node:crypto";
import { lookup } from "node:dns/promises";
import { isIP } from "node:net";
import { request } from "node:https";

import type { ServerAsset } from "./model.js";
import { isPublicIpv4Address } from "./server-catalog.js";

const MAX_ASSET_BYTES = 2 * 1024 * 1024;
const MAX_REDIRECTS = 3;
const REQUEST_TIMEOUT_MS = 5_000;
const MAX_DIMENSION = 4_096;
const MAX_PIXELS = 16_777_216;

export type ServerAssetFetcher = (sourceUrl: string, now: Date) => Promise<ServerAsset>;

function isPublicIpv6Address(address: string): boolean {
    const normalized = address.toLowerCase().split("%", 1)[0] ?? "";
    if (!normalized || isIP(normalized) !== 6) return false;
    if (normalized === "::" || normalized === "::1") return false;
    if (normalized.startsWith("fc") || normalized.startsWith("fd") || /^fe[89ab]/.test(normalized)) return false;
    if (normalized.startsWith("ff")) return false;

    const mapped = /^::ffff:(\d+\.\d+\.\d+\.\d+)$/.exec(normalized);
    if (mapped?.[1]) return isPublicServerAssetAddress(mapped[1]);

    // Public artwork hosts do not need transition, documentation or private
    // IPv6 ranges. Requiring global unicast makes the SSRF boundary explicit.
    const firstGroup = Number.parseInt(normalized.split(":", 1)[0] ?? "", 16);
    return firstGroup >= 0x2000 && firstGroup <= 0x3fff && !normalized.startsWith("2001:db8:");
}

export function isPublicServerAssetAddress(address: string): boolean {
    if (isIP(address) === 6) return isPublicIpv6Address(address);
    if (!isPublicIpv4Address(address)) return false;
    const [first = -1, second = -1, third = -1] = address.split(".").map(Number);
    if (first === 192 && second === 0 && third === 0) return false;
    if (first === 192 && second === 0 && third === 2) return false;
    if (first === 198 && (second === 18 || second === 19)) return false;
    if (first === 198 && second === 51 && third === 100) return false;
    if (first === 203 && second === 0 && third === 113) return false;
    return true;
}

async function resolvePublicAddresses(hostname: string): Promise<Array<{ address: string; family: 4 | 6 }>> {
    const addresses = await lookup(hostname, { all: true, verbatim: true });
    if (!addresses.length || addresses.some(({ address }) => !isPublicServerAssetAddress(address))) {
        throw new Error("Server artwork host must resolve exclusively to public addresses");
    }
    return addresses.map(({ address, family }) => ({ address, family: family as 4 | 6 }));
}

function readPngDimensions(bytes: Buffer): { mimeType: ServerAsset["mimeType"]; width: number; height: number } | null {
    const magic = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
    if (bytes.length < 24 || !bytes.subarray(0, 8).equals(magic) || bytes.toString("ascii", 12, 16) !== "IHDR") return null;
    return { mimeType: "image/png", width: bytes.readUInt32BE(16), height: bytes.readUInt32BE(20) };
}

function readJpegDimensions(bytes: Buffer): { mimeType: ServerAsset["mimeType"]; width: number; height: number } | null {
    if (bytes.length < 4 || bytes[0] !== 0xff || bytes[1] !== 0xd8) return null;
    let offset = 2;
    while (offset + 4 <= bytes.length) {
        if (bytes[offset] !== 0xff) return null;
        while (bytes[offset] === 0xff) offset += 1;
        const marker = bytes[offset++];
        if (marker === undefined || marker === 0xd9 || marker === 0xda) break;
        if (marker === 0x01 || (marker >= 0xd0 && marker <= 0xd7)) continue;
        if (offset + 2 > bytes.length) return null;
        const length = bytes.readUInt16BE(offset);
        if (length < 2 || offset + length > bytes.length) return null;
        const isStartOfFrame = marker >= 0xc0 && marker <= 0xcf && ![0xc4, 0xc8, 0xcc].includes(marker);
        if (isStartOfFrame) {
            if (length < 7) return null;
            return { mimeType: "image/jpeg", height: bytes.readUInt16BE(offset + 3), width: bytes.readUInt16BE(offset + 5) };
        }
        offset += length;
    }
    return null;
}

function readWebpDimensions(bytes: Buffer): { mimeType: ServerAsset["mimeType"]; width: number; height: number } | null {
    if (bytes.length < 30 || bytes.toString("ascii", 0, 4) !== "RIFF" || bytes.toString("ascii", 8, 12) !== "WEBP") return null;
    const chunk = bytes.toString("ascii", 12, 16);
    if (chunk === "VP8X") {
        return {
            mimeType: "image/webp",
            width: 1 + bytes.readUIntLE(24, 3),
            height: 1 + bytes.readUIntLE(27, 3),
        };
    }
    if (chunk === "VP8 " && bytes.length >= 30 && bytes[23] === 0x9d && bytes[24] === 0x01 && bytes[25] === 0x2a) {
        return {
            mimeType: "image/webp",
            width: bytes.readUInt16LE(26) & 0x3fff,
            height: bytes.readUInt16LE(28) & 0x3fff,
        };
    }
    if (chunk === "VP8L" && bytes.length >= 25 && bytes[20] === 0x2f) {
        const packed = bytes.readUInt32LE(21);
        return {
            mimeType: "image/webp",
            width: 1 + (packed & 0x3fff),
            height: 1 + ((packed >>> 14) & 0x3fff),
        };
    }
    return null;
}

export function inspectServerAsset(bytes: Buffer, responseMimeType: string): Omit<ServerAsset, "hash" | "bytes" | "createdAt"> {
    const dimensions = readPngDimensions(bytes) ?? readJpegDimensions(bytes) ?? readWebpDimensions(bytes);
    if (!dimensions) throw new Error("Server artwork is not a valid PNG, JPEG, or WebP image");
    if (responseMimeType.toLowerCase().split(";", 1)[0]?.trim() !== dimensions.mimeType) {
        throw new Error("Server artwork Content-Type does not match its image data");
    }
    if (
        dimensions.width < 16 ||
        dimensions.height < 16 ||
        dimensions.width > MAX_DIMENSION ||
        dimensions.height > MAX_DIMENSION ||
        dimensions.width * dimensions.height > MAX_PIXELS
    ) {
        throw new Error("Server artwork dimensions are outside the supported range");
    }
    return dimensions;
}

async function download(sourceUrl: URL, redirectsRemaining: number): Promise<{ bytes: Buffer; mimeType: string }> {
    if (sourceUrl.protocol !== "https:" || sourceUrl.username || sourceUrl.password || sourceUrl.port && sourceUrl.port !== "443") {
        throw new Error("Server artwork must use a standard HTTPS URL");
    }
    const addresses = await resolvePublicAddresses(sourceUrl.hostname);
    const selected = addresses[0]!;

    return new Promise((resolve, reject) => {
        const req = request(sourceUrl, {
            method: "GET",
            family: selected.family,
            headers: { accept: "image/png,image/jpeg,image/webp", "user-agent": "MTA-Neon-Identity/1" },
            servername: isIP(sourceUrl.hostname) ? undefined : sourceUrl.hostname,
            lookup: (_hostname, _options, callback) => callback(null, selected.address, selected.family),
        });
        const fail = (error: Error) => {
            req.destroy();
            reject(error);
        };
        req.setTimeout(REQUEST_TIMEOUT_MS, () => fail(new Error("Server artwork request timed out")));
        req.once("error", reject);
        req.once("response", (response) => {
            const status = response.statusCode ?? 0;
            if ([301, 302, 303, 307, 308].includes(status)) {
                response.resume();
                const location = response.headers.location;
                if (!location || redirectsRemaining <= 0) return reject(new Error("Server artwork has too many redirects"));
                const redirected = new URL(location, sourceUrl);
                void download(redirected, redirectsRemaining - 1).then(resolve, reject);
                return;
            }
            if (status !== 200) {
                response.resume();
                return reject(new Error(`Server artwork returned HTTP ${status}`));
            }
            const declaredLength = Number(response.headers["content-length"] ?? 0);
            if (declaredLength > MAX_ASSET_BYTES) {
                response.resume();
                return reject(new Error("Server artwork exceeds the 2 MiB limit"));
            }
            const chunks: Buffer[] = [];
            let total = 0;
            response.on("data", (chunk: Buffer) => {
                total += chunk.length;
                if (total > MAX_ASSET_BYTES) return fail(new Error("Server artwork exceeds the 2 MiB limit"));
                chunks.push(chunk);
            });
            response.once("end", () => resolve({ bytes: Buffer.concat(chunks, total), mimeType: String(response.headers["content-type"] ?? "") }));
            response.once("error", reject);
        });
        req.end();
    });
}

export const fetchServerAsset: ServerAssetFetcher = async (sourceUrl, now) => {
    const url = new URL(sourceUrl);
    const downloaded = await download(url, MAX_REDIRECTS);
    const inspected = inspectServerAsset(downloaded.bytes, downloaded.mimeType);
    return {
        hash: createHash("sha256").update(downloaded.bytes).digest("hex"),
        ...inspected,
        bytes: downloaded.bytes,
        createdAt: now,
    };
};
