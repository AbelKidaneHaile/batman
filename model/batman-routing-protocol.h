#ifndef BATMAN_ROUTING_PROTOCOL_H
#define BATMAN_ROUTING_PROTOCOL_H

#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4-interface.h"
#include "ns3/inet-socket-address.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/ipv4-routing-table-entry.h"
#include "ns3/random-variable-stream.h"
#include "ns3/timer.h"
#include "ns3/node.h"
#include "ns3/wifi-net-device.h"
#include "ns3/socket.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/batman-packet.h"
#include <map>
#include <vector>
#include <set>

namespace ns3 {

/**
 * \ingroup batman
 * \brief Batman routing protocol implementation
 *
 * This class implements the Batman (Better Approach To Mobile Adhoc Networking)
 * routing protocol for mesh networks. Batman uses a proactive link-state approach
 * where nodes periodically broadcast originator messages (OGMs) to discover and
 * maintain routes to all other nodes in the network.
 */
class BatmanRoutingProtocol : public Ipv4RoutingProtocol
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId();
  
  /**
   * \brief Constructor
   */
  BatmanRoutingProtocol();
  
  /**
   * \brief Destructor
   */
  virtual ~BatmanRoutingProtocol();

  // Inherited from Ipv4RoutingProtocol
  virtual Ptr<Ipv4Route> RouteOutput(Ptr<Packet> p, const Ipv4Header &header,
                                   Ptr<NetDevice> oif, Socket::SocketErrno &sockerr);
  virtual bool RouteInput(Ptr<const Packet> p, const Ipv4Header &header,
                         Ptr<const NetDevice> idev, UnicastForwardCallback ucb,
                         MulticastForwardCallback mcb, LocalDeliverCallback lcb,
                         ErrorCallback ecb);
  virtual void SetIpv4(Ptr<Ipv4> ipv4);
  virtual void PrintRoutingTable(Ptr<OutputStreamWrapper> stream,
                               Time::Unit unit = Time::S) const;
  virtual void NotifyInterfaceUp(uint32_t interface);
  virtual void NotifyInterfaceDown(uint32_t interface);
  virtual void NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address);
  virtual void NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address);

  /**
   * Assign a fixed random variable stream number to the random variables
   * used by this model. Return the number of streams (possibly zero) that
   * have been assigned.
   *
   * \param stream first stream index to use
   * \return the number of stream indices assigned by this helper
   */
  int64_t AssignStreams(int64_t stream);

protected:
  virtual void DoDispose();

