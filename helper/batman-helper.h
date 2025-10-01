#ifndef BATMAN_HELPER_H
#define BATMAN_HELPER_H

#include "ns3/object-factory.h"
#include "ns3/node.h"
#include "ns3/node-container.h"
#include "ns3/ipv4-routing-helper.h"
#include "ns3/ipv4-list-routing.h"

namespace ns3 {

/**
 * \ingroup batman
 * \brief Helper class that adds Batman routing to nodes.
 */
class BatmanHelper : public Ipv4RoutingHelper
{
public:
  /**
   * \brief Create a Batman helper that is used to make life easier for people wanting
   * to use Batman routing
   */
  BatmanHelper();

  /**
   * \brief Destroy the Batman helper
   */
  virtual ~BatmanHelper();

  /**
   * \returns pointer to clone of this BatmanHelper
   *
   * This method is mainly for internal use by the other helpers;
   * clients are expected to free the dynamic memory allocated by this method
   */
  BatmanHelper* Copy() const;

  /**
   * \param node the node for which an exception is to be defined
   * \param interface an interface of node on which Batman is not to be installed
   *
   * This method allows the user to specify an interface on which Batman is not to be installed on
   */
  void ExcludeInterface(Ptr<Node> node, uint32_t interface);

  /**
   * \param name the name of the attribute to set
   * \param value the value of the attribute to set.
   *
   * This method controls the attributes of ns3::BatmanRoutingProtocol
   */
  void Set(std::string name, const AttributeValue &value);

  /**
   * \param stream The output stream
   * \param unit The time unit to be used in the report
   * \param node The node for which the routing table is to be printed
   * \param time The time at which the routing table is to be printed.
   * \param interface The interface for which the routing table entry is to be provided
   *
   * This method calls the PrintRoutingTable() method of the
   * BatmanRoutingProtocol stored in the Ipv4 object, for the given Ipv4 interface,
   * at the given time, if Batman is being used
   */
  void PrintRoutingTableAt(Time time, Ptr<Node> node, Ptr<OutputStreamWrapper> stream,
                          Time::Unit unit = Time::S) const;

  /**
   * \param stream The output stream
   * \param unit The time unit to be used in the report
   * \param time The time at which the routing table is to be printed.
   *
   * This method calls the PrintRoutingTable() method of the
   * BatmanRoutingProtocol stored in the Ipv4 object, for every node at the
   * given time, if Batman is being used
   */
  void PrintRoutingTableAllAt(Time time, Ptr<OutputStreamWrapper> stream,
                             Time::Unit unit = Time::S) const;

  /**
   * \param stream The output stream
   * \param unit The time unit to be used in the report
   * \param node The node for which the routing table is to be printed
   * \param time The time at which the routing table is to be printed.
   * \param interface The interface for which the routing table entry is to be provided
   *
   * This method calls the PrintRoutingTable() method of the
   * BatmanRoutingProtocol stored in the Ipv4 object, for the given Ipv4 interface,
   * at the given time, if Batman is being used
   */
  void PrintRoutingTableEveryAt(Time printInterval, Ptr<Node> node, Ptr<OutputStreamWrapper> stream,
                               Time::Unit unit = Time::S) const;

  /**
   * \param printInterval The time interval for which the routing table is to be printed.
   * \param stream The output stream
   * \param unit The time unit to be used in the report
   *
   * This method calls the PrintRoutingTable() method of the
   * BatmanRoutingProtocol stored in the Ipv4 object, for all nodes at the
   * given time interval, if Batman is being used
   */
  void PrintRoutingTableAllEveryAt(Time printInterval, Ptr<OutputStreamWrapper> stream,
                                  Time::Unit unit = Time::S) const;

  /**
   * Assign a fixed random variable stream number to the random variables
   * used by this model. Return the number of streams (possibly zero) that
   * have been assigned. The Install() method should have previously been
   * called by the user.
   *
   * \param c NetDeviceContainer of the set of net devices for which the
   *          BatmanRoutingProtocol should be modified to use a fixed stream
   * \param stream first stream index to use
   * \return the number of stream indices assigned by this helper
   */
  int64_t AssignStreams(NodeContainer c, int64_t stream);

public:
  /**
   * \brief Create an batman routing protocol and associate it to a node
   * \param node the node on which the routing protocol is running
   * \returns the created routing protocol
   */
  virtual Ptr<Ipv4RoutingProtocol> Create(Ptr<Node> node) const;

  /**
   * \brief Print routing table for a given node
   * \param node The node for which routing table has to be printed
   * \param stream The output stream
   * \param unit The time unit to be used in the report
   */
  void PrintTable(Ptr<Node> node, Ptr<OutputStreamWrapper> stream, Time::Unit unit = Time::S) const;

  /**
   * \brief Print routing table for all nodes
   * \param stream The output stream
   * \param unit The time unit to be used in the report
   */
  void PrintTableAllAt(Ptr<OutputStreamWrapper> stream, Time::Unit unit = Time::S) const;

  ObjectFactory m_agentFactory; //!< Object factory to create batman routing object
  std::map<Ptr<Node>, std::set<uint32_t>> m_interfaceExclusions; //!< Interface exclusions
};

} // namespace ns3

#endif // BATMAN_HELPER_H
