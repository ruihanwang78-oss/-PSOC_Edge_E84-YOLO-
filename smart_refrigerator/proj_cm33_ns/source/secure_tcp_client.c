/*******************************************************************************
* File Name:   secure_tcp_client.c
*
* Description: HTTP POST client for PSoC Edge E84 (plain TCP, no TLS).
*              Supports cyclic send/receive of test detection data.
*
********************************************************************************/
#include "display.h"
#include "json_parser.h"
#include "ipc_shared.h"

#include "cybsp.h"
#include "retarget_io_init.h"

/* FreeRTOS header file. */
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include "cyabs_rtos.h"

/* Standard C header file. */
#include <string.h>
#include <stdio.h>          /* 新增：用于 snprintf */

/* Cypress secure socket header file. */
#include "cy_secure_sockets.h"

/* Wi-Fi connection manager header files. */
#include "cy_wcm.h"
#include "cy_wcm_error.h"

/* Secure TCP client task header file. */
#include "secure_tcp_client.h"

/* IP address related header files (part of the lwIP TCP/IP stack). */
#include "ip_addr.h"

/* Wi-Fi credentials and TCP port settings header file. */
#include "network_credentials.h"

/* to use the portable formatting macros */
#include <inttypes.h>



/*******************************************************************************
* Macros
*******************************************************************************/
#define RESET_VAL                          (0U)
#define APP_SDIO_INTERRUPT_PRIORITY        (7U)
#define APP_HOST_WAKE_INTERRUPT_PRIORITY   (2U)
#define APP_SDIO_FREQUENCY_HZ              (25000000U)
#define SDHC_SDIO_64BYTES_BLOCK            (64U)

/* HTTP 配置 */
#define HTTP_POST_PATH      "/api/update"
#define HTTP_GET_PATH       "/api/reminders"
#define DEVICE_ID           "fridge_01"

#define NUM_CLASSES         6
static const char *g_category_map[NUM_CLASSES] = {
    "fruit", "protein", "drink", "vegetable", "dessert", "vegetable"
};

#define HTTP_SEND_INTERVAL_MS              (10000U)   /* 每 10 秒发一次测试数据 */

/* 测试数据集数量 */
#define TEST_DATA_SET_COUNT                (3U)

/*******************************************************************************
* Function Prototypes
*******************************************************************************/
cy_rslt_t connect_to_tcp_server(cy_socket_sockaddr_t address);
cy_rslt_t create_tcp_client_socket(void);
cy_rslt_t tcp_disconnection_handler(cy_socket_t socket_handle, void *arg);
cy_rslt_t http_post_detection(const char *json_payload, char *response_buf, uint32_t resp_buf_size, uint32_t *out_received);

#if(USE_AP_INTERFACE)
    static cy_rslt_t softap_start(void);
#else
    static cy_rslt_t connect_to_wifi_ap(void);
#endif

/*******************************************************************************
* Global Variables
*******************************************************************************/
cy_socket_t client_handle;
cy_wcm_ip_address_t softap_ip_address;
extern cy_stc_scb_uart_context_t    DEBUG_UART_context;
static mtb_hal_sdio_t sdio_instance;
static cy_stc_sd_host_context_t sdhc_host_context;
static cy_wcm_config_t wcm_config;

#if (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP)
static cy_stc_syspm_callback_params_t sdcardDSParams =
{
    .context   = &sdhc_host_context,
    .base      = CYBSP_WIFI_SDIO_HW
};

static cy_stc_syspm_callback_t sdhcDeepSleepCallbackHandler =
{
    .callback           = Cy_SD_Host_DeepSleepCallback,
    .skipMode           = SYSPM_SKIP_MODE,
    .type               = CY_SYSPM_DEEPSLEEP,
    .callbackParams     = &sdcardDSParams,
    .prevItm            = NULL,
    .nextItm            = NULL,
    .order              = SYSPM_CALLBACK_ORDER
};
#endif



/*******************************************************************************
* SDIO / WiFi 初始化（保留原样）
*******************************************************************************/
static void sdio_interrupt_handler(void)
{
    mtb_hal_sdio_process_interrupt(&sdio_instance);
}

