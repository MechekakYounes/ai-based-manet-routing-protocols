/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2009 IITP RAS
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Based on 
 *      NS-2 AOMDV model developed by the CMU/MONARCH group and optimized and
 *      tuned by Samir Das and Mahesh Marina, University of Cincinnati;
 * 
 *      AOMDV-UU implementation by Erik Nordström of Uppsala University
 *      http://core.it.uu.se/core/index.php/AOMDV-UU
 *
 * Authors: Elena Buchatskaia <borovkovaes@iitp.ru>
 *          Pavel Boyko <boyko@iitp.ru>
 */

#include "aomdv-routing-protocol.h"
#include "ns3/log.h"
#include "ns3/boolean.h"
#include "ns3/random-variable-stream.h"
#include "ns3/inet-socket-address.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/wifi-net-device.h"
#include "ns3/adhoc-wifi-mac.h"
#include "ns3/string.h"
#include "ns3/pointer.h"
#include "ns3/wifi-mpdu.h"
#include <algorithm>
#include <limits>

NS_LOG_COMPONENT_DEFINE ("AomdvRoutingProtocol");

namespace ns3
{
namespace aomdv
{

NS_OBJECT_ENSURE_REGISTERED (RoutingProtocol);

const uint32_t RoutingProtocol::AOMDV_PORT = 654;

//-----------------------------------------------------------------------------
/// Tag used by AOMDV implementation

class DeferredRouteOutputTag : public Tag
{
public:
  DeferredRouteOutputTag (int32_t o = -1) : Tag (), m_oif (o) {}

  static TypeId GetTypeId ()
  {
    static TypeId tid = TypeId ("ns3::aomdv::DeferredRouteOutputTag")
      .SetParent<Tag> ()
      .SetGroupName ("Aomdv")
      .AddConstructor<DeferredRouteOutputTag> ()
    ;
    return tid;
  }

  TypeId GetInstanceTypeId () const override
  {
    return GetTypeId ();
  }

  int32_t GetInterface () const
  {
    return m_oif;
  }

  void SetInterface (int32_t oif)
  {
    m_oif = oif;
  }

  uint32_t GetSerializedSize () const override
  {
    return sizeof (int32_t);
  }

  void Serialize (TagBuffer i) const override
  {
    i.WriteU32 (m_oif);
  }

  void Deserialize (TagBuffer i) override
  {
    m_oif = i.ReadU32 ();
  }

