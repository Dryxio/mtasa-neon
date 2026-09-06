from __future__ import annotations


PACKAGE_VERSION = "1.0.0"
MINIMUM_PYTHON_VERSION = "3.10.0"

_NEONLIB = {
    "__init__.py", "anchored.py", "catalogue.py", "components.py", "context.py",
    "discovery.py", "initialize.py", "jsonio.py", "luals.py", "mutation.py",
    "package_contract.py", "portable.py", "project.py", "proof.py", "runtime.py",
    "scenario.py", "schema.py", "supervisor.py", "winfs.py",
}

_SCHEMAS = {
    "neon-agent-context.schema.json", "neon-api-index.schema.json", "neon-api.schema.json",
    "neon-artifact-index.schema.json", "neon-artifact.schema.json", "neon-assertion.schema.json",
    "neon-check-result.schema.json", "neon-component.schema.json", "neon-evidence.schema.json",
    "neon-init-result.schema.json", "neon-mutation-result.schema.json",
    "neon-package-manifest.schema.json", "neon-probe-config.schema.json",
    "neon-probe-install-result.schema.json", "neon-probe-report.schema.json",
    "neon-project-api.schema.json", "neon-project.schema.json", "neon-proof-result.schema.json",
    "neon-runtime-compare-result.schema.json", "neon-runtime-snapshot.schema.json",
    "neon-scenario-verify-result.schema.json", "neon-semantic-snapshot.schema.json",
    "neon-supervisor-result.schema.json", "neon-supervisor-session.schema.json",
    "neon-test-result.schema.json", "neon-test.schema.json",
}

REQUIRED_PACKAGE_PATHS = frozenset({
    "LICENSE",
    "neon",
    "neon.cmd",
    "Tools/neon-api/README.md",
    "Tools/neon-api/THIRD_PARTY_NOTICES.md",
    "Tools/neon-api/neon-api.json",
    "Tools/neon-api/event-emission-evidence.json",
    "Tools/neon-api/neon.py",
    "Tools/neon-api/licenses/GFDL-1.3.md",
    "Tools/neon-api/runtime-probe/client.lua",
    "Tools/neon-api/runtime-probe/meta.xml",
    "Tools/neon-api/runtime-probe/server.lua",
    *(f"Tools/neon-api/neonlib/{name}" for name in _NEONLIB),
    *(f"Tools/neon-api/schemas/{name}" for name in _SCHEMAS),
})
