#include "batman-routing-protocol.h"
#include "ns3/log.h"
#include "ns3/inet-socket-address.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/wifi-net-device.h"
#include "ns3/adhoc-wifi-mac.h"
#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/string.h"
#include "ns3/pointer.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/ipv4-static-routing.h"
// #include "ipv4-static-routing-helper.h"
// #include "ipv4-static-routing.h"
#include <algorithm>
#include <iomanip>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("BatmanRoutingProtocol");
NS_OBJECT_ENSURE_REGISTERED(BatmanRoutingProtocol);

const uint32_t BATMAN_PORT = 4305;

struct RouteInfo {
  Ipv4Address destination;
  Ipv4Address nextHop;
  uint8_t tq;
  bool isBidirectional;
  uint8_t hopCount;
  bool isFromOriginators;
  std::string routeType;
  
  RouteInfo() : tq(0), isBidirectional(false), hopCount(0), isFromOriginators(false) {}
};

TypeId
BatmanRoutingProtocol::GetTypeId()
{
  static TypeId tid = TypeId("ns3::BatmanRoutingProtocol")
    .SetParent<Ipv4RoutingProtocol>()
    .SetGroupName("Internet")
    .AddConstructor<BatmanRoutingProtocol>()
    .AddAttribute("HelloInterval", 
                  "Hello messages broadcast interval",
                  TimeValue(Seconds(1.0)),
                  MakeTimeAccessor(&BatmanRoutingProtocol::m_helloInterval),
                  MakeTimeChecker())
    .AddAttribute("PurgeTimeout", 
                  "Time to wait before purging neighbor",
                  TimeValue(Seconds(3.0)),
                  MakeTimeAccessor(&BatmanRoutingProtocol::m_purgeTimeout),
                  MakeTimeChecker())
    .AddAttribute("BidirectionalTimeout", 
                  "Time to wait for bidirectional link confirmation",
                  TimeValue(Seconds(2.0)),
                  MakeTimeAccessor(&BatmanRoutingProtocol::m_bidirectionalTimeout),
                  MakeTimeChecker())
    .AddAttribute("HopPenalty", 
                  "Penalty applied per hop (0-255)",
                  UintegerValue(10),
                  MakeUintegerAccessor(&BatmanRoutingProtocol::m_hopPenalty),
                  MakeUintegerChecker<uint8_t>())
    .AddAttribute("MaxTTL", 
                  "Maximum TTL for Batman packets",
                  UintegerValue(50),
                  MakeUintegerAccessor(&BatmanRoutingProtocol::m_maxTTL),
                  MakeUintegerChecker<uint8_t>())
    .AddAttribute("WindowSize", 
                  "Size of sliding window for TQ calculation",
                  UintegerValue(64),
                  MakeUintegerAccessor(&BatmanRoutingProtocol::m_windowSize),
                  MakeUintegerChecker<uint32_t>())
    .AddAttribute("EnableBidirectionalCheck", 
                  "Enable bidirectional link checking",
                  BooleanValue(true),
                  MakeBooleanAccessor(&BatmanRoutingProtocol::m_enableBidirectionalCheck),
                  MakeBooleanChecker());
  return tid;
}

BatmanRoutingProtocol::BatmanRoutingProtocol()
  : m_seqNum(0),
    m_helloInterval(Seconds(1.0)),
    m_purgeTimeout(Seconds(3.0)),
    m_bidirectionalTimeout(Seconds(2.0)),
    m_hopPenalty(10),
    m_maxTTL(50),
    m_windowSize(64),
    m_enableBidirectionalCheck(true)
{
  NS_LOG_FUNCTION(this);
  m_uniformRandomVariable = CreateObject<UniformRandomVariable>();
}

BatmanRoutingProtocol::~BatmanRoutingProtocol()
{
  NS_LOG_FUNCTION(this);
}

void
BatmanRoutingProtocol::DoDispose()
{
  NS_LOG_FUNCTION(this);
  
  m_ipv4 = 0;
  m_neighbors.clear();
  m_originators.clear();
  m_routeCache.clear();
  m_slidingWindows.clear();
  
  for (std::map<uint32_t, Ptr<Socket>>::iterator iter = m_socketAddresses.begin();
       iter != m_socketAddresses.end(); iter++)
  {
    iter->second->Close();
  }
  m_socketAddresses.clear();
  
  m_helloTimer.Cancel();
  m_purgeTimer.Cancel();
  
  Ipv4RoutingProtocol::DoDispose();
}

void
BatmanRoutingProtocol::SetIpv4(Ptr<Ipv4> ipv4)
{
  NS_LOG_FUNCTION(this << ipv4);
  NS_ASSERT(ipv4 != 0);
  NS_ASSERT(m_ipv4 == 0);
  
  m_ipv4 = ipv4;
  
  m_helloTimer.SetFunction(&BatmanRoutingProtocol::HelloTimerExpire, this);
  m_purgeTimer.SetFunction(&BatmanRoutingProtocol::PurgeTimerExpire, this);
  
  Simulator::ScheduleNow(&BatmanRoutingProtocol::Start, this);
}

