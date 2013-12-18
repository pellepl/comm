#include "comm.h"


void comm_init(
    comm *comm,
    unsigned char conf,
    comm_addr this_addr,
    comm_phy_rx_char_fn rx, comm_phy_tx_char_fn tx, comm_phy_tx_buf_fn tx_buf, comm_phy_tx_flush_fn tx_flush,
    comm_app_get_time_fn get_time,
    comm_app_user_rx_fn user_rx, comm_app_user_ack_fn user_ack,
    comm_app_user_err_fn user_err, comm_app_user_inf_fn user_inf, comm_app_user_alert_fn alert)
{
  COMM_MEMSET(comm, 0, sizeof(comm));
  comm->conf = conf;
  comm_phy_init(comm, rx);
  comm_link_init(comm, comm_nwk_rx, tx, tx_buf, tx_flush);
  comm_nwk_init(comm, this_addr, comm_tra_rx, comm_link_tx);
  comm_tra_init(comm, comm_app_rx, comm_nwk_tx);
  comm_app_init(comm, user_rx, user_ack, comm_tra_tx, get_time, user_err, user_inf, alert);
}

void comm_conf(comm *comm, unsigned char conf) {
  comm->conf = conf;
}

void comm_tick(comm *comm, comm_time time) {
  COMM_LOCK(comm);
  comm_tra_tick(comm, time);
  COMM_UNLOCK(comm);
}

static unsigned char _comm_data[COMM_LNK_MAX_DATA];
int comm_tx(comm *comm, comm_addr dst, unsigned int len, unsigned char *data, int ack) {
  comm_arg tx;
  if (data != 0 && len > 0) COMM_MEMCPY(&_comm_data[COMM_H_SIZE], data, len);
  tx.dst = dst;
  tx.len = len;
  tx.data = &_comm_data[COMM_H_SIZE];
  tx.flags = ack ? COMM_FLAG_REQACK_BIT : 0;
  tx.timestamp = comm->app.get_time_f();
  int res = comm_app_tx(comm, &tx);
  return res == R_COMM_OK ? tx.seqno : res;
}

int comm_alert(comm *comm, unsigned char type, unsigned short len, unsigned char *data) {
  comm_arg tx;
  if (data != 0 && len > 0) COMM_MEMCPY(&_comm_data[COMM_H_SIZE - COMM_H_SIZE_TRA + COMM_H_SIZE_ALERT], data, len);
  tx.len = len + COMM_H_SIZE_ALERT;
  tx.data = &_comm_data[COMM_H_SIZE - COMM_H_SIZE_TRA + COMM_H_SIZE_ALERT];
  tx.data[0] = comm->nwk.addr;
  tx.data[1] = type;
  tx.flags = COMM_STAT_ALERT_BIT;
  tx.timestamp = comm->app.get_time_f();
  int res = comm_app_tx(comm, &tx);
  return res;
}


int comm_reply(comm *comm, comm_arg* rtx, unsigned short len, unsigned char *data) {
  if ((rtx->flags & COMM_FLAG_REQACK_BIT) == 0) {
    return R_COMM_APP_NOT_AN_ACK;
  }
  if (data != 0 && len > 0) COMM_MEMCPY(rtx->data, data, len);
  rtx->len = len;
  rtx->flags = COMM_STAT_REPLY_BIT | COMM_FLAG_ISACK_BIT;
  comm_addr src = rtx->src;
  rtx->src = rtx->dst;
  rtx->dst = src;
  rtx->timestamp = comm->app.get_time_f();
  return comm_app_tx(comm, rtx);
}

int comm_ping(comm *comm, comm_addr dst) {
  comm_arg tx;
  tx.dst = dst;
  tx.len = 1;
  tx.data = &_comm_data[COMM_H_SIZE];
  tx.data[0] = COMM_TRA_INF_PING;
  tx.flags = COMM_FLAG_INF_BIT;
  tx.timestamp = comm->app.get_time_f();
  return comm_app_tx(comm, &tx);
}
