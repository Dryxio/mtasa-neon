import { describe, expect, it } from "vitest";

import { inspectServerAsset, isPublicServerAssetAddress } from "../src/server-assets.js";

function png(width: number, height: number): Buffer {
    const bytes = Buffer.alloc(24);
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]).copy(bytes);
    bytes.write("IHDR", 12, "ascii");
    bytes.writeUInt32BE(width, 16);
    bytes.writeUInt32BE(height, 20);
    return bytes;
}

describe("server artwork validation", () => {
    it("accepts only globally routable image hosts", () => {
        expect(isPublicServerAssetAddress("8.8.8.8")).toBe(true);
        expect(isPublicServerAssetAddress("2606:4700:4700::1111")).toBe(true);
        for (const address of ["127.0.0.1", "10.0.0.1", "169.254.169.254", "198.18.0.1", "203.0.113.8", "::1", "fc00::1", "2001:db8::1"]) {
            expect(isPublicServerAssetAddress(address), address).toBe(false);
        }
    });

    it("recognizes image dimensions from magic bytes", () => {
        expect(inspectServerAsset(png(512, 512), "image/png")).toEqual({
            mimeType: "image/png",
            width: 512,
            height: 512,
        });
    });

    it("rejects spoofed MIME types and unreasonable dimensions", () => {
        expect(() => inspectServerAsset(png(512, 512), "image/jpeg")).toThrow("Content-Type does not match");
        expect(() => inspectServerAsset(png(8, 8), "image/png")).toThrow("dimensions are outside");
        expect(() => inspectServerAsset(Buffer.from("not an image"), "image/png")).toThrow("not a valid");
    });
});