static void host_wake_interrupt_handler(void)
{
    mtb_hal_gpio_process_interrupt(&wcm_config.wifi_host_wake_pin);
}

static void app_sdio_init(void)
{
    cy_rslt_t result;
    mtb_hal_sdio_cfg_t sdio_hal_cfg;
    
    cy_stc_sysint_t sdio_intr_cfg =
    {
        .intrSrc = CYBSP_WIFI_SDIO_IRQ,
        .intrPriority = APP_SDIO_INTERRUPT_PRIORITY
    };

    cy_stc_sysint_t host_wake_intr_cfg =
    {
        .intrSrc = CYBSP_WIFI_HOST_WAKE_IRQ,
        .intrPriority = APP_HOST_WAKE_INTERRUPT_PRIORITY
    };

    cy_en_sysint_status_t interrupt_init_status = Cy_SysInt_Init(&sdio_intr_cfg,
            sdio_interrupt_handler);

    if(CY_SYSINT_SUCCESS != interrupt_init_status)
    {
        handle_app_error();
    }

    NVIC_EnableIRQ(CYBSP_WIFI_SDIO_IRQ);

    result = mtb_hal_sdio_setup(&sdio_instance,
             &CYBSP_WIFI_SDIO_sdio_hal_config, NULL, &sdhc_host_context);

    if(CY_RSLT_SUCCESS != result)
    {
        handle_app_error();
    }

    Cy_SD_Host_Enable(CYBSP_WIFI_SDIO_HW);
    Cy_SD_Host_Init(CYBSP_WIFI_SDIO_HW, CYBSP_WIFI_SDIO_sdio_hal_config.
            host_config, &sdhc_host_context);
    Cy_SD_Host_SetHostBusWidth(CYBSP_WIFI_SDIO_HW, CY_SD_HOST_BUS_WIDTH_4_BIT);
    sdio_hal_cfg.frequencyhal_hz = APP_SDIO_FREQUENCY_HZ;
    sdio_hal_cfg.block_size = SDHC_SDIO_64BYTES_BLOCK;

    mtb_hal_sdio_configure(&sdio_instance, &sdio_hal_cfg);

#if (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP)
    Cy_SysPm_RegisterCallback(&sdhcDeepSleepCallbackHandler);
#endif

    mtb_hal_gpio_setup(&wcm_config.wifi_wl_pin, CYBSP_WIFI_WL_REG_ON_PORT_NUM,
            CYBSP_WIFI_WL_REG_ON_PIN);

    mtb_hal_gpio_setup(&wcm_config.wifi_host_wake_pin,
            CYBSP_WIFI_HOST_WAKE_PORT_NUM, CYBSP_WIFI_HOST_WAKE_PIN);

    cy_en_sysint_status_t interrupt_init_status_host_wake =
            Cy_SysInt_Init(&host_wake_intr_cfg, host_wake_interrupt_handler);

    if(CY_SYSINT_SUCCESS != interrupt_init_status_host_wake)
    {
        handle_app_error();
    }

    NVIC_EnableIRQ(CYBSP_WIFI_HOST_WAKE_IRQ);
}

