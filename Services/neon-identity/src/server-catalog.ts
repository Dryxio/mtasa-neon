import { z } from "zod";

import { isCanonicalIpv4Endpoint, SERVER_ID_PATTERN } from "./config.js";
import type { RegisteredServer } from "./model.js";

const countryCodeSchema = z.string().regex(/^[A-Z]{2}$/);
const publicUrlSchema = z.url().max(2_048).refine((value) => new URL(value).protocol === "https:", {
    message: "Public server links must use HTTPS",
});
const artworkSourceUrlSchema = publicUrlSchema.refine((value) => {
    const url = new URL(value);
    return !url.username && !url.password && !url.hash && (!url.port || url.port === "443");
}, { message: "Server artwork must use a standard HTTPS URL without credentials or fragments" });
const publicServerLinkSchema = z
    .object({
        kind: z.enum(["website", "discord", "instagram", "x", "facebook", "vk", "youtube", "tiktok"]),
        label: z.string().trim().min(1).max(80),
        url: publicUrlSchema,
    })
    .strict();
const publicServerSchema = z
    .object({
        id: z.string().regex(SERVER_ID_PATTERN),
        endpoints: z.array(z.string().refine(isCanonicalIpv4Endpoint)).min(1).max(16),
        name: z.string().trim().min(1).max(120),
        tagline: z.string().trim().min(1).max(160),
        description: z.string().trim().min(1).max(600),
        countries: z.array(countryCodeSchema).max(16),
        languages: z.array(z.string().trim().min(1).max(80)).max(16),
        links: z.array(publicServerLinkSchema).max(12),
        accent: z.string().regex(/^#[0-9A-Fa-f]{6}$/).optional(),
        logo_url: artworkSourceUrlSchema.optional(),
        banner_url: artworkSourceUrlSchema.optional(),
    })
    .strict();
const publicServerCatalogSchema = z
    .object({
        schema_version: z.literal(1),
        servers: z.array(publicServerSchema).max(256),
    })
    .strict();

export const serverHeartbeatSchema = z
    .object({
        registry_protocol: z.literal(1),
        game_port: z.number().int().min(1).max(65_535),
        http_port: z.number().int().min(1).max(65_535),
        server_version: z.string().regex(/^[A-Za-z0-9][A-Za-z0-9._+-]{0,63}$/),
        name: z.string().trim().min(1).max(96),
        tagline: z.string().trim().max(160).default(""),
        description: z.string().trim().max(600).default(""),
        countries: z.array(countryCodeSchema).max(16).default([]),
        languages: z.array(z.string().trim().min(1).max(80)).max(16).default([]),
        links: z.array(publicServerLinkSchema).max(12).default([]),
        accent: z.string().regex(/^#[0-9A-Fa-f]{6}$/).optional(),
        logo_url: artworkSourceUrlSchema.optional(),
        banner_url: artworkSourceUrlSchema.optional(),
    })
    .strict();

export type PublicServerCatalog = z.infer<typeof publicServerCatalogSchema>;
export type ServerHeartbeat = z.infer<typeof serverHeartbeatSchema>;

export function isPublicIpv4Address(address: string): boolean {
    const octets = address.split(".").map(Number);
    if (octets.length !== 4 || octets.some((octet) => !Number.isInteger(octet) || octet < 0 || octet > 255)) return false;
    const [first, second] = octets;
    if (first === undefined || second === undefined) return false;

    // Registration is source-IP bound. Private, link-local, carrier NAT and
    // multicast ranges can never produce a public endpoint for Neon clients.
    if (first === 0 || first === 10 || first === 127 || first >= 224) return false;
    if (first === 100 && second >= 64 && second <= 127) return false;
    if (first === 169 && second === 254) return false;
    if (first === 172 && second >= 16 && second <= 31) return false;
    if (first === 192 && second === 168) return false;
    return true;
}

export function validatePublicServerCatalog(input: unknown): PublicServerCatalog {
    const parsed = publicServerCatalogSchema.safeParse(input);
    if (!parsed.success) {
        throw new Error(`Public Neon server catalogue is invalid: ${z.prettifyError(parsed.error)}`);
    }

    const serverIds = new Set<string>();
    const publishedEndpoints = new Set<string>();
    for (const server of parsed.data.servers) {
        if (serverIds.has(server.id)) {
            throw new Error(`Public Neon server catalogue contains a duplicate server ID: ${server.id}`);
        }
        serverIds.add(server.id);
        if (new Set(server.countries).size !== server.countries.length) {
            throw new Error(`Public Neon server catalogue contains duplicate countries for: ${server.id}`);
        }
        if (new Set(server.languages).size !== server.languages.length) {
            throw new Error(`Public Neon server catalogue contains duplicate languages for: ${server.id}`);
        }
        for (const endpoint of server.endpoints) {
            const address = endpoint.slice(0, endpoint.lastIndexOf(":"));
            if (!isPublicIpv4Address(address)) {
                throw new Error(`Public Neon server catalogue cannot publish a private endpoint: ${endpoint}`);
            }
            if (publishedEndpoints.has(endpoint)) {
                throw new Error(`Public Neon server catalogue contains a duplicate endpoint: ${endpoint}`);
            }
            publishedEndpoints.add(endpoint);
        }
    }
    return parsed.data;
}

export function buildPublicServerCatalog(
    servers: RegisteredServer[],
    assetUrl: (hash: string) => string = (hash) => `https://invalid.example/v1/server-registry/assets/${hash}`,
): PublicServerCatalog {
    return validatePublicServerCatalog({
        schema_version: 1,
        servers: servers.map((server) => ({
            id: server.id,
            endpoints: [server.endpoint],
            name: server.name,
            tagline: server.tagline,
            description: server.description,
            countries: server.countries,
            languages: server.languages,
            links: server.links,
            ...(server.accent ? { accent: server.accent } : {}),
            ...(server.logoAssetHash ? { logo_url: assetUrl(server.logoAssetHash) } : {}),
            ...(server.bannerAssetHash ? { banner_url: assetUrl(server.bannerAssetHash) } : {}),
        })),
    });
}
