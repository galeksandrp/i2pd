#include <cstdlib>
#include <string>
#include <boost/asio.hpp>

#include "util.h"
#include "Log.h"

#ifdef WIN32
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sysinfoapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <shlobj.h>

#ifdef _MSC_VER
#pragma comment(lib, "IPHLPAPI.lib")
#endif // _MSC_VER

#define MALLOC(x) HeapAlloc(GetProcessHeap(), 0, (x))
#define FREE(x) HeapFree(GetProcessHeap(), 0, (x))

// inet_pton exists Windows since Vista, but XP haven't that function!
// This function was written by Petar Korponai?. See http://stackoverflow.com/questions/15660203/inet-pton-identifier-not-found
int inet_pton_xp(int af, const char *src, void *dst)
{
	struct sockaddr_storage ss;
	int size = sizeof (ss);
	char src_copy[INET6_ADDRSTRLEN + 1];

	ZeroMemory (&ss, sizeof (ss));
	strncpy (src_copy, src, INET6_ADDRSTRLEN + 1);
	src_copy[INET6_ADDRSTRLEN] = 0;

	if (WSAStringToAddress (src_copy, af, NULL, (struct sockaddr *)&ss, &size) == 0)
	{
		switch (af)
		{
		case AF_INET:
			*(struct in_addr *)dst = ((struct sockaddr_in *)&ss)->sin_addr;
			return 1;
		case AF_INET6:
			*(struct in6_addr *)dst = ((struct sockaddr_in6 *)&ss)->sin6_addr;
			return 1;
		}
	}
	return 0;
}
#else /* !WIN32 => UNIX */
#include <sys/types.h>
#include <ifaddrs.h>
#endif

#define address_pair_v4(a,b) { boost::asio::ip::address_v4::from_string (a).to_ulong (), boost::asio::ip::address_v4::from_string (b).to_ulong () }
#define address_pair_v6(a,b) { boost::asio::ip::address_v6::from_string (a).to_bytes (), boost::asio::ip::address_v6::from_string (b).to_bytes () }

namespace i2p
{
namespace util
{
namespace net
{
#ifdef WIN32
	bool IsWindowsXPorLater()
	{
		OSVERSIONINFO osvi;

		ZeroMemory(&osvi, sizeof(OSVERSIONINFO));
		osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
		GetVersionEx(&osvi);

		if (osvi.dwMajorVersion <= 5)
			return true;
		else
			return false;
	}

	int GetMTUWindowsIpv4(sockaddr_in inputAddress, int fallback)
	{
		ULONG outBufLen = 0;
		PIP_ADAPTER_ADDRESSES pAddresses = nullptr;
		PIP_ADAPTER_ADDRESSES pCurrAddresses = nullptr;
		PIP_ADAPTER_UNICAST_ADDRESS pUnicast = nullptr;

		if(GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, pAddresses, &outBufLen)
			== ERROR_BUFFER_OVERFLOW)
		{
			FREE(pAddresses);
			pAddresses = (IP_ADAPTER_ADDRESSES*) MALLOC(outBufLen);
		}

		DWORD dwRetVal = GetAdaptersAddresses(
			AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, pAddresses, &outBufLen
		);

		if(dwRetVal != NO_ERROR)
		{
			LogPrint(eLogError, "NetIface: GetMTU(): enclosed GetAdaptersAddresses() call has failed");
			FREE(pAddresses);
			return fallback;
		}

		pCurrAddresses = pAddresses;
		while(pCurrAddresses)
		{
			PIP_ADAPTER_UNICAST_ADDRESS firstUnicastAddress = pCurrAddresses->FirstUnicastAddress;

			pUnicast = pCurrAddresses->FirstUnicastAddress;
			if(pUnicast == nullptr)
				LogPrint(eLogError, "NetIface: GetMTU(): not a unicast ipv4 address, this is not supported");

			for(int i = 0; pUnicast != nullptr; ++i)
			{
				LPSOCKADDR lpAddr = pUnicast->Address.lpSockaddr;
				sockaddr_in* localInterfaceAddress = (sockaddr_in*) lpAddr;
				if(localInterfaceAddress->sin_addr.S_un.S_addr == inputAddress.sin_addr.S_un.S_addr)
				{
					auto result = pAddresses->Mtu;
					FREE(pAddresses);
					return result;
				}
				pUnicast = pUnicast->Next;
			}
			pCurrAddresses = pCurrAddresses->Next;
		}

		LogPrint(eLogError, "NetIface: GetMTU(): no usable unicast ipv4 addresses found");
		FREE(pAddresses);
		return fallback;
	}

