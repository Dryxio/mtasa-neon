import { Resolver } from "node:dns/promises";
import { isPublicIpv4Address, publicHostSchema } from "./server-catalog.js";

export type PublicHostResolver = (host: string) => Promise<string[]>;

export const resolvePublicHost: PublicHostResolver = async (host) => {
    // Bound DNS work and use the resolved literal for ASE and publication, so
    // a later DNS answer cannot redirect the verification to another address.
    const resolver = new Resolver({ timeout: 2_000, tries: 1 });
    try {
        return await resolver.resolve4(host);
    } finally {
        resolver.cancel();
    }
};

export async function resolvePublicEndpoint(host: string, resolve: PublicHostResolver): Promise<string | null> {
    if (!publicHostSchema.safeParse(host).success) return null;
    try {
        const addresses = [...new Set(await resolve(host))];
        // One endpoint is leased per identity. Round-robin DNS could otherwise
        // copy a link that connects to an endpoint with no matching ticket.
        if (addresses.length !== 1 || !isPublicIpv4Address(addresses[0]!)) return null;
        return addresses[0]!;
    } catch {
        return null;
    }
}
