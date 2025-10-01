#include "ns3/batman-helper.h"
#include "ns3/batman-routing-protocol.h"
#include "ns3/batman-routing-protocol.h"
#include "ns3/node.h"
#include "ns3/node-list.h"
#include "ns3/names.h"
#include "ns3/ipv4-list-routing.h"
#include "ns3/log.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("BatmanHelper");

BatmanHelper::BatmanHelper()
  : Ipv4RoutingHelper()
{
  m_agentFactory.SetTypeId("ns3::BatmanRoutingProtocol");
}

BatmanHelper::~BatmanHelper()
{
  m_interfaceExclusions.clear();
}

BatmanHelper*
BatmanHelper::Copy() const
{
  return new BatmanHelper(*this);
}

void
BatmanHelper::ExcludeInterface(Ptr<Node> node, uint32_t interface)
{
  NS_LOG_FUNCTION(this << node << interface);
  
  std::map<Ptr<Node>, std::set<uint32_t>>::iterator it = m_interfaceExclusions.find(node);
  
  if (it == m_interfaceExclusions.end())
  {
    std::set<uint32_t> interfaces;
    interfaces.insert(interface);
    
    m_interfaceExclusions.insert(std::make_pair(node, std::set<uint32_t>(interfaces)));
  }
  else
  {
    it->second.insert(interface);
  }
}

Ptr<Ipv4RoutingProtocol>
BatmanHelper::Create(Ptr<Node> node) const
{
  NS_LOG_FUNCTION(this << node);
  
  Ptr<BatmanRoutingProtocol> agent = m_agentFactory.Create<BatmanRoutingProtocol>();
  
  std::map<Ptr<Node>, std::set<uint32_t>>::const_iterator it = m_interfaceExclusions.find(node);
  
  if (it != m_interfaceExclusions.end())
  {
    // Handle interface exclusions if needed
    // For now, we'll just log them
    for (std::set<uint32_t>::const_iterator i = it->second.begin(); i != it->second.end(); ++i)
    {
      NS_LOG_INFO("Excluding interface " << *i << " on node " << node->GetId());
    }
  }
  
  node->AggregateObject(agent);
  return agent;
}

void
BatmanHelper::Set(std::string name, const AttributeValue &value)
{
  m_agentFactory.Set(name, value);
}

void
BatmanHelper::PrintRoutingTableAt(Time time, Ptr<Node> node, Ptr<OutputStreamWrapper> stream,
                                 Time::Unit unit) const
{
  Simulator::Schedule(time, &BatmanHelper::PrintTable, this, node, stream, unit);
}

void
BatmanHelper::PrintRoutingTableAllAt(Time time, Ptr<OutputStreamWrapper> stream,
                                    Time::Unit unit) const
{
  Simulator::Schedule(time, &BatmanHelper::PrintTableAllAt, this, stream, unit);
}

void
BatmanHelper::PrintRoutingTableEveryAt(Time printInterval, Ptr<Node> node,
                                      Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
{
  Simulator::Schedule(printInterval, &BatmanHelper::PrintTable, this, node, stream, unit);
  Simulator::Schedule(printInterval + printInterval,
                     &BatmanHelper::PrintRoutingTableEveryAt, this,
                     printInterval, node, stream, unit);
}

void
BatmanHelper::PrintRoutingTableAllEveryAt(Time printInterval, Ptr<OutputStreamWrapper> stream,
                                         Time::Unit unit) const
{
  Simulator::Schedule(printInterval, &BatmanHelper::PrintTableAllAt, this, stream, unit);
  Simulator::Schedule(printInterval + printInterval,
                     &BatmanHelper::PrintRoutingTableAllEveryAt, this,
                     printInterval, stream, unit);
}

void
BatmanHelper::PrintTable(Ptr<Node> node, Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
{
  Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
  if (ipv4)
  {
    Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol();
    NS_ASSERT(rp);
    
    if (DynamicCast<Ipv4ListRouting>(rp))
    {
      Ptr<Ipv4ListRouting> lr = DynamicCast<Ipv4ListRouting>(rp);
      int16_t priority;
      Ptr<Ipv4RoutingProtocol> temp = lr->GetRoutingProtocol(0, priority);
      
      if (DynamicCast<BatmanRoutingProtocol>(temp))
      {
        Ptr<BatmanRoutingProtocol> batman = DynamicCast<BatmanRoutingProtocol>(temp);
        batman->PrintRoutingTable(stream, unit);
      }
    }
    else if (DynamicCast<BatmanRoutingProtocol>(rp))
    {
      Ptr<BatmanRoutingProtocol> batman = DynamicCast<BatmanRoutingProtocol>(rp);
      batman->PrintRoutingTable(stream, unit);
    }
  }
}

void
BatmanHelper::PrintTableAllAt(Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
{
  for (uint32_t i = 0; i < NodeList::GetNNodes(); i++)
  {
    Ptr<Node> node = NodeList::GetNode(i);
    PrintTable(node, stream, unit);
  }
}

int64_t
BatmanHelper::AssignStreams(NodeContainer c, int64_t stream)
{
  int64_t currentStream = stream;
  Ptr<Node> node;
  
  for (NodeContainer::Iterator i = c.Begin(); i != c.End(); ++i)
  {
    node = (*i);
    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    NS_ASSERT_MSG(ipv4, "Ipv4 not installed on node");
    
    Ptr<Ipv4RoutingProtocol> proto = ipv4->GetRoutingProtocol();
    NS_ASSERT_MSG(proto, "Ipv4 routing not installed on node");
    
    Ptr<BatmanRoutingProtocol> batman = DynamicCast<BatmanRoutingProtocol>(proto);
    if (batman)
    {
      currentStream += batman->AssignStreams(currentStream);
      continue;
    }
    
    // Batman may also be in a list
    Ptr<Ipv4ListRouting> list = DynamicCast<Ipv4ListRouting>(proto);
    if (list)
    {
      int16_t priority;
      Ptr<Ipv4RoutingProtocol> listProto;
      Ptr<BatmanRoutingProtocol> listBatman;
      
      for (uint32_t i = 0; i < list->GetNRoutingProtocols(); i++)
      {
        listProto = list->GetRoutingProtocol(i, priority);
        listBatman = DynamicCast<BatmanRoutingProtocol>(listProto);
        if (listBatman)
        {
          currentStream += listBatman->AssignStreams(currentStream);
          break;
        }
      }
    }
  }
  
  return (currentStream - stream);
}

} // namespace ns3