void
BatmanRoutingProtocol::Start()
{
  NS_LOG_FUNCTION(this);
  
  Ptr<Socket> socket = Socket::CreateSocket(GetObject<Node>(),
                                          UdpSocketFactory::GetTypeId());
  
  InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), BATMAN_PORT);
  
  if (socket->Bind(local) == -1) {
    NS_LOG_ERROR("Failed to bind Batman socket");
    return;
  }
  
  socket->SetAllowBroadcast(true);
  socket->SetAttribute("IpTtl", UintegerValue(1));
  socket->SetRecvCallback(MakeCallback(&BatmanRoutingProtocol::RecvBatman, this));

  for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); i++) {
    if (m_ipv4->GetAddress(i, 0).GetLocal() == Ipv4Address::GetLoopback()) {
      continue;
    }
    
    if (IsInterfaceExcluded(i)) {
      NS_LOG_INFO("Interface " << i << " excluded from Batman operation");
      continue;
    }
    
    Ipv4InterfaceAddress ifaceAddr = m_ipv4->GetAddress(i, 0);
    if (ifaceAddr.GetLocal() == Ipv4Address("0.0.0.0")) {
      NS_LOG_WARN("Interface " << i << " has no valid IP address, skipping");
      continue;
    }
    
    m_socketAddresses[i] = socket;
    NS_LOG_INFO("Batman socket assigned to interface " << i 
                << " with address " << ifaceAddr.GetLocal());
  }
  
  Time jitter = Time(MilliSeconds(m_uniformRandomVariable->GetInteger(0, 100)));
  m_helloTimer.Schedule(jitter);
  m_purgeTimer.Schedule(m_purgeTimeout);
  
  NS_LOG_INFO("Batman protocol started with shared socket");
}

void
BatmanRoutingProtocol::HelloTimerExpire()
{
  SendBatmanPacket();
  m_helloTimer.Schedule(m_helloInterval);
}

void
BatmanRoutingProtocol::SendBatmanPacket()
{
  NS_LOG_FUNCTION(this);
  
  if (m_socketAddresses.empty()) {
    NS_LOG_ERROR("No sockets available for sending Batman packets");
    return;
  }
  
  auto iter = m_socketAddresses.begin();
  uint32_t interface = iter->first;
  Ptr<Socket> socket = iter->second;
  
  Ipv4InterfaceAddress iface = m_ipv4->GetAddress(interface, 0);
  
  BatmanPacket batmanPacket;
  batmanPacket.SetOriginator(iface.GetLocal());
  batmanPacket.SetPrevSender(iface.GetLocal());
  batmanPacket.SetTQ(255);                                                      // maximum quality for own packets 255
  batmanPacket.SetSeqNum(++m_seqNum);
  batmanPacket.SetTTL(m_maxTTL);

  std::set<Ipv4Address> bidirectionalNeighbors;
  for (const auto& neighbor : m_neighbors) {
    if (neighbor.second.isBidirectional) {
      bidirectionalNeighbors.insert(neighbor.first);
    }
  }
  batmanPacket.SetBidirectionalNeighbors(bidirectionalNeighbors);
  
  Ptr<Packet> packet = Create<Packet>();
  packet->AddHeader(batmanPacket);
  
  Address destination;

  bool sent = false;

  try {
    InetSocketAddress dest1(iface.GetBroadcast(), BATMAN_PORT);
    int result1 = socket->SendTo(packet, 0, dest1);
    if (result1 != -1) {
      sent = true;
      NS_LOG_DEBUG("Sent Batman packet via subnet broadcast: Orig=" << iface.GetLocal() 
                   << " SeqNum=" << batmanPacket.GetSeqNum() 
                   << " Interface=" << interface
                   << " BidirNeighbors=" << bidirectionalNeighbors.size());
    }
  } catch (...) {

  }
  

  if (!sent) {
    try {
      InetSocketAddress dest2(Ipv4Address("255.255.255.255"), BATMAN_PORT);
      int result2 = socket->SendTo(packet, 0, dest2);
      if (result2 != -1) {
        sent = true;
        NS_LOG_DEBUG("Sent Batman packet via limited broadcast: Orig=" << iface.GetLocal() 
                     << " SeqNum=" << batmanPacket.GetSeqNum());
      }
    } catch (...) {

    }
  }
  
  if (!sent) {
    NS_LOG_WARN("Failed to send Batman packet on interface " << interface);
  }
}

