/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2019 SIGNET Lab, Department of Information Engineering, University of Padova
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors: Alvise De Biasio <alvise.debiasio@gmail.com>
 *          Federico Chiariotti <whatever@blbl.it>
 *          Michele Polese <michele.polese@gmail.com>
 *          Davide Marcato <davidemarcato@outlook.com>
 *          
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/quic-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/config-store-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/flow-monitor-helper.h"
#include <iostream>

using namespace ns3;

Ptr<PacketSink> sink;     //!< Pointer to the packet sink application
std::vector<uint64_t> lastTotalRx; //!< The value of the last total received bytes
std::ofstream   netStatsOut; // Create an output file stream (optional)
FlowMonitorHelper flowHelper;
Ptr<FlowMonitor> flowMonitor;

NS_LOG_COMPONENT_DEFINE("QuicChangeOnSpin");

// connect to a number of traces
static void
CwndChange (Ptr<OutputStreamWrapper> stream, uint32_t oldCwnd, uint32_t newCwnd)
{
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << oldCwnd << "\t" << newCwnd << std::endl;
}

static void
RttChange (Ptr<OutputStreamWrapper> stream, Time oldRtt, Time newRtt)
{
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << oldRtt.GetSeconds () << "\t" << newRtt.GetSeconds () << std::endl;
}

static void
Rx (Ptr<OutputStreamWrapper> stream, Ptr<const Packet> p, const QuicHeader& q, Ptr<const QuicSocketBase> qsb)
{
  *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << p->GetSize() << std::endl;
}

static void
receivedSpin (Ptr<OutputStreamWrapper> stream, Ptr<const Packet> p, const QuicHeader& q, Ptr<const QuicSocketBase> qsb)
{
  if (q.IsShort()) {
    *stream->GetStream () << Simulator::Now ().GetSeconds () << "\t" << q.GetSpinBit() << std::endl;
  }
}

/**
 * Rx drop callback
 *
 * \param p The dropped packet.
 */
static void
RxDrop(Ptr<const Packet> p)
{
    NS_LOG_UNCOND("RxDrop at " << Simulator::Now().GetSeconds());
}

static void
Traces(uint32_t serverId, std::string pathVersion, std::string finalPart)
{
  AsciiTraceHelper asciiTraceHelper;

  std::ostringstream pathCW;
  pathCW << "/NodeList/" << serverId << "/$ns3::QuicL4Protocol/SocketList/0/QuicSocketBase/CongestionWindow";
  NS_LOG_INFO("Matches cw " << Config::LookupMatches(pathCW.str().c_str()).GetN());

  std::ostringstream fileCW;
  fileCW << pathVersion << "QUIC-cwnd-change"  << serverId << "" << finalPart;

  std::ostringstream pathRTT;
  pathRTT << "/NodeList/" << serverId << "/$ns3::QuicL4Protocol/SocketList/0/QuicSocketBase/RTT";

  std::ostringstream fileRTT;
  fileRTT << pathVersion << "QUIC-rtt"  << serverId << "" << finalPart;

  std::ostringstream pathRCWnd;
  pathRCWnd<< "/NodeList/" << serverId << "/$ns3::QuicL4Protocol/SocketList/0/QuicSocketBase/RWND";

  std::ostringstream fileRCWnd;
  fileRCWnd<<pathVersion << "QUIC-rwnd-change"  << serverId << "" << finalPart;

  std::ostringstream fileName;
  fileName << pathVersion << "QUIC-rx-data" << serverId << "" << finalPart;

  std::ostringstream fileSpin;
  fileSpin << pathVersion << "QUIC-spin" << serverId << "" << finalPart;

  std::ostringstream pathRx;
  pathRx << "/NodeList/" << serverId << "/$ns3::QuicL4Protocol/SocketList/*/QuicSocketBase/Rx";
  NS_LOG_INFO("Matches rx " << Config::LookupMatches(pathRx.str().c_str()).GetN());

  std::ostringstream pathSpin;
  pathSpin << "/NodeList/" << serverId << "/$ns3::QuicL4Protocol/SocketList/*/QuicSocketBase/SpinBit";

  Ptr<OutputStreamWrapper> stream = asciiTraceHelper.CreateFileStream (fileName.str ().c_str ());
  Config::ConnectWithoutContext (pathRx.str ().c_str (), MakeBoundCallback (&Rx, stream));

  Ptr<OutputStreamWrapper> stream1 = asciiTraceHelper.CreateFileStream (fileCW.str ().c_str ());
  Config::ConnectWithoutContext (pathCW.str ().c_str (), MakeBoundCallback(&CwndChange, stream1));

  Ptr<OutputStreamWrapper> stream2 = asciiTraceHelper.CreateFileStream (fileRTT.str ().c_str ());
  Config::ConnectWithoutContext (pathRTT.str ().c_str (), MakeBoundCallback(&RttChange, stream2));

  Ptr<OutputStreamWrapper> stream4 = asciiTraceHelper.CreateFileStream (fileRCWnd.str ().c_str ());
  Config::ConnectWithoutContextFailSafe (pathRCWnd.str ().c_str (), MakeBoundCallback(&CwndChange, stream4));

  Ptr<OutputStreamWrapper> stream5 = asciiTraceHelper.CreateFileStream (fileSpin.str ().c_str ());
  Config::ConnectWithoutContextFailSafe (pathSpin.str ().c_str (), MakeBoundCallback(&receivedSpin, stream5));
}

