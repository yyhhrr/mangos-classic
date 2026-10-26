#ifndef MANGOS_BATTLEGROUND_AV_SUPPORT_H
#define MANGOS_BATTLEGROUND_AV_SUPPORT_H

#include <array>
#include <cstdint>

enum AVSupportType : std::uint8_t
{
    AV_SUPPORT_AIR_1,
    AV_SUPPORT_AIR_2,
    AV_SUPPORT_AIR_3,
    AV_SUPPORT_GROUND,
    AV_SUPPORT_CAVALRY,
    AV_SUPPORT_ELEMENTAL,
    AV_SUPPORT_MAX
};

enum AVSupportResource : std::uint8_t
{
    AV_RESOURCE_SCRAPS,
    AV_RESOURCE_AIR_1,
    AV_RESOURCE_AIR_2,
    AV_RESOURCE_AIR_3,
    AV_RESOURCE_ELEMENTAL,
    AV_RESOURCE_SUPPLIES_NEAR,
    AV_RESOURCE_SUPPLIES_FAR,
    AV_RESOURCE_HIDES,
    AV_RESOURCE_MOUNTS,
    AV_RESOURCE_MAX
};

enum AVSupportStage : std::uint8_t
{
    AV_SUPPORT_COLLECTING,
    AV_SUPPORT_READY,
    AV_SUPPORT_ACTIVE,
    AV_SUPPORT_DONE,
    AV_SUPPORT_UNAVAILABLE
};

namespace AVSupport
{
    // Quantities are item units. Mine quests consume ten boxes per turn-in.
    constexpr std::uint32_t Requirement(unsigned resource)
    {
        return resource == AV_RESOURCE_SCRAPS ? 1500 :
               resource == AV_RESOURCE_AIR_1 ? 120 :
               resource == AV_RESOURCE_AIR_2 ? 60 :
               resource == AV_RESOURCE_AIR_3 ? 30 :
               resource == AV_RESOURCE_ELEMENTAL ? 200 :
               resource == AV_RESOURCE_SUPPLIES_NEAR ? 280 :
               resource == AV_RESOURCE_SUPPLIES_FAR ? 70 :
               resource == AV_RESOURCE_HIDES || resource == AV_RESOURCE_MOUNTS ? 25 : 0;
    }

    constexpr unsigned DefenderTier(std::uint32_t scraps)
    {
        return scraps >= 1500 ? 3 : scraps >= 1000 ? 2 : scraps >= 500 ? 1 : 0;
    }

    class State
    {
    public:
        State() { Reset(); }

        void Reset()
        {
            for (auto& team : m_resources) team.fill(0);
            for (auto& team : m_stages) team.fill(AV_SUPPORT_COLLECTING);
            for (auto& team : m_rescued) team.fill(false);
        }

        std::uint32_t GetProgress(unsigned team, unsigned resource) const
        {
            return team < 2 && resource < AV_RESOURCE_MAX ? m_resources[team][resource] : 0;
        }

        bool AddProgress(unsigned team, unsigned resource, std::uint32_t amount)
        {
            if (team >= 2 || resource >= AV_RESOURCE_MAX || !amount) return false;
            std::uint32_t& count = m_resources[team][resource];
            const std::uint32_t maximum = ~std::uint32_t(0);
            count = amount > maximum - count ? maximum : count + amount;
            return true;
        }

        bool Rescue(unsigned team, unsigned tier)
        {
            if (team >= 2 || tier >= 3 || m_rescued[team][tier]) return false;
            m_rescued[team][tier] = true;
            return true;
        }

        bool IsRescued(unsigned team, unsigned tier) const
        {
            return team < 2 && tier < 3 && m_rescued[team][tier];
        }

        AVSupportStage GetStage(unsigned team, unsigned type) const
        {
            if (team >= 2 || type >= AV_SUPPORT_MAX) return AV_SUPPORT_UNAVAILABLE;
            if (m_stages[team][type] != AV_SUPPORT_COLLECTING) return m_stages[team][type];
            bool ready = false;
            if (type <= AV_SUPPORT_AIR_3)
                ready = m_rescued[team][type] && Enough(team, AV_RESOURCE_AIR_1 + type);
            else if (type == AV_SUPPORT_GROUND)
                ready = Enough(team, AV_RESOURCE_SUPPLIES_NEAR) && Enough(team, AV_RESOURCE_SUPPLIES_FAR);
            else if (type == AV_SUPPORT_CAVALRY)
                ready = Enough(team, AV_RESOURCE_HIDES) && Enough(team, AV_RESOURCE_MOUNTS);
            else if (type == AV_SUPPORT_ELEMENTAL)
                ready = Enough(team, AV_RESOURCE_ELEMENTAL);
            return ready ? AV_SUPPORT_READY : AV_SUPPORT_COLLECTING;
        }

        bool TryBegin(unsigned team, unsigned type)
        {
            if (GetStage(team, type) != AV_SUPPORT_READY) return false;
            // Lock before consuming: callers can never start the same event twice.
            m_stages[team][type] = AV_SUPPORT_ACTIVE;
            if (type <= AV_SUPPORT_AIR_3) Consume(team, AV_RESOURCE_AIR_1 + type);
            else if (type == AV_SUPPORT_GROUND)
            {
                Consume(team, AV_RESOURCE_SUPPLIES_NEAR);
                Consume(team, AV_RESOURCE_SUPPLIES_FAR);
            }
            else if (type == AV_SUPPORT_CAVALRY)
            {
                Consume(team, AV_RESOURCE_HIDES);
                Consume(team, AV_RESOURCE_MOUNTS);
            }
            else Consume(team, AV_RESOURCE_ELEMENTAL);
            return true;
        }

        bool Finish(unsigned team, unsigned type, bool success)
        {
            if (GetStage(team, type) != AV_SUPPORT_ACTIVE) return false;
            m_stages[team][type] = success ? AV_SUPPORT_DONE : AV_SUPPORT_COLLECTING;
            return true;
        }

    private:
        bool Enough(unsigned team, unsigned resource) const
        {
            return m_resources[team][resource] >= Requirement(resource);
        }
        void Consume(unsigned team, unsigned resource)
        {
            m_resources[team][resource] -= Requirement(resource);
        }

        std::array<std::array<std::uint32_t, AV_RESOURCE_MAX>, 2> m_resources;
        std::array<std::array<AVSupportStage, AV_SUPPORT_MAX>, 2> m_stages;
        std::array<std::array<bool, 3>, 2> m_rescued;
    };
}

#endif
