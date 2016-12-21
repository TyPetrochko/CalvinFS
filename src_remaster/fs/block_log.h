// Author: Alex Thomson (thomson@cs.yale.edu)
//         Kun  Ren <kun.ren@yale.edu>
//
// TODO(agt): Reduce number of string copies.

#ifndef CALVIN_FS_BLOCK_LOG_H_
#define CALVIN_FS_BLOCK_LOG_H_

#include <google/protobuf/repeated_field.h>
#include <set>
#include <vector>

#include "common/types.h"
#include "components/log/log.h"
#include "components/log/log_app.h"
#include "components/log/paxos2.h"
#include "fs/batch.pb.h"
#include "fs/block_store.h"
#include "fs/calvinfs.h"
#include "fs/metadata.pb.h"
#include "machine/app/app.h"
#include "proto/action.pb.h"

using std::set;
using std::vector;
using std::make_pair;

class Header;
class Machine;
class MessageBuffer;

class SequenceSource : public Source<UInt64Pair*> {
 public:
  explicit SequenceSource(Source<PairSequence*>* source)
      : source_(source), current_(NULL), index_(0), offset_(0) {
  }
  virtual ~SequenceSource() {
    delete source_;
    delete current_;
  }

  virtual bool Get(UInt64Pair** p) {
    // Get a non-empty sequence or return false.
    while (current_ == NULL) {
      if (!source_->Get(&current_)) {
        return false;
      }
      if (current_->pairs_size() == 0) {
        delete current_;
        current_ = NULL;
        LOG(ERROR) <<"^^^^^^^^^SequenceSource wrong!!!";

      } else {
        index_ = 0;
        offset_ = current_->misc();
      }
    }

    // Expose next element in sequence.
    *p = new UInt64Pair();
    (*p)->set_first(current_->pairs(index_).first());
    (*p)->set_second(offset_);
    offset_ +=  current_->pairs(index_).second();
    if (++index_ == current_->pairs_size()) {
      delete current_;
      current_ = NULL;
    }
    return true;
  }

 private:
  Source<PairSequence*>* source_;
  PairSequence* current_;
  int index_;
  int offset_;
};

class BlockLogApp : public App {
 public:
  BlockLogApp() : go_(true), going_(false), to_delete_(60) {}

  virtual ~BlockLogApp() {
    Stop();
  }

