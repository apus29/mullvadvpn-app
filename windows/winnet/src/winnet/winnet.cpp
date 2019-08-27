#include "stdafx.h"
#include "winnet.h"
#include "NetworkInterfaces.h"
#include "interfaceutils.h"
#include "../../shared/logsinkadapter.h"
#include "libcommon/error.h"
#include "libcommon/network.h"
#include "netmonitor.h"
#include "routemanager.h"
#include <cstdint>
#include <stdexcept>
#include <memory>
#include <optional>

using namespace routemanager;

namespace
{

NetMonitor *g_NetMonitor = nullptr;
RouteManager *g_RouteManager = nullptr;

Network ConvertNetwork(const WINNET_IPNETWORK &in)
{
	//
	// Convert WINNET_IPNETWORK into Network aka IP_ADDRESS_PREFIX
	//

	Network out{ 0 };

	out.PrefixLength = in.prefix;

	switch (in.type)
	{
		case WINNET_IP_TYPE::IPV4:
		{
			out.Prefix.si_family = AF_INET;
			out.Prefix.Ipv4.sin_family = AF_INET;
			out.Prefix.Ipv4.sin_addr.s_addr = common::network::LiteralAddressToNetwork(in.bytes);

			break;
		}
		case WINNET_IP_TYPE::IPV6:
		{
			out.Prefix.si_family = AF_INET6;
			out.Prefix.Ipv6.sin6_family = AF_INET6;
			memcpy(out.Prefix.Ipv6.sin6_addr.u.Byte, in.bytes, 16);

			break;
		}
		default:
		{
			throw std::logic_error("Missing case handler in switch clause");
		}
	}

	return out;
}

std::optional<Node> ConvertNode(const WINNET_NODE *in)
{
	if (nullptr == in)
	{
		return {};
	}

	if (nullptr != in->deviceName)
	{
		// This node is represented by device name.
		return Node(in->deviceName);
	}

	if (nullptr == in->gateway)
	{
		throw std::runtime_error("Invalid 'WINNET_NODE' definition");
	}

	//
	// This node is represented by gateway.
	//

	NodeAddress gateway{ 0 };

	switch (in->gateway->type)
	{
		case WINNET_IP_TYPE::IPV4:
		{
			gateway.si_family = AF_INET;
			gateway.Ipv4.sin_family = AF_INET;
			gateway.Ipv4.sin_addr.s_addr = common::network::LiteralAddressToNetwork(in->gateway->bytes);

			break;
		}
		case WINNET_IP_TYPE::IPV6:
		{
			gateway.si_family = AF_INET6;
			gateway.Ipv6.sin6_family = AF_INET6;
			memcpy(&gateway.Ipv6.sin6_addr.u.Byte, in->gateway->bytes, 16);

			break;
		}
		default:
		{
			throw std::logic_error("Invalid gateway type specifier in 'WINNET_NODE' definition");
		}
	}

	return Node(gateway);
}

std::vector<Route> ConvertRoutes(const WINNET_ROUTE *routes, uint32_t numRoutes)
{
	std::vector<Route> out;

	out.reserve(numRoutes);

	for (size_t i = 0; i < numRoutes; ++i)
	{
		out.emplace_back(Route
		{
			ConvertNetwork(routes[i].network),
			ConvertNode(routes[i].node)
		});
	}

	return out;
}

void UnwindAndLog(MullvadLogSink logSink, void *logSinkContext, const std::exception &err)
{
	if (nullptr == logSink)
	{
		return;
	}

	auto logger = std::make_shared<shared::LogSinkAdapter>(logSink, logSinkContext);

	common::error::UnwindException(err, logger);
}

} //anonymous namespace

extern "C"
WINNET_LINKAGE
WINNET_ETM_STATUS
WINNET_API
WinNet_EnsureTopMetric(
	const wchar_t *deviceAlias,
	MullvadLogSink logSink,
	void *logSinkContext
)
{
	try
	{
		NetworkInterfaces interfaces;
		bool metrics_set = interfaces.SetTopMetricForInterfacesByAlias(deviceAlias);
		return metrics_set ? WINNET_ETM_STATUS_METRIC_SET : WINNET_ETM_STATUS_METRIC_NO_CHANGE;
	}
	catch (const std::exception &err)
	{
		UnwindAndLog(logSink, logSinkContext, err);
		return WINNET_ETM_STATUS_FAILURE;
	}
	catch (...)
	{
		return WINNET_ETM_STATUS_FAILURE;
	}
};