	int GetMTUWindowsIpv6(sockaddr_in6 inputAddress, int fallback)
	{
		ULONG outBufLen = 0;
		PIP_ADAPTER_ADDRESSES pAddresses = nullptr;
		PIP_ADAPTER_ADDRESSES pCurrAddresses = nullptr;
		PIP_ADAPTER_UNICAST_ADDRESS pUnicast = nullptr;

		if(GetAdaptersAddresses(AF_INET6, GAA_FLAG_INCLUDE_PREFIX, nullptr, pAddresses, &outBufLen)
			== ERROR_BUFFER_OVERFLOW)
		{
			FREE(pAddresses);
			pAddresses = (IP_ADAPTER_ADDRESSES*) MALLOC(outBufLen);
		}

		DWORD dwRetVal = GetAdaptersAddresses(
			AF_INET6, GAA_FLAG_INCLUDE_PREFIX, nullptr, pAddresses, &outBufLen
		);

		if(dwRetVal != NO_ERROR)
		{
			LogPrint(eLogError, "NetIface: GetMTU(): enclosed GetAdaptersAddresses() call has failed");
			FREE(pAddresses);
			return fallback;
		}

		bool found_address = false;
		pCurrAddresses = pAddresses;
		while(pCurrAddresses)
		{
			PIP_ADAPTER_UNICAST_ADDRESS firstUnicastAddress = pCurrAddresses->FirstUnicastAddress;
			pUnicast = pCurrAddresses->FirstUnicastAddress;
			if(pUnicast == nullptr)
				LogPrint(eLogError, "NetIface: GetMTU(): not a unicast ipv6 address, this is not supported");

			for(int i = 0; pUnicast != nullptr; ++i)
			{
				LPSOCKADDR lpAddr = pUnicast->Address.lpSockaddr;
				sockaddr_in6 *localInterfaceAddress = (sockaddr_in6*) lpAddr;

				for (int j = 0; j != 8; ++j)
				{
					if (localInterfaceAddress->sin6_addr.u.Word[j] != inputAddress.sin6_addr.u.Word[j])
						break;
					else
						found_address = true;
				}

				if (found_address)
				{
					auto result = pAddresses->Mtu;
					FREE(pAddresses);
					pAddresses = nullptr;
					return result;
				}
				pUnicast = pUnicast->Next;
			}

			pCurrAddresses = pCurrAddresses->Next;
		}

		LogPrint(eLogError, "NetIface: GetMTU(): no usable unicast ipv6 addresses found");
		FREE(pAddresses);
		return fallback;
	}

