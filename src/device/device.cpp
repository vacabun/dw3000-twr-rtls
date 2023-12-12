#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
// #include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/uart.h>
// #include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>

#include "msg/twr_poll.hpp"
#include "msg/twr_response.hpp"
#include "msg/twr_final.hpp"
#include "msg/twr_report.hpp"

#include "device/device.hpp"

LOG_MODULE_REGISTER(device, LOG_LEVEL);

Device *Device::device_ptr = nullptr;

K_HEAP_DEFINE(device_rx_work_msg_heap, 1024);
K_MUTEX_DEFINE(transceiver_mutex);

Device::Device()
{

    device_ptr = this;
    {
        dw_hw_init();
        dw_hw_reset();
        dw_hw_init_interrupt();
        dw_spi_speed_fast();

#if CONFIG_DW3000
        if (dwt_probe((struct dwt_probe_s *)&dw3000_probe_interf) == DWT_ERROR)
        {
            LOG_DBG("DEV Probe FAILED");
        }
        while (!dwt_checkidlerc())
            ;
#endif

        if (dwt_initialise(DW_INIT_CONFIG_PARAMETER) == DWT_ERROR)
        {
            LOG_DBG("DEV INIT FAILED");
        }

        LOG_DBG("Device ID: 0x%x\n", dwt_readdevid());

        k_sleep(K_SECONDS(1));
#if CONFIG_DW3000
        dwt_configuretxrf(&txconfig_options);
#endif
        dwt_setrxantennadelay(RX_ANT_DLY); // set RX antenna delay time
        dwt_settxantennadelay(TX_ANT_DLY); // set TX antenna delay time

        dwt_setrxaftertxdelay(0);
        dwt_setrxtimeout(0);

        // dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);
    }
    // dwt config
    {
#if CONFIG_DW3000
        config.chan = 5;                                                      // Channel number.
        config.txPreambLength = DWT_PLEN_128;                                 // Preamble length. Used in TX only.
        config.rxPAC = DWT_PAC8;                                              // Preamble acquisition chunk size. Used in RX only.
        config.txCode = 9;                                                    // TX preamble code. Used in TX only.
        config.rxCode = 9;                                                    // RX preamble code. Used in RX only.
        config.sfdType = DWT_SFD_DW_8;                                        // use non-standard 8 symbol
        config.dataRate = DWT_BR_6M8;                                         // Data rate.
        config.phrMode = DWT_PHRMODE_STD;                                     // PHY header mode.
        config.phrRate = DWT_PHRRATE_STD;                                     // PHY header rate.
        config.sfdTO = (129 + 8 - 8);                                         // SFD timeout Used in RX only. (preamble length + 1 + SFD length - PAC size).
        config.stsMode = (dwt_sts_mode_e)(DWT_STS_MODE_1 | DWT_STS_MODE_SDC); // STS mode 1 with SDC
        config.stsLength = DWT_STS_LEN_64;                                    // STS length see allowed values in Enum dwt_sts_lengths_e
        config.pdoaMode = DWT_PDOA_M0;                                        // PDOA mode off

        dwt_set_keyreg_128(&keys_options[KEY_INDEX - 1]); // set aes key
#endif
#if CONFIG_DW1000
        config.chan = 5;                      // Channel number.
        config.prf = DWT_PRF_64M;             // Pulse repetition frequency.
        config.txPreambLength = DWT_PLEN_128; // Preamble length. Used in TX only.
        config.rxPAC = DWT_PAC8;              // Preamble acquisition chunk size. Used in RX only.
        config.txCode = 9;                    // TX preamble code. Used in TX only.
        config.rxCode = 9;                    // RX preamble code. Used in RX only.
        config.nsSFD = 1;                     // 0 to use standard SFD, 1 to use non-standard SFD
        config.dataRate = DWT_BR_6M8;         // Data rate.
        config.phrMode = DWT_PHRMODE_STD;     // PHY header mode.
        config.sfdTO = (129 + 8 - 8);         // SFD timeout Used in RX only. (preamble length + 1 + SFD length - PAC size).
#endif
    }

    dwt_configure(&config);

#if CONFIG_DW3000
    // 802_15_4 frame
    {
        mac_frame.mhr_802_15_4.frame_ctrl[0] = 0x09;
        mac_frame.mhr_802_15_4.frame_ctrl[1] = 0xEC;
        mac_frame.mhr_802_15_4.sequence_num = 0x00;
        // mac_frame.mhr_802_15_4.dest_pan_id
        // mac_frame.mhr_802_15_4.dest_addr
        // mac_frame.mhr_802_15_4.src_addr
        mac_frame.mhr_802_15_4.aux_security.security_ctrl = 0x0f;
        // mac_frame.mhr_802_15_4.aux_security.frame_counter
        mac_frame.mhr_802_15_4.aux_security.key_indentifier = KEY_INDEX;
        // mac_frame.payload_ptr
    }
    // aes config
    {
        aes_config.aes_otp_sel_key_block = AES_key_otp_sel_1st_128;
        aes_config.aes_key_otp_type = AES_key_RAM;
        aes_config.aes_core_type = AES_core_type_CCM;
        // aes_config.mic
        aes_config.key_src = AES_KEY_Src_Register;
        aes_config.key_load = AES_KEY_Load;
        aes_config.key_addr = 0;
        aes_config.key_size = AES_KEY_128bit;
        // aes_config.mode
    }
    {
        // aes_job_rx.nonce
        // aes_job_rx.payload = rx_buffer;
        aes_job_rx.header = (uint8_t *)MHR_802_15_4_PTR(&mac_frame);
        aes_job_rx.header_len = MAC_FRAME_HEADER_SIZE(&mac_frame);
        // aes_job_rx.payload_len
        aes_job_rx.src_port = AES_Src_Rx_buf_0;
        aes_job_rx.dst_port = AES_Dst_Rx_buf_0;
        aes_job_rx.mode = AES_Decrypt;
        // aes_job_rx.mic_size
    }
    {
        // aes_job_tx.nonce
        // aes_job_tx.payload
        aes_job_tx.header = (uint8_t *)MHR_802_15_4_PTR(&mac_frame);
        aes_job_tx.header_len = MAC_FRAME_HEADER_SIZE(&mac_frame);
        // aes_job_tx.payload_len
        aes_job_tx.src_port = AES_Src_Tx_buf;
        aes_job_tx.dst_port = AES_Dst_Tx_buf;
        aes_job_tx.mode = AES_Encrypt;
        // aes_job_rx.mic_size
    }
#endif
    {
#if CONFIG_DW3000
        dwt_setinterrupt(DWT_INT_RXFCG_BIT_MASK, 0, DWT_ENABLE_INT);
        dwt_setcallbacks(
            nullptr,
            &Device::rx_ok_cb,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr);
#endif
#if CONFIG_DW1000
        dwt_setinterrupt(DWT_INT_RFCG, 1);
        dwt_setcallbacks(
            nullptr,
            &Device::rx_ok_cb,
            nullptr,
            nullptr);
#endif
    }

    k_sleep(K_MSEC(100));
    dwt_rxenable(DWT_START_RX_IMMEDIATE);

    device_address = DEVICE_ADDR;
    pan_id = PAN_ID;

#if CONFIG_DW1000
    dwt_setpanid(pan_id);
    dwt_setaddress16(device_address);

    // set frame type (0-2), SEC (3), Pending (4), ACK (5), PanIDcomp(6)
    /*frame type 0x1 == data*/ /* 0x20 ACK request*/ /*0x40 PID comp*/
    msg_f_send.frameCtrl[0] = 0x1 | 0x40;
    // source/dest addressing modes and frame version
    msg_f_send.frameCtrl[1] = 0x8 /*dest extended address (16bits)*/ |
                              0x80 /*src extended address (16bits)*/;
    msg_f_send.panID[0] = 0xF0;
    msg_f_send.panID[1] = 0xF0;
    msg_f_send.sourceAddr[0] = device_address & 0xFF;        // copy the address
    msg_f_send.sourceAddr[1] = (device_address >> 8) & 0xFF; // copy the address

#endif

    dw1000_hw_interrupt_enable();
}