  void Print (std::ostream &os) const override
  {
    os << "DeferredRouteOutputTag: output interface = " << m_oif;
  }

private:
  int32_t m_oif;
};

NS_OBJECT_ENSURE_REGISTERED (DeferredRouteOutputTag);


//-----------------------------------------------------------------------------
RoutingProtocol::RoutingProtocol () :
  m_rreqRetries (2),
  m_ttlStart (1),
  m_ttlIncrement (2),
  m_ttlThreshold (7),
  m_timeoutBuffer (2),
  m_rreqRateLimit (10),
  m_rerrRateLimit (10),
  m_activeRouteTimeout (Seconds (3)),
  m_netDiameter (35),
  m_nodeTraversalTime (MilliSeconds (40)),
  m_netTraversalTime (Time ((2 * m_netDiameter) * m_nodeTraversalTime)),
  m_pathDiscoveryTime ( Time (2 * m_netTraversalTime)),
  m_myRouteTimeout (Time (2 * std::max (m_pathDiscoveryTime, m_activeRouteTimeout))),
  m_helloInterval (Seconds (1)),
  m_allowedHelloLoss (2),
  m_deletePeriod (Time (5 * std::max (m_activeRouteTimeout, m_helloInterval))),
  m_nextHopWait (m_nodeTraversalTime + MilliSeconds (10)),
  m_blackListTimeout (Time (m_rreqRetries * m_netTraversalTime)),
  m_maxQueueLen (64),
  m_maxQueueTime (Seconds (30)),
  m_destinationOnly (false),
  m_gratuitousReply (true),
  m_enableHello (true),
  m_enableBroadcast (true),
  m_routingTable (m_deletePeriod),
  m_queue (m_maxQueueLen, m_maxQueueTime),
  m_requestId (0),
  m_seqNo (0),
  m_rreqCount (0),
  m_rerrCount (0),
  m_rreqIdCache (m_pathDiscoveryTime),
  m_dpd (m_pathDiscoveryTime),
  m_nb (Time (m_allowedHelloLoss * m_helloInterval)),
  m_rreqRateLimitTimer (Timer::CANCEL_ON_DESTROY),
  m_rerrRateLimitTimer (Timer::CANCEL_ON_DESTROY),
  m_htimer (Timer::CANCEL_ON_DESTROY)
{
  m_nb.SetCallback (MakeCallback (&RoutingProtocol::SendRerrWhenBreaksLinkToNextHop, this));
}

TypeId
RoutingProtocol::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::aomdv::RoutingProtocol")
    .SetParent<Ipv4RoutingProtocol> ()
    .SetGroupName ("Aomdv")
    .AddConstructor<RoutingProtocol> ()
    .AddAttribute ("HelloInterval", "HELLO messages emission interval.",
                   TimeValue (Seconds (1)),
                   MakeTimeAccessor (&RoutingProtocol::m_helloInterval),
                   MakeTimeChecker ())
    .AddAttribute ("RreqRetries", "Maximum number of retransmissions of RREQ to discover a route",
                   UintegerValue (2),
                   MakeUintegerAccessor (&RoutingProtocol::m_rreqRetries),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("RreqRateLimit", "Maximum number of RREQ per second.",
                   UintegerValue (10),
                   MakeUintegerAccessor (&RoutingProtocol::m_rreqRateLimit),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("RerrRateLimit", "Maximum number of RERR per second.",
                   UintegerValue (10),
                   MakeUintegerAccessor (&RoutingProtocol::m_rerrRateLimit),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("NodeTraversalTime", "Conservative estimate of the average one hop traversal time for packets and should include "
                   "queuing delays, interrupt processing times and transfer times.",
                   TimeValue (MilliSeconds (40)),
                   MakeTimeAccessor (&RoutingProtocol::m_nodeTraversalTime),
                   MakeTimeChecker ())
    .AddAttribute ("NextHopWait", "Period of our waiting for the neighbour's RREP_ACK = 10 ms + NodeTraversalTime",
                   TimeValue (MilliSeconds (50)),
                   MakeTimeAccessor (&RoutingProtocol::m_nextHopWait),
                   MakeTimeChecker ())
    .AddAttribute ("ActiveRouteTimeout", "Period of time during which the route is considered to be valid",
                   TimeValue (Seconds (3)),
                   MakeTimeAccessor (&RoutingProtocol::m_activeRouteTimeout),
                   MakeTimeChecker ())
    .AddAttribute ("MyRouteTimeout", "Value of lifetime field in RREP generating by this node = 2 * max(ActiveRouteTimeout, PathDiscoveryTime)",
                   TimeValue (Seconds (11.2)),
                   MakeTimeAccessor (&RoutingProtocol::m_myRouteTimeout),
                   MakeTimeChecker ())
    .AddAttribute ("BlackListTimeout", "Time for which the node is put into the blacklist = RreqRetries * NetTraversalTime",
                   TimeValue (Seconds (5.6)),
                   MakeTimeAccessor (&RoutingProtocol::m_blackListTimeout),
                   MakeTimeChecker ())
    .AddAttribute ("DeletePeriod", "DeletePeriod is intended to provide an upper bound on the time for which an upstream node A "
                   "can have a neighbor B as an active next hop for destination D, while B has invalidated the route to D."
                   " = 5 * max (HelloInterval, ActiveRouteTimeout)",
                   TimeValue (Seconds (15)),
                   MakeTimeAccessor (&RoutingProtocol::m_deletePeriod),
                   MakeTimeChecker ())
    .AddAttribute ("NetDiameter", "Net diameter measures the maximum possible number of hops between two nodes in the network",
                   UintegerValue (35),
                   MakeUintegerAccessor (&RoutingProtocol::m_netDiameter),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("NetTraversalTime", "Estimate of the average net traversal time = 2 * NodeTraversalTime * NetDiameter",
                   TimeValue (Seconds (2.8)),
                   MakeTimeAccessor (&RoutingProtocol::m_netTraversalTime),
                   MakeTimeChecker ())
    .AddAttribute ("PathDiscoveryTime", "Estimate of maximum time needed to find route in network = 2 * NetTraversalTime",
                   TimeValue (Seconds (5.6)),
                   MakeTimeAccessor (&RoutingProtocol::m_pathDiscoveryTime),
                   MakeTimeChecker ())
    .AddAttribute ("MaxQueueLen", "Maximum number of packets that we allow a routing protocol to buffer.",
                   UintegerValue (64),
                   MakeUintegerAccessor (&RoutingProtocol::SetMaxQueueLen,
                                         &RoutingProtocol::GetMaxQueueLen),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("MaxQueueTime", "Maximum time packets can be queued (in seconds)",
                   TimeValue (Seconds (30)),
                   MakeTimeAccessor (&RoutingProtocol::SetMaxQueueTime,
                                     &RoutingProtocol::GetMaxQueueTime),
                   MakeTimeChecker ())
    .AddAttribute ("AllowedHelloLoss", "Number of hello messages which may be loss for valid link.",
                   UintegerValue (2),
                   MakeUintegerAccessor (&RoutingProtocol::m_allowedHelloLoss),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("GratuitousReply", "Indicates whether a gratuitous RREP should be unicast to the node originated route discovery.",
                   BooleanValue (true),
                   MakeBooleanAccessor (&RoutingProtocol::SetGratuitousReplyFlag,
                                        &RoutingProtocol::GetGratuitousReplyFlag),
                   MakeBooleanChecker ())
    .AddAttribute ("DestinationOnly", "Indicates only the destination may respond to this RREQ.",
                   BooleanValue (false),
                   MakeBooleanAccessor (&RoutingProtocol::SetDestinationOnlyFlag,
                                        &RoutingProtocol::GetDestinationOnlyFlag),
                   MakeBooleanChecker ())
    .AddAttribute ("EnableHello", "Indicates whether a hello messages enable.",
                   BooleanValue (true),
                   MakeBooleanAccessor (&RoutingProtocol::SetHelloEnable,
                                        &RoutingProtocol::GetHelloEnable),
                   MakeBooleanChecker ())
    .AddAttribute ("EnableBroadcast", "Indicates whether a broadcast data packets forwarding enable.",
                   BooleanValue (true),
                   MakeBooleanAccessor (&RoutingProtocol::SetBroadcastEnable,
                                        &RoutingProtocol::GetBroadcastEnable),
                   MakeBooleanChecker ())
    .AddAttribute ("UniformRv",
                   "Access to the underlying UniformRandomVariable",
                   StringValue ("ns3::UniformRandomVariable"),
                   MakePointerAccessor (&RoutingProtocol::m_uniformRandomVariable),
                   MakePointerChecker<UniformRandomVariable> ())
  ;
  return tid;
}

void
RoutingProtocol::SetMaxQueueLen (uint32_t len)
{
  m_maxQueueLen = len;
  m_queue.SetMaxQueueLen (len);
}

void
RoutingProtocol::SetMaxQueueTime (Time t)
{
  m_maxQueueTime = t;
  m_queue.SetQueueTimeout (t);
}

RoutingProtocol::~RoutingProtocol ()
{
}

void
RoutingProtocol::DoDispose ()
{
  m_ipv4 = nullptr;
  m_lo = nullptr;
  for (auto iter = m_socketAddresses.begin (); iter != m_socketAddresses.end (); iter++)
    {
      iter->first->Close ();
    }
  m_socketAddresses.clear ();
  for (auto iter = m_socketSubnetBroadcastAddresses.begin (); iter != m_socketSubnetBroadcastAddresses.end (); iter++)
    {
      iter->first->Close ();
    }
  m_socketSubnetBroadcastAddresses.clear ();
  Ipv4RoutingProtocol::DoDispose ();
}

void
RoutingProtocol::PrintRoutingTable (Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
{
  *stream->GetStream () << "Node: " << m_ipv4->GetObject<Node> ()->GetId ()
                        << "; Time: " << Simulator::Now ().As (unit)
                        << ", Local time: " << m_ipv4->GetObject<Node> ()->GetLocalTime ().As (unit)
                        << ", AOMDV Routing table" << std::endl;
  m_routingTable.Print (stream, unit);
  *stream->GetStream () << std::endl;
}

int64_t
RoutingProtocol::AssignStreams (int64_t stream)
{
  NS_LOG_FUNCTION (this << stream);
  m_uniformRandomVariable->SetStream (stream);
  return 1;
}

void
RoutingProtocol::Start ()
{
  NS_LOG_FUNCTION (this);
  if (m_enableHello)
    {
      m_nb.ScheduleTimer ();
    }
  m_rreqRateLimitTimer.SetFunction (&RoutingProtocol::RreqRateLimitTimerExpire, this);
  m_rreqRateLimitTimer.Schedule (Seconds (1));

  m_rerrRateLimitTimer.SetFunction (&RoutingProtocol::RerrRateLimitTimerExpire, this);
  m_rerrRateLimitTimer.Schedule (Seconds (1));
}

Ptr<Ipv4Route>
RoutingProtocol::RouteOutput (Ptr<Packet> p, const Ipv4Header &header,
                              Ptr<NetDevice> oif, Socket::SocketErrno &sockerr)
{
  NS_LOG_FUNCTION (this << header << (oif ? oif->GetIfIndex () : 0));
  if (!p)
    {
      NS_LOG_DEBUG ("Packet is == 0");
      return LoopbackRoute (header, oif);
    }
  if (m_socketAddresses.empty ())
    {
      sockerr = Socket::ERROR_NOROUTETOHOST;
      NS_LOG_LOGIC ("No aomdv interfaces");
      return Ptr<Ipv4Route> ();
    }
  sockerr = Socket::ERROR_NOTERROR;
  Ptr<Ipv4Route> route;
  Ipv4Address dst = header.GetDestination ();
  RoutingTableEntry rt;
  if (m_routingTable.LookupValidRoute (dst, rt))
    {
      RoutingTableEntry::Path *path = rt.PathFind ();
      if (path)
        {
          route = path->GetRoute ();
          NS_ASSERT (route);
          NS_LOG_DEBUG ("Exist route to " << route->GetDestination () << " from interface " << route->GetSource ());
          if (oif && route->GetOutputDevice () != oif)
            {
              NS_LOG_DEBUG ("Output device doesn't match. Dropped.");
              sockerr = Socket::ERROR_NOROUTETOHOST;
              return Ptr<Ipv4Route> ();
            }
          UpdateRouteLifeTime (dst, m_activeRouteTimeout);
          UpdatePathsLifeTime (route->GetGateway (), m_activeRouteTimeout);
          path->SetExpire (std::max (m_activeRouteTimeout, path->GetExpire ()));
          return route;
        }
    }

  // Valid route not found, in this case we return loopback. 
  uint32_t iif = (oif ? m_ipv4->GetInterfaceForDevice (oif) : -1);
  DeferredRouteOutputTag tag (iif);
  NS_LOG_DEBUG ("Valid Route not found");
  if (!p->PeekPacketTag (tag))
    {
      p->AddPacketTag (tag);
    }
  return LoopbackRoute (header, oif);
}

void
RoutingProtocol::DeferredRouteOutput (Ptr<const Packet> p, const Ipv4Header & header,
                                      UnicastForwardCallback ucb, ErrorCallback ecb)
{
  NS_LOG_FUNCTION (this << p << header);
  NS_ASSERT (p && p != Ptr<Packet> ());

  QueueEntry newEntry (p, header, ucb, ecb);
  bool result = m_queue.Enqueue (newEntry);
  if (result)
    {
      NS_LOG_LOGIC ("Add packet " << p->GetUid () << " to queue. Protocol " << (uint16_t) header.GetProtocol ());
      RoutingTableEntry rt;
      bool routeFound = m_routingTable.LookupRoute (header.GetDestination (), rt);
      if (!routeFound || (rt.GetFlag () != IN_SEARCH))
        {
          SendRequest (header.GetDestination ());
        }
    }
}

bool
RoutingProtocol::RouteInput (Ptr<const Packet> p, const Ipv4Header &header,
                             Ptr<const NetDevice> idev, const UnicastForwardCallback &ucb,
                             const MulticastForwardCallback &mcb, const LocalDeliverCallback &lcb,
                             const ErrorCallback &ecb)
{
  NS_LOG_FUNCTION (this << p->GetUid () << header.GetDestination () << idev->GetAddress ());
  if (m_socketAddresses.empty ())
    {
      NS_LOG_LOGIC ("No aomdv interfaces");
      return false;
    }
  NS_ASSERT (m_ipv4);
  NS_ASSERT (p);
  // Check if input device supports IP
  NS_ASSERT (m_ipv4->GetInterfaceForDevice (idev) >= 0);
  int32_t iif = m_ipv4->GetInterfaceForDevice (idev);

  Ipv4Address dst = header.GetDestination ();
  Ipv4Address origin = header.GetSource ();

  // Deferred route request
  if (idev == m_lo)
    {
      DeferredRouteOutputTag tag;
      if (p->PeekPacketTag (tag))
        {
          DeferredRouteOutput (p, header, ucb, ecb);
          return true;
        }
    }

  // Duplicate of own packet
  if (IsMyOwnAddress (origin))
    {
      return true;
    }

  // AOMDV is not a multicast routing protocol
  if (dst.IsMulticast ())
    {
      return false; 
    }

  // Broadcast local delivery/forwarding
  for (auto j = m_socketAddresses.begin (); j != m_socketAddresses.end (); ++j)
    {
      Ipv4InterfaceAddress iface = j->second;
      if (m_ipv4->GetInterfaceForAddress (iface.GetLocal ()) == iif)
        {
          if (dst == iface.GetBroadcast () || dst.IsBroadcast ())
            {
              if (m_dpd.IsDuplicate (p, header))
                {
                  NS_LOG_DEBUG ("Duplicated packet " << p->GetUid () << " from " << origin << ". Drop.");
                  return true;
                }
              UpdatePathsLifeTime (origin, m_activeRouteTimeout);
              Ptr<Packet> packet = p->Copy ();
              if (!lcb.IsNull ())
                {
                  NS_LOG_LOGIC ("Broadcast local delivery to " << iface.GetLocal ());
                  lcb (p, header, iif);
                }
              else
                {
                  NS_LOG_ERROR ("Unable to deliver packet locally due to null callback " << p->GetUid () << " from " << origin);
                  ecb (p, header, Socket::ERROR_NOROUTETOHOST);
                }
              if (!m_enableBroadcast)
                {
                  return true;
                }
              if (header.GetTtl () > 1)
                {
                  NS_LOG_LOGIC ("Forward broadcast. TTL " << (uint16_t) header.GetTtl ());
                  RoutingTableEntry toBroadcast;
                  if (m_routingTable.LookupRoute (dst, toBroadcast))
                    {
                      RoutingTableEntry::Path *path = toBroadcast.PathFind ();
                      if (path)
                        {
                          Ptr<Ipv4Route> route = path->GetRoute ();
                          ucb (route, packet, header);
                        }
                    }
                  else
                    {
                      NS_LOG_DEBUG ("No route to forward broadcast. Drop packet " << p->GetUid ());
                    }
                }
              else
                {
                  NS_LOG_DEBUG ("TTL exceeded. Drop packet " << p->GetUid ());
                }
              return true;
            }
        }
    }

  // Unicast local delivery
  if (m_ipv4->IsDestinationAddress (dst, iif))
    {
      UpdatePathsLifeTime (origin, m_activeRouteTimeout);
      RoutingTableEntry toOrigin;
      if (m_routingTable.LookupValidRoute (origin, toOrigin))
        {
          RoutingTableEntry::Path *origPath = toOrigin.PathFind ();
          if (origPath)
            {
              UpdatePathsLifeTime (origPath->GetNextHop (), m_activeRouteTimeout);
              m_nb.Update (origPath->GetNextHop (), m_activeRouteTimeout);
            }
        }
      if (!lcb.IsNull ())
        {
          NS_LOG_LOGIC ("Unicast local delivery to " << dst);
          lcb (p, header, iif);
        }
      else
        {
          NS_LOG_ERROR ("Unable to deliver packet locally due to null callback " << p->GetUid () << " from " << origin);
          ecb (p, header, Socket::ERROR_NOROUTETOHOST);
        }
      return true;
    }

  // Forwarding
  return Forwarding (p, header, ucb, ecb);
}

bool
RoutingProtocol::Forwarding (Ptr<const Packet> p, const Ipv4Header & header,
                             UnicastForwardCallback ucb, ErrorCallback ecb)
{
  NS_LOG_FUNCTION (this);
  Ipv4Address dst = header.GetDestination ();
  Ipv4Address origin = header.GetSource ();
  m_routingTable.Purge ();
  RoutingTableEntry toDst;
  if (m_routingTable.LookupRoute (dst, toDst))
    {
      if (toDst.GetFlag () == VALID)
        {
          RoutingTableEntry::Path *path = toDst.PathFind ();
          if (path)
            {
              Ptr<Ipv4Route> route = path->GetRoute ();
              NS_LOG_LOGIC (route->GetSource () << " forwarding to " << dst << " from " << origin << " packet " << p->GetUid ());

              UpdatePathsLifeTime (origin, m_activeRouteTimeout);
              UpdateRouteLifeTime (dst, m_activeRouteTimeout);
              path->SetExpire (std::max (m_activeRouteTimeout, path->GetExpire ()));
              UpdatePathsLifeTime (route->GetGateway (), m_activeRouteTimeout);

              RoutingTableEntry toOrigin;
              if (m_routingTable.LookupRoute (origin, toOrigin))
                {
                  RoutingTableEntry::Path *origPath = toOrigin.PathFind ();
                  if (origPath)
                    {
                      UpdatePathsLifeTime (origPath->GetNextHop (), m_activeRouteTimeout);
                      m_nb.Update (origPath->GetNextHop (), m_activeRouteTimeout);
                    }
                }
              m_nb.Update (route->GetGateway (), m_activeRouteTimeout);

              ucb (route, p, header);
              return true;
            }
        }
      else
        {
          if (toDst.GetValidSeqNo ())
            {
              SendRerrWhenNoRouteToForward (dst, toDst.GetSeqNo (), origin);
              NS_LOG_DEBUG ("Drop packet " << p->GetUid () << " because no route to forward it.");
              return false;
            }
        }
    }
  NS_LOG_LOGIC ("route not found to " << dst << ". Send RERR message.");
  NS_LOG_DEBUG ("Drop packet " << p->GetUid () << " because no route to forward it.");
  SendRerrWhenNoRouteToForward (dst, 0, origin);
  return false;
}

void
RoutingProtocol::SetIpv4 (Ptr<Ipv4> ipv4)
{
  NS_ASSERT (ipv4);
  NS_ASSERT (!m_ipv4);

  m_ipv4 = ipv4;

  // Create lo route.
  NS_ASSERT (m_ipv4->GetNInterfaces () == 1 && m_ipv4->GetAddress (0, 0).GetLocal () == Ipv4Address ("127.0.0.1"));
  m_lo = m_ipv4->GetNetDevice (0);
  NS_ASSERT (m_lo);

  RoutingTableEntry rt (/*dst=*/ Ipv4Address::GetLoopback (), /*validSeqNo=*/ true, /*seqno=*/ 0, 
                        /*lifeTime=*/ Simulator::GetMaximumSimulationTime ());
  rt.PathInsert (/*device=*/ m_lo, /*nextHop=*/ Ipv4Address::GetLoopback (), /*hop=*/ 1, 
                 /*expireTime=*/ Simulator::GetMaximumSimulationTime (), 
                 /*lastHop=*/ Ipv4Address::GetLoopback (), 
                 /*iface=*/ Ipv4InterfaceAddress (Ipv4Address::GetLoopback (), Ipv4Mask ("255.0.0.0")));
  m_routingTable.AddRoute (rt);

  Simulator::ScheduleNow (&RoutingProtocol::Start, this);
}

void
RoutingProtocol::NotifyTxError (WifiMacDropReason reason, Ptr<const WifiMpdu> mpdu)
{
  m_nb.GetTxErrorCallback () (mpdu->GetHeader ());
}

void
RoutingProtocol::NotifyInterfaceUp (uint32_t i)
{
  NS_LOG_FUNCTION (this << m_ipv4->GetAddress (i, 0).GetLocal ());
  Ptr<Ipv4L3Protocol> l3 = m_ipv4->GetObject<Ipv4L3Protocol> ();
  if (l3->GetNAddresses (i) > 1)
    {
      NS_LOG_WARN ("AOMDV does not work with more then one address per each interface.");
    }
  Ipv4InterfaceAddress iface = l3->GetAddress (i, 0);
  if (iface.GetLocal () == Ipv4Address ("127.0.0.1"))
    {
      return;
    }
 
  // Create a socket to listen only on this interface
  Ptr<Socket> socket = Socket::CreateSocket (GetObject<Node> (),
                                             UdpSocketFactory::GetTypeId ());
  NS_ASSERT (socket);
  socket->SetRecvCallback (MakeCallback (&RoutingProtocol::RecvAomdv, this));
  socket->Bind (InetSocketAddress (Ipv4Address::GetAny (), AOMDV_PORT));
  socket->BindToNetDevice (l3->GetNetDevice (i));
  socket->SetAllowBroadcast (true);
  socket->SetAttribute ("IpTtl", UintegerValue (1));
  m_socketAddresses.insert (std::make_pair (socket, iface));

  // create also a subnet broadcast socket
  socket = Socket::CreateSocket (GetObject<Node> (),
                                 UdpSocketFactory::GetTypeId ());
  NS_ASSERT (socket);
  socket->SetRecvCallback (MakeCallback (&RoutingProtocol::RecvAomdv, this));
  socket->Bind (InetSocketAddress (iface.GetBroadcast (), AOMDV_PORT));
  socket->BindToNetDevice (l3->GetNetDevice (i));
  socket->SetAllowBroadcast (true);
  socket->SetAttribute ("IpTtl", UintegerValue (1));
  m_socketSubnetBroadcastAddresses.insert (std::make_pair (socket, iface));

  // Add local broadcast record to the routing table
  Ptr<NetDevice> dev = m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (iface.GetLocal ()));
  RoutingTableEntry rt (/*dst=*/ iface.GetBroadcast (), /*validSeqNo=*/ true, /*seqno=*/ 0, 
                        /*lifeTime=*/ Simulator::GetMaximumSimulationTime ());
  rt.PathInsert (/*device=*/ dev, /*nextHop=*/ iface.GetBroadcast (), /*hop=*/ 1, 
                 /*expireTime=*/ Simulator::GetMaximumSimulationTime (), 
                 /*lastHop=*/ iface.GetBroadcast (), 
                 /*iface=*/ iface);
  m_routingTable.AddRoute (rt);

  if (l3->GetInterface (i)->GetArpCache ())
    {
      m_nb.AddArpCache (l3->GetInterface (i)->GetArpCache ());
    }

  // Allow neighbor manager use this interface for layer 2 feedback if possible
  Ptr<WifiNetDevice> wifi = dev->GetObject<WifiNetDevice> ();
  if (!wifi)
    {
      return;
    }
  Ptr<WifiMac> mac = wifi->GetMac ();
  if (!mac)
    {
      return;
    }

  mac->TraceConnectWithoutContext ("DroppedMpdu",
                                   MakeCallback (&RoutingProtocol::NotifyTxError, this));
}

void
RoutingProtocol::NotifyInterfaceDown (uint32_t i)
{
  NS_LOG_FUNCTION (this << m_ipv4->GetAddress (i, 0).GetLocal ());

  // Disable layer 2 link state monitoring (if possible)
  Ptr<Ipv4L3Protocol> l3 = m_ipv4->GetObject<Ipv4L3Protocol> ();
  Ptr<NetDevice> dev = l3->GetNetDevice (i);
  Ptr<WifiNetDevice> wifi = dev->GetObject<WifiNetDevice> ();
  if (wifi)
    {
      Ptr<WifiMac> mac = wifi->GetMac ()->GetObject<AdhocWifiMac> ();
      if (mac)
        {
          mac->TraceDisconnectWithoutContext ("DroppedMpdu",
                                              MakeCallback (&RoutingProtocol::NotifyTxError, this));
          m_nb.DelArpCache (l3->GetInterface (i)->GetArpCache ());
        }
    }

  // Close socket 
  Ptr<Socket> socket = FindSocketWithInterfaceAddress (m_ipv4->GetAddress (i, 0));
  NS_ASSERT (socket);
  socket->Close ();
  m_socketAddresses.erase (socket);

  // Close subnet broadcast socket
  socket = FindSubnetBroadcastSocketWithInterfaceAddress (m_ipv4->GetAddress (i, 0));
  NS_ASSERT (socket);
  socket->Close ();
  m_socketSubnetBroadcastAddresses.erase (socket);

  if (m_socketAddresses.empty ())
    {
      NS_LOG_LOGIC ("No aomdv interfaces");
      m_htimer.Cancel ();
      m_nb.Clear ();
      m_routingTable.Clear ();
      return;
    }
  m_routingTable.DeleteAllRoutesFromInterface (m_ipv4->GetAddress (i, 0));
}

void
RoutingProtocol::NotifyAddAddress (uint32_t i, Ipv4InterfaceAddress address)
{
  NS_LOG_FUNCTION (this << " interface " << i << " address " << address);
  Ptr<Ipv4L3Protocol> l3 = m_ipv4->GetObject<Ipv4L3Protocol> ();
  if (!l3->IsUp (i))
    {
      return;
    }
  if (l3->GetNAddresses (i) == 1)
    {
      Ipv4InterfaceAddress iface = l3->GetAddress (i, 0);
      Ptr<Socket> socket = FindSocketWithInterfaceAddress (iface);
      if (!socket)
        {
          if (iface.GetLocal () == Ipv4Address ("127.0.0.1"))
            {
              return;
            }
          // Create a socket to listen only on this interface
          Ptr<Socket> s1 = Socket::CreateSocket (GetObject<Node> (),
                                                 UdpSocketFactory::GetTypeId ());
          NS_ASSERT (s1);
          s1->SetRecvCallback (MakeCallback (&RoutingProtocol::RecvAomdv, this));
          s1->Bind (InetSocketAddress (iface.GetLocal (), AOMDV_PORT));
          s1->BindToNetDevice (l3->GetNetDevice (i));
          s1->SetAllowBroadcast (true);
          m_socketAddresses.insert (std::make_pair (s1, iface));

          // create also a subnet directed broadcast socket
          Ptr<Socket> s2 = Socket::CreateSocket (GetObject<Node> (),
                                                 UdpSocketFactory::GetTypeId ());
          NS_ASSERT (s2);
          s2->SetRecvCallback (MakeCallback (&RoutingProtocol::RecvAomdv, this));
          s2->Bind (InetSocketAddress (iface.GetBroadcast (), AOMDV_PORT));
          s2->BindToNetDevice (l3->GetNetDevice (i));
          s2->SetAllowBroadcast (true);
          s2->SetAttribute ("IpTtl", UintegerValue (1));
          m_socketSubnetBroadcastAddresses.insert (std::make_pair (s2, iface));

          // Add local broadcast record to the routing table
          Ptr<NetDevice> dev = m_ipv4->GetNetDevice (
              m_ipv4->GetInterfaceForAddress (iface.GetLocal ()));
          RoutingTableEntry rt (/*dst=*/ iface.GetBroadcast (), /*validSeqNo=*/ true, /*seqno=*/ 0, 
                              /*lifeTime=*/ Simulator::GetMaximumSimulationTime ());
          rt.PathInsert (/*device=*/ dev, /*nextHop=*/ iface.GetBroadcast (), /*hop=*/ 1, 
                         /*expireTime=*/ Simulator::GetMaximumSimulationTime (), 
                         /*lastHop=*/ iface.GetBroadcast (), 
                         /*iface=*/ iface);
          m_routingTable.AddRoute (rt);
        }
    }
  else
    {
      NS_LOG_LOGIC ("AOMDV does not work with more then one address per each interface. Ignore added address");
    }
}

void
RoutingProtocol::NotifyRemoveAddress (uint32_t i, Ipv4InterfaceAddress address)
{
  NS_LOG_FUNCTION (this);
  Ptr<Socket> socket = FindSocketWithInterfaceAddress (address);
  if (socket)
    {
      m_routingTable.DeleteAllRoutesFromInterface (address);
      socket->Close ();
      m_socketAddresses.erase (socket);

      Ptr<Socket> unicastSocket = FindSubnetBroadcastSocketWithInterfaceAddress (address);
      if (unicastSocket)
        {
          unicastSocket->Close ();
          m_socketSubnetBroadcastAddresses.erase (unicastSocket);
        }

      Ptr<Ipv4L3Protocol> l3 = m_ipv4->GetObject<Ipv4L3Protocol> ();
      if (l3->GetNAddresses (i))
        {
          Ipv4InterfaceAddress iface = l3->GetAddress (i, 0);
          // Create a socket to listen only on this interface
          Ptr<Socket> s1 = Socket::CreateSocket (GetObject<Node> (),
                                                 UdpSocketFactory::GetTypeId ());
          NS_ASSERT (s1);
          s1->SetRecvCallback (MakeCallback (&RoutingProtocol::RecvAomdv, this));
          s1->Bind (InetSocketAddress (iface.GetLocal (), AOMDV_PORT));
          s1->BindToNetDevice (l3->GetNetDevice (i));
          s1->SetAllowBroadcast (true);
          s1->SetAttribute ("IpTtl", UintegerValue (1));
          m_socketAddresses.insert (std::make_pair (s1, iface));

          // create also a unicast socket
          Ptr<Socket> s2 = Socket::CreateSocket (GetObject<Node> (),
                                                 UdpSocketFactory::GetTypeId ());
          NS_ASSERT (s2);
          s2->SetRecvCallback (MakeCallback (&RoutingProtocol::RecvAomdv, this));
          s2->Bind (InetSocketAddress (iface.GetBroadcast (), AOMDV_PORT));
          s2->BindToNetDevice (l3->GetNetDevice (i));
          s2->SetAllowBroadcast (true);
          s2->SetAttribute ("IpTtl", UintegerValue (1));
          m_socketSubnetBroadcastAddresses.insert (std::make_pair (s2, iface));

          // Add local broadcast record to the routing table
          Ptr<NetDevice> dev = m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (iface.GetLocal ()));
          RoutingTableEntry rt (/*dst=*/ iface.GetBroadcast (), /*validSeqNo=*/ true, /*seqno=*/ 0, 
                              /*lifeTime=*/ Simulator::GetMaximumSimulationTime ());
          rt.PathInsert (/*device=*/ dev, /*nextHop=*/ iface.GetBroadcast (), /*hop=*/ 1, 
                         /*expireTime=*/ Simulator::GetMaximumSimulationTime (), 
                         /*lastHop=*/ iface.GetBroadcast (), 
                         /*iface=*/ iface);
          m_routingTable.AddRoute (rt);
        }
      if (m_socketAddresses.empty ())
        {
          NS_LOG_LOGIC ("No aomdv interfaces");
          m_htimer.Cancel ();
          m_nb.Clear ();
          m_routingTable.Clear ();
          return;
        }
    }
  else
    {
      NS_LOG_LOGIC ("Remove address not participating in AOMDV operation");
    }
}

bool
RoutingProtocol::IsMyOwnAddress (Ipv4Address src)
{
  NS_LOG_FUNCTION (this << src);
  for (auto j = m_socketAddresses.begin (); j != m_socketAddresses.end (); ++j)
    {
      Ipv4InterfaceAddress iface = j->second;
      if (src == iface.GetLocal ())
        {
          return true;
        }
    }
  return false;
}

Ptr<Ipv4Route> 
RoutingProtocol::LoopbackRoute (const Ipv4Header & hdr, Ptr<NetDevice> oif) const
{
  NS_LOG_FUNCTION (this << hdr);
  NS_ASSERT (m_lo);
  Ptr<Ipv4Route> rt = Create<Ipv4Route> ();
  rt->SetDestination (hdr.GetDestination ());

  auto j = m_socketAddresses.begin ();
  if (oif)
    {
      for (j = m_socketAddresses.begin (); j != m_socketAddresses.end (); ++j)
        {
          Ipv4Address addr = j->second.GetLocal ();
          int32_t interface = m_ipv4->GetInterfaceForAddress (addr);
          if (oif == m_ipv4->GetNetDevice (static_cast<uint32_t> (interface)))
            {
              rt->SetSource (addr);
              break;
            }
        }
    }
  else
    {
      rt->SetSource (j->second.GetLocal ());
    }
  NS_ASSERT_MSG (rt->GetSource () != Ipv4Address (), "Valid AOMDV source address not found");
  rt->SetGateway (Ipv4Address ("127.0.0.1"));
  rt->SetOutputDevice (m_lo);
  return rt;
}

void
RoutingProtocol::SendRequest (Ipv4Address dst)
{
  NS_LOG_FUNCTION (this << dst);
  if (m_rreqCount == m_rreqRateLimit)
    {
      Simulator::Schedule (m_rreqRateLimitTimer.GetDelayLeft () + MicroSeconds (100),
                           &RoutingProtocol::SendRequest, this, dst);
      return;
    }
  else
    {
      m_rreqCount++;
    }

  RreqHeader rreqHeader;
  rreqHeader.SetDst (dst);

  RoutingTableEntry rt;
  uint16_t ttl = m_ttlStart;
  if (m_routingTable.LookupRoute (dst, rt))
    {
      if (rt.GetFlag () != IN_SEARCH)
        {
          ttl = std::min<uint16_t> (rt.GetLastHopCount () + m_ttlIncrement, m_netDiameter);
        }
      else
        {
          ttl = rt.GetLastHopCount () + m_ttlIncrement;
          if (ttl > m_ttlThreshold)
            {
              ttl = m_netDiameter;
            }
        }
      if (ttl == m_netDiameter)
        {
          rt.IncrementRreqCnt ();
        }
      if (rt.GetValidSeqNo ())
        {
          rreqHeader.SetDstSeqno (rt.GetSeqNo ());
        }
      else
        {
          rreqHeader.SetUnknownSeqno (true);
        }
      rt.SetLastHopCount (ttl);
      rt.SetFlag (IN_SEARCH);
      rt.SetLifeTime (m_pathDiscoveryTime);
      m_routingTable.Update (rt);
    }
  else
    {
      rreqHeader.SetUnknownSeqno (true);
      RoutingTableEntry newEntry (/*dst=*/ dst, /*validSeqNo=*/ false, /*seqno=*/ 0, /*lifeTime=*/ m_pathDiscoveryTime);
      if (ttl == m_netDiameter)
        {
          newEntry.IncrementRreqCnt ();
        }
      newEntry.SetFlag (IN_SEARCH);
      m_routingTable.AddRoute (newEntry);
    }
  if (m_gratuitousReply)
    {
      rreqHeader.SetGratiousRrep (true);
    }
  if (m_destinationOnly)
    {
      rreqHeader.SetDestinationOnly (true);
    }

  m_seqNo++;
  rreqHeader.SetOriginSeqno (m_seqNo);
  m_requestId++;
  rreqHeader.SetId (m_requestId);
  rreqHeader.SetHopCount (0);

  // Send RREQ as subnet directed broadcast from each interface used by aomdv
  for (auto j = m_socketAddresses.begin (); j != m_socketAddresses.end (); ++j)
    {
      Ptr<Socket> socket = j->first;
      Ipv4InterfaceAddress iface = j->second;

      rreqHeader.SetOrigin (iface.GetLocal ());
      m_rreqIdCache.IsDuplicate (iface.GetLocal (), m_requestId);

      Ptr<Packet> packet = Create<Packet> ();
      SocketIpTtlTag tag;
      tag.SetTtl (ttl);
      packet->ReplacePacketTag (tag);
      packet->AddHeader (rreqHeader);
      TypeHeader tHeader (AOMDVTYPE_RREQ);
      packet->AddHeader (tHeader);

      Ipv4Address destination;
      if (iface.GetMask () == Ipv4Mask::GetOnes ())
        {
          destination = Ipv4Address ("255.255.255.255");
        }
      else
        { 
          destination = iface.GetBroadcast ();
        }
      NS_LOG_DEBUG ("Send RREQ with id " << rreqHeader.GetId () << " to socket");
      m_lastBcastTime = Simulator::Now ();
      Simulator::Schedule (MilliSeconds (m_uniformRandomVariable->GetInteger (0, 10)),
                           &RoutingProtocol::SendTo, this, socket, packet, destination); 
    }
  ScheduleRreqRetry (dst);
}

void
RoutingProtocol::SendTo (Ptr<Socket> socket, Ptr<Packet> packet, Ipv4Address destination)
{
  SocketIpTtlTag tag;
  packet->RemovePacketTag(tag);
  socket->SendTo (packet, 0, InetSocketAddress (destination, AOMDV_PORT));
}

void
RoutingProtocol::ScheduleRreqRetry (Ipv4Address dst)
{
  NS_LOG_FUNCTION (this << dst);
  if (m_addressReqTimer.find (dst) == m_addressReqTimer.end ())
    {
      Timer timer (Timer::CANCEL_ON_DESTROY);
      m_addressReqTimer[dst] = timer;
    }
  m_addressReqTimer[dst].SetFunction (&RoutingProtocol::RouteRequestTimerExpire, this);
  m_addressReqTimer[dst].Remove ();
  m_addressReqTimer[dst].SetArguments (dst);
  RoutingTableEntry rt;
  m_routingTable.LookupRoute (dst, rt);
  Time retry;
  if (rt.GetLastHopCount () < m_netDiameter)
    {
      retry = 2 * m_nodeTraversalTime * (rt.GetLastHopCount () + m_timeoutBuffer);
    }
  else
    {
      uint16_t backoffFactor = (rt.GetRreqCnt () > 0) ? (rt.GetRreqCnt () - 1) : 0;
      retry = m_netTraversalTime * (1 << backoffFactor);
    }
  m_addressReqTimer[dst].Schedule (retry);
  NS_LOG_LOGIC ("Scheduled RREQ retry in " << retry.GetSeconds () << " seconds");
}

void
RoutingProtocol::RecvAomdv (Ptr<Socket> socket)
{
  NS_LOG_FUNCTION (this << socket);
  Address sourceAddress;
  Ptr<Packet> packet = socket->RecvFrom (sourceAddress);
  InetSocketAddress inetSourceAddr = InetSocketAddress::ConvertFrom (sourceAddress);
  Ipv4Address sender = inetSourceAddr.GetIpv4 ();
  Ipv4Address receiver;

  if (m_socketAddresses.find (socket) != m_socketAddresses.end ())
    {
      receiver = m_socketAddresses[socket].GetLocal ();
    }
  else if (m_socketSubnetBroadcastAddresses.find (socket) != m_socketSubnetBroadcastAddresses.end ())
    {
      receiver = m_socketSubnetBroadcastAddresses[socket].GetLocal ();
    }
  else
    {
      NS_ASSERT_MSG (false, "Received a packet from an unknown socket");
    }
  NS_LOG_DEBUG ("AOMDV node " << this << " received a AOMDV packet from " << sender << " to " << receiver);

  UpdateRouteToNeighbor (sender, receiver);
  TypeHeader tHeader (AOMDVTYPE_RREQ);
  packet->RemoveHeader (tHeader);
  if (!tHeader.IsValid ())
    {
      NS_LOG_DEBUG ("AOMDV message " << packet->GetUid () << " with unknown type received: " << tHeader.Get () << ". Drop");
      return;
    }
  switch (tHeader.Get ())
    {
    case AOMDVTYPE_RREQ:
      {
        RecvRequest (packet, receiver, sender);
        break;
      }
    case AOMDVTYPE_RREP:
      {
        RecvReply (packet, receiver, sender);
        break;
      }
    case AOMDVTYPE_RERR:
      {
        RecvError (packet, sender);
        break;
      }
    case AOMDVTYPE_RREP_ACK:
      {
        RecvReplyAck (sender);
        break;
      }
    }
}


bool
RoutingProtocol::UpdatePathsLifeTime (Ipv4Address addr, Time lifetime)
{
  NS_LOG_FUNCTION (this << addr << lifetime);
  RoutingTableEntry rt;
  if (m_routingTable.LookupRoute (addr, rt))
    {
      if (rt.GetFlag () == VALID)
        {
          NS_LOG_DEBUG ("Updating VALID route");
          rt.SetRreqCnt (0);
          rt.SetLifeTime (std::max (lifetime, rt.GetLifeTime ()));
          rt.UpdatePathsLifeTime (lifetime);
          m_routingTable.Update (rt);
          return true;
        }
    }
  return false;
}
bool
RoutingProtocol::UpdateRouteLifeTime (Ipv4Address addr, Time lifetime)
{
  // Called on every RouteOutput. Must NOT touch path lifetimes — that would
  // push every path's expiry forward and prevent Purge() from ever removing
  // stale paths, which leads to unbounded path-list growth.
  RoutingTableEntry* rt = m_routingTable.LookupRoutePtr (addr);
  if (rt == nullptr)
    {
      return false;
    }
  if (rt->GetFlag () != VALID)
    {
      return false;
    }
  rt->SetLifeTime (std::max (lifetime, rt->GetLifeTime ()));
  return true;
}

void
RoutingProtocol::UpdateRouteToNeighbor (Ipv4Address sender, Ipv4Address receiver)
{
  NS_LOG_FUNCTION (this << "sender " << sender << " receiver " << receiver);
  RoutingTableEntry toNeighbor;
  if (!m_routingTable.LookupRoute (sender, toNeighbor))
    {
      Ptr<NetDevice> dev = m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (receiver));
      RoutingTableEntry newEntry (/*dst=*/ sender, /*validSeqNo=*/ false, /*seqno=*/ 0, 
                                  /*lifeTime=*/ m_activeRouteTimeout);
      newEntry.PathInsert (/*device=*/ dev, /*nextHop=*/ sender, /*hop=*/ 1, 
                           /*expireTime=*/ m_activeRouteTimeout, 
                           /*lastHop=*/ sender, 
                           /*iface=*/ m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0));
      m_routingTable.AddRoute (newEntry);
    }
  else
    {
      Ptr<NetDevice> dev = m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (receiver));
      std::vector<RoutingTableEntry::Path> paths;
      bool validPath = false;
      toNeighbor.GetPaths (paths);
      for (auto i = paths.begin (); i != paths.end (); ++i)
        {
          if ((i->GetHopCount () == 1) && (i->GetOutputDevice () == dev))
            {
              validPath = true;
              break;
            }
        }
      if (toNeighbor.GetValidSeqNo () && validPath)
        {
          toNeighbor.SetLifeTime (std::max (m_activeRouteTimeout, toNeighbor.GetLifeTime ()));
          toNeighbor.UpdatePathsLifeTime (m_activeRouteTimeout);
          m_routingTable.Update (toNeighbor);
        }
      else
        {
          RoutingTableEntry newEntry (/*dst=*/ sender, /*validSeqNo=*/ false, /*seqno=*/ 0, 
                                      /*lifeTime=*/ std::max (m_activeRouteTimeout, toNeighbor.GetLifeTime ()));
          newEntry.PathInsert (/*device=*/ dev, /*nextHop=*/ sender, /*hop=*/ 1, 
                               /*expireTime=*/ std::max (m_activeRouteTimeout, toNeighbor.GetLifeTime ()), 
                               /*lastHop=*/ sender, 
                               /*iface=*/ m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0));
          m_routingTable.Update (newEntry);
        }
    }
}

void
RoutingProtocol::RecvRequest (Ptr<Packet> p, Ipv4Address receiver, Ipv4Address src)
{
  NS_LOG_FUNCTION (this);
  bool killRequestPropagation = false;
  RreqHeader rreqHeader;
  p->RemoveHeader (rreqHeader);
  IdCache::UniqueId* b = nullptr;
  RoutingTableEntry::Path* reversePath = nullptr;
  RoutingTableEntry::Path* erp = nullptr;

  // A node ignores all RREQs received from any node in its blacklist
  RoutingTableEntry toPrev;
  if (m_routingTable.LookupRoute (src, toPrev))
    {
      if (toPrev.IsUnidirectional ())
        {
          NS_LOG_DEBUG ("Ignoring RREQ from node in blacklist");
          return;
        }
    }

  uint32_t id = rreqHeader.GetId ();
  Ipv4Address origin = rreqHeader.GetOrigin ();

  if ((b = m_rreqIdCache.GetId (origin, id)) == nullptr)
    {
      m_rreqIdCache.InsertId (origin, id);
      b = m_rreqIdCache.GetId (origin, id);
    }
  else
    {
      killRequestPropagation = true;
    }
  
  if (rreqHeader.GetHopCount () == 0) 
    {
      rreqHeader.SetFirstHop (receiver);
    }
  uint8_t hop = rreqHeader.GetHopCount () + 1;

  RoutingTableEntry toOrigin;
  Ptr<NetDevice> dev = m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (receiver));
  if (!m_routingTable.LookupRoute (origin, toOrigin))
    {
      RoutingTableEntry newEntry (/*dst=*/ origin, /*validSeqNo=*/ true, /*seqNo=*/ rreqHeader.GetOriginSeqno (),
                                  /*lifeTime=*/ Time (2 * m_netTraversalTime - 2 * hop * m_nodeTraversalTime));
      reversePath = newEntry.PathInsert (dev, src, rreqHeader.GetHopCount () + 1, 
                                         Time (2 * m_netTraversalTime - 2 * hop * m_nodeTraversalTime), 
                                         rreqHeader.GetFirstHop (), 
                                         m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0));
      m_routingTable.AddRoute (newEntry);
    }
  else
    {
      if (toOrigin.GetValidSeqNo ())
        {
          if (int32_t (rreqHeader.GetOriginSeqno ()) - int32_t (toOrigin.GetSeqNo ()) > 0)
            {
              toOrigin.SetSeqNo (rreqHeader.GetOriginSeqno ());
              toOrigin.SetAdvertisedHopCount (INFINITY2);
              toOrigin.PathAllDelete ();
              toOrigin.SetFlag (VALID);
              reversePath = toOrigin.PathInsert (dev, src, rreqHeader.GetHopCount () + 1, 
                                                 Time (2 * m_netTraversalTime - 2 * hop * m_nodeTraversalTime), 
                                                 rreqHeader.GetFirstHop (), 
                                                 m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0));
              toOrigin.SetLastHopCount (toOrigin.PathGetMaxHopCount ());           
              m_routingTable.Update (toOrigin);
            }
          else if ((int32_t (rreqHeader.GetOriginSeqno ()) == int32_t (toOrigin.GetSeqNo ())) &&
                   (toOrigin.GetAdvertisedHopCount () > rreqHeader.GetHopCount ()))
            {
              if (toOrigin.GetFlag () == VALID)
                {
                  if ((reversePath = toOrigin.PathLookupDisjoint (src, rreqHeader.GetFirstHop ()))) 
                    {
                      if (reversePath->GetHopCount () == (rreqHeader.GetHopCount () + 1))
                        {
                          reversePath->SetExpire (std::max (reversePath->GetExpire (), Time (2 * m_netTraversalTime - 2 * hop * m_nodeTraversalTime)));
                          m_routingTable.Update (toOrigin);
                        }
                    }
                  else if (toOrigin.PathNewDisjoint (src, rreqHeader.GetFirstHop ()))
                    {
                      if ((toOrigin.GetNumberofPaths () < AOMDV_MAX_PATHS) &&
                          (((rreqHeader.GetHopCount () + 1) - toOrigin.PathGetMinHopCount ()) <= AOMDV_PRIM_ALT_PATH_LENGTH_DIFF))
                        {
                          reversePath = toOrigin.PathInsert (dev, src, rreqHeader.GetHopCount () + 1, 
                                                             Time (2 * m_netTraversalTime - 2 * hop * m_nodeTraversalTime), 
                                                             rreqHeader.GetFirstHop (), 
                                                             m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0));
                          toOrigin.SetLastHopCount (toOrigin.PathGetMaxHopCount ());
                          m_routingTable.Update (toOrigin);
                        }
                      if (((rreqHeader.GetHopCount () + 1) - toOrigin.PathGetMinHopCount ()) > AOMDV_PRIM_ALT_PATH_LENGTH_DIFF) 
                        {
                          return;
                        }
                    }
                  else if ((IsMyOwnAddress (rreqHeader.GetDst ())) && 
                           (((erp = toOrigin.PathLookupLastHop (rreqHeader.GetFirstHop ())) == nullptr) ||
                            ((rreqHeader.GetHopCount () + 1) > erp->GetHopCount ())))  
                    {
                      return;
                    }
                }
              else
                {
                  return;
                } 
            }
        }
    }

  RoutingTableEntry toNeighbor;
  if (!m_routingTable.LookupRoute (src, toNeighbor))
    {
      NS_LOG_DEBUG ("Neighbor:" << src << " not found in routing table. Creating an entry"); 
      RoutingTableEntry newEntry (/*dst=*/ src, /*validSeqNo=*/ false, /*seqNo=*/ rreqHeader.GetOriginSeqno (),
                                  /*lifeTime=*/ m_activeRouteTimeout);
      newEntry.PathInsert (dev, src, 1, 
                           m_activeRouteTimeout, src, 
                           m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0));
      m_routingTable.AddRoute (newEntry);
    }
  else
    {
      toNeighbor.SetLifeTime (m_activeRouteTimeout);
      toNeighbor.SetValidSeqNo (false);
      toNeighbor.SetSeqNo (rreqHeader.GetOriginSeqno ()); 
      toNeighbor.SetFlag (VALID);
      RoutingTableEntry::Path *pPath = toNeighbor.PathLookup (src);
      if (pPath)
        {
          pPath->SetOutputDevice (m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (receiver)));
          pPath->SetInterface (m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0));
          pPath->SetHopCount (1);
          pPath->SetNextHop (src);
        }
      m_routingTable.Update (toNeighbor);
    }
  m_nb.Update (src, Time (m_allowedHelloLoss * m_helloInterval));

  NS_LOG_LOGIC (receiver << " receive RREQ with hop count " << static_cast<uint32_t> (rreqHeader.GetHopCount ()) 
                         << " ID " << rreqHeader.GetId ()
                         << " to destination " << rreqHeader.GetDst ());

  if (IsMyOwnAddress (rreqHeader.GetDst ()))
    {
      m_routingTable.LookupRoute (origin, toOrigin);
      NS_LOG_DEBUG ("Send reply since I am the destination");
      SendReply (rreqHeader, toOrigin, src);
      return;
    }
  
  RoutingTableEntry toDst;
  Ipv4Address dst = rreqHeader.GetDst ();
  if (m_routingTable.LookupRoute (dst, toDst))
    {
      RoutingTableEntry::Path *dstFirstPath = toDst.PathFind ();
      if (dstFirstPath && dstFirstPath->GetNextHop () == src)
        {
          NS_LOG_DEBUG ("Drop RREQ from " << src << ", dest next hop " << dstFirstPath->GetNextHop ());
          return;
        }
      if ((rreqHeader.GetUnknownSeqno () || (int32_t (toDst.GetSeqNo ()) - int32_t (rreqHeader.GetDstSeqno ()) >= 0))
          && toDst.GetValidSeqNo ())
        {
          if (!rreqHeader.GetDestinationOnly () && toDst.GetFlag () == VALID)
            {
              if (reversePath)
                {
                  #ifdef AOMDV_NODE_DISJOINT_PATHS
                  if (b && b->count == 0)
                    {
                      b->count = 1;
                      if (toDst.GetAdvertisedHopCount () == INFINITY2)
                        {
                          toDst.SetAdvertisedHopCount (toDst.PathGetMaxHopCount ());
                        }
                      RoutingTableEntry::Path *forwardPath = toDst.PathFind ();
                      if (forwardPath)
                        {
                          toDst.SetError (true);
                          m_routingTable.Update (toDst);
                          SendReplyByIntermediateNode (toDst, toOrigin, rreqHeader.GetGratiousRrep (), forwardPath->GetExpire (), 
                                                       forwardPath->GetLastHop (), rreqHeader.GetId ());
                          return;
                        }
                    }
                  #endif
                  #ifdef AOMDV_LINK_DISJOINT_PATHS
                  RoutingTableEntry::Path *forwardPath = nullptr;
                  std::vector<RoutingTableEntry::Path> paths;
                  toDst.GetPaths (paths);
                  AOMDVRoute rt;
                  for (auto i = paths.begin (); i != paths.end (); ++i)
                    {
                      if (b && !(b->ForwardPathLookup (i->GetNextHop (), rt, i->GetLastHop ())))
                        {
                          forwardPath = const_cast<RoutingTableEntry::Path*> (&(*i));
                          break;
                        }
                    }
                  if (forwardPath && b && !(b->ReversePathLookup (reversePath->GetNextHop (), rt, reversePath->GetLastHop ())))
                    {
                      b->ReversePathInsert (reversePath->GetNextHop (), reversePath->GetLastHop ());
                      b->ForwardPathInsert (forwardPath->GetNextHop (), forwardPath->GetLastHop ());
                      if (toDst.GetAdvertisedHopCount () == INFINITY2)
                        {
                          toDst.SetAdvertisedHopCount (toDst.PathGetMaxHopCount ());
                        }
                      toDst.SetError (true);
                      m_routingTable.Update (toDst);
                      SendReplyByIntermediateNode (toDst, toOrigin, rreqHeader.GetGratiousRrep (), forwardPath->GetExpire (), 
                                                   forwardPath->GetLastHop (), rreqHeader.GetId ());
                      return;
                    }
                  #endif
                }
            }
          rreqHeader.SetDstSeqno (toDst.GetSeqNo ());
          rreqHeader.SetUnknownSeqno (false);
        }
    }   
  if (killRequestPropagation)
    {
      return;
    }
  else
    {
      if (toOrigin.GetAdvertisedHopCount () == INFINITY2)
        {
          toOrigin.SetAdvertisedHopCount (toOrigin.PathGetMaxHopCount ());
          m_routingTable.Update (toOrigin);
        }
      rreqHeader.SetHopCount (toOrigin.GetAdvertisedHopCount ());
      #ifdef AOMDV_NODE_DISJOINT_PATHS
      RoutingTableEntry::Path *origPath = toOrigin.PathFind ();
      if (origPath)
        {
          rreqHeader.SetFirstHop (origPath->GetLastHop ());
        }
      #endif
      for (auto j = m_socketAddresses.begin (); j != m_socketAddresses.end (); ++j)
        {
          Ptr<Socket> socket = j->first;
          Ipv4InterfaceAddress iface = j->second;
          Ptr<Packet> packet = Create<Packet> ();
          packet->AddHeader (rreqHeader);
          TypeHeader tHeader (AOMDVTYPE_RREQ);
          packet->AddHeader (tHeader);

          Ipv4Address destination;
          if (iface.GetMask () == Ipv4Mask::GetOnes ())
            {
              destination = Ipv4Address ("255.255.255.255");
            }
          else
            { 
              destination = iface.GetBroadcast ();
            }
          m_lastBcastTime = Simulator::Now ();
          Simulator::Schedule (MilliSeconds (m_uniformRandomVariable->GetInteger (0, 10)), 
                               &RoutingProtocol::SendTo, this, socket, packet, destination); 
        }
    }
}

