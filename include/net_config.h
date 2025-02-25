/**
 * @file net_config.h
 * @brief CycloneTCP configuration file
 *
 * @section License
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2010-2022 Oryx Embedded SARL. All rights reserved.
 *
 * This file is part of CycloneTCP Open.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * @author Oryx Embedded SARL (www.oryx-embedded.com)
 * @version 2.2.0
 **/

#ifndef _NET_CONFIG_H
#define _NET_CONFIG_H

#include "settings.h"
#include "toniebox_state_type.h"
#include "pcaplog_types.h"

#define TONIE_AUTH_TOKEN_LENGTH 32
#define JWT_AUTH_TOKEN_LENGTH 32

typedef struct
{
    pcaplog_t pcap_data;
} http_client_private_t;

typedef struct
{
    settings_t *settings;
    settings_t *settingsNoOverlay;
    toniebox_state_t *state;
    bool skip_taf_header;
} client_ctx_t;

typedef struct
{
    uint8_t authentication_token[TONIE_AUTH_TOKEN_LENGTH];
    client_ctx_t client_ctx;
    bool_t authenticated;
    pcaplog_t pcap_data;
    bool api_access_only;
} http_connection_private_t;

#define CONTENT_LENGTH_MAX (INT32_MAX)
#define TONIE_HEADER_LENGTH 0x1000
#define TONIE_LENGTH_MAX (CONTENT_LENGTH_MAX - TONIE_HEADER_LENGTH)

#define HTTP_CLIENT_PRIVATE_CONTEXT \
    const char *serverName;         \
    http_client_private_t private;  \
    void *sourceCtx;

#define HTTP_SERVER_DIGEST_AUTH_SUPPORT ENABLED
#define HTTP_SERVER_PRIVATE_CONTEXT http_connection_private_t private;
#define HTTP_SERVER_PERSISTENT_CONN_SUPPORT ENABLED

#define MQTT_CLIENT_DEFAULT_TIMEOUT 500

#define MQTT_CLIENT_SUPPORT ENABLED
#define MQTT_CLIENT_TLS_SUPPORT ENABLED
#define MQTT_CLIENT_MAX_PASSWORD_LEN 64
#define MQTT_CLIENT_MAX_USERNAME_LEN 64
#define MQTT_CLIENT_MAX_WILL_TOPIC_LEN 64

// Trace level for TCP/IP stack debugging
#define MEM_TRACE_LEVEL TRACE_LEVEL_INFO
#define NIC_TRACE_LEVEL TRACE_LEVEL_INFO
#define ETH_TRACE_LEVEL TRACE_LEVEL_OFF
#define LLDP_TRACE_LEVEL TRACE_LEVEL_OFF
#define ARP_TRACE_LEVEL TRACE_LEVEL_OFF
#define IP_TRACE_LEVEL TRACE_LEVEL_OFF
#define IPV4_TRACE_LEVEL TRACE_LEVEL_OFF
#define IPV6_TRACE_LEVEL TRACE_LEVEL_OFF
#define ICMP_TRACE_LEVEL TRACE_LEVEL_OFF
#define IGMP_TRACE_LEVEL TRACE_LEVEL_OFF
#define ICMPV6_TRACE_LEVEL TRACE_LEVEL_OFF
#define MLD_TRACE_LEVEL TRACE_LEVEL_OFF
#define NDP_TRACE_LEVEL TRACE_LEVEL_OFF
#define UDP_TRACE_LEVEL TRACE_LEVEL_OFF
#define TCP_TRACE_LEVEL TRACE_LEVEL_OFF
#define SOCKET_TRACE_LEVEL TRACE_LEVEL_OFF
#define RAW_SOCKET_TRACE_LEVEL TRACE_LEVEL_OFF
#define BSD_SOCKET_TRACE_LEVEL TRACE_LEVEL_OFF
#define WEB_SOCKET_TRACE_LEVEL TRACE_LEVEL_OFF
#define AUTO_IP_TRACE_LEVEL TRACE_LEVEL_INFO
#define SLAAC_TRACE_LEVEL TRACE_LEVEL_INFO
#define DHCP_TRACE_LEVEL TRACE_LEVEL_INFO
#define DHCPV6_TRACE_LEVEL TRACE_LEVEL_INFO
#define DNS_TRACE_LEVEL TRACE_LEVEL_INFO
#define MDNS_TRACE_LEVEL TRACE_LEVEL_OFF
#define NBNS_TRACE_LEVEL TRACE_LEVEL_OFF
#define LLMNR_TRACE_LEVEL TRACE_LEVEL_OFF
#define ECHO_TRACE_LEVEL TRACE_LEVEL_INFO
#define COAP_TRACE_LEVEL TRACE_LEVEL_INFO
#define FTP_TRACE_LEVEL TRACE_LEVEL_INFO
#define HTTP_TRACE_LEVEL TRACE_LEVEL_WARNING
#define MQTT_TRACE_LEVEL TRACE_LEVEL_WARNING
#define MQTT_SN_TRACE_LEVEL TRACE_LEVEL_INFO
#define SMTP_TRACE_LEVEL TRACE_LEVEL_INFO
#define SNMP_TRACE_LEVEL TRACE_LEVEL_INFO
#define SNTP_TRACE_LEVEL TRACE_LEVEL_INFO
#define TFTP_TRACE_LEVEL TRACE_LEVEL_INFO
#define MODBUS_TRACE_LEVEL TRACE_LEVEL_INFO

