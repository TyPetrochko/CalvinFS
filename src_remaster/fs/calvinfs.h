// Author: Alexander Thomson <thomson@cs.yale.edu>
//

#ifndef CALVIN_FS_CALVINFS_H_
#define CALVIN_FS_CALVINFS_H_

#include <atomic>
#include "common/atomic.h"
#include "fs/fs.h"
#include "fs/calvinfs_config.pb.h"
#include "common/source.h"

string TopDir(const string& path);

class Action;
class Machine;
class MessageBuffer;
class LogApp;
class Scheduler;
class StoreApp;
class BlockStore;

class CalvinFSConfigMap {
 public:
  explicit CalvinFSConfigMap(const CalvinFSConfig& config);

  // Looks up the (serialized) CalvinFSConfig in machine->AppData()
  explicit CalvinFSConfigMap(Machine* machine);

  // Lookup what replica the specified machine belongs to.
  uint64 LookupReplica(uint64 machine_id);

  // Map block id to blucket id.
  uint64 HashBlockID(uint64 block_id);

  // Map filename to mds id.
  uint64 HashFileName(const Slice& filename);

  // Lookup machine containing blucket (id, replica).
  uint64 LookupBlucket(uint64 id, uint64 replica = 0);

  // Lookup machine containing MDS (id, replica).
  uint64 LookupMetadataShard(uint64 id, uint64 replica = 0);

  const map<pair<uint64, uint64>, uint64>& mds() { return metadata_shards_; }

  const CalvinFSConfig& config() { return config_; }

  // Lookup what replica is the master of a given path
  // If not yet mapped, uses deterministic default replica based on dir.
  uint32 LookupReplicaByDir(string dir, Machine *machine);

  // sets the involved_replicas field on the action based on machine's current
  // master map
  void LookupInvolvedReplicas(Action* action);

  // Change what replica is the master of a given path
  // Only changes the local map.
  void ChangeReplicaForPath(string path, uint32 new_master, Machine *machine);

  // This sends intra-replica RPCs and optionally waits for responses
  // RPCs are sent from machine to all other machines in the same replica
  void ChangeReplicaForPath(string path, uint32 new_master, Machine* machine, string app_name, bool wait);

  uint64 GetPartitionsPerReplica();

  uint64 GetReplicas();

 private:
  void Init(const CalvinFSConfig& config);

  CalvinFSConfig config_;

  // machine -> replica
  map<uint64, uint64> replicas_;

  // (id, replica) -> machine
  map<pair<uint64, uint64>, uint64> bluckets_;
  map<pair<uint64, uint64>, uint64> metadata_shards_;

  // path -> replica MOVED TO MACHINE
  // map<string, uint32> masters_;

};

// One machine, one replica, one blucket, one mds.
CalvinFSConfig MakeCalvinFSConfig();

// 'n' machines, one replica, one blucket and one mds per machine.
CalvinFSConfig MakeCalvinFSConfig(int n);

// 'n' partitions, 'r' replicas (thus n*r total machines).
// One blucket and one mds per machine.
CalvinFSConfig MakeCalvinFSConfig(int n, int r);

// LocalCalvinFS interface.
class LocalCalvinFS : public FS {
 public:
  LocalCalvinFS();
  virtual ~LocalCalvinFS();
  virtual Status ReadFileToString(const string& path, string* data);
  virtual Status CreateDirectory(const string& path);
  virtual Status CreateFile(const string& path);
  virtual Status WriteStringToFile(const string& data, const string& path);
  virtual Status AppendStringToFile(const string& data, const string& path);
  virtual Status LS(const string& path, vector<string>* contents);
  virtual Status Remove(const string& path);
  virtual Status Copy(const string& from_path, const string& to_path);

 private:
  // Machine for apps.
  Machine* machine_;

  // Log, scheduler and metadata store.
  LogApp* log_;
  Scheduler* scheduler_;
  StoreApp* metadata_;

  // Block store.
  BlockStore* blocks_;

  Source<Action*>* source_;

  // Action results queue.
  AtomicQueue<MessageBuffer*>* results_;
};

#endif  // CALVIN_FS_CALVINFS_H_