  virtual void Start() {
    // Get local config info.
    config_ = new CalvinFSConfigMap(machine());

    // Record local replica id.
    replica_ = config_->LookupReplica(machine()->machine_id());

    // Record local replica's paxos machine.
    uint32 replica_count = config_->config().block_replication_factor();
    local_paxos_leader_ = replica_ * (machine()->config().size() / replica_count);

    // Note what machines contain metadata shards on the same replica.
    for (auto it = config_->mds().begin(); it != config_->mds().end(); ++it) {
      if (it->first.second == replica_) {
        mds_.insert(it->second);
      }
    }

    // Get ptr to local block store app.
    blocks_ = reinterpret_cast<BlockStoreApp*>(machine()->GetApp("blockstore"))
        ->blocks_;

    // Get ptr to paxos leader (maybe).
    if (machine()->machine_id() == local_paxos_leader_) {
      paxos_leader_ =
          reinterpret_cast<Paxos2App*>(machine()->GetApp("paxos2"));
    }

    // Get reader of Paxos output (using closest paxos machine).
    batch_sequence_ =
        new SequenceSource(
            new RemoteLogSource<PairSequence>(machine(), local_paxos_leader_, "paxos2"));

    // Okay, finally, start main loop!
    going_ = true;
    while (go_.load()) {
      // Create new batch once per epoch.
      double next_epoch = GetTime() + 0.005;

      // Create batch (iff there are any pending requests).
      int count = queue_.Size();
      int remaster_count = remaster_queue_.Size();
      int remaster_postpone_count = remaster_postponed_.Size();
      if (count > 0 || remaster_count > 0 || remaster_postpone_count > 0) {
        ActionBatch batch;
        uint64 actual_offset = 0;
        uint32 current_replica = config_->LookupReplica(machine()->machine_id());

        vector<Action*> remaster_actions;
        set<string> dangerous_paths;
        for (int i = 0; i < remaster_count; i++) {
          Action* a = NULL;
          remaster_queue_.Pop(&a);
          remaster_actions.push_back(a);
          MetadataAction::RemasterInput in;
          in.ParseFromString(a->input());
          dangerous_paths.insert(in.path());
        }

        // copy regular actions into batch
        for (int i = 0; i < count; i++) {
          Action* a = NULL;
          queue_.Pop(&a);

          // want to make sure this action has all files it needs mastered
          // locally, and that none of them are being remastered away
          // by any remaster transaction (assume all actions are run in parallel)
          if (ActionIsLocal(a, &dangerous_paths)) {
            a->set_version_offset(actual_offset++);
            a->set_origin(current_replica);
            batch.mutable_entries()->AddAllocated(a);
          } else {
            // if not all files are mastered here, forward up to the client to
            // do some forwarding or remastering.
            SendUpToClient(a);
          }
          // TODO If it collides, send it as an "action RPC" to client app
        }

        // copy remaster actions into batch
        for (int i = 0; i < remaster_count; i++) {
          Action* a = remaster_actions.back();
          remaster_actions.pop_back();
          a->set_version_offset(actual_offset++);
          a->set_origin(current_replica);
          LOG(ERROR) << "Adding action "<<IntToString(a->distinct_id())<<" to the batch";
          // remaster it right here, because adding it to the batch doesn't work
          MetadataAction::RemasterInput in;
          in.ParseFromString(a->input());
          LOG(ERROR) << "HACK CHANGING MAP FROM BLOCK_LOG on machine "<<machine()->machine_id()<<" for path /"<<in.path()<<" changing master "<<in.old_master()<<"->"<<in.new_master();
          config_->ChangeReplicaForPath(in.path(), in.new_master(), machine());
          // batch.mutable_entries()->AddAllocated(a);
        }

        // Move all from remaster_postponed into remaster_queue,
        // so they will be put with the next batch.
        Action* remaster_action = NULL;
        while (remaster_postponed_.Pop(&remaster_action)) {
          LOG(ERROR) << "Postponed txn with ID "<<remaster_action->distinct_id()<<" and type "<<remaster_action->action_type()<<" will soon be executed";
          remaster_queue_.Push(remaster_action);
        }

        // Avoid multiple allocation.
        string* block = new string();
        batch.SerializeToString(block);

        // Choose block_id.
        uint64 block_id =
            machine()->GetGUID() * 2 + (block->size() > 1024 ? 1 : 0);

        // Send batch to block stores.
        for (uint64 i = 0; i < config_->config().block_replication_factor();
             i++) {
          Header* header = new Header();
          header->set_from(machine()->machine_id());
          header->set_to(
              config_->LookupBlucket(config_->HashBlockID(block_id), i));
          header->set_type(Header::RPC);
          header->set_app(name());
          header->set_rpc("BATCH");
          header->add_misc_int(block_id);
          header->add_misc_int(actual_offset);
          header->add_misc_bool(true);
          machine()->SendMessage(header, new MessageBuffer(Slice(*block)));
        }

        // Scheduler block for eventual deallocation.
        //to_delete_.Push(block);
      }

      // Delete old blocks.
      /**string* block;
      while (to_delete_.Pop(&block)) {
        delete block;
      }**/

      // Sleep until next epoch.
      SpinUntil(next_epoch);
    }

    going_ = false;
  }

