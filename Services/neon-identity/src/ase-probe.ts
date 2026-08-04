import { createSocket } from "node:dgram";

export type AseProbe = (address: string, gamePort: number, serverVersion: string) => Promise<boolean>;

const NEON_REGISTRY_RULE = Buffer.from("NeonRegistryProtocol", "ascii");

export function hasNeonRegistryProtocol(message: Buffer): boolean {
    const position = message.indexOf(NEON_REGISTRY_RULE);
    if (position < 1) return false;

    // MTA's EYE1 reply prefixes strings with their length including the
    // terminator. The matching value is encoded immediately after the key.
    const valuePosition = position + NEON_REGISTRY_RULE.length;
    return message[position - 1] === NEON_REGISTRY_RULE.length + 1 &&
        message[valuePosition] === 2 && message[valuePosition + 1] === "1".charCodeAt(0);
}

export const probeMtaAse: AseProbe = async (address, gamePort, serverVersion) => {
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
                socket.send(Buffer.from("s"), asePort, address, (error) => {
                    if (error) finish(false);
                });
                return;
            }
            finish(hasNeonRegistryProtocol(message));
        });
        socket.send(Buffer.from("v"), asePort, address, (error) => {
            if (error) finish(false);
        });
    });
};