void flowThroughput()
{
     Ptr<Ipv4FlowClassifier> classifier=DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());

     std::string proto;
     std::map < FlowId, FlowMonitor::FlowStats > stats = flowMonitor->GetFlowStats();
     netStatsOut << "  Time: " << Simulator::Now ().GetSeconds () << std::endl;
     if (lastTotalRx.size() < stats.size())
         lastTotalRx.resize(stats.size(), 0);
     for (std::map < FlowId, FlowMonitor::FlowStats>::iterator flow=stats.begin(); flow!=stats.end(); flow++)
         {
          if (classifier->FindFlow(flow->first).sourceAddress == "10.1.6.2") continue;
          Ipv4FlowClassifier::FiveTuple  t = classifier->FindFlow(flow->first);
             switch(t.protocol)
             {
             case(6):
                 proto = "TCP";
                 break;
             case(17):
                 proto = "UDP";
                 break;
             default:
                 exit(1);
             }
             netStatsOut << "FlowID: " << flow->first << " (" << proto << " "
             << t.sourceAddress << " / " << t.sourcePort << " --> "
             << t.destinationAddress << " / " << t.destinationPort << ")" << std::endl;

                 //  printStats(flow->second);

        netStatsOut << "  Tx Bytes: " << flow->second.txBytes << std::endl;
        netStatsOut << "  Rx Bytes: " << flow->second.rxBytes << std::endl;
        netStatsOut << "  Tx Packets: " << flow->second.txPackets << std::endl;
        netStatsOut << "  Rx Packets: " << flow->second.rxPackets << std::endl;
        netStatsOut << "  Lost Packets: " << flow->second.lostPackets << std::endl;
        netStatsOut << "  Pkt Lost Ratio: " << ((double)flow->second.txPackets-(double)flow->second.rxPackets)/(double)flow->second.txPackets << std::endl;
        

        netStatsOut << "  Throughput: " << ((lastTotalRx[flow->first - 1] > 0 ? ( ((double)flow->second.rxBytes - lastTotalRx[flow->first - 1])*8/(1000000) ) : ((double)flow->second.rxBytes*8/(1000000))) / .5) << std::endl;
        lastTotalRx[flow->first - 1] = flow->second.rxBytes;

        netStatsOut << "  Mean{Throughput}: " << (flow->second.rxBytes*8/(1000000*Simulator::Now().GetSeconds())) << std::endl;
        netStatsOut << "  Mean{Delay}: " << (flow->second.delaySum.GetSeconds()/flow->second.rxPackets) << std::endl;
        netStatsOut << "  Mean{Jitter}: " << (flow->second.jitterSum.GetSeconds()/(flow->second.rxPackets)) << std::endl;


         }
  Simulator::Schedule (Seconds (.5), &flowThroughput); // Callback every 0.5s
}

