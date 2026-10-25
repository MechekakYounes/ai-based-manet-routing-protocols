#include "aomdv-packet.h"

#include "ns3/address-utils.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/type-id.h"

namespace ns3
{
namespace aomdv
{

NS_LOG_COMPONENT_DEFINE("AomdvPacket");
NS_OBJECT_ENSURE_REGISTERED(TypeHeader);
NS_OBJECT_ENSURE_REGISTERED(RreqHeader);
NS_OBJECT_ENSURE_REGISTERED(RrepHeader);
NS_OBJECT_ENSURE_REGISTERED(RerrHeader);

TypeHeader::TypeHeader(MessageType t)
    : m_type(t),
      m_valid(true)
{
}

TypeId
TypeHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::aomdv::TypeHeader")
                            .SetParent<Header>()
                            .SetGroupName("Aomdv")
                            .AddConstructor<TypeHeader>();
    return tid;
}

TypeId
TypeHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
TypeHeader::GetSerializedSize() const
{
    return 1;
}

void
TypeHeader::Serialize(Buffer::Iterator i) const
{
    i.WriteU8(static_cast<uint8_t>(m_type));
}

uint32_t
TypeHeader::Deserialize(Buffer::Iterator start)
{
    auto i = start;
    uint8_t type = i.ReadU8();
    m_valid = true;

    switch (type)
    {
    case AOMDVTYPE_RREQ:
    case AOMDVTYPE_RREP:
    case AOMDVTYPE_RERR:
    case AOMDVTYPE_HELLO:
        m_type = static_cast<MessageType>(type);
        break;
    default:
        m_valid = false;
        break;
    }

    return i.GetDistanceFrom(start);
}

void
TypeHeader::Print(std::ostream& os) const
{
    switch (m_type)
    {
    case AOMDVTYPE_RREQ:
        os << "RREQ";
        break;
    case AOMDVTYPE_RREP:
        os << "RREP";
        break;
    case AOMDVTYPE_RERR:
        os << "RERR";
        break;
    case AOMDVTYPE_HELLO:
        os << "HELLO";
        break;
    default:
        os << "UNKNOWN";
        break;
    }
}

MessageType
TypeHeader::Get() const
{
    return m_type;
}

bool
TypeHeader::IsValid() const
{
    return m_valid;
}

bool
TypeHeader::operator==(const TypeHeader& o) const
{
    return m_type == o.m_type && m_valid == o.m_valid;
}

std::ostream&
operator<<(std::ostream& os, const TypeHeader& h)
{
    h.Print(os);
    return os;
}

RreqHeader::RreqHeader()
    : m_requestID(0),
      m_dst(),
      m_dstSeqNo(0),
      m_origin(),
      m_originSeqNo(0),
      m_hopCount(0)
{
}

TypeId
RreqHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::aomdv::RreqHeader")
                            .SetParent<Header>()
                            .SetGroupName("Aomdv")
                            .AddConstructor<RreqHeader>();
    return tid;
}

TypeId
RreqHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

void
RreqHeader::Print(std::ostream& os) const
{
    os << "RREQ id=" << m_requestID << " dst=" << m_dst << " dstSeq=" << m_dstSeqNo
       << " origin=" << m_origin << " originSeq=" << m_originSeqNo << " hops=" << m_hopCount;
}

uint32_t
RreqHeader::GetSerializedSize() const
{
    return 24;
}

void
RreqHeader::Serialize(Buffer::Iterator i) const
{
    i.WriteU8(0); // reserved/flags byte in this compact format
    i.WriteU8(0);
    i.WriteHtonU16(static_cast<uint16_t>(m_hopCount));
    i.WriteHtonU32(m_requestID);
    WriteTo(i, m_dst);
    i.WriteHtonU32(m_dstSeqNo);
    WriteTo(i, m_origin);
    i.WriteHtonU32(m_originSeqNo);
}

uint32_t
RreqHeader::Deserialize(Buffer::Iterator start)
{
    auto i = start;
    i.ReadU8();
    i.ReadU8();
    m_hopCount = i.ReadNtohU16();
    m_requestID = i.ReadNtohU32();
    ReadFrom(i, m_dst);
    m_dstSeqNo = i.ReadNtohU32();
    ReadFrom(i, m_origin);
    m_originSeqNo = i.ReadNtohU32();
    return i.GetDistanceFrom(start);
}

void
RreqHeader::SetDst(Ipv4Address a)
{
    m_dst = a;
}

void
RreqHeader::SetOrigin(Ipv4Address a)
{
    m_origin = a;
}

void
RreqHeader::SetId(uint32_t id)
{
    m_requestID = id;
}

void
RreqHeader::SetDstSeqNo(uint32_t s)
{
    m_dstSeqNo = s;
}

void
RreqHeader::SetOriginSeqNo(uint32_t s)
{
    m_originSeqNo = s;
}

void
RreqHeader::SetHopCount(uint32_t h)
{
    m_hopCount = h;
}

void
RreqHeader::IncrementHopCount()
{
    m_hopCount++;
}

Ipv4Address
RreqHeader::GetDst() const
{
    return m_dst;
}

Ipv4Address
RreqHeader::GetOrigin() const
{
    return m_origin;
}

uint32_t
RreqHeader::GetId() const
{
    return m_requestID;
}

uint32_t
RreqHeader::GetDstSeqNo() const
{
    return m_dstSeqNo;
}

uint32_t
RreqHeader::GetOriginSeqNo() const
{
    return m_originSeqNo;
}

uint32_t
RreqHeader::GetHopCount() const
{
    return m_hopCount;
}

RrepHeader::RrepHeader()
    : m_dst(),
      m_dstSeqNo(0),
      m_origin(),
      m_hopCount(0),
      m_lifeTime(Seconds(0)),
      m_pathList()
{
}

