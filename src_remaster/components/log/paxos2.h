// Author: Alex Thomson (thomson@cs.yale.edu)
// Author: Kun  Ren (kun.ren@yale.edu)
//

#ifndef CALVIN_COMPONENTS_LOG_PAXOS2_H_
#define CALVIN_COMPONENTS_LOG_PAXOS2_H_

#include <set>
#include <vector>
#include <map>

#include "common/atomic.h"
#include "common/mutex.h"
#include "common/types.h"
#include "components/log/log.h"
#include "components/log/log_app.h"
#include "machine/app/app.h"
#include "proto/start_app.pb.h"
#include "proto/scalar.pb.h"
#include "proto/action.pb.h"

using std::vector;
using std::map;
using std::pair;

class Header;
class Machine;
class MessageBuffer;

class Paxos2App : public LogApp {
 public:
  Paxos2App(Log* log, const vector<uint64>& participants);
  Paxos2App(Log* log, uint64 count);
  virtual ~Paxos2App() {
    Stop();
  }
  virtual void Start();
  virtual void Stop();
  void Append(uint64 blockid, uint64 count = 1);
  void GetRemoteSequence(MessageBuffer** result);

 protected:
  virtual void HandleOtherMessages(Header* header, MessageBuffer* message);

  // Returns true iff leader.
  bool IsLeader();

  // Leader's main loop.
  void RunLeader();

  // Followers' main loop.
  void RunFollower();

  // Participant list.
  vector<uint64> participants_;

  // True iff main thread SHOULD run.
  std::atomic<bool> go_;

  // True iff main thread IS running.
  std::atomic<bool> going_;

  // Current request sequence that will get replicated.
  PairSequence sequence_;
  std::atomic<uint64> count_;
  Mutex mutex_;

   std::atomic<uint64> has_local_sequence_;


  uint32 replica_count;
  uint32 partitions_per_replica;

  AtomicQueue<MessageBuffer*> sequences_other_replicas;

  map<uint32, Log::Reader*> readers_for_local_log;

  Log* local_log_;
  
};

#endif  // CALVIN_COMPONENTS_LOG_PAXOS2_H_
