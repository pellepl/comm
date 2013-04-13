/*
 * comm_tra.c
 *
 *  Created on: Jun 8, 2012
 *      Author: petera
 */

#include "comm.h"

#define COMM_TRA_SEQNO_MASK     ((unsigned short)0xfff0)
#define COMM_TRA_FREE_ACK_SLOT  ((unsigned short)COMM_NO_SEQNO)

#define COMM_TRA_INDEX_TO_USER(comm, ix) ((ix)+1 < (comm)->nwk.addr ? ((ix)+1) : ((ix)+2))
#define COMM_TRA_USER_TO_INDEX(comm, u)  ((u) < (comm)->nwk.addr ? ((u)-1) : ((u)-2))

static int comm_tra_tx_seqno(comm *comm, comm_arg* tx, unsigned short seqno);

static void comm_tra_tx_resend(comm *comm, comm_time time) {

  // check txed packets wanting acks and resend on timeout
  int i;
  for (i = 0; i < COMM_MAX_PENDING; i++) {
    if (comm->tra.acks_tx_pend[i].busy &&
        (time - comm->tra.acks_tx_pend[i].timestamp) >= COMM_RESEND_TICK) {
      if (comm->tra.acks_tx_pend[i].resends >= COMM_MAX_RESENDS) {
        // reached limit, waste packet
        COMM_TRA_DBG("pkt wasted %03x, tries %i", comm->tra.acks_tx_pend[i].arg.seqno, comm->tra.acks_tx_pend[i].resends);
        comm->tra.acks_tx_pend[i].busy = 0;
        comm->app.user_err_f(comm, R_COMM_TRA_NO_ACK, comm->tra.acks_tx_pend[i].arg.seqno,
            comm->tra.acks_tx_pend[i].arg.len, comm->tra.acks_tx_pend[i].arg.data);
        continue;
      } else {
        // resend
        comm->tra.acks_tx_pend[i].resends++;
        comm->tra.acks_tx_pend[i].timestamp = time;
        COMM_TRA_DBG("resending %03x try %i", comm->tra.acks_tx_pend[i].arg.seqno, comm->tra.acks_tx_pend[i].resends);

        unsigned short t_len = comm->tra.acks_tx_pend[i].arg.len;
        unsigned char *t_data = comm->tra.acks_tx_pend[i].arg.data;
        int res = comm->tra.down_tx_f(comm, &comm->tra.acks_tx_pend[i].arg);
        // reset length and data ptr
        comm->tra.acks_tx_pend[i].arg.len = t_len;
        comm->tra.acks_tx_pend[i].arg.data = t_data;

        switch (res) {
        case R_COMM_OK:
          continue;
        case R_COMM_PHY_FAIL:
          // major error, report and bail out
          comm->app.user_err_f(comm, res, comm->tra.acks_tx_pend[i].arg.seqno,
              comm->tra.acks_tx_pend[i].arg.len, comm->tra.acks_tx_pend[i].arg.data);
          return;
        default:
          // report and mark as free, app must take care of this
          comm->tra.acks_tx_pend[i].busy = 0;
          comm->app.user_err_f(comm, res, comm->tra.acks_tx_pend[i].arg.seqno,
              comm->tra.acks_tx_pend[i].arg.len, comm->tra.acks_tx_pend[i].arg.data);
          break;
        }
      }
    } // busy overdue slot
  } // all slots
}

