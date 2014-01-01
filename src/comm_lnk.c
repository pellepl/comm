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

static int comm_link_packet_ready(comm *co) {
  co->lnk.rx_arg->data = &co->lnk.buf[1];
  co->lnk.rx_arg->len = co->lnk.len;
  comm_arg * rx_arg = co->lnk.rx_arg;
#if COMM_LNK_DOUBLE_RX_BUFFER
  if (co->lnk.buf == &co->lnk._buf1[0]) {
    co->lnk.buf = &co->lnk._buf2[0];
    co->lnk.rx_arg = &co->lnk._rx_arg2;
  } else {
    co->lnk.buf = &co->lnk._buf1[0];
    co->lnk.rx_arg = &co->lnk._rx_arg1;
  }
#elif COMM_LNK_ALLOCATE_RX_BUFFER
  // allocate new fresh instances of link buffer data and link argument
  // for next packet
  co->lnk.alloc_f(co,
      (void**)&co->lnk.buf, (void**)&co->lnk.rx_arg,
      COMM_LNK_MAX_DATA, sizeof(comm_arg));
#endif
  int res = co->lnk.up_rx_f(co, rx_arg);
#if COMM_LNK_ALLOCATE_RX_BUFFER
  // remove previous allocations as it is now handled
  co->lnk.free_f(co, rx_arg->data, rx_arg);
#endif
  return res;
}

int comm_link_rx(comm *co, unsigned char c, unsigned char *fin) {
  switch (co->lnk.state) {
  case COMM_LNK_STATE_PRE:
    if (c == COMM_LNK_PREAMBLE) {
      co->lnk.state = COMM_LNK_STATE_LEN;
      //COMM_LNK_DBG("pre ok");
    } else {
      COMM_LNK_DBG("preamble fail, got 0x%02x\n",c);
      return R_COMM_LNK_PRE_FAIL;
    }
    break;
  case COMM_LNK_STATE_LEN:
	if (c > COMM_LNK_MAX_DATA-1) {
      co->lnk.state = co->conf & COMM_CONF_SKIP_PREAMPLE ? COMM_LNK_STATE_LEN : COMM_LNK_STATE_PRE;
      COMM_LNK_DBG("bad length, got 0x%02x\n",c);
      return R_COMM_LNK_LEN_BAD;
	}
    co->lnk.len = c + 1;
    co->lnk.l_crc = COMM_CRC_INIT;
    co->lnk.ix = 0;
    co->lnk.state = COMM_LNK_STATE_DAT;
    COMM_LNK_DBG("len %i", c);
    break;
  case COMM_LNK_STATE_DAT:
    //COMM_LNK_DBG("dat %02x @ %i", c, co->lnk.ix);
    co->lnk.buf[co->lnk.ix++] = c;
    if ((co->conf & COMM_CONF_SKIP_CRC) == 0) {
      co->lnk.l_crc = _crc_ccitt_16(co->lnk.l_crc, c);
    }
    if (co->lnk.ix == co->lnk.len) {
      if (co->conf & COMM_CONF_SKIP_CRC) {
        co->lnk.ix = 0;
        co->lnk.state = co->conf & COMM_CONF_SKIP_PREAMPLE ? COMM_LNK_STATE_LEN : COMM_LNK_STATE_PRE;
        int res = comm_link_packet_ready(co);
        if (fin) *fin = 1;
        return res;
      } else {
        co->lnk.ix = 1;
        co->lnk.state = COMM_LNK_STATE_CRC;
      }
    }

    break;
  case COMM_LNK_STATE_CRC:
    //COMM_LNK_DBG("crc %02x @ %i", c, co->lnk.ix);
    if (co->lnk.ix == 1) {
      co->lnk.r_crc = (c << 8);
      co->lnk.ix = 0;
    } else {
      co->lnk.r_crc |= c;
      co->lnk.state = co->conf & COMM_CONF_SKIP_PREAMPLE ? COMM_LNK_STATE_LEN : COMM_LNK_STATE_PRE;
      if (co->lnk.l_crc == co->lnk.r_crc) {
        int res = comm_link_packet_ready(co);
        if (fin) *fin = 1;
        return res;
      } else {
        COMM_LNK_DBG("crc fail, len %i, remote %04x, local %04x\n", co->lnk.len, co->lnk.l_crc, co->lnk.r_crc);
        return R_COMM_LNK_CRC_FAIL;
      }
    }
    break;
  }
  return R_COMM_OK;
}