TypeId
RrepHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::aomdv::RrepHeader")
                            .SetParent<Header>()
                            .SetGroupName("Aomdv")
                            .AddConstructor<RrepHeader>();
    return tid;
}

TypeId
RrepHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

void
RrepHeader::Print(std::ostream& os) const
{
    os << "RREP dst=" << m_dst << " dstSeq=" << m_dstSeqNo << " origin=" << m_origin
       << " hops=" << m_hopCount << " lifetime=" << m_lifeTime.GetMilliSeconds()
       << "ms pathNodes=" << m_pathList.size();
}

uint32_t
RrepHeader::GetSerializedSize() const
{
    return 20 + static_cast<uint32_t>(m_pathList.size()) * 4;
}

void
RrepHeader::Serialize(Buffer::Iterator i) const
{
    i.WriteU8(static_cast<uint8_t>(m_pathList.size()));
    i.WriteU8(0);
    i.WriteHtonU16(static_cast<uint16_t>(m_hopCount));
    WriteTo(i, m_dst);
    i.WriteHtonU32(m_dstSeqNo);
    WriteTo(i, m_origin);
    i.WriteHtonU32(static_cast<uint32_t>(m_lifeTime.GetMilliSeconds()));
    for (const auto& addr : m_pathList)
    {
        WriteTo(i, addr);
    }
}

uint32_t
RrepHeader::Deserialize(Buffer::Iterator start)
{
    auto i = start;
    uint8_t pathCount = i.ReadU8();
    i.ReadU8();
    m_hopCount = i.ReadNtohU16();
    ReadFrom(i, m_dst);
    m_dstSeqNo = i.ReadNtohU32();
    ReadFrom(i, m_origin);
    m_lifeTime = MilliSeconds(i.ReadNtohU32());

    m_pathList.clear();
    m_pathList.reserve(pathCount);
    for (uint8_t k = 0; k < pathCount; ++k)
    {
        Ipv4Address addr;
        ReadFrom(i, addr);
        m_pathList.push_back(addr);
    }

    return i.GetDistanceFrom(start);
}

void
RrepHeader::SetDst(Ipv4Address a)
{
    m_dst = a;
}

void
RrepHeader::SetOrigin(Ipv4Address a)
{
    m_origin = a;
}

void
RrepHeader::SetDstSeqNo(uint32_t s)
{
    m_dstSeqNo = s;
}

void
RrepHeader::SetHopCount(uint32_t h)
{
    m_hopCount = h;
}

void
RrepHeader::IncrementHopCount()
{
    m_hopCount++;
}

void
RrepHeader::SetLifeTime(Time t)
{
    m_lifeTime = t;
}

void
RrepHeader::AddPathNode(Ipv4Address a)
{
    m_pathList.push_back(a);
}

Ipv4Address
RrepHeader::GetDst() const
{
    return m_dst;
}

Ipv4Address
RrepHeader::GetOrigin() const
{
    return m_origin;
}

uint32_t
RrepHeader::GetDstSeqNo() const
{
    return m_dstSeqNo;
}

uint32_t
RrepHeader::GetHopCount() const
{
    return m_hopCount;
}

Time
RrepHeader::GetLifeTime() const
{
    return m_lifeTime;
}

std::vector<Ipv4Address>
RrepHeader::GetPathList() const
{
    return m_pathList;
}

RerrHeader::RerrHeader()
    : m_unreachable()
{
}

TypeId
RerrHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::aomdv::RerrHeader")
                            .SetParent<Header>()
                            .SetGroupName("Aomdv")
                            .AddConstructor<RerrHeader>();
    return tid;
}

TypeId
RerrHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

void
RerrHeader::Print(std::ostream& os) const
{
    os << "RERR unreachable=" << m_unreachable.size();
}

uint32_t
RerrHeader::GetSerializedSize() const
{
    return 2 + static_cast<uint32_t>(m_unreachable.size()) * 8;
}

void
RerrHeader::Serialize(Buffer::Iterator i) const
{
    i.WriteU8(static_cast<uint8_t>(m_unreachable.size()));
    i.WriteU8(0);
    for (const auto& [addr, seq] : m_unreachable)
    {
        WriteTo(i, addr);
        i.WriteHtonU32(seq);
    }
}

uint32_t
RerrHeader::Deserialize(Buffer::Iterator start)
{
    auto i = start;
    uint8_t count = i.ReadU8();
    i.ReadU8();
    m_unreachable.clear();
    m_unreachable.reserve(count);

    for (uint8_t k = 0; k < count; ++k)
    {
        Ipv4Address addr;
        ReadFrom(i, addr);
        uint32_t seq = i.ReadNtohU32();
        m_unreachable.emplace_back(addr, seq);
    }

    return i.GetDistanceFrom(start);
}

void
RerrHeader::AddUnreachableNode(Ipv4Address dst, uint32_t seqNo)
{
    m_unreachable.emplace_back(dst, seqNo);
}

bool
RerrHeader::RemoveUnreachableNode(Ipv4Address dst, uint32_t& seqNo)
{
    for (auto it = m_unreachable.begin(); it != m_unreachable.end(); ++it)
    {
        if (it->first == dst)
        {
            seqNo = it->second;
            m_unreachable.erase(it);
            return true;
        }
    }
    return false;
}

bool
RerrHeader::IsEmpty() const
{
    return m_unreachable.empty();
}

uint32_t
RerrHeader::GetDestCount() const
{
    return static_cast<uint32_t>(m_unreachable.size());
}

const std::vector<std::pair<Ipv4Address, uint32_t>>&
RerrHeader::GetUnreachableNodes() const
{
    return m_unreachable;
}

} // namespace aomdv
} // namespace ns3
