#include <string.h>
#include <ctype.h>

#include "hw.h"
#include "tessel.h"
#include "tm.h"
#include "host_spi.h"
#include "utility/nvmem.h"
#include "utility/wlan.h"
#include "utility/hci.h"
#include "utility/security.h"
//#include "utility/os.h"
#include "utility/netapp.h"
#include "utility/evnt_handler.h"

/**
 * Configuration
 */

static volatile int inuse = 0;
uint8_t hw_wifi_ip[4] = {0, 0, 0, 0};
uint8_t hw_cc_ver[2] = {0, 0};

void hw_net__inuse_start (void)
{
	inuse = 1;
}

void hw_net__inuse_stop (void)
{
	inuse = 0;
}

#define CC3000_START hw_net__inuse_start();
#define CC3000_END hw_net__inuse_stop();

int hw_net_inuse ()
{
	// If a tm_net device call is being made, indicate so.
	return inuse;
}

int hw_net_is_connected ()
{
  return ulCC3000Connected;
}

int hw_net_has_ip ()
{
  return ulCC3000DHCP;
}

int hw_net_online_status(){
	// checks if the CC is connected to wifi and an IP address is allocated
	return ulCC3000Connected && ulCC3000DHCP;
}

int hw_net_ssid (char ssid[33])
{
	CC3000_START;
	tNetappIpconfigRetArgs ipinfo;
	netapp_ipconfig(&ipinfo);
	memset(ssid, 0, 33);
	memcpy(ssid, ipinfo.uaSSID, 32);
	CC3000_END;
	return 0;
}

uint32_t hw_net_defaultgateway ()
{
	CC3000_START;
	tNetappIpconfigRetArgs ipinfo;
	netapp_ipconfig(&ipinfo);
	CC3000_END;

	char* aliasable_ip = (char*) ipinfo.aucDefaultGateway;
	return *((uint32_t *) aliasable_ip);
}

uint32_t hw_net_dhcpserver ()
{
	CC3000_START;
	tNetappIpconfigRetArgs ipinfo;
	netapp_ipconfig(&ipinfo);
	CC3000_END;

	char* aliasable_ip = (char*) ipinfo.aucDHCPServer;
	return *((uint32_t *) aliasable_ip);
}

uint32_t hw_net_dnsserver ()
{
	CC3000_START;
	tNetappIpconfigRetArgs ipinfo;
	netapp_ipconfig(&ipinfo);
	CC3000_END;

	char* aliasable_ip = (char*) ipinfo.aucDNSServer;
	return *((uint32_t *) aliasable_ip);
}

uint8_t hw_net_rssi ()
{
  uint8_t results[50];
  int res = wlan_ioctl_get_scan_results(10000, results);

  if (res == -1){
  	TM_DEBUG("RSSI check failed. Aborting");
  	return 0;
  } else if (results[4] == 0){
  	TM_DEBUG("WARNING: Using cached results");
  } else if (results[4] == 2) {
  	TM_DEBUG("No results. Are you connected to wifi?");
  	return 0;
  } else if ( (results[8] & 1) == 0) {
  	TM_DEBUG("RSSI results are not valid. Aborting.");
  	return 0;
  }

  // otherwise results are valid
  // TM_DEBUG("Found %lu networks", results[0] | results[1] << 8 | results[2] << 16 | results[3] << 24);
  // TM_DEBUG("RSSI: %d", results[8] - 1);

  return results[8] - 1;
}


int hw_net_mac (uint8_t mac[MAC_ADDR_LEN])
{
	CC3000_START;
	int ret = nvmem_get_mac_address(mac);
	CC3000_END;
	return ret;
}


void tm_net_initialize_dhcp_server (void)
{
  // Added by Hai Ta
  //
  // Network mask is assumed to be 255.255.255.0
  //

  uint8_t pucSubnetMask[4], pucIP_Addr[4], pucIP_DefaultGWAddr[4], pucDNS[4];

  pucDNS[0] = 0x08;
  pucDNS[1] = 0x08;
  pucDNS[2] = 0x08;
  pucDNS[3] = 0x08;

  pucSubnetMask[0] = 0;
  pucSubnetMask[1] = 0;
  pucSubnetMask[2] = 0;
  pucSubnetMask[3] = 0;
  pucIP_Addr[0] = 0;
  pucIP_Addr[1] = 0;
  pucIP_Addr[2] = 0;
  pucIP_Addr[3] = 0;
  // Use default gateway 192.168.1.1 here
  pucIP_DefaultGWAddr[0] = 0;
  pucIP_DefaultGWAddr[1] = 0;
  pucIP_DefaultGWAddr[2] = 0;
  pucIP_DefaultGWAddr[3] = 0;

  // In order for gethostbyname( ) to work, it requires DNS server to be configured prior to its usage
  // so I am gonna add full static

  // Netapp_Dhcp is used to configure the network interface, static or dynamic (DHCP).
  // In order to activate DHCP mode, aucIP, aucSubnetMask, aucDefaultGateway must be 0.The default mode of CC3000 is DHCP mode.
  netapp_dhcp((unsigned long *)&pucIP_Addr[0], (unsigned long *)&pucSubnetMask[0], (unsigned long *)&pucIP_DefaultGWAddr[0], (unsigned long *)&pucDNS[0]);
}

