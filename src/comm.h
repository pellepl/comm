/*
 * comm.h
 *
 *  Created on: Jun 8, 2012
 *      Author: petera
 */

#ifndef COMM_H_
#define COMM_H_

#include "comm_config.h"
/* errors */

#define R_COMM_OK               0

#define R_COMM_PHY_FAIL         -20000
#define R_COMM_PHY_TMO          -20001
#define R_COMM_PHY_TRY_LATER    -20002

#define R_COMM_LNK_PRE_FAIL     -20010
#define R_COMM_LNK_CRC_FAIL     -20011
#define R_COMM_LNK_LEN_BAD      -20012

#define R_COMM_NWK_NOT_ME       -20020
#define R_COMM_NWK_TO_SELF      -20021
#define R_COMM_NWK_BAD_ADDR     -20022

#define R_COMM_TRA_PEND_Q_FULL  -20030
#define R_COMM_TRA_ACK_Q_FULL   -20031
#define R_COMM_TRA_NO_ACK       -20032
#define R_COMM_TRA_CANNOT_ACK_BROADCAST -20033

#define R_COMM_APP_NOT_AN_ACK   -20040



/* protocol defines */

#ifndef COMM_LNK_MAX_DATA
#define COMM_LNK_MAX_DATA       256
#endif
#define COMM_APP_MAX_DATA       (COMM_LNK_MAX_DATA-1-2)

#define COMM_NWK_BRDCAST        ((comm_addr)0x0)

#define COMM_NO_SEQNO           (0xffff)

#if COMM_USER_DIFFERENTIATION
#define COMM_TRA_MAX_USERS       (COMM_MAX_USERS+1-2) /*do not include self and broadcast */
#else
#define COMM_TRA_MAX_USERS       1
#endif

#define COMM_TRA_INF_PING        0x01
#define COMM_TRA_INF_CONGESTION  0x02
#define COMM_TRA_INF_PONG        0x81

// bits set by txer/rxer
#define COMM_FLAG_REQACK_BIT     (1<<0)  /* indicates that ack is requested */
#define COMM_FLAG_ISACK_BIT      (1<<1)  /* indicates that this is an ack */
#define COMM_FLAG_INF_BIT        (1<<2)  /* indicates an info packet */
#define COMM_FLAG_RESENT_BIT     (1<<3)  /* indicates a resent packet */
// status flags set in transport layer
#define COMM_STAT_RESEND_BIT     (1<<4)  /* indicates a packet whose ack is already registered - ie packet is resent */
#define COMM_STAT_ACK_MISS_BIT   (1<<5)  /* indicates an ack for an already acked packet or a packet not wanting ack */
// status flags set in app layer
#define COMM_STAT_REPLY_BIT      (1<<6)  /* indicates that this message will be acked on app level */
#define COMM_STAT_ALERT_BIT      (1<<7)  /* indicates that this in an alert packet */

/* header sizes */

#define COMM_H_SIZE_LNK         0
#define COMM_H_SIZE_NWK         1
#define COMM_H_SIZE_TRA         2

#define COMM_H_SIZE_ALERT       2

#define COMM_H_SIZE (COMM_H_SIZE_LNK+COMM_H_SIZE_NWK+COMM_H_SIZE_TRA)

/* types */

typedef unsigned char comm_addr;

/*
  rx/tx structure
 */
typedef struct {
  unsigned char *data;
  unsigned short len;
  comm_addr src;
  comm_addr dst;
  unsigned char flags;
  unsigned short seqno;
  comm_time timestamp;
} comm_arg;


/* layer func types */

typedef struct comm comm;

typedef int (*comm_phy_tx_char_fn)(unsigned char c);
typedef int (*comm_phy_tx_buf_fn)(unsigned char *c, unsigned short len);
typedef int (*comm_phy_tx_flush_fn)(comm *c, comm_arg* tx);

typedef int (*comm_phy_rx_char_fn)(unsigned char *c);

typedef int (*comm_phy_lnk_rx_fn)(comm *c, unsigned char data, unsigned char *fin);
typedef void (*comm_lnk_phy_err_fn)(comm *c, int err);

typedef int (*comm_tx_fn)(comm *c, comm_arg *tx);
typedef int (*comm_rx_fn)(comm *c, comm_arg *rx);

typedef void (*comm_tra_app_ack_fn)(comm *c, comm_arg *rx);

/* user / application func types */

