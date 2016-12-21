// Author: Alex Thomson (thomson@cs.yale.edu)
// Author: Kun  Ren (kun.ren@yale.edu)
//

#include "components/scheduler/locking_scheduler.h"

#include <glog/logging.h>
#include <set>
#include <string>
#include "common/source.h"
#include "common/types.h"
#include "common/utils.h"
#include "components/scheduler/lock_manager.h"
#include "components/scheduler/scheduler.h"
#include "components/store/store_app.h"
#include "proto/header.pb.h"
#include "proto/action.pb.h"

using std::string;

REGISTER_APP(LockingScheduler) {
  return new LockingScheduler();
}

void LockingScheduler::MainLoopBody() {
  Action* action;

  // Start processing the next incoming action request.
  if (static_cast<int>(active_actions_.size()) < kMaxActiveActions &&
      running_action_count_ < kMaxRunningActions &&
      action_requests_->Get(&action)) {

    high_water_mark_ = action->version();
    active_actions_.insert(action->version());
    int ungranted_requests = 0;

    // Request write locks. Track requests so we can check that we don't
    // re-request any as read locks.
    set<string> writeset;
    for (int i = 0; i < action->writeset_size(); i++) {
      if (store_->IsLocal(action->writeset(i))) {
        writeset.insert(action->writeset(i));
        if (!lm_.WriteLock(action, action->writeset(i))) {
          ungranted_requests++;
        }
      }
    }

    // Request read locks.
    for (int i = 0; i < action->readset_size(); i++) {
      // Avoid re-requesting shared locks if an exclusive lock is already
      // requested.
      if (store_->IsLocal(action->readset(i))) {
        if (writeset.count(action->readset(i)) == 0) {
          if (!lm_.ReadLock(action, action->readset(i))) {
            ungranted_requests++;
          }
        }
      }
    }
    
    LOG(ERROR) << "Acquired locks for txn "<<action->distinct_id()<<" on machine "<< local_replica_;

    // If all read and write locks were immediately acquired, this action
    // is ready to run.
    if (ungranted_requests == 0) {

      running_action_count_++;
      store_->RunAsync(action, &completed_);
      //LOG(ERROR) << "Machine: "<<machine()->machine_id()<<":------------ Get Lock immediately:" << action->version()<<" distinct id is:"<<action->distinct_id();
    } else {
      //LOG(ERROR) << "Machine: "<<machine()->machine_id()<<":------------ BLOCK:" << action->version()<<" distinct id is:"<<action->distinct_id();
    }
  }

  // Process all actions that have finished running.
  while (completed_.Pop(&action)) {

    set<string> writeset;

    // Release write locks. 
    for (int i = 0; i < action->writeset_size(); i++) {
      if (store_->IsLocal(action->writeset(i))) {
        writeset.insert(action->writeset(i));
        lm_.Release(action, action->writeset(i));
      }
    }

    // Release read locks.
    for (int i = 0; i < action->readset_size(); i++) {
      if (store_->IsLocal(action->readset(i))) {
        if (writeset.count(action->readset(i)) == 0) {
          lm_.Release(action, action->readset(i));
        }
      }
    }
    //LOG(ERROR) << "Machine: "<<machine()->machine_id()<<":** scheduler finish running action:" << action->version()<<" distinct id is:"<<action->distinct_id();
    active_actions_.erase(action->version());
    running_action_count_--;
    safe_version_.store(
        active_actions_.empty()
        ? (high_water_mark_ + 1)
        : *active_actions_.begin());
  }

  // Start executing all actions that have newly acquired all their locks.
  while (lm_.Ready(&action)) {
    //LOG(ERROR) << "Machine: "<<machine()->machine_id()<<":------------ Previous blocked, now active:" << action->version()<<" distinct id is:"<<action->distinct_id();    
    running_action_count_++;
    store_->RunAsync(action, &completed_);
  }
}