/**
 * Calulate the throughput
 */
// void
// CalculateThroughput()
// {
//     Time now = Simulator::Now(); /* Return the simulator's virtual time. */
//     double cur = (sink->GetTotalRx() - lastTotalRx) * (double)8 /
//                  1e5; /* Convert Application RX Packets to MBits. */
//     std::cout << now.GetSeconds() << "s: \t" << cur << " Mbit/s" << std::endl;
//     lastTotalRx = sink->GetTotalRx();
//     Simulator::Schedule(MilliSeconds(100), &CalculateThroughput);
// }

int
main (int argc, char *argv[])
{

  Config::SetDefault ("ns3::QuicSocketBase::SocketSndBufSize", UintegerValue(40000000));
  Config::SetDefault ("ns3::QuicSocketBase::SocketRcvBufSize", UintegerValue(40000000));
  Config::SetDefault ("ns3::QuicStreamBase::StreamSndBufSize", UintegerValue(40000000));
  Config::SetDefault ("ns3::QuicStreamBase::StreamRcvBufSize", UintegerValue(40000000));
  Config::SetDefault ("ns3::QuicSocketBase::SchedulingPolicy", TypeIdValue(QuicSocketTxEdfScheduler::GetTypeId ()));
  // The below value configures the default behavior of global routing.
  // By default, it is disabled.  To respond to interface events, set to true
  Config::SetDefault ("ns3::Ipv4GlobalRouting::RespondToInterfaceEvents", BooleanValue (true));
  Config::SetDefault("ns3::Ipv4GlobalRouting::RandomEcmpRouting",     BooleanValue(true)); // enable multi-path routing

  CommandLine cmd;
  cmd.Parse (argc, argv);
  
  std::cout
      << "\n\n#################### SIMULATION SET-UP ####################\n\n\n";

  LogLevel log_precision = LOG_LEVEL_INFO;
  Time::SetResolution (Time::NS);
  LogComponentEnableAll (LOG_PREFIX_TIME);
  LogComponentEnableAll (LOG_PREFIX_FUNC);
  LogComponentEnableAll (LOG_PREFIX_NODE);
  // LogComponentEnable ("QuicEchoClientApplication", log_precision);
  // LogComponentEnable ("QuicEchoServerApplication", log_precision);
  // LogComponentEnable ("QuicSocketBase", log_precision);
  // LogComponentEnable ("QuicStreamBase", log_precision);
  // LogComponentEnable("QuicStreamRxBuffer", log_precision);
  // LogComponentEnable("QuicStreamTxBuffer", log_precision);
  // LogComponentEnable("QuicSocketTxScheduler", log_precision);
  // LogComponentEnable("QuicSocketTxEdfScheduler", log_precision);
  // LogComponentEnable ("Socket", log_precision);
  // LogComponentEnable ("Application", log_precision);
  LogComponentEnable ("BulkSendApplication", log_precision);
  // LogComponentEnable ("Node", log_precision);
  //LogComponentEnable ("InternetStackHelper", log_precision);
  //LogComponentEnable ("QuicSocketFactory", log_precision);
  //LogComponentEnable ("ObjectFactory", log_precision);
  //LogComponentEnable ("TypeId", log_precision);
  //LogComponentEnable ("QuicL4Protocol", log_precision);
  // LogComponentEnable ("QuicL5Protocol", log_precision);
  //LogComponentEnable ("ObjectBase", log_precision);

  //LogComponentEnable ("QuicEchoHelper", log_precision);
  //LogComponentEnable ("UdpSocketImpl", log_precision);
  // LogComponentEnable ("QuicHeader", log_precision);
  //LogComponentEnable ("QuicSubheader", log_precision);
  //LogComponentEnable ("Header", log_precision);
  //LogComponentEnable ("PacketMetadata", log_precision);
  // LogComponentEnable ("QuicSocketTxBuffer", log_precision);


  NodeContainer nodes;
  nodes.Create (4);
  auto n1 = nodes.Get (0); // Client
  auto s1 = nodes.Get (1); // First Router
  auto s2 = nodes.Get (2); // Second Router
  auto n2 = nodes.Get (3); // Server

  NodeContainer n1s1;
  n1s1.Add (n1);
  n1s1.Add (s1);

  NodeContainer s1s2;
  s1s2.Add (s1);
  s1s2.Add (s2);

  NodeContainer s2n2;
  s2n2.Add (s2);
  s2n2.Add (n2);

  double error_p = 0.01; // 1%

  // Configure the error model
  // Here we use RateErrorModel with packet error rate
  Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable> ();
  uv->SetStream (50);

  RateErrorModel error_model;
  error_model.SetRandomVariable (uv);
  error_model.SetUnit (RateErrorModel::ERROR_UNIT_PACKET);
  error_model.SetRate (error_p);
  
  // Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
  // em->SetAttribute("ErrorRate", DoubleValue(0.00001));

  PointToPointHelper lossLink;
  lossLink.SetDeviceAttribute ("DataRate", StringValue ("17Mbps"));
  lossLink.SetChannelAttribute ("Delay", StringValue ("33ms"));

  PointToPointHelper delayLink;
  delayLink.SetDeviceAttribute ("DataRate", StringValue ("17Mbps"));
  delayLink.SetChannelAttribute ("Delay", StringValue ("100ms"));

  PointToPointHelper goodLink;
  goodLink.SetDeviceAttribute ("DataRate", StringValue ("17Mbps"));

  // Delay 100ms로 설정 시 DataRate 너무 낮으면 이상해짐

  // lossLink.SetDeviceAttribute ("ReceiveErrorModel",
  //                                  PointerValue (&error_model));

  QuicHelper stack;
  stack.InstallQuic (nodes);

  // Create the point-to-point link required by the topology

  NetDeviceContainer n1s1Device;
  n1s1Device = goodLink.Install (n1s1);

  NetDeviceContainer s1s2LossDevice;
  s1s2LossDevice = lossLink.Install (s1s2);

  NetDeviceContainer s1s2DelayDevice;
  s1s2DelayDevice = delayLink.Install (s1s2);

  NetDeviceContainer s2n2Device;
  s2n2Device = goodLink.Install (s2n2);

  Ipv4AddressHelper address;

  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer n1s1Interfaces = address.Assign (n1s1Device);

  address.SetBase ("10.1.2.0", "255.255.255.0");
  address.Assign (s1s2LossDevice);

  address.SetBase ("10.1.3.0", "255.255.255.0");
  address.Assign (s1s2DelayDevice);

  address.SetBase ("10.1.4.0", "255.255.255.0");
  Ipv4InterfaceContainer s2n2Interfaces = address.Assign (s2n2Device);

  ApplicationContainer clientApps;
  ApplicationContainer serverApps;

  // QUIC client and server
  uint32_t dlPort = 1025;

  // QuicServerHelper dlPacketSinkHelper (dlPort);
  PacketSinkHelper dlPacketSinkHelper ("ns3::QuicSocketFactory",
                                      InetSocketAddress (s2n2Interfaces.GetAddress (1), dlPort));

  serverApps.Add (dlPacketSinkHelper.Install (n2));

  // sink = StaticCast<PacketSink> (serverApps.Get (0));
  // QuicClientHelper dlClient (s4n2Interfaces.GetAddress (1), dlPort);
  BulkSendHelper dlClient ("ns3::QuicSocketFactory",
                           InetSocketAddress (s2n2Interfaces.GetAddress (1), dlPort));
  // BulkSend 경우 path 하나 끊기면 전송 실패함 (delayLink 활성화 시간을 0.1초 빠르게 함으로써 해결됨)

  // double interPacketInterval = 1000;
  // dlClient.SetAttribute ("Interval", TimeValue (MicroSeconds(interPacketInterval)));
  // Interval 설정 안하면 이상해짐 근데 Interval 때문에 전송 속도가 느려짐 -> 어떻게 해야 하나?

  dlClient.SetAttribute("MaxBytes", UintegerValue(10000));
  dlClient.SetAttribute ("SendSize", UintegerValue (1400));
  // dlClient.SetAttribute ("PacketSize", UintegerValue(1039));
  // dlClient.SetAttribute ("MaxPackets", UintegerValue(11000));
  clientApps.Add (dlClient.Install (n1));

  s1s2LossDevice.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(&error_model)); // s2쪽에서 패킷 드랍
  // s1s2Device.Get(1)->SetAttribute("ReceiveErrorModel", PointerValue(em));

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  // n1s1Device.Get(0)->TraceConnectWithoutContext("PhyRxDrop", MakeCallback(&RxDrop));
  // n1s1Device.Get(1)->TraceConnectWithoutContext("PhyRxDrop", MakeCallback(&RxDrop));
  // s1s2Device.Get(0)->TraceConnectWithoutContext("PhyRxDrop", MakeCallback(&RxDrop));
  // s1s2Device.Get(1)->TraceConnectWithoutContext("PhyRxDrop", MakeCallback(&RxDrop));
  // s2n2Device.Get(0)->TraceConnectWithoutContext("PhyRxDrop", MakeCallback(&RxDrop));
  s1s2LossDevice.Get(1)->TraceConnectWithoutContext("PhyRxDrop", MakeCallback(&RxDrop));

  flowMonitor = flowHelper.InstallAll();
  netStatsOut.open("netStatsMPLS.txt"); //Write Network measurements to output file

  serverApps.Stop (Seconds (2000.0));
  serverApps.Start (Seconds (0.99));
  clientApps.Stop (Seconds (100.0));
  clientApps.Start (Seconds (1.0));

  Ptr<Ipv4> ipv4s1 = s1->GetObject<Ipv4> ();

  Simulator::Schedule (Seconds (1.0), &Ipv4::SetDown, ipv4s1, 3); // delay link 비활성화, 0: loopback interface, 1: 들어오는거
  Simulator::Schedule (Seconds (29.9), &Ipv4::SetUp, ipv4s1, 3); // delay link 활성화, 0: loopback interface, 1: 들어오는거
  Simulator::Schedule (Seconds (30), &Ipv4::SetDown, ipv4s1, 2); // lossy link 비활성화, 0: loopback interface, 1: 들어오는거
  // Simulator::Schedule(Seconds(1.1), &CalculateThroughput);
  Simulator::Schedule (Seconds (1.1), &flowThroughput); // Callback every 0.5s

  Simulator::Schedule (Seconds (0.991), &Traces, n2->GetId(),
        "./server", ".txt");
  Simulator::Schedule (Seconds (1.001), &Traces, n1->GetId(),
        "./client", ".txt");

  // 비정상 패킷 많이 생김 (프린팅을 위한 패킷 메타데이터 저장으로 인한 충돌 문제로 추정)
  // Packet::EnablePrinting ();

  // stdout, stderr 출력을 파일로 저장하면 느려짐, 그냥 실행해도 소폭 느려짐 (원인모름)
  // Packet::EnableChecking ();

  // stack.EnablePcapIpv4All ("quictesterStream");
  lossLink.EnablePcapAll ("quicChangeOnSpin");
  lossLink.EnablePcap("quicChangeOnSpinTest", NodeContainer(s1), true);

  std::cout << "\n\n#################### STARTING RUN ####################\n\n";
  Simulator::Stop (Seconds (2000.0));
  Simulator::Run ();

  flowMonitor->SerializeToXmlFile("quicChangeOnSpin.xml", true, true);

  std::cout
      << "\n\n#################### RUN FINISHED ####################\n\n\n";
  Simulator::Destroy ();

  std::cout
      << "\n\n#################### SIMULATION END ####################\n\n\n";
  return 0;
}
