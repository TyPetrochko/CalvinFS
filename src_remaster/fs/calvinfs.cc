// Author: Alexander Thomson <thomson@cs.yale.edu>
//         Kun  Ren <kun.ren@yale.edu>
//

#include "fs/calvinfs.h"

#include <map>
#include <set>
#include <stack>
#include "common/utils.h"
#include "components/log/log_app.h"
#include "components/scheduler/scheduler.h"
#include "components/scheduler/serial_scheduler.h"
#include "components/store/btreestore.h"
#include "components/store/kvstore.h"
#include "components/store/versioned_kvstore.h"
#include "fs/block_store.h"
#include "fs/calvinfs_config.pb.h"
#include "fs/metadata_store.h"
#include "machine/cluster_config.h"
#include "machine/machine.h"

using std::set;
using std::map;
using std::stack;
using std::pair;
using std::make_pair;

string TopDir(const string& path) {
  // Root dir is a special case.
  if (path.empty()) {
    return path;
  }
  
  std::size_t offset = string(path, 1).find('/');
  if (offset == string::npos) {
    return path;
  } else {
    return string(path, 0, offset+1);
  }
}

CalvinFSConfigMap::CalvinFSConfigMap(const CalvinFSConfig& config) {
  Init(config);
}

CalvinFSConfigMap::CalvinFSConfigMap(Machine* machine) {
  string s;
  CHECK(machine->AppData()->Lookup("calvinfs-config", &s)) << "no config found";
  CalvinFSConfig config;
  config.ParseFromString(s);
  Init(config);
}

uint64 CalvinFSConfigMap::LookupReplica(uint64 machine_id) {
  CHECK(replicas_.count(machine_id) != 0) << "unknown machine: " << machine_id;
  return replicas_[machine_id];
}

uint64 CalvinFSConfigMap::HashBlockID(uint64 block_id) {
  return FNVHash(UInt64ToString(33 * block_id)) % config_.blucket_count();
}

uint64 CalvinFSConfigMap::HashFileName(const Slice& filename) {
  return FNVHash(filename) % config_.metadata_shard_count();
}

uint64 CalvinFSConfigMap::LookupBlucket(uint64 id, uint64 replica) {
  auto it = bluckets_.find(make_pair(id, replica));
  if (it == bluckets_.end()) {
    LOG(FATAL) << "nonexistant blucket (" << id << ", " << replica << ")";
  }
  return it->second;
}

uint64 CalvinFSConfigMap::LookupMetadataShard(uint64 id, uint64 replica) {
  auto it = metadata_shards_.find(make_pair(id, replica));
  if (it == metadata_shards_.end()) {
    LOG(FATAL) << "nonexistant md shard (" << id << ", " << replica << ")";
  }
  return it->second;
}

uint64 CalvinFSConfigMap::GetPartitionsPerReplica () {
  return config_.metadata_shard_count();
}

uint64 CalvinFSConfigMap::GetReplicas () {
  return config_.metadata_replication_factor();
}

void CalvinFSConfigMap::Init(const CalvinFSConfig& config) {
  config_.CopyFrom(config);

  for (int i = 0; i < config.replicas_size(); i++) {
    replicas_[config.replicas(i).machine()] = config.replicas(i).replica();
  }

  for (int i = 0; i < config.bluckets_size(); i++) {
    bluckets_[make_pair(config.bluckets(i).id(),
                        config.bluckets(i).replica())] =
        config.bluckets(i).machine();
  }

  for (int i = 0; i < config.metadata_shards_size(); i++) {
    metadata_shards_[make_pair(config.metadata_shards(i).id(),
                               config.metadata_shards(i).replica())] =
        config.metadata_shards(i).machine();
  }
}