static void comm_tra_tx_pending_acks(comm *comm) {

  int u, ix, acks = 0;
  for (u = 0; u < (COMM_TRA_MAX_USERS); u++) {
    for (ix = 0; ix < COMM_MAX_PENDING; ix++) {
      if (comm->tra.acks_rx_pend[u][ix] != COMM_TRA_FREE_ACK_SLOT) {
        comm_arg tx_ack;
        unsigned char ack_data[COMM_H_SIZE + 1];
        tx_ack.src = comm->nwk.addr;
        tx_ack.dst = COMM_TRA_INDEX_TO_USER(comm, u);
        tx_ack.flags = COMM_FLAG_ISACK_BIT;
        tx_ack.data = &ack_data[COMM_H_SIZE];
        tx_ack.len = 0;
        int res = comm_tra_tx_seqno(comm, &tx_ack, comm->tra.acks_rx_pend[u][ix]);
        COMM_TRA_DBG("tx ack %03x to %i[%i], res %i", comm->tra.acks_rx_pend[u][ix],tx_ack.dst, u,res);
        if (res == R_COMM_OK) {
          comm->tra.acks_rx_pend[u][ix] = COMM_TRA_FREE_ACK_SLOT;
          acks++;
          if (COMM_ACK_THROTTLE != 0 && acks > COMM_ACK_THROTTLE) {
            return;
          }
        } else {
          // error
          COMM_TRA_DBG("tx ack %03x to %i[%i] fail, err %i", comm->tra.acks_rx_pend[u][ix], tx_ack.dst, u, res);
          switch (res) {
          case R_COMM_PHY_FAIL:
            // major error, report and bail out
            comm->app.user_err_f(comm, res, COMM_TRA_FREE_ACK_SLOT, 0, 0);
            return;
          case R_COMM_PHY_TMO:
            // just mark this ack as free and continue silently, rely on resend
            comm->tra.acks_rx_pend[u][ix] = COMM_TRA_FREE_ACK_SLOT;
            break;
          case R_COMM_NWK_BAD_ADDR:
          default:
            // report and mark as free, rely on resend
            comm->app.user_err_f(comm, res, COMM_TRA_FREE_ACK_SLOT, 0, 0);
            comm->tra.acks_rx_pend[u][ix] = COMM_TRA_FREE_ACK_SLOT;
            break;
          }
        }
      } // used ack entry
    } // per ack entry
  } // per user
}

void comm_tra_tick(comm *comm, comm_time time) {
#if COMM_ACK_DIRECTLY == 0
  comm_tra_tx_pending_acks(comm);
#endif
  comm_tra_tx_resend(comm, time);
}

static int comm_tra_register_tx_tobeacked(comm *comm, comm_arg* tx) {
  int i;
  for (i = 0; i < COMM_MAX_PENDING; i++) {
    if (!comm->tra.acks_tx_pend[i].busy) {
      break;
    }
  }
  if (i == COMM_MAX_PENDING) {
    return R_COMM_TRA_PEND_Q_FULL;
  }
  comm->tra.acks_tx_pend[i].busy = 1;
  comm->tra.acks_tx_pend[i].timestamp = comm->app.get_time_f();
  comm->tra.acks_tx_pend[i].resends = 0;
  COMM_MEMCPY(&comm->tra.acks_tx_pend[i].arg, tx, sizeof(comm_arg));
  COMM_MEMCPY(
      &comm->tra.acks_tx_pend[i].data[COMM_H_SIZE - COMM_H_SIZE_TRA],
      tx->data, tx->len);
  comm->tra.acks_tx_pend[i].arg.data =
      &comm->tra.acks_tx_pend[i].data[COMM_H_SIZE - COMM_H_SIZE_TRA];
  unsigned short xtra_h = COMM_FLAG_RESENT_BIT;
  comm->tra.acks_tx_pend[i].arg.data[0] |= (xtra_h & 0xff00) >> 8;
  comm->tra.acks_tx_pend[i].arg.data[1] |= (xtra_h & 0x00ff);

  return R_COMM_OK;
}

static int comm_tra_got_rx_ack(comm *comm, comm_arg *rx) {

  int i;
  for (i = 0; i < COMM_MAX_PENDING; i++) {
    if (comm->tra.acks_tx_pend[i].busy &&
        comm->tra.acks_tx_pend[i].arg.seqno == rx->seqno &&
        (comm->tra.acks_tx_pend[i].arg.dst == rx->src || !COMM_USER_DIFFERENTIATION)) {
      comm->tra.acks_tx_pend[i].busy = 0;
      return R_COMM_OK;
    }
  }
  // got an ack for something not wanting ack
  rx->flags |= COMM_STAT_ACK_MISS_BIT;
  COMM_TRA_DBG("rx ack: late/invalid, seqno %03x from %i", rx->seqno, rx->src);
  return R_COMM_OK;
}

