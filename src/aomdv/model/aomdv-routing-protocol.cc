#include "aomdv-routing-protocol.h"

#include "ns3/boolean.h"
#include "ns3/inet-socket-address.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/log.h"
#include "ns3/node.h"
#include "ns3/packet.h"
#include "ns3/pointer.h"
#include "ns3/simulator.h"
#include "ns3/socket-factory.h"
#include "ns3/nstime.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"

namespace ns3
{
namespace aomdv
{

NS_LOG_COMPONENT_DEFINE("AomdvRoutingProtocol");
NS_OBJECT_ENSURE_REGISTERED(RoutingProtocol);

TypeId
RoutingProtocol::GetTypeId()
{
    static TypeId tid = TypeId("ns3::aomdv::RoutingProtocol")
                            .SetParent<Ipv4RoutingProtocol>()
                            .SetGroupName("Aomdv")
                            .AddConstructor<RoutingProtocol>()
                            .AddAttribute("MaxPaths",
                                          "Maximum number of alternate paths per destination",
                                          UintegerValue(3),
                                          MakeUintegerAccessor(&RoutingProtocol::m_maxPaths),
                                          MakeUintegerChecker<uint32_t>(1, 8))
                            .AddAttribute("ActiveRouteTimeout",
                                          "Period of time a route is considered valid",
                                          TimeValue(Seconds(30)),
                                          MakeTimeAccessor(&RoutingProtocol::m_activeRouteTimeout),
                                          MakeTimeChecker())
                            .AddAttribute("PathDiscoveryTime",
                                          "Upper bound on route discovery time",
                                          TimeValue(Seconds(5)),
                                          MakeTimeAccessor(&RoutingProtocol::m_pathDiscoveryTime),
                                          MakeTimeChecker())
                            .AddAttribute("HelloInterval",
                                          "HELLO messages emission interval",
                                          TimeValue(Seconds(1)),
                                          MakeTimeAccessor(&RoutingProtocol::m_helloInterval),
                                          MakeTimeChecker())
                            .AddAttribute("EnableHello",
                                          "Enable/disable HELLO messages",
                                          BooleanValue(false),
                                          MakeBooleanAccessor(&RoutingProtocol::m_enableHello),
                                          MakeBooleanChecker())
                            .AddAttribute("MaxQueueLen",
                                          "Maximum number of packets to queue while discovering route",
                                          UintegerValue(64),
                                          MakeUintegerAccessor(&RoutingProtocol::m_maxQueueLen),
                                          MakeUintegerChecker<uint32_t>());
    return tid;
}

RoutingProtocol::RoutingProtocol()
{
    m_uniformRandomVariable = CreateObject<UniformRandomVariable>();
}

RoutingProtocol::~RoutingProtocol() = default;

void
RoutingProtocol::SetIpv4(Ptr<Ipv4> ipv4)
{
    NS_ASSERT(ipv4);
    NS_ASSERT(!m_ipv4);
    m_ipv4 = ipv4;
    Simulator::ScheduleNow(&RoutingProtocol::Start, this);
}

void
RoutingProtocol::Start()
{
    NS_LOG_FUNCTION(this);
    if (m_enableHello)
    {
        ScheduleHello();
    }
    SchedulePurge();
}

Ptr<Ipv4Route>
RoutingProtocol::RouteOutput(Ptr<Packet> p,
                             const Ipv4Header& header,
                             Ptr<NetDevice> oif,
                             Socket::SocketErrno& sockerr)
{
    (void)p;
    (void)oif;
    Ipv4Address dst = header.GetDestination();

    if (dst.IsMulticast() || dst.IsBroadcast())
    {
        sockerr = Socket::ERROR_NOROUTETOHOST;
        return nullptr;
    }

    RoutingTableEntry entry;
    if (m_routingTable.LookupRoute(dst, entry))
    {
        AomdvPath best(Ipv4Address::GetZero(), 0, Seconds(0));
        if (entry.GetBestPath(best))
        {
            sockerr = Socket::ERROR_NOTERROR;
            return BuildRoute(dst, best);
        }
    }

    RequestRouteDiscovery(dst);
    sockerr = Socket::ERROR_NOROUTETOHOST;
    return nullptr;
}

bool
RoutingProtocol::RouteInput(Ptr<const Packet> p,
                            const Ipv4Header& header,
                            Ptr<const NetDevice> idev,
                            const UnicastForwardCallback& ucb,
                            const MulticastForwardCallback& mcb,
                            const LocalDeliverCallback& lcb,
                            const ErrorCallback& ecb)
{
    (void)mcb;
    (void)ecb;

    Ipv4Address dst = header.GetDestination();
    uint32_t iif = m_ipv4->GetInterfaceForDevice(idev);

    if (m_ipv4->IsDestinationAddress(dst, iif))
    {
        lcb(p, header, iif);
        return true;
    }

    RoutingTableEntry entry;
    if (m_routingTable.LookupRoute(dst, entry))
    {
        AomdvPath best(Ipv4Address::GetZero(), 0, Seconds(0));
        if (entry.GetBestPath(best))
        {
            Ptr<Ipv4Route> route = BuildRoute(dst, best);
            ucb(route, p, header);
            return true;
        }
    }

    RequestRouteDiscovery(dst);
    return false;
}

void
RoutingProtocol::ProcessRreq(const RreqHeader& rreqHeader, Ipv4Address receiver, Ipv4Address src)
{
    auto key = std::make_pair(rreqHeader.GetOrigin(), rreqHeader.GetId());
    auto it = m_rreqSeenTable.find(key);
    if (it != m_rreqSeenTable.end() && Simulator::Now() < it->second)
    {
        return;
    }
    m_rreqSeenTable[key] = Simulator::Now() + m_pathDiscoveryTime;

    RreqHeader rreqCopy = rreqHeader;
    rreqCopy.IncrementHopCount();

    RoutingTableEntry toOrigin;
    bool found = m_routingTable.LookupRoute(rreqCopy.GetOrigin(), toOrigin);
    if (!found)
    {
        toOrigin = RoutingTableEntry(rreqCopy.GetOrigin(), rreqCopy.GetOriginSeqNo());
    }
    else if (rreqCopy.GetOriginSeqNo() > toOrigin.GetSeqNo())
    {
        toOrigin.SetSeqNo(rreqCopy.GetOriginSeqNo());
    }

    Time expire = Simulator::Now() + m_activeRouteTimeout;
    toOrigin.AddPath(src, rreqCopy.GetHopCount(), expire);
    if (found)
    {
        m_routingTable.Update(toOrigin);
    }
    else
    {
        m_routingTable.AddEntry(toOrigin);
    }

    if (IsMyOwnAddress(rreqCopy.GetDst()))
    {
        if (rreqCopy.GetDstSeqNo() > m_seqNo)
        {
            m_seqNo = rreqCopy.GetDstSeqNo();
        }
        m_seqNo++;

        RoutingTableEntry rt;
        if (m_routingTable.LookupRoute(rreqCopy.GetOrigin(), rt))
        {
            SendReply(rreqCopy, rt);
        }
        return;
    }

    RoutingTableEntry toDst;
    if (m_routingTable.LookupRoute(rreqCopy.GetDst(), toDst))
    {
        AomdvPath best(Ipv4Address::GetZero(), 0, Seconds(0));
        if (toDst.GetBestPath(best) && toDst.GetSeqNo() >= rreqCopy.GetDstSeqNo())
        {
            SendReply(rreqCopy, toOrigin);
            return;
        }
    }

    for (auto& [socket, iface] : m_socketAddresses)
    {
        (void)iface;
        Ptr<Packet> packet = Create<Packet>();
        packet->AddHeader(rreqCopy);
        packet->AddHeader(TypeHeader(AOMDVTYPE_RREQ));
        socket->SendTo(packet, 0, InetSocketAddress(Ipv4Address("255.255.255.255"), AOMDV_PORT));
    }

    (void)receiver;
}

void
RoutingProtocol::ProcessRrep(RrepHeader rrepHeader, Ipv4Address receiver, Ipv4Address src)
{
    Ipv4Address dst = rrepHeader.GetDst();
    Ipv4Address origin = rrepHeader.GetOrigin();

    rrepHeader.IncrementHopCount();
    rrepHeader.AddPathNode(receiver);

    RoutingTableEntry toDst;
    bool found = m_routingTable.LookupRoute(dst, toDst);
    if (!found)
    {
        toDst = RoutingTableEntry(dst, rrepHeader.GetDstSeqNo());
    }
    else if (rrepHeader.GetDstSeqNo() > toDst.GetSeqNo())
    {
        toDst.SetSeqNo(rrepHeader.GetDstSeqNo());
    }

    Time expire = Simulator::Now() + rrepHeader.GetLifeTime();
    toDst.AddPath(src, rrepHeader.GetHopCount(), expire);

    if (found)
    {
        m_routingTable.Update(toDst);
    }
    else
    {
        m_routingTable.AddEntry(toDst);
    }

    if (IsMyOwnAddress(origin))
    {
        NS_LOG_INFO("AOMDV installed path to " << dst << " via " << src);
        return;
    }

    RoutingTableEntry toOrigin;
    if (!m_routingTable.LookupRoute(origin, toOrigin))
    {
        return;
    }

    AomdvPath best(Ipv4Address::GetZero(), 0, Seconds(0));
    if (!toOrigin.GetBestPath(best))
    {
        return;
    }

    ForwardReply(rrepHeader, best.nextHop);
}

void
RoutingProtocol::ProcessRerr(const RerrHeader& rerrHeader, Ipv4Address src)
{
    RerrHeader propagate;

    for (const auto& [dst, seq] : rerrHeader.GetUnreachableNodes())
    {
        if (m_routingTable.InvalidateRoute(dst, seq))
        {
            propagate.AddUnreachableNode(dst, seq);
        }
    }

    auto brokenAffected = m_routingTable.InvalidateRoutesWithNextHop(src);
    for (const auto& [dst, seq] : brokenAffected)
    {
        propagate.AddUnreachableNode(dst, seq);
    }

    if (propagate.IsEmpty())
    {
        return;
    }

    Ptr<Packet> packet = Create<Packet>();
    packet->AddHeader(propagate);
    packet->AddHeader(TypeHeader(AOMDVTYPE_RERR));

    for (auto& [socket, iface] : m_socketAddresses)
    {
        (void)iface;
        socket->SendTo(packet->Copy(),
                       0,
                       InetSocketAddress(Ipv4Address("255.255.255.255"), AOMDV_PORT));
    }
}

void
RoutingProtocol::SendReply(const RreqHeader& rreqHeader, RoutingTableEntry& toOrigin)
{
    RrepHeader rrep;
    rrep.SetDst(rreqHeader.GetDst());
    rrep.SetOrigin(rreqHeader.GetOrigin());
    rrep.SetDstSeqNo(m_seqNo);
    rrep.SetHopCount(0);
    rrep.SetLifeTime(m_myRouteTimeout);

    AomdvPath best(Ipv4Address::GetZero(), 0, Seconds(0));
    if (!toOrigin.GetBestPath(best))
    {
        return;
    }

    for (auto& [socket, iface] : m_socketAddresses)
    {
        (void)iface;
        Ptr<Packet> packet = Create<Packet>();
        packet->AddHeader(rrep);
        packet->AddHeader(TypeHeader(AOMDVTYPE_RREP));
        socket->SendTo(packet, 0, InetSocketAddress(best.nextHop, AOMDV_PORT));
        break;
    }
}

void
RoutingProtocol::ForwardReply(RrepHeader& rrepHeader, Ipv4Address nextHop)
{
    for (auto& [socket, iface] : m_socketAddresses)
    {
        (void)iface;
        Ptr<Packet> packet = Create<Packet>();
        packet->AddHeader(rrepHeader);
        packet->AddHeader(TypeHeader(AOMDVTYPE_RREP));
        socket->SendTo(packet, 0, InetSocketAddress(nextHop, AOMDV_PORT));
        break;
    }
}

void
RoutingProtocol::SendRerr(Ipv4Address brokenLink)
{
    RerrHeader rerr;
    auto affected = m_routingTable.InvalidateRoutesWithNextHop(brokenLink);
    for (const auto& [dst, seq] : affected)
    {
        rerr.AddUnreachableNode(dst, seq);
    }

    if (rerr.IsEmpty())
    {
        return;
    }

    Ptr<Packet> packet = Create<Packet>();
    packet->AddHeader(rerr);
    packet->AddHeader(TypeHeader(AOMDVTYPE_RERR));

    for (auto& [socket, iface] : m_socketAddresses)
    {
        (void)iface;
        socket->SendTo(packet->Copy(),
                       0,
                       InetSocketAddress(Ipv4Address("255.255.255.255"), AOMDV_PORT));
    }
}

void
RoutingProtocol::SendHello()
{
    for (auto& [socket, iface] : m_socketAddresses)
    {
        RrepHeader hello;
        hello.SetDst(iface.GetLocal());
        hello.SetOrigin(iface.GetLocal());
        hello.SetDstSeqNo(m_seqNo);
        hello.SetHopCount(0);
        hello.SetLifeTime(Seconds(1.5) * m_helloInterval.GetSeconds());

        Ptr<Packet> packet = Create<Packet>();
        packet->AddHeader(hello);
        packet->AddHeader(TypeHeader(AOMDVTYPE_HELLO));
        socket->SendTo(packet, 0, InetSocketAddress(iface.GetBroadcast(), AOMDV_PORT));
    }
}

void
RoutingProtocol::RecvAomdv(Ptr<Socket> socket)
{
    Address sourceAddress;
    Ptr<Packet> packet = socket->RecvFrom(sourceAddress);
    InetSocketAddress inetSrc = InetSocketAddress::ConvertFrom(sourceAddress);
    Ipv4Address src = inetSrc.GetIpv4();

    Ipv4InterfaceAddress iface;
    auto it = m_socketAddresses.find(socket);
    if (it != m_socketAddresses.end())
    {
        iface = it->second;
    }

    TypeHeader typeHdr;
    packet->RemoveHeader(typeHdr);
    if (!typeHdr.IsValid())
    {
        return;
    }

    switch (typeHdr.Get())
    {
    case AOMDVTYPE_RREQ:
    {
        RreqHeader rreq;
        packet->RemoveHeader(rreq);
        ProcessRreq(rreq, iface.GetLocal(), src);
        break;
    }
    case AOMDVTYPE_RREP:
    {
        RrepHeader rrep;
        packet->RemoveHeader(rrep);
        ProcessRrep(rrep, iface.GetLocal(), src);
        break;
    }
    case AOMDVTYPE_HELLO:
    {
        RrepHeader hello;
        packet->RemoveHeader(hello);

        RoutingTableEntry oneHop;
        bool found = m_routingTable.LookupRoute(src, oneHop);
        if (!found)
        {
            oneHop = RoutingTableEntry(src, hello.GetDstSeqNo());
        }
        else if (hello.GetDstSeqNo() > oneHop.GetSeqNo())
        {
            oneHop.SetSeqNo(hello.GetDstSeqNo());
        }

        oneHop.AddPath(src, 1, Simulator::Now() + hello.GetLifeTime());
        if (found)
        {
            m_routingTable.Update(oneHop);
        }
        else
        {
            m_routingTable.AddEntry(oneHop);
        }
        break;
    }
    case AOMDVTYPE_RERR:
    {
        RerrHeader rerr;
        packet->RemoveHeader(rerr);
        ProcessRerr(rerr, src);
        break;
    }
    default:
        break;
    }
}

void
RoutingProtocol::SendRequest(Ipv4Address dst)
{
    m_seqNo++;
    m_requestId++;

    RreqHeader rreq;
    rreq.SetDst(dst);
    rreq.SetDstSeqNo(0);
    rreq.SetId(m_requestId);
    rreq.SetHopCount(0);

    for (const auto& [socket, iface] : m_socketAddresses)
    {
        (void)socket;
        rreq.SetOrigin(iface.GetLocal());
        rreq.SetOriginSeqNo(m_seqNo);
        break;
    }

    for (auto& [socket, iface] : m_socketAddresses)
    {
        (void)iface;
        Ptr<Packet> packet = Create<Packet>();
        packet->AddHeader(rreq);
        packet->AddHeader(TypeHeader(AOMDVTYPE_RREQ));
        socket->SendTo(packet, 0, InetSocketAddress(Ipv4Address("255.255.255.255"), AOMDV_PORT));
    }

    NS_LOG_INFO("AOMDV sent RREQ for " << dst << " id=" << m_requestId);
}

void
RoutingProtocol::RequestRouteDiscovery(Ipv4Address dst)
{
    auto it = m_lastRreqTime.find(dst);
    if (it != m_lastRreqTime.end() && (Simulator::Now() - it->second) < MilliSeconds(50))
    {
        return;
    }

    m_lastRreqTime[dst] = Simulator::Now();
    SendRequest(dst);
}

void
RoutingProtocol::NotifyInterfaceUp(uint32_t iface)
{
    Ptr<Ipv4L3Protocol> l3 = m_ipv4->GetObject<Ipv4L3Protocol>();
    if (l3->GetNAddresses(iface) == 0)
    {
        return;
    }

    Ipv4InterfaceAddress ifaceAddr = l3->GetAddress(iface, 0);
    if (ifaceAddr.GetLocal() == Ipv4Address("127.0.0.1"))
    {
        return;
    }

    Ptr<Socket> socket = Socket::CreateSocket(m_ipv4->GetObject<Node>(), UdpSocketFactory::GetTypeId());
    socket->SetAllowBroadcast(true);
    socket->BindToNetDevice(l3->GetNetDevice(iface));
    socket->Bind(InetSocketAddress(Ipv4Address::GetAny(), AOMDV_PORT));
    socket->SetRecvCallback(MakeCallback(&RoutingProtocol::RecvAomdv, this));

    m_socketAddresses.emplace(socket, ifaceAddr);
}

void
RoutingProtocol::NotifyInterfaceDown(uint32_t iface)
{
    Ptr<Ipv4L3Protocol> l3 = m_ipv4->GetObject<Ipv4L3Protocol>();
    for (auto it = m_socketAddresses.begin(); it != m_socketAddresses.end(); ++it)
    {
        if (l3->GetInterfaceForAddress(it->second.GetLocal()) == static_cast<int32_t>(iface))
        {
            it->first->Close();
            m_socketAddresses.erase(it);
            break;
        }
    }
}

void
RoutingProtocol::NotifyAddAddress(uint32_t iface, Ipv4InterfaceAddress address)
{
    (void)iface;
    (void)address;
}

void
RoutingProtocol::NotifyRemoveAddress(uint32_t iface, Ipv4InterfaceAddress address)
{
    (void)iface;
    (void)address;
}

void
RoutingProtocol::PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
{
    (void)unit;
    m_routingTable.Print(stream);
}

bool
RoutingProtocol::IsMyOwnAddress(Ipv4Address src) const
{
    for (const auto& [socket, iface] : m_socketAddresses)
    {
        (void)socket;
        if (iface.GetLocal() == src)
        {
            return true;
        }
    }
    return false;
}

Ipv4Address
RoutingProtocol::GetLocalAddress(Ptr<Socket> socket) const
{
    auto it = m_socketAddresses.find(socket);
    if (it != m_socketAddresses.end())
    {
        return it->second.GetLocal();
    }
    return Ipv4Address::GetZero();
}

Ptr<Ipv4Route>
RoutingProtocol::BuildRoute(Ipv4Address dst, const AomdvPath& path) const
{
    Ptr<Ipv4Route> route = Create<Ipv4Route>();
    route->SetDestination(dst);
    route->SetGateway(path.nextHop);

    for (const auto& [socket, iface] : m_socketAddresses)
    {
        (void)socket;
        int32_t ifIndex = m_ipv4->GetInterfaceForAddress(iface.GetLocal());
        if (ifIndex >= 0)
        {
            route->SetSource(iface.GetLocal());
            route->SetOutputDevice(m_ipv4->GetNetDevice(static_cast<uint32_t>(ifIndex)));
            break;
        }
    }

    return route;
}

void
RoutingProtocol::ScheduleHello()
{
    m_helloTimer.SetFunction(&RoutingProtocol::HelloTimerExpire, this);
    m_helloTimer.Schedule(m_helloInterval);
}

void
RoutingProtocol::HelloTimerExpire()
{
    SendHello();
    m_helloTimer.Cancel();
    ScheduleHello();
}

void
RoutingProtocol::SchedulePurge()
{
    m_purgeTimer.SetFunction(&RoutingProtocol::PurgeTimerExpire, this);
    m_purgeTimer.Schedule(Seconds(1));
}

void
RoutingProtocol::PurgeTimerExpire()
{
    m_routingTable.Purge();
    m_purgeTimer.Cancel();
    SchedulePurge();
}

int64_t
RoutingProtocol::AssignStreams(int64_t stream)
{
    m_uniformRandomVariable->SetStream(stream);
    return 1;
}

} // namespace aomdv
} // namespace ns3
