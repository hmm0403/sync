#ifndef __PROTOCOL_MSG_H_
#define __PROTOCOL_MSG_H_

#include "sizeof.h"

#define	  INITSYNC_MSG						(POW_2_1)
#define   MDP_MSG							(POW_2_2)

// INITSYNC control message
#define   INITSYNC_CNTL_MSG_BASE			(POW_2_8)

#define   INITSYNC_CNTL_NORMAL				(POW_2_9)
#define   INITSYNC_CNTL_RESUME				(POW_2_10)

#define   INITSYNC_CNTL_REQ_AFLIST			(POW_2_11)
#define   INITSYNC_CNTL_REQ_TOLIST			(POW_2_12)
#define   INITSYNC_CNTL_REQ_SOLIST			(POW_2_13)
#define   INITSYNC_CNTL_REQ_MFLIST			(POW_2_14)
#define   INITSYNC_CNTL_REQ_ALL_MFLIST		(INITSYNC_CNTL_REQ_SOLIST | INITSYNC_CNTL_REQ_MFLIST)
#define   INITSYNC_CNTL_REQ					(INITSYNC_CNTL_NORMAL | INITSYNC_CNTL_RESUME | INITSYNC_CNTL_REQ_AFLIST | INITSYNC_CNTL_REQ_TOLIST | INITSYNC_CNTL_REQ_SOLIST | INITSYNC_CNTL_REQ_MFLIST)

#define   INITSYNC_CNTL_AFLIST				(POW_2_15)
#define   INITSYNC_CNTL_TOLIST				(POW_2_16)
#define   INITSYNC_CNTL_SOLIST				(POW_2_17)
#define   INITSYNC_CNTL_MFLIST				(POW_2_18)
#define	  INITSYNC_CNTL_SEGMENT				(POW_2_19)
#define   INITSYNC_CNTL_ALL_MFLIST			(INITSYNC_CNTL_SOLIST | INITSYNC_CNTL_MFLIST)
#define   INITSYNC_CNTL_FLIST				(INITSYNC_CNTL_AFLIST | INITSYNC_CNTL_TOLIST | INITSYNC_CNTL_SOLIST | INITSYNC_CNTL_MFLIST | INITSYNC_CNTL_SEGMENT)

#define   INITSYNC_CNTL_FILE				(POW_2_20)
#define   INITSYNC_CNTL_FILE_ACK			(POW_2_21)
#define   INITSYNC_CNTL_FILE_END			(POW_2_22)
#define   INITSYNC_CNTL_TARGET_READY		(POW_2_23)
#define   INITSYNC_CNTL_RSPNS_FAIL			(POW_2_24)

#define	  INITSYNC_CNTL_CHECKPOINT			(POW_2_25)
#define	  INITSYNC_CNTL_CHECKPOINT_ACK		(POW_2_26)

#define	  INITSYNC_CNTL_ERROR				(POW_2_27)

#define   INITSYNC_CNTL_PAUSE				(POW_2_28)
#define   INITSYNC_CNTL_STOP				(POW_2_29)

#define   INITSYNC_CNTL_SHORT				(INITSYNC_CNTL_CHECKPOINT | INITSYNC_CNTL_CHECKPOINT_ACK | INITSYNC_CNTL_REQ | INITSYNC_CNTL_TARGET_READY | INITSYNC_CNTL_RSPNS_FAIL | INITSYNC_CNTL_FILE_ACK | INITSYNC_CNTL_FILE_END | INITSYNC_CNTL_ERROR | INITSYNC_CNTL_PAUSE | INITSYNC_CNTL_STOP)
#define   INITSYNC_CNTL_LONG				(INITSYNC_CNTL_FLIST | INITSYNC_CNTL_FILE)


/*****************************************************************
 * TYPEDEFS.
 *****************************************************************/
/**
        @brief  comm_msg_tag structure

        This structure defines the message to store information of system call intercepted.
*/
typedef struct comm_msg_tag {
        uint64        length;       /**< Total packet size except for this field size */
        unsigned int        pid;          /**< Process identifier */

        unsigned int        fid;          /**< cmd identifier */
        unsigned int        blank;

        unsigned long long  hsid;         /**< Message sequence identifier */
        unsigned long long  gsid;

		uint64				timestamp;
} COMM_MSG_STRUCT;
#define SIZEOF_COMM_MSG_STRUCT         ( sizeof(COMM_MSG_STRUCT) )


#endif