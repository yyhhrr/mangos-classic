#ifndef MANGOS_ALTERAC_VALLEY_EVENT_RULES_H
#define MANGOS_ALTERAC_VALLEY_EVENT_RULES_H

#include <cstdint>
#include <set>

// Kept independent of world objects so ownership, re-entry and stale ritual
// participation are tested with the same rules as the live scripts.
namespace AVEvents
{
    constexpr unsigned RitualParticipants = 10;
    enum class Phase : std::uint8_t { Idle, Rescue, Rally, March, Ritual, Channel, ElementWait, Assault, Air, Follow };
    class Once
    {
    public:
        bool Claim() { if (m_claimed) return false; m_claimed = true; return true; }
        void Reset() { m_claimed = false; }
        bool Claimed() const { return m_claimed; }
    private:
        bool m_claimed = false;
    };
    template<class Guid> class Participants
    {
    public:
        template<class Valid> bool Add(Guid guid, Valid valid)
        {
            Prune(valid);
            if (!valid(guid)) return false;
            return m_players.insert(guid).second;
        }
        template<class Valid> unsigned Prune(Valid valid)
        {
            for (auto it = m_players.begin(); it != m_players.end(); )
                if (!valid(*it)) it = m_players.erase(it); else ++it;
            return unsigned(m_players.size());
        }
        void Clear() { m_players.clear(); }
        std::set<Guid> const& Players() const { return m_players; }
    private:
        std::set<Guid> m_players;
    };
    constexpr bool ValidDelivery(bool active, bool owner, bool alive, bool sameBattle, bool sameTeam,
                                  bool incomplete, bool followerNearStable, bool ownerNearStable)
    {
        return active && owner && alive && sameBattle && sameTeam && incomplete && followerNearStable && ownerNearStable;
    }
}
#endif