int comm_link_tx(comm *co, comm_arg* tx) {
  int res = R_COMM_OK;
  unsigned short crc = COMM_CRC_INIT;
  unsigned int i;

  unsigned short len = tx->len;
  unsigned char *buf = tx->data;

  if (co->lnk.phy_tx_f) {
    if ((co->conf & COMM_CONF_SKIP_CRC) == 0) {
      for (i = 0; i < len; i++) {
        crc = _crc_ccitt_16(crc, buf[i]);
      }
    }

    if ((co->conf & COMM_CONF_SKIP_PREAMPLE) == 0) {
      res = co->lnk.phy_tx_f(COMM_LNK_PREAMBLE);
      if (res != R_COMM_OK) {
        return res;
      }
    }

    res = co->lnk.phy_tx_f(len-1);
    if (res != R_COMM_OK) {
      return res;
    }

    if (co->lnk.phy_tx_buf_f) {
      res = co->lnk.phy_tx_buf_f(buf, len);
    } else {
      for (i = 0; i < len; i++) {
        res = co->lnk.phy_tx_f(buf[i]);
        if (res != R_COMM_OK) {
          return res;
        }
      }
    }

    if ((co->conf & COMM_CONF_SKIP_CRC) == 0) {
      res = co->lnk.phy_tx_f((crc>>8) & 0xff);
      if (res != R_COMM_OK) {
        return res;
      }

      res = co->lnk.phy_tx_f(crc & 0xff);
      if (res != R_COMM_OK) {
        return res;
      }
    }
  }

  if (co->lnk.phy_tx_flush_f) {
    res = co->lnk.phy_tx_flush_f(co, tx);
  }
  return res;
}

void comm_lnk_phy_err(comm *co, int err) {
  // reset state on timeout
  if (err == R_COMM_PHY_TMO && co->lnk.state != COMM_LNK_STATE_PRE) {
    co->lnk.state = COMM_LNK_STATE_PRE;
  }
}

void comm_init_alloc(comm *co, comm_lnk_alloc_rx_fn alloc_f, comm_lnk_free_rx_fn free_f) {
  // NB, the stack will first allocate a buffer which will be used to fill with packet data.
  // When packet has been received successfully, a new buffer will be allocated for next-coming packet
  // and previous buffer will be sent up in stack. When stack returns to link layer, the previous
  // buffer will be freed. The stack will this way always have at least one buffer allocated while
  // waiting for packet, and at least two buffers allocated while reporting a packet up to upper
  // layers. Would the upper layers switch thread context or similar, more buffers could be
  // simultaneously allocated.
  co->lnk.alloc_f = alloc_f;
  co->lnk.free_f = free_f;
  // allocate first instance of link buffer and link arg
  alloc_f(co, (void**)&co->lnk.buf, (void**)&co->lnk.rx_arg, COMM_LNK_MAX_DATA, sizeof(comm_arg));
}

void comm_link_init(comm *co, comm_rx_fn up_rx_f, comm_phy_tx_char_fn phy_tx_f, comm_phy_tx_buf_fn phy_tx_buf_f,
    comm_phy_tx_flush_fn phy_tx_flush_f) {
  co->lnk.up_rx_f = up_rx_f;
  co->lnk.phy_tx_f = phy_tx_f;
  co->lnk.phy_tx_buf_f = phy_tx_buf_f;
  co->lnk.phy_tx_flush_f = phy_tx_flush_f;
  co->lnk.state = co->conf & COMM_CONF_SKIP_PREAMPLE ? COMM_LNK_STATE_LEN : COMM_LNK_STATE_PRE;
  co->lnk.phy_err_f = comm_lnk_phy_err;
#if COMM_LNK_DOUBLE_RX_BUFFER
  co->lnk.buf = &co->lnk._buf1[0];
  co->lnk.rx_arg = &co->lnk._rx_arg1;
#elif COMM_LNK_ALLOCATE_RX_BUFFER
#else
  co->lnk.buf = &co->lnk._buf[0];
  co->lnk.rx_arg = &co->lnk._rx_arg;
#endif
}
