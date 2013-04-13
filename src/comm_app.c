/*
 * comm_app.c
 *
 *  Created on: Jun 13, 2012
 *      Author: petera
 */

#include "comm.h"

void comm_app_init(comm *comm, comm_app_user_rx_fn up_rx_f, comm_app_user_ack_fn ack_f, comm_tx_fn app_tx_f, comm_app_get_time_fn get_time_f, comm_app_user_err_fn err_f,
                   comm_app_user_inf_fn inf_f) {
  comm->app.app_tx_f = app_tx_f;
  comm->app.user_ack_f = ack_f;
  comm->app.user_up_rx_f = up_rx_f;
  comm->app.user_err_f = err_f;
  comm->app.get_time_f = get_time_f;
  comm->app.ack_f = comm_app_ack;
  comm->app.inf_f = inf_f;
}

int comm_app_rx(comm *comm, comm_arg *rx) {
   return comm->app.user_up_rx_f(comm, rx, rx->len, rx->len > 0 ? rx->data : 0);
}

void comm_app_ack(comm *comm, comm_arg *rx) {
   return comm->app.user_ack_f(comm, rx, rx->seqno, rx->len, rx->len > 0 ? rx->data : 0);
}

void comm_app_inf(comm *comm, comm_arg *rx) {
   return comm->app.user_inf_f(comm, rx);
}

int comm_app_tx(comm *comm, comm_arg *tx) {
  int res;
  COMM_LOCK(comm);
  res = comm->app.app_tx_f(comm, tx);
  COMM_UNLOCK(comm);
  return res;
}