void
RoutingProtocol::SendReply (RreqHeader const & rreqHeader, RoutingTableEntry & toOrigin,
                            Ipv4Address firstHop)
{
  NS_LOG_FUNCTION (this << toOrigin.GetDestination ());
  if (!rreqHeader.GetUnknownSeqno () && (rreqHeader.GetDstSeqno () == m_seqNo + 1))
    {
      m_seqNo++;
    }
  RrepHeader rrepHeader (/*prefixSize=*/ 0, /*hops=*/ 0, /*dst=*/ rreqHeader.GetDst (),
                         /*dstSeqNo=*/ m_seqNo, /*origin=*/ toOrigin.GetDestination (), 
                         /*requestId=*/ rreqHeader.GetId (), /*firstHop=*/ firstHop,
                         /*lifeTime=*/ m_myRouteTimeout);
  Ptr<Packet> packet = Create<Packet> ();
  packet->AddHeader (rrepHeader);
  TypeHeader tHeader (AOMDVTYPE_RREP);
  packet->AddHeader (tHeader);
  RoutingTableEntry::Path *origPath = toOrigin.PathFind ();
  if (!origPath)
    {
      return;
    }
  Ptr<Socket> socket = FindSocketWithInterfaceAddress (origPath->GetInterface ());
  NS_ASSERT (socket);
  socket->SendTo (packet, 0, InetSocketAddress (origPath->GetNextHop (), AOMDV_PORT));
}