uint32 CalvinFSConfigMap::LookupReplicaByDir(string dir, Machine *machine) {
  uint32 replica;
  if (machine->Masters()->Lookup(dir, &replica)) {
    LOG(ERROR) << "Looking up " << dir << " --> " << replica << " from replica "<<machine->machine_id();
    // it already exists in the master map
    return (uint32) replica;
  } else {
    // use default value for master, found by parsing the top directory
    string top_dir = TopDir(dir);
    // Root dir is a special case. Currently root belongs to replica 0
    if (top_dir.empty()) {
      return 0;
    }

    string num_string = string(top_dir, 2);
    uint32 num = StringToInt(num_string);

    uint32 master = num / config_.metadata_shard_count();
    // no need to save this default value to the master-map,
    // as every node will come up with the same value.
    return master;
  }
}

void CalvinFSConfigMap::LookupInvolvedReplicas(Action* action, Machine* machine, map<string, uint32>* master_map) {
  action->clear_involved_replicas();
  set<uint32> replica_involved;

  // LookupReplicaByDir may be slow, so keep a map for every path in read and write sets

  for (int i = 0; i < action->writeset_size(); i++) {
    uint32 replica = LookupReplicaByDir(action->writeset(i), machine);
    replica_involved.insert(replica);
    (*master_map)[action->writeset(i)] = replica;
  }

  for (int i = 0; i < action->readset_size(); i++) {
    uint32 replica = LookupReplicaByDir(action->readset(i), machine);
    replica_involved.insert(replica);
    (*master_map)[action->readset(i)] = replica;
  }

  CHECK(replica_involved.size() >= 1);
  
  for (set<uint32>::iterator it=replica_involved.begin(); it!=replica_involved.end(); ++it) {
    action->add_involved_replicas(*it);
  }
}

// Change what replica is the master of a given path
// Only changes the local map.
void CalvinFSConfigMap::ChangeReplicaForPath(string path, uint32 new_master, Machine *machine) {
  LOG(ERROR) << "Path " << path << " now maps to " << new_master;
  machine->Masters()->Put(path, new_master);
}

// This sends intra-replica RPCs and optionally waits for responses
// RPCs are sent from machine_id to all other machines in the same replica
void CalvinFSConfigMap::SendIntrareplicaRemasterRequests(MetadataAction::RemasterInput in, Machine* machine, bool wait) {
  // don't change local map. that happens when the remaster txn is executed.

  atomic<int> ack_counter;
  ack_counter = 0;
  int to_expect = 0;

  // send this to every other machine on this replica
  for (uint32 machine_index = 0; machine_index < GetPartitionsPerReplica(); machine_index++) {
    uint32 to_machine = GetPartitionsPerReplica() * LookupReplica(machine->machine_id()) + machine_index;
    if (to_machine != machine->machine_id()) {
      uint64 distinct_id = machine->GetGUID();
      string channel_name = "action-result-" + UInt64ToString(distinct_id);

      Action* a = new Action();
      a->set_client_machine(machine->machine_id());
      a->set_client_channel(channel_name);
      a->set_action_type(wait ? MetadataAction::REMASTER_SYNC : MetadataAction::REMASTER_ASYNC);
      a->set_remaster(true);
      a->set_distinct_id(distinct_id);
      in.set_dest(to_machine);

      in.SerializeToString(a->mutable_input());
      a->add_readset(in.path());
      a->add_writeset(in.path());

      Header* header = new Header();
      header->set_from(machine->machine_id());
      header->set_to(to_machine);
      header->set_type(Header::RPC);
      header->set_app("blocklog");
      header->set_rpc("APPEND");
      if (wait) {
        to_expect++;
        header->set_ack_counter(reinterpret_cast<uint64>(&ack_counter));
      }
      string* block = new string();
      a->SerializeToString(block);
      machine->SendMessage(header, new MessageBuffer(Slice(*block)));
    }
  }

  // now that all RPCs have been sent, wait for responses
  if (to_expect > 0) {
    LOG(ERROR) << "Begin waiting for synchronous responses";
    while (ack_counter.load() < to_expect) {
      usleep(10);
    }
    LOG(ERROR) << "Done waiting for synchronous responses";
  }
}