int hw_net_is_readable (int ulSocket)
{
	CC3000_START;
	fd_set readSet;        // Socket file descriptors we want to wake up for, using select()
	FD_ZERO(&readSet);
	FD_SET(ulSocket, &readSet);
	struct timeval timeout;

	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	int rcount = select( ulSocket+1, &readSet, (fd_set *) 0, (fd_set *) 0, &timeout );
	int flag = FD_ISSET(ulSocket, &readSet);
	CC3000_END;

	(void) rcount;
	return flag;
}

int hw_net_block_until_readable (int ulSocket, int timeout)
{
  while (1) {
    if (hw_net_is_readable(ulSocket)) {
      break;
    }
    if (timeout > 0 && --timeout == 0) {
      return -1;
    }
    hw_wait_ms(100);
  }
  return 0;
}

void hw_net_initialize (void)
{
	CC3000_START;
	SpiInit(4e6);
	hw_wait_us(100);

//	TM_COMMAND('w', "Calling wlan_init\n");
	wlan_init(CC3000_UsynchCallback, NULL, NULL, NULL, ReadWlanInterruptPin,
	WlanInterruptEnable, WlanInterruptDisable, WriteWlanPin);

//	TM_COMMAND('w',"Calling wlan_start...\n");
//	tm_sleep_ms(100);
	wlan_start(0);
//	TM_COMMAND('w',"wlan started\n");
//
//	// Reset all the previous configuration
//	TM_COMMAND('w',"setting conn policy\n");
//	wlan_ioctl_set_connection_policy(0, 0, 0);
//	TM_COMMAND('w',"deleting profiles\n");
//	wlan_ioctl_del_profile(255);

//	tm_sleep_ms(100);
//	TM_COMMAND('w',"setting event mask\n");
	wlan_set_event_mask(HCI_EVNT_WLAN_KEEPALIVE|HCI_EVNT_WLAN_UNSOL_INIT|HCI_EVNT_WLAN_ASYNC_PING_REPORT);
//	TM_COMMAND('w',"done setting event mask\n");

 	unsigned long aucDHCP = 14400;
	unsigned long aucARP = 3600;
	unsigned long aucKeepalive = 10;
	unsigned long aucInactivity = 8;
	if (netapp_timeout_values(&aucDHCP, &aucARP, &aucKeepalive, &aucInactivity) != 0) {
		TM_DEBUG("Error setting inactivity timeout!");
	}

	unsigned char version[2];
	if (nvmem_read_sp_version(version)) {
		TM_ERR("Failed to read CC3000 firmware version.");
	} 

	memcpy(hw_cc_ver, version, 2);

	CC3000_END;
}

void hw_net_config(int should_connect_to_open_ap, int should_use_fast_connect, int auto_start){
	wlan_ioctl_set_connection_policy(should_connect_to_open_ap, should_use_fast_connect, auto_start);
}

void hw_net_smartconfig_initialize (void)
{
	CC3000_START;
	StartSmartConfig();
	CC3000_END;
}

void hw_net_disconnect (void)
{
	CC3000_START;
	wlan_stop();
	hw_digital_write(CC3K_CONN_LED, 0);
	CC3000_END;
}

int strcicmp(char const *a, char const *b)
{
    for (;; a++, b++) {
        int d = (tolower((unsigned char) *a) - tolower((unsigned char) *b));
        if (d != 0 || !*a)
            return d;
    }
}

//  WLAN_SEC_UNSEC,
//  WLAN_SEC_WEP (ASCII support only),
//  WLAN_SEC_WPA or WLAN_SEC_WPA2
int hw_net_connect (const char *security_type, const char *ssid, const char *keys){
  CC3000_START;
  int security = WLAN_SEC_WPA2;
  char * security_print = "wpa2";
  if (strcicmp(security_type, "wpa") == 0){
    security = WLAN_SEC_WPA;
    security_print = "wpa";
  } else if (strcicmp(security_type, "wep") == 0){
    security = WLAN_SEC_WEP;
    security_print = "wep";
  } else if (keys[0] == 0){
    security = WLAN_SEC_UNSEC;
    security_print = "unsecure";
  }
  TM_DEBUG("Attempting to connect with security type %s... ", security_print);
  int connected = wlan_connect(security, (char *) ssid, strlen(ssid), 0, (unsigned char *) keys, strlen(keys));
  if (connected != 0) {
    TM_DEBUG("Error #%d in connecting...", connected);
  } else {
    TM_DEBUG("Acquiring IP address...");
    TM_COMMAND('W', "{\"acquiring\": 1}");
  }
  CC3000_END;
  return connected;
}

int hw_net_connect_wpa2 (const char *ssid, const char *keys)
{
	CC3000_START;
	TM_DEBUG("Attempting to connect...");
//	TM_COMMAND('w', "SSID: %s", (char *) ssid);
//	TM_COMMAND('w', "Pass: %s", (char *) keys);

	int connected = wlan_connect(WLAN_SEC_WPA2, (char *) ssid, strlen(ssid), 0, (unsigned char *) keys, strlen(keys));
	if (connected != 0) {
		TM_DEBUG("Error #%d in connecting...", connected);
	} else {
		TM_DEBUG("Acquiring IP address...");
	}
	CC3000_END;
	return connected;
}

