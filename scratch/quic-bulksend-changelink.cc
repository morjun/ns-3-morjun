/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2013 ResiliNets, ITTC, University of Kansas
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
 * Authors of the original TCP example: 
 * Justin P. Rohrer, Truc Anh N. Nguyen <annguyen@ittc.ku.edu>, Siddharth Gangadhar <siddharth@ittc.ku.edu>
 * James P.G. Sterbenz <jpgs@ittc.ku.edu>, director
 * ResiliNets Research Group  http://wiki.ittc.ku.edu/resilinets
 * Information and Telecommunication Technology Center (ITTC)
 * and Department of Electrical Engineering and Computer Science
 * The University of Kansas Lawrence, KS USA.
 *
 * Work supported in part by NSF FIND (Future Internet Design) Program
 * under grant CNS-0626918 (Postmodern Internet Architecture),
 * NSF grant CNS-1050226 (Multilayer Network Resilience Analysis and Experimentation on GENI),
 * US Department of Defense (DoD), and ITTC at The University of Kansas.
 *
 * “TCP Westwood(+) Protocol Implementation in ns-3”
 * Siddharth Gangadhar, Trúc Anh Ngọc Nguyễn , Greeshma Umapathi, and James P.G. Sterbenz,
 * ICST SIMUTools Workshop on ns-3 (WNS3), Cannes, France, March 2013
 *
 * Adapted to QUIC by:
 *          Alvise De Biasio <alvise.debiasio@gmail.com>
 *          Federico Chiariotti <chiariotti.federico@gmail.com>
 *          Michele Polese <michele.polese@gmail.com>
 *          Davide Marcato <davidemarcato@outlook.com>
 *
 */

#include <iostream>
#include <fstream>
#include <string>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/quic-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/error-model.h"
#include "ns3/tcp-header.h"
#include "ns3/udp-header.h"
#include "ns3/enum.h"
#include "ns3/event-id.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/traffic-control-module.h"

#include "ns3/netanim-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("QuicVariantsComparisonBulkSend");

Ptr<PacketSink> sink;     //!< Pointer to the packet sink application
std::vector<uint64_t> lastTotalRx; //!< The value of the last total received bytes
std::ofstream   netStatsOut; // Create an output file stream (optional)
FlowMonitorHelper flowHelper;
Ptr<FlowMonitor> flowMonitor;

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
  std::ostringstream pathRx;
  pathRx << "/NodeList/" << serverId << "/$ns3::QuicL4Protocol/SocketList/*/QuicSocketBase/Rx";
  NS_LOG_INFO("Matches rx " << Config::LookupMatches(pathRx.str().c_str()).GetN());

  Ptr<OutputStreamWrapper> stream = asciiTraceHelper.CreateFileStream (fileName.str ().c_str ());
  Config::ConnectWithoutContext (pathRx.str ().c_str (), MakeBoundCallback (&Rx, stream));

  Ptr<OutputStreamWrapper> stream1 = asciiTraceHelper.CreateFileStream (fileCW.str ().c_str ());
  Config::ConnectWithoutContext (pathCW.str ().c_str (), MakeBoundCallback(&CwndChange, stream1));

  Ptr<OutputStreamWrapper> stream2 = asciiTraceHelper.CreateFileStream (fileRTT.str ().c_str ());
  Config::ConnectWithoutContext (pathRTT.str ().c_str (), MakeBoundCallback(&RttChange, stream2));

  Ptr<OutputStreamWrapper> stream4 = asciiTraceHelper.CreateFileStream (fileRCWnd.str ().c_str ());
  Config::ConnectWithoutContextFailSafe (pathRCWnd.str ().c_str (), MakeBoundCallback(&CwndChange, stream4));
}
// 여기까지 Trace 관련 (공식 tutorial 및 manual 참조)

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
          // if (classifier->FindFlow(flow->first).sourceAddress == "10.1.6.2") continue;
          if(flow->first == 2) continue;
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
  Simulator::Schedule (Seconds (.1), &flowThroughput); // Callback every 0.1s(100ms)
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

