/*******************************************************************************
* File Name: network_credentials.h
*
* Description: This file is the public interface for Wi-Fi/Soft-AP credentials 
* and TLS credentials.
*
* Related Document: See README.md
*
********************************************************************************
* Copyright 2024-2025, Cypress Semiconductor Corporation (an Infineon company) or
* an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
*
* This software, including source code, documentation and related
* materials ("Software") is owned by Cypress Semiconductor Corporation
* or one of its affiliates ("Cypress") and is protected by and subject to
* worldwide patent protection (United States and foreign),
* United States copyright laws and international treaty provisions.
* Therefore, you may use this Software only as provided in the license
* agreement accompanying the software package from which you
* obtained this Software ("EULA").
* If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
* non-transferable license to copy, modify, and compile the Software
* source code solely for use in connection with Cypress's
* integrated circuit products.  Any reproduction, modification, translation,
* compilation, or representation of this Software except as specified
* above is prohibited without the express written permission of Cypress.
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
* reserves the right to make changes to the Software without notice. Cypress
* does not assume any liability arising out of the application or use of the
* Software or any product or circuit described in the Software. Cypress does
* not authorize its products for use in any products where a malfunction or
* failure of the Cypress product may reasonably be expected to result in
* significant property damage, injury or death ("High Risk Product"). By
* including Cypress's product in a High Risk Product, the manufacturer
* of such system or application assumes all risk of such use and in doing
* so agrees to indemnify Cypress against all liability.
*******************************************************************************/

/*******************************************************************************
* Include guard
*******************************************************************************/
#ifndef NETWORK_CREDENTIALS_H_
#define NETWORK_CREDENTIALS_H_

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

#include "cy_wcm.h"


/*******************************************************************************
* Macros
*******************************************************************************/
#define INITIALISER_IPV4_ADDRESS(addr_var, addr_val)   addr_var = { \
                                                       CY_WCM_IP_VER_V4, \
                                                       { .v4 = (uint32_t) \
                                                       (addr_val) } }

#define MAKE_IPV4_ADDRESS(a, b, c, d)              ((((uint32_t) d) << 24U) | \
                                                   (((uint32_t) c) << 16U) | \
                                                   (((uint32_t) b) << 8U) | \
                                                   ((uint32_t) a))
#if(USE_IPV6_ADDRESS)
    /* Converts a 16-bit value from host byte order (little-endian) to network byte order (big-endian) */
    #define HTONS(x) ( ( ( (x) & 0x0000FF00) >> 8U ) | ((x) & 0x000000FF) << 8U )

    #define MAKE_IPV6_ADDRESS(a, b, c, d, e, f, g, h)  { \
                ( (uint32_t) (HTONS(a)) | ( (uint32_t) (HTONS(b)) << 16U ) ), \
                ( (uint32_t) (HTONS(c)) | ( (uint32_t) (HTONS(d)) << 16U ) ), \
                ( (uint32_t) (HTONS(e)) | ( (uint32_t) (HTONS(f)) << 16U ) ), \
                ( (uint32_t) (HTONS(g)) | ( (uint32_t) (HTONS(h)) << 16U ) ), \
                }
#endif /* USE_IPV6_ADDRESS */

/* To use the Wi-Fi device in AP interface mode, set this macro as '1' */
#define USE_AP_INTERFACE                   (0U)

/* Change the server IP address to match the TCP server address (IP address
 * of the PC).
 */
#if(USE_IPV6_ADDRESS)
    #define TCP_SERVER_IP_ADDRESS          MAKE_IPV6_ADDRESS(0xFE80, 0, 0  \
                                           ,0, 0xF0F3, 0xB58C, 0x8FC2, 0xA690)
#else
    #define TCP_SERVER_IP_ADDRESS          MAKE_IPV4_ADDRESS(8, 133, 22, 44)
#endif

#if(USE_AP_INTERFACE)
    #define WIFI_INTERFACE_TYPE                CY_WCM_INTERFACE_TYPE_AP

    /* SoftAP Credentials: Modify SOFTAP_SSID and SOFTAP_PASSWORD as required */
    #define SOFTAP_SSID                        "MY_SOFT_AP"
    #define SOFTAP_PASSWORD                    "psoc1234"

    /* Security type of the SoftAP. See 'cy_wcm_security_t' structure
     * in "cy_wcm.h" for more details.
     */
    #define SOFTAP_SECURITY_TYPE               CY_WCM_SECURITY_WPA2_AES_PSK

    #define SOFTAP_IP_ADDRESS_COUNT            (2U)

    #define SOFTAP_IP_ADDRESS                  MAKE_IPV4_ADDRESS(192, 168, 10, 1)
    #define SOFTAP_NETMASK                     MAKE_IPV4_ADDRESS(255, 255, 255, 0)
    #define SOFTAP_GATEWAY                     MAKE_IPV4_ADDRESS(192, 168, 10, 1)
    #define SOFTAP_RADIO_CHANNEL               (1U)
