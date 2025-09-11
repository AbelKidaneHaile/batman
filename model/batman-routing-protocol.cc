#include "ns3/batman-routing-protocol.h"
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

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("BatmanRoutingProtocol");
NS_OBJECT_ENSURE_REGISTERED(BatmanRoutingProtocol);

const uint32_t BATMAN_PORT = 4305;

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
  
  // Initialize timers
  m_helloTimer.SetFunction(&BatmanRoutingProtocol::HelloTimerExpire, this);
  m_purgeTimer.SetFunction(&BatmanRoutingProtocol::PurgeTimerExpire, this);
  
  Simulator::ScheduleNow(&BatmanRoutingProtocol::Start, this);
}

void
BatmanRoutingProtocol::Start()
{
  NS_LOG_FUNCTION(this);
  
  // Create sockets for all interfaces except excluded ones
  for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); i++) {
    if (m_ipv4->GetAddress(i, 0).GetLocal() == Ipv4Address::GetLoopback()) {
      continue;
    }
    
    if (IsInterfaceExcluded(i)) {
      NS_LOG_INFO("Interface " << i << " excluded from Batman operation");
      continue;
    }
    
    Ptr<Socket> socket = Socket::CreateSocket(GetObject<Node>(),
                                            UdpSocketFactory::GetTypeId());
    InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), BATMAN_PORT);
    socket->Bind(local);
    socket->BindToNetDevice(m_ipv4->GetNetDevice(i));
    socket->SetAllowBroadcast(true);
    socket->SetRecvCallback(MakeCallback(&BatmanRoutingProtocol::RecvBatman, this));
    m_socketAddresses[i] = socket;
    
    NS_LOG_INFO("Batman socket created for interface " << i);
  }
  
  // Start periodic broadcasts with jitter
  Time jitter = Time(MilliSeconds(m_uniformRandomVariable->GetInteger(0, 100)));
  m_helloTimer.Schedule(jitter);
  m_purgeTimer.Schedule(m_purgeTimeout);
  
  NS_LOG_INFO("Batman protocol started");
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
  
  for (std::map<uint32_t, Ptr<Socket>>::const_iterator iter = m_socketAddresses.begin();
       iter != m_socketAddresses.end(); ++iter) {
    
    uint32_t interface = iter->first;
    Ptr<Socket> socket = iter->second;
    
    Ipv4InterfaceAddress iface = m_ipv4->GetAddress(interface, 0);
    
    BatmanPacket batmanPacket;
    batmanPacket.SetOriginator(iface.GetLocal());
    batmanPacket.SetPrevSender(iface.GetLocal());
    batmanPacket.SetTQ(255); // Maximum quality for own packets
    batmanPacket.SetSeqNum(++m_seqNum);
    batmanPacket.SetTTL(m_maxTTL);
    
    Ptr<Packet> packet = Create<Packet>();
    packet->AddHeader(batmanPacket);
    
    InetSocketAddress destination = InetSocketAddress(iface.GetBroadcast(), BATMAN_PORT);
    socket->SendTo(packet, 0, destination);
    
    NS_LOG_DEBUG("Sent Batman packet: Orig=" << iface.GetLocal() 
                 << " SeqNum=" << batmanPacket.GetSeqNum() 
                 << " Interface=" << interface);
  }
}

