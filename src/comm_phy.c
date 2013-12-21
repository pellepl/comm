/*
 * comm_phy.c
 *
 *  Created on: Jun 13, 2012
 *      Author: petera
 */
#include "comm.h"

void comm_phy_init(comm *co, comm_phy_rx_char_fn rx) {
  co->phy.rx = rx;
  co->phy.up_rx_f = comm_link_rx;
  co->phy.max_conseq_tmo = COMM_MAX_CONSEQ_TMO;
}

int comm_phy_receive(comm* co) {
  unsigned char c;
  int res = co->phy.rx(&c);
  if (res == R_COMM_OK) {
    co->phy.last_was_tmo = 0;
    co->phy.conseq_tmo = 0;
    res = co->phy.up_rx_f(co, c, 0);
    return res;
  } else {
    if (res == R_COMM_PHY_TMO) {
      if (co->phy.last_was_tmo) {
        co->phy.conseq_tmo++;
        if (co->phy.max_conseq_tmo > 0 && co->phy.conseq_tmo == co->phy.max_conseq_tmo) {
          co->phy.conseq_tmo = 0;
          co->app.user_err_f(co, COMM_NO_SEQNO, res, 0, 0);
        }
      } else {
        co->phy.last_was_tmo = 1;
      }
    } else {
      co->app.user_err_f(co, COMM_NO_SEQNO, res, 0, 0);
    }
    co->lnk.phy_err_f(co, res);
    COMM_PHY_DBG("rx fail %i", res);
  }
  return res;
}
