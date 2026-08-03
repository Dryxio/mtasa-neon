#!/usr/bin/env bash
set -euo pipefail

if [[ ${EUID} -ne 0 ]]; then
    printf '%s\n' "Run this command through sudo." >&2
    exit 1
fi

env_file=/etc/neon-identity/neon-identity.env
if [[ ! -f ${env_file} ]]; then
    printf '%s\n' "Neon Identity has not been configured yet." >&2
    exit 1
fi

printf '%s' "Discord client secret: " >&2
IFS= read -r -s discord_secret
printf '\n' >&2

# Discord currently issues URL-safe application secrets. Restricting the
# accepted alphabet also keeps the systemd EnvironmentFile unambiguous.
if [[ ! ${discord_secret} =~ ^[A-Za-z0-9._~-]{16,256}$ ]]; then
    unset discord_secret
    printf '%s\n' "The secret format is invalid; no configuration was changed." >&2
    exit 1
fi

temporary_file=$(mktemp /etc/neon-identity/neon-identity.env.XXXXXX)
trap 'rm -f "${temporary_file}"' EXIT

while IFS= read -r line; do
    if [[ ${line} == DISCORD_CLIENT_SECRET=* ]]; then
        printf 'DISCORD_CLIENT_SECRET=%s\n' "${discord_secret}"
    else
        printf '%s\n' "${line}"
    fi
done < "${env_file}" > "${temporary_file}"

unset discord_secret
chown root:root "${temporary_file}"
chmod 0600 "${temporary_file}"
mv "${temporary_file}" "${env_file}"
trap - EXIT

systemctl enable --now neon-identity.service
systemctl restart neon-identity.service
systemctl --no-pager --full status neon-identity.service
