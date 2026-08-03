import { describe, expect, it } from "vitest";

import type { DiscordClient, DiscordGuildMember } from "../src/discord.js";
import type { DiscordProfile } from "../src/model.js";
import { RequiredGuildDiscordPolicy } from "../src/policy.js";

const profile: DiscordProfile = { id: "123", username: "tester", globalName: null, avatar: null };

function discordWithMember(member: DiscordGuildMember | null): DiscordClient {
    return {
        createAuthorizationUrl: () => "https://discord.test",
        exchangeCode: async () => "token",
        getCurrentUser: async () => profile,
        getGuildMember: async () => member,
    };
}

describe("RequiredGuildDiscordPolicy", () => {
    it("requires membership, completed screening, and every configured role", async () => {
        const missingMember = new RequiredGuildDiscordPolicy(discordWithMember(null), {
            guildId: "guild",
            requiredRoleIds: ["verified"],
            botToken: "secret",
            requireCompletedScreening: true,
        });
        await expect(missingMember.evaluate(profile)).resolves.toEqual({
            allowed: false,
            code: "discord_guild_membership_required",
        });

        const pending = new RequiredGuildDiscordPolicy(discordWithMember({ roles: ["verified"], pending: true }), {
            guildId: "guild",
            requiredRoleIds: ["verified"],
            botToken: "secret",
            requireCompletedScreening: true,
        });
        await expect(pending.evaluate(profile)).resolves.toEqual({
            allowed: false,
            code: "discord_membership_screening_incomplete",
        });

        const missingRole = new RequiredGuildDiscordPolicy(discordWithMember({ roles: [], pending: false }), {
            guildId: "guild",
            requiredRoleIds: ["verified"],
            botToken: "secret",
            requireCompletedScreening: true,
        });
        await expect(missingRole.evaluate(profile)).resolves.toEqual({ allowed: false, code: "discord_role_required" });

        const allowed = new RequiredGuildDiscordPolicy(discordWithMember({ roles: ["verified"], pending: false }), {
            guildId: "guild",
            requiredRoleIds: ["verified"],
            botToken: "secret",
            requireCompletedScreening: true,
        });
        await expect(allowed.evaluate(profile)).resolves.toEqual({ allowed: true });
    });
});
