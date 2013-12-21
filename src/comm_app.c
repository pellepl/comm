/*
 * comm_app.c
 *
 *  Created on: Jun 13, 2012
 *      Author: petera
 */

#include "comm.h"

void comm_app_init(
    comm *co,
    comm_app_user_rx_fn up_rx_f,
    comm_app_user_ack_fn ack_f,
    comm_tx_fn app_tx_f,
    comm_app_get_time_fn get_time_f,
    comm_app_user_err_fn err_f,
    comm_app_user_inf_fn inf_f,
    comm_app_user_alert_fn alert_f) {
  co->app.app_tx_f = app_tx_f;
  co->app.user_ack_f = ack_f;
  co->app.user_up_rx_f = up_rx_f;
  co->app.user_err_f = err_f;
  co->app.get_time_f = get_time_f;
  co->app.ack_f = comm_app_ack;
  co->app.inf_f = inf_f;
  co->app.alert_f = alert_f;
}

int comm_app_rx(comm *co, comm_arg *rx) {
   return co->app.user_up_rx_f(co, rx, rx->len, rx->len > 0 ? rx->data : 0);
}

void comm_app_ack(comm *co, comm_arg *rx) {
   return co->app.user_ack_f(co, rx, rx->seqno, rx->len, rx->len > 0 ? rx->data : 0);
}

void comm_app_inf(comm *co, comm_arg *rx) {
   return co->app.user_inf_f(co, rx);
}

int comm_app_tx(comm *co, comm_arg *tx) {
  int res;
  COMM_LOCK(comm);
  res = co->app.app_tx_f(co, tx);
  COMM_UNLOCK(comm);
  return res;
}

