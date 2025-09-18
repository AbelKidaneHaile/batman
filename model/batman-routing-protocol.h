#ifndef BATMAN_ROUTING_PROTOCOL_H
#define BATMAN_ROUTING_PROTOCOL_H

#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4.h"
#include "ns3/ipv4-route.h"
#include "ns3/timer.h"
#include "ns3/socket.h"
#include "ns3/ipv4-interface.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/random-variable-stream.h"
#include "batman-packet.h"
#include <map>
#include <set>
#include <vector>

namespace ns3 {

/**
 * \ingroup batman
 * \brief B.A.T.M.A.N. routing protocol implementation
 */

// Neighbor information structure
struct NeighborInfo
{
  Ipv4Address neighbor;         ///< Neighbor IP address
  Time lastSeen;               ///< Last time we heard from this neighbor
  uint16_t lastSeqNum;         ///< Last sequence number received
  uint8_t tq;                  ///< Transmission quality (0-255)
  uint32_t interface;          ///< Interface where neighbor was seen
  bool isBidirectional;        ///< True if bidirectional link confirmed
  Time bidirectionalTimeout;   ///< When bidirectional check expires
  std::set<Ipv4Address> bidirectionalNeighbors; ///< Neighbors announced by this neighbor

  NeighborInfo() 
    : lastSeen(Seconds(0)), lastSeqNum(0), tq(0), interface(0), 
      isBidirectional(false), bidirectionalTimeout(Seconds(0)) {}
};

// Originator information structure
struct OriginatorInfo
{
  Ipv4Address originator;      ///< Originator IP address
  Ipv4Address nextHop;         ///< Next hop to reach originator
  uint8_t tq;                  ///< Best transmission quality to originator
  uint16_t lastSeqNum;         ///< Last sequence number from originator
  Time lastUpdate;             ///< Last update time
  uint32_t interface;          ///< Interface to use for this route
  uint8_t hopCount;            ///< Hop count to originator

  OriginatorInfo() 
    : tq(0), lastSeqNum(0), lastUpdate(Seconds(0)), interface(0), hopCount(0) {}
};

// Route cache entry
struct RouteEntry
{
  Ipv4Address destination;     ///< Destination address
  Ipv4Address nextHop;         ///< Next hop address
  uint32_t interface;          ///< Output interface
  uint8_t metric;              ///< Route metric (hop count)
  Time lastUpdate;             ///< Last update time

  RouteEntry() 
    : interface(0), metric(0), lastUpdate(Seconds(0)) {}
};

// Sliding window for TQ calculation
struct SlidingWindow
{
  std::vector<bool> receivedPackets; ///< Bitmap of received packets
  uint32_t windowSize;               ///< Size of the window
  uint32_t currentIndex;             ///< Current position in window
  uint32_t receivedCount;            ///< Number of received packets in window

  SlidingWindow(uint32_t size = 64) 
    : receivedPackets(size, false), windowSize(size), currentIndex(0), receivedCount(0) {}
};

class BatmanRoutingProtocol : public Ipv4RoutingProtocol
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId();

  /// Default constructor
  BatmanRoutingProtocol();
  
  /// Destructor
  virtual ~BatmanRoutingProtocol();

  // Inherited from Ipv4RoutingProtocol
  virtual Ptr<Ipv4Route> RouteOutput(Ptr<Packet> p, const Ipv4Header &header,
                                    Ptr<NetDevice> oif, Socket::SocketErrno &sockerr);

  virtual bool RouteInput(Ptr<const Packet> p, const Ipv4Header &header,
                         Ptr<const NetDevice> idev, UnicastForwardCallback ucb,
                         MulticastForwardCallback mcb, LocalDeliverCallback lcb,
                         ErrorCallback ecb);

  virtual void NotifyInterfaceUp(uint32_t interface);
  virtual void NotifyInterfaceDown(uint32_t interface);
  virtual void NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address);
  virtual void NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address);
  virtual void SetIpv4(Ptr<Ipv4> ipv4);
  virtual void PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit = Time::S) const;

  /**
   * Assign a fixed random variable stream number to the random variables used by this model.
   * \param stream first stream index to use
   * \return the number of stream indices assigned by this model
   */
  int64_t AssignStreams(int64_t stream);

private:
  /// Start protocol operation
  void Start();

  /// Cleanup
  virtual void DoDispose();

  // Timer callbacks
  void HelloTimerExpire();
  void PurgeTimerExpire();

  // Packet handling
  void SendBatmanPacket();
  void RecvBatman(Ptr<Socket> socket);
  void ForwardBatmanPacket(const BatmanPacket& receivedPacket, uint32_t incomingInterface);

  // Utility functions
  bool IsMyOwnBroadcast(const BatmanPacket& packet);
  uint8_t CalculateTQ(Ipv4Address neighbor, uint8_t receivedTQ);
  bool IsNewerSequence(uint16_t newSeq, uint16_t oldSeq);
  bool IsBidirectionalNeighbor(Ipv4Address neighbor);
  uint32_t GetInterfaceForSocket(Ptr<Socket> socket) const;
  bool IsInterfaceExcluded(uint32_t interface) const;
  Ptr<Socket> FindSocketWithInterfaceAddress(Ipv4InterfaceAddress addr) const;

  // Sliding window functions
  void UpdateSlidingWindow(Ipv4Address neighbor, uint16_t seqNum);
  uint8_t GetSlidingWindowTQ(Ipv4Address neighbor);

  // Route management
  void UpdateRoute(Ipv4Address dest, Ipv4Address nextHop, uint32_t interface, uint8_t hopCount);
  void RemoveRoute(Ipv4Address dest);
  void PurgeNeighbors();

  // Member variables
  Ptr<Ipv4> m_ipv4;                                    ///< IPv4 reference
  std::map<uint32_t, Ptr<Socket>> m_socketAddresses;  ///< Socket per interface
  std::map<Ipv4Address, NeighborInfo> m_neighbors;     ///< Neighbor table
  std::map<Ipv4Address, OriginatorInfo> m_originators; ///< Originator table
  std::map<Ipv4Address, RouteEntry> m_routeCache;      ///< Route cache
  std::map<Ipv4Address, SlidingWindow> m_slidingWindows; ///< Sliding windows for TQ
  std::set<uint32_t> m_interfaceExclusions;            ///< Excluded interfaces

  // Timers
  Timer m_helloTimer;        ///< Timer for hello broadcasts
  Timer m_purgeTimer;        ///< Timer for purging old entries

  // Configuration parameters
  uint16_t m_seqNum;                    ///< Sequence number for own packets
  Time m_helloInterval;                 ///< Hello broadcast interval
  Time m_purgeTimeout;                  ///< Timeout for purging entries
  Time m_bidirectionalTimeout;          ///< Timeout for bidirectional checks
  uint8_t m_hopPenalty;                 ///< Penalty applied per hop
  uint8_t m_maxTTL;                     ///< Maximum TTL for Batman packets
  uint32_t m_windowSize;                ///< Size of sliding window
  bool m_enableBidirectionalCheck;      ///< Enable bidirectional checking

  /// Uniform random variable for jitter
  Ptr<UniformRandomVariable> m_uniformRandomVariable;
};

} // namespace ns3

#endif /* BATMAN_ROUTING_PROTOCOL_H */