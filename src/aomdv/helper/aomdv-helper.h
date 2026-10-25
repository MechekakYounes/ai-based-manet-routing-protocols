#pragma once

#include "ns3/ipv4-routing-helper.h"
#include "ns3/node-container.h"
#include "ns3/object-factory.h"
#include "ns3/output-stream-wrapper.h"

namespace ns3
{

class AomdvHelper : public Ipv4RoutingHelper
{
  public:
    AomdvHelper();
    AomdvHelper* Copy() const override;
    Ptr<Ipv4RoutingProtocol> Create(Ptr<Node> node) const override;

    void Set(std::string name, const AttributeValue& value);
    int64_t AssignStreams(NodeContainer c, int64_t stream);
    void PrintRoutingTableAllAt(Time printTime, Ptr<OutputStreamWrapper> stream) const;

  private:
    ObjectFactory m_agentFactory;
};

} // namespace ns3
