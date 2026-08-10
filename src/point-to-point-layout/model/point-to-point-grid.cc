/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Author: Josh Pelkey <jpelkey@gatech.edu>
 */

// Implement an object to create a grid topology.

#include "point-to-point-grid.h"

#include "ns3/constant-position-mobility-model.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv6-address-generator.h"
#include "ns3/log.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/string.h"
#include "ns3/vector.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("PointToPointGridHelper");

PointToPointGridHelper::PointToPointGridHelper(uint32_t nRows,
                                               uint32_t nCols,
                                               PointToPointHelper pointToPoint)
    : PointToPointGridHelper(nRows, nCols, 0, pointToPoint)
{
}

PointToPointGridHelper::PointToPointGridHelper(uint32_t nRows,
                                               uint32_t nCols,
                                               uint32_t nDiags,
                                               PointToPointHelper pointToPoint)
    : m_xSize(nCols),
      m_ySize(nRows)
{
    // Bounds check
    if (m_xSize < 1 || m_ySize < 1 || (m_xSize < 2 && m_ySize < 2))
    {
        NS_FATAL_ERROR("Need more nodes for grid.");
    }
    if (nDiags > 2)
    {
        NS_FATAL_ERROR("PointToPointGridHelper supports 0, 1, or 2 diagonal directions.");
    }

    InternetStackHelper stack;

    for (uint32_t y = 0; y < nRows; ++y)
    {
        NodeContainer rowNodes;
        NetDeviceContainer rowDevices;
        NetDeviceContainer colDevices;
        NetDeviceContainer diagDevices;

        for (uint32_t x = 0; x < nCols; ++x)
        {
            rowNodes.Create(1);

            // install p2p links across the row
            if (x > 0)
            {
                rowDevices.Add(pointToPoint.Install(rowNodes.Get(x - 1), rowNodes.Get(x)));
            }

            // install vertical p2p links
            if (y > 0)
            {
                colDevices.Add(pointToPoint.Install((m_nodes.at(y - 1)).Get(x), rowNodes.Get(x)));
            }

            // Install top-left to bottom-right diagonal links (backslash).
            if (nDiags >= 1 && x > 0 && y > 0)
            {
                diagDevices.Add(
                    pointToPoint.Install((m_nodes.at(y - 1)).Get(x - 1), rowNodes.Get(x)));
            }

            // Install top-right to bottom-left diagonal links (slash).
            if (nDiags >= 2 && x + 1 < nCols && y > 0)
            {
                diagDevices.Add(
                    pointToPoint.Install((m_nodes.at(y - 1)).Get(x + 1), rowNodes.Get(x)));
            }
        }

        m_nodes.push_back(rowNodes);
        m_rowDevices.push_back(rowDevices);
        m_diagDevices.push_back(diagDevices);

        if (y > 0)
        {
            m_colDevices.push_back(colDevices);
        }
    }
}

PointToPointGridHelper::~PointToPointGridHelper()
{
}

void
PointToPointGridHelper::InstallStack(InternetStackHelper stack)
{
    for (uint32_t i = 0; i < m_nodes.size(); ++i)
    {
        NodeContainer rowNodes = m_nodes[i];
        for (uint32_t j = 0; j < rowNodes.GetN(); ++j)
        {
            stack.Install(rowNodes.Get(j));
        }
    }
}

void
PointToPointGridHelper::AssignIpv4Addresses(Ipv4AddressHelper rowIp, Ipv4AddressHelper colIp)
{
    AssignIpv4Addresses(rowIp, colIp, Ipv4AddressHelper());
}