  bool ActionIsLocal(Action* a, set<string>* dangerous_paths) {
    map<string, uint32> masters;
    // figures out what replicas are needed by a and for which files
    config_->LookupInvolvedReplicas(a, machine(), &masters);
    for (auto it = masters.begin(); it != masters.end(); it++) {
      if (it->second != replica_) {
        LOG(ERROR) << "Machine "<<IntToString(machine()->machine_id()) <<
            " must re-queue " << it->first << " because it's not mastered here. is mastered on "<<it->second
            <<". action id is " << a->distinct_id() << " type is " << a->action_type();
        return false;
      }
      if (dangerous_paths->find(it->first) != dangerous_paths->end()) {
        LOG(ERROR) << "Machine "<<IntToString(machine()->machine_id()) <<
            " must re-queue " << it->first << " because the path is being remastered";
        return false;
      }
    }
    return true;
  }

  void SendUpToClient(Action* a) {
    Header* header = new Header();
    header->set_from(machine()->machine_id());
    header->set_to(machine()->machine_id());
    header->set_type(Header::RPC);
    header->set_app("client");
    header->set_rpc("ACTION");
    string* block = new string();
    a->SerializeToString(block);
    machine()->SendMessage(header, new MessageBuffer(Slice(*block)));
  }

  virtual void Stop() {
    go_ = false;
    while (going_.load()) {
      usleep(10);
    }
  }

  // Takes ownership of '*entry'.
  virtual void Append(Action* entry) {
    entry->set_origin(replica_);
    queue_.Push(entry);
  }

  virtual void Append(const Slice& entry, uint64 count = 1) {
    CHECK(count == 1);
    Action* a = new Action();
    a->ParseFromArray(entry.data(), entry.size());
    a->set_origin(replica_);
    queue_.Push(a);
  }

