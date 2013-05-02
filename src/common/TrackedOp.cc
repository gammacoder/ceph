// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 

#include "TrackedOp.h"
#include "common/Formatter.h"
#include <iostream>
#include <vector>
#include "common/debug.h"
#include "common/config.h"
#include "msg/Message.h"
#include "include/assert.h"

#define dout_subsys ceph_subsys_optracker
#undef dout_prefix
#define dout_prefix _prefix(_dout)

static ostream& _prefix(std::ostream* _dout)
{
  return *_dout << "--OSD::tracker-- ";
}

void OpHistory::on_shutdown()
{
  arrived.clear();
  duration.clear();
  shutdown = true;
}

void OpHistory::insert(utime_t now, TrackedOpRef op)
{
  if (shutdown)
    return;
  duration.insert(make_pair(op->get_duration(), op));
  arrived.insert(make_pair(op->get_arrived(), op));
  cleanup(now);
}

void OpHistory::cleanup(utime_t now)
{
  while (arrived.size() &&
	 (now - arrived.begin()->first >
	  (double)(g_conf->op_tracker_history_duration))) {
    duration.erase(make_pair(
	arrived.begin()->second->get_duration(),
	arrived.begin()->second));
    arrived.erase(arrived.begin());
  }

  while (duration.size() > g_conf->op_tracker_history_size) {
    arrived.erase(make_pair(
	duration.begin()->second->get_arrived(),
	duration.begin()->second));
    duration.erase(duration.begin());
  }
}

void OpHistory::dump_ops(utime_t now, Formatter *f)
{
  cleanup(now);
  f->open_object_section("OpHistory");
  f->dump_int("num to keep", g_conf->op_tracker_history_size);
  f->dump_int("duration to keep", g_conf->op_tracker_history_duration);
  {
    f->open_array_section("Ops");
    for (set<pair<utime_t, TrackedOpRef> >::const_iterator i =
	   arrived.begin();
	 i != arrived.end();
	 ++i) {
      f->open_object_section("Op");
      i->second->dump(now, f);
      f->close_section();
    }
    f->close_section();
  }
  f->close_section();
}

void OpTracker::dump_historic_ops(ostream &ss)
{
  JSONFormatter jf(true);
  Mutex::Locker locker(ops_in_flight_lock);
  utime_t now = ceph_clock_now(g_ceph_context);
  history.dump_ops(now, &jf);
  jf.flush(ss);
}

void OpTracker::dump_ops_in_flight(ostream &ss)
{
  JSONFormatter jf(true);
  Mutex::Locker locker(ops_in_flight_lock);
  jf.open_object_section("ops_in_flight"); // overall dump
  jf.dump_int("num_ops", ops_in_flight.size());
  jf.open_array_section("ops"); // list of TrackedOps
  utime_t now = ceph_clock_now(g_ceph_context);
  for (xlist<TrackedOp*>::iterator p = ops_in_flight.begin(); !p.end(); ++p) {
    jf.open_object_section("op");
    (*p)->dump(now, &jf);
    jf.close_section(); // this TrackedOp
  }
  jf.close_section(); // list of TrackedOps
  jf.close_section(); // overall dump
  jf.flush(ss);
}

void OpTracker::register_inflight_op(xlist<TrackedOp*>::item *i)
{
  Mutex::Locker locker(ops_in_flight_lock);
  ops_in_flight.push_back(i);
  ops_in_flight.back()->seq = seq++;
}

void OpTracker::unregister_inflight_op(TrackedOp *i)
{
  Mutex::Locker locker(ops_in_flight_lock);
  assert(i->xitem.get_list() == &ops_in_flight);
  utime_t now = ceph_clock_now(g_ceph_context);
  i->xitem.remove_myself();
  i->request->clear_data();
  history.insert(now, TrackedOpRef(i));
}

bool OpTracker::check_ops_in_flight(std::vector<string> &warning_vector)
{
  Mutex::Locker locker(ops_in_flight_lock);
  if (!ops_in_flight.size())
    return false;

  utime_t now = ceph_clock_now(g_ceph_context);
  utime_t too_old = now;
  too_old -= g_conf->op_tracker_complaint_time;

  utime_t oldest_secs = now - ops_in_flight.front()->received_time;

  dout(10) << "ops_in_flight.size: " << ops_in_flight.size()
           << "; oldest is " << oldest_secs
           << " seconds old" << dendl;

  if (oldest_secs < g_conf->op_tracker_complaint_time)
    return false;

  xlist<TrackedOp*>::iterator i = ops_in_flight.begin();
  warning_vector.reserve(g_conf->op_tracker_log_threshold + 1);

  int slow = 0;     // total slow
  int warned = 0;   // total logged
  while (!i.end() && (*i)->received_time < too_old) {
    slow++;

    // exponential backoff of warning intervals
    if (((*i)->received_time +
	 (g_conf->op_tracker_complaint_time *
	  (*i)->warn_interval_multiplier)) < now) {
      // will warn
      if (warning_vector.empty())
	warning_vector.push_back("");
      warned++;
      if (warned > g_conf->op_tracker_log_threshold)
        break;

      utime_t age = now - (*i)->received_time;
      stringstream ss;
      ss << "slow request " << age << " seconds old, received at " << (*i)->received_time
	 << ": " << *((*i)->request) << " currently "
	 << ((*i)->current.size() ? (*i)->current : (*i)->state_string());
      warning_vector.push_back(ss.str());

      // only those that have been shown will backoff
      (*i)->warn_interval_multiplier *= 2;
    }
    ++i;
  }

  // only summarize if we warn about any.  if everything has backed
  // off, we will stay silent.
  if (warned > 0) {
    stringstream ss;
    ss << slow << " slow requests, " << warned << " included below; oldest blocked for > "
       << oldest_secs << " secs";
    warning_vector[0] = ss.str();
  }

  return warning_vector.size();
}

void OpTracker::mark_event(TrackedOp *op, const string &dest)
{
  utime_t now = ceph_clock_now(g_ceph_context);
  return _mark_event(op, dest, now);
}

void OpTracker::_mark_event(TrackedOp *op, const string &evt,
			    utime_t time)
{
  Mutex::Locker locker(ops_in_flight_lock);
  dout(5) << //"reqid: " << op->get_reqid() <<
	     ", seq: " << op->seq
	  << ", time: " << time << ", event: " << evt
	  << ", request: " << *op->request << dendl;
}

void OpTracker::RemoveOnDelete::operator()(TrackedOp *op) {
  op->mark_event("done");
  tracker->unregister_inflight_op(op);
  // Do not delete op, unregister_inflight_op took control
}

void TrackedOp::mark_event(const string &event)
{
  utime_t now = ceph_clock_now(g_ceph_context);
  {
    Mutex::Locker l(lock);
    events.push_back(make_pair(now, event));
  }
  tracker->mark_event(this, event);
}
