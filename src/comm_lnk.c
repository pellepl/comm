/*
 * comm_lnk.c
 *
 *  Created on: Jun 8, 2012
 *      Author: petera
 */

#include "comm.h"


#define COMM_LNK_PREAMBLE     0x5a
#define COMM_LNK_STATE_PRE    0
#define COMM_LNK_STATE_LEN    1
#define COMM_LNK_STATE_DAT    2
#define COMM_LNK_STATE_CRC    3

#define COMM_CRC_INIT         0xffff

static unsigned short _crc_ccitt_16(unsigned short crc, unsigned char data) {
  crc  = (unsigned char)(crc >> 8) | (crc << 8);
  crc ^= data;
  crc ^= (unsigned char)(crc & 0xff) >> 4;
  crc ^= (crc << 8) << 4;
  crc ^= ((crc & 0xff) << 4) << 1;
  return crc;
}

int comm_link_rx(comm *comm, unsigned char c, unsigned char *fin) {
  switch (comm->lnk.state) {
  case COMM_LNK_STATE_PRE:
    if (c == COMM_LNK_PREAMBLE) {
      comm->lnk.state = COMM_LNK_STATE_LEN;
      //COMM_LNK_DBG("pre ok");
    } else {
      COMM_LNK_DBG("preamble fail, got 0x%02x\n",c);
      return R_COMM_LNK_PRE_FAIL;
    }
    break;
  case COMM_LNK_STATE_LEN:
    comm->lnk.len = c + 1;
    comm->lnk.l_crc = COMM_CRC_INIT;
    comm->lnk.ix = 0;
    comm->lnk.state = COMM_LNK_STATE_DAT;
    COMM_LNK_DBG("len %i", c);
    break;
  case COMM_LNK_STATE_DAT:
    //COMM_LNK_DBG("dat %02x @ %i", c, comm->lnk.ix);
    comm->lnk.buf[comm->lnk.ix++] = c;
    comm->lnk.l_crc = _crc_ccitt_16(comm->lnk.l_crc, c);
    if (comm->lnk.ix == comm->lnk.len) {
      comm->lnk.ix = 1;
      comm->lnk.state = COMM_LNK_STATE_CRC;
    }

    break;
  case COMM_LNK_STATE_CRC:
    //COMM_LNK_DBG("crc %02x @ %i", c, comm->lnk.ix);
    if (comm->lnk.ix == 1) {
      comm->lnk.r_crc = (c << 8);
      comm->lnk.ix = 0;
    } else {
      comm->lnk.r_crc |= c;
      comm->lnk.state = COMM_LNK_STATE_PRE;
      if (comm->lnk.l_crc == comm->lnk.r_crc) {
        comm->lnk.rx_arg->data = comm->lnk.buf;
        comm->lnk.rx_arg->len = comm->lnk.len;
        comm_arg * rx_arg = comm->lnk.rx_arg;
#if COMM_LNK_DOUBLE_RX_BUFFER
        if (comm->lnk.buf == &comm->lnk._buf1[0]) {
          comm->lnk.buf = &comm->lnk._buf2[0];
          comm->lnk.rx_arg = &comm->lnk._rx_arg2;
        } else {
          comm->lnk.buf = &comm->lnk._buf1[0];
          comm->lnk.rx_arg = &comm->lnk._rx_arg1;
        }
#elif COMM_LNK_ALLOCATE_RX_BUFFER
        // allocate new fresh instances of link buffer data and link argument
        // for next packet
        comm->lnk.alloc_f(comm,
            (void**)&comm->lnk.buf, (void**)&comm->lnk.rx_arg,
            COMM_LNK_MAX_DATA, sizeof(comm_arg));
#endif
        int res = comm->lnk.up_rx_f(comm, rx_arg);
#if COMM_LNK_ALLOCATE_RX_BUFFER
        // remove previous allocations as it is now handled
        comm->lnk.free_f(comm, rx_arg->data, rx_arg);
#endif
        if (fin) *fin = 1;
        return res;
      } else {
        COMM_LNK_DBG("crc fail, len %i, remote %04x, local %04x\n", comm->lnk.len, comm->lnk.l_crc, comm->lnk.r_crc);
        return R_COMM_LNK_CRC_FAIL;
      }
    }
    break;
  }
  return R_COMM_OK;
}