static int comm_tra_register_rx_reqack_pending(comm *comm, comm_arg* rx, unsigned short **ack_entry) {

  int i;
  int cand = COMM_MAX_PENDING;
  unsigned char u = COMM_USER_DIFFERENTIATION ? COMM_TRA_USER_TO_INDEX(comm, rx->src) : 0;
  for (i = 0; i < COMM_MAX_PENDING; i++) {
    if (comm->tra.acks_rx_pend[u][i] == rx->seqno) {
      // already in ack queue, fill same
      cand = i;
      rx->flags |= COMM_STAT_RESEND_BIT;
      break;
    } else if (comm->tra.acks_rx_pend[u][i] == COMM_TRA_FREE_ACK_SLOT && cand == COMM_MAX_PENDING) {
      // new ack requested
      cand = i;
    }
  }
  if (cand == COMM_MAX_PENDING) {
    return R_COMM_TRA_ACK_Q_FULL;
  }

  comm->tra.acks_rx_pend[u][cand] = rx->seqno;
  *ack_entry = &(comm->tra.acks_rx_pend[u][cand]);

  return R_COMM_OK;
}

static int comm_tra_send_inf(comm *comm, int dst, unsigned char inf) {
  comm_arg tx_inf;
  unsigned char inf_data[COMM_H_SIZE + 2];
  tx_inf.src = comm->nwk.addr;
  tx_inf.dst = dst;
  tx_inf.flags = COMM_FLAG_INF_BIT;
  tx_inf.data = &inf_data[COMM_H_SIZE];
  tx_inf.data[0] = inf;
  tx_inf.len = 1;
  int res = comm_tra_tx(comm, &tx_inf);
  return res;
}

static int comm_tra_handle_remote_inf(comm *comm, comm_arg* rx) {
  // todo
  if (rx->len > 0) {
    unsigned char code = *rx->data;
    switch (code) {
    case COMM_TRA_INF_PING:
      return comm_tra_send_inf(comm, rx->src, COMM_TRA_INF_PONG);
    case COMM_TRA_INF_PONG:
    case COMM_TRA_INF_CONGESTION:
      if (comm->app.inf_f) {
        comm->app.inf_f(comm, rx);
      }
      break;
    }
  }
  return R_COMM_OK;
}

static int comm_tra_handle_rx(comm *comm, comm_arg* rx) {

  int res = R_COMM_OK;
  COMM_TRA_DBG("rx pkt len %i, flags %02x, seq %03x", rx->len, rx->flags, rx->seqno);
  unsigned short *ack_entry = 0;
  if (rx->flags & COMM_FLAG_INF_BIT) {
    // remote transport info packet
    COMM_TRA_DBG("info pkt");
    res = comm_tra_handle_remote_inf(comm, rx);
  } else if (rx->flags & COMM_FLAG_REQACK_BIT) {
    // this packet wants to be acked
    COMM_TRA_DBG("rx pkt wants ack");
    res = comm_tra_register_rx_reqack_pending(comm, rx, &ack_entry);
    if (res == R_COMM_TRA_ACK_Q_FULL) {
      (void)comm_tra_send_inf(comm, rx->src, COMM_TRA_INF_CONGESTION);
    }
  } else if (rx->flags & COMM_FLAG_ISACK_BIT) {
    // this is an ack
    COMM_TRA_DBG("rx pkt is ack");
    res = comm_tra_got_rx_ack(comm, rx);
    if (res == R_COMM_OK) {
      if ((rx->flags & COMM_STAT_ACK_MISS_BIT) == 0) {
        comm->app.ack_f(comm, rx);
      }
    }
  }
  if (res == R_COMM_OK && rx->len > 0) {
    // only send up to app if all ok and len is ok
    if ((rx->flags & (COMM_FLAG_ISACK_BIT | COMM_FLAG_INF_BIT)) == 0) {
      // acks already reported
      res = comm->tra.up_rx_f(comm, rx);
      // now, see if app wants to/has sent ack itself
      if (res == R_COMM_OK &&
          (rx->flags & COMM_STAT_REPLY_BIT) &&
          (rx->flags & COMM_FLAG_ISACK_BIT)) {
        COMM_TRA_DBG("pkt is acked by APP");
        *ack_entry = COMM_TRA_FREE_ACK_SLOT;
      }
#if COMM_ACK_DIRECTLY
      if (res == R_COMM_OK &&
          (rx->flags & COMM_STAT_REPLY_BIT) == 0 &&
          (rx->flags & COMM_FLAG_REQACK_BIT)) {
        comm_tra_tx_pending_acks(comm);
      }
#endif
    }
  }
  if (res < R_COMM_OK) {
    COMM_TRA_DBG("rx err %i", res);
  }
  return res;
}