  virtual void HandleMessage(Header* header, MessageBuffer* message) {
    // Don't run any RPCs before Start() is called.
    while (!going_.load()) {
      usleep(10);
      if (!go_.load()) {
        return;
      }
    }
    
    if (header->rpc() == "APPEND") {
      Action* a = new Action();
      a->ParseFromArray((*message)[0].data(), (*message)[0].size());
      a->set_origin(replica_);
      if(a->remaster()){
        LOG(ERROR) << "Received remaster request on machine " << IntToString(machine()->machine_id())
        << " with action type " << IntToString((int)a->action_type());
        MetadataAction::RemasterInput in;
        in.ParseFromString(a->input());
        switch(a->action_type()){
          case MetadataAction::REMASTER:
            // sent to some node on old master.
            // send synchronously to other machines on this replica
            LOG(ERROR) << "About to synchronously update entire replica";
            config_->SendIntrareplicaRemasterRequests(in, machine(), true);
            LOG(ERROR) << "Done synchronously updating entire replica";
            // now send REMASTER_FOLLOWS to other replicas
            config_->SendRemasterFollows(in, machine());
            remaster_queue_.Push(a);
            break;
          case MetadataAction::REMASTER_SYNC:
            // sent to every other node on a replica
            // change master-map at the end of this epoch
            remaster_queue_.Push(a);
            // notify when complete
            machine()->SendReplyMessage(header, new MessageBuffer());
            break;
          case MetadataAction::REMASTER_FOLLOW:
            // change master map at the end of the next epoch
            config_->SendIntrareplicaRemasterRequests(in, machine(), false);
          case MetadataAction::REMASTER_ASYNC:
            LOG(ERROR) << "Added txn id "<< a->distinct_id()<<" with type "<< a->action_type() << " to the postponed queue";
            remaster_postponed_.Push(a);
            break;
          default:
            LOG(FATAL) << "Unknown remaster command";
        }
      } else{
        // regular action!
        queue_.Push(a);
      }
      // LOG(ERROR) << "Machine: "<<machine()->machine_id() <<" =>Block log recevie a APPEND request. distinct id is:"<< a->distinct_id()<<" from machine:"<<header->from();
    } else if (header->rpc() == "BATCH") {
      // Write batch block to local block store.
      uint64 block_id = header->misc_int(0);
      uint64 batch_size = header->misc_int(1);
      bool need_submit = header->misc_bool(0);

      blocks_->Put(block_id, (*message)[0]);
      // Parse batch.
      ActionBatch batch;
      batch.ParseFromArray((*message)[0].data(), (*message)[0].size());
      uint64 message_from_ = header->from();

      //  If (This batch come from this replica) → send SUBMIT to the Sequencer(LogApp) on the master node of the local paxos participants
      if (config_->LookupReplica(message_from_) == replica_ && need_submit == true) {
        Header* header = new Header();
        header->set_from(machine()->machine_id());
        header->set_to(local_paxos_leader_);  // Local Paxos leader.
        header->set_type(Header::RPC);
        header->set_app(name());
        header->set_rpc("SUBMIT");
        header->add_misc_int(block_id);
        header->add_misc_int(batch_size);
        machine()->SendMessage(header, new MessageBuffer());
      }

      // Forward sub-batches to relevant readers (same replica only).
      map<uint64, ActionBatch> subbatches;
      for (int i = 0; i < batch.entries_size(); i++) {
        set<uint64> recipients;
    
        if (batch.entries(i).fake_action() == true) {
          continue;        
        }

        if (batch.entries(i).action_type() == MetadataAction::REMASTER
            || batch.entries(i).action_type() == MetadataAction::REMASTER_FOLLOW
            || batch.entries(i).action_type() == MetadataAction::REMASTER_ASYNC
            || batch.entries(i).action_type() == MetadataAction::REMASTER_SYNC) {
          MetadataAction::RemasterInput in;
          in.ParseFromString(batch.entries(i).input());
          recipients.insert(in.dest());
        } else {

          for (int j = 0; j < batch.entries(i).readset_size(); j++) {
            if (config_->LookupReplicaByDir(batch.entries(i).readset(j), machine()) == batch.entries(i).origin()) {
              uint64 mds = config_->HashFileName(batch.entries(i).readset(j));
              recipients.insert(config_->LookupMetadataShard(mds, replica_));
            }
          }
          for (int j = 0; j < batch.entries(i).writeset_size(); j++) {
            if (config_->LookupReplicaByDir(batch.entries(i).writeset(j), machine()) == batch.entries(i).origin()) {
              uint64 mds = config_->HashFileName(batch.entries(i).writeset(j));
              recipients.insert(config_->LookupMetadataShard(mds, replica_));
            }
          }
        }
        LOG(ERROR) << "machine "<<machine()->machine_id()<<" has action as part of batch with id "<<batch.entries(i).distinct_id();

        for (auto it = recipients.begin(); it != recipients.end(); ++it) {
          subbatches[*it].add_entries()->CopyFrom(batch.entries(i));
        }
      }

      for (auto it = mds_.begin(); it != mds_.end(); ++it) {
        header = new Header();
        header->set_from(machine()->machine_id());
        header->set_to(*it);
        header->set_type(Header::RPC);
        header->set_app(name());
        header->set_rpc("SUBBATCH");
        header->add_misc_int(block_id);
        machine()->SendMessage(header, new MessageBuffer(subbatches[*it]));
      }

    } else if (header->rpc() == "SUBMIT") {

      uint64 block_id = header->misc_int(0);

      uint64 count = header->misc_int(1);
      paxos_leader_->Append(block_id, count);
    } else if (header->rpc() == "SUBBATCH") {
      uint64 block_id = header->misc_int(0);
      ActionBatch* batch = new ActionBatch();
      batch->ParseFromArray((*message)[0].data(), (*message)[0].size());
      subbatches_.Put(block_id, batch);
    } else if (header->rpc() == "APPEND_MULTIREPLICA_ACTIONS") {
      MessageBuffer* m = NULL;
      PairSequence sequence;
      AtomicQueue<Action*> new_generated_queue;

      paxos_leader_->GetRemoteSequence(&m);
      CHECK(m != NULL);

      sequence.ParseFromArray((*m)[0].data(), (*m)[0].size());
      ActionBatch* fake_subbatch = NULL;
      Action* new_action;

      for (int i = 0; i < sequence.pairs_size();i++) {
        uint64 fake_subbatch_id = sequence.pairs(i).first();

        bool got_it;
        do {
          got_it = fakebatches_.Lookup(fake_subbatch_id, &fake_subbatch);
          usleep(10);
        } while (got_it == false);

        if (fake_subbatch->entries_size() == 0) {
          continue;
        }
        
        int subbatch_size = fake_subbatch->entries_size();

        for (int j = 0; j < subbatch_size / 2; j++) {
          fake_subbatch->mutable_entries()->SwapElements(j, fake_subbatch->entries_size()-1-j);
        }
        
        for (int j = 0; j < subbatch_size; j++) {
          new_action = new Action();
          new_action->CopyFrom(*(fake_subbatch->mutable_entries()->ReleaseLast()));
          if (new_action->fake_action() == false) {
            new_action->clear_client_machine();
            new_action->clear_client_channel();
          } else {
            new_action->set_fake_action(false);
          }

          new_action->set_new_generated(true);
          new_generated_queue.Push(new_action);
        }

        fakebatches_.Erase(fake_subbatch_id);
        delete fake_subbatch;
        fake_subbatch = NULL;
      }

      if (new_generated_queue.Size() > 0) {
        // Generated a new batch and submit to paxos leader.
        int count = new_generated_queue.Size();
        uint64 block_id = 0;

        if (count != 0) {
          ActionBatch batch;

          for (int i = 0; i < count; i++) {
            Action* a = NULL;
            new_generated_queue.Pop(&a);

            a->set_version_offset(i);
	          a->set_origin(config_->LookupReplica(machine()->machine_id()));
            batch.mutable_entries()->AddAllocated(a);
          }

          // Avoid multiple allocation.
          string* block = new string();
          batch.SerializeToString(block);

          // Choose block_id.
          block_id = machine()->GetGUID() * 2 + (block->size() > 1024 ? 1 : 0);


          // Send batch to block stores.
          for (uint64 i = 0; i < config_->config().block_replication_factor(); i++) {
            Header* header = new Header();
            header->set_from(machine()->machine_id());
            header->set_to(config_->LookupBlucket(config_->HashBlockID(block_id), i));
            header->set_type(Header::RPC);
            header->set_app(name());
            header->set_rpc("BATCH");
            header->add_misc_int(block_id);
            header->add_misc_int(count);
            header->add_misc_bool(false);
            machine()->SendMessage(header, new MessageBuffer(Slice(*block)));
          }
          
          //to_delete_.Push(block);
        }

         // Submit to paxos leader
         paxos_leader_->Append(block_id, count);
       }

      // Send ack to paxos_leader.
      Header* h = new Header();
      h->set_from(machine()->machine_id());
      h->set_to(machine()->machine_id());
      h->set_type(Header::ACK);

      Scalar s;
      s.ParseFromArray((*message)[0].data(), (*message)[0].size());
      h->set_ack_counter(FromScalar<uint64>(s));
      machine()->SendMessage(h, new MessageBuffer());   

    } else {
      LOG(FATAL) << "unknown RPC type: " << header->rpc();
    }
  }