int comm_link_tx(comm *comm, comm_arg* tx) {
  int res;
  unsigned short crc = COMM_CRC_INIT;
  unsigned int i;

  unsigned short len = tx->len;
  unsigned char *buf = tx->data;

  for (i = 0; i < len; i++) {
    crc = _crc_ccitt_16(crc, buf[i]);
  }

  res = comm->lnk.phy_tx_f(COMM_LNK_PREAMBLE);
  if (res != R_COMM_OK) {
    return res;
  }

  res = comm->lnk.phy_tx_f(len-1);
  if (res != R_COMM_OK) {
    return res;
  }

  if (comm->lnk.phy_tx_buf_f) {
    res = comm->lnk.phy_tx_buf_f(buf, len);
  } else {
    for (i = 0; i < len; i++) {
      res = comm->lnk.phy_tx_f(buf[i]);
      if (res != R_COMM_OK) {
        return res;
      }
    }
  }

  res = comm->lnk.phy_tx_f((crc>>8) & 0xff);
  if (res != R_COMM_OK) {
    return res;
  }

  res = comm->lnk.phy_tx_f(crc & 0xff);
  if (res != R_COMM_OK) {
    return res;
  }

  if (comm->lnk.phy_tx_flush_f) {
    res = comm->lnk.phy_tx_flush_f(comm, tx);
  }
  return res;
}

void comm_lnk_phy_err(comm *comm, int err) {
  // reset state on timeout
  if (err == R_COMM_PHY_TMO && comm->lnk.state != COMM_LNK_STATE_PRE) {
    comm->lnk.state = COMM_LNK_STATE_PRE;
  }
}

void comm_init_alloc(comm *comm, comm_lnk_alloc_rx_fn alloc_f, comm_lnk_free_rx_fn free_f) {
  // NB, the stack will first allocate a buffer which will be used to fill with packet data.
  // When packet has been received successfully, a new buffer will be allocated for nextcoming packet
  // and previous buffer will be sent up in stack. When stack returns to link layer, the previous
  // buffer will be freed. The stack will this always have at least one buffer allocated while waiting
  // for packet, and at least two buffers allocated while reporting a packet up to upper layers.
  // Would the upper layers switch thread context or similar, more buffers could be simultaneously
  // allocated.
  comm->lnk.alloc_f = alloc_f;
  comm->lnk.free_f = free_f;
  // allocate first instance of link buffer and link arg
  alloc_f(comm, (void**)&comm->lnk.buf, (void**)&comm->lnk.rx_arg, COMM_LNK_MAX_DATA, sizeof(comm_arg));
}

void comm_link_init(comm *comm, comm_rx_fn up_rx_f, comm_phy_tx_char_fn phy_tx_f, comm_phy_tx_buf_fn phy_tx_buf_f,
    comm_phy_tx_flush_fn phy_tx_flush_f) {
  comm->lnk.up_rx_f = up_rx_f;
  comm->lnk.phy_tx_f = phy_tx_f;
  comm->lnk.phy_tx_buf_f = phy_tx_buf_f;
  comm->lnk.phy_tx_flush_f = phy_tx_flush_f;
  comm->lnk.state = COMM_LNK_STATE_PRE;
  comm->lnk.phy_err_f = comm_lnk_phy_err;
#if COMM_LNK_DOUBLE_RX_BUFFER
  comm->lnk.buf = &comm->lnk._buf1[0];
  comm->lnk.rx_arg = &comm->lnk._rx_arg1;
#elif COMM_LNK_ALLOCATE_RX_BUFFER
#else
  comm->lnk.buf = &comm->lnk._buf[0];
  comm->lnk.rx_arg = &comm->lnk._rx_arg;
#endif
}
