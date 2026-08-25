#!/usr/bin/env node
import { build } from "esbuild";
import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { basename, join, resolve } from "node:path";
import process from "node:process";
import { runInNewContext } from "node:vm";
import YAML from "yaml";

const UPSTREAM_REPOSITORY = "https://github.com/multitheftauto/wiki.multitheftauto.com.git";
const NEON_REPOSITORY = "https://github.com/Dryxio/wiki.mtasa-neon.com.git";
const UPSTREAM_LICENSE = "GFDL-1.3-or-later";
const NEON_LICENSE = "GFDL-1.3-or-later";

function usage(message) {
  if (message) process.stderr.write(`${message}\n`);
  process.stderr.write(
    "usage: import-sources.mjs --upstream-wiki PATH --upstream-revision SHA " +
    "--neon-wiki PATH --neon-revision SHA --output FILE\n",
  );
  process.exit(2);
}

function parseArguments(argv) {
  const result = {};
  for (let index = 0; index < argv.length; index += 2) {
    const key = argv[index];
    const value = argv[index + 1];
    if (!key?.startsWith("--") || value === undefined) usage("invalid arguments");
    result[key.slice(2)] = value;
  }
  for (const key of ["upstream-wiki", "upstream-revision", "neon-wiki", "neon-revision", "output"]) {
    if (!result[key]) usage(`missing --${key}`);
  }
  return result;
}

function git(repository, ...args) {
  return execFileSync("git", args, { cwd: repository, encoding: "utf8" }).trim();
}

function verifyRevision(repository, requested) {
  const revision = git(repository, "rev-parse", "--verify", `${requested}^{commit}`);
  if (!/^[0-9a-f]{40}$/.test(revision)) throw new Error(`invalid Git revision: ${revision}`);
  return revision;
}

function gitText(repository, revision, path) {
  return execFileSync("git", ["show", `${revision}:${path}`], {
    cwd: repository,
    encoding: "utf8",
    maxBuffer: 16 * 1024 * 1024,
  });
}

function gitFiles(repository, revision, directory, suffix) {
  return git(repository, "ls-tree", "-r", "--name-only", revision, "--", directory)
    .split("\n")
    .filter((path) => path.endsWith(suffix))
    .sort((a, b) => a.localeCompare(b, "en"));
}

function text(value) {
  return typeof value === "string" && value.trim() ? value.trim() : undefined;
}

function scalar(value) {
  if (value === undefined || value === null) return undefined;
  return String(value);
}

function cleanObject(value) {
  if (Array.isArray(value)) return value.map(cleanObject);
  if (!value || typeof value !== "object") return value;
  return Object.fromEntries(
    Object.entries(value)
      .filter(([, item]) => item !== undefined)
      .map(([key, item]) => [key, cleanObject(item)]),
  );
}

function normalizeParameters(parameters = []) {
  if (!Array.isArray(parameters)) return [];
  return parameters.map((parameter, index) => cleanObject({
    name: text(parameter?.name) ?? `arg${index + 1}`,
    type: text(parameter?.type) ?? "unknown",
    optional: Boolean(parameter?.optional) || parameter?.default !== undefined,
    default: scalar(parameter?.default),
    description: text(parameter?.description),
  }));
}

function normalizeReturns(returns) {
  const description = text(returns?.description);
  const values = Array.isArray(returns?.values) ? returns.values : [];
  return values.map((value) => cleanObject({
    name: text(value?.name),
    type: text(value?.type) ?? "unknown",
    description: text(value?.description) ?? description,
  }));
}

function normalizeVersion(version) {
  if (!version || typeof version !== "object") return undefined;
  const result = cleanObject({
    added: scalar(version.added),
    updated: scalar(version.updated),
    deprecated: scalar(version.deprecated),
    removed: scalar(version.removed),
    replacement: text(version.replacement),
  });
  return Object.keys(result).length ? result : undefined;
}

function normalizeOop(oop) {
  if (!oop || typeof oop !== "object") return undefined;
  const result = cleanObject({
    element: text(oop.element),
    method: text(oop.method),
    constructorClass: text(oop.constructorclass),
    staticClass: text(oop.staticclass),
  });
  return Object.keys(result).length ? result : undefined;
}