extern "C"
WINNET_LINKAGE
WINNET_GTII_STATUS
WINNET_API
WinNet_GetTapInterfaceIpv6Status(
	MullvadLogSink logSink,
	void *logSinkContext
)
{
	try
	{
		MIB_IPINTERFACE_ROW iface = { 0 };

		iface.InterfaceLuid = NetworkInterfaces::GetInterfaceLuid(InterfaceUtils::GetTapInterfaceAlias());
		iface.Family = AF_INET6;

		const auto status = GetIpInterfaceEntry(&iface);

		if (NO_ERROR == status)
		{
			return WINNET_GTII_STATUS_ENABLED;
		}

		if (ERROR_NOT_FOUND == status)
		{
			return WINNET_GTII_STATUS_DISABLED;
		}

		common::error::Throw("Resolve TAP IPv6 interface", status);
	}
	catch (const std::exception &err)
	{
		UnwindAndLog(logSink, logSinkContext, err);
		return WINNET_GTII_STATUS_FAILURE;
	}
	catch (...)
	{
		return WINNET_GTII_STATUS_FAILURE;
	}
}

extern "C"
WINNET_LINKAGE
bool
WINNET_API
WinNet_GetTapInterfaceAlias(
	wchar_t **alias,
	MullvadLogSink logSink,
	void *logSinkContext
)
{
	try
	{
		const auto currentAlias = InterfaceUtils::GetTapInterfaceAlias();

		auto stringBuffer = new wchar_t[currentAlias.size() + 1];
		wcscpy(stringBuffer, currentAlias.c_str());

		*alias = stringBuffer;

		return true;
	}
	catch (const std::exception &err)
	{
		UnwindAndLog(logSink, logSinkContext, err);
		return false;
	}
	catch (...)
	{
		return false;
	}
}

extern "C"
WINNET_LINKAGE
void
WINNET_API
WinNet_ReleaseString(
	wchar_t *str
)
{
	try
	{
		delete[] str;
	}
	catch (...)
	{
	}
}

extern "C"
WINNET_LINKAGE
bool
WINNET_API
WinNet_ActivateConnectivityMonitor(
	WinNetConnectivityMonitorCallback callback,
	void *callbackContext,
	bool *currentConnectivity,
	MullvadLogSink logSink,
	void *logSinkContext
)
{
	try
	{
		if (nullptr != g_NetMonitor)
		{
			throw std::runtime_error("Cannot activate connectivity monitor twice");
		}

		auto forwarder = [callback, callbackContext](bool connected)
		{
			callback(connected, callbackContext);
		};

		bool connected = false;

		auto logger = std::make_shared<shared::LogSinkAdapter>(logSink, logSinkContext);

		g_NetMonitor = new NetMonitor(logger, forwarder, connected);

		if (nullptr != currentConnectivity)
		{
			*currentConnectivity = connected;
		}

		return true;
	}
	catch (const std::exception &err)
	{
		UnwindAndLog(logSink, logSinkContext, err);
		return false;
	}
	catch (...)
	{
		return false;
	}
}

extern "C"
WINNET_LINKAGE
void
WINNET_API
WinNet_DeactivateConnectivityMonitor(
)
{
	try
	{
		delete g_NetMonitor;
		g_NetMonitor = nullptr;
	}
	catch (...)
	{
	}
}

extern "C"
WINNET_LINKAGE
WINNET_CC_STATUS
WINNET_API
WinNet_CheckConnectivity(
	MullvadLogSink logSink,
	void *logSinkContext
)
{
	try
	{
		return (NetMonitor::CheckConnectivity(std::make_shared<shared::LogSinkAdapter>(logSink, logSinkContext))
			? WINNET_CC_STATUS_CONNECTED : WINNET_CC_STATUS_NOT_CONNECTED);
	}
	catch (const std::exception &err)
	{
		UnwindAndLog(logSink, logSinkContext, err);
		return WINNET_CC_STATUS_CONNECTIVITY_UNKNOWN;
	}
	catch (...)
	{
		return WINNET_CC_STATUS_CONNECTIVITY_UNKNOWN;
	}
}

extern "C"
WINNET_LINKAGE
bool
WINNET_API
WinNet_ActivateRouteManager(
	const WINNET_ROUTE *routes,
	uint32_t numRoutes,
	MullvadLogSink logSink,
	void *logSinkContext
)
{
	try
	{
		if (nullptr != g_RouteManager)
		{
			throw std::runtime_error("Cannot activate route manager twice");
		}

		g_RouteManager = new RouteManager;
		g_RouteManager->addRoutes(ConvertRoutes(routes, numRoutes));

		return true;
	}
	catch (const std::exception &err)
	{
		UnwindAndLog(logSink, logSinkContext, err);
		return false;
	}
	catch (...)
	{
		return false;
	}
}

extern "C"
WINNET_LINKAGE
void
WINNET_API
WinNet_DeactivateRouteManager(
)
{
	try
	{
		delete g_RouteManager;
		g_RouteManager = nullptr;
	}
	catch (...)
	{
	}
}