void BatmanRoutingProtocol::RecvBatman(Ptr<Socket> socket)
{
  NS_LOG_FUNCTION(this << socket);
  
  Ptr<Packet> receivedPacket;
  Address sourceAddress;
  receivedPacket = socket->RecvFrom(sourceAddress);
  
  if (receivedPacket == 0 || receivedPacket->GetSize() == 0) {
    NS_LOG_WARN("RecvBatman: empty packet received");
    return;
  }
  
  NS_LOG_DEBUG("RecvBatman: packet size=" << receivedPacket->GetSize());
  
  InetSocketAddress inetSourceAddr = InetSocketAddress::ConvertFrom(sourceAddress);
  Ipv4Address sender = inetSourceAddr.GetIpv4();
  
  BatmanPacket batmanPacket;
  receivedPacket->RemoveHeader(batmanPacket);
  
  NS_LOG_DEBUG("Received Batman packet from " << sender 
               << " Orig=" << batmanPacket.GetOriginator()
               << " SeqNum=" << batmanPacket.GetSeqNum()
               << " TQ=" << (int)batmanPacket.GetTQ()
               << " TTL=" << (int)batmanPacket.GetTTL()
               << " BidirNeighbors=" << batmanPacket.GetNumBidirectionalNeighbors());
  
  if (IsMyOwnBroadcast(batmanPacket)) {
    NS_LOG_DEBUG("Ignoring own broadcast");
    return;
  }

  uint32_t incomingInterface = GetInterfaceForSocket(socket);
  if (incomingInterface == std::numeric_limits<uint32_t>::max()) {
    NS_LOG_ERROR("Could not determine incoming interface");
    return;
  }
  
  if (batmanPacket.GetPrevSender() == sender) {
    UpdateSlidingWindow(sender, batmanPacket.GetSeqNum());
    
    NeighborInfo& neighbor = m_neighbors[sender];
    neighbor.neighbor = sender;
    neighbor.lastSeen = Simulator::Now();
    neighbor.lastSeqNum = batmanPacket.GetSeqNum();
    neighbor.interface = incomingInterface;
    neighbor.tq = GetSlidingWindowTQ(sender);
    neighbor.bidirectionalNeighbors = batmanPacket.GetBidirectionalNeighbors();
    
    if (m_enableBidirectionalCheck) {
      neighbor.isBidirectional = IsBidirectionalNeighbor(sender);
      neighbor.bidirectionalTimeout = Simulator::Now() + m_bidirectionalTimeout;
    } else {
      neighbor.isBidirectional = true;
    }
    
    NS_LOG_DEBUG("Updated neighbor " << sender << " TQ=" << (int)neighbor.tq 
                 << " Bidirectional=" << neighbor.isBidirectional);
  }
  
  Ipv4Address originator = batmanPacket.GetOriginator();
  
  bool isOurAddress = false;
  for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); i++) {
    if (m_ipv4->GetAddress(i, 0).GetLocal() == originator) {
      isOurAddress = true;
      break;
    }
  }
  
  if (isOurAddress) {
    NS_LOG_DEBUG("Ignoring packet about our own address");
    return;
  }

  bool isNewer = (m_originators.find(originator) == m_originators.end()) ||
                 IsNewerSequence(batmanPacket.GetSeqNum(), m_originators[originator].lastSeqNum);
  
  if (isNewer || batmanPacket.GetSeqNum() == m_originators[originator].lastSeqNum) {
    uint8_t newTQ = CalculateTQ(sender, batmanPacket.GetTQ());
    
    //---->>> Decision criteria for route updates:
    // 1. if this is a new destination, accept any route with TQ > 0
    // 2. if newer sequence number, accept if TQ is reasonable (> 10)
    // 3. if same sequence number, only accept if TQ is significantly better (+20)
    // 4. for multi-hop scenarios, don't overly penalize longer paths with better TQ
    
    bool shouldUpdate = false;
    bool isNewDestination = (m_originators.find(originator) == m_originators.end());
    uint8_t currentTQ = 0;
    

    if (!isNewDestination) {
      currentTQ = m_originators[originator].tq;
    }
    

    if (isNewDestination) {
      shouldUpdate = (newTQ > 10); 
      NS_LOG_DEBUG("New destination " << originator << " TQ=" << (int)newTQ);
    } 
    else if (IsNewerSequence(batmanPacket.GetSeqNum(), m_originators[originator].lastSeqNum)) {
      shouldUpdate = (newTQ > 10); 
      NS_LOG_DEBUG("Newer sequence for " << originator << " old_seq=" << m_originators[originator].lastSeqNum 
                   << " new_seq=" << batmanPacket.GetSeqNum() << " TQ=" << (int)newTQ);
    }
    else if (batmanPacket.GetSeqNum() == m_originators[originator].lastSeqNum) {

      uint8_t oldHops = m_originators[originator].hopCount;
      uint8_t newHops = m_maxTTL - batmanPacket.GetTTL() + 1;

      if (newTQ > currentTQ + 15) {
        shouldUpdate = true;
        NS_LOG_DEBUG("Better route for " << originator << " current_TQ=" << (int)currentTQ 
                     << " new_TQ=" << (int)newTQ << " current_hops=" << (int)oldHops 
                     << " new_hops=" << (int)newHops);
      }
      else if (newHops < oldHops && newTQ >= currentTQ - 10) {
        shouldUpdate = true;
        NS_LOG_DEBUG("Shorter path for " << originator << " TQ=" << (int)newTQ 
                     << " hops: " << (int)oldHops << " -> " << (int)newHops);
      }
    }
    
    if (shouldUpdate && newTQ > 0) {

      bool senderIsReachable = true;
      if (m_enableBidirectionalCheck && sender == batmanPacket.GetPrevSender()) {
        auto neighborIt = m_neighbors.find(sender);
        if (neighborIt != m_neighbors.end()) {
          senderIsReachable = neighborIt->second.isBidirectional || 
                             (neighborIt->second.tq > 30); // Allow if decent link quality
        }
      }
      
      if (senderIsReachable) {
        uint8_t newHopCount = m_maxTTL - batmanPacket.GetTTL() + 1;
        
        m_originators[originator].originator = originator;
        m_originators[originator].nextHop = sender;
        m_originators[originator].tq = newTQ;
        m_originators[originator].lastSeqNum = batmanPacket.GetSeqNum();
        m_originators[originator].lastUpdate = Simulator::Now();
        m_originators[originator].interface = incomingInterface;
        m_originators[originator].hopCount = newHopCount;
        
        UpdateRoute(originator, sender, incomingInterface, newHopCount);
        
        NS_LOG_INFO("Updated route to " << originator << " via " << sender 
                   << " TQ=" << (int)newTQ << " Hops=" << (int)newHopCount
                   << " (was: " << (isNewDestination ? "new" : std::to_string((int)currentTQ)) << ")");
      }
    } else if (newTQ > 0) {
      NS_LOG_DEBUG("Route to " << originator << " not updated: current_TQ=" 
                   << (int)currentTQ << " new_TQ=" << (int)newTQ);
    }
  }

  if (batmanPacket.GetTTL() > 1) {
    ForwardBatmanPacket(batmanPacket, incomingInterface);
  }
}

void
BatmanRoutingProtocol::ForwardBatmanPacket(const BatmanPacket& receivedPacket, uint32_t incomingInterface)
{
  NS_LOG_FUNCTION(this);
  
  for (std::map<uint32_t, Ptr<Socket>>::const_iterator iter = m_socketAddresses.begin();
       iter != m_socketAddresses.end(); ++iter) {
    
    uint32_t interface = iter->first;
    if (interface == incomingInterface) {
      continue; 
    }
    
    Ptr<Socket> socket = iter->second;
    Ipv4InterfaceAddress iface = m_ipv4->GetAddress(interface, 0);
    
    BatmanPacket forwardPacket = receivedPacket;
    forwardPacket.SetPrevSender(iface.GetLocal());
    forwardPacket.SetTTL(receivedPacket.GetTTL() - 1);
    forwardPacket.SetTQ(CalculateTQ(Ipv4Address::GetAny(), receivedPacket.GetTQ()));
    
    Ptr<Packet> packet = Create<Packet>();
    packet->AddHeader(forwardPacket);
    
    InetSocketAddress destination = InetSocketAddress(iface.GetBroadcast(), BATMAN_PORT);
    socket->SendTo(packet, 0, destination);
    
    NS_LOG_DEBUG("Forwarded Batman packet on interface " << interface);
  }
}