int comm_tra_rx(comm *comm, comm_arg* rx) {
  unsigned short tra_h = ((*rx->data)<<8) | (*(rx->data+1));
  rx->data += COMM_H_SIZE_TRA;
  rx->len -= COMM_H_SIZE_TRA;
  unsigned char flags = tra_h & ~(COMM_TRA_SEQNO_MASK);
  unsigned short seqno = (tra_h & COMM_TRA_SEQNO_MASK) >> 4;
  rx->flags = flags;
  rx->seqno = seqno;
  return comm_tra_handle_rx(comm, rx);
}

static int comm_tra_tx_seqno(comm *comm, comm_arg* tx, unsigned short seqno) {
  tx->seqno = seqno;
  unsigned short tra_h = (seqno << 4) | ((tx->flags) &  ~(COMM_TRA_SEQNO_MASK));
  tx->data -= COMM_H_SIZE_TRA;
  tx->len += COMM_H_SIZE_TRA;
  tx->data[0] = (tra_h & 0xff00) >> 8;
  tx->data[1] = (tra_h & 0x00ff);
  if (tx->flags & COMM_FLAG_REQACK_BIT) {
    // this is to be acked, save for resend
    int res = comm_tra_register_tx_tobeacked(comm, tx);
    if (res != R_COMM_OK) {
      return res;
    }
  }
  return comm->tra.down_tx_f(comm, tx);
}

int comm_tra_tx(comm *comm, comm_arg* tx) {
  unsigned short seqno;
  if (COMM_USER_DIFFERENTIATION && tx->dst == COMM_NWK_BRDCAST && (tx->flags & COMM_FLAG_REQACK_BIT)) {
    return R_COMM_TRA_CANNOT_ACK_BROADCAST;
  }
  if ((tx->flags & COMM_STAT_REPLY_BIT) == 0) {
    // plain send, take nbr from sequence index for this dst and increase
    unsigned char u = COMM_USER_DIFFERENTIATION ? COMM_TRA_USER_TO_INDEX(comm, tx->dst) : 0;
    seqno = (comm->tra.seqno[u]++) & (COMM_TRA_SEQNO_MASK >> 4);
  } else {
    // this is a reply, use same seqno as rx
    seqno = tx->seqno;
  }
  return comm_tra_tx_seqno(comm, tx, seqno);
}

void comm_tra_init(comm *comm, comm_rx_fn up_rx_f, comm_tx_fn down_tx_f) {
  int i,j;
  for (i = 0; i < COMM_TRA_MAX_USERS; i++) {
    for (j = 0; j < COMM_MAX_PENDING; j++) {
      comm->tra.acks_rx_pend[i][j] = COMM_TRA_FREE_ACK_SLOT;
    }
  }
  comm->tra.up_rx_f = up_rx_f;
  comm->tra.down_tx_f = down_tx_f;
}