private:
  /**
   * \brief Structure to hold route cache entries
   */
  struct RouteEntry {
    Ipv4Address destination;     //!< Destination address
    Ipv4Address nextHop;         //!< Next hop address
    uint32_t interface;          //!< Interface to use
    uint8_t metric;              //!< Route metric (hop count)
    Time lastUpdate;             //!< Last update time
    
    RouteEntry() : interface(0), metric(0), lastUpdate(Seconds(0)) {}
  };

  /**
   * \brief Structure to hold neighbor information
   */
  struct NeighborInfo {
    Ipv4Address neighbor;        //!< Neighbor IP address
    Time lastSeen;              //!< Last time we heard from this neighbor
    uint8_t tq;                 //!< Transmission Quality to this neighbor
    uint16_t lastSeqNum;        //!< Last sequence number received
    uint32_t interface;         //!< Interface through which neighbor is reachable
    Time bidirectionalTimeout; //!< Timeout for bidirectional link
    bool isBidirectional;       //!< Whether the link is bidirectional
    std::vector<Ipv4Address> bidirectionalNeighbors; //!< List of neighbors announced by this neighbor
    
    NeighborInfo() : lastSeen(Seconds(0)), tq(0), lastSeqNum(0), interface(0), 
                     bidirectionalTimeout(Seconds(0)), isBidirectional(false) {}
  };

  /**
   * \brief Structure to hold originator information
   */
  struct OriginatorInfo {
    Ipv4Address originator;     //!< Originator IP address
    Ipv4Address nextHop;        //!< Next hop to reach originator
    uint8_t tq;                 //!< Best transmission quality to originator
    uint16_t lastSeqNum;        //!< Last sequence number from originator
    Time lastUpdate;            //!< Last update time
    uint32_t interface;         //!< Interface to use for this route
    uint8_t hopCount;           //!< Number of hops to originator
    
    OriginatorInfo() : tq(0), lastSeqNum(0), lastUpdate(Seconds(0)), interface(0), hopCount(0) {}
  };

  /**
   * \brief Structure for sliding window TQ calculation
   */
  struct SlidingWindow {
    std::vector<bool> receivedPackets; //!< Sliding window of received packets
    uint32_t windowSize;               //!< Size of the sliding window
    uint32_t currentIndex;             //!< Current index in the window
    uint32_t receivedCount;            //!< Number of received packets in window
    
    SlidingWindow(uint32_t size = 64) : windowSize(size), currentIndex(0), receivedCount(0) {
      receivedPackets.resize(windowSize, false);
    }
  };

  // Member variables
  Ptr<Ipv4> m_ipv4;                                    //!< IPv4 object
  std::map<Ipv4Address, NeighborInfo> m_neighbors;     //!< Neighbor information table
  std::map<Ipv4Address, OriginatorInfo> m_originators; //!< Originator information table
  std::map<Ipv4Address, RouteEntry> m_routeCache;      //!< Route cache for efficient lookups
  std::map<uint32_t, Ptr<Socket>> m_socketAddresses;  //!< Socket per interface
  std::map<Ipv4Address, SlidingWindow> m_slidingWindows; //!< Sliding windows for TQ calculation
  std::set<uint32_t> m_interfaceExclusions;            //!< Excluded interfaces
  
  // Timers
  Timer m_helloTimer;          //!< Timer for sending hello messages
  Timer m_purgeTimer;          //!< Timer for purging old entries
  
  // Protocol parameters
  uint16_t m_seqNum;           //!< Sequence number for our messages
  Time m_helloInterval;        //!< Hello interval
  Time m_purgeTimeout;         //!< Timeout for purging neighbors/originators
  Time m_bidirectionalTimeout; //!< Timeout for bidirectional links
  uint8_t m_hopPenalty;        //!< Penalty applied per hop
  uint8_t m_maxTTL;            //!< Maximum TTL for packets
  uint32_t m_windowSize;       //!< Size of sliding window for TQ calculation
  bool m_enableBidirectionalCheck; //!< Enable bidirectional link checking
  
  // Random variable for jitter
  Ptr<UniformRandomVariable> m_uniformRandomVariable;

  // Protocol methods
  
  /**
   * \brief Start the protocol operation
   */
  void Start();
  
  /**
   * \brief Send Batman originator message on all interfaces
   */
  void SendBatmanPacket();
  
  /**
   * \brief Purge old neighbors and originators
   */
  void PurgeNeighbors();
  
  /**
   * \brief Hello timer expiry handler
   */
  void HelloTimerExpire();
  
  /**
   * \brief Purge timer expiry handler
   */
  void PurgeTimerExpire();
  
  /**
   * \brief Update route in routing cache
   * \param dest Destination address
   * \param nextHop Next hop address
   * \param interface Interface to use
   * \param hopCount Number of hops
   */
  void UpdateRoute(Ipv4Address dest, Ipv4Address nextHop, uint32_t interface, uint8_t hopCount);
  
  /**
   * \brief Calculate transmission quality using sliding window
   * \param neighbor Neighbor address
   * \param receivedTQ TQ value received in packet
   * \return Calculated TQ value
   */
  uint8_t CalculateTQ(Ipv4Address neighbor, uint8_t receivedTQ);
  
  /**
   * \brief Check if this is our own broadcast
   * \param packet The Batman packet to check
   * \return True if this is our own packet
   */
  bool IsMyOwnBroadcast(const BatmanPacket& packet);
  
  /**
   * \brief Forward Batman packet to other interfaces
   * \param packet The packet to forward
   * \param incomingInterface Interface the packet was received on
   */
  void ForwardBatmanPacket(const BatmanPacket& packet, uint32_t incomingInterface);
  
  /**
   * \brief Check if sequence number is newer
   * \param newSeq New sequence number
   * \param oldSeq Old sequence number
   * \return True if new sequence number is newer
   */
  bool IsNewerSequence(uint16_t newSeq, uint16_t oldSeq);
  
  /**
   * \brief Update sliding window for TQ calculation
   * \param neighbor Neighbor address
   * \param seqNum Sequence number of received packet
   */
  void UpdateSlidingWindow(Ipv4Address neighbor, uint16_t seqNum);
  
  /**
   * \brief Calculate TQ from sliding window
   * \param neighbor Neighbor address
   * \return TQ value (0-255)
   */
  uint8_t GetSlidingWindowTQ(Ipv4Address neighbor);
  
  /**
   * \brief Check if link to neighbor is bidirectional
   * \param neighbor Neighbor address
   * \return True if link is bidirectional
   */
  bool IsBidirectionalNeighbor(Ipv4Address neighbor);
  
  /**
   * \brief Remove route from routing cache
   * \param dest Destination address
   */
  void RemoveRoute(Ipv4Address dest);

  // Socket handling methods
  
  /**
   * \brief Find socket associated with interface address
   * \param addr Interface address
   * \return Socket pointer or null
   */
  Ptr<Socket> FindSocketWithInterfaceAddress(Ipv4InterfaceAddress addr) const;
  
  /**
   * \brief Receive Batman packet callback
   * \param socket Socket that received the packet
   */
  void RecvBatman(Ptr<Socket> socket);
  
  /**
   * \brief Get interface index for given socket
   * \param socket Socket pointer
   * \return Interface index
   */
  uint32_t GetInterfaceForSocket(Ptr<Socket> socket) const;
  
  /**
   * \brief Check if interface is excluded from Batman operation
   * \param interface Interface index
   * \return True if excluded
   */
  bool IsInterfaceExcluded(uint32_t interface) const;
};

} // namespace ns3

#endif // BATMAN_ROUTING_PROTOCOL_H