bool
BatmanRoutingProtocol::IsMyOwnBroadcast(const BatmanPacket& packet)
{
  for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); i++) {
    if (m_ipv4->GetAddress(i, 0).GetLocal() == packet.GetOriginator()) {
      return true;
    }
  }
  return false;
}

uint8_t
BatmanRoutingProtocol::CalculateTQ(Ipv4Address neighbor, uint8_t receivedTQ)
{
  if (receivedTQ < m_hopPenalty) {
    return 0;
  }
  
  uint8_t penalizedTQ = receivedTQ - m_hopPenalty;

  if (neighbor != Ipv4Address::GetAny()) {
    auto neighborIt = m_neighbors.find(neighbor);
    if (neighborIt != m_neighbors.end()) {

      uint32_t adjustedTQ = (static_cast<uint32_t>(penalizedTQ) * neighborIt->second.tq) / 255;
      return static_cast<uint8_t>(std::min(adjustedTQ, static_cast<uint32_t>(255)));
    }
  }
  
  return penalizedTQ;
}

void
BatmanRoutingProtocol::UpdateSlidingWindow(Ipv4Address neighbor, uint16_t seqNum)
{
  auto windowIt = m_slidingWindows.find(neighbor);
  if (windowIt == m_slidingWindows.end()) {
    m_slidingWindows[neighbor] = SlidingWindow(m_windowSize);
    windowIt = m_slidingWindows.find(neighbor);
    
    SlidingWindow& window = windowIt->second;
    for (uint32_t i = 0; i < window.windowSize; i++) {
      window.receivedPackets[i] = false;
    }
    window.receivedCount = 0;
    window.currentIndex = 0;
  }
  
  SlidingWindow& window = windowIt->second;
  
  static std::map<Ipv4Address, uint16_t> lastSeqNums;
  static std::map<Ipv4Address, bool> firstPacket;
  
  if (firstPacket.find(neighbor) == firstPacket.end()) {
    firstPacket[neighbor] = false;
    lastSeqNums[neighbor] = seqNum;
    
    if (window.receivedPackets[window.currentIndex]) {
      window.receivedCount--;
    }
    window.receivedPackets[window.currentIndex] = true;
    window.receivedCount++;
    window.currentIndex = (window.currentIndex + 1) % window.windowSize;
    
    NS_LOG_DEBUG("First packet from " << neighbor << " seqNum=" << seqNum << " TQ=" << GetSlidingWindowTQ(neighbor));
    return;
  }
  
  uint16_t expectedSeq = lastSeqNums[neighbor] + 1;
  int16_t seqDiff = static_cast<int16_t>(seqNum - expectedSeq);
  
  if (seqNum == expectedSeq || seqDiff == 0) {
    if (window.receivedPackets[window.currentIndex]) {
      window.receivedCount--;
    }
    window.receivedPackets[window.currentIndex] = true;
    window.receivedCount++;
    window.currentIndex = (window.currentIndex + 1) % window.windowSize;
    
    lastSeqNums[neighbor] = seqNum;
  }
  else if (seqDiff > 0 && seqDiff < static_cast<int16_t>(window.windowSize / 2)) {
    for (int16_t i = 0; i < seqDiff; i++) {
      if (window.receivedPackets[window.currentIndex]) {
        window.receivedCount--;
      }
      window.receivedPackets[window.currentIndex] = false;
      window.currentIndex = (window.currentIndex + 1) % window.windowSize;
    }
    
    if (window.receivedPackets[window.currentIndex]) {
      window.receivedCount--;
    }
    window.receivedPackets[window.currentIndex] = true;
    window.receivedCount++;
    window.currentIndex = (window.currentIndex + 1) % window.windowSize;
    
    lastSeqNums[neighbor] = seqNum;
  }
  else if (seqDiff <= 0) {
    NS_LOG_DEBUG("Duplicate/old packet from " << neighbor << " seqNum=" << seqNum << " expected=" << expectedSeq);
    return;
  }
  else {

    for (uint32_t i = 0; i < window.windowSize; i++) {
      window.receivedPackets[i] = false;
    }
    window.receivedCount = 1; 
    window.currentIndex = 1;
    window.receivedPackets[0] = true;
    
    lastSeqNums[neighbor] = seqNum;
  }
  
  NS_LOG_DEBUG("Updated sliding window for " << neighbor << " seqNum=" << seqNum 
               << " receivedCount=" << window.receivedCount 
               << " TQ=" << GetSlidingWindowTQ(neighbor));
}

uint8_t
BatmanRoutingProtocol::GetSlidingWindowTQ(Ipv4Address neighbor)
{
  auto windowIt = m_slidingWindows.find(neighbor);
  if (windowIt == m_slidingWindows.end()) {
    return 0;
  }
  
  const SlidingWindow& window = windowIt->second;
  
  uint32_t receivedPackets = window.receivedCount;
  uint32_t windowSize = window.windowSize;
  
  if (receivedPackets == 0) {
    return 0;
  }

  uint32_t usedSlots = 0;
  for (uint32_t i = 0; i < windowSize; i++) {
    if (window.receivedPackets[i]) {
      usedSlots++;
    }
  }

  if (usedSlots > receivedPackets) {
    usedSlots = receivedPackets;
  }
  
  uint32_t effectiveWindowSize = windowSize;
  
  if (usedSlots < windowSize / 2) {
    effectiveWindowSize = std::max(static_cast<uint32_t>(8), usedSlots * 2);
  }
  
  uint32_t tq = (receivedPackets * 255) / effectiveWindowSize;
  
  if (tq > 0 && tq < 100) {
    tq = std::min(static_cast<uint32_t>(200), tq * 2 + 50);
  }
  
  uint8_t result = static_cast<uint8_t>(std::min(tq, static_cast<uint32_t>(255)));
  
  NS_LOG_DEBUG("TQ for " << neighbor << ": received=" << receivedPackets 
               << " windowSize=" << windowSize << " effectiveSize=" << effectiveWindowSize 
               << " usedSlots=" << usedSlots << " TQ=" << (int)result);
  
  return result;
}