  Source<Action*>* GetActionSource() {
    return new ActionSource(this);
  }

 private:
  // True iff main thread SHOULD run.
  std::atomic<bool> go_;

  // True iff main thread IS running.
  std::atomic<bool> going_;

  // CalvinFS configuration.
  CalvinFSConfigMap* config_;

  // Replica to which we belong.
  uint64 replica_;

  // This machine's local block store.
  BlockStore* blocks_;

  // List of machine ids that have metadata shards with replica == replica_.
  set<uint64> mds_;

  // Local paxos app (used only by machine 0).
  Paxos2App* paxos_leader_;

  // Number of votes for each batch (used only by machine 0).
  map<uint64, int> batch_votes_;

  // Subbatches received.
  AtomicMap<uint64, ActionBatch*> subbatches_;
  
  // Paxos log output.
  Source<UInt64Pair*>* batch_sequence_;

  // Pending append requests.
  AtomicQueue<Action*> queue_;
  
  // Pending remaster requests.
  AtomicQueue<Action*> remaster_queue_;
  
  // Remaster requests that have been postponed
  // to ensure new master has received all relevant txns
  AtomicQueue<Action*> remaster_postponed_;

  // Delayed deallocation queue.
  // TODO(agt): Ugh this is horrible, we should replace this with ref counting!
  DelayQueue<string*> to_delete_;

