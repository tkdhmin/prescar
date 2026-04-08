//////////////////////////////////////////////////////////////////////////////////
// main.c for Cosmos+ OpenSSD
// Copyright (c) 2016 Hanyang University ENC Lab.
// Contributed by Yong Ho Song <yhsong@enc.hanyang.ac.kr>
//				  Youngjin Jo <yjjo@enc.hanyang.ac.kr>
//				  Sangjin Lee <sjlee@enc.hanyang.ac.kr>
//				  Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//				  Kibin Park <kbpark@enc.hanyang.ac.kr>
//
// This file is part of Cosmos+ OpenSSD.
//
// Cosmos+ OpenSSD is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// Cosmos+ OpenSSD is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Cosmos+ OpenSSD; see the file COPYING.
// If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Company: ENC Lab. <http://enc.hanyang.ac.kr>
// Engineer: Sangjin Lee <sjlee@enc.hanyang.ac.kr>
//			 Jaewook Kwak <jwkwak@enc.hanyang.ac.kr>
//			 Kibin Park <kbpark@enc.hanyang.ac.kr>
//
// Project Name: Cosmos+ OpenSSD
// Design Name: Cosmos+ Firmware
// Module Name: Main
// File Name: main.c
//
// Version: v1.0.2
//
// Description:
//   - initializes caches, MMU, exception handler
//   - calls nvme_main function
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.0.2
//   - An address region (0x0020_0000 ~ 0x179F_FFFF) is used to uncached & nonbuffered region
//   - An address region (0x1800_0000 ~ 0x3FFF_FFFF) is used to cached & buffered region
//
// * v1.0.1
//   - Paging table setting is modified for QSPI or SD card boot mode
//     * An address region (0x0010_0000 ~ 0x001F_FFFF) is used to place code, data, heap and stack sections
//     * An address region (0x0010_0000 ~ 0x001F_FFFF) is setted a cached&bufferd region
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////


#include "xil_cache.h"
#include "xil_exception.h"
#include "xil_mmu.h"
#include "xparameters_ps.h"
#include "xscugic_hw.h"
#include "xscugic.h"
#include "xil_printf.h"
#include "nvme/debug.h"

#include "nvme/nvme.h"
#include "nvme/nvme_main.h"
#include "nvme/host_lld.h"
#include "memory_map.h"

XScuGic GicInstance;

int main()
{
	unsigned int u;

	XScuGic_Config* IntcConfig;

	Xil_ICacheDisable();
	Xil_DCacheDisable();
	Xil_DisableMMU();
	unsigned int i = 0;
	// Paging table set
#define MB (1024*1024)
	for (u = 0; u < 4096; u++)
	{
		if (u < 0x2) {
			if (!i) {
				xil_printf("buffered %u~\r\n", u);
				i = 1;
			}
			Xil_SetTlbAttributes(u * MB, 0xC1E); //(~2) cached & buffered
		}
		else if (u < GROWING_SEGMENT_HEAD_ADDR / MB) {
			if (i) {
				xil_printf("unbuffered %u \r\n", u);
				i = 0;
			}
			Xil_SetTlbAttributes(u * MB, 0xC12); // uncached & unbuffered
		}
		else if (u <= RESERVED0_END_ADDR / MB) {
			if (!i) {
				xil_printf("buffered %u \r\n", u);
				i = 1;
			}
			Xil_SetTlbAttributes(u * MB, 0xC1E); // cached & buffered (~255)
		}
		// else if (u >= HNSW_ONDEMAND_ENTRY_START_ADDR / MB && u <= (TOP_K_CURRENT_DISTANCE_BUFFER + TOP_K_CURRENT_DISTANCE_BUFFER_SIZE) / MB) {
		// 	if (i) { xil_printf("[force uncached] HNSW related %u \r\n", u); i = 0; }
		// 	Xil_SetTlbAttributes(u * MB, 0xC12);
		// }
		else if (u <= DATA_BUFFER_MAP_ADDR / MB) {
			// else if (u < (0x180)){ // (~384)
			if (i) {
				xil_printf("unbuffered %u \r\n", u);
				i = 0;
			}
			Xil_SetTlbAttributes(u * MB, 0xC12); // uncached & unbuffered
		}
		else if (u < TRASH_CAN_ADDR / MB) {
			// else if (u < 0x400)
			if (!i) {
				xil_printf("buffered %u \r\n", u);
				i = 1;
			}
			Xil_SetTlbAttributes(u * MB, 0xC1E); // cached & buffered
		}
		else {
			if (i) {
				xil_printf("unbuffered %u \r\n", u);
				i = 0;
			}
			Xil_SetTlbAttributes(u * MB, 0xC12); // uncached & unbuffered	
		}
	}

	Xil_EnableMMU();
	Xil_ICacheEnable();
	Xil_DCacheEnable();
	xil_printf("[!] MMU has been enabled.\r\n");
	xil_printf("DRAM_BUFFER_MAP is 0x%x\n", DATA_BUFFER_MAP_ADDR / MB);
	xil_printf("RESERVED is 0x%x\n", RESERVED1_START_ADDR / MB);

	xil_printf("\r\n Hello COSMOS+ OpenSSD !!! \r\n");

	Xil_ExceptionInit();

	IntcConfig = XScuGic_LookupConfig(XPAR_SCUGIC_SINGLE_DEVICE_ID);
	XScuGic_CfgInitialize(&GicInstance, IntcConfig, IntcConfig->CpuBaseAddress);
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
		(Xil_ExceptionHandler)XScuGic_InterruptHandler,
		&GicInstance);

	XScuGic_Connect(&GicInstance, 61,
		(Xil_ExceptionHandler)dev_irq_handler,
		(void*)0);

	XScuGic_Enable(&GicInstance, 61);

	// Enable interrupts in the Processor.
	Xil_ExceptionEnableMask(XIL_EXCEPTION_IRQ);
	Xil_ExceptionEnable();
	xil_printf("\r\n dev_irq_init !!! \r\n");

	xil_printf("\r\n dev_irq_init !!! 22 \r\n");
	nvme_main();

	xil_printf("done\r\n");

	return 0;
}