// Number of network adapters
#define NET_INTERFACE_COUNT 2

// Size of the MAC address filter
#define MAC_ADDR_FILTER_SIZE 12

// IPv4 support
#define IPV4_SUPPORT ENABLED
// Size of the IPv4 multicast filter
#define IPV4_MULTICAST_FILTER_SIZE 4

// IPv4 fragmentation support
#define IPV4_FRAG_SUPPORT ENABLED
// Maximum number of fragmented packets the host will accept
// and hold in the reassembly queue simultaneously
#define IPV4_MAX_FRAG_DATAGRAMS 4
// Maximum datagram size the host will accept when reassembling fragments
#define IPV4_MAX_FRAG_DATAGRAM_SIZE 8192

// Size of ARP cache
#define ARP_CACHE_SIZE 8
// Maximum number of packets waiting for address resolution to complete
#define ARP_MAX_PENDING_PACKETS 2

// IGMP host support
#define IGMP_HOST_SUPPORT ENABLED
// DHCP server support
#define DHCP_SERVER_SUPPORT ENABLED

// IPv6 support
#define IPV6_SUPPORT ENABLED
// Size of the IPv6 multicast filter
#define IPV6_MULTICAST_FILTER_SIZE 8

// IPv6 fragmentation support
#define IPV6_FRAG_SUPPORT ENABLED
// Maximum number of fragmented packets the host will accept
// and hold in the reassembly queue simultaneously
#define IPV6_MAX_FRAG_DATAGRAMS 4
// Maximum datagram size the host will accept when reassembling fragments
#define IPV6_MAX_FRAG_DATAGRAM_SIZE 8192

// MLD support
#define MLD_SUPPORT ENABLED
// RA service support
#define NDP_ROUTER_ADV_SUPPORT ENABLED

// Neighbor cache size
#define NDP_NEIGHBOR_CACHE_SIZE 8
// Destination cache size
#define NDP_DEST_CACHE_SIZE 8
// Maximum number of packets waiting for address resolution to complete
#define NDP_MAX_PENDING_PACKETS 2

// TCP support
#define TCP_SUPPORT ENABLED
// Default buffer size for transmission
#define TCP_DEFAULT_TX_BUFFER_SIZE (1430 * 2)
// Default buffer size for reception
#define TCP_DEFAULT_RX_BUFFER_SIZE (1430 * 2)
// Default SYN queue size for listening sockets
#define TCP_DEFAULT_SYN_QUEUE_SIZE 4
// Maximum number of retransmissions
#define TCP_MAX_RETRIES 5
// Selective acknowledgment support
#define TCP_SACK_SUPPORT DISABLED
// TCP keep-alive support
#define TCP_KEEP_ALIVE_SUPPORT DISABLED

// UDP support
#define UDP_SUPPORT ENABLED
// Receive queue depth for connectionless sockets
#define UDP_RX_QUEUE_SIZE 4

// Raw socket support
#define RAW_SOCKET_SUPPORT DISABLED
// Receive queue depth for raw sockets
#define RAW_SOCKET_RX_QUEUE_SIZE 4

// BSD socket support
#define BSD_SOCKET_SUPPORT DISABLED

// Number of sockets that can be opened simultaneously
#define SOCKET_MAX_COUNT 32

// LLMNR responder support
#define LLMNR_RESPONDER_SUPPORT ENABLED

// HTTP client support
#define HTTP_CLIENT_SUPPORT ENABLED
// HTTP over TLS
#define HTTP_CLIENT_TLS_SUPPORT ENABLED

#define HTTP_SERVER_TLS_SUPPORT ENABLED

#define HTTP_SERVER_MULTIPART_TYPE_SUPPORT ENABLED

/* match original cloud settings */
#define HTTP_SERVER_IDLE_TIMEOUT (5 * 60000)
#define HTTP_SERVER_TIMEOUT (1 * 60000)
#define HTTP_SERVER_BUFFER_SIZE 1024 * 32

#define HTTP_SERVER_MAX_REQUESTS (UINT32_MAX - 1)

#endif