	int GetMTUWindows(const boost::asio::ip::address& localAddress, int fallback)
	{
#ifdef UNICODE
		string localAddress_temporary = localAddress.to_string();
		wstring localAddressUniversal(localAddress_temporary.begin(), localAddress_temporary.end());
#else
		std::string localAddressUniversal = localAddress.to_string();
#endif

		if (IsWindowsXPorLater())
		{
			#define inet_pton inet_pton_xp
		}

		if(localAddress.is_v4())
		{
			sockaddr_in inputAddress;
			inet_pton(AF_INET, localAddressUniversal.c_str(), &(inputAddress.sin_addr));
			return GetMTUWindowsIpv4(inputAddress, fallback);
		}
		else if(localAddress.is_v6())
		{
			sockaddr_in6 inputAddress;
			inet_pton(AF_INET6, localAddressUniversal.c_str(), &(inputAddress.sin6_addr));
			return GetMTUWindowsIpv6(inputAddress, fallback);
		} else {
			LogPrint(eLogError, "NetIface: GetMTU(): address family is not supported");
			return fallback;
		}
	}
#else // assume unix
	int GetMTUUnix(const boost::asio::ip::address& localAddress, int fallback)
	{
		ifaddrs* ifaddr, *ifa = nullptr;
		if(getifaddrs(&ifaddr) == -1)
		{
			LogPrint(eLogError, "NetIface: Can't call getifaddrs(): ", strerror(errno));
			return fallback;
		}

		int family = 0;
		// look for interface matching local address
		for(ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
		{
			if(!ifa->ifa_addr)
				continue;

			family = ifa->ifa_addr->sa_family;
			if(family == AF_INET && localAddress.is_v4())
			{
				sockaddr_in* sa = (sockaddr_in*) ifa->ifa_addr;
				if(!memcmp(&sa->sin_addr, localAddress.to_v4().to_bytes().data(), 4))
					break; // address matches
			}
			else if(family == AF_INET6 && localAddress.is_v6())
			{
				sockaddr_in6* sa = (sockaddr_in6*) ifa->ifa_addr;
				if(!memcmp(&sa->sin6_addr, localAddress.to_v6().to_bytes().data(), 16))
					break; // address matches
			}
		}
		int mtu = fallback;
		if(ifa && family)
		{ // interface found?
			int fd = socket(family, SOCK_DGRAM, 0);
			if(fd > 0)
			{
				ifreq ifr;
				strncpy(ifr.ifr_name, ifa->ifa_name, IFNAMSIZ); // set interface for query
				if(ioctl(fd, SIOCGIFMTU, &ifr) >= 0)
					mtu = ifr.ifr_mtu; // MTU
				else
					LogPrint (eLogError, "NetIface: Failed to run ioctl: ", strerror(errno));
				close(fd);
			}
			else
				LogPrint(eLogError, "NetIface: Failed to create datagram socket");
		}
		else
			LogPrint(eLogWarning, "NetIface: interface for local address", localAddress.to_string(), " not found");
		freeifaddrs(ifaddr);

		return mtu;
	}
#endif // WIN32

	int GetMTU(const boost::asio::ip::address& localAddress)
	{
		const int fallback = 576; // fallback MTU

#ifdef WIN32
		return GetMTUWindows(localAddress, fallback);
#else
		return GetMTUUnix(localAddress, fallback);
#endif
		return fallback;
	}

	const boost::asio::ip::address GetInterfaceAddress(const std::string & ifname, bool ipv6)
	{
#ifdef WIN32
		LogPrint(eLogError, "NetIface: cannot get address by interface name, not implemented on WIN32");
		if(ipv6)
			return boost::asio::ip::address::from_string("::1");
		else
			return boost::asio::ip::address::from_string("127.0.0.1");
#else
		int af = (ipv6 ? AF_INET6 : AF_INET);
		ifaddrs * addrs = nullptr;
		if(getifaddrs(&addrs) == 0)
		{
			// got ifaddrs
			ifaddrs * cur = addrs;
			while(cur)
			{
				std::string cur_ifname(cur->ifa_name);
				if (cur_ifname == ifname && cur->ifa_addr && cur->ifa_addr->sa_family == af)
				{
					// match
					char * addr = new char[INET6_ADDRSTRLEN];
					bzero(addr, INET6_ADDRSTRLEN);
					if(af == AF_INET)
						inet_ntop(af, &((sockaddr_in *)cur->ifa_addr)->sin_addr, addr, INET6_ADDRSTRLEN);
					else
						inet_ntop(af, &((sockaddr_in6 *)cur->ifa_addr)->sin6_addr, addr, INET6_ADDRSTRLEN);
					freeifaddrs(addrs);
					std::string cur_ifaddr(addr);
					delete[] addr;
					return boost::asio::ip::address::from_string(cur_ifaddr);
				}
				cur = cur->ifa_next;
			}
		}
		if(addrs) freeifaddrs(addrs);
		std::string fallback;
		if(ipv6)
		{
			fallback = "::1";
			LogPrint(eLogWarning, "NetIface: cannot find ipv6 address for interface ", ifname);
		} else {
			fallback = "127.0.0.1";
			LogPrint(eLogWarning, "NetIface: cannot find ipv4 address for interface ", ifname);
		}
		return boost::asio::ip::address::from_string(fallback);
#endif
	}

	bool IsInReservedRange(const boost::asio::ip::address& host) {
		// https://en.wikipedia.org/wiki/Reserved_IP_addresses
		if(host.is_v4())
		{
			static const std::vector< std::pair<uint32_t, uint32_t> > reservedIPv4Ranges = {
				address_pair_v4("0.0.0.0",      "0.255.255.255"),
				address_pair_v4("10.0.0.0",     "10.255.255.255"),
				address_pair_v4("100.64.0.0",   "100.127.255.255"),
				address_pair_v4("127.0.0.0",    "127.255.255.255"),
				address_pair_v4("169.254.0.0",  "169.254.255.255"),
				address_pair_v4("172.16.0.0",   "172.31.255.255"),
				address_pair_v4("192.0.0.0",    "192.0.0.255"),
				address_pair_v4("192.0.2.0",    "192.0.2.255"),
				address_pair_v4("192.88.99.0",  "192.88.99.255"),
				address_pair_v4("192.168.0.0",  "192.168.255.255"),
				address_pair_v4("198.18.0.0",   "192.19.255.255"),
				address_pair_v4("198.51.100.0", "198.51.100.255"),
				address_pair_v4("203.0.113.0",  "203.0.113.255"),
				address_pair_v4("224.0.0.0",    "255.255.255.255")
			};

			uint32_t ipv4_address = host.to_v4 ().to_ulong ();
			for(const auto& it : reservedIPv4Ranges) {
				if (ipv4_address >= it.first && ipv4_address <= it.second)
					return true;
			}
		}
		if(host.is_v6())
		{
			static const std::vector< std::pair<boost::asio::ip::address_v6::bytes_type, boost::asio::ip::address_v6::bytes_type> > reservedIPv6Ranges = {
				address_pair_v6("2001:db8::", "2001:db8:ffff:ffff:ffff:ffff:ffff:ffff"),
				address_pair_v6("fc00::",     "fdff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"),
				address_pair_v6("fe80::",     "febf:ffff:ffff:ffff:ffff:ffff:ffff:ffff")
			};

			boost::asio::ip::address_v6::bytes_type ipv6_address = host.to_v6 ().to_bytes ();
			for(const auto& it : reservedIPv6Ranges) {
				if (ipv6_address >= it.first && ipv6_address <= it.second)
					return true;
			}
		}
		return false;
	}
} // net
} // util
} // i2p
