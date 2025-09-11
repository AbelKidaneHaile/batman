#ifndef BATMAN_PACKET_H
#define BATMAN_PACKET_H

#include "ns3/header.h"
#include "ns3/ipv4-address.h"
#include "ns3/nstime.h"

namespace ns3 {

/**
 * \ingroup batman
 * \brief Batman packet header
 *
 * Batman packets contain originator information and are used to discover
 * and maintain routes in the mesh network. Each packet contains:
 * - Originator address: The node that originally created this packet
 * - Previous sender: The node that last forwarded this packet  
 * - Transmission Quality (TQ): Quality metric for the path
 * - Sequence number: To detect loops and measure packet loss
 * - TTL: Time to live to limit propagation
 */
class BatmanPacket : public Header
{
public:
  /**
   * \brief Constructor
   */
  BatmanPacket();
  
  /**
   * \brief Destructor
   */
  virtual ~BatmanPacket();

  /**
   * \brief Set the originator address
   * \param originator The originator IP address
   */
  void SetOriginator(Ipv4Address originator) { m_originator = originator; }
  
  /**
   * \brief Get the originator address
   * \return The originator IP address
   */
  Ipv4Address GetOriginator() const { return m_originator; }
  
  /**
   * \brief Set the previous sender address
   * \param prevSender The previous sender IP address
   */
  void SetPrevSender(Ipv4Address prevSender) { m_prevSender = prevSender; }
  
  /**
   * \brief Get the previous sender address
   * \return The previous sender IP address
   */
  Ipv4Address GetPrevSender() const { return m_prevSender; }
  
  /**
   * \brief Set the transmission quality
   * \param tq The transmission quality (0-255)
   */
  void SetTQ(uint8_t tq) { m_tq = tq; }
  
  /**
   * \brief Get the transmission quality
   * \return The transmission quality (0-255)
   */
  uint8_t GetTQ() const { return m_tq; }
  
  /**
   * \brief Set the sequence number
   * \param seqNum The sequence number
   */
  void SetSeqNum(uint16_t seqNum) { m_seqNum = seqNum; }
  
  /**
   * \brief Get the sequence number
   * \return The sequence number
   */
  uint16_t GetSeqNum() const { return m_seqNum; }
  
  /**
   * \brief Set the TTL (Time To Live)
   * \param ttl The TTL value
   */
  void SetTTL(uint8_t ttl) { m_ttl = ttl; }
  
  /**
   * \brief Get the TTL (Time To Live)
   * \return The TTL value
   */
  uint8_t GetTTL() const { return m_ttl; }
  
  /**
   * \brief Set the packet flags
   * \param flags The packet flags
   */
  void SetFlags(uint8_t flags) { m_flags = flags; }
  
  /**
   * \brief Get the packet flags
   * \return The packet flags
   */
  uint8_t GetFlags() const { return m_flags; }

  // Header interface
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId();
  
  /**
   * \brief Get the instance type ID.
   * \return the instance TypeId
   */
  virtual TypeId GetInstanceTypeId() const;
  
  /**
   * \brief Print the packet contents
   * \param os The output stream
   */
  virtual void Print(std::ostream &os) const;
  
  /**
   * \brief Get the serialized size
   * \return The serialized size in bytes
   */
  virtual uint32_t GetSerializedSize() const;
  
  /**
   * \brief Serialize the packet
   * \param start The buffer iterator
   */
  virtual void Serialize(Buffer::Iterator start) const;
  
  /**
   * \brief Deserialize the packet
   * \param start The buffer iterator
   * \return The number of bytes deserialized
   */
  virtual uint32_t Deserialize(Buffer::Iterator start);

  // Packet type flags
  static const uint8_t BATMAN_TYPE_OGM = 0x01;     //!< Originator message
  static const uint8_t BATMAN_TYPE_ECHO = 0x02;    //!< Echo request/reply
  static const uint8_t BATMAN_DIRECTLINK = 0x40;   //!< Direct link flag
  static const uint8_t BATMAN_PRIMARIES_FIRST_HOP = 0x80; //!< Primary interface flag

private:
  Ipv4Address m_originator;  //!< Originator address
  Ipv4Address m_prevSender;  //!< Previous sender address
  uint8_t m_tq;              //!< Transmission quality
  uint16_t m_seqNum;         //!< Sequence number
  uint8_t m_ttl;             //!< Time to live
  uint8_t m_flags;           //!< Packet flags
  uint8_t m_version;         //!< Protocol version
  uint8_t m_reserved;        //!< Reserved field for future use
};

} // namespace ns3

#endif // BATMAN_PACKET_H
