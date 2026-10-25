#include "aomdv-helper.h"

#include "ns3/aomdv-routing-protocol.h"
#include "ns3/ipv4-list-routing.h"
#include "ns3/ipv4.h"
#include "ns3/node-list.h"
#include "ns3/simulator.h"

namespace ns3
{

AomdvHelper::AomdvHelper()
    : Ipv4RoutingHelper()
{
    m_agentFactory.SetTypeId("ns3::aomdv::RoutingProtocol");
}

AomdvHelper*
AomdvHelper::Copy() const
{
    return new AomdvHelper(*this);
}

Ptr<Ipv4RoutingProtocol>
AomdvHelper::Create(Ptr<Node> node) const
{
    Ptr<aomdv::RoutingProtocol> agent = m_agentFactory.Create<aomdv::RoutingProtocol>();
    node->AggregateObject(agent);
    return agent;
}

void
AomdvHelper::Set(std::string name, const AttributeValue& value)
{
    m_agentFactory.Set(name, value);
}

int64_t
AomdvHelper::AssignStreams(NodeContainer c, int64_t stream)
{
    int64_t currentStream = stream;
    for (auto i = c.Begin(); i != c.End(); ++i)
    {
        Ptr<Node> node = *i;
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        NS_ASSERT_MSG(ipv4, "Ipv4 not installed on node");

        Ptr<Ipv4RoutingProtocol> proto = ipv4->GetRoutingProtocol();
        NS_ASSERT_MSG(proto, "Ipv4 routing not installed on node");

        Ptr<aomdv::RoutingProtocol> aomdv = DynamicCast<aomdv::RoutingProtocol>(proto);
        if (aomdv)
        {
            currentStream += aomdv->AssignStreams(currentStream);
            continue;
        }

        Ptr<Ipv4ListRouting> list = DynamicCast<Ipv4ListRouting>(proto);
        if (list)
        {
            int16_t priority;
            for (uint32_t idx = 0; idx < list->GetNRoutingProtocols(); ++idx)
            {
                Ptr<Ipv4RoutingProtocol> listProto = list->GetRoutingProtocol(idx, priority);
                Ptr<aomdv::RoutingProtocol> listAomdv = DynamicCast<aomdv::RoutingProtocol>(listProto);
                if (listAomdv)
                {
                    currentStream += listAomdv->AssignStreams(currentStream);
                    break;
                }
            }
        }
    }
    return currentStream - stream;
}

static void
PrintRouteAt(Ptr<OutputStreamWrapper> stream, Ptr<Node> node)
{
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol();
    if (rp)
    {
        rp->PrintRoutingTable(stream, Time::S);
    }
}

void
AomdvHelper::PrintRoutingTableAllAt(Time printTime, Ptr<OutputStreamWrapper> stream) const
{
    for (auto it = NodeList::Begin(); it != NodeList::End(); ++it)
    {
        Simulator::Schedule(printTime, &PrintRouteAt, stream, *it);
    }
}

} // namespace ns3
