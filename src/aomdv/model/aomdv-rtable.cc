#include "aomdv-rtable.h"

#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/ptr.h"   // for Ptr<>
#include "ns3/node.h"           
#include "ns3/node-list.h"      
#include "ns3/ipv4.h"           
#include "ns3/ipv4-address.h"     
#include "ns3/energy-source.h"

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

bool
RoutingTableEntry::AddPath(Ipv4Address nextHop, uint32_t hopCount, Time expire)
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
static double EnergyFractionOfAddress (Ipv4Address addr)
{
    // Resolve IPv4 -> Node. NodeList scan; costly, but works without
    // any map. Cache it if you use this in a long simulation.
    Ptr<Node> node = nullptr;
    for (uint32_t i = 0; i < NodeList::GetNNodes(); ++i)
    {
        Ptr<Node> n    = NodeList::GetNode(i);
        Ptr<Ipv4> ipv4 = n->GetObject<Ipv4>();
        if (!ipv4) continue;
        for (uint32_t j = 0; j < ipv4->GetNInterfaces(); ++j)
            for (uint32_t k = 0; k < ipv4->GetNAddresses(j); ++k)
                if (ipv4->GetAddress(j, k).GetLocal() == addr)
                {
                    node = n; goto done;
                }
    }
    done:
    if (!node) return 1.0;                          // unknown -> don't penalize
    Ptr<EnergySource> src = node->GetObject<EnergySource>();
    return src ? src->GetEnergyFraction() : 1.0;   // in [0,1]
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
bool RoutingTableEntry::GetBestPath (AomdvPath& best) const
{
    bool   found = false;
    for (const auto& p : m_paths)
    {
        if (!p.valid || Simulator::Now() >= p.expireTime)
            continue;

        double energy = std::numeric_limits<double>::max();
        for (const auto& hop : p.hops)
            energy = std::min(energy, EnergyFractionOfAddress(hop));

        if (!found || energy > best.minEnergyFraction)   // reuse field for the metric
        {
            best = p;
            best.minEnergyFraction = energy;             // record for next comparison
            found = true;
        }
    }
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

void
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

} // namespace aomdv
} // namespace ns3
