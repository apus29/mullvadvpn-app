#pragma once

#include "ifirewallrule.h"
#include "libwfp/ipaddress.h"
#include <string>
#include <cstdint>

namespace rules
{

class RestrictDns : public IFirewallRule
{
	uint16_t m_relayPort;

public:

	RestrictDns(const std::wstring &tunnelInterfaceAlias, const wfp::IpAddress v4DnsHost, std::unique_ptr<wfp::IpAddress> v6DnsHost,
		uint16_t relayPort);

	bool apply(IObjectInstaller &objectInstaller) override;

private:

	const std::wstring m_tunnelInterfaceAlias;
	const wfp::IpAddress m_v4DnsHost;
	const std::unique_ptr<wfp::IpAddress> m_v6DnsHost;

};

}
