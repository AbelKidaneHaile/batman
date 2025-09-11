#include "ns3/test.h"
#include "ns3/batman-routing-protocol.h"
#include "ns3/batman-packet.h"
#include "ns3/batman-helper.h"
#include "ns3/node.h"
#include "ns3/simulator.h"

using namespace ns3;

class BatmanPacketTestCase : public TestCase
{
public:
  BatmanPacketTestCase();
  virtual ~BatmanPacketTestCase() {}

private:
  virtual void DoRun();
};

BatmanPacketTestCase::BatmanPacketTestCase()
  : TestCase("Batman packet serialization/deserialization test")
{
}

void
BatmanPacketTestCase::DoRun()
{
  BatmanPacket packet;
  packet.SetOriginator(Ipv4Address("10.1.1.1"));
  packet.SetPrevSender(Ipv4Address("10.1.1.2"));
  packet.SetTQ(200);
  packet.SetSeqNum(12345);
  packet.SetTTL(10);

  Ptr<Packet> p = Create<Packet>();
  p->AddHeader(packet);

  BatmanPacket receivedPacket;
  p->RemoveHeader(receivedPacket);

  NS_TEST_ASSERT_MSG_EQ(receivedPacket.GetOriginator(), Ipv4Address("10.1.1.1"),
                       "Originator address mismatch");
  NS_TEST_ASSERT_MSG_EQ(receivedPacket.GetPrevSender(), Ipv4Address("10.1.1.2"),
                       "Previous sender address mismatch");
  NS_TEST_ASSERT_MSG_EQ(receivedPacket.GetTQ(), 200, "TQ value mismatch");
  NS_TEST_ASSERT_MSG_EQ(receivedPacket.GetSeqNum(), 12345, "Sequence number mismatch");
  NS_TEST_ASSERT_MSG_EQ(receivedPacket.GetTTL(), 10, "TTL value mismatch");
}

class BatmanRoutingTestCase : public TestCase
{
public:
  BatmanRoutingTestCase();
  virtual ~BatmanRoutingTestCase() {}

private:
  virtual void DoRun();
};

BatmanRoutingTestCase::BatmanRoutingTestCase()
  : TestCase("Batman routing protocol basic functionality test")
{
}

void
BatmanRoutingTestCase::DoRun()
{
  Ptr<Node> node = CreateObject<Node>();
  Ptr<BatmanRoutingProtocol> batman = CreateObject<BatmanRoutingProtocol>();
  
  NS_TEST_ASSERT_MSG_NE(batman, 0, "Failed to create Batman routing protocol");
  
  // Test would require more complex setup with interfaces and sockets
  // This is a basic instantiation test
}

class BatmanHelperTestCase : public TestCase
{
public:
  BatmanHelperTestCase();
  virtual ~BatmanHelperTestCase() {}

private:
  virtual void DoRun();
};

BatmanHelperTestCase::BatmanHelperTestCase()
  : TestCase("Batman helper functionality test")
{
}

void
BatmanHelperTestCase::DoRun()
{
  BatmanHelper helper;
  Ptr<Node> node = CreateObject<Node>();
  
  Ptr<Ipv4RoutingProtocol> routing = helper.Create(node);
  NS_TEST_ASSERT_MSG_NE(routing, 0, "Failed to create routing protocol via helper");
  
  Ptr<BatmanRoutingProtocol> batman = DynamicCast<BatmanRoutingProtocol>(routing);
  NS_TEST_ASSERT_MSG_NE(batman, 0, "Created protocol is not BatmanRoutingProtocol");
}

class BatmanTestSuite : public TestSuite
{
public:
  BatmanTestSuite();
};

BatmanTestSuite::BatmanTestSuite()
  : TestSuite("batman", UNIT)
{
  AddTestCase(new BatmanPacketTestCase, TestCase::QUICK);
  AddTestCase(new BatmanRoutingTestCase, TestCase::QUICK);
  AddTestCase(new BatmanHelperTestCase, TestCase::QUICK);
}

static BatmanTestSuite g_batmanTestSuite;
