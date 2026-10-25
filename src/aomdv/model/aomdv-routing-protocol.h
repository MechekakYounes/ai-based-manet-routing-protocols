#pragma once

#include "aomdv-packet.h"
#include "aomdv-rtable.h"

#include "ns3/ipv4-interface.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/net-device.h"
#include "ns3/output-stream-wrapper.h"
#include "ns3/random-variable-stream.h"
#include "ns3/socket.h"
#include "ns3/timer.h"

#include <map>

namespace ns3
{
namespace aomdv
{

class RoutingProtocol : public Ipv4RoutingProtocol
{
  public:
    static const uint32_t AOMDV_PORT = 654;

    static TypeId GetTypeId();
    RoutingProtocol();
    ~RoutingProtocol() override;

    Ptr<Ipv4Route> RouteOutput(Ptr<Packet> p,
                               const Ipv4Header& header,
                               Ptr<NetDevice> oif,
                               Socket::SocketErrno& sockerr) override;

    bool RouteInput(Ptr<const Packet> p,
                    const Ipv4Header& header,
                    Ptr<const NetDevice> idev,
                    const UnicastForwardCallback& ucb,
                    const MulticastForwardCallback& mcb,
                    const LocalDeliverCallback& lcb,
                    const ErrorCallback& ecb) override;

    void NotifyInterfaceUp(uint32_t iface) override;
    void NotifyInterfaceDown(uint32_t iface) override;
    void NotifyAddAddress(uint32_t iface, Ipv4InterfaceAddress address) override;
    void NotifyRemoveAddress(uint32_t iface, Ipv4InterfaceAddress address) override;
    void SetIpv4(Ptr<Ipv4> ipv4) override;
    void PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit) const override;

    int64_t AssignStreams(int64_t stream);

  private:
    Time m_activeRouteTimeout{Seconds(30)};
    Time m_myRouteTimeout{Seconds(11.2)};
    Time m_pathDiscoveryTime{Seconds(5)};
    uint32_t m_maxPaths{3};
    uint32_t m_rreqRetries{2};
    bool m_enableHello{true};
    Time m_helloInterval{Seconds(1)};
    uint32_t m_maxQueueLen{64};

    Ptr<Ipv4> m_ipv4;
    RoutingTable m_routingTable;

    std::map<std::pair<Ipv4Address, uint32_t>, Time> m_rreqSeenTable;
    std::map<Ipv4Address, Time> m_lastRreqTime;

    uint32_t m_requestId{0};
    uint32_t m_seqNo{0};

    std::map<Ptr<Socket>, Ipv4InterfaceAddress> m_socketAddresses;

    Timer m_helloTimer;
    Timer m_purgeTimer;

    Ptr<UniformRandomVariable> m_uniformRandomVariable;

    void Start();
    void SendRequest(Ipv4Address dst);
    void RecvAomdv(Ptr<Socket> socket);
    void ProcessRreq(const RreqHeader& rreqHeader, Ipv4Address receiver, Ipv4Address src);
    void ProcessRrep(RrepHeader rrepHeader, Ipv4Address receiver, Ipv4Address src);
    void ProcessRerr(const RerrHeader& rerrHeader, Ipv4Address src);
    void SendReply(const RreqHeader& rreqHeader, RoutingTableEntry& toOrigin);
    void ForwardReply(RrepHeader& rrepHeader, Ipv4Address nextHop);
    void SendRerr(Ipv4Address brokenLink);
    void SendHello();
    void HelloTimerExpire();
    void PurgeTimerExpire();

    bool IsMyOwnAddress(Ipv4Address src) const;
    Ipv4Address GetLocalAddress(Ptr<Socket> socket) const;

    Ptr<Ipv4Route> BuildRoute(Ipv4Address dst, const AomdvPath& path) const;

    void SchedulePurge();
    void ScheduleHello();
    void RequestRouteDiscovery(Ipv4Address dst);
};

} // namespace aomdv
} // namespace ns3
