#include "comm.h"


void comm_init(
    comm *co,
    unsigned char conf,
    comm_addr this_addr,
    comm_phy_rx_char_fn rx, comm_phy_tx_char_fn tx, comm_phy_tx_buf_fn tx_buf, comm_phy_tx_flush_fn tx_flush,
    comm_app_get_time_fn get_time,
    comm_app_user_rx_fn user_rx, comm_app_user_ack_fn user_ack,
    comm_app_user_err_fn user_err, comm_app_user_inf_fn user_inf, comm_app_user_alert_fn alert)
{
  COMM_MEMSET(co,  0, sizeof(comm));
  co->conf = conf;
  comm_phy_init(co,  rx);
  comm_link_init(co,  comm_nwk_rx, tx, tx_buf, tx_flush);
  comm_nwk_init(co,  this_addr, comm_tra_rx, comm_link_tx);
  comm_tra_init(co,  comm_app_rx, comm_nwk_tx);
  comm_app_init(co,  user_rx, user_ack, comm_tra_tx, get_time, user_err, user_inf, alert);
}

void comm_conf(comm *co, unsigned char conf) {
  co->conf = conf;
}

void comm_tick(comm *co, comm_time time) {
  COMM_LOCK(comm);
  comm_tra_tick(co,  time);
  COMM_UNLOCK(comm);
}

static unsigned char _comm_data[COMM_LNK_MAX_DATA];
int comm_tx(comm *co, comm_addr dst, unsigned int len, unsigned char *data, int ack) {
  comm_arg tx;
  if (data != 0 && len > 0) COMM_MEMCPY(&_comm_data[COMM_H_SIZE], data, len);
  tx.dst = dst;
  tx.len = len;
  tx.data = &_comm_data[COMM_H_SIZE];
  tx.flags = ack ? COMM_FLAG_REQACK_BIT : 0;
  tx.timestamp = co->app.get_time_f();
  int res = comm_app_tx(co,  &tx);
  return res == R_COMM_OK ? tx.seqno : res;
}

int comm_alert(comm *co, unsigned char type, unsigned short len, unsigned char *data) {
  comm_arg tx;
  if (data != 0 && len > 0) COMM_MEMCPY(&_comm_data[COMM_H_SIZE - COMM_H_SIZE_TRA + COMM_H_SIZE_ALERT], data, len);
  tx.len = len + COMM_H_SIZE_ALERT;
  tx.data = &_comm_data[COMM_H_SIZE - COMM_H_SIZE_TRA + COMM_H_SIZE_ALERT];
  tx.data[0] = co->nwk.addr;
  tx.data[1] = type;
  tx.flags = COMM_STAT_ALERT_BIT;
  tx.timestamp = co->app.get_time_f();
  int res = comm_app_tx(co,  &tx);
  return res;
}


int comm_reply(comm *co, comm_arg* rtx, unsigned short len, unsigned char *data) {
  if ((rtx->flags & COMM_FLAG_REQACK_BIT) == 0) {
    return R_COMM_APP_NOT_AN_ACK;
  }
  if (data != 0 && len > 0) COMM_MEMCPY(rtx->data, data, len);
  rtx->len = len;
  rtx->flags = COMM_STAT_REPLY_BIT | COMM_FLAG_ISACK_BIT;
  comm_addr src = rtx->src;
  rtx->src = rtx->dst;
  rtx->dst = src;
  rtx->timestamp = co->app.get_time_f();
  return comm_app_tx(co,  rtx);
}

int comm_ping(comm *co, comm_addr dst) {
  comm_arg tx;
  tx.dst = dst;
  tx.len = 1;
  tx.data = &_comm_data[COMM_H_SIZE];
  tx.data[0] = COMM_TRA_INF_PING;
  tx.flags = COMM_FLAG_INF_BIT;
  tx.timestamp = co->app.get_time_f();
  return comm_app_tx(co,  &tx);
}

#ifdef COMM_STATS
void comm_clear_stats(comm *co) {
  co->stat.tx = 0;
  co->stat.tx_fail= 0;
}

unsigned char comm_squal(comm *co) {
  unsigned int tx_fail_ratio;
  if (co->stat.tx > 0) {
    tx_fail_ratio = (255*co->stat.tx_fail) / (co->stat.tx * COMM_MAX_RESENDS);
  } else {
    tx_fail_ratio = 128 + co->stat.tx_fail;
  }
  if (tx_fail_ratio > 255) tx_fail_ratio = 255;
  return (unsigned char)(255 - tx_fail_ratio);
}

#endif