bool
BatmanRoutingProtocol::IsBidirectionalNeighbor(Ipv4Address neighbor)
{
  auto neighborIt = m_neighbors.find(neighbor);
  if (neighborIt == m_neighbors.end()) {
    return false;
  }
  
  if (!m_enableBidirectionalCheck) {
    return true;
  }
  
  const std::set<Ipv4Address>& theirNeighbors = neighborIt->second.bidirectionalNeighbors;
  
  for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); i++) {
    Ipv4Address myAddr = m_ipv4->GetAddress(i, 0).GetLocal();
    if (myAddr == Ipv4Address::GetLoopback()) {
      continue;
    }
    
    if (theirNeighbors.find(myAddr) != theirNeighbors.end()) {
      NS_LOG_DEBUG("Bidirectional link confirmed: " << neighbor << " announces " << myAddr);
      return true;
    }
  }
  
  Time now = Simulator::Now();
  if ((now - neighborIt->second.lastSeen) < m_bidirectionalTimeout && 
      neighborIt->second.tq > 50) {
    
    if (!theirNeighbors.empty() || 
        (now - neighborIt->second.lastSeen) < Seconds(2.0)) {
      NS_LOG_DEBUG("Bidirectional link assumed for bootstrap: " << neighbor 
                   << " TQ=" << (int)neighborIt->second.tq);
      return true;
    }
  }
  
  NS_LOG_DEBUG("No bidirectional confirmation for " << neighbor 
               << " (TQ=" << (int)neighborIt->second.tq 
               << ", lastSeen=" << (now - neighborIt->second.lastSeen).GetSeconds() << "s ago)");
  return false;
}

bool
BatmanRoutingProtocol::IsNewerSequence(uint16_t newSeq, uint16_t oldSeq)
{

  return (int16_t)(newSeq - oldSeq) > 0;
}

void
BatmanRoutingProtocol::UpdateRoute(Ipv4Address dest, Ipv4Address nextHop, uint32_t interface, uint8_t hopCount)
{
  NS_LOG_FUNCTION(this << dest << nextHop << interface << (int)hopCount);
  

  RouteEntry& entry = m_routeCache[dest];
  entry.destination = dest;
  entry.nextHop = nextHop;
  entry.interface = interface;
  entry.metric = hopCount;
  entry.lastUpdate = Simulator::Now();
  
  NS_LOG_DEBUG("Route cached: " << dest << " via " << nextHop 
               << " interface " << interface << " hops " << (int)hopCount);
}

void
BatmanRoutingProtocol::RemoveRoute(Ipv4Address dest)
{
  NS_LOG_FUNCTION(this << dest);
  
  m_routeCache.erase(dest);
  
  NS_LOG_DEBUG("Route removed from cache: " << dest);
}

void
BatmanRoutingProtocol::PurgeTimerExpire()
{
  PurgeNeighbors();
  m_purgeTimer.Schedule(m_purgeTimeout);
}

void
BatmanRoutingProtocol::PurgeNeighbors()
{
  NS_LOG_FUNCTION(this);
  
  Time now = Simulator::Now();
  
  for (auto it = m_neighbors.begin(); it != m_neighbors.end();) {
    if (now - it->second.lastSeen > m_purgeTimeout) {
      NS_LOG_DEBUG("Purging neighbor " << it->first);
      
      m_slidingWindows.erase(it->first);
      
      it = m_neighbors.erase(it);
    } else {
      ++it;
    }
  }
  
  for (auto it = m_originators.begin(); it != m_originators.end();) {
    if (now - it->second.lastUpdate > m_purgeTimeout) {
      NS_LOG_DEBUG("Purging originator " << it->first);
      
      RemoveRoute(it->first);
      
      it = m_originators.erase(it);
    } else {
      ++it;
    }
  }

  for (auto it = m_routeCache.begin(); it != m_routeCache.end();) {
    if (now - it->second.lastUpdate > m_purgeTimeout) {
      NS_LOG_DEBUG("Purging cached route " << it->first);
      it = m_routeCache.erase(it);
    } else {
      ++it;
    }
  }
}

