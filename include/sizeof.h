#ifndef __INITSYNC_SIZEOF_H__
#define __INITSYNC_SIZEOF_H__


#include <sys/types.h>
#include <time.h>


//#include <net/if.h>
//#include <netinet/ip_icmp.h>
//#include <netinet/in.h>

#define POW_2_0             (1)                  // 1                  0x00000001
#define POW_2_1             (POW_2_0 << 1)       // 2                  0x00000002
#define POW_2_2             (POW_2_1 << 1)       // 4                  0x00000004
#define POW_2_3             (POW_2_2 << 1)       // 8                  0x00000008

#define POW_2_4             (POW_2_3 << 1)       // 16                 0x00000010
#define POW_2_5             (POW_2_4 << 1)       // 32                 0x00000020
#define POW_2_6             (POW_2_5 << 1)       // 64                 0x00000040
#define POW_2_7             (POW_2_6 << 1)       // 128                0x00000080

#define POW_2_8             (POW_2_7 << 1)       // 256                0x00000100
#define POW_2_9             (POW_2_8 << 1)       // 512                0x00000200
#define POW_2_10            (POW_2_9  << 1)      // 1024               0x00000400
#define POW_2_11            (POW_2_10 << 1)      // 2048               0x00000800

#define POW_2_12            (POW_2_11 << 1)      // 4096               0x00001000
#define POW_2_13            (POW_2_12 << 1)      // 8192               0x00002000
#define POW_2_14            (POW_2_13 << 1)      // 16384              0x00004000
#define POW_2_15            (POW_2_14 << 1)      // 32768              0x00008000

#define POW_2_16            (POW_2_15 << 1)      // 65536              0x00010000
#define POW_2_17            (POW_2_16 << 1)      // 131072             0x00020000
#define POW_2_18            (POW_2_17 << 1)      // 262144             0x00040000
#define POW_2_19            (POW_2_18 << 1)      // 524288             0x00080000

#define POW_2_20            (POW_2_19 << 1)      // 1048576            0x00100000
#define POW_2_21            (POW_2_20 << 1)      // 2097152            0x00200000
#define POW_2_22            (POW_2_21 << 1)      // 4194304            0x00400000
#define POW_2_23            (POW_2_22 << 1)      // 8388608            0x00800000

#define POW_2_24            (POW_2_23 << 1)      // 16777216           0x01000000
#define POW_2_25            (POW_2_24 << 1)      // 33554432           0x02000000
#define POW_2_26            (POW_2_25 << 1)      // 67108864           0x04000000
#define POW_2_27            (POW_2_26 << 1)      // 134217728          0x08000000

#define POW_2_28            (POW_2_27 << 1)      // 268435456          0x10000000
#define POW_2_29            (POW_2_28 << 1)      // 536870912          0x20000000
#define POW_2_30            (POW_2_29 << 1)      // 1073741824         0x40000000
#define POW_2_31            (POW_2_30 << 1)      // 2147483648         0x80000000



#define SIZEOF_CHAR         ( sizeof(char) )
#define SIZEOF_SHORT        ( sizeof(short) )
#define SIZEOF_INT          ( sizeof(int) )
#define SIZEOF_LONG         ( sizeof(long) )
#define SIZEOF_LLONG        ( sizeof(long long) )
#define SIZEOF_LOFF_T       ( sizeof(loff_t) )
#define SIZEOF_FLOAT        ( sizeof(float) )
#define SIZEOF_DOUBLE       ( sizeof(double) )
#ifndef SIZEOF_POINTER
	#define SIZEOF_POINTER      ( sizeof(int*) )
#endif
#define SIZEOF_TIMEVAL      ( sizeof(struct timeval) )
#define SIZEOF_IFREQ        ( sizeof(struct ifreq) )
#ifndef SIZEOF_LINGER
	#define SIZEOF_LINGER       ( sizeof(struct linger) )
#endif
#define SIZEOF_SOCKADDR_IN  ( sizeof(struct sockaddr_in) )
#define SIZEOF_SOCKADDR     ( sizeof(struct sockaddr) )
#define SIZEOF_ICMP         ( sizeof(icmp_echo_reply) )
//#define SIZEOF_MSGHDR       ( sizeof(struct msghdr) )  //linux
#define SIZEOF_WSAMSG       ( sizeof(struct WSAMSG) )    //windows msghdr
//#define SIZEOF_IP           ( sizeof(IPV4_HDR) )		 //linux

#define SIZEOF_OFF_T        ( sizeof(off_t) )

/**********************************************************
 *           LENGTH definition
 **********************************************************/
#define LEN_ARG_CHAR	(sizeof(char))
#define LEN_ARG_SHORT	(sizeof(short))
#define LEN_ARG_INT	(sizeof(int))
#define LEN_ARG_LONG	(sizeof(long))
#define LEN_ARG_LLONG	(sizeof(long long))
#define LEN_ARG_LOFF_T	(sizeof(loff_t))

#define LEN_TYPE        (LEN_ARG_CHAR)
#define LEN_ARG_LEN     (LEN_ARG_INT)
#define LEN_ARG_HDR     (LEN_TYPE + LEN_ARG_LEN)

#define LEN_NL_MSG_LEN	(4)
#define LEN_NL_MSG_TYPE	(1)
#define LEN_NL_ARG_TYPE	(1)
//#define LEN_NL_MSG_HDR	(LEN_NL_MSG_LEN + LEN_NL_MSG_TYPE + LEN_NL_ARG_TYPE)

#define LEN_MDP_MSG_TYPE			(1)
#define LEN_MDP_MSG_FIX_TYPE_LEN	(1)
#define LEN_NULL_CHAR				(1)

#define DF_PATH_LEN         (128)
#ifndef MAX_PATH_LEN
	#define MAX_PATH_LEN        (260)
#endif

#endif
