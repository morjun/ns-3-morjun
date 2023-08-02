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
#include <iostream>

#include "ns3/netanim-module.h"

#include "ns3/object.h"
#include "ns3/uinteger.h"
#include "ns3/traced-value.h"
#include "ns3/trace-source-accessor.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("quic-tester-bulksend");

int
main (int argc, char *argv[])
{
  CommandLine cmd;
  cmd.Parse (argc, argv);

  std::cout
      << "\n\n#################### SIMULATION SET-UP ####################\n\n\n";

  LogLevel log_precision = LOG_LEVEL_LOGIC;
  Time::SetResolution (Time::NS);
  LogComponentEnableAll (LOG_PREFIX_TIME);
  LogComponentEnableAll (LOG_PREFIX_FUNC);
  LogComponentEnableAll (LOG_PREFIX_NODE);
  // LogComponentEnable ("QuicEchoClientApplication", log_precision);
  // LogComponentEnable ("QuicEchoServerApplication", log_precision);
  // LogComponentEnable ("QuicClient", log_precision);
//  LogComponentEnable ("QuicHeader", LOG_LEVEL_INFO);
  LogComponentEnable ("QuicSocketBase", log_precision);
  LogComponentEnable ("BulkSendApplication", log_precision);
//  LogComponentEnable ("QuicStreamBase", LOG_LEVEL_LOGIC);
//  LogComponentEnable ("Socket", log_precision);
//  LogComponentEnable ("Application", log_precision);
//  LogComponentEnable ("Node", log_precision);
//  LogComponentEnable ("InternetStackHelper", log_precision);
//  LogComponentEnable ("QuicSocketFactory", log_precision);
//  LogComponentEnable ("ObjectFactory", log_precision);
//  //LogComponentEnable ("TypeId", log_precision);
  // LogComponentEnable ("UdpL4Protocol", log_precision);
//  LogComponentEnable ("QuicL4Protocol", log_precision);
 LogComponentEnable ("QuicL5Protocol", log_precision);
//  //LogComponentEnable ("ObjectBase", log_precision);
//
//  LogComponentEnable ("QuicEchoHelper", log_precision);
    // LogComponentEnable ("QuicSocketTxScheduler", log_precision);
//  LogComponentEnable ("QuicSocketRxBuffer", log_precision);
//  LogComponentEnable ("QuicHeader", log_precision);
//  LogComponentEnable ("QuicSubheader", log_precision);
//  LogComponentEnable ("Header", log_precision);
//  LogComponentEnable ("PacketMetadata", log_precision);


  NodeContainer nodes;
  nodes.Create (2);
  auto n1 = nodes.Get (0);
  auto n2 = nodes.Get (1);

  double error_p = 0.01;

  // Configure the error model
  // Here we use RateErrorModel with packet error rate
  Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable> ();
  uv->SetStream (50);
  RateErrorModel error_model;
  error_model.SetRandomVariable (uv);
  error_model.SetUnit (RateErrorModel::ERROR_UNIT_PACKET);
  error_model.SetRate (error_p);

  PointToPointHelper pointToPoint;
  pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
  pointToPoint.SetChannelAttribute ("Delay", StringValue ("100ms"));
  pointToPoint.SetDeviceAttribute ("ReceiveErrorModel",
                                   PointerValue (&error_model));

  NetDeviceContainer devices;
  devices = pointToPoint.Install (nodes);

  QuicHelper stack;
  stack.InstallQuic (nodes);

  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");

  Ipv4InterfaceContainer interfaces = address.Assign (devices);

  ApplicationContainer clientApps;
  ApplicationContainer serverApps;

  BulkSendHelper ftp ("ns3::QuicSocketFactory", InetSocketAddress (interfaces.GetAddress (0)));

  ftp.SetAttribute("Remote", AddressValue(InetSocketAddress (interfaces.GetAddress (1), 9)));
  ftp.SetAttribute("SendSize", UintegerValue(1024));
  ftp.SetAttribute("MaxBytes", UintegerValue(1000000));

  PacketSinkHelper sinkHelper ("ns3::QuicSocketFactory", InetSocketAddress (interfaces.GetAddress (1), 9));
  sinkHelper.SetAttribute("Protocol", TypeIdValue(QuicSocketFactory::GetTypeId()));

  serverApps.Add (sinkHelper.Install (nodes.Get (1)));

  serverApps.Start (Seconds (1.0));
  serverApps.Stop (Seconds (1200.0));

  clientApps.Add (ftp.Install (nodes.Get (0)));
  
  clientApps.Start (Seconds (2.0));
  clientApps.Stop (Seconds (1200.0));

  Packet::EnablePrinting ();
  Packet::EnableChecking ();

  // AnimationInterface anim ("quic-tester-anim.xml");

  // anim.SetConstantPosition(nodes.Get(0), 50.0, 0.0);
  // anim.SetConstantPosition(nodes.Get(1), 75.0, 0.0);

  // AsciiTraceHelper ascii;

  // pointToPoint.EnableAsciiAll(ascii.CreateFileStream("quictest.tr"));
  // pointToPoint.EnablePcapAll ("quictest");

  stack.EnablePcapIpv4All ("quictestRealBulksend");
  // stack.EnableAsciiIpv4All ("quictestIpv4PatchedV2");

  std::cout << "\n\n#################### STARTING RUN ####################\n\n";
  Simulator::Run ();
  std::cout
      << "\n\n#################### RUN FINISHED ####################\n\n\n";
  Simulator::Destroy ();

  std::cout
      << "\n\n#################### SIMULATION END ####################\n\n\n";
  return 0;
}