void
BatmanRoutingProtocol::RecvBatman(Ptr<Socket> socket)
{
  NS_LOG_FUNCTION(this << socket);
  
  Ptr<Packet> receivedPacket;
  Address sourceAddress;
  receivedPacket = socket->RecvFrom(sourceAddress);
  
  InetSocketAddress inetSourceAddr = InetSocketAddress::ConvertFrom(sourceAddress);
  Ipv4Address sender = inetSourceAddr.GetIpv4();
  
  BatmanPacket batmanPacket;
  receivedPacket->RemoveHeader(batmanPacket);
  
  NS_LOG_DEBUG("Received Batman packet from " << sender 
               << " Orig=" << batmanPacket.GetOriginator()
               << " SeqNum=" << batmanPacket.GetSeqNum()
               << " TQ=" << (int)batmanPacket.GetTQ()
               << " TTL=" << (int)batmanPacket.GetTTL());
  
  // Ignore our own broadcasts
  if (IsMyOwnBroadcast(batmanPacket)) {
    NS_LOG_DEBUG("Ignoring own broadcast");
    return;
  }
  
  // Find the interface this packet was received on
  uint32_t incomingInterface = GetInterfaceForSocket(socket);
  if (incomingInterface == std::numeric_limits<uint32_t>::max()) {
    NS_LOG_ERROR("Could not determine incoming interface");
    return;
  }
  
  // Update neighbor information (direct neighbors only)
  if (batmanPacket.GetPrevSender() == sender) {
    UpdateSlidingWindow(sender, batmanPacket.GetSeqNum());
    
    NeighborInfo& neighbor = m_neighbors[sender];
    neighbor.neighbor = sender;
    neighbor.lastSeen = Simulator::Now();
    neighbor.lastSeqNum = batmanPacket.GetSeqNum();
    neighbor.interface = incomingInterface;
    neighbor.tq = GetSlidingWindowTQ(sender);
    
    if (m_enableBidirectionalCheck) {
      // Check for bidirectionality
      neighbor.isBidirectional = IsBidirectionalNeighbor(sender);
      neighbor.bidirectionalTimeout = Simulator::Now() + m_bidirectionalTimeout;
    } else {
      neighbor.isBidirectional = true;
    }
    
    NS_LOG_DEBUG("Updated neighbor " << sender << " TQ=" << (int)neighbor.tq 
                 << " Bidirectional=" << neighbor.isBidirectional);
  }
  
  // Process originator information
  Ipv4Address originator = batmanPacket.GetOriginator();
  
  // Skip if this is about ourselves
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
  
  // Check if this is a newer sequence number
  bool isNewer = (m_originators.find(originator) == m_originators.end()) ||
                 IsNewerSequence(batmanPacket.GetSeqNum(), m_originators[originator].lastSeqNum);
  
  if (isNewer) {
    uint8_t newTQ = CalculateTQ(sender, batmanPacket.GetTQ());
    
    // Only update if TQ is better or this is the first entry
    bool shouldUpdate = (m_originators.find(originator) == m_originators.end()) ||
                       (newTQ > m_originators[originator].tq) ||
                       IsNewerSequence(batmanPacket.GetSeqNum(), m_originators[originator].lastSeqNum);
    
    if (shouldUpdate && newTQ > 0) {
      // Check if sender is a bidirectional neighbor for direct routes
      bool senderIsBidirectional = true;
      if (m_enableBidirectionalCheck && sender == batmanPacket.GetPrevSender()) {
        auto neighborIt = m_neighbors.find(sender);
        if (neighborIt != m_neighbors.end()) {
          senderIsBidirectional = neighborIt->second.isBidirectional;
        }
      }
      
      if (senderIsBidirectional) {
        m_originators[originator].originator = originator;
        m_originators[originator].nextHop = sender;
        m_originators[originator].tq = newTQ;
        m_originators[originator].lastSeqNum = batmanPacket.GetSeqNum();
        m_originators[originator].lastUpdate = Simulator::Now();
        m_originators[originator].interface = incomingInterface;
        m_originators[originator].hopCount = m_maxTTL - batmanPacket.GetTTL() + 1;
        
        UpdateRoute(originator, sender, incomingInterface, m_originators[originator].hopCount);
        
        NS_LOG_DEBUG("Updated originator " << originator << " via " << sender 
                     << " TQ=" << (int)newTQ << " Hops=" << (int)m_originators[originator].hopCount);
      }
    }
  }
  
  // Forward the packet if TTL > 1
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
      continue; // Don't forward back to incoming interface
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
  
  // If we have a specific neighbor, consider link quality
  if (neighbor != Ipv4Address::GetAny()) {
    auto neighborIt = m_neighbors.find(neighbor);
    if (neighborIt != m_neighbors.end()) {
      // Apply neighbor's link quality
      uint32_t adjustedTQ = (penalizedTQ * neighborIt->second.tq) / 255;
      return static_cast<uint8_t>(adjustedTQ);
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
  }
  
  SlidingWindow& window = windowIt->second;
  
  // For now, assume consecutive packets (simplified)
  // In real implementation, would handle sequence number gaps
  if (window.receivedPackets[window.currentIndex]) {
    window.receivedCount--;
  }
  
  window.receivedPackets[window.currentIndex] = true;
  window.receivedCount++;
  window.currentIndex = (window.currentIndex + 1) % window.windowSize;
}

uint8_t
BatmanRoutingProtocol::GetSlidingWindowTQ(Ipv4Address neighbor)
{
  auto windowIt = m_slidingWindows.find(neighbor);
  if (windowIt == m_slidingWindows.end()) {
    return 0;
  }
  
  const SlidingWindow& window = windowIt->second;
  if (window.receivedCount == 0) {
    return 0;
  }
  
  // Calculate TQ as percentage of received packets
  uint32_t tq = (window.receivedCount * 255) / window.windowSize;
  return static_cast<uint8_t>(std::min(tq, static_cast<uint32_t>(255)));
}

bool
BatmanRoutingProtocol::IsBidirectionalNeighbor(Ipv4Address neighbor)
{
  // Simple bidirectional check - in real implementation would
  // check if neighbor announces us in its originator messages
  auto neighborIt = m_neighbors.find(neighbor);
  if (neighborIt == m_neighbors.end()) {
    return false;
  }
  
  // For simplification, consider link bidirectional if we've heard from
  // neighbor recently and TQ is above threshold
  Time now = Simulator::Now();
  return (now - neighborIt->second.lastSeen < m_bidirectionalTimeout) && 
         (neighborIt->second.tq > 50); // Threshold for good link quality
}

bool
BatmanRoutingProtocol::IsNewerSequence(uint16_t newSeq, uint16_t oldSeq)
{
  // Handle sequence number wraparound
  return (int16_t)(newSeq - oldSeq) > 0;
}

void
BatmanRoutingProtocol::UpdateRoute(Ipv4Address dest, Ipv4Address nextHop, uint32_t interface, uint8_t hopCount)
{
  NS_LOG_FUNCTION(this << dest << nextHop << interface << (int)hopCount);
  
  // This is a simplified route update - in a real implementation,
  // you would integrate with ns-3's routing table more thoroughly
  Ptr<Ipv4StaticRouting> staticRouting = CreateObject<Ipv4StaticRouting>();
  staticRouting->SetIpv4(m_ipv4);
  
  // Remove old route if it exists
  RemoveRoute(dest);
  
  // Add new route
  staticRouting->AddHostRouteTo(dest, nextHop, interface, hopCount);
  
  NS_LOG_DEBUG("Route updated: " << dest << " via " << nextHop 
               << " interface " << interface << " hops " << (int)hopCount);
}

void
BatmanRoutingProtocol::RemoveRoute(Ipv4Address dest)
{
  // In a real implementation, you would properly remove the route
  // from the routing table. This is simplified for demonstration.
  NS_LOG_DEBUG("Removing route to " << dest);
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
  
  // Purge old neighbors
  for (auto it = m_neighbors.begin(); it != m_neighbors.end();) {
    if (now - it->second.lastSeen > m_purgeTimeout) {
      NS_LOG_DEBUG("Purging neighbor " << it->first);
      
      // Remove sliding window
      m_slidingWindows.erase(it->first);
      
      it = m_neighbors.erase(it);
    } else {
      ++it;
    }
  }
  
  // Purge old originators
  for (auto it = m_originators.begin(); it != m_originators.end();) {
    if (now - it->second.lastUpdate > m_purgeTimeout) {
      NS_LOG_DEBUG("Purging originator " << it->first);
      
      // Remove route
      RemoveRoute(it->first);
      
      it = m_originators.erase(it);
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
  
  // Check if we have a route to the destination
  auto originatorIt = m_originators.find(destination);
  if (originatorIt != m_originators.end()) {
    Ptr<Ipv4Route> route = Create<Ipv4Route>();
    route->SetDestination(destination);
    route->SetGateway(originatorIt->second.nextHop);
    route->SetSource(m_ipv4->GetAddress(originatorIt->second.interface, 0).GetLocal());
    route->SetOutputDevice(m_ipv4->GetNetDevice(originatorIt->second.interface));
    sockerr = Socket::ERROR_NOTERROR;
    
    NS_LOG_DEBUG("Route found for " << destination << " via " << originatorIt->second.nextHop);
    return route;
  }
  
  // Check if destination is a direct neighbor
  auto neighborIt = m_neighbors.find(destination);
  if (neighborIt != m_neighbors.end() && neighborIt->second.isBidirectional) {
    Ptr<Ipv4Route> route = Create<Ipv4Route>();
    route->SetDestination(destination);
    route->SetGateway(destination); // Direct neighbor
    route->SetSource(m_ipv4->GetAddress(neighborIt->second.interface, 0).GetLocal());
    route->SetOutputDevice(m_ipv4->GetNetDevice(neighborIt->second.interface));
    sockerr = Socket::ERROR_NOTERROR;
    
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
  
  // Check if this is for local delivery
  for (uint32_t i = 0; i < m_ipv4->GetNInterfaces(); i++) {
    if (m_ipv4->GetAddress(i, 0).GetLocal() == destination ||
        m_ipv4->GetAddress(i, 0).GetBroadcast() == destination) {
      NS_LOG_DEBUG("Local delivery for " << destination);
      lcb(p, header, inputInterface);
      return true;
    }
  }
  
  // Check for multicast/broadcast
  if (destination.IsMulticast()) {
    NS_LOG_DEBUG("Multicast packet");
    // For multicast, we need to create a multicast route
    Ptr<Ipv4MulticastRoute> mrtentry = Create<Ipv4MulticastRoute>();
    mrtentry->SetGroup(destination);
    mrtentry->SetOrigin(header.GetSource());
    mrtentry->SetParent(inputInterface);
    
    // Add output interfaces (all interfaces except the input interface)
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
    // For broadcast, deliver locally 
    lcb(p, header, inputInterface);
    return true;
  }
  
  // Forward packet if route exists
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
  
  // Check direct neighbor route
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
  
  // Create socket for new interface if not already exists
  if (m_socketAddresses.find(interface) == m_socketAddresses.end()) {
    Ptr<Socket> socket = Socket::CreateSocket(GetObject<Node>(),
                                            UdpSocketFactory::GetTypeId());
    InetSocketAddress local = InetSocketAddress(Ipv4Address::GetAny(), BATMAN_PORT);
    socket->Bind(local);
    socket->BindToNetDevice(m_ipv4->GetNetDevice(interface));
    socket->SetAllowBroadcast(true);
    socket->SetRecvCallback(MakeCallback(&BatmanRoutingProtocol::RecvBatman, this));
    m_socketAddresses[interface] = socket;
    
    NS_LOG_INFO("Created Batman socket for interface " << interface);
  }
}

void
BatmanRoutingProtocol::NotifyInterfaceDown(uint32_t interface)
{
  NS_LOG_FUNCTION(this << interface);
  
  // Close socket for interface
  auto socketIt = m_socketAddresses.find(interface);
  if (socketIt != m_socketAddresses.end()) {
    socketIt->second->Close();
    m_socketAddresses.erase(socketIt);
    NS_LOG_INFO("Closed Batman socket for interface " << interface);
  }
  
  // Remove neighbors and originators using this interface
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
}

void
BatmanRoutingProtocol::NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address)
{
  NS_LOG_FUNCTION(this << interface << address);
  // Address changes might require socket recreation, but we'll keep it simple
}

void
BatmanRoutingProtocol::NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address)
{
  NS_LOG_FUNCTION(this << interface << address);
  // Address changes might require socket recreation, but we'll keep it simple
}

void
BatmanRoutingProtocol::PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
{
  NS_LOG_FUNCTION(this << stream);
  
  *stream->GetStream() << "Batman Routing Table at " << Simulator::Now().As(unit) << ":\n";
  *stream->GetStream() << "Destination\t\tNext Hop\t\tTQ\tHops\tInterface\tLast Update\n";
  *stream->GetStream() << "--------------------------------------------------------------------\n";
  
  for (const auto& entry : m_originators) {
    *stream->GetStream() << entry.first << "\t\t"
                        << entry.second.nextHop << "\t\t"
                        << (int)entry.second.tq << "\t"
                        << (int)entry.second.hopCount << "\t"
                        << entry.second.interface << "\t\t"
                        << (Simulator::Now() - entry.second.lastUpdate).As(unit) << " ago\n";
  }
  
  *stream->GetStream() << "\nDirect Neighbors:\n";
  *stream->GetStream() << "Neighbor\t\tTQ\tBidirectional\tInterface\tLast Seen\n";
  *stream->GetStream() << "--------------------------------------------------------\n";
  
  for (const auto& neighbor : m_neighbors) {
    *stream->GetStream() << neighbor.first << "\t\t"
                        << (int)neighbor.second.tq << "\t"
                        << (neighbor.second.isBidirectional ? "Yes" : "No") << "\t\t"
                        << neighbor.second.interface << "\t\t"
                        << (Simulator::Now() - neighbor.second.lastSeen).As(unit) << " ago\n";
  }
  
  *stream->GetStream() << "\n";
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
