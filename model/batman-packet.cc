#include "batman-packet.h"
#include "ns3/address-utils.h"
#include "ns3/packet.h"
#include "ns3/log.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("BatmanPacket");
NS_OBJECT_ENSURE_REGISTERED(BatmanPacket);

TypeId
BatmanPacket::GetTypeId()
{
  static TypeId tid = TypeId("ns3::BatmanPacket")
    .SetParent<Header>()
    .SetGroupName("Internet")
    .AddConstructor<BatmanPacket>();
  return tid;
}

BatmanPacket::BatmanPacket()
  : m_tq(0),
    m_seqNum(0),
    m_ttl(0),
    m_flags(BATMAN_TYPE_OGM),
    m_version(1),
    m_reserved(0)
{
  NS_LOG_FUNCTION(this);
}

BatmanPacket::~BatmanPacket()
{
  NS_LOG_FUNCTION(this);
}

TypeId
BatmanPacket::GetInstanceTypeId() const
{
  return GetTypeId();
}

uint32_t
BatmanPacket::GetSerializedSize() const
{
  // 2 IPv4 addresses (4 bytes each) + TQ + SeqNum + TTL + Flags + Version + Reserved
  return 4 + 4 + 1 + 2 + 1 + 1 + 1 + 1; // 15 bytes total
}

void
BatmanPacket::Serialize(Buffer::Iterator start) const
{
  NS_LOG_FUNCTION(this << &start);
  
  WriteTo(start, m_originator);
  WriteTo(start, m_prevSender);
  start.WriteU8(m_tq);
  start.WriteHtonU16(m_seqNum);
  start.WriteU8(m_ttl);
  start.WriteU8(m_flags);
  start.WriteU8(m_version);
  start.WriteU8(m_reserved);
  
  NS_LOG_DEBUG("Serialized Batman packet: Orig=" << m_originator
               << " PrevSender=" << m_prevSender 
               << " TQ=" << (int)m_tq
               << " SeqNum=" << m_seqNum
               << " TTL=" << (int)m_ttl
               << " Flags=" << (int)m_flags);
}

uint32_t
BatmanPacket::Deserialize(Buffer::Iterator start)
{
  NS_LOG_FUNCTION(this << &start);
  
  ReadFrom(start, m_originator);
  ReadFrom(start, m_prevSender);
  m_tq = start.ReadU8();
  m_seqNum = start.ReadNtohU16();
  m_ttl = start.ReadU8();
  m_flags = start.ReadU8();
  m_version = start.ReadU8();
  m_reserved = start.ReadU8();
  
  NS_LOG_DEBUG("Deserialized Batman packet: Orig=" << m_originator
               << " PrevSender=" << m_prevSender 
               << " TQ=" << (int)m_tq
               << " SeqNum=" << m_seqNum
               << " TTL=" << (int)m_ttl
               << " Flags=" << (int)m_flags);
  
  return GetSerializedSize();
}

void
BatmanPacket::Print(std::ostream &os) const
{
  os << "Batman Packet: "
     << "Orig=" << m_originator
     << " PrevSender=" << m_prevSender
     << " TQ=" << (int)m_tq
     << " SeqNum=" << m_seqNum
     << " TTL=" << (int)m_ttl
     << " Flags=" << (int)m_flags
     << " Version=" << (int)m_version;
}

} // namespace ns3