void
PointToPointGridHelper::AssignIpv4Addresses(Ipv4AddressHelper rowIp,
                                            Ipv4AddressHelper colIp,
                                            Ipv4AddressHelper diagIp)
{
    // Assign addresses to all row devices in the grid.
    // These devices are stored in a vector.  Each row
    // of the grid has all the row devices in one entry
    // of the vector.  These entries come in pairs.
    for (uint32_t i = 0; i < m_rowDevices.size(); ++i)
    {
        Ipv4InterfaceContainer rowInterfaces;
        NetDeviceContainer rowContainer = m_rowDevices[i];
        for (uint32_t j = 0; j < rowContainer.GetN(); j += 2)
        {
            rowInterfaces.Add(rowIp.Assign(rowContainer.Get(j)));
            rowInterfaces.Add(rowIp.Assign(rowContainer.Get(j + 1)));
            rowIp.NewNetwork();
        }
        m_rowInterfaces.push_back(rowInterfaces);
    }

    // Assign addresses to all col devices in the grid.
    // These devices are stored in a vector.  Each col
    // of the grid has all the col devices in one entry
    // of the vector.  These entries come in pairs.
    for (uint32_t i = 0; i < m_colDevices.size(); ++i)
    {
        Ipv4InterfaceContainer colInterfaces;
        NetDeviceContainer colContainer = m_colDevices[i];
        for (uint32_t j = 0; j < colContainer.GetN(); j += 2)
        {
            colInterfaces.Add(colIp.Assign(colContainer.Get(j)));
            colInterfaces.Add(colIp.Assign(colContainer.Get(j + 1)));
            colIp.NewNetwork();
        }
        m_colInterfaces.push_back(colInterfaces);
    }

    // Assign IPv4 addresses to all diagonal point-to-point links.
    // Each entry in m_diagDevices contains the diagonal devices created for one grid row.
    for (uint32_t i = 0; i < m_diagDevices.size(); ++i)
    {
        Ipv4InterfaceContainer diagInterfaces;
        NetDeviceContainer diagContainer = m_diagDevices[i];

        // PointToPointHelper::Install() creates two devices per link, so process the
        // container in pairs and place both endpoints in the same IPv4 subnet.
        for (uint32_t j = 0; j < diagContainer.GetN(); j += 2)
        {
            diagInterfaces.Add(diagIp.Assign(diagContainer.Get(j)));
            diagInterfaces.Add(diagIp.Assign(diagContainer.Get(j + 1)));

            // Use a new subnet for the next diagonal point-to-point link.
            diagIp.NewNetwork();
        }

        // Retain the assigned interfaces in the same row-based layout as m_diagDevices.
        m_diagInterfaces.push_back(diagInterfaces);
    }
}

void
PointToPointGridHelper::AssignIpv6Addresses(Ipv6Address addrBase, Ipv6Prefix prefix)
{
    AssignIpv6Addresses(addrBase, prefix, prefix);
}

void
PointToPointGridHelper::AssignIpv6Addresses(Ipv6Address addrBase,
                                            Ipv6Prefix prefix,
                                            Ipv6Prefix diagPrefix)
{
    Ipv6AddressGenerator::Init(addrBase, prefix);
    Ipv6Address v6network;
    Ipv6AddressHelper addrHelper;

    // Assign addresses to all row devices in the grid.
    // These devices are stored in a vector.  Each row
    // of the grid has all the row devices in one entry
    // of the vector.  These entries come in pairs.
    for (uint32_t i = 0; i < m_rowDevices.size(); ++i)
    {
        Ipv6InterfaceContainer rowInterfaces;
        NetDeviceContainer rowContainer = m_rowDevices[i];
        for (uint32_t j = 0; j < rowContainer.GetN(); j += 2)
        {
            v6network = Ipv6AddressGenerator::GetNetwork(prefix);
            addrHelper.SetBase(v6network, prefix);
            Ipv6InterfaceContainer ic = addrHelper.Assign(rowContainer.Get(j));
            rowInterfaces.Add(ic);
            ic = addrHelper.Assign(rowContainer.Get(j + 1));
            rowInterfaces.Add(ic);
            Ipv6AddressGenerator::NextNetwork(prefix);
        }
        m_rowInterfaces6.push_back(rowInterfaces);
    }

    // Assign addresses to all col devices in the grid.
    // These devices are stored in a vector.  Each col
    // of the grid has all the col devices in one entry
    // of the vector.  These entries come in pairs.
    for (uint32_t i = 0; i < m_colDevices.size(); ++i)
    {
        Ipv6InterfaceContainer colInterfaces;
        NetDeviceContainer colContainer = m_colDevices[i];
        for (uint32_t j = 0; j < colContainer.GetN(); j += 2)
        {
            v6network = Ipv6AddressGenerator::GetNetwork(prefix);
            addrHelper.SetBase(v6network, prefix);
            Ipv6InterfaceContainer ic = addrHelper.Assign(colContainer.Get(j));
            colInterfaces.Add(ic);
            ic = addrHelper.Assign(colContainer.Get(j + 1));
            colInterfaces.Add(ic);
            Ipv6AddressGenerator::NextNetwork(prefix);
        }
        m_colInterfaces6.push_back(colInterfaces);
    }

    // Assign addresses to all diagonal point-to-point links.
    for (uint32_t i = 0; i < m_diagDevices.size(); ++i)
    {
        Ipv6InterfaceContainer diagInterfaces;
        NetDeviceContainer diagContainer = m_diagDevices[i];
        for (uint32_t j = 0; j < diagContainer.GetN(); j += 2)
        {
            v6network = Ipv6AddressGenerator::GetNetwork(diagPrefix);
            addrHelper.SetBase(v6network, diagPrefix);
            Ipv6InterfaceContainer ic = addrHelper.Assign(diagContainer.Get(j));
            diagInterfaces.Add(ic);
            ic = addrHelper.Assign(diagContainer.Get(j + 1));
            diagInterfaces.Add(ic);
            Ipv6AddressGenerator::NextNetwork(diagPrefix);
        }
        m_diagInterfaces6.push_back(diagInterfaces);
    }
}