#if(USE_AP_INTERFACE)
static cy_rslt_t softap_start(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    #if(USE_IPV6_ADDRESS)
        cy_wcm_ip_address_t ip_address;
    #endif 

    cy_wcm_ap_credentials_t softap_credentials = {SOFTAP_SSID, SOFTAP_PASSWORD,
                                                  SOFTAP_SECURITY_TYPE};

    static const cy_wcm_ip_setting_t softap_ip_info = {
        INITIALISER_IPV4_ADDRESS(.ip_address, SOFTAP_IP_ADDRESS),
        INITIALISER_IPV4_ADDRESS(.gateway,    SOFTAP_GATEWAY),
        INITIALISER_IPV4_ADDRESS(.netmask,    SOFTAP_NETMASK)
        };

    cy_wcm_wifi_band_t wifi_band ={CY_WCM_WIFI_BAND_ANY};
    cy_wcm_ap_config_t softap_config = {softap_credentials, wifi_band,
                                        SOFTAP_RADIO_CHANNEL,
                                        softap_ip_info,
                                        NULL};

    result = cy_wcm_start_ap(&softap_config);

    if (CY_RSLT_SUCCESS == result)
    {
        printf("Wi-Fi Device configured as Soft AP\n");
        printf("Connect TCP client device to the network: SSID: %s Password:%s\n",
                SOFTAP_SSID, SOFTAP_PASSWORD);
    #if(USE_IPV6_ADDRESS)
        result = cy_wcm_get_ipv6_addr(CY_WCM_INTERFACE_TYPE_AP,
                 CY_WCM_IPV6_LINK_LOCAL, &ip_address);
        if (CY_RSLT_SUCCESS == result)
        {
            printf("SofAP IPv6 Address : %s\n\n",
                   ip6addr_ntoa((const ip6_addr_t*)&ip_address.ip.v6));
        }
    #else
        printf("SofAP IPv4 Address : %s\n\n",
                ip4addr_ntoa((const ip4_addr_t *)&softap_ip_info.
                        ip_address.ip.v4));
    #endif  
    }

    return result;
}
#endif /* USE_AP_INTERFACE */

#if(!USE_AP_INTERFACE)
cy_rslt_t connect_to_wifi_ap(void)
{
    cy_rslt_t result;

    cy_wcm_connect_params_t wifi_conn_param;
    cy_wcm_ip_address_t ip_address;

     memset(&wifi_conn_param, RESET_VAL, sizeof(cy_wcm_connect_params_t));
    memcpy(wifi_conn_param.ap_credentials.SSID, WIFI_SSID, sizeof(WIFI_SSID));
    memcpy(wifi_conn_param.ap_credentials.password, WIFI_PASSWORD,
            sizeof(WIFI_PASSWORD));
    wifi_conn_param.ap_credentials.security = WIFI_SECURITY_TYPE;

    for(uint32_t conn_retries = RESET_VAL; conn_retries < MAX_WIFI_CONN_RETRIES;
            conn_retries++ )
    {
        result = cy_wcm_connect_ap(&wifi_conn_param, &ip_address);
        if (CY_RSLT_SUCCESS == result)
        {
            printf("Successfully connected to Wi-Fi network '%s'.\n",
                                wifi_conn_param.ap_credentials.SSID);

            #if(USE_IPV6_ADDRESS)
                result = cy_wcm_get_ipv6_addr(CY_WCM_INTERFACE_TYPE_STA,
                                              CY_WCM_IPV6_LINK_LOCAL, &ip_address);
                if (CY_RSLT_SUCCESS == result)
                {
                    printf("IPv6 address (link-local) assigned: %s\n",
                            ip6addr_ntoa((const ip6_addr_t*)&ip_address.ip.v6));
                }
            #else      
                printf("IPv4 address assigned: %s\n",
                        ip4addr_ntoa((const ip4_addr_t*)&ip_address.ip.v4));
       
            #endif /* USE_IPV6_ADDRESS */
            return result;
        }
        printf("Connection to Wi-Fi network failed with error code %"PRIu32"."
               "Retrying in %d ms...\n", result, WIFI_CONN_RETRY_INTERVAL_MSEC);
        vTaskDelay(pdMS_TO_TICKS(WIFI_CONN_RETRY_INTERVAL_MSEC));
    }

    printf("Exceeded maximum Wi-Fi connection attempts\n");
    return result;
}
#endif /* (!USE_AP_INTERFACE)*/

