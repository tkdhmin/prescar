//////////////////////////////////////////////////////////////////////////////////
// nvme_io_cmd.h for Cosmos+ OpenSSD
// Copyright (c) 2016 Hanyang University ENC Lab.
// Contributed by Yong Ho Song <yhsong@enc.hanyang.ac.kr> Youngjin Jo
// <yjjo@enc.hanyang.ac.kr> Sangjin Lee <sjlee@enc.hanyang.ac.kr>
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
//
// Project Name: Cosmos+ OpenSSD
// Design Name: Cosmos+ Firmware
// Module Name: NVMe IO Command Handler
// File Name: nvme_io_cmd.h
//
// Version: v1.0.0
//
// Description:
//   - declares functions for handling NVMe IO commands
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////

#ifndef VECTOR1_COSMOS_APP_SRC_NVME_NVME_IO_CMD_H_
#define VECTOR1_COSMOS_APP_SRC_NVME_NVME_IO_CMD_H_

#include "xtime_l.h"
#include "../ss/super.h"
#include "nvme.h"
// struct NVME_COMMAND;

void handle_nvme_io_cmd(NVME_COMMAND* nvmeCmd);
void TriggerInternalPageWrite(const unsigned int lsa, const unsigned int bufAddr, const unsigned int bufSize);
void TriggerInternalPageRead(const unsigned int startLsa, const unsigned int bufAddr, const unsigned int bufSize);
void TriggerInternalPagesRead(const unsigned int startLsa, const unsigned int bufAddr, const unsigned int numPages);
void TriggerInternalPagesWrite(const unsigned int startLsa, const unsigned int bufAddr, const unsigned int numPages);
unsigned int GetTypefromCmdSlotTag(int cmdSlotTag);

/**
 * @brief Defines condition types for Sealed Segment iteration
 */
enum SS_CONDITION_TYPE { SS_CONDITION_NEEDS_INDEXING = 0, SS_CONDITION_READY_FOR_SEARCH = 1 };

/**
 * @brief Check condition based on type and parameter
 */
#define CHECK_CONDITION(type, value) \
  ((type) == SS_CONDITION_NEEDS_INDEXING ? !HAS_INDEX_STORED_TO(value) : HAS_INDEX_STORED_TO(value))
 // ((type) == SS_CONDITION_NEEDS_INDEXING ? !HAS_INDEX_STORED_TO(value) : HAS_INDEX_STORED_TO(value))

/**
 * @brief Set appropriate target based on iterator type
 */
#define SET_TARGET(type, ss_ptr)             \
  do {                                            \
    if ((type) == SS_CONDITION_NEEDS_INDEXING) { \
      targetSsForIndexing = ss_ptr;      \
    } else {                                      \
      targetSsForSearching = ss_ptr;     \
    }                                             \
  } while (0)

 /**
  * @brief Clear appropriate target based on iterator type
  */
#define CLEAR_TARGET(type)                        \
  do {                                            \
    if ((type) == SS_CONDITION_NEEDS_INDEXING) { \
      CLEAR_SS_INDEX_TARGET();                   \
    } else {                                      \
      CLEAR_SS_SEARCH_TARGET();                  \
    }                                             \
  } while (0)


  /**
   * @brief Functions for supporting index building.
   */
int findNextSs(enum SS_CONDITION_TYPE conditionType);
int tryIndexBuild(int resumePoint);
void tryVectorSearch(unsigned int);
/* Baseline */
void assignNextSsToIndex(void);
void assignNextSsToSearch(void);
int findNextSsFromLevel(enum SS_CONDITION_TYPE conditionType, int specificLevel);


unsigned int numVectorSearch;

unsigned int raw_vector_size;
unsigned int cur_vector_seq;
unsigned int shouldDelayCompaction;

#define DIMENSION (raw_vector_size)(sizeof(float))
#define STAT

struct STAT_PER_TYPE {
  unsigned int c;
  unsigned int found;
  unsigned int hit;
  XTime TOTAL_TIME;
  XTime NAND_READ_SS;
  XTime NAND_READ_LOG;
  XTime DEVICE_MEMCPY;
};

enum CMD_TYPE {
  START_CMD,
  KV_PUT,
  KV_GET,
  KV_DEL,
  PAUSE_BUILD,
  RESUME_BUILD,
  UNUSED,
  NOT_SUPPORTED,
  VECTOR_BUILD,
  VECTOR_SEARCH,
  VECTOR_BUILD_STATUS,
  LAST_CMD,
};

#endif  // VECTOR1_COSMOS_APP_SRC_NVME_NVME_IO_CMD_H_