void
RoutingProtocol::SendReplyByIntermediateNode (RoutingTableEntry & toDst, RoutingTableEntry & toOrigin,
                                              bool gratRep, Time expire, 
                                              Ipv4Address firstHop, uint32_t requestId)
{
  NS_LOG_FUNCTION (this);
  RoutingTableEntry::Path *dstPath = toDst.PathFind ();
  RoutingTableEntry::Path *origPath = toOrigin.PathFind ();
  if (!dstPath || !origPath)
    {
      return;
    }

  RrepHeader rrepHeader (/*prefix size=*/ 0, /*hops=*/ dstPath->GetHopCount (), /*dst=*/ toDst.GetDestination (), 
                         /*dst seqno=*/ toDst.GetSeqNo (), /*origin=*/ toOrigin.GetDestination (),
                         /*requestId=*/ requestId, /*firstHop=*/ firstHop, /*lifetime=*/ toDst.GetLifeTime ());

  if (dstPath->GetHopCount () == 1)
    {
      rrepHeader.SetAckRequired (true);
      RoutingTableEntry toNextHop;
      if (m_routingTable.LookupRoute (origPath->GetNextHop (), toNextHop))
        {
          m_routingTable.Update (toNextHop);
        }
    }
  toDst.InsertPrecursor (origPath->GetNextHop ());
  toOrigin.InsertPrecursor (dstPath->GetNextHop ());
  m_routingTable.Update (toDst);
  m_routingTable.Update (toOrigin);

  Ptr<Packet> packet = Create<Packet> ();
  packet->AddHeader (rrepHeader);
  TypeHeader tHeader (AOMDVTYPE_RREP);
  packet->AddHeader (tHeader);
  Ptr<Socket> socket = FindSocketWithInterfaceAddress (origPath->GetInterface ());
  NS_ASSERT (socket);
  socket->SendTo (packet, 0, InetSocketAddress (origPath->GetNextHop (), AOMDV_PORT));

  // Generating gratuitous RREPs
  if (gratRep)
    {
      RrepHeader gratRepHeader (/*prefix size=*/ 0, /*hops=*/ origPath->GetHopCount (), /*dst=*/ toOrigin.GetDestination (),
                                /*dst seqno=*/ toOrigin.GetSeqNo (), /*origin=*/ toDst.GetDestination (), /*requestId=*/
                                requestId, /*firstHop=*/ firstHop, /*lifetime=*/ toOrigin.GetLifeTime ());
      Ptr<Packet> packetToDst = Create<Packet> ();
      packetToDst->AddHeader (gratRepHeader);
      TypeHeader type (AOMDVTYPE_RREP);
      packetToDst->AddHeader (type);
      Ptr<Socket> dstSocket = FindSocketWithInterfaceAddress (dstPath->GetInterface ());
      NS_ASSERT (dstSocket);
      NS_LOG_LOGIC ("Send gratuitous RREP " << packet->GetUid ());
      dstSocket->SendTo (packetToDst, 0, InetSocketAddress (dstPath->GetNextHop (), AOMDV_PORT));
    }
}