void CalvinFSConfigMap::SendRemasterFollows(MetadataAction::RemasterInput in, Machine* machine) {
  // send this to a random machine on each replica
  // forward remaster request to new master, and all other masters
  for (uint32 replica = 0; replica < GetReplicas(); replica++) {
    if (replica != LookupReplica(machine->machine_id())) {
      // forward to everyone else
      uint32 dest = replica * GetPartitionsPerReplica() + (rand() % GetPartitionsPerReplica());
      LOG(ERROR) << "Sent REMASTER_FOLLOW to node " << dest << " for path "<<in.path() << " from node "<<machine->machine_id()
      << " replica count is "<<GetReplicas() << " partition count is "<<GetPartitionsPerReplica();
      SendRemasterRequest(machine, dest, in.path(), in.old_master(), in.new_master(), 1);
    }
  }
}

/*
 * 0 for REMASTER (send to one node on old master)
 * 1 for REMASTER_FOLLOW
 * 2 for REMASTER_SYNC
 * 3 for REMASTER_ASYNC
 */
void CalvinFSConfigMap::SendRemasterRequest(Machine* machine, uint32 to_machine, string path, uint32 old_master, uint32 new_master, int type) {
  // sends remaster request as a transaction, even though it's a really weird
  // kind of transaction.
  LOG(ERROR) << "Sending remaster request to "<< IntToString(to_machine);

  uint64 distinct_id = machine->GetGUID();
  string channel_name = "action-result-" + UInt64ToString(distinct_id);

  Action* a = new Action();
  a->set_client_machine(machine->machine_id());
  a->set_client_channel(channel_name);
  switch (type) {
    case 0:
      a->set_action_type(MetadataAction::REMASTER);
      break;
    case 1:
      a->set_action_type(MetadataAction::REMASTER_FOLLOW);
      break;
    case 2:
      a->set_action_type(MetadataAction::REMASTER_SYNC);
      break;
    case 3:
      a->set_action_type(MetadataAction::REMASTER_ASYNC);
      break;
    default:
      LOG(FATAL) << "Bad remaster type in SendRemasterRequest";
  }
  a->set_remaster(true);
  a->set_distinct_id(distinct_id);

  MetadataAction::RemasterInput in;
  in.set_path(path.data(), path.size());
  in.set_old_master(old_master);
  in.set_new_master(new_master);
  in.set_dest(to_machine);
  in.SerializeToString(a->mutable_input());
  a->add_readset(path);
  a->add_writeset(path);
  // don't bother with read and write sets, with any luck this will never be
  // executed in the regular way.

  // send this to a random machine on the old master replica
  Header* header = new Header();
  header->set_from(machine->machine_id());
  header->set_to(to_machine);
  header->set_type(Header::RPC);
  header->set_app("blocklog");
  header->set_rpc("APPEND");
  string* block = new string();
  a->SerializeToString(block);
  machine->SendMessage(header, new MessageBuffer(Slice(*block)));
  // completely asynchronous: do not wait for response
}

////////////////////////////////////////////////////////////////////////////////

CalvinFSConfig MakeCalvinFSConfig() {
  CalvinFSConfig c;
  c.set_block_replication_factor(1);
  c.set_metadata_replication_factor(1);
  c.set_blucket_count(1);
  c.set_metadata_shard_count(1);

  c.add_replicas()->set_machine(0);
  c.mutable_replicas(0)->set_replica(0);
  c.add_bluckets()->set_id(0);
  c.add_metadata_shards()->set_id(0);
  return c;
}

CalvinFSConfig MakeCalvinFSConfig(int n) {
  CalvinFSConfig c;
  c.set_block_replication_factor(1);
  c.set_metadata_replication_factor(1);
  c.set_blucket_count(n);
  c.set_metadata_shard_count(n);

  for (int i = 0; i < n; i++) {
    c.add_replicas()->set_machine(i);
    c.mutable_replicas(i)->set_replica(0);
    c.add_bluckets()->set_id(i);
    c.mutable_bluckets(i)->set_machine(i);
    c.add_metadata_shards()->set_id(i);
    c.mutable_metadata_shards(i)->set_machine(i);
  }
  return c;
}

