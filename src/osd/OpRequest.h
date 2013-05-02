// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2012 New Dream Network/Sage Weil <sage@newdream.net>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 */

#ifndef OPREQUEST_H_
#define OPREQUEST_H_
#include <sstream>
#include <stdint.h>
#include <vector>

#include <include/utime.h>
#include "msg/Message.h"
#include <tr1/memory>
#include "common/TrackedOp.h"
#include "osd/osd_types.h"


/**
 * The OpRequest takes in a Message* and takes over a single reference
 * to it, which it puts() when destroyed.
 * OpRequest is itself ref-counted. The expectation is that you get a Message
 * you want to track, create an OpRequest with it, and then pass around that OpRequest
 * the way you used to pass around the Message.
 */
struct OpRequest : public TrackedOp {
  friend class OpTracker;
  // rmw flags
  int rmw_flags;

  bool check_rmw(int flag) {
    return rmw_flags & flag;
  }
  bool may_read() { return need_read_cap() || need_class_read_cap(); }
  bool may_write() { return need_write_cap() || need_class_write_cap(); }
  bool includes_pg_op() { return check_rmw(CEPH_OSD_RMW_FLAG_PGOP); }
  bool need_read_cap() {
    return check_rmw(CEPH_OSD_RMW_FLAG_READ);
  }
  bool need_write_cap() {
    return check_rmw(CEPH_OSD_RMW_FLAG_WRITE);
  }
  bool need_class_read_cap() {
    return check_rmw(CEPH_OSD_RMW_FLAG_CLASS_READ);
  }
  bool need_class_write_cap() {
    return check_rmw(CEPH_OSD_RMW_FLAG_CLASS_WRITE);
  }
  void set_read() { rmw_flags |= CEPH_OSD_RMW_FLAG_READ; }
  void set_write() { rmw_flags |= CEPH_OSD_RMW_FLAG_WRITE; }
  void set_class_read() { rmw_flags |= CEPH_OSD_RMW_FLAG_CLASS_READ; }
  void set_class_write() { rmw_flags |= CEPH_OSD_RMW_FLAG_CLASS_WRITE; }
  void set_pg_op() { rmw_flags |= CEPH_OSD_RMW_FLAG_PGOP; }

  void dump(utime_t now, Formatter *f) const;

private:
  osd_reqid_t reqid;
  uint8_t hit_flag_points;
  uint8_t latest_flag_point;
  static const uint8_t flag_queued_for_pg=1 << 0;
  static const uint8_t flag_reached_pg =  1 << 1;
  static const uint8_t flag_delayed =     1 << 2;
  static const uint8_t flag_started =     1 << 3;
  static const uint8_t flag_sub_op_sent = 1 << 4;
  static const uint8_t flag_commit_sent = 1 << 5;

  OpRequest(Message *req, OpTracker *tracker) :
    TrackedOp(req, tracker),
    rmw_flags(0),
    hit_flag_points(0), latest_flag_point(0) {}
public:
  ~OpRequest() {
    assert(request);
    request->put();
  }

  bool been_queued_for_pg() { return hit_flag_points & flag_queued_for_pg; }
  bool been_reached_pg() { return hit_flag_points & flag_reached_pg; }
  bool been_delayed() { return hit_flag_points & flag_delayed; }
  bool been_started() { return hit_flag_points & flag_started; }
  bool been_sub_op_sent() { return hit_flag_points & flag_sub_op_sent; }
  bool been_commit_sent() { return hit_flag_points & flag_commit_sent; }
  bool currently_queued_for_pg() { return latest_flag_point & flag_queued_for_pg; }
  bool currently_reached_pg() { return latest_flag_point & flag_reached_pg; }
  bool currently_delayed() { return latest_flag_point & flag_delayed; }
  bool currently_started() { return latest_flag_point & flag_started; }
  bool currently_sub_op_sent() { return latest_flag_point & flag_sub_op_sent; }
  bool currently_commit_sent() { return latest_flag_point & flag_commit_sent; }

  const char *state_string() const {
    switch(latest_flag_point) {
    case flag_queued_for_pg: return "queued for pg";
    case flag_reached_pg: return "reached pg";
    case flag_delayed: return "delayed";
    case flag_started: return "started";
    case flag_sub_op_sent: return "waiting for sub ops";
    case flag_commit_sent: return "commit sent; apply or cleanup";
    default: break;
    }
    return "no flag points reached";
  }

  void mark_queued_for_pg() {
    mark_event("queued_for_pg");
    current = "queued for pg";
    hit_flag_points |= flag_queued_for_pg;
    latest_flag_point = flag_queued_for_pg;
  }
  void mark_reached_pg() {
    mark_event("reached_pg");
    current = "reached pg";
    hit_flag_points |= flag_reached_pg;
    latest_flag_point = flag_reached_pg;
  }
  void mark_delayed(string s) {
    mark_event(s);
    current = s;
    hit_flag_points |= flag_delayed;
    latest_flag_point = flag_delayed;
  }
  void mark_started() {
    mark_event("started");
    current = "started";
    hit_flag_points |= flag_started;
    latest_flag_point = flag_started;
  }
  void mark_sub_op_sent(string s) {
    mark_event(s);
    current = s;
    hit_flag_points |= flag_sub_op_sent;
    latest_flag_point = flag_sub_op_sent;
  }
  void mark_commit_sent() {
    mark_event("commit_sent");
    current = "commit sent";
    hit_flag_points |= flag_commit_sent;
    latest_flag_point = flag_commit_sent;
  }

  void mark_event(const string &event);
  osd_reqid_t get_reqid() const {
    return reqid;
  }

  void init_from_message();
};

typedef std::tr1::shared_ptr<OpRequest> OpRequestRef;


#endif /* OPREQUEST_H_ */