typedef comm_time (*comm_app_get_time_fn)(void);
/* invoked on transport info */
typedef void (*comm_tra_app_inf_fn)(comm *c, comm_arg *rx);
/* received stuff from rx->src, in here one might to call comm_app_reply  */
typedef int (*comm_app_user_rx_fn)(comm *c, comm_arg *rx,  unsigned short len, unsigned char *data);
/* received an ack */
typedef void (*comm_app_user_ack_fn)(comm *c, comm_arg *rx, unsigned short seqno, unsigned short len, unsigned char *data);
/* invoked on error */
typedef void (*comm_app_user_err_fn)(comm *c, int err, unsigned short seqno, unsigned short len, unsigned char *data);
/* invoked on transport info */
typedef void (*comm_app_user_inf_fn)(comm *c, comm_arg *rx);
/* invoked on node alert packet */
typedef void (*comm_app_user_alert_fn)(comm *c, comm_addr node_address, unsigned char type, unsigned short len, unsigned char *data);
#if COMM_LNK_ALLOCATE_RX_BUFFER
typedef void (*comm_lnk_alloc_rx_fn)(comm *c, void **data, void **arg, unsigned int data_len, unsigned int arg_len);
typedef void (*comm_lnk_free_rx_fn)(comm *c, void *data, void *arg);
#endif

/* PHY, primitive timeout and pipe error handling
 */
typedef struct {
  comm_phy_rx_char_fn rx;
  comm_phy_lnk_rx_fn up_rx_f;
  unsigned int conseq_tmo;
  unsigned int max_conseq_tmo;
  char last_was_tmo;
} comm_phy;

/* LINK - frames of 1 to 256 bytes of data, frame and crc check
   [5a] [len] [..] [CRChi] [CRClo]
 */

typedef struct {
  unsigned char state;
  unsigned short len;
  unsigned short ix;
#if COMM_LNK_DOUBLE_RX_BUFFER
  unsigned char _buf1[COMM_LNK_MAX_DATA];
  unsigned char _buf2[COMM_LNK_MAX_DATA];
  comm_arg _rx_arg1;
  comm_arg _rx_arg2;
#elif COMM_LNK_ALLOCATE_RX_BUFFER
  comm_lnk_alloc_rx_fn alloc_f;
  comm_lnk_free_rx_fn free_f;
#else
  unsigned char _buf[COMM_LNK_MAX_DATA];
  comm_arg _rx_arg;
#endif
  unsigned char *buf;
  unsigned short r_crc;
  unsigned short l_crc;
  comm_arg *rx_arg;
  comm_rx_fn up_rx_f;
  comm_phy_tx_char_fn phy_tx_f;
  comm_phy_tx_buf_fn phy_tx_buf_f;
  comm_phy_tx_flush_fn phy_tx_flush_f;
  comm_lnk_phy_err_fn phy_err_f;
} comm_lnk;


/* NETWORK - allows for point to point or broadcast communication
   255 bytes of payload
   [src7:4 | dst3:0] [..]
*/

typedef struct {
  comm_addr addr;
  comm_rx_fn up_rx_f;
  comm_tx_fn down_tx_f;
} comm_nwk;

/* TRANSPORT - ack with/without piggyback function, packet sequencing (but does not provide order), retransmission scheme
   [seqno_hi] [seqno_lo7:4 | flags3:0] [..]
   253 bytes of payload
*/

struct comm_tra_pkt {
  unsigned char busy;
  comm_time timestamp;
  unsigned char resends;
  unsigned char data[COMM_LNK_MAX_DATA];
  comm_arg arg;
};

typedef struct {
  comm_rx_fn up_rx_f;
  comm_tx_fn down_tx_f;

  unsigned char acks_tx_pend_count;
  unsigned short seqno[COMM_TRA_MAX_USERS];
  unsigned short acks_rx_pend[COMM_TRA_MAX_USERS][COMM_MAX_PENDING];
  struct comm_tra_pkt acks_tx_pend[COMM_MAX_PENDING];
} comm_tra;

/* APPLICATION
 */

typedef struct {
  comm_app_user_rx_fn user_up_rx_f;
  comm_app_user_ack_fn user_ack_f;
  comm_app_user_err_fn user_err_f;
  comm_app_user_inf_fn user_inf_f;
  comm_app_user_alert_fn alert_f;
  comm_app_get_time_fn get_time_f;
  comm_tra_app_ack_fn ack_f;
  comm_tra_app_inf_fn inf_f;
  comm_tx_fn app_tx_f;
} comm_app;

/* DEVICE
 */
struct comm {
  comm_phy phy;
  comm_lnk lnk;
  comm_nwk nwk;
  comm_tra tra;
  comm_app app;
  unsigned char conf;
#ifdef COMM_STATS
  struct {
    unsigned int tx; // packets requiring ack
    unsigned int tx_fail; // resent packets requiring ack
  } stat;
#endif
};

#define COMM_CONF_SKIP_PREAMPLE      (1<<0)
#define COMM_CONF_SKIP_CRC           (1<<1)

/* layer funcs */

void comm_app_init(
    comm *c,
    comm_app_user_rx_fn up_rx_f,
    comm_app_user_ack_fn ack_f,
    comm_tx_fn app_tx_f,
    comm_app_get_time_fn get_time_f,
    comm_app_user_err_fn err_f,
    comm_app_user_inf_fn inf_f,
    comm_app_user_alert_fn alert_f);
int comm_app_rx(comm *c, comm_arg* rx);
int comm_app_tx(comm *c, comm_arg* tx);
void comm_app_ack(comm *c, comm_arg *rx);