void
RoutingProtocol::SendReplyAck (Ipv4Address neighbor)
{
  NS_LOG_FUNCTION (this << " to " << neighbor);
  RrepAckHeader h;
  TypeHeader typeHeader (AOMDVTYPE_RREP_ACK);
  Ptr<Packet> packet = Create<Packet> ();
  packet->AddHeader (h);
  packet->AddHeader (typeHeader);
  RoutingTableEntry toNeighbor;
  if (m_routingTable.LookupRoute (neighbor, toNeighbor))
    {
      RoutingTableEntry::Path *path = toNeighbor.PathFind ();
      if (path)
        {
          Ptr<Socket> socket = FindSocketWithInterfaceAddress (path->GetInterface ());
          NS_ASSERT (socket);
          socket->SendTo (packet, 0, InetSocketAddress (neighbor, AOMDV_PORT));
        }
    }
}

void
RoutingProtocol::RecvReply (Ptr<Packet> p, Ipv4Address receiver, Ipv4Address sender)
{
  NS_LOG_FUNCTION (this << " src " << sender);
  RrepHeader rrepHeader;
  p->RemoveHeader (rrepHeader);
  IdCache::UniqueId* b = nullptr;
  RoutingTableEntry::Path* forwardPath = nullptr;
  Ipv4Address dst = rrepHeader.GetDst ();
  NS_LOG_LOGIC ("RREP destination " << dst << " RREP origin " << rrepHeader.GetOrigin ());

  if (IsMyOwnAddress (dst))
    {
      return;
    }

  uint8_t hop = rrepHeader.GetHopCount () + 1;

  // If RREP is Hello message
  if (dst == rrepHeader.GetOrigin ())
    {
      ProcessHello (rrepHeader, receiver);
      return;
    }

  Ptr<NetDevice> dev = m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (receiver));
  RoutingTableEntry newEntry (/*dst=*/ dst, /*validSeqNo=*/ true, /*seqno=*/ rrepHeader.GetDstSeqno (), 
                              /*lifeTime=*/ rrepHeader.GetLifeTime ());
  RoutingTableEntry toDst;
  if (m_routingTable.LookupRoute (dst, toDst))
    {
      if ((!toDst.GetValidSeqNo ()) ||
          ((int32_t (rrepHeader.GetDstSeqno ()) - int32_t (toDst.GetSeqNo ())) > 0) ||
          ((rrepHeader.GetDstSeqno () == toDst.GetSeqNo ()) && (toDst.GetFlag () != VALID)))
        {
          // A valid RREP can repair an invalid route when its sequence number
          // is equal to the stored sequence number.
          toDst.SetSeqNo (rrepHeader.GetDstSeqno ());
          toDst.SetAdvertisedHopCount (INFINITY2);
          toDst.PathAllDelete ();
          toDst.SetFlag (VALID);
          // RREP lifetime is a duration; PathInsert adds Simulator::Now() internally.
          // The next hop is the neighbour that sent this RREP, not the RREQ origin.
          forwardPath = toDst.PathInsert (dev, sender, hop,
                                          rrepHeader.GetLifeTime (), rrepHeader.GetFirstHop (),
                                          m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0));
          toDst.SetLastHopCount (toDst.PathGetMaxHopCount ());
          toDst.SetLifeTime (std::max (toDst.GetLifeTime (), rrepHeader.GetLifeTime ()));
          m_routingTable.Update (toDst);
        }
      else if ((rrepHeader.GetDstSeqno () == toDst.GetSeqNo ()) && (toDst.GetFlag () == VALID)
               && (toDst.GetAdvertisedHopCount () > rrepHeader.GetHopCount ()))
        {
          if ((forwardPath = toDst.PathLookupDisjoint (sender, rrepHeader.GetFirstHop ())))
            {
              if (forwardPath->GetHopCount () == hop)
                {
                  forwardPath->SetExpire (std::max (forwardPath->GetExpire (), rrepHeader.GetLifeTime ()));
                  toDst.SetLifeTime (std::max (toDst.GetLifeTime (), rrepHeader.GetLifeTime ()));
                  m_routingTable.Update (toDst);
                }
            }
          else if ((toDst.PathNewDisjoint (sender, rrepHeader.GetFirstHop ()))
                   && (toDst.GetNumberofPaths () < AOMDV_MAX_PATHS) 
                   && (hop - toDst.PathGetMinHopCount () <= AOMDV_PRIM_ALT_PATH_LENGTH_DIFF))
            {
              forwardPath = toDst.PathInsert (dev, sender, hop,
                                              rrepHeader.GetLifeTime (), 
                                              rrepHeader.GetFirstHop (),
                                              m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0));
              toDst.SetLastHopCount (toDst.PathGetMaxHopCount ());
              toDst.SetLifeTime (std::max (toDst.GetLifeTime (), rrepHeader.GetLifeTime ()));
              m_routingTable.Update (toDst);
            }
          else
            {
              return;
            }
        }
      else          
        {
          return;
        }
    }
  else
    {
      // Keep the route path alive for the lifetime advertised in the RREP.
      forwardPath = newEntry.PathInsert (dev, sender, hop,
                                         rrepHeader.GetLifeTime (), 
                                         rrepHeader.GetFirstHop (), 
                                         m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0));
      m_routingTable.AddRoute (newEntry);
      toDst = newEntry;
    }

  if (rrepHeader.GetAckRequired ())
    {
      SendReplyAck (sender);
      rrepHeader.SetAckRequired (false);
    }
  NS_LOG_LOGIC ("receiver " << receiver << " origin " << rrepHeader.GetOrigin ());
  if (IsMyOwnAddress (rrepHeader.GetOrigin ()))
    {
      if (toDst.GetFlag () == IN_SEARCH)
        {
          toDst.SetFlag (VALID);
          m_routingTable.Update (toDst);
          m_addressReqTimer[dst].Remove ();
          m_addressReqTimer.erase (dst);
        }
      m_routingTable.LookupRoute (dst, toDst);
      RoutingTableEntry::Path *pRoute = toDst.PathFind ();
      if (pRoute)
        {
          SendPacketFromQueue (dst, pRoute->GetRoute ());
        }
      return;
    }

  RoutingTableEntry toOrigin;
  uint32_t id = rrepHeader.GetRequestID ();
  // The RREQ cache is keyed by the original requester, which is the RREP origin.
  // Looking up by dst returns nullptr at intermediate nodes and drops every RREP
  // when AOMDV_NODE_DISJOINT_PATHS is enabled.
  b = m_rreqIdCache.GetId (rrepHeader.GetOrigin (), id);
  #ifdef AOMDV_NODE_DISJOINT_PATHS
  if (!m_routingTable.LookupRoute (rrepHeader.GetOrigin (), toOrigin) || (toOrigin.GetFlag () != VALID)
      || (b == nullptr) || (b->count))
    {
      return;
    }

  b->count = 1;
  RoutingTableEntry::Path *reversePath = toOrigin.PathFind ();
  if (!reversePath)
    {
      return;
    }

  if (toDst.GetAdvertisedHopCount () == INFINITY2)
    {
      toDst.SetAdvertisedHopCount (toDst.PathGetMaxHopCount ());
    }
  rrepHeader.SetHopCount (toDst.GetAdvertisedHopCount ());
  RoutingTableEntry::Path *dstP = toDst.PathFind ();
  if (dstP)
    {
      rrepHeader.SetFirstHop (dstP->GetLastHop ());
    }
  reversePath->SetExpire (m_activeRouteTimeout);
  toDst.SetError (true);
  m_routingTable.Update (toDst);
  m_routingTable.Update (toOrigin);
  #endif // AOMDV_NODE_DISJOINT_PATHS

  #ifdef AOMDV_LINK_DISJOINT_PATHS
  if (!m_routingTable.LookupRoute (rrepHeader.GetOrigin (), toOrigin) || (toOrigin.GetFlag () != VALID)
      || (b == nullptr))
    {
      return;
    }
  RoutingTableEntry::Path* reversePath = nullptr;
  std::vector<RoutingTableEntry::Path> paths;
  toDst.GetPaths (paths);
  AOMDVRoute rt;
  for (auto i = paths.begin (); i != paths.end (); ++i)
    {
      if (!(b->ReversePathLookup (i->GetNextHop (), rt, i->GetLastHop ())))
        {
          reversePath = const_cast<RoutingTableEntry::Path*> (&(*i));
          break;
        }
    }

  if (reversePath && forwardPath && !(b->ForwardPathLookup (forwardPath->GetNextHop (), rt, forwardPath->GetLastHop ())))
    {
      if (forwardPath->GetNextHop () == rrepHeader.GetOrigin () && forwardPath->GetLastHop () == rrepHeader.GetFirstHop ())
        {
          b->ReversePathInsert (reversePath->GetNextHop (), reversePath->GetLastHop ());
          b->ForwardPathInsert (forwardPath->GetNextHop (), forwardPath->GetLastHop ());
          if (toDst.GetAdvertisedHopCount () == INFINITY2)
            {
              toDst.SetAdvertisedHopCount (toDst.PathGetMaxHopCount ());
            }
          rrepHeader.SetHopCount (toDst.GetAdvertisedHopCount ());
          reversePath->SetExpire (m_activeRouteTimeout);  
          toDst.SetError (true);
          m_routingTable.Update (toDst);
          m_routingTable.Update (toOrigin);
        }
    }
  else
    {
      return;
    }
  #endif // AOMDV_LINK_DISJOINT_PATHS

  Ptr<Packet> packet = Create<Packet> ();
  packet->AddHeader (rrepHeader);
  TypeHeader tHeader (AOMDVTYPE_RREP);
  packet->AddHeader (tHeader);
  RoutingTableEntry::Path *toOrigPath = toOrigin.PathFind ();
  if (!toOrigPath)
    {
      return;
    }
  Ptr<Socket> socket = FindSocketWithInterfaceAddress (toOrigPath->GetInterface ());
  NS_ASSERT (socket);
  socket->SendTo (packet, 0, InetSocketAddress (toOrigPath->GetNextHop (), AOMDV_PORT));
}

