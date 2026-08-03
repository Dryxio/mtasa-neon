import type { DiscordClient } from "./discord.js";
import type { DiscordProfile } from "./model.js";

export type DiscordPolicyResult = { allowed: true } | { allowed: false; code: string };

export interface DiscordIdentityPolicy {
    evaluate(profile: DiscordProfile): Promise<DiscordPolicyResult>;
}

export class AllowAllDiscordPolicy implements DiscordIdentityPolicy {
    async evaluate(): Promise<DiscordPolicyResult> {
        return { allowed: true };
    }
}

interface RequiredGuildPolicyConfig {
    guildId: string;
    requiredRoleIds: string[];
    botToken: string;
    requireCompletedScreening: boolean;
}

/**
 * This policy is intentionally injectable: Neon Identity is global, while a
 * guild or role requirement belongs to a particular deployment or game server.
 */
export class RequiredGuildDiscordPolicy implements DiscordIdentityPolicy {
    constructor(
        private readonly discord: DiscordClient,
        private readonly config: RequiredGuildPolicyConfig,
    ) {}

    async evaluate(profile: DiscordProfile): Promise<DiscordPolicyResult> {
        const member = await this.discord.getGuildMember(this.config.guildId, profile.id, this.config.botToken);
        if (!member) return { allowed: false, code: "discord_guild_membership_required" };
        if (this.config.requireCompletedScreening && member.pending) {
            return { allowed: false, code: "discord_membership_screening_incomplete" };
        }
        const missingRole = this.config.requiredRoleIds.some((roleId) => !member.roles.includes(roleId));
        if (missingRole) return { allowed: false, code: "discord_role_required" };
        return { allowed: true };
    }
}
