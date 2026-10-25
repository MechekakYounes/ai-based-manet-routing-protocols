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
 * Authors: Elena Buchatskaia <borovkovaes@iitp.ru>
 *          Pavel Boyko <boyko@iitp.ru>
 */

#include "aomdv-helper.h"
#include "ns3/aomdv-routing-protocol.h"
#include "ns3/node-list.h"
#include "ns3/names.h"
#include "ns3/ptr.h"
#include "ns3/ipv4-list-routing.h"

namespace ns3
{

AomdvHelper::AomdvHelper ()
  : Ipv4RoutingHelper ()
{
  m_agentFactory.SetTypeId ("ns3::aomdv::RoutingProtocol");
}

AomdvHelper*
AomdvHelper::Copy () const
{
  return new AomdvHelper (*this);
}

Ptr<Ipv4RoutingProtocol>
AomdvHelper::Create (Ptr<Node> node) const
{
  Ptr<aomdv::RoutingProtocol> agent = m_agentFactory.Create<aomdv::RoutingProtocol> ();
  node->AggregateObject (agent);
  return agent;
}

void
AomdvHelper::Set (std::string name, const AttributeValue &value)
{
  m_agentFactory.Set (name, value);
}

int64_t
AomdvHelper::AssignStreams (NodeContainer c, int64_t stream)
{
  int64_t currentStream = stream;
  Ptr<Node> node;
  for (auto i = c.Begin (); i != c.End (); ++i)
    {
      node = (*i);
      Ptr<Ipv4> ipv4 = node->GetObject<Ipv4> ();
      NS_ASSERT_MSG (ipv4, "Ipv4 not installed on node");
      Ptr<Ipv4RoutingProtocol> proto = ipv4->GetRoutingProtocol ();
      NS_ASSERT_MSG (proto, "Ipv4 routing not installed on node");
      Ptr<aomdv::RoutingProtocol> aomdv = DynamicCast<aomdv::RoutingProtocol> (proto);
      if (aomdv)
        {
          currentStream += aomdv->AssignStreams (currentStream);
          continue;
        }
      // Aomdv may also be in a list
      Ptr<Ipv4ListRouting> list = DynamicCast<Ipv4ListRouting> (proto);
      if (list)
        {
          int16_t priority;
          Ptr<Ipv4RoutingProtocol> listProto;
          Ptr<aomdv::RoutingProtocol> listAomdv;
          for (uint32_t j = 0; j < list->GetNRoutingProtocols (); j++)
            {
              listProto = list->GetRoutingProtocol (j, priority);
              listAomdv = DynamicCast<aomdv::RoutingProtocol> (listProto);
              if (listAomdv)
                {
                  currentStream += listAomdv->AssignStreams (currentStream);
                  break;
                }
            }
        }
    }
  return (currentStream - stream);
}

} // namespace ns3