void
RoutingProtocol::RecvReplyAck (Ipv4Address neighbor)
{
  NS_LOG_FUNCTION (this);
  RoutingTableEntry rt;
  if (m_routingTable.LookupRoute (neighbor, rt))
    {

      rt.SetFlag (VALID);
      m_routingTable.Update (rt);
    }
}

void
RoutingProtocol::ProcessHello (RrepHeader const & rrepHeader, Ipv4Address receiver)
{
  NS_LOG_FUNCTION (this << "from " << rrepHeader.GetDst ());
  RoutingTableEntry toNeighbor;
  if (!m_routingTable.LookupRoute (rrepHeader.GetDst (), toNeighbor))
    {
      Ptr<NetDevice> dev = m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (receiver));
      RoutingTableEntry newEntry (/*dst=*/ rrepHeader.GetDst (), /*validSeqNo=*/ true, /*seqno=*/ rrepHeader.GetDstSeqno (), 
                                  /*lifeTime=*/ rrepHeader.GetLifeTime ());
      newEntry.PathInsert (/*device=*/ dev, /*nextHop=*/ rrepHeader.GetDst (), /*hop=*/ 1, 
                           /*expireTime=*/ rrepHeader.GetLifeTime (), 
                           /*lastHop=*/ rrepHeader.GetFirstHop (), 
                           /*iface=*/ m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0));
      m_routingTable.AddRoute (newEntry);
    }
  else
    {
      RoutingTableEntry::Path *neighborPath = toNeighbor.PathFind ();
      toNeighbor.SetLifeTime (std::max (Time (m_allowedHelloLoss * m_helloInterval), toNeighbor.GetLifeTime ()));
      toNeighbor.SetSeqNo (rrepHeader.GetDstSeqno ());
      toNeighbor.SetValidSeqNo (true);
      toNeighbor.SetFlag (VALID);
      if (neighborPath)
        {
          neighborPath->SetOutputDevice (m_ipv4->GetNetDevice (m_ipv4->GetInterfaceForAddress (receiver)));
          neighborPath->SetInterface (m_ipv4->GetAddress (m_ipv4->GetInterfaceForAddress (receiver), 0));
          neighborPath->SetHopCount (1);
          neighborPath->SetNextHop (rrepHeader.GetDst ());
          neighborPath->SetExpire (std::max (Time (m_allowedHelloLoss * m_helloInterval), toNeighbor.GetLifeTime ()));
        }
      m_routingTable.Update (toNeighbor);
    }
  if (m_enableHello)
    {
      m_nb.Update (rrepHeader.GetDst (), Time (m_allowedHelloLoss * m_helloInterval));
    }
}


