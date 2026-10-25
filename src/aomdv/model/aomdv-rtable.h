#pragma once

#include "ns3/ipv4-address.h"
#include "ns3/nstime.h"
#include "ns3/output-stream-wrapper.h"
#include "ns3/ptr.h"

#include <list>
#include <map>
#include <utility>
#include <vector>

namespace ns3
{
namespace aomdv
{

struct AomdvPath
{
    Ipv4Address nextHop;
    uint32_t hopCount;
    Time expireTime;
    bool valid;

    AomdvPath(Ipv4Address nh, uint32_t hc, Time exp)
        : nextHop(nh),
          hopCount(hc),
          expireTime(exp),
          valid(true)
    {
    }
};

class RoutingTableEntry
{
  public:
    RoutingTableEntry(Ipv4Address dst = Ipv4Address(), uint32_t seqNo = 0);

    Ipv4Address GetDestination() const;
    uint32_t GetSeqNo() const;
    void SetSeqNo(uint32_t s);

    bool AddPath(Ipv4Address nextHop, uint32_t hopCount, Time expire);
    bool GetBestPath(AomdvPath& path) const;
    std::list<AomdvPath> GetAllPaths() const;
    bool Purge();
    uint32_t InvalidatePaths(Ipv4Address brokenNextHop);
    void InvalidateAllPaths();

    uint32_t GetAdvertisedHopCount() const;
    void SetAdvertisedHopCount(uint32_t h);

  private:
    Ipv4Address m_dst;
    uint32_t m_seqNo;
    uint32_t m_advertisedHopCount;
    std::list<AomdvPath> m_paths;
};

class RoutingTable
{
  public:
    bool AddEntry(RoutingTableEntry& entry);
    bool DeleteEntry(Ipv4Address dst);
    bool LookupRoute(Ipv4Address dst, RoutingTableEntry& entry);
    bool Update(RoutingTableEntry& entry);
    void Purge();
    void Print(Ptr<OutputStreamWrapper> stream) const;

    std::vector<std::pair<Ipv4Address, uint32_t>> InvalidateRoutesWithNextHop(Ipv4Address brokenNextHop);
    bool InvalidateRoute(Ipv4Address dst, uint32_t seqNo);

  private:
    std::map<Ipv4Address, RoutingTableEntry> m_ipv4AddressEntry;
};

} // namespace aomdv
} // namespace ns3
