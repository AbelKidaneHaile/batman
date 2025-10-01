#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include "ns3/batman-helper.h"
#include "ns3/batman-routing-protocol.h"
//netanim for visualization
#include "ns3/netanim-module.h" 

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("BatmanSimulation");

int main(int argc, char *argv[])
{
  // simulation parameters
  uint32_t nNodes = 10;
  double duration = 100.0;
  std::string phyMode("DsssRate1Mbps");
  
  CommandLine cmd;
  cmd.AddValue("nNodes", "Number of nodes", nNodes);
  cmd.AddValue("duration", "Simulation duration", duration);
  cmd.AddValue("phyMode", "WiFi PHY mode", phyMode);
  cmd.Parse(argc, argv);
  
  // logging
  LogComponentEnable("BatmanRoutingProtocol", LOG_LEVEL_INFO);
  LogComponentEnable("BatmanSimulation", LOG_LEVEL_INFO);
  
  NS_LOG_INFO("Creating " << nNodes << " nodes");
  
  // nodes
  NodeContainer nodes;
  nodes.Create(nNodes);
  
  // configure WiFi
  WifiHelper wifi;
  wifi.SetStandard(WIFI_PHY_STANDARD_80211b);
  
  YansWifiPhyHelper wifiPhy = YansWifiPhyHelper::Default();
  YansWifiChannelHelper wifiChannel;
  wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
  wifiChannel.AddPropagationLoss("ns3::FriisPropagationLossModel");
  wifiPhy.SetChannel(wifiChannel.Create());
  
  WifiMacHelper wifiMac;
  wifiMac.SetType("ns3::AdhocWifiMac");
  
  NetDeviceContainer devices = wifi.Install(wifiPhy, wifiMac, nodes);
  
  // configure mobility
  MobilityHelper mobility;
  mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                               "MinX", DoubleValue(0.0),
                               "MinY", DoubleValue(0.0),
                               "DeltaX", DoubleValue(100.0),
                               "DeltaY", DoubleValue(100.0),
                               "GridWidth", UintegerValue(5),
                               "LayoutType", StringValue("RowFirst"));
  
  mobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                           "Bounds", RectangleValue(Rectangle(-500, 500, -500, 500)),
                           "Speed", StringValue("ns3::ConstantRandomVariable[Constant=10.0]"));
  mobility.Install(nodes);
  
  // install Internet stack with batman routing
  InternetStackHelper stack;
  BatmanHelper batman;
  stack.SetRoutingHelper(batman);
  stack.Install(nodes);
  
  // assign IP addresses
  Ipv4AddressHelper address;
  address.SetBase("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = address.Assign(devices);
 
  uint16_t port = 9;

  UdpEchoServerHelper echoServer(port);
  ApplicationContainer serverApps = echoServer.Install(nodes.Get(nNodes - 1));
  serverApps.Start(Seconds(1.0));
  serverApps.Stop(Seconds(duration));

  UdpEchoClientHelper echoClient(interfaces.GetAddress(nNodes - 1), port);
  echoClient.SetAttribute("MaxPackets", UintegerValue(100));
  echoClient.SetAttribute("Interval", TimeValue(Seconds(1.0)));
  echoClient.SetAttribute("PacketSize", UintegerValue(1024));
  
  ApplicationContainer clientApps = echoClient.Install(nodes.Get(0));
  clientApps.Start(Seconds(2.0));
  clientApps.Stop(Seconds(duration));
  
  // tracing
  AsciiTraceHelper ascii;
  wifiPhy.EnableAsciiAll(ascii.CreateFileStream("/home/ns3dce/dce-linux-dev/source/ns-3-dev/src/batman/trace/batman-simulation.tr"));
  wifiPhy.EnablePcapAll("/home/ns3dce/dce-linux-dev/source/ns-3-dev/src/batman/pcap/batman-simulation", false);
  
  Ptr<OutputStreamWrapper> routingStream = Create<OutputStreamWrapper>("/home/ns3dce/dce-linux-dev/source/ns-3-dev/src/batman/routing/batman-routing.txt", std::ios::out);
  batman.PrintRoutingTableAllAt(Seconds(10), routingStream);
  
  AnimationInterface anim("/home/ns3dce/dce-linux-dev/source/ns-3-dev/src/batman/batman-animation.xml"); //abel netanim 
  for (uint32_t i = 0; i < nodes.GetN(); ++i)
  {
      anim.UpdateNodeDescription(nodes.Get(i), "Node-" + std::to_string(i)); // label
      anim.UpdateNodeColor(nodes.Get(i), 0, 255, 0); // green
  }
  NS_LOG_INFO("Starting simulation for " << duration << " seconds");
  
  Simulator::Stop(Seconds(duration));
  Simulator::Run();
  Simulator::Destroy();
  
  NS_LOG_INFO("Simulation completed");
  
  return 0;
}