void
RoutingProtocol::RecvError (Ptr<Packet> p, Ipv4Address src)
{
  NS_LOG_FUNCTION (this << " from " << src);
  RerrHeader receivedHeader;
  p->RemoveHeader (receivedHeader);

  std::map<Ipv4Address, uint32_t> routesViaSender;
  m_routingTable.GetListOfDestinationWithNextHop (src, routesViaSender);

  // Only process destinations named by this RERR and routed through its sender.
  std::map<Ipv4Address, uint32_t> affected;
  std::pair<Ipv4Address, uint32_t> item;
  while (receivedHeader.RemoveUnDestination (item))
    {
      if (routesViaSender.find (item.first) != routesViaSender.end ())
        {
          affected[item.first] = item.second;
        }
    }

  std::map<Ipv4Address, uint32_t> unreachable;
  std::vector<Ipv4Address> precursors;

  for (const auto &failure : affected)
    {
      RoutingTableEntry toDst;
      if (!m_routingTable.LookupRoute (failure.first, toDst))
        {
          continue;
        }

      toDst.PathPurge ();
      // A newer destination sequence number means all cached paths in this entry
      // are stale; otherwise only the path through the reporting neighbour failed.
      const bool newerSequence =
          !toDst.GetValidSeqNo () ||
          (static_cast<int32_t> (failure.second) -
           static_cast<int32_t> (toDst.GetSeqNo ())) > 0;

      if (newerSequence)
        {
          toDst.SetSeqNo (failure.second);
          toDst.SetValidSeqNo (true);
          toDst.PathAllDelete ();
        }
      else
        {
          toDst.PathDelete (src);
        }

      if (toDst.PathEmpty ())
        {
          toDst.SetFlag (INVALID);
          toDst.SetLifeTime (m_routingTable.GetBadLinkLifetime ());
          unreachable[failure.first] = toDst.GetSeqNo ();
          toDst.GetPrecursors (precursors);
        }
      else
        {
          // Another valid path remains; do not propagate this destination as
          // unreachable to upstream nodes.
          toDst.SetFlag (VALID);
          Time remaining = toDst.PathGetMaxExpirationTime () - Simulator::Now ();
          toDst.SetLifeTime (std::max (Seconds (0), remaining));
        }

      m_routingTable.Update (toDst);
    }

  if (unreachable.empty ())
    {
      return;
    }

  RerrHeader outgoingHeader;
  for (auto i = unreachable.begin (); i != unreachable.end ();)
    {
      if (!outgoingHeader.AddUnDestination (i->first, i->second))
        {
          TypeHeader typeHeader (AOMDVTYPE_RERR);
          Ptr<Packet> packet = Create<Packet> ();
          packet->AddHeader (outgoingHeader);
          packet->AddHeader (typeHeader);
          SendRerrMessage (packet, precursors);
          outgoingHeader.Clear ();
        }
      else
        {
          ++i;
        }
    }

  if (outgoingHeader.GetDestCount () != 0)
    {
      TypeHeader typeHeader (AOMDVTYPE_RERR);
      Ptr<Packet> packet = Create<Packet> ();
      packet->AddHeader (outgoingHeader);
      packet->AddHeader (typeHeader);
      SendRerrMessage (packet, precursors);
    }
}

void
RoutingProtocol::RouteRequestTimerExpire (Ipv4Address dst)
{
  NS_LOG_LOGIC (this);
  RoutingTableEntry toDst;
  if (m_routingTable.LookupValidRoute (dst, toDst))
    {
      RoutingTableEntry::Path *path = toDst.PathFind ();
      if (path)
        {
          SendPacketFromQueue (dst, path->GetRoute ());
          NS_LOG_LOGIC ("route to " << dst << " found");
          return;
        }
    }
  if (toDst.GetRreqCnt () == m_rreqRetries)
    {
      NS_LOG_LOGIC ("route discovery to " << dst << " has been attempted RreqRetries (" << m_rreqRetries << ") times");
      m_addressReqTimer.erase (dst);
      m_routingTable.DeleteRoute (dst);
      NS_LOG_DEBUG ("Route not found. Drop all packets with dst " << dst);
      m_queue.DropPacketWithDst (dst);
      return;
    }

  if (toDst.GetFlag () == IN_SEARCH)
    {
      NS_LOG_LOGIC ("Resend RREQ to " << dst << " ttl " << m_netDiameter);
      SendRequest (dst);
    }
  else
    {
      NS_LOG_DEBUG ("Route down. Stop search. Drop packet with destination " << dst);
      m_addressReqTimer.erase (dst);
      m_routingTable.DeleteRoute (dst);
      m_queue.DropPacketWithDst (dst);
    }
}

void
RoutingProtocol::HelloTimerExpire ()
{
  NS_LOG_FUNCTION (this);
  Time offset = Seconds (0);
  if (m_lastBcastTime > Seconds (0))
    {
      offset = Simulator::Now () - m_lastBcastTime;
      NS_LOG_DEBUG ("Hello deferred due to last bcast at:" << m_lastBcastTime);
    }
  else
    {
      SendHello ();
    }
  m_htimer.Cancel ();
  Time diff = m_helloInterval - offset;
  m_htimer.Schedule (std::max (Seconds (0), diff));
  m_lastBcastTime = Seconds (0);
}

void
RoutingProtocol::RreqRateLimitTimerExpire ()
{
  NS_LOG_FUNCTION (this);
  m_rreqCount = 0;
  m_rreqRateLimitTimer.Schedule (Seconds (1));
}

void
RoutingProtocol::RerrRateLimitTimerExpire ()
{
  NS_LOG_FUNCTION (this);
  m_rerrCount = 0;
  m_rerrRateLimitTimer.Schedule (Seconds (1));
}

