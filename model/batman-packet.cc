#include "batman-packet.h"
#include "ns3/address-utils.h"
#include "ns3/packet.h"
#include "ns3/log.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("BatmanPacket");
NS_OBJECT_ENSURE_REGISTERED(BatmanPacket);

BatmanPacket::BatmanPacket()
  : m_version(BATMAN_VERSION),
    m_ttl(0),
    m_tq(0),
    m_seqNum(0)
{
  NS_LOG_FUNCTION(this);
}

BatmanPacket::~BatmanPacket()
{
  NS_LOG_FUNCTION(this);
}

TypeId
BatmanPacket::GetTypeId()
{
  static TypeId tid = TypeId("ns3::BatmanPacket")
    .SetParent<Header>()
    .SetGroupName("Internet")
    .AddConstructor<BatmanPacket>();
  return tid;
}

TypeId
BatmanPacket::GetInstanceTypeId() const
{
  return GetTypeId();
}

void
BatmanPacket::Print(std::ostream &os) const
{
  os << "BatmanPacket("
     << "Version=" << (uint16_t)m_version
     << ", TTL=" << (uint16_t)m_ttl
     << ", TQ=" << (uint16_t)m_tq
     << ", SeqNum=" << m_seqNum
     << ", Orig=" << m_originator
     << ", PrevSender=" << m_prevSender
     << ", NumNeighbors=" << (uint16_t)m_bidirectionalNeighbors.size()
     << ")";
}

uint32_t
BatmanPacket::GetSerializedSize() const
{
  // fixed header: 1+1+1+1+2+2+4+4+1+3 = 20 bytes
  // variable part: 4 bytes per bidirectional neighbor
  return 20 + (m_bidirectionalNeighbors.size() * 4);
}

void
BatmanPacket::Serialize(Buffer::Iterator start) const
{
  NS_LOG_FUNCTION(this << &start);
  
  Buffer::Iterator i = start;
  
  // write fixed header
  i.WriteU8(m_version);     // 1 byte
  i.WriteU8(m_ttl);         // 1 byte
  i.WriteU8(m_tq);          // 1 byte
  i.WriteU8(0);             // 1 byte - reserved
  
  i.WriteHtonU16(m_seqNum); // 2 bytes
  i.WriteHtonU16(0);        // 2 bytes - reserved/padding
  
  WriteTo(i, m_originator); // 4 bytes
  WriteTo(i, m_prevSender); // 4 bytes
  
  // Write number of bidirectional neighbors
  i.WriteU8(static_cast<uint8_t>(std::min(m_bidirectionalNeighbors.size(), static_cast<size_t>(255))));
  i.WriteU8(0);             // 1 byte - reserved
  i.WriteU8(0);             // 1 byte - reserved  
  i.WriteU8(0);             // 1 byte - reserved
  
  // write bidirectional neighbors (limit to 255 to prevent overflow)
  size_t count = 0;
  for (const auto& neighbor : m_bidirectionalNeighbors) {
    if (count >= 255) break;
    WriteTo(i, neighbor);   // 4 bytes each
    count++;
  }
}

uint32_t
BatmanPacket::Deserialize(Buffer::Iterator start)
{
  NS_LOG_FUNCTION(this << &start);
  
  Buffer::Iterator i = start;

  m_version = i.ReadU8();     // 1 byte
  m_ttl = i.ReadU8();         // 1 byte
  m_tq = i.ReadU8();          // 1 byte
  i.ReadU8();                 // 1 byte - Skip reserved
  
  m_seqNum = i.ReadNtohU16(); // 2 bytes
  i.ReadNtohU16();            // 2 bytes - Skip reserved/padding
  
  ReadFrom(i, m_originator);  // 4 bytes
  ReadFrom(i, m_prevSender);  // 4 bytes

  uint8_t numNeighbors = i.ReadU8(); // 1 byte
  i.ReadU8();                        // 1 byte - Skip reserved
  i.ReadU8();                        // 1 byte - Skip reserved
  i.ReadU8();                        // 1 byte - Skip reserved

  m_bidirectionalNeighbors.clear();
  for (uint8_t j = 0; j < numNeighbors; j++) {
    Ipv4Address neighbor;
    ReadFrom(i, neighbor);    // 4 bytes each
    m_bidirectionalNeighbors.insert(neighbor);
  }
  

  return 20 + (numNeighbors * 4);
}

void
BatmanPacket::AddBidirectionalNeighbor(Ipv4Address neighbor)
{
  m_bidirectionalNeighbors.insert(neighbor);
}

void
BatmanPacket::RemoveBidirectionalNeighbor(Ipv4Address neighbor)
{
  m_bidirectionalNeighbors.erase(neighbor);
}

void
BatmanPacket::SetBidirectionalNeighbors(const std::set<Ipv4Address>& neighbors)
{
  m_bidirectionalNeighbors = neighbors;
}

const std::set<Ipv4Address>&
BatmanPacket::GetBidirectionalNeighbors() const
{
  return m_bidirectionalNeighbors;
}

bool
BatmanPacket::HasBidirectionalNeighbor(Ipv4Address neighbor) const
{
  return m_bidirectionalNeighbors.find(neighbor) != m_bidirectionalNeighbors.end();
}

uint8_t
BatmanPacket::GetNumBidirectionalNeighbors() const
{
  return static_cast<uint8_t>(m_bidirectionalNeighbors.size());
}

} 