/*******************************************************************************
* 普通 TCP Socket 创建（无 TLS）
*******************************************************************************/
cy_rslt_t create_tcp_client_socket(void)
{
    cy_rslt_t result;
    cy_socket_opt_callback_t tcp_disconnection_option;

    #if(USE_IPV6_ADDRESS)
        result = cy_socket_create(CY_SOCKET_DOMAIN_AF_INET6, CY_SOCKET_TYPE_STREAM,
                                  CY_SOCKET_IPPROTO_TCP, &client_handle);
    #else
        result = cy_socket_create(CY_SOCKET_DOMAIN_AF_INET, CY_SOCKET_TYPE_STREAM,
                                  CY_SOCKET_IPPROTO_TCP, &client_handle);
    #endif

    if (CY_RSLT_SUCCESS != result)
    {
        printf("Failed to create socket! Error Code: %"PRIu32"\n", result);
        return result;
    }

    tcp_disconnection_option.callback = tcp_disconnection_handler;
    tcp_disconnection_option.arg = NULL;
    result = cy_socket_setsockopt(client_handle, CY_SOCKET_SOL_SOCKET,
                                  CY_SOCKET_SO_DISCONNECT_CALLBACK,
                                  &tcp_disconnection_option,
                                  sizeof(cy_socket_opt_callback_t));
    if (CY_RSLT_SUCCESS != result)
    {
        printf("Set socket option: CY_SOCKET_SO_DISCONNECT_CALLBACK failed! "
                "Error Code: %"PRIu32"\n", result);
        return result;
    }

    return CY_RSLT_SUCCESS;
}

/*******************************************************************************
* 普通 TCP 连接（无 TLS 握手）
*******************************************************************************/
cy_rslt_t connect_to_tcp_server(cy_socket_sockaddr_t address)
{
    cy_rslt_t result = CY_RSLT_MODULE_SECURE_SOCKETS_TIMEOUT;
    cy_rslt_t conn_result;  

    for(uint32_t conn_retries = RESET_VAL; conn_retries < MAX_TCP_SERVER_CONN_RETRIES; conn_retries++)
    {
        conn_result = create_tcp_client_socket();
        if(CY_RSLT_SUCCESS != conn_result )
        {
            printf("Failed to create socket! Error Code: %"PRIu32"\n", conn_result);
            handle_app_error();
        }
        
        conn_result = cy_socket_connect(client_handle, &address, sizeof(cy_socket_sockaddr_t));
        if (CY_RSLT_SUCCESS == conn_result)
        {
            printf("Connected to TCP server\n");
            return conn_result;
        }

        printf("Could not connect to TCP server.\n");
        printf("Trying to reconnect to TCP server...Please check if server is listening\n");
        cy_socket_delete(client_handle);
    }

     printf("Exceeded maximum connection attempts to the TCP server\n");
     return result;
}

cy_rslt_t tcp_disconnection_handler(cy_socket_t socket_handle, void *arg)
{
    cy_rslt_t result;
    result = cy_socket_disconnect(socket_handle, RESET_VAL);
    cy_socket_delete(socket_handle);
    printf("Disconnected from the TCP server!\n");
    return result;
}

/*******************************************************************************
* 从 HTTP 响应首行解析状态码
*******************************************************************************/
static int parse_http_status(const char *response)
{
    int status = 0;
    if (response == NULL) {
        return 0;
    }
    if (sscanf(response, "HTTP/1.1 %d", &status) == 1) {
        return status;
    }
    if (sscanf(response, "HTTP/1.0 %d", &status) == 1) {
        return status;
    }
    return 0;
}