Ptr<Ipv4Route>
BatmanRoutingProtocol::RouteOutput(Ptr<Packet> p, const Ipv4Header &header,
                                 Ptr<NetDevice> oif, Socket::SocketErrno &sockerr)
{
  NS_LOG_FUNCTION(this << header << oif);
  
  Ipv4Address destination = header.GetDestination();
  
  // handle broadcast and multicast - return NULL to let system handle
  if (destination.IsBroadcast() || destination.IsMulticast()) {
    NS_LOG_DEBUG("Broadcast/Multicast packet " << destination << " - delegating to system");
    sockerr = Socket::ERROR_NOTERROR;
    return 0;
  }
  
  // handle subnet broadcasts specifically
  for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); i++) {
    Ipv4InterfaceAddress ifaceAddr = m_ipv4->GetAddress(i, 0);
    if (destination == ifaceAddr.GetBroadcast()) {
      NS_LOG_DEBUG("Subnet broadcast " << destination << " - delegating to system");
      sockerr = Socket::ERROR_NOTERROR;
      return 0;
    }
  }
  
  auto routeIt = m_routeCache.find(destination);
  if (routeIt != m_routeCache.end()) {
    auto neighborIt = m_neighbors.find(routeIt->second.nextHop);
    if (neighborIt != m_neighbors.end() && 
        (Simulator::Now() - neighborIt->second.lastSeen) < m_purgeTimeout) {
      
      Ptr<Ipv4Route> route = Create<Ipv4Route>();
      route->SetDestination(destination);
      route->SetGateway(routeIt->second.nextHop);
      route->SetSource(m_ipv4->GetAddress(routeIt->second.interface, 0).GetLocal());
      route->SetOutputDevice(m_ipv4->GetNetDevice(routeIt->second.interface));
      sockerr = Socket::ERROR_NOTERROR;
      
      NS_LOG_DEBUG("Cached route found for " << destination << " via " << routeIt->second.nextHop);
      return route;
    } else {
      m_routeCache.erase(routeIt);
    }
  }

  auto originatorIt = m_originators.find(destination);
  if (originatorIt != m_originators.end()) {
    Ptr<Ipv4Route> route = Create<Ipv4Route>();
    route->SetDestination(destination);
    route->SetGateway(originatorIt->second.nextHop);
    route->SetSource(m_ipv4->GetAddress(originatorIt->second.interface, 0).GetLocal());
    route->SetOutputDevice(m_ipv4->GetNetDevice(originatorIt->second.interface));
    sockerr = Socket::ERROR_NOTERROR;

    UpdateRoute(destination, originatorIt->second.nextHop, 
               originatorIt->second.interface, originatorIt->second.hopCount);
    
    NS_LOG_DEBUG("Originator route found for " << destination << " via " << originatorIt->second.nextHop);
    return route;
  }
  
  auto neighborIt = m_neighbors.find(destination);
  if (neighborIt != m_neighbors.end() && neighborIt->second.isBidirectional) {
    Ptr<Ipv4Route> route = Create<Ipv4Route>();
    route->SetDestination(destination);
    route->SetGateway(destination); 
    route->SetSource(m_ipv4->GetAddress(neighborIt->second.interface, 0).GetLocal());
    route->SetOutputDevice(m_ipv4->GetNetDevice(neighborIt->second.interface));
    sockerr = Socket::ERROR_NOTERROR;

    UpdateRoute(destination, destination, neighborIt->second.interface, 1);
    
    NS_LOG_DEBUG("Direct route found for neighbor " << destination);
    return route;
  }
  
  NS_LOG_DEBUG("No route found for " << destination);
  sockerr = Socket::ERROR_NOROUTETOHOST;
  return 0;
}

bool
BatmanRoutingProtocol::RouteInput(Ptr<const Packet> p, const Ipv4Header &header,
                                Ptr<const NetDevice> idev, UnicastForwardCallback ucb,
                                MulticastForwardCallback mcb, LocalDeliverCallback lcb,
                                ErrorCallback ecb)
{
  NS_LOG_FUNCTION(this << p << header);
  
  Ipv4Address destination = header.GetDestination();
  uint32_t inputInterface = m_ipv4->GetInterfaceForDevice(idev);

  for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); i++) {
    if (m_ipv4->GetAddress(i, 0).GetLocal() == destination ||
        m_ipv4->GetAddress(i, 0).GetBroadcast() == destination) {
      NS_LOG_DEBUG("Local delivery for " << destination);
      lcb(p, header, inputInterface);
      return true;
    }
  }
  
  if (destination.IsMulticast()) {
    NS_LOG_DEBUG("Multicast packet");
    Ptr<Ipv4MulticastRoute> mrtentry = Create<Ipv4MulticastRoute>();
    mrtentry->SetGroup(destination);
    mrtentry->SetOrigin(header.GetSource());
    mrtentry->SetParent(inputInterface);
    
    for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); i++) {
      if (i != inputInterface && !IsInterfaceExcluded(i)) {
        mrtentry->SetOutputTtl(i, 1);
      }
    }
    
    mcb(mrtentry, p, header);
    return true;
  }
  
  if (destination.IsBroadcast()) {
    NS_LOG_DEBUG("Broadcast packet");
    lcb(p, header, inputInterface);
    return true;
  }
  
  auto routeIt = m_routeCache.find(destination);
  if (routeIt != m_routeCache.end()) {
    Ptr<Ipv4Route> route = Create<Ipv4Route>();
    route->SetDestination(destination);
    route->SetGateway(routeIt->second.nextHop);
    route->SetSource(header.GetSource());
    route->SetOutputDevice(m_ipv4->GetNetDevice(routeIt->second.interface));
    
    NS_LOG_DEBUG("Forwarding packet via cached route to " << destination << " via " << routeIt->second.nextHop);
    ucb(route, p, header);
    return true;
  }
  
  auto originatorIt = m_originators.find(destination);
  if (originatorIt != m_originators.end()) {
    Ptr<Ipv4Route> route = Create<Ipv4Route>();
    route->SetDestination(destination);
    route->SetGateway(originatorIt->second.nextHop);
    route->SetSource(header.GetSource());
    route->SetOutputDevice(m_ipv4->GetNetDevice(originatorIt->second.interface));
    
    NS_LOG_DEBUG("Forwarding packet to " << destination << " via " << originatorIt->second.nextHop);
    ucb(route, p, header);
    return true;
  }
  
  auto neighborIt = m_neighbors.find(destination);
  if (neighborIt != m_neighbors.end() && neighborIt->second.isBidirectional) {
    Ptr<Ipv4Route> route = Create<Ipv4Route>();
    route->SetDestination(destination);
    route->SetGateway(destination);
    route->SetSource(header.GetSource());
    route->SetOutputDevice(m_ipv4->GetNetDevice(neighborIt->second.interface));
    
    NS_LOG_DEBUG("Forwarding packet to direct neighbor " << destination);
    ucb(route, p, header);
    return true;
  }
  
  NS_LOG_DEBUG("No route found for forwarding " << destination);
  return false;
}

