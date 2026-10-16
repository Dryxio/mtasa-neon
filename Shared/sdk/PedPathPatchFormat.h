#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace PedPathPatch
{
    constexpr unsigned Areas = 64;
    constexpr unsigned MaxNodes = 4096;
    constexpr unsigned MaxLinks = 32768;
    constexpr unsigned DynamicLinks = 192;
    constexpr unsigned MaxFileSize = 4 * 1024 * 1024;

    struct Address
    {
        std::uint16_t area{}, node{};
    };
    struct Link
    {
        Address      to;
        std::uint8_t length{}, flags{};
    };
    struct Edit
    {
        std::uint16_t               node{};
        std::array<std::int16_t, 3> position{};
        std::uint8_t                width{}, flood{}, enabled{};
        std::vector<Link>           links;
    };
    struct Area
    {
        unsigned                     id{}, nodes{}, vehicles{}, addresses{}, carLinks{};
        std::array<std::uint8_t, 32> baseDigest{};
        std::vector<Edit>            edits;
    };
    struct Patch
    {
        std::map<unsigned, Area> areas;
    };

    class Reader
    {
        const std::uint8_t* m_data;
        std::size_t         m_size, m_offset{};

    public:
        Reader(const void* data, std::size_t size) : m_data(static_cast<const std::uint8_t*>(data)), m_size(size) {}
        void Bytes(void* out, std::size_t count)
        {
            if (count > m_size - m_offset)
                throw std::runtime_error("truncated-patch");
            std::memcpy(out, m_data + m_offset, count);
            m_offset += count;
        }
        std::uint8_t U8()
        {
            std::uint8_t v;
            Bytes(&v, 1);
            return v;
        }
        std::uint16_t U16()
        {
            auto lo = U8();
            return lo | (unsigned(U8()) << 8);
        }
        std::uint32_t U32()
        {
            auto lo = U16();
            return lo | (std::uint32_t(U16()) << 16);
        }
        bool Done() const { return m_offset == m_size; }
    };

    inline Patch Parse(const void* data, std::size_t size)
    {
        if (!data || size < 12 || size > MaxFileSize)
            throw std::runtime_error("invalid-patch-size");
        Reader r(data, size);
        char   magic[8];
        r.Bytes(magic, 8);
        if (std::memcmp(magic, "NPEDPAT1", 8))
            throw std::runtime_error("unsupported-patch-format");
        const auto count = r.U32();
        if (count != Areas)
            throw std::runtime_error("invalid-area-count");
        Patch patch;
        bool  changed = false;
        for (unsigned i = 0; i < count; ++i)
        {
            Area a;
            a.id = r.U32();
            a.nodes = r.U32();
            a.vehicles = r.U32();
            a.addresses = r.U32();
            a.carLinks = r.U32();
            r.Bytes(a.baseDigest.data(), a.baseDigest.size());
            const auto edits = r.U32();
            if (a.id >= Areas || a.nodes > MaxNodes || a.vehicles > a.nodes || a.addresses > MaxLinks - DynamicLinks || a.carLinks > 65535 ||
                edits > MaxNodes || patch.areas.count(a.id))
                throw std::runtime_error("invalid-area-header");
            unsigned           next = a.nodes, linkCount = a.addresses;
            std::set<unsigned> seen;
            for (unsigned j = 0; j < edits; ++j)
            {
                Edit e;
                e.node = r.U16();
                for (auto& p : e.position)
                    p = static_cast<std::int16_t>(r.U16());
                e.width = r.U8();
                e.flood = r.U8();
                e.enabled = r.U8();
                const auto links = r.U8();
                if (e.node < a.vehicles || e.node >= MaxNodes || !seen.insert(e.node).second || e.enabled > 1 || links > 15 || (!e.enabled && links) ||
                    (!e.enabled && e.node >= a.nodes))
                    throw std::runtime_error("invalid-ped-node");
                // Appends have deterministic addresses; a missing slot must never
                // accidentally become a valid native path node.
                if (e.node >= a.nodes && e.node != next++)
                    throw std::runtime_error("non-contiguous-added-nodes");
                std::set<unsigned> targets;
                for (unsigned k = 0; k < links; ++k)
                {
                    Link l;
                    l.to.area = r.U16();
                    l.to.node = r.U16();
                    l.length = r.U8();
                    l.flags = r.U8();
                    if (l.to.area >= Areas || !l.length || l.flags > 3 || !targets.insert((unsigned(l.to.area) << 16) | l.to.node).second ||
                        (l.to.area == a.id && l.to.node == e.node))
                        throw std::runtime_error("invalid-ped-link");
                    e.links.push_back(l);
                }
                linkCount += links;
                if (linkCount > MaxLinks - DynamicLinks)
                    throw std::runtime_error("too-many-links");
                a.edits.push_back(std::move(e));
            }
            changed |= !a.edits.empty();
            patch.areas.emplace(a.id, std::move(a));
        }
        if (!changed || !r.Done())
            throw std::runtime_error("empty-patch-or-trailing-data");
        for (const auto& entry : patch.areas)
            for (const auto& e : entry.second.edits)
                for (const auto& l : e.links)
                {
                    const auto target = patch.areas.find(l.to.area);
                    if (target == patch.areas.end() || l.to.node < target->second.vehicles)
                        throw std::runtime_error("missing-ped-link-dependency");
                    const Edit* targetEdit = nullptr;
                    for (const auto& candidate : target->second.edits)
                        if (candidate.node == l.to.node)
                            targetEdit = &candidate;
                    if ((targetEdit && (!targetEdit->enabled || targetEdit->flood != e.flood)) || (!targetEdit && l.to.node >= target->second.nodes))
                        throw std::runtime_error("invalid-ped-link-target");
                }
        return patch;
    }
}
