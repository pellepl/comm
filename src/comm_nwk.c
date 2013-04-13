/*
 * comm_nwk.c
 *
 *  Created on: Jun 8, 2012
 *      Author: petera
 */

#include "comm.h"

int comm_nwk_rx(comm *comm, comm_arg* rx) {
  unsigned char addresses = *rx->data;
  comm_addr src = (addresses & 0xf0) >> 4;
  comm_addr dst = (addresses & 0x0f);
  if (src == comm->nwk.addr && COMM_USER_DIFFERENTIATION) {
    return R_COMM_NWK_TO_SELF;
  }
  rx->data += COMM_H_SIZE_NWK;
  rx->len -= COMM_H_SIZE_NWK;
  if (dst == COMM_NWK_BRDCAST || dst == comm->nwk.addr || !COMM_USER_DIFFERENTIATION) {
    rx->src = src;
    rx->dst = dst;
    COMM_NWK_DBG("pkt from %x, len %i", src, rx->len);
    return comm->nwk.up_rx_f(comm, rx);
  } else {
    COMM_NWK_DBG("pkt not to me (%x), but to %x", comm->nwk.addr, dst);
    return R_COMM_NWK_NOT_ME;
  }
}

int comm_nwk_tx(comm *comm, comm_arg* tx) {
  comm_addr dst = tx->dst;
  if (dst == comm->nwk.addr && COMM_USER_DIFFERENTIATION) {
    return R_COMM_NWK_TO_SELF;
  }
  if (dst > COMM_MAX_USERS && COMM_USER_DIFFERENTIATION) {
    return R_COMM_NWK_BAD_ADDR;
  }
  tx->data -= COMM_H_SIZE_NWK;
  tx->len += COMM_H_SIZE_NWK;
  *tx->data = ((comm->nwk.addr << 4) & 0xf0) | (dst & 0x0f);
  return comm->nwk.down_tx_f(comm, tx);
}

void comm_nwk_init(comm *comm, comm_addr addr, comm_rx_fn up_rx_f, comm_tx_fn down_tx_f) {
  comm->nwk.addr = addr;
  comm->nwk.up_rx_f = up_rx_f;
  comm->nwk.down_tx_f = down_tx_f;
}