void
BatmanRoutingProtocol::NotifyInterfaceUp(uint32_t interface)
{
  NS_LOG_FUNCTION(this << interface);
  
  if (IsInterfaceExcluded(interface)) {
    NS_LOG_INFO("Interface " << interface << " is excluded");
    return;
  }
  
  if (m_socketAddresses.find(interface) == m_socketAddresses.end()) {
    Ipv4InterfaceAddress ifaceAddr = m_ipv4->GetAddress(interface, 0);
    if (ifaceAddr.GetLocal() == Ipv4Address("0.0.0.0")) {
      NS_LOG_WARN("Interface " << interface << " has no valid IP address, skipping");
      return;
    }
    
    Ptr<Socket> socket = Socket::CreateSocket(GetObject<Node>(),
                                            UdpSocketFactory::GetTypeId());
    
    socket->BindToNetDevice(m_ipv4->GetNetDevice(interface));
    
    InetSocketAddress local = InetSocketAddress(ifaceAddr.GetLocal(), BATMAN_PORT);
    if (socket->Bind(local) == -1) {
      NS_LOG_ERROR("Failed to bind Batman socket on interface " << interface);
      return;
    }
    
    socket->SetAllowBroadcast(true);
    socket->SetAttribute("IpTtl", UintegerValue(1));
    socket->SetRecvCallback(MakeCallback(&BatmanRoutingProtocol::RecvBatman, this));
    m_socketAddresses[interface] = socket;
    
    NS_LOG_INFO("Created Batman socket for interface " << interface);
  }
}

void
BatmanRoutingProtocol::NotifyInterfaceDown(uint32_t interface)
{
  NS_LOG_FUNCTION(this << interface);
  
  auto socketIt = m_socketAddresses.find(interface);
  if (socketIt != m_socketAddresses.end()) {
    socketIt->second->Close();
    m_socketAddresses.erase(socketIt);
    NS_LOG_INFO("Closed Batman socket for interface " << interface);
  }
  
  for (auto it = m_neighbors.begin(); it != m_neighbors.end();) {
    if (it->second.interface == interface) {
      NS_LOG_DEBUG("Removing neighbor " << it->first << " due to interface down");
      m_slidingWindows.erase(it->first);
      it = m_neighbors.erase(it);
    } else {
      ++it;
    }
  }
  
  for (auto it = m_originators.begin(); it != m_originators.end();) {
    if (it->second.interface == interface) {
      NS_LOG_DEBUG("Removing originator " << it->first << " due to interface down");
      RemoveRoute(it->first);
      it = m_originators.erase(it);
    } else {
      ++it;
    }
  }
  
  for (auto it = m_routeCache.begin(); it != m_routeCache.end();) {
    if (it->second.interface == interface) {
      NS_LOG_DEBUG("Removing cached route " << it->first << " due to interface down");
      it = m_routeCache.erase(it);
    } else {
      ++it;
    }
  }
}

void
BatmanRoutingProtocol::NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address)
{
  NS_LOG_FUNCTION(this << interface << address);
}

void
BatmanRoutingProtocol::NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address)
{
  NS_LOG_FUNCTION(this << interface << address);

}




//------------------------------------------------------------------------------------------------------------------------------
// void
// BatmanRoutingProtocol::PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
// {
//   NS_LOG_FUNCTION(this << stream);
  
//   *stream->GetStream() << "Batman Routing Table at " << Simulator::Now().As(unit) << ":\n";
//   *stream->GetStream() << std::left << std::setw(15) << "Destination" 
//                       << std::setw(15) << "Next Hop" 
//                       << std::setw(12) << "Metric(TQ)" 
//                       << std::setw(15) << "Bidirectional"
//                       << std::setw(8) << "Hops"
//                       << "Type\n";
//   *stream->GetStream() << "--------------------------------------------------------------------------------\n";
  
//   // Create a map to collect all routing information
//   std::map<Ipv4Address, RouteInfo> allRoutes;
  
//   // Add routes from originators table (both direct and multi-hop)
//   for (const auto& entry : m_originators) {
//     RouteInfo info;
//     info.destination = entry.first;
//     info.nextHop = entry.second.nextHop;
//     info.tq = entry.second.tq;
//     info.hopCount = entry.second.hopCount;
//     info.isFromOriginators = true;
    
//     // Determine bidirectional status
//     if (entry.first == entry.second.nextHop) {
//       // Direct route - check if destination is bidirectional
//       auto neighborIt = m_neighbors.find(entry.first);
//       if (neighborIt != m_neighbors.end()) {
//         info.isBidirectional = neighborIt->second.isBidirectional;
//         // Use the actual neighbor TQ if available (more accurate)
//         if (neighborIt->second.tq > 0) {
//           info.tq = neighborIt->second.tq;
//         }
//       } else {
//         info.isBidirectional = false;
//       }
//       info.routeType = "Direct";
//     } else {
//       // Multi-hop route - check if next hop is bidirectional
//       auto neighborIt = m_neighbors.find(entry.second.nextHop);
//       if (neighborIt != m_neighbors.end()) {
//         info.isBidirectional = neighborIt->second.isBidirectional;
//       } else {
//         info.isBidirectional = false;
//       }
//       info.routeType = "Multi-hop";
//     }
    
//     allRoutes[entry.first] = info;
//   }
  
//   // Add direct neighbors that are not in originators table
//   for (const auto& neighbor : m_neighbors) {
//     if (allRoutes.find(neighbor.first) == allRoutes.end()) {
//       RouteInfo info;
//       info.destination = neighbor.first;
//       info.nextHop = neighbor.first; // Direct neighbor
//       info.tq = neighbor.second.tq;
//       info.isBidirectional = neighbor.second.isBidirectional;
//       info.hopCount = 1;
//       info.isFromOriginators = false;
//       info.routeType = "Direct";
      
//       allRoutes[neighbor.first] = info;
//     }
//   }
  
//   // Print all routes sorted by destination IP
//   for (const auto& route : allRoutes) {
//     const RouteInfo& info = route.second;
    
