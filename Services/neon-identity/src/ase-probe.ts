import { createSocket } from "node:dgram";

export type AseProbe = (address: string, gamePort: number, serverVersion: string, expectedServerId?: string) => Promise<boolean>;

const NEON_REGISTRY_RULE = Buffer.from("NeonRegistryProtocol", "ascii");

function hasAseRule(message: Buffer, name: string, value: string): boolean {
    const key = Buffer.from(name, "ascii");
    const position = message.indexOf(key);
    if (position < 1) return false;
    const valuePosition = position + key.length;
    const valueBytes = Buffer.from(value, "ascii");
    return message[position - 1] === key.length + 1 &&
        message[valuePosition] === valueBytes.length + 1 &&
        message.subarray(valuePosition + 1, valuePosition + 1 + valueBytes.length).equals(valueBytes);
}

export function hasNeonRegistryProtocol(message: Buffer): boolean {
    return hasAseRule(message, NEON_REGISTRY_RULE.toString("ascii"), "1") || hasAseRule(message, NEON_REGISTRY_RULE.toString("ascii"), "2");
}

export function hasNeonServerIdentity(message: Buffer, serverId: string): boolean {
    return hasAseRule(message, "NeonRegistryProtocol", "2") && hasAseRule(message, "NeonIdentityServerId", serverId);
}

export const probeMtaAse: AseProbe = async (address, gamePort, serverVersion, expectedServerId) => {
    const asePort = gamePort + 123;
    if (asePort > 65_535) return false;
    const expectedVersion = serverVersion.split(".", 2).join(".");

    return new Promise<boolean>((resolve) => {
        const socket = createSocket("udp4");
        let settled = false;
        let stage: "version" | "rules" = "version";
        const finish = (verified: boolean) => {
            if (settled) return;
            settled = true;
            clearTimeout(timer);
            socket.close();
            resolve(verified);
        };
        const timer = setTimeout(() => finish(false), 2_500);
        timer.unref();
        socket.once("error", () => finish(false));
        socket.on("message", (message) => {
            if (stage === "version") {
                if (!message.toString("ascii").startsWith(expectedVersion)) return finish(false);
                stage = "rules";
                socket.send(Buffer.from("s"), (error) => {
                    if (error) finish(false);
                });
                return;
            }
            const expectedProtocol = expectedServerId ? "2" : "1";
            finish(expectedServerId ? hasNeonServerIdentity(message, expectedServerId) : hasAseRule(message, "NeonRegistryProtocol", expectedProtocol));
        });
        // A connected UDP socket only accepts responses from the exact game
        // endpoint being verified; unrelated datagrams cannot satisfy either
        // stage of this security-sensitive proof.
        socket.connect(asePort, address, () => {
            socket.send(Buffer.from("v"), (error) => {
                if (error) finish(false);
            });
        });
    });
};