CalvinFSConfig MakeCalvinFSConfig(int n, int r) {
  CalvinFSConfig c;
  c.set_block_replication_factor(r);
  c.set_metadata_replication_factor(r);
  c.set_blucket_count(n);
  c.set_metadata_shard_count(n);

  for (int i = 0; i < r; i++) {
    for (int j = 0; j < n; j++) {
      int m = i*n+j;  // machine id
      c.add_replicas()->set_machine(m);
      c.mutable_replicas(m)->set_replica(i);
      c.add_bluckets()->set_id(j);
      c.mutable_bluckets(m)->set_replica(i);
      c.mutable_bluckets(m)->set_machine(m);
      c.add_metadata_shards()->set_id(j);
      c.mutable_metadata_shards(m)->set_replica(i);
      c.mutable_metadata_shards(m)->set_machine(m);
    }
  }
  return c;
}

////////////////////////////////////////////////////////////////////////////////

LocalCalvinFS::LocalCalvinFS()
    : machine_(new Machine(0, ClusterConfig::LocalCluster(1))),
      blocks_(new HybridBlockStore()) {
  // Save basic config info.
  string fsconfig;
  MakeCalvinFSConfig().SerializeToString(&fsconfig);
  machine_->AppData()->Put("calvinfs-config", fsconfig);

  // Start metadata store app.
  StartAppProto sap;
  sap.add_participants(0);
  sap.set_app("MetadataStoreApp");
  sap.set_app_name("metadata");
  machine_->AddApp(sap);
  metadata_ = reinterpret_cast<StoreApp*>(machine_->GetApp("metadata"));
  reinterpret_cast<MetadataStore*>(metadata_->store())->SetMachine(machine_);

  // Start scheduler app.
  sap.set_app("SerialScheduler");
  sap.set_app_name("scheduler");
  machine_->AddApp(sap);
  scheduler_ = reinterpret_cast<Scheduler*>(machine_->GetApp("scheduler"));

  // Bind scheduler to store.
  scheduler_->SetStore("metadata", 0);

  // Start log app.
  sap.set_app("LogApp");
  sap.set_app_name("log");
  machine_->AddApp(sap);
  log_ = reinterpret_cast<LogApp*>(machine_->GetApp("log"));

  // Connect log to scheduler.
  source_ = new RemoteLogSource<Action>(machine_, 0, "log");
  scheduler_->SetActionSource(source_);

  // Get results queue.
  results_ = machine_->DataChannel("action-results");
}

LocalCalvinFS::~LocalCalvinFS() {
  delete blocks_;
  delete machine_;
  delete source_;
}

// NOTE: This is a snapshot (not linearizable) read.
//
// TODO(agt): Avoid rereading blocks that appear multiple times in a file?
Status LocalCalvinFS::ReadFileToString(const string& path, string* data) {
  data->clear();

  // Lookup MetadataEntry.
  Action a;
  a.set_version(scheduler_->SafeVersion());
  a.set_action_type(MetadataAction::LOOKUP);
  MetadataAction::LookupInput in;
  in.set_path(path);
  in.SerializeToString(a.mutable_input());
  metadata_->GetRWSets(&a);
  metadata_->Run(&a);
  MetadataAction::LookupOutput out;
  out.ParseFromString(a.output());
  if (!out.success()) {
    return Status::Error("metadata lookup error");
  }
  if (out.entry().type() != DATA) {
    return Status::Error("wrong file type");
  }

  // Get blocks.
  for (int i = 0; i < out.entry().file_parts_size(); i++) {
    if (out.entry().file_parts(i).block_id() == 0) {
      // Implicit all-zero block!
      data->append(out.entry().file_parts(i).length(), '\0');
    } else {
      // Block from block store.
      string block;
      if (!blocks_->Get(out.entry().file_parts(i).block_id(), &block)) {
        return Status::Error("block lookup error");
      }
      data->append(
          block,
          out.entry().file_parts(i).block_offset(),
          out.entry().file_parts(i).length());
    }
  }

  return Status::OK();
}