/*******************************************************************************
* HTTP POST 发送检测数据（可复用）
* @param json_payload  要发送的 JSON 字符串
* @param response_buf  接收响应的缓冲区
* @param resp_buf_size 缓冲区大小
* @param out_received  实际接收到的字节数
* @return CY_RSLT_SUCCESS 成功
*******************************************************************************/
cy_rslt_t http_post_detection(const char *json_payload, char *response_buf, uint32_t resp_buf_size, uint32_t *out_received)
{
    cy_rslt_t result;
    char request[1024];
    char server_ip_str[16];
    uint32_t bytes_sent = 0;
    int payload_len = strlen(json_payload);
    int req_len;

    /* ========== 修复：手动格式化 IP，避免对宏取地址 ========== */
    #if(USE_IPV6_ADDRESS)
        /* IPv6 情况（你当前没开，先保留） */
        strncpy(server_ip_str, "...", sizeof(server_ip_str)-1);
    #else
        uint32_t ip = TCP_SERVER_IP_ADDRESS;
        snprintf(server_ip_str, sizeof(server_ip_str), "%d.%d.%d.%d",
                 (int)(ip & 0xFF),
                 (int)((ip >> 8) & 0xFF),
                 (int)((ip >> 16) & 0xFF),
                 (int)((ip >> 24) & 0xFF));
    #endif
    server_ip_str[sizeof(server_ip_str)-1] = '\0';
    /* ======================================================== */

    req_len = snprintf(request, sizeof(request),
        "POST %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        HTTP_POST_PATH,
        server_ip_str,
        TCP_SERVER_PORT,
        payload_len,
        json_payload
    );

    result = cy_socket_send(client_handle, request, req_len, CY_SOCKET_FLAGS_NONE, &bytes_sent);
    if (CY_RSLT_SUCCESS != result)
    {
        printf("[HTTP] Send failed! Error code: %"PRIu32"\n", result);
        return result;
    }
    printf("[HTTP] Sent %lu bytes\n", bytes_sent);

    result = cy_socket_recv(client_handle, response_buf, resp_buf_size - 1, CY_SOCKET_FLAGS_NONE, out_received);
    if (CY_RSLT_SUCCESS == result && *out_received > 0)
    {
        response_buf[*out_received] = '\0';
    }
    else
    {
        *out_received = 0;
    }

    return result;
}

/*******************************************************************************
* HTTP GET 请求（用于 /api/reminders 等获取类接口）
*******************************************************************************/
cy_rslt_t http_get_request(char *response_buf, uint32_t resp_buf_size, uint32_t *out_received)
{
    cy_rslt_t result;
    char request[256];
    char server_ip_str[16];
    uint32_t bytes_sent = 0;

    /* 格式化 IP 字符串 */
    uint32_t ip = TCP_SERVER_IP_ADDRESS;
    snprintf(server_ip_str, sizeof(server_ip_str), "%d.%d.%d.%d",
             (int)(ip & 0xFF), (int)((ip >> 8) & 0xFF),
             (int)((ip >> 16) & 0xFF), (int)((ip >> 24) & 0xFF));

    /* 构造 GET 报文（无 body，无 Content-Length） */
    int req_len = snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Connection: close\r\n"
        "\r\n",
        HTTP_GET_PATH,      /* "/api/reminders" */
        server_ip_str,
        TCP_SERVER_PORT
    );

    /* 发送 */
    result = cy_socket_send(client_handle, request, req_len, CY_SOCKET_FLAGS_NONE, &bytes_sent);
    if (result != CY_RSLT_SUCCESS) {
        printf("[HTTP] GET send failed: 0x%08lx\n", result);
        return result;
    }
    printf("[HTTP] GET sent %lu bytes\n", bytes_sent);

    /* 接收响应 */
    result = cy_socket_recv(client_handle, response_buf, resp_buf_size - 1, CY_SOCKET_FLAGS_NONE, out_received);
    if (result == CY_RSLT_SUCCESS && *out_received > 0) {
        response_buf[*out_received] = '\0';
    } else {
        *out_received = 0;
    }

    return result;
}

/*******************************************************************************
* 主 Task：循环收发数据（新增 ML 检测数据上传）
*******************************************************************************/
/* 全局接收缓冲区 */
static char g_http_response[1024];
static uint32_t g_http_received = 0;

