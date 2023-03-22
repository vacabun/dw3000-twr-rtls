/*
 * Copyright (c) 2016 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/uart.h>

// use swd debug
#if CONFIG_DEBUG_SWD == 1
#include "stm32f4xx_ll_system.h"
#endif

#include "dw3000.h"

BUILD_ASSERT(DT_NODE_HAS_COMPAT(DT_CHOSEN(zephyr_console), zephyr_cdc_acm_uart), "Console device is not ACM CDC UART device");
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#define DEFAULT_STACKSIZE 2048

#if defined(TEST_READING_DEV_ID) || defined(TEST_AES_SS_TWR_INITIATOR) || defined(TEST_AES_SS_TWR_RESPONDER)

#define TEST_STACKSIZE 4096
#define _TEST_
#include "test/test.h"
void (*test_fun)(void *, void *, void *);
static K_THREAD_STACK_DEFINE(test_stack, TEST_STACKSIZE);
static struct k_thread test_thread;

#endif // TEST_READING_DEV_ID TEST_AES_SS_TWR_INITIATOR TEST_AES_SS_TWR_RESPONDER

#if defined(DEVICE_TAG)
#include "device/tag.hpp"
static K_THREAD_STACK_DEFINE(tag_stack, DEFAULT_STACKSIZE);
static struct k_thread tag_thread;
#endif // DEVICE_TAG

#if defined(DEVICE_ANCHOR)

#include <device/anthor.hpp>


// tx Workqueue Thread
K_HEAP_DEFINE(tx_msg_heap, 1024);

#define ANTHOR_TX_WORK_QUEUE_STACK_SIZE 512
#define ANTHOR_TX_WORK_QUEUE_PRIORITY 5

K_THREAD_STACK_DEFINE(anthor_tx_work_q_stack_area, ANTHOR_TX_WORK_QUEUE_STACK_SIZE);
struct k_work_q anthor_tx_work_q;

// main thread
static K_THREAD_STACK_DEFINE(anthor_responder_stack, DEFAULT_STACKSIZE);
static struct k_thread anthor_responder_thread;

#endif // DEVICE_ANCHOR

int main(void)
{
	// swd debug
#if CONFIG_DEBUG_SWD == 1
	LL_DBGMCU_EnableDBGSleepMode();
#endif

	/* usb config */
	if (usb_enable(NULL))
		return 0;
	k_sleep(K_MSEC(1000));

#if defined(TEST_READING_DEV_ID)
	test_fun = read_dev_id;
#endif // TEST_READING_DEV_ID

#if defined(TEST_AES_SS_TWR_INITIATOR)
	test_fun = ss_aes_twr_initiator;
#endif // TEST_AES_SS_TWR_INITIATOR

#if defined(TEST_AES_SS_TWR_RESPONDER)
	test_fun = ss_aes_twr_responder;
#endif // TEST_AES_SS_TWR_RESPONDER

#if defined(_TEST_)
	k_thread_create(&test_thread,
					test_stack,
					TEST_STACKSIZE,
					test_fun,
					NULL,
					NULL,
					NULL,
					K_PRIO_COOP(7),
					0,
					K_NO_WAIT);
#endif //_TEST_

#if defined(DEVICE_TAG)
	Tag tag;

	std::function<void(void *, void *, void *)> func = [&](void *arg1, void *arg2, void *arg3)
	{
		tag.app(arg1, arg2, arg3);
	};
	k_thread_create(
		&tag_thread,
		tag_stack,
		DEFAULT_STACKSIZE,
		[](void *arg1, void *arg2, void *arg3)
		{
			std::function<void(void *, void *, void *)> *func_ptr =
				static_cast<std::function<void(void *, void *, void *)> *>(arg1);
			(*func_ptr)(arg2, arg3, nullptr);
		},
		&func,
		NULL,
		NULL,
		K_PRIO_COOP(7),
		0,
		K_NO_WAIT);
#endif //_DEVICE_TAG_

#if defined(DEVICE_ANCHOR)
	Anthor anthor;

	std::function<void(void *, void *, void *)> func = [&](void *arg1, void *arg2, void *arg3)
	{
		anthor.app(arg1, arg2, arg3);
	};

	// tx Workqueue Thread
	k_work_queue_init(&anthor_tx_work_q);
	k_work_queue_start(&anthor_tx_work_q,
					   anthor_tx_work_q_stack_area,
					   K_THREAD_STACK_SIZEOF(anthor_tx_work_q_stack_area),
					   ANTHOR_TX_WORK_QUEUE_PRIORITY, NULL);

	k_thread_create(
		&anthor_responder_thread,
		anthor_responder_stack,
		DEFAULT_STACKSIZE,
		[](void *arg1, void *arg2, void *arg3)
		{
			std::function<void(void *, void *, void *)> *func_ptr =
				static_cast<std::function<void(void *, void *, void *)> *>(arg1);
			(*func_ptr)(arg2, arg3, nullptr);
		},
		&func,
		NULL,
		NULL,
		K_PRIO_COOP(7),
		0,
		K_NO_WAIT);
#endif // _DEVICE_ANCHOR_

#if defined(DEVICE)

#endif
	return 0;
}
