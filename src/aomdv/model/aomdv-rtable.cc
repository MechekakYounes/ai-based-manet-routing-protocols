#include "aomdv-rtable.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/ptr.h"   // for Ptr<>
#include "ns3/node.h"           
#include "ns3/node-list.h"      
#include "ns3/ipv4.h"           
#include "ns3/ipv4-address.h"     
#include "ns3/energy-source.h"

#include <cmath>
#include <unordered_map>
namespace ns3
{
namespace aomdv
{

NS_LOG_COMPONENT_DEFINE("AomdvRoutingTable");

RoutingTableEntry::RoutingTableEntry(Ipv4Address dst, uint32_t seqNo)
    : m_dst(dst),
      m_seqNo(seqNo),
      m_advertisedHopCount(0)
{
}

Ipv4Address
RoutingTableEntry::GetDestination() const
{
    return m_dst;
}

uint32_t
RoutingTableEntry::GetSeqNo() const
{
    return m_seqNo;
}

void
RoutingTableEntry::SetSeqNo(uint32_t s)
{
    m_seqNo = s;
}
/*

bool RoutingTableEntry::AddPath(Ipv4Address nextHop, uint32_t hopCount, Time expire)
{   
    if (m_paths.empty())
    {
        m_advertisedHopCount = hopCount;
    }
    else if (hopCount > m_advertisedHopCount + 2)
    {
        NS_LOG_DEBUG("AOMDV rejected path via " << nextHop << " hopCount=" << hopCount
                                                << " advertisedHopCount=" << m_advertisedHopCount);
        return false;
    }

    for (auto& p : m_paths)
    {
        if (p.nextHop == nextHop)
        {
            p.hopCount = hopCount;
            p.expireTime = expire;
            p.valid = true;
            return true;
        }
    }

    m_paths.emplace_back(nextHop, hopCount, expire);
    if (hopCount > m_advertisedHopCount)
    {
        m_advertisedHopCount = hopCount;
    }
    return true;
}
*/
bool
RoutingTableEntry::AddPath (Ipv4Address nextHop,
                            uint32_t    hopCount,
                            Time        expire,
                            const std::vector<Ipv4Address>& hops)
{
    if (m_paths.empty ())
    {
        m_advertisedHopCount = hopCount;
    }
    else if (hopCount > m_advertisedHopCount + 2)
    {
        NS_LOG_DEBUG ("AOMDV rejected path via " << nextHop
                      << " hopCount=" << hopCount
                      << " advertisedHopCount=" << m_advertisedHopCount);
        return false;
    }

    for (auto& p : m_paths)
    {
        if (p.nextHop == nextHop)
        {
            p.hopCount   = hopCount;
            p.expireTime = expire;
            p.valid      = true;
            if (!hops.empty ())
                p.hops = hops;          // ← refresh hop list
            return true;
        }
    }

    AomdvPath p (nextHop, hopCount, expire);
    p.valid = true;

    if (hops.empty ())
        p.hops.push_back (nextHop);
    else
        p.hops = hops;

    m_paths.push_back (p);              // ← ONE insertion only (emplace_back removed)

    if (hopCount > m_advertisedHopCount)
        m_advertisedHopCount = hopCount;

    return true;
}
/*
static const std::unordered_map<uint32_t, Ptr<Node>>&
NodeMap ()
{
    static std::unordered_map<uint32_t, Ptr<Node>> map;
    if (map.empty ())
    {
        for (uint32_t i = 0; i < NodeList::GetNNodes (); ++i)
        {
            Ptr<Node>  node = NodeList::GetNode (i);
            Ptr<Ipv4>  ipv4 = node->GetObject<Ipv4> ();
            if (!ipv4) continue;
            for (uint32_t j = 0; j < ipv4->GetNInterfaces (); ++j)
                for (uint32_t k = 0; k < ipv4->GetNAddresses (j); ++k)
                {
                    Ipv4Address a = ipv4->GetAddress (j, k).GetLocal ();
                    if (a.IsAny () || a.IsBroadcast () || a.IsMulticast ())
                        continue;
                    map[a.Get ()] = node;
                }
        }
    }
    return map;
}
*/

static const std::unordered_map<uint32_t, Ptr<Node>>&
NodeMap ()
{
    static bool built = false;
    static std::unordered_map<uint32_t, Ptr<Node>> map;
    if (!built)
    {
        for (uint32_t i = 0; i < NodeList::GetNNodes (); ++i)
        {
            Ptr<Node> node = NodeList::GetNode (i);
            Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
            if (!ipv4) continue;
            for (uint32_t j = 1; j < ipv4->GetNInterfaces (); ++j)   // j=1 skips loopback
                for (uint32_t k = 0; k < ipv4->GetNAddresses (j); ++k)
                {
                    Ipv4Address a = ipv4->GetAddress (j, k).GetLocal ();
                    if (a.IsAny () || a.IsBroadcast () || a.IsMulticast ())
                        continue;
                    map[a.Get ()] = node;
                }
        }
        built = true;
    }
    return map;
}

double EnergyFractionOfAddress (Ipv4Address addr)
{
    const auto& map = NodeMap ();
    auto it = map.find (addr.Get ());
    if (it == map.end ()) return 1.0;

    Ptr<ns3::energy::EnergySource> src = it->second->GetObject<ns3::energy::EnergySource> ();
    if (!src) return 1.0;

    double f = src->GetEnergyFraction ();
    if (std::isnan (f) || f < 0.0) f = 0.0;    // NaN/negative → treat as depleted
    if (f > 1.0)                   f = 1.0;    // over 1 shouldn't happen; clamp

    return f;
}
void
RoutingTable::PrintWithEnergy (Ptr<OutputStreamWrapper> stream) const
{
    std::ostream* os = stream->GetStream ();
    *os << "\n===== AOMDV paths + energies at t="
        << Simulator::Now ().GetSeconds () << " s =====\n";

    for (const auto& [dst, entry] : m_ipv4AddressEntry)
    {
        *os << "Dest " << dst << ":\n";
        auto paths = entry.GetAllPaths ();
        if (paths.empty ())
        {
            *os << "  (no valid paths)\n";
            continue;
        }
        for (const auto& p : paths)
        {
            double minE = 1.0;
            *os << "  via " << p.nextHop
                << "  hops=" << p.hopCount
                << "  nodes: ";
            for (const auto& h : p.hops)
            {
                double e = EnergyFractionOfAddress (h);
                minE = std::min (minE, e);
                *os << h << "(" << (e * 100.0) << "%) ";
            }
            *os << " -> bottleneck=" << (minE * 100.0) << "%\n";
        }
    }
    *os << "\n";
}
/*
bool RoutingTableEntry::GetBestPath(AomdvPath& best) const
{
    bool found = false;
    for (const auto& p : m_paths)
    {
        if (!p.valid || Simulator::Now() >= p.expireTime)
        {
            continue;
        }

        if (!found || p.hopCount < best.hopCount)
        {
            best = p;
            found = true;
        }
    }
    return found;
}
*/
bool
RoutingTableEntry::GetBestPath (AomdvPath& best) const
{
    bool found = false;
    for (const auto& p : m_paths)
    {
        if (!p.valid || Simulator::Now () >= p.expireTime)
            continue;

        double energy = 1.0;
        for (const auto& hop : p.hops)
            energy = std::min (energy, EnergyFractionOfAddress (hop));
            NS_LOG_DEBUG ("GetBestPath: candidate via " << p.nextHop
              << " hopCount=" << p.hopCount
              << " p.hops.size()=" << p.hops.size()
              << " bottleneck=" << (energy * 100.0) << "%");

        NS_LOG_DEBUG ("GetBestPath: candidate via " << p.nextHop
                      << " hops=" << p.hopCount
                      << " bottleneck=" << (energy * 100.0) << "%"
                      << (found && energy > best.minEnergyFraction ? "  ← new best" : ""));

        if (!found || energy > best.minEnergyFraction)
        {
            best = p;
            best.minEnergyFraction = energy;
            found = true;
        }
    }

    NS_LOG_DEBUG ("GetBestPath: selected via "
                  << (found ? best.nextHop : Ipv4Address ("0.0.0.0"))
                  << " bottleneck="
                  << (found ? best.minEnergyFraction * 100.0 : 0.0) << "%");

    return found;
}


std::list<AomdvPath>
RoutingTableEntry::GetAllPaths() const
{
    std::list<AomdvPath> valid;
    for (const auto& p : m_paths)
    {
        if (p.valid && Simulator::Now() < p.expireTime)
        {
            valid.push_back(p);
        }
    }
    return valid;
}

bool
RoutingTableEntry::Purge()
{
    m_paths.remove_if([](const AomdvPath& p) { return !p.valid || Simulator::Now() >= p.expireTime; });
    return !m_paths.empty();
}

uint32_t
RoutingTableEntry::InvalidatePaths(Ipv4Address brokenNextHop)
{
    uint32_t invalidated = 0;
    for (auto& p : m_paths)
    {
        if (p.nextHop == brokenNextHop && p.valid)
        {
            p.valid = false;
            invalidated++;
        }
    }
    return invalidated;
}

void
RoutingTableEntry::InvalidateAllPaths()
{
    for (auto& p : m_paths)
    {
        p.valid = false;
    }
}

uint32_t
RoutingTableEntry::GetAdvertisedHopCount() const
{
    return m_advertisedHopCount;
}

void
RoutingTableEntry::SetAdvertisedHopCount(uint32_t h)
{
    m_advertisedHopCount = h;
}

bool
RoutingTable::AddEntry(RoutingTableEntry& entry)
{
    auto result = m_ipv4AddressEntry.emplace(entry.GetDestination(), entry);
    return result.second;
}

bool
RoutingTable::DeleteEntry(Ipv4Address dst)
{
    return m_ipv4AddressEntry.erase(dst) != 0;
}

bool
RoutingTable::LookupRoute(Ipv4Address dst, RoutingTableEntry& entry)
{
    auto it = m_ipv4AddressEntry.find(dst);
    if (it == m_ipv4AddressEntry.end())
    {
        return false;
    }

    entry = it->second;
    return true;
}

bool
RoutingTable::Update(RoutingTableEntry& entry)
{
    auto it = m_ipv4AddressEntry.find(entry.GetDestination());
    if (it == m_ipv4AddressEntry.end())
    {
        return false;
    }

    it->second = entry;
    return true;
}

void
RoutingTable::Purge()
{
    for (auto it = m_ipv4AddressEntry.begin(); it != m_ipv4AddressEntry.end();)
    {
        if (!it->second.Purge())
        {
            it = m_ipv4AddressEntry.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

std::vector<std::pair<Ipv4Address, uint32_t>>
RoutingTable::InvalidateRoutesWithNextHop(Ipv4Address brokenNextHop)
{
    std::vector<std::pair<Ipv4Address, uint32_t>> affected;
    for (auto& [dst, entry] : m_ipv4AddressEntry)
    {
        if (entry.InvalidatePaths(brokenNextHop) > 0)
        {
            affected.emplace_back(dst, entry.GetSeqNo());
        }
    }
    return affected;
}

bool
RoutingTable::InvalidateRoute(Ipv4Address dst, uint32_t seqNo)
{
    auto it = m_ipv4AddressEntry.find(dst);
    if (it == m_ipv4AddressEntry.end())
    {
        return false;
    }

    it->second.SetSeqNo(std::max(it->second.GetSeqNo(), seqNo));
    it->second.InvalidateAllPaths();
    return true;
}

/*void
RoutingTable::Print(Ptr<OutputStreamWrapper> stream) const
{
    *stream->GetStream() << "\nAOMDV Routing Table\nDestination\tSeqNo\tPaths\n";

    for (const auto& [addr, entry] : m_ipv4AddressEntry)
    {
        auto paths = entry.GetAllPaths();
        *stream->GetStream() << addr << "\t" << entry.GetSeqNo() << "\t" << paths.size() << " path(s)\n";
        int i = 1;
        for (const auto& p : paths)
        {
            *stream->GetStream() << "  Path " << i++ << ": NextHop=" << p.nextHop << " Hops=" << p.hopCount
                                 << " Expires=" << p.expireTime.GetSeconds() << "s\n";
        }
    }
}
*/
void
RoutingTable::Print (Ptr<OutputStreamWrapper> stream) const
{
    PrintWithEnergy (stream);   // was: the original tabular print
}
} // namespace aomdv
} // namespace ns3
