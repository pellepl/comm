/*
 * comm_phy.c
 *
 *  Created on: Jun 13, 2012
 *      Author: petera
 */
#include "comm.h"

void comm_phy_init(comm *comm, comm_phy_rx_char_fn rx) {
  comm->phy.rx = rx;
  comm->phy.up_rx_f = comm_link_rx;
  comm->phy.max_conseq_tmo = COMM_MAX_CONSEQ_TMO;
}

int comm_phy_receive(comm* comm) {
  unsigned char c;
  int res = comm->phy.rx(&c);
  if (res == R_COMM_OK) {
    comm->phy.last_was_tmo = 0;
    comm->phy.conseq_tmo = 0;
    res = comm->phy.up_rx_f(comm, c, 0);
    return res;
  } else {
    if (res == R_COMM_PHY_TMO) {
      if (comm->phy.last_was_tmo) {
        comm->phy.conseq_tmo++;
        if (comm->phy.max_conseq_tmo > 0 && comm->phy.conseq_tmo == comm->phy.max_conseq_tmo) {
          comm->phy.conseq_tmo = 0;
          comm->app.user_err_f(comm, COMM_NO_SEQNO, res, 0, 0);
        }
      } else {
        comm->phy.last_was_tmo = 1;
      }
    } else {
      comm->app.user_err_f(comm, COMM_NO_SEQNO, res, 0, 0);
    }
    comm->lnk.phy_err_f(comm, res);
    COMM_PHY_DBG("rx fail %i", res);
  }
  return res;
}