#else
    #define WIFI_INTERFACE_TYPE                CY_WCM_INTERFACE_TYPE_STA

    /* Wi-Fi Credentials: Modify WIFI_SSID, WIFI_PASSWORD, and WIFI_SECURITY_TYPE
     * to match your Wi-Fi network credentials.
     * Note: Maximum length of the Wi-Fi SSID and password is set to
     * CY_WCM_MAX_SSID_LEN and CY_WCM_MAX_PASSPHRASE_LEN as defined in cy_wcm.h
     * file.
     */
    #define WIFI_SSID                         "Xiaomi 13 Pro"
    #define WIFI_PASSWORD                     "162810Wang"

    /* Security type of the Wi-Fi access point. See 'cy_wcm_security_t' structure
     * in "cy_wcm.h" for more details.
     */
    #define WIFI_SECURITY_TYPE                CY_WCM_SECURITY_WPA2_MIXED_PSK

    /* Maximum number of connection retries to a Wi-Fi network. */
    #define MAX_WIFI_CONN_RETRIES             (10U)

    /* Wi-Fi re-connection time interval in milliseconds */
    #define WIFI_CONN_RETRY_INTERVAL_MSEC     (1000U)
#endif


/*******************************************************************************
* Keys and Certificates
*******************************************************************************/
/* TCP client certificate. Copy from the TCP client certificate
 * generated by OpenSSL (See Readme.md on how to generate a SSL certificate).
 */
#define keyCLIENT_CERTIFICATE_PEM \
"-----BEGIN CERTIFICATE-----\n"\
"MIIBrDCCAVECFEO4KpaJXgFHY7ST3Y4VJMKHSOITMAoGCCqGSM49BAMCMFsxCzAJ\n"\
"BgNVBAYTAklOMQswCQYDVQQIDAJLQTESMBAGA1UEBwwJQkFOR0FMT1JFMQwwCgYD\n"\
"VQQKDANJRlgxDDAKBgNVBAsMA0lDVzEPMA0GA1UEAwwGY2xpZW50MB4XDTI0MTAw\n"\
"ODEzMjU0MVoXDTI3MDcwNTEzMjU0MVowVTELMAkGA1UEBhMCSU4xCzAJBgNVBAgM\n"\
"AktBMQwwCgYDVQQHDANJQ1cxDDAKBgNVBAoMA0lGWDEMMAoGA1UECwwDQ1NTMQ8w\n"\
"DQYDVQQDDAZjbGllbnQwWTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAASNqoySkIac\n"\
"udVHYndjwlHRaKqdNubhvUC4sx78YrG5xPxWXg+W0a2WumuU5CN9vLGB6cmsIE/F\n"\
"ddjUP3oCoIRsMAoGCCqGSM49BAMCA0kAMEYCIQCguRMY3ZJwa2+Meu/chNh+1w+C\n"\
"183CgFi8ezeJck9DagIhAOovQDzO4pvA8/sHxBNvsmYTH2GbW/iNfiiUDHACOhap\n"\
"-----END CERTIFICATE-----\n"

/* Private key of the TCP client. Copy from the TCP client key
 * generated by OpenSSL (See Readme.md on how to create a private key).
 */
#define keyCLIENT_PRIVATE_KEY_PEM \
"-----BEGIN EC PRIVATE KEY-----\n"\
"MHcCAQEEIE1z5i+VyfrjB+wyOq8W/p8vEjFyNc0OnMmS/soP6Dg4oAoGCCqGSM49\n"\
"AwEHoUQDQgAEjaqMkpCGnLnVR2J3Y8JR0WiqnTbm4b1AuLMe/GKxucT8Vl4PltGt\n"\
"lrprlOQjfbyxgenJrCBPxXXY1D96AqCEbA==\n"\
"-----END EC PRIVATE KEY-----\n"

/* TCP server certificate. In this example this is the RootCA
 * certificate so as to verify the TCP server's identity. */
#define keySERVER_ROOTCA_PEM \
"-----BEGIN CERTIFICATE-----\n"\
"MIICCjCCAbGgAwIBAgIUZzn/3rHOUDN+b2iBmw+GXg8MiI0wCgYIKoZIzj0EAwIw\n"\
"WzELMAkGA1UEBhMCSU4xCzAJBgNVBAgMAktBMRIwEAYDVQQHDAlCQU5HQUxPUkUx\n"\
"DDAKBgNVBAoMA0lGWDEMMAoGA1UECwwDSUNXMQ8wDQYDVQQDDAZjbGllbnQwHhcN\n"\
"MjQxMDA4MTMyNTA5WhcNMjcwNzA1MTMyNTA5WjBbMQswCQYDVQQGEwJJTjELMAkG\n"\
"A1UECAwCS0ExEjAQBgNVBAcMCUJBTkdBTE9SRTEMMAoGA1UECgwDSUZYMQwwCgYD\n"\
"VQQLDANJQ1cxDzANBgNVBAMMBmNsaWVudDBZMBMGByqGSM49AgEGCCqGSM49AwEH\n"\
"A0IABKVZCQmcN+WTqMA/ZPXmtpD//B2u8PnIQmvo+ZWdxbxDt0MrO6bmoOLq/0Mu\n"\
"V+tqDI0tBccr8zh7d2LB2KO3KEijUzBRMB0GA1UdDgQWBBQ0rHW8DNO9/Cu8nfWp\n"\
"2YJlpf1llTAfBgNVHSMEGDAWgBQ0rHW8DNO9/Cu8nfWp2YJlpf1llTAPBgNVHRMB\n"\
"Af8EBTADAQH/MAoGCCqGSM49BAMCA0cAMEQCIDzY6H7RlahGuEiumXhMcBe0Fzeo\n"\
"aU6TZRrJXZR9Lj//AiA5K/1Wbhmn9vAGQ2HDr7lsLPvX7Jo5Wzguk6YGNlSPPQ==\n"\
"-----END CERTIFICATE-----\n"

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* NETWORK_CREDENTIALS_H_ */
