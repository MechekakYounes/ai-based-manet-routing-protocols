#pragma once

#include "ns3/enum.h"
#include "ns3/header.h"
#include "ns3/ipv4-address.h"
#include "ns3/nstime.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace ns3
{
namespace aomdv
{

enum MessageType : uint8_t
{
    AOMDVTYPE_RREQ = 1,
    AOMDVTYPE_RREP = 2,
    AOMDVTYPE_RERR = 3,
    AOMDVTYPE_HELLO = 4,
};

class TypeHeader : public Header
{
  public:
    TypeHeader(MessageType t = AOMDVTYPE_RREQ);

    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;
    void Print(std::ostream& os) const override;

    MessageType Get() const;
    bool IsValid() const;

    bool operator==(const TypeHeader& o) const;

  private:
    MessageType m_type;
    bool m_valid;
};

std::ostream& operator<<(std::ostream& os, const TypeHeader& h);

class RreqHeader : public Header
{
  public:
    RreqHeader();

    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    void Print(std::ostream& os) const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;

    void SetDst(Ipv4Address a);
    void SetOrigin(Ipv4Address a);
    void SetId(uint32_t id);
    void SetDstSeqNo(uint32_t s);
    void SetOriginSeqNo(uint32_t s);
    void SetHopCount(uint32_t h);
    void IncrementHopCount();

    Ipv4Address GetDst() const;
    Ipv4Address GetOrigin() const;
    uint32_t GetId() const;
    uint32_t GetDstSeqNo() const;
    uint32_t GetOriginSeqNo() const;
    uint32_t GetHopCount() const;

  private:
    uint32_t m_requestID;
    Ipv4Address m_dst;
    uint32_t m_dstSeqNo;
    Ipv4Address m_origin;
    uint32_t m_originSeqNo;
    uint32_t m_hopCount;
};

class RrepHeader : public Header
{
  public:
    RrepHeader();

    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    void Print(std::ostream& os) const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;

    void SetDst(Ipv4Address a);
    void SetOrigin(Ipv4Address a);
    void SetDstSeqNo(uint32_t s);
    void SetHopCount(uint32_t h);
    void IncrementHopCount();
    void SetLifeTime(Time t);

    void AddPathNode(Ipv4Address a);

    Ipv4Address GetDst() const;
    Ipv4Address GetOrigin() const;
    uint32_t GetDstSeqNo() const;
    uint32_t GetHopCount() const;
    Time GetLifeTime() const;
    std::vector<Ipv4Address> GetPathList() const;

  private:
    Ipv4Address m_dst;
    uint32_t m_dstSeqNo;
    Ipv4Address m_origin;
    uint32_t m_hopCount;
    Time m_lifeTime;
    std::vector<Ipv4Address> m_pathList; // for multipath implementation
};

class RerrHeader : public Header
{
  public:
    RerrHeader();

    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    void Print(std::ostream& os) const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;

    void AddUnreachableNode(Ipv4Address dst, uint32_t seqNo);
    bool RemoveUnreachableNode(Ipv4Address dst, uint32_t& seqNo);
    bool IsEmpty() const;
    uint32_t GetDestCount() const;

    const std::vector<std::pair<Ipv4Address, uint32_t>>& GetUnreachableNodes() const;

  private:
    std::vector<std::pair<Ipv4Address, uint32_t>> m_unreachable;
};

} // namespace aomdv
} // namespace ns3