void
RoutingProtocol::AckTimerExpire (Ipv4Address neighbor, Time blacklistTimeout)
{
  NS_LOG_FUNCTION (this);
  m_routingTable.MarkLinkAsUnidirectional (neighbor, blacklistTimeout);
}

void
RoutingProtocol::SendHello ()
{
  NS_LOG_FUNCTION (this);
  for (auto j = m_socketAddresses.begin (); j != m_socketAddresses.end (); ++j)
    {
      Ptr<Socket> socket = j->first;
      Ipv4InterfaceAddress iface = j->second;
      RrepHeader helloHeader (/*prefix size=*/ 0, /*hops=*/ 1, /*dst=*/ iface.GetLocal (), /*dst seqno=*/ m_seqNo,
                              /*origin=*/ iface.GetLocal (), /*requestId=*/ 0, /*firstHop=*/ Ipv4Address (),
                              /*lifetime=*/ Time (m_allowedHelloLoss * m_helloInterval));
      Ptr<Packet> packet = Create<Packet> ();
      packet->AddHeader (helloHeader);
      TypeHeader tHeader (AOMDVTYPE_RREP);
      packet->AddHeader (tHeader);

      Ipv4Address destination;
      if (iface.GetMask () == Ipv4Mask::GetOnes ())
        {
          destination = Ipv4Address ("255.255.255.255");
        }
      else
        { 
          destination = iface.GetBroadcast ();
        }
      Time jitter = MilliSeconds (m_uniformRandomVariable->GetInteger (0, 10));
      Simulator::Schedule (jitter, &RoutingProtocol::SendTo, this, socket, packet, destination);
    }
}

void
RoutingProtocol::SendPacketFromQueue (Ipv4Address dst, Ptr<Ipv4Route> route)
{
  NS_LOG_FUNCTION (this);
  QueueEntry queueEntry;
  while (m_queue.Dequeue (dst, queueEntry))
    {
      DeferredRouteOutputTag tag;
      Ptr<Packet> p = ConstCast<Packet> (queueEntry.GetPacket ());
      if (p->RemovePacketTag (tag) && 
          tag.GetInterface () != -1 &&
          tag.GetInterface () != m_ipv4->GetInterfaceForDevice (route->GetOutputDevice ()))
        {
          NS_LOG_DEBUG ("Output device doesn't match. Dropped.");
          return;
        }
      UnicastForwardCallback ucb = queueEntry.GetUnicastForwardCallback ();
      Ipv4Header header = queueEntry.GetIpv4Header ();
      header.SetSource (route->GetSource ());
      header.SetTtl (header.GetTtl () + 1); // compensate extra TTL decrement by fake loopback routing
      ucb (route, p, header);
    }
}


void
RoutingProtocol::SendRerrWhenBreaksLinkToNextHop (Ipv4Address nextHop)
{
  NS_LOG_FUNCTION (this << nextHop);
  RerrHeader rerrHeader;
  std::vector<Ipv4Address> precursors;
  std::map<Ipv4Address, uint32_t> affected;
  std::map<Ipv4Address, uint32_t> unreachable;

  RoutingTableEntry toNextHop;
  if (!m_routingTable.LookupRoute (nextHop, toNextHop))
    {
      return;
    }

  // Keep the neighbour's precursors because the neighbour itself is unreachable.
  toNextHop.GetPrecursors (precursors);
  m_routingTable.GetListOfDestinationWithNextHop (nextHop, affected);

  // AOMDV must remove only the failed next-hop path. Preserve a destination route
  // while at least one non-expired alternate path remains.
  for (const auto &item : affected)
    {
      RoutingTableEntry toDst;
      if (!m_routingTable.LookupRoute (item.first, toDst))
        {
          continue;
        }

      toDst.PathPurge ();
      toDst.PathDelete (nextHop);

      if (toDst.PathEmpty ())
        {
          toDst.SetFlag (INVALID);
          toDst.SetLifeTime (m_routingTable.GetBadLinkLifetime ());
          unreachable[item.first] = toDst.GetSeqNo ();
          toDst.GetPrecursors (precursors);
        }
      else
        {
          toDst.SetFlag (VALID);
          Time remaining = toDst.PathGetMaxExpirationTime () - Simulator::Now ();
          toDst.SetLifeTime (std::max (Seconds (0), remaining));
        }
      m_routingTable.Update (toDst);
    }

  // Report the broken neighbour as unreachable as well.
  unreachable[nextHop] = toNextHop.GetSeqNo ();

  for (auto i = unreachable.begin (); i != unreachable.end ();)
    {
      if (!rerrHeader.AddUnDestination (i->first, i->second))
        {
          NS_LOG_LOGIC ("Send RERR message with maximum size.");
          TypeHeader typeHeader (AOMDVTYPE_RERR);
          Ptr<Packet> packet = Create<Packet> ();
          packet->AddHeader (rerrHeader);
          packet->AddHeader (typeHeader);
          SendRerrMessage (packet, precursors);
          rerrHeader.Clear ();
        }
      else
        {
          ++i;
        }
    }

  if (rerrHeader.GetDestCount () != 0)
    {
      TypeHeader typeHeader (AOMDVTYPE_RERR);
      Ptr<Packet> packet = Create<Packet> ();
      packet->AddHeader (rerrHeader);
      packet->AddHeader (typeHeader);
      SendRerrMessage (packet, precursors);
    }
}

void
RoutingProtocol::SendRerrWhenNoRouteToForward (Ipv4Address dst,
                                               uint32_t dstSeqNo, Ipv4Address origin)
{
  NS_LOG_FUNCTION (this);
  if (m_rerrCount == m_rerrRateLimit)
    {
      NS_ASSERT (m_rerrRateLimitTimer.IsRunning ());
      NS_LOG_LOGIC ("RerrRateLimit reached at " << Simulator::Now ().GetSeconds () << " with timer delay left " 
                                                << m_rerrRateLimitTimer.GetDelayLeft ().GetSeconds ()
                                                << "; suppressing RERR");
      return;
    }
  RerrHeader rerrHeader;
  rerrHeader.AddUnDestination (dst, dstSeqNo);
  RoutingTableEntry toOrigin;
  Ptr<Packet> packet = Create<Packet> ();
  packet->AddHeader (rerrHeader);
  packet->AddHeader (TypeHeader (AOMDVTYPE_RERR));
  if (m_routingTable.LookupValidRoute (origin, toOrigin))
    {
      RoutingTableEntry::Path *path = toOrigin.PathFind ();
      if (path)
        {
          Ptr<Socket> socket = FindSocketWithInterfaceAddress (path->GetInterface ());
          NS_ASSERT (socket);
          NS_LOG_LOGIC ("Unicast RERR to the source of the data transmission");
          socket->SendTo (packet, 0, InetSocketAddress (path->GetNextHop (), AOMDV_PORT));
        }
    }
  else
    {
      for (auto i = m_socketAddresses.begin (); i != m_socketAddresses.end (); ++i)
        {
          Ptr<Socket> socket = i->first;
          Ipv4InterfaceAddress iface = i->second;
          NS_ASSERT (socket);
          NS_LOG_LOGIC ("Broadcast RERR message from interface " << iface.GetLocal ());
          Ipv4Address destination;
          if (iface.GetMask () == Ipv4Mask::GetOnes ())
            {
              destination = Ipv4Address ("255.255.255.255");
            }
          else
            { 
              destination = iface.GetBroadcast ();
            }
          socket->SendTo (packet->Copy (), 0, InetSocketAddress (destination, AOMDV_PORT));
        }
    }
}

void
RoutingProtocol::SendRerrMessage (Ptr<Packet> packet, std::vector<Ipv4Address> precursors)
{
  NS_LOG_FUNCTION (this);

  if (precursors.empty ())
    {
      NS_LOG_LOGIC ("No precursors");
      return;
    }
  if (m_rerrCount == m_rerrRateLimit)
    {
      NS_ASSERT (m_rerrRateLimitTimer.IsRunning ());
      NS_LOG_LOGIC ("RerrRateLimit reached at " << Simulator::Now ().GetSeconds () << " with timer delay left " 
                                                << m_rerrRateLimitTimer.GetDelayLeft ().GetSeconds ()
                                                << "; suppressing RERR");
      return;
    }
  if (precursors.size () == 1)
    {
      RoutingTableEntry toPrecursor;
      if (m_routingTable.LookupValidRoute (precursors.front (), toPrecursor))
        {
          RoutingTableEntry::Path *path = toPrecursor.PathFind ();
          if (path)
            {
              Ptr<Socket> socket = FindSocketWithInterfaceAddress (path->GetInterface ());
              NS_ASSERT (socket);
              NS_LOG_LOGIC ("one precursor => unicast RERR to " << toPrecursor.GetDestination () << " from " << 
                             path->GetInterface ().GetLocal ());
              Simulator::Schedule (MilliSeconds (m_uniformRandomVariable->GetInteger (0, 10)),
                                   &RoutingProtocol::SendTo, this, socket, packet, precursors.front ());
              m_rerrCount++;
            }
        }
      return;
    }

  std::vector<Ipv4InterfaceAddress> ifaces;
  RoutingTableEntry toPrecursor;
  for (auto i = precursors.begin (); i != precursors.end (); ++i)
    {
      if (m_routingTable.LookupValidRoute (*i, toPrecursor))
        {
          RoutingTableEntry::Path *path = toPrecursor.PathFind ();
          if (path && std::find (ifaces.begin (), ifaces.end (), path->GetInterface ()) == ifaces.end ())
            {
              ifaces.push_back (path->GetInterface ());
            }
        }
    }

  for (auto i = ifaces.begin (); i != ifaces.end (); ++i)
    {
      Ptr<Socket> socket = FindSocketWithInterfaceAddress (*i);
      NS_ASSERT (socket);
      NS_LOG_LOGIC ("Broadcast RERR message from interface " << i->GetLocal ());
      Ptr<Packet> p = packet->Copy ();
      Ipv4Address destination;
      if (i->GetMask () == Ipv4Mask::GetOnes ())
        {
          destination = Ipv4Address ("255.255.255.255");
        }
      else
        { 
          destination = i->GetBroadcast ();
        }
      Simulator::Schedule (MilliSeconds (m_uniformRandomVariable->GetInteger (0, 10)),
                           &RoutingProtocol::SendTo, this, socket, p, destination);
    }
}

Ptr<Socket>
RoutingProtocol::FindSocketWithInterfaceAddress (Ipv4InterfaceAddress addr) const
{
  NS_LOG_FUNCTION (this << addr);
  for (auto j = m_socketAddresses.begin (); j != m_socketAddresses.end (); ++j)
    {
      Ptr<Socket> socket = j->first;
      Ipv4InterfaceAddress iface = j->second;
      if (iface == addr)
        {
          return socket;
        }
    }
  return Ptr<Socket> ();
}

Ptr<Socket>
RoutingProtocol::FindSubnetBroadcastSocketWithInterfaceAddress (Ipv4InterfaceAddress addr) const
{
  NS_LOG_FUNCTION (this << addr);
  for (auto j = m_socketSubnetBroadcastAddresses.begin (); j != m_socketSubnetBroadcastAddresses.end (); ++j)
    {
      Ptr<Socket> socket = j->first;
      Ipv4InterfaceAddress iface = j->second;
      if (iface == addr)
        {
          return socket;
        }
    }
  return Ptr<Socket> ();
}

void
RoutingProtocol::DoInitialize (void)
{
  NS_LOG_FUNCTION (this);
  uint32_t startTime;
  if (m_enableHello)
    {
      m_htimer.SetFunction (&RoutingProtocol::HelloTimerExpire, this);
      startTime = m_uniformRandomVariable->GetInteger (0, 100);
      NS_LOG_DEBUG ("Starting at time " << startTime << "ms");
      m_htimer.Schedule (MilliSeconds (startTime));
    }
  Ipv4RoutingProtocol::DoInitialize ();
}

} // namespace aomdv
} // namespace ns3