int main (int argc, char *argv[])
{
  std::string transport_prot = "QuicBbr";
  double error_p = 0.01;
  std::string bandwidth = "100Mbps";
  std::string delay = "33ms";
  std::string highDelay = "100ms";

  bool tracing = false;
  std::string prefix_file_name = "quicBulksendChangeLink";
  double data_mbytes = 40;
  uint32_t mtu_bytes = 1400;
  uint16_t num_flows = 1;
  float duration = 20;
  uint32_t run = 0;
  bool flowMonitorFlag = true;
  bool pcap = true;
  std::string queue_disc_type = "ns3::PfifoFastQueueDisc";

  LogComponentEnable ("Config", LOG_LEVEL_ALL);
  // LogComponentEnable ("QuicHeader", LOG_LEVEL_ALL); //LOG_LEVEL_INFO
  // LogComponentEnable ("QuicSocketBase", LOG_LEVEL_INFO); //LOG_LEVEL_INFO

  CommandLine cmd;
  cmd.AddValue ("transport_prot", "Transport protocol to use: TcpNewReno, "
                "TcpHybla, TcpHighSpeed, TcpHtcp, TcpVegas, TcpScalable, TcpVeno, "
                "TcpBic, TcpYeah, TcpIllinois, TcpWestwood, TcpWestwoodPlus, TcpLedbat ", transport_prot);
  cmd.AddValue ("error_p", "Packet error rate", error_p);
  cmd.AddValue ("bandwidth", "Bottleneck bandwidth", bandwidth);
  cmd.AddValue ("delay", "Bottleneck delay", delay);
  // cmd.AddValue ("access_bandwidth", "Access link bandwidth", access_bandwidth);
  cmd.AddValue ("highDelay", "Access link delay", highDelay);
  cmd.AddValue ("tracing", "Flag to enable/disable tracing", tracing);
  cmd.AddValue ("prefix_name", "Prefix of output trace file", prefix_file_name);
  cmd.AddValue ("data", "Number of Megabytes of data to transmit", data_mbytes);
  cmd.AddValue ("mtu", "Size of IP packets to send in bytes", mtu_bytes);
  cmd.AddValue ("num_flows", "Number of flows", num_flows);
  cmd.AddValue ("duration", "Time to allow flows to run in seconds", duration);
  cmd.AddValue ("run", "Run index (for setting repeatable seeds)", run);
  cmd.AddValue ("flowMonitorFlag", "Enable flow monitor", flowMonitorFlag);
  cmd.AddValue ("pcap_tracing", "Enable or disable PCAP tracing", pcap);
  cmd.AddValue ("queue_disc_type", "Queue disc type for gateway (e.g. ns3::CoDelQueueDisc)", queue_disc_type);
  cmd.Parse (argc, argv);

  transport_prot = std::string ("ns3::") + transport_prot;

  SeedManager::SetSeed (1);
  SeedManager::SetRun (run);

  // User may find it convenient to enable logging
  Time::SetResolution (Time::NS);
  LogComponentEnableAll (LOG_PREFIX_TIME);
  LogComponentEnableAll (LOG_PREFIX_FUNC);
  LogComponentEnableAll (LOG_PREFIX_NODE);
  // LogComponentEnable("QuicVariantsComparison", LOG_LEVEL_ALL);
  // LogComponentEnable("BulkSendApplication", LOG_LEVEL_INFO);
  // LogComponentEnable("PfifoFastQueueDisc", LOG_LEVEL_ALL);
  // LogComponentEnable ("QuicSocketBase", LOG_LEVEL_ALL);
  // LogComponentEnable("TcpVegas", LOG_LEVEL_ALL);
  // LogComponentEnable("QuicBbr", LOG_LEVEL_ALL);
  // LogComponentEnable("QuicL5Protocol", LOG_LEVEL_ALL);
  // LogComponentEnable("QuicHeader", LOG_LEVEL_ALL);

  // Set the simulation start and stop time
  float start_time = 0.1;
  float stop_time = start_time + duration;

  // 4 MB of TCP buffer
  Config::SetDefault ("ns3::QuicSocketBase::SocketRcvBufSize", UintegerValue (1 << 21));
  Config::SetDefault ("ns3::QuicSocketBase::SocketSndBufSize", UintegerValue (1 << 21));
  Config::SetDefault ("ns3::QuicStreamBase::StreamSndBufSize", UintegerValue (1 << 21));
  Config::SetDefault ("ns3::QuicStreamBase::StreamRcvBufSize", UintegerValue (1 << 21));

  Config::SetDefault ("ns3::Ipv4GlobalRouting::RespondToInterfaceEvents", BooleanValue (true));
  Config::SetDefault("ns3::Ipv4GlobalRouting::RandomEcmpRouting",     BooleanValue(true)); // enable multi-path routing
 
  // Select congestion control variant
  if (transport_prot.compare ("ns3::TcpWestwoodPlus") == 0)
    { 
      // TcpWestwoodPlus is not an actual TypeId name; we need TcpWestwood here
      Config::SetDefault ("ns3::QuicL4Protocol::SocketType", TypeIdValue (TcpWestwood::GetTypeId ()));
      // the default protocol type in ns3::TcpWestwood is WESTWOOD
      Config::SetDefault ("ns3::TcpWestwood::ProtocolType", EnumValue (TcpWestwood::WESTWOODPLUS));
    }
  else
    {
      TypeId tcpTid;
      NS_ABORT_MSG_UNLESS (TypeId::LookupByNameFailSafe (transport_prot, &tcpTid), "TypeId " << transport_prot << " not found");
      Config::SetDefault ("ns3::QuicL4Protocol::SocketType", TypeIdValue (TypeId::LookupByName (transport_prot))); //quic
      // Quic의 L4에서의 SocketType을 설정해준다: QuicBbr
    }

  // Create gateways, sources, and sinks
  // sources(*num_flows) -> gateway -> gateway -> sinks(*num_flows)
  NodeContainer gateways;
  gateways.Create (2);

  NodeContainer sources;
  sources.Create (num_flows);
  NodeContainer sinks;
  sinks.Create (num_flows);

  // AnimationInterface anim ("quic-bulksend-custom-animation.xml");
  // anim.SetConstantPosition(gateways.Get(0), 50.0, 0.0);
  // anim.SetConstantPosition(gateways.Get(1), 75.0, 0.0);
  

  // Configure the error model
  // Here we use RateErrorModel with packet error rate
  Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable> ();
  uv->SetStream (50);
  RateErrorModel error_model;
  error_model.SetRandomVariable (uv);
  error_model.SetUnit (RateErrorModel::ERROR_UNIT_PACKET);
  error_model.SetRate (error_p);

  PointToPointHelper lossLink; // gateway - gateway
  lossLink.SetDeviceAttribute ("DataRate", StringValue (bandwidth));
  lossLink.SetChannelAttribute ("Delay", StringValue (delay));
  lossLink.SetDeviceAttribute ("ReceiveErrorModel", PointerValue (&error_model));

  PointToPointHelper delayLink;
  delayLink.SetDeviceAttribute ("DataRate", StringValue (bandwidth));
  delayLink.SetChannelAttribute ("Delay", StringValue (highDelay));
  
  PointToPointHelper goodLink; // source - gateway
  goodLink.SetDeviceAttribute ("DataRate", StringValue (bandwidth));
  // goodLink.SetChannelAttribute ("Delay", StringValue (highDelay));

  QuicHelper stack;
  stack.InstallQuic (sources);
  stack.InstallQuic (sinks);
  stack.InstallQuic (gateways);

  TrafficControlHelper tchPfifo;
  tchPfifo.SetRootQueueDisc ("ns3::PfifoFastQueueDisc");
  // https://www.nsnam.org/docs/release/3.33/doxygen/classns3_1_1_queue_disc.html

  TrafficControlHelper tchCoDel;
  tchCoDel.SetRootQueueDisc ("ns3::CoDelQueueDisc");

  Ipv4AddressHelper address;
  address.SetBase ("10.0.0.0", "255.255.255.0");

  // Configure the sources and sinks net devices
  // and the channels between the sources/sinks and the gateways

  PointToPointHelper LocalLink; // gateway - sink
  LocalLink.SetDeviceAttribute ("DataRate", StringValue (bandwidth));
  // LocalLink.SetChannelAttribute ("Delay", StringValue (highDelay));

  Ipv4InterfaceContainer sink_interfaces;

  DataRate bottle_b (bandwidth);
  Time high_d (highDelay);
  Time bottle_d (delay);

  uint32_t size = (bottle_b.GetBitRate () / 8) *
    ((high_d + bottle_d) * 2).GetSeconds (); // Why?

  Config::SetDefault ("ns3::PfifoFastQueueDisc::MaxSize",
                      QueueSizeValue (QueueSize (QueueSizeUnit::PACKETS, size / mtu_bytes)));
  Config::SetDefault ("ns3::CoDelQueueDisc::MaxSize",
                      QueueSizeValue (QueueSize (QueueSizeUnit::BYTES, size)));

  for (int i = 0; i < num_flows; i++)
    {
    // anim.SetConstantPosition(sources.Get(i), 0.0, i*10);
    // anim.SetConstantPosition(sinks.Get(i), 100.0, i*10);

      NetDeviceContainer devices;
      devices = goodLink.Install (sources.Get (i), gateways.Get (0)); //채널 생성, 네트워크 디바이스를 각 노드에 설치하고 컨테이너에 저장
      tchPfifo.Install (devices);
      address.NewNetwork (); // source - gateway 네트워크
      Ipv4InterfaceContainer interfaces = address.Assign (devices);

      devices = LocalLink.Install (gateways.Get (1), sinks.Get (i));
      if (queue_disc_type.compare ("ns3::PfifoFastQueueDisc") == 0) // default
        {
          tchPfifo.Install (devices);
        }
      else if (queue_disc_type.compare ("ns3::CoDelQueueDisc") == 0)
        {
          tchCoDel.Install (devices);
        }
      else
        {
          NS_FATAL_ERROR ("Queue not recognized. Allowed values are ns3::CoDelQueueDisc or ns3::PfifoFastQueueDisc");
        }
      address.NewNetwork ();
      interfaces = address.Assign (devices); // gateway - sink 네트워크
      sink_interfaces.Add (interfaces.Get (1));
      
      NetDeviceContainer lossDevices = lossLink.Install (gateways.Get (0), gateways.Get (1));
      if (queue_disc_type.compare ("ns3::PfifoFastQueueDisc") == 0)
        {
          tchPfifo.Install (lossDevices);
        }
      else if (queue_disc_type.compare ("ns3::CoDelQueueDisc") == 0)
        {
          tchCoDel.Install (lossDevices);
        }
      else
        {
          NS_FATAL_ERROR ("Queue not recognized. Allowed values are ns3::CoDelQueueDisc or ns3::PfifoFastQueueDisc");
        }

      address.NewNetwork (); // gateway - gateway 네트워크
      interfaces = address.Assign (lossDevices);
      lossDevices.Get(1)->TraceConnectWithoutContext("PhyRxDrop", MakeCallback(&RxDrop));

      NetDeviceContainer delayDevices = delayLink.Install (gateways.Get (0), gateways.Get (1));
      if (queue_disc_type.compare ("ns3::PfifoFastQueueDisc") == 0)
        {
          tchPfifo.Install (delayDevices);
        }
      else if (queue_disc_type.compare ("ns3::CoDelQueueDisc") == 0)
        {
          tchCoDel.Install (delayDevices);
        }
      else
        {
          NS_FATAL_ERROR ("Queue not recognized. Allowed values are ns3::CoDelQueueDisc or ns3::PfifoFastQueueDisc");
        }

      address.NewNetwork (); // gateway - gateway 네트워크
      interfaces = address.Assign (delayDevices);
    }

  NS_LOG_INFO ("Initialize Global Routing.");
  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  uint16_t port = 50000;
  Address sinkLocalAddress (InetSocketAddress (Ipv4Address::GetAny (), port));
  
  ApplicationContainer clientApps;
  ApplicationContainer serverApps;
  // applications client and server
  for (uint16_t i = 0; i < sources.GetN (); i++)
    {
      AddressValue remoteAddress (InetSocketAddress (sink_interfaces.GetAddress (i, 0), port));
      BulkSendHelper ftp ("ns3::QuicSocketFactory", Address ());
      // This traffic generator simply sends data as fast as possible up to MaxBytes or until the application is stopped (if MaxBytes is zero).
      // https://www.nsnam.org/docs/release/3.19/doxygen/classns3_1_1_bulk_send_helper.html

      ftp.SetAttribute ("Remote", remoteAddress);
      ftp.SetAttribute ("SendSize", UintegerValue (1400));
      ftp.SetAttribute ("MaxBytes", UintegerValue(int(data_mbytes*1000000)));
      clientApps.Add(ftp.Install (sources.Get (i)));

      PacketSinkHelper sinkHelper ("ns3::QuicSocketFactory", sinkLocalAddress);
      // Receive and consume traffic generated to an IP address and port.
      // https://www.nsnam.org/docs/release/3.29/doxygen/classns3_1_1_packet_sink_helper.html#details

      sinkHelper.SetAttribute ("Protocol", TypeIdValue (QuicSocketFactory::GetTypeId ()));
      serverApps.Add(sinkHelper.Install (sinks.Get (i)));
      // sinks: server, sources: client? (Client -> Server Bulk send)
    }

  serverApps.Start (Seconds (0.99));
  clientApps.Stop (Seconds (10.0));
  clientApps.Start (Seconds (2));

  for (uint16_t i = 0; i < num_flows; i++)
    {
      auto n2 = sinks.Get (i);
      auto n1 = sources.Get (i);
      Time t = Seconds(2.100001);
      Simulator::Schedule (t, &Traces, n2->GetId(), 
            "./server", ".txt");
      Simulator::Schedule (t, &Traces, n1->GetId(), 
            "./client", ".txt");
    }

  if (pcap) //pcap tracing
    {
      lossLink.EnablePcapAll (prefix_file_name, true);
      LocalLink.EnablePcapAll (prefix_file_name, true);
      goodLink.EnablePcapAll (prefix_file_name, true);
    }

  // Flow monitor
  if (flowMonitorFlag)
    {
      flowMonitor = flowHelper.InstallAll ();
      netStatsOut.open("netStatsMPLS.txt"); //Write Network measurements to output file
    }
  
  Ptr<Ipv4> ipv4s1 = gateways.Get(0)->GetObject<Ipv4> ();

  Simulator::Schedule (Seconds (1.00), &Ipv4::SetUp, ipv4s1, 2); // lossy link 활성화, 0: loopback interface, 1: 들어오는거
  Simulator::Schedule (Seconds (0.99), &Ipv4::SetDown, ipv4s1, 3); // delay link 비활성화, 0: loopback interface, 1: 들어오는거
  Simulator::Schedule (Seconds (3.0), &Ipv4::SetUp, ipv4s1, 3); // delay link 활성화, 0: loopback interface, 1: 들어오는거
  Simulator::Schedule (Seconds (3.01), &Ipv4::SetDown, ipv4s1, 2); // lossy link 비활성화, 0: loopback interface, 1: 들어오는거
  Simulator::Schedule (Seconds (1.0), &flowThroughput); // Callback every 0.1s

  Simulator::Stop (Seconds (stop_time));
  Simulator::Run ();

  if (flowMonitorFlag)
    {

  flowMonitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowHelper.GetClassifier ());
  FlowMonitor::FlowStatsContainer stats = flowMonitor->GetFlowStats ();
  for (std::map<FlowId, FlowMonitor::FlowStats>::const_iterator i = stats.begin (); i != stats.end (); ++i)
    {
      Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (i->first);
      if (t.sourceAddress == "10.1.1.2")
        {
          continue;
        }
      std::cout << "Flow " << i->first  << " (" << t.sourceAddress << " -> " << t.destinationAddress << ")\n";
      std::cout << "  Tx Packets: " << i->second.txPackets << "\n";
      std::cout << "  Tx Bytes:   " << i->second.txBytes << "\n";
      std::cout << "  TxOffered:  " << i->second.txBytes * 8.0 / 9.0 / 1000 / 1000  << " Mbps\n";
      std::cout << "  Rx Packets: " << i->second.rxPackets << "\n";
      std::cout << "  Rx Bytes:   " << i->second.rxBytes << "\n";
      std::cout << "  Throughput: " << i->second.rxBytes * 8.0 / 9.0 / 1000 / 1000  << " Mbps\n";
    }
      flowHelper.SerializeToXmlFile (prefix_file_name + ".flowmonitor", true, true);
    }

  Simulator::Destroy ();
  return 0;
}