void
PointToPointGridHelper::BoundingBox(double ulx, double uly, double lrx, double lry)
{
    double xDist;
    double yDist;
    if (lrx > ulx)
    {
        xDist = lrx - ulx;
    }
    else
    {
        xDist = ulx - lrx;
    }
    if (lry > uly)
    {
        yDist = lry - uly;
    }
    else
    {
        yDist = uly - lry;
    }
    double xAdder = xDist / m_xSize;
    double yAdder = yDist / m_ySize;
    double yLoc = yDist / 2;
    for (uint32_t i = 0; i < m_ySize; ++i)
    {
        double xLoc = xDist / 2;
        for (uint32_t j = 0; j < m_xSize; ++j)
        {
            Ptr<Node> node = GetNode(i, j);
            Ptr<ConstantPositionMobilityModel> loc =
                node->GetObject<ConstantPositionMobilityModel>();
            if (!loc)
            {
                loc = CreateObject<ConstantPositionMobilityModel>();
                node->AggregateObject(loc);
            }
            Vector locVec(xLoc, yLoc, 0);
            loc->SetPosition(locVec);

            xLoc += xAdder;
        }
        yLoc += yAdder;
    }
}

Ptr<Node>
PointToPointGridHelper::GetNode(uint32_t row, uint32_t col)
{
    if (row > m_nodes.size() - 1 || col > m_nodes.at(row).GetN() - 1)
    {
        NS_FATAL_ERROR("Index out of bounds in PointToPointGridHelper::GetNode.");
    }

    return (m_nodes.at(row)).Get(col);
}

Ipv4Address
PointToPointGridHelper::GetIpv4Address(uint32_t row, uint32_t col)
{
    if (row > m_nodes.size() - 1 || col > m_nodes.at(row).GetN() - 1)
    {
        NS_FATAL_ERROR("Index out of bounds in PointToPointGridHelper::GetIpv4Address.");
    }

    // Right now this just gets one of the addresses of the
    // specified node.  The exact device can't be specified.
    // If you picture the grid, the address returned is the
    // address of the left (row) device of all nodes, with
    // the exception of the left-most nodes in the grid;
    // in which case the right (row) device address is
    // returned
    if (col == 0)
    {
        return (m_rowInterfaces.at(row)).GetAddress(0);
    }
    else
    {
        return (m_rowInterfaces.at(row)).GetAddress((2 * col) - 1);
    }
}

Ipv6Address
PointToPointGridHelper::GetIpv6Address(uint32_t row, uint32_t col)
{
    if (row > m_nodes.size() - 1 || col > m_nodes.at(row).GetN() - 1)
    {
        NS_FATAL_ERROR("Index out of bounds in PointToPointGridHelper::GetIpv6Address.");
    }

    // Right now this just gets one of the addresses of the
    // specified node.  The exact device can't be specified.
    // If you picture the grid, the address returned is the
    // address of the left (row) device of all nodes, with
    // the exception of the left-most nodes in the grid;
    // in which case the right (row) device address is
    // returned
    if (col == 0)
    {
        return (m_rowInterfaces6.at(row)).GetAddress(0, 1);
    }
    else
    {
        return (m_rowInterfaces6.at(row)).GetAddress((2 * col) - 1, 1);
    }
}

} // namespace ns3