function normalizeFunctionContract(section, side, path) {
  return cleanObject({
    side,
    description: text(section.description),
    parameters: normalizeParameters(section.parameters),
    returns: normalizeReturns(section.returns),
    oop: normalizeOop(section.oop),
    version: normalizeVersion(section.version),
    requiresReview: Boolean(section.requires_review),
    sourcePath: path,
  });
}

function parseYaml(source, path) {
  try {
    // The upstream corpus contains at least one self-merge inside an anchor.
    // Keeping aliases unmerged lets us resolve only the top-level side contract
    // and avoids turning malformed documentation into a recursive object walk.
    return YAML.parse(source, { merge: false, prettyErrors: true });
  } catch (error) {
    throw new Error(`${path}: ${error.message}`);
  }
}

function resolveSideSection(document, side) {
  const section = document?.[side];
  if (!section || typeof section !== "object") return undefined;
  const inherited = section["<<"];
  const own = Object.fromEntries(Object.entries(section).filter(([key]) => key !== "<<"));
  if (!inherited || typeof inherited !== "object" || Array.isArray(inherited)) return own;
  const base = Object.fromEntries(Object.entries(inherited).filter(([key]) => key !== "<<"));
  return { ...base, ...own };
}

function importUpstreamFunctions(repository, revision) {
  const functions = [];
  for (const path of gitFiles(repository, revision, "functions", ".yaml")) {
    const document = parseYaml(gitText(repository, revision, path), path);
    const contracts = [];
    for (const side of ["shared", "client", "server"]) {
      const section = resolveSideSection(document, side);
      if (section && typeof section === "object" && text(section.name)) {
        contracts.push(normalizeFunctionContract(section, side, path));
      }
    }
    if (!contracts.length) continue;
    const names = [...new Set(contracts.map((contract) => {
      const section = resolveSideSection(document, contract.side);
      return text(section.name);
    }))];
    if (names.length !== 1) throw new Error(`${path}: side variants disagree on the function name`);
    functions.push({ name: names[0], provider: "mta", contracts });
  }
  return functions.sort(compareEntities);
}

function importUpstreamEvents(repository, revision) {
  const events = [];
  for (const path of gitFiles(repository, revision, "events", ".yaml")) {
    const document = parseYaml(gitText(repository, revision, path), path);
    const name = text(document?.name);
    if (!name || document?.redirect) continue;
    const side = ["client", "server", "shared"].includes(document.type) ? document.type : "shared";
    events.push(cleanObject({
      name,
      provider: "mta",
      side,
      description: text(document.description),
      parameters: normalizeParameters(document.parameters),
      sourceElement: cleanObject({
        type: text(document.source_element?.type),
        description: text(document.source_element?.description),
      }),
      canceling: text(document.canceling),
      version: normalizeVersion(document.version),
      requiresReview: Boolean(document.requires_review),
      sourcePath: path,
    }));
  }
  return events.sort(compareEntities);
}

function normalizeOopOnlyMethods(methods) {
  if (!Array.isArray(methods)) return [];
  return methods.map((method) => cleanObject({
    name: text(method?.name),
    signature: text(method?.signature),
    parameters: normalizeParameters(method?.parameters),
  })).filter((method) => method.name);
}

function importNamedDocuments(repository, revision, directory, kind) {
  const result = [];
  for (const path of gitFiles(repository, revision, directory, ".yaml")) {
    const document = parseYaml(gitText(repository, revision, path), path);
    const fallbackName = basename(path, ".yaml");
    const name = text(document?.name) ?? fallbackName;
    result.push(cleanObject({
      name,
      provider: "mta",
      description: text(document?.description),
      redirect: text(document?.redirect),
      oopOnlyMethods: kind === "element" ? normalizeOopOnlyMethods(document?.oop_only_methods) : undefined,
      requiresReview: Boolean(document?.requires_review),
      sourcePath: path,
    }));
  }
  return result.sort(compareEntities);
}

function parseNeonReturnTypes(signature, name) {
  const marker = `${name}(`;
  const offset = signature.indexOf(marker);
  if (offset < 0) return [{ type: "unknown" }];
  const prefix = signature.slice(0, offset).trim();
  if (!prefix) return [];
  return prefix.split(",").map((part) => ({ type: part.trim() || "unknown" }));
}