//     *stream->GetStream() << std::left << std::setw(15) << info.destination
//                         << std::setw(15) << info.nextHop
//                         << std::setw(12) << (int)info.tq
//                         << std::setw(15) << (info.isBidirectional ? "Yes" : "No")
//                         << std::setw(8) << (int)info.hopCount
//                         << info.routeType << "\n";
//   }
  
//   // Print summary statistics
//   int totalRoutes = allRoutes.size();
//   int bidirectionalRoutes = 0;
//   int directRoutes = 0;
//   int multihopRoutes = 0;
//   int validTQRoutes = 0;
  
//   for (const auto& route : allRoutes) {
//     const RouteInfo& info = route.second;
//     if (info.isBidirectional) bidirectionalRoutes++;
//     if (info.routeType == "Direct") directRoutes++;
//     if (info.routeType == "Multi-hop") multihopRoutes++;
//     if (info.tq > 0) validTQRoutes++;
//   }
  
//   *stream->GetStream() << "--------------------------------------------------------------------------------\n";
//   *stream->GetStream() << "Summary: " << totalRoutes << " total routes ("
//                       << directRoutes << " direct, " << multihopRoutes << " multi-hop), "
//                       << bidirectionalRoutes << " bidirectional, "
//                       << validTQRoutes << " with valid TQ\n\n";
// }
//------------------------------------------------------------------------------------------------------------------------------

// Updated routing table print function that has the corrected format 



void
BatmanRoutingProtocol::PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
{
    NS_LOG_FUNCTION(this << stream);

    *stream->GetStream() << "Batman Routing Table at " << Simulator::Now().As(unit) << ":\n";
    *stream->GetStream() << std::left 
                         << std::setw(15) << "Destination" 
                         << std::setw(18) << "Next Hop" 
                         << std::setw(12) << "Metric(TQ)" 
                         << std::setw(15) << "Bidirectional" 
                         << std::setw(6)  << "Hops" 
                         << "Type\n";
    *stream->GetStream() << "--------------------------------------------------------------------------------\n";

    std::map<Ipv4Address, RouteInfo> allRoutes;

    for (const auto& entry : m_originators) {
        RouteInfo info;
        info.destination = entry.first;
        info.nextHop = entry.second.nextHop;
        info.tq = entry.second.tq;
        info.hopCount = entry.second.hopCount;
        info.isFromOriginators = true;

        if (entry.first == entry.second.nextHop) {
            auto neighborIt = m_neighbors.find(entry.first);
            if (neighborIt != m_neighbors.end()) {
                info.isBidirectional = neighborIt->second.isBidirectional;
                if (neighborIt->second.tq > 0) {
                    info.tq = neighborIt->second.tq;
                }
            } else {
                info.isBidirectional = false;
            }
            info.routeType = "Direct";
        } else {
            auto neighborIt = m_neighbors.find(entry.second.nextHop);
            if (neighborIt != m_neighbors.end()) {
                info.isBidirectional = neighborIt->second.isBidirectional;
            } else {
                info.isBidirectional = false;
            }
            info.routeType = "Multi-hop";
        }

        allRoutes[entry.first] = info;
    }


    for (const auto& neighbor : m_neighbors) {
        if (allRoutes.find(neighbor.first) == allRoutes.end()) {
            RouteInfo info;
            info.destination = neighbor.first;
            info.nextHop = neighbor.first;
            info.tq = neighbor.second.tq;
            info.isBidirectional = neighbor.second.isBidirectional;
            info.hopCount = 1;
            info.isFromOriginators = false;
            info.routeType = "Direct";

            allRoutes[neighbor.first] = info;
        }
    }


    for (const auto& route : allRoutes) {
        const RouteInfo& info = route.second;

        *stream->GetStream() << std::left 
                             << info.destination << "\t"
                             << "\t" << info.nextHop << "\t" << "\t" << "\t"
                             << std::setw(12) << (int)info.tq
                             << std::setw(15) << (info.isBidirectional ? "Yes" : "No")
                             << std::setw(6)  << (int)info.hopCount
                             << info.routeType << "\n";
    }

    // print summary statistics
    int totalRoutes = allRoutes.size();
    int bidirectionalRoutes = 0;
    int directRoutes = 0;
    int multihopRoutes = 0;
    int validTQRoutes = 0;

    for (const auto& route : allRoutes) {
        const RouteInfo& info = route.second;
        if (info.isBidirectional) bidirectionalRoutes++;
        if (info.routeType == "Direct") directRoutes++;
        if (info.routeType == "Multi-hop") multihopRoutes++;
        if (info.tq > 0) validTQRoutes++;
    }

    *stream->GetStream() << "--------------------------------------------------------------------------------\n";
    *stream->GetStream() << "Summary: " << totalRoutes << " total routes ("
                         << directRoutes << " direct, " << multihopRoutes << " multi-hop), "
                         << bidirectionalRoutes << " bidirectional, "
                         << validTQRoutes << " with valid TQ\n\n";
}



uint32_t
BatmanRoutingProtocol::GetInterfaceForSocket(Ptr<Socket> socket) const
{
  for (const auto& pair : m_socketAddresses) {
    if (pair.second == socket) {
      return pair.first;
    }
  }
  return std::numeric_limits<uint32_t>::max();
}

bool
BatmanRoutingProtocol::IsInterfaceExcluded(uint32_t interface) const
{
  return m_interfaceExclusions.find(interface) != m_interfaceExclusions.end();
}

Ptr<Socket>
BatmanRoutingProtocol::FindSocketWithInterfaceAddress(Ipv4InterfaceAddress addr) const
{
  for (const auto& pair : m_socketAddresses) {
    if (m_ipv4->GetAddress(pair.first, 0) == addr) {
      return pair.second;
    }
  }
  return 0;
}

int64_t
BatmanRoutingProtocol::AssignStreams(int64_t stream)
{
  NS_LOG_FUNCTION(this << stream);
  m_uniformRandomVariable->SetStream(stream);
  return 1;
}

} // namespace ns3