void tcp_secure_client_task(void *arg)
{
    (void)arg;
    cy_rslt_t result;
    uint32_t test_index = 0;
    int wifi_initialized = 0;  /* 标志：Wi-Fi 是否已初始化 */

    /* 硬编码服务器地址（来自 network_credentials.h） */
    cy_socket_sockaddr_t tcp_server_address = {
        #if(USE_IPV6_ADDRESS)
            .ip_address.ip.v6 =  TCP_SERVER_IP_ADDRESS,
            .ip_address.version = CY_SOCKET_IP_VER_V6,
        #else
            .ip_address.ip.v4 = TCP_SERVER_IP_ADDRESS,
            .ip_address.version = CY_SOCKET_IP_VER_V4,
        #endif
            .port = TCP_SERVER_PORT
    };

    /* 初始化显示模块 */
    display_init();

    /* ========== 主循环：先等待识别 → 再连接 Wi-Fi → 传输 ========== */
    for(;;)
    {
        printf("\n========== 等待视觉识别结果 ==========\n");

        /* 1. 阻塞等待 CM55 的识别结果（20秒识别期间 CM33 完全休眠） */
        while (g_ml_result.ready != 1)
        {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        __DSB();
        __ISB();

        /* 调试：打印从共享内存读到的原始识别结果 */
        printf("\r\n[CM33] RAW ML RESULT: ready=%lu, count=%lu\r\n",
               g_ml_result.ready, g_ml_result.count);
        for (uint32_t i = 0; i < g_ml_result.count && i < MAX_DETECTIONS; i++) {
            printf("[CM33] RAW ML RESULT: [%lu] label=%lu %s, conf=%.2f\r\n",
                   i, g_ml_result.labels[i],
                   g_label_names[g_ml_result.labels[i]],
                   g_ml_result.confs[i]);
        }

        printf("[CM33] 收到识别结果，开始传输（Round %lu）...\n", test_index + 1);

        /* 2. 首次传输时才初始化 Wi-Fi（避免与识别并行） */
        if (!wifi_initialized)
        {
            app_sdio_init();
            wcm_config.interface = WIFI_INTERFACE_TYPE;
            wcm_config.wifi_interface_instance = &sdio_instance;

            result = cy_wcm_init(&wcm_config);
            if(CY_RSLT_SUCCESS != result) {
                printf("Wi-Fi Connection Manager initialization failed! Error code: 0x%08"PRIx32"\n", (uint32_t)result);
                printf("[CM33] 网络初始化失败，放弃本次传输，等待下一轮\n");
                __DSB();
                g_ml_result.ready = 0;
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
            printf("Wi-Fi Connection Manager initialized.\r\n");

            #if(USE_AP_INTERFACE)
                result = softap_start();
                if (CY_RSLT_SUCCESS != result) {
                    printf("Failed to Start Soft AP! Error code: 0x%08"PRIx32"\n", (uint32_t)result);
                    printf("[CM33] SoftAP 启动失败，放弃本次传输，等待下一轮\n");
                    __DSB();
                    g_ml_result.ready = 0;
                    vTaskDelay(pdMS_TO_TICKS(5000));
                    continue;
                }
            #else
                result = connect_to_wifi_ap();
                if(CY_RSLT_SUCCESS != result) {
                    printf("\n Failed to connect to Wi-Fi AP.\n");
                    printf("[CM33] Wi-Fi 连接失败，放弃本次传输，等待下一轮\n");
                    __DSB();
                    g_ml_result.ready = 0;
                    vTaskDelay(pdMS_TO_TICKS(5000));
                    continue;
                }
            #endif

            result = cy_socket_init();
            if (CY_RSLT_SUCCESS != result) {
                printf("Socket initialization failed!\n");
                printf("[CM33] Socket 初始化失败，放弃本次传输，等待下一轮\n");
                __DSB();
                g_ml_result.ready = 0;
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
            printf("Socket initialized\n");

            wifi_initialized = 1;
        }

        /* 3. 连接服务器 */
        result = connect_to_tcp_server(tcp_server_address);
        if (CY_RSLT_SUCCESS != result) {
            printf("[CM33] 连接失败，清除标志等待重试\n");
            __DSB();
            g_ml_result.ready = 0;
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        /* 3. POST 发送 ML 数据：按类别统计数量，符合 /api/update 格式 */
        {
            char ml_json[512];
            int pos = 0;
            uint32_t det_count = (g_ml_result.count < MAX_DETECTIONS)
                                 ? g_ml_result.count : MAX_DETECTIONS;

            /* 按 label 统计每个类别出现的次数 */
            uint32_t counts[NUM_CLASSES] = {0};
            for (uint32_t i = 0; i < det_count; i++) {
                uint32_t label = g_ml_result.labels[i];
                if (label < NUM_CLASSES) {
                    counts[label]++;
                }
            }

            pos += snprintf(ml_json + pos, sizeof(ml_json) - pos,
                            "{\"device_id\":\"%s\",\"items\":[", DEVICE_ID);

            int first = 1;
            for (uint32_t i = 0; i < NUM_CLASSES; i++) {
                pos += snprintf(ml_json + pos, sizeof(ml_json) - pos,
                                "%s{\"name\":\"%s\",\"count\":%lu,\"category\":\"%s\"}",
                                first ? "" : ",",
                                g_label_names[i], counts[i], g_category_map[i]);
                first = 0;
            }
            pos += snprintf(ml_json + pos, sizeof(ml_json) - pos, "]}");

            printf("[CM33] POST payload: %s\n", ml_json);

            result = http_post_detection(ml_json, g_http_response,
                                         sizeof(g_http_response),
                                         &g_http_received);
            if (CY_RSLT_SUCCESS == result && g_http_received > 0)
            {
                int status = parse_http_status(g_http_response);
                if (status >= 200 && status < 300) {
                    printf("[CM33] ML detection data sent OK (HTTP %d)\n", status);
                } else {
                    printf("[CM33] ML detection send failed: HTTP %d\n", status);
                    printf("[CM33] Server response: %s\n", g_http_response);
                }
            }
            else
            {
                printf("[CM33] ML detection send failed: 0x%08lx, received=%lu\n",
                       result, g_http_received);
            }
        }

        /* POST 完成后断开，GET 需要重新连接（服务器可能关闭了连接） */
        cy_socket_disconnect(client_handle, 0);
        cy_socket_delete(client_handle);

        __DSB();
        g_ml_result.ready = 0;  /* 通知 CM55 已读取，可开始下一个窗口 */

        /* 4. GET 获取提醒数据 */
        result = connect_to_tcp_server(tcp_server_address);
        if (CY_RSLT_SUCCESS != result) {
            printf("[CM33] GET 连接失败\n");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        result = http_get_request(g_http_response, sizeof(g_http_response), &g_http_received);

        if (result == CY_RSLT_SUCCESS && g_http_received > 0) {
            g_http_response[g_http_received] = '\0';

            {
                const char *body = g_http_response;
                const char *hend = strstr(g_http_response, "\r\n\r\n");
                if (hend) body = hend + 4;
                printf("[JSON BODY] %s\n", body);
            }

            /* 解析 JSON */
            parsed_response_t parsed;
            if (parse_cloud_response(g_http_response, (int)g_http_received, &parsed) == 0) {
                printf("[JSON] Alerts:%d | Recipes:%d | Valid:%d\n",
                       parsed.alert_count, parsed.recipe_count, parsed.valid);

                /* 拷贝 alerts 到本地 buffer（带 \0） */
                const char *alerts[MAX_ALERTS * 2];
                char alert_bufs[MAX_ALERTS][2][255];
                int ac = (parsed.alert_count < MAX_ALERTS) ? parsed.alert_count : MAX_ALERTS;
                for (int i = 0; i < ac; i++) {
                    int il = (parsed.alert_item_lens[i] < 255) ? parsed.alert_item_lens[i] : 255;
                    memcpy(alert_bufs[i][0], parsed.alert_items[i], il);
                    alert_bufs[i][0][il] = '\0';
                    alerts[i * 2] = alert_bufs[i][0];

                    int ml = (parsed.alert_msg_lens[i] < 255) ? parsed.alert_msg_lens[i] : 255;
                    memcpy(alert_bufs[i][1], parsed.alert_msgs[i], ml);
                    alert_bufs[i][1][ml] = '\0';
                    alerts[i * 2 + 1] = alert_bufs[i][1];
                }

                display_show(alerts, ac);
            } else {
                printf("[JSON] Parse failed or incomplete.\n");
            }
        } else {
            printf("[HTTP] No response.\n");
        }

        /* 5. 断开连接 */
        cy_socket_disconnect(client_handle, 0);
        cy_socket_delete(client_handle);

        printf("[CM33] 本轮传输完成，等待下一轮识别结果\n");
        test_index++;

        /* 循环流程：完成后返回 for(;;) 开头，等待下一个 g_ml_result.ready == 1 */
    }
}

/* [] END OF FILE */