function normalizeNeonFunction(entry) {
  const signature = text(entry.signature) ?? `${entry.name}(...)`;
  const oopMethods = Array.isArray(entry.oop) ? [...new Set(entry.oop.filter((item) => typeof item === "string"))] : [];
  return cleanObject({
    name: entry.name,
    provider: "neon",
    category: text(entry.category),
    extension: Boolean(entry.extension),
    contracts: [cleanObject({
      side: entry.side,
      signature,
      description: text(entry.summary),
      parameters: normalizeParameters(entry.arguments),
      returns: parseNeonReturnTypes(signature, entry.name),
      returnDescription: text(entry.returns),
      oopMethods,
      notes: Array.isArray(entry.notes) ? entry.notes.filter((item) => typeof item === "string") : [],
      requiresReview: false,
      example: text(entry.example),
      sourcePath: text(entry.source),
      sourceCommit: text(entry.commit),
      testResource: text(entry.test),
    })],
  });
}

async function importNeonFunctions(repository, revision) {
  const temporary = await mkdtemp(join(tmpdir(), "neon-api-import-"));
  try {
    const directory = join(temporary, "data");
    await mkdir(directory, { recursive: true });
    for (const name of ["neon-functions-base.ts", "neon-functions.ts"]) {
      await writeFile(join(directory, name), gitText(repository, revision, `web/src/data/${name}`), "utf8");
    }
    const bundled = await build({
      entryPoints: [join(directory, "neon-functions.ts")],
      bundle: true,
      format: "iife",
      globalName: "NeonDataBundle",
      platform: "browser",
      write: false,
      logLevel: "silent",
    });
    const sandbox = Object.create(null);
    runInNewContext(bundled.outputFiles[0].text, sandbox, {
      timeout: 2_000,
      contextCodeGeneration: { strings: false, wasm: false },
    });
    const loaded = sandbox.NeonDataBundle;
    if (!Array.isArray(loaded.neonFunctions)) throw new Error("Neon wiki does not export neonFunctions as an array");
    return loaded.neonFunctions.map(normalizeNeonFunction).sort(compareEntities);
  } finally {
    await rm(temporary, { recursive: true, force: true });
  }
}

function compareEntities(left, right) {
  return left.name.localeCompare(right.name, "en", { sensitivity: "variant" });
}

function digestEntities(document) {
  const payload = canonicalStringify({
    functions: document.functions,
    events: document.events,
    elements: document.elements,
    types: document.types,
  });
  return createHash("sha256").update(payload).digest("hex");
}

function canonicalStringify(value) {
  if (Array.isArray(value)) return `[${value.map(canonicalStringify).join(",")}]`;
  if (value && typeof value === "object") {
    return `{${Object.keys(value).sort().map((key) => `${JSON.stringify(key)}:${canonicalStringify(value[key])}`).join(",")}}`;
  }
  return JSON.stringify(value);
}

async function main() {
  const args = parseArguments(process.argv.slice(2));
  const upstreamRoot = resolve(args["upstream-wiki"]);
  const neonRoot = resolve(args["neon-wiki"]);
  const upstreamRevision = verifyRevision(upstreamRoot, args["upstream-revision"]);
  const neonRevision = verifyRevision(neonRoot, args["neon-revision"]);

  const upstreamFunctions = importUpstreamFunctions(upstreamRoot, upstreamRevision);
  const neonFunctions = await importNeonFunctions(neonRoot, neonRevision);
  const document = {
    schemaVersion: "1.0.0",
    sources: {
      upstreamWiki: {
        repository: UPSTREAM_REPOSITORY,
        revision: upstreamRevision,
        license: UPSTREAM_LICENSE,
      },
      neonWiki: {
        repository: NEON_REPOSITORY,
        revision: neonRevision,
        license: NEON_LICENSE,
      },
    },
    functions: [...upstreamFunctions, ...neonFunctions].sort(compareEntities),
    events: importUpstreamEvents(upstreamRoot, upstreamRevision),
    elements: importNamedDocuments(upstreamRoot, upstreamRevision, "elements", "element"),
    types: importNamedDocuments(upstreamRoot, upstreamRevision, "types", "type"),
  };
  document.digest = digestEntities(document);
  const output = resolve(args.output);
  await writeFile(output, `${JSON.stringify(document, null, 2)}\n`, "utf8");
  process.stdout.write(`${JSON.stringify({
    status: "pass",
    output,
    digest: document.digest,
    counts: Object.fromEntries(["functions", "events", "elements", "types"].map((key) => [key, document[key].length])),
  })}\n`);
}

main().catch((error) => {
  process.stderr.write(`${error.stack ?? error.message}\n`);
  process.exit(1);
});