Status LocalCalvinFS::CreateDirectory(const string& path) {
  Action a;
  a.set_client_machine(0);
  a.set_client_channel("action-results");
  a.set_action_type(MetadataAction::CREATE_FILE);
  MetadataAction::CreateFileInput in;
  in.set_path(path);
  in.set_type(DIR);
  in.SerializeToString(a.mutable_input());
  metadata_->GetRWSets(&a);
  string as;
  a.SerializeToString(&as);
  log_->Append(as);

  MessageBuffer* m = NULL;
  while (!results_->Pop(&m)) {
    // Wait for action to complete and be sent back.
  }

  a.ParseFromArray((*m)[0].data(), (*m)[0].size());
  delete m;

  // Success?
  MetadataAction::CreateFileOutput out;
  out.ParseFromString(a.output());
  return out.success() ? Status::OK() : Status::Error("failed to create dir");
}

Status LocalCalvinFS::CreateFile(const string& path) {
  Action a;
  a.set_client_machine(0);
  a.set_client_channel("action-results");
  a.set_action_type(MetadataAction::CREATE_FILE);
  MetadataAction::CreateFileInput in;
  in.set_path(path);
  in.set_type(DATA);
  in.SerializeToString(a.mutable_input());
  metadata_->GetRWSets(&a);
  string as;
  a.SerializeToString(&as);
  log_->Append(as);

  MessageBuffer* m = NULL;
  while (!results_->Pop(&m)) {
    // Wait for action to complete and be sent back.
  }

  a.ParseFromArray((*m)[0].data(), (*m)[0].size());
  delete m;


  // Success?
  MetadataAction::CreateFileOutput out;
  out.ParseFromString(a.output());
  return out.success() ? Status::OK() : Status::Error("failed to create file");
}

// TODO(agt): Break up large writes into multiple blocks?
Status LocalCalvinFS::AppendStringToFile(const string& data, const string& path) {
  // Write data block.
  uint64 block_id = machine_->GetGUID() * 2 + (data.size() > 1024 ? 1 : 0);
  blocks_->Put(block_id, data);

  // Update metadata.
  Action a;
  a.set_client_machine(0);
  a.set_client_channel("action-results");
  a.set_action_type(MetadataAction::APPEND);
  MetadataAction::AppendInput in;
  in.set_path(path);
  in.add_data();
  in.mutable_data(0)->set_length(data.size());
  in.mutable_data(0)->set_block_id(block_id);
  in.SerializeToString(a.mutable_input());
  metadata_->GetRWSets(&a);
  string as;
  a.SerializeToString(&as);
  log_->Append(as);

  MessageBuffer* m = NULL;
  while (!results_->Pop(&m)) {
    // Wait for action to complete and be sent back.
  }

  a.ParseFromArray((*m)[0].data(), (*m)[0].size());
  delete m;

  // Success?
  MetadataAction::AppendOutput out;
  out.ParseFromString(a.output());
  return out.success() ? Status::OK() : Status::Error("append failed");
}

// NOTE: Snapshot (not linearizable) read.
Status LocalCalvinFS::LS(const string& path, vector<string>* contents) {
  contents->clear();

  // Lookup MetadataEntry.
  Action a;
  a.set_version(scheduler_->SafeVersion());
  a.set_action_type(MetadataAction::LOOKUP);
  MetadataAction::LookupInput in;
  in.set_path(path);
  in.SerializeToString(a.mutable_input());
  metadata_->GetRWSets(&a);
  metadata_->Run(&a);

  MetadataAction::LookupOutput out;
  out.ParseFromString(a.output());
  if (!out.success()) {
    return Status::Error("metadata lookup error");
  }
  if (out.entry().type() != DIR) {
    return Status::Error("wrong file type");
  }

  // Read dir contents.
  for (int i = 0; i < out.entry().dir_contents_size(); i++) {
    contents->push_back(out.entry().dir_contents(i));
  }

  return Status::OK();
}

Status LocalCalvinFS::Remove(const string& path) {
  return Status::Error("not implemented");
}

Status LocalCalvinFS::Copy(const string& from_path, const string& to_path) {
  return Status::Error("not implemented");
}

Status LocalCalvinFS::WriteStringToFile(const string& data, const string& path) {
  return Status::Error("not implemented");
}

