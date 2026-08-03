import { generateKeyPairSync, randomBytes } from "node:crypto";

const { privateKey } = generateKeyPairSync("ed25519");
const privateJwk = privateKey.export({ format: "jwk" });

process.stdout.write(`NEON_TICKET_PRIVATE_JWK='${JSON.stringify(privateJwk)}'\n`);
process.stdout.write(`OAUTH_COOKIE_KEY=${randomBytes(32).toString("base64url")}\n`);