void Device::app(void *, void *, void *)
{
}

uint64_t Device::tx_msg(uint8_t *msg, uint16_t len, uint16_t dest_addr, uint8_t mode)
{
#if CONFIG_DW3000
    uint64_t dest_addr_u64 = (uint64_t)dest_addr;
    mac_frame_set_pan_ids_and_addresses_802_15_4(&mac_frame, pan_id, dest_addr_u64, device_address);
    if (mode == DWT_START_TX_DELAYED)
    {
        uint32_t resp_tx_time = (get_sys_timestamp_u64() + (TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8;
        dwt_setdelayedtrxtime(resp_tx_time);
        uint64_t resp_tx_ts = (((uint64_t)(resp_tx_time & 0xFFFFFFFEUL)) << 8) + TX_ANT_DLY;
        set_msg_dly_ts(msg, len, resp_tx_ts);
    }
    {
        uint8_t nonce[13];
        mac_frame_get_nonce(&mac_frame, nonce);
        aes_job_tx.nonce = nonce;
        aes_job_tx.payload = msg;
        aes_job_tx.payload_len = len;
        aes_job_tx.mic_size = mac_frame_get_aux_mic_size(&mac_frame);
        aes_config.mode = AES_Encrypt;
        aes_config.mic = dwt_mic_size_from_bytes(mac_frame_get_aux_mic_size(&mac_frame));
        dwt_configure_aes(&aes_config);
    }
    int8_t status = dwt_do_aes(&aes_job_tx, aes_config.aes_core_type);
    if (status < 0)
    {
        LOG_DBG("TX: AES length error");
    }
    else if (status & DWT_AES_ERRORS)
    {
        LOG_DBG("TX: ERROR AES");
    }
    else
    {
        uint64_t tx_ts;
        k_mutex_lock(&transceiver_mutex, K_FOREVER);
        {
            dwt_writetxfctrl(aes_job_tx.header_len + aes_job_tx.payload_len + aes_job_tx.mic_size + FCS_LEN, 0, 1);
            dwt_forcetrxoff();
            if (dwt_starttx(mode) == DWT_SUCCESS)
            {
                // waitforsysstatus(NULL, NULL, DWT_INT_TXFRS_BIT_MASK, 0);
                int64_t current_time_us = k_uptime_get();
                int64_t timeout_us = current_time_us + 1000;
                while (!(dwt_readsysstatuslo() & DWT_INT_TXFRS_BIT_MASK))
                {
                    if (k_uptime_get() > timeout_us)
                    {
                        LOG_DBG("tx finish wait timeout");
                        break;
                    }
                }
                dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

                char log_s[60];
                snprintf(log_s, 60, "tx msg from %016llx to %016llx", device_address, dest_addr_u64);
                LOG_HEXDUMP_DBG(msg, len, log_s);
            }
            else
            {
                LOG_DBG("tx error.");
                return 0;
            }
            tx_ts = get_tx_timestamp_u64();
            mac_frame.mhr_802_15_4.sequence_num++;
            mac_frame_update_aux_frame_cnt(&mac_frame, mac_frame_get_aux_frame_cnt(&mac_frame) + 1);

            dwt_rxenable(DWT_START_RX_IMMEDIATE);
        }
        k_mutex_unlock(&transceiver_mutex);
        return tx_ts;
    }
#endif
    msg_f_send.destAddr[0] = dest_addr & 0xFF;
    msg_f_send.destAddr[1] = (dest_addr >> 8) & 0xFF;

    for (uint16_t i = 0; i < len; i++)
    {
        msg_f_send.messageData[i] = msg[i];
    }
    uint16_t SendMsgLength = 11 + len;
    // msg_f_send.seqNum = 0xFF;
    k_mutex_lock(&transceiver_mutex, K_FOREVER);
    {
        if (mode == DWT_START_TX_DELAYED)
        {
            uint32 final_tx_time = dwt_readsystimestamphi32() + 0x17cdc00 / 80; // (0x17cdc00 / 80) 10ms/8
            dwt_setdelayedtrxtime(final_tx_time);
        }

        dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS);
        dwt_forcetrxoff();
        dwt_writetxdata(SendMsgLength, (uint8 *)&msg_f_send, 0);
        dwt_writetxfctrl(SendMsgLength, 0, 0);
        dwt_starttx(mode);
        while ((dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS) != SYS_STATUS_TXFRS)
            ;
        dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS);

        return get_tx_timestamp_u64();
    }
    k_mutex_unlock(&transceiver_mutex);
    return 0;
}