  uint64 local_paxos_leader_;

  // fake multi-replicas actions batch received.
  AtomicMap<uint64, ActionBatch*> fakebatches_;

  friend class ActionSource;
  class ActionSource : public Source<Action*> {
   public:
    virtual ~ActionSource() {}
    virtual bool Get(Action** a) {
      while (true) {
        // Make sure we have a valid (i.e. non-zero) subbatch_id_, or return
        // false if we can't get one.
        if (subbatch_id_ == 0) {
          UInt64Pair* p = NULL;
          if (log_->batch_sequence_->Get(&p)) {
            subbatch_id_ = p->first();
            subbatch_version_ = p->second();
            delete p;
          } else {
            usleep(50);
            return false;
          }
        }

        // Make sure we have a valid pointer to the current (nonempty)
        // subbatch, or return false if we can't get one.
        if (subbatch_ == NULL) {
          // Have we received the subbatch corresponding to subbatch_id_?
          if (!log_->subbatches_.Lookup(subbatch_id_, &subbatch_)) {
            // Nope. Gotta try again later.
            usleep(20);
            return false;
          } else {
            // Got the subbatch! Is it empty?
            if (subbatch_->entries_size() == 0) {
              // Doh, the batch was empty! Throw it away and keep looking.
              delete subbatch_;
              log_->subbatches_.Erase(subbatch_id_);
              subbatch_ = NULL;
              subbatch_id_ = 0;
            } else {
              // Okay, got a non-empty subbatch! Reverse the order of elements
              // so we can now repeatedly call ReleaseLast on the entries.
              for (int i = 0; i < subbatch_->entries_size() / 2; i++) {
                subbatch_->mutable_entries()->SwapElements(
                    i,
                    subbatch_->entries_size()-1-i);
              }
              // Now we are ready to start returning actions from this subbatch.
              break;
            }
          }
        } else {
          // Already had a good subbatch. Onward.
          break;
        }
      }

      // Should be good to go now.
      CHECK(subbatch_->entries_size() != 0);
      *a = subbatch_->mutable_entries()->ReleaseLast();
      (*a)->set_version(subbatch_version_ + (*a)->version_offset());
      (*a)->clear_version_offset();
      if (subbatch_->entries_size() == 0) {
        // Okay, NOW the batch is empty.
        delete subbatch_;
        log_->subbatches_.Erase(subbatch_id_);
        subbatch_ = NULL;
        subbatch_id_ = 0;
      }
      return true;
    }

   private:
    friend class BlockLogApp;
    ActionSource(BlockLogApp* log)
      : log_(log), subbatch_id_(0), subbatch_(NULL) {
    }

    // Pointer to underlying BlockLogApp.
    BlockLogApp* log_;

    // ID of (and pointer to) current subbatch (which is technically owned by
    // log_->subbatches_).
    uint64 subbatch_id_;
    ActionBatch* subbatch_;

    // Version of initial action in subbatch_.
    uint64 subbatch_version_;
  };
};

#endif  // CALVIN_FS_BLOCK_LOG_H_