void comm_tra_init(comm *c, comm_rx_fn up_rx_f, comm_tx_fn nwk_tx_f);
int comm_tra_rx(comm *c, comm_arg* rx);
int comm_tra_tx(comm *c, comm_arg* tx);
void comm_tra_tick(comm *c, comm_time time);

void comm_nwk_init(comm *c, comm_addr addr, comm_rx_fn up_rx_f, comm_tx_fn lnk_tx_f);
int comm_nwk_rx(comm *c, comm_arg* rx);
int comm_nwk_tx(comm *c, comm_arg* tx);

void comm_link_init(comm *c, comm_rx_fn up_rx_f, comm_phy_tx_char_fn phy_tx_f, comm_phy_tx_buf_fn phy_tx_buf_f,
    comm_phy_tx_flush_fn phy_tx_flush_f);
int comm_link_rx(comm *c, unsigned char data, unsigned char *fin);
int comm_link_tx(comm *c, comm_arg* tx);
void comm_lnk_phy_err(comm *c, int err);

void comm_phy_init(comm *c, comm_phy_rx_char_fn rx);

/* api funcs */

/* Note - these functions are not thread safe. comm_tick, comm_tx or comm_phy_receive
   should not run simultaneously
 */

/* Initialize the comm stack
   @param comm      the comm struct
   @param conf      0, or COMM_CONF_SKIP_PREAMPLE and/or COMM_CONF_SKIP_CRC
   @param this_addr address of this node
   @param rx        function for receiving a character
   @param tx        function for sending a character
   @param tx_buf    function for sending buffer (may be NULL)
   @param tx_flush  function for flushing a packet (may be NULL)
   @param get_time  function for getting time
   @param user_rx   callback when packet is received
   @param user_ack  callback when a sent packet is acked
   @param user_err  callback when an error occurs
   @param user_inf  callback when stack reports communication information
   @param alert     callback when stack receives a node alert packet
 */
void comm_init(
    comm *c,
    unsigned char conf,
    comm_addr this_addr,
    comm_phy_rx_char_fn rx, comm_phy_tx_char_fn tx, comm_phy_tx_buf_fn tx_buf, comm_phy_tx_flush_fn tx_flush,
    comm_app_get_time_fn get_time,
    comm_app_user_rx_fn user_rx, comm_app_user_ack_fn user_ack,
    comm_app_user_err_fn user_err, comm_app_user_inf_fn user_inf, comm_app_user_alert_fn alert);
#if COMM_LNK_ALLOCATE_RX_BUFFER
/* If COMM_LNK_ALLOCATE_RX_BUFFER configuration is on, comm_init_alloc must be called
   directly after comm_init and before comm stack is used.
   This will make link layer call alloc_f when a packet is received and use the returned buffer
   for reporting packet up in stack to user.
   free_f will be called when upcall to stack from link layer has finished.
 */
void comm_init_alloc(comm *c, comm_lnk_alloc_rx_fn alloc_f, comm_lnk_free_rx_fn free_f);
#endif
/* Enable or disable protocol parts
   @param comm    the comm stack struct
   @param conf    COMM_CONF_SKIP_PREAMPLE and/or COMM_CONF_SKIP_CRC
 */
void comm_conf(comm *c, unsigned char conf);
/* Calls comm_phy_rx_char_fn given in init and propagates it thru stack.
 */
int comm_phy_receive(comm *c);
/* Call this in a system tick func to dispatch acks and resend unacked stuff.
 */
void comm_tick(comm *c, comm_time time);
/* Sends stuff to dst, returns negative for err or seqno.
   If packet is acked, user will be callbacked in user_ack function in comm_init.
   If packet is never acked but is supposed to, user will be callbacked in user_err function
   in comm_init.
   Any other error will be callbacked in user_err.
   @param comm    the comm stack struct
   @param dst     destination of packet
   @param len     length of packet
   @param data    contents of packet
   @param app_ack if packet is supposed to be acked upon reception at destination
 */
int comm_tx(comm *c, comm_addr dst, unsigned int len, unsigned char *data, int app_ack);
/* Directly returns answer to a received pkt as piggyback on ack. This must
   be invoked within the stack call to user_rx function given in comm_init.
   @param comm    the comm stack struct
   @param rx      the rx packet argument given in user_rx function upon reception
   @param len     the length of the reply data
   @param data    the reply data
 */
int comm_reply(comm *c, comm_arg* rx, unsigned short len, unsigned char *data);
/* Pings given node. Response will be callbacked to user_inf function in comm_init
 */
int comm_ping(comm *c, comm_addr dst);
/* Alerts other nodes of this node
   @param comm    the comm stack struct
   @param type    the type of alert, user defined
   @param len     the length of the alert data
   @param data    the data contents of the alert
 */
int comm_alert(comm *c, unsigned char type, unsigned short len, unsigned char *data);

#ifdef COMM_STATS
void comm_clear_stats(comm *c);
unsigned char comm_squal(comm *c);
#endif
#endif /* COMM_H_ */