void Device::set_msg_dly_ts(uint8_t *msg, uint16_t len, uint64_t ts)
{
    uint8_t msg_type = msg[0];

    switch (msg_type)
    {
    case MSG_TWR_RESPONSE:
#if defined(SS_TWR)
        msg::twr_response *m = (msg::twr_response *)msg;
        m->resp_tx_ts = ts;
#endif
        break;
    }
}

void Device::tx_done_cb(const dwt_cb_data_t *cb_data)
{
    LOG_DBG("TX done");
}

void Device::rx_ok_cb(const dwt_cb_data_t *cb_data)
{
#if CONFIG_DW3000
    k_mutex_lock(&transceiver_mutex, K_NO_WAIT);
    {
        // check sts quality
        int16_t stsQual;
        uint16_t stsStatus;
        if ((dwt_readstsquality(&stsQual) >= 0) && (dwt_readstsstatus(&stsStatus, 0) == DWT_SUCCESS))
        {
            LOG_DBG("STS quality: %d, STS status: %d, STS quality good.", stsQual, stsStatus);
        }
        else
        {
            LOG_DBG("STS quality: %d, STS status: %d, STS quality bad.", stsQual, stsStatus);
            return;
        }

        uint16_t frame_len = cb_data->datalength;

        device_ptr->aes_config.mode = AES_Decrypt;

        device_ptr->aes_job_rx.payload = device_ptr->rx_buffer;
        PAYLOAD_PTR_802_15_4(&device_ptr->mac_frame) = device_ptr->rx_buffer;

        uint64_t src_addr;
        uint64_t dst_addr;
        uint16_t msg_len;
        int8_t status = rx_aes_802_15_4(&device_ptr->mac_frame,
                                        frame_len,
                                        &device_ptr->aes_job_rx,
                                        sizeof(device_ptr->rx_buffer),
                                        keys_options,
                                        &src_addr,
                                        &dst_addr,
                                        &device_ptr->aes_config,
                                        &msg_len);
        if (status != AES_RES_OK)
        {
            LOG_DBG("AES error");
        }
        else if (dst_addr == device_ptr->device_address || dst_addr == BROADCAST_ADDR)
        {
            uint8_t *msg = device_ptr->rx_buffer;

            char log_s[60];
            snprintf(log_s, 60, "recv msg from %016llx to %016llx", src_addr, dst_addr);
            LOG_HEXDUMP_DBG(msg, msg_len, log_s);

            rx_work_msg *work_msg = (rx_work_msg *)k_heap_alloc(&device_rx_work_msg_heap, sizeof(rx_work_msg), K_NO_WAIT);
            if (work_msg != NULL)
            {
                memcpy(work_msg->msg, msg, msg_len);
                work_msg->len = msg_len;
                work_msg->src_addr = src_addr;
                work_msg->src_addr = src_addr;
                work_msg->rx_ts = get_rx_timestamp_u64();
                k_work_init(&work_msg->work, Device::rx_work_handler);
                extern struct k_work_q device_rx_work_q;
                k_work_submit_to_queue(&device_rx_work_q, &work_msg->work);
            }
            else
            {
                LOG_DBG("heap alloc failed.");
            }
        }
        else
        {
            LOG_DBG("The address is not sent to this machine, ignore the message.");
        }
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
    }
    k_mutex_unlock(&transceiver_mutex);
#endif
}

void Device::rx_work_handler(struct k_work *item)
{
    rx_work_msg *work_msg = CONTAINER_OF(item, rx_work_msg, work);

    device_ptr->msg_process_cb(work_msg->msg, work_msg->len, work_msg->src_addr, work_msg->dst_addr, work_msg->rx_ts);

    k_heap_free(&device_rx_work_msg_heap, work_msg);
}
void Device::msg_process_cb(uint8_t *msg, uint16_t msg_len, uint64_t src_addr, uint64_t dst_addr, uint64_t rx_ts)
{
}