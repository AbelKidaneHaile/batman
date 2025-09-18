#ifndef BATMAN_PACKET_H
#define BATMAN_PACKET_H

#include "ns3/header.h"
#include "ns3/ipv4-address.h"
#include <vector>
#include <set>

namespace ns3 {

/**
 * \ingroup batman
 * \brief B.A.T.M.A.N. packet header
 *
 * The packet format is:
 * \verbatim
 * 0                   1                   2                   3
 * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |     Version   |      TTL      |      TQ       |   Reserved    |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                        Sequence Number                        |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                        Originator Address                     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                        Previous Sender                       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |   Num Neighbors   |                Reserved                   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                    Bidirectional Neighbors                   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                              ...                              |
 * \endverbatim
 */

class BatmanPacket : public Header
{
public:
  /// Default constructor
  BatmanPacket();
  
  /// Destructor
  virtual ~BatmanPacket();

  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId();

  // Header serialization/deserialization
  virtual TypeId GetInstanceTypeId() const;
  virtual void Print(std::ostream &os) const;
  virtual uint32_t GetSerializedSize() const;
  virtual void Serialize(Buffer::Iterator start) const;
  virtual uint32_t Deserialize(Buffer::Iterator start);

  // Getters and setters
  void SetOriginator(Ipv4Address originator) { m_originator = originator; }
  Ipv4Address GetOriginator() const { return m_originator; }

  void SetPrevSender(Ipv4Address prevSender) { m_prevSender = prevSender; }
  Ipv4Address GetPrevSender() const { return m_prevSender; }

  void SetSeqNum(uint16_t seqNum) { m_seqNum = seqNum; }
  uint16_t GetSeqNum() const { return m_seqNum; }

  void SetTTL(uint8_t ttl) { m_ttl = ttl; }
  uint8_t GetTTL() const { return m_ttl; }

  void SetTQ(uint8_t tq) { m_tq = tq; }
  uint8_t GetTQ() const { return m_tq; }

  void SetVersion(uint8_t version) { m_version = version; }
  uint8_t GetVersion() const { return m_version; }

  // Bidirectional neighbors management
  void AddBidirectionalNeighbor(Ipv4Address neighbor);
  void RemoveBidirectionalNeighbor(Ipv4Address neighbor);
  void SetBidirectionalNeighbors(const std::set<Ipv4Address>& neighbors);
  const std::set<Ipv4Address>& GetBidirectionalNeighbors() const;
  bool HasBidirectionalNeighbor(Ipv4Address neighbor) const;
  uint8_t GetNumBidirectionalNeighbors() const;

private:
  uint8_t m_version;                              ///< Protocol version (default 1)
  uint8_t m_ttl;                                  ///< Time to live
  uint8_t m_tq;                                   ///< Transmission quality
  uint16_t m_seqNum;                              ///< Sequence number
  Ipv4Address m_originator;                       ///< Originator address
  Ipv4Address m_prevSender;                       ///< Previous sender address
  std::set<Ipv4Address> m_bidirectionalNeighbors; ///< List of bidirectional neighbors

  static const uint8_t BATMAN_VERSION = 1;       ///< Protocol version
};

} // namespace ns3

#endif /* BATMAN_PACKET_H */