// Author: Alex Thomson (thomson@cs.yale.edu)
// Author: Kun  Ren (kun.ren@yale.edu)
//

#include "components/log/paxos2.h"

#include <atomic>
#include <glog/logging.h>
#include <google/protobuf/message.h>
#include <queue>
#include <set>
#include <utility>
#include <vector>

#include "common/types.h"
#include "components/log/log.h"
#include "components/log/log_reader.pb.h"
#include "components/log/local_mem_log.h"
#include "machine/machine.h"
#include "machine/message_buffer.h"
#include "proto/header.pb.h"
#include "proto/scalar.pb.h"


using std::atomic;
using std::make_pair;
using std::pair;
using std::queue;
using std::set;
using std::vector;

// Make Paxos2App startable.
REGISTER_APP(Paxos2App) {
  Scalar s;
  s.ParseFromString(ARG);
  return new Paxos2App(new LocalMemLog(), FromScalar<uint64>(s));
}

REGISTER_APP(Paxos2App2) {
  StartAppProto sap;
  sap.ParseFromString(ARG);
  vector<uint64> participants;
  for (int i = 0; i < sap.participants_size(); i++) {
    participants.push_back(sap.participants(i));
  }
  return new Paxos2App(new LocalMemLog(), participants);
}

Paxos2App::Paxos2App(Log* log, const vector<uint64>& participants)
    : participants_(participants), go_(true), going_(false), count_(0) {
  log_ = log;
  local_log_ = new LocalMemLog();
  has_local_sequence_ = 0;
}

Paxos2App::Paxos2App(Log* log, uint64 count)
    : go_(true), going_(false), count_(0) {
  log_ = log;
  local_log_ = new LocalMemLog();
  has_local_sequence_ = 0;
  for (uint64 i = 0; i < count; i++) {
    participants_.push_back(i);
  }
}

bool Paxos2App::IsLeader() {
  return machine()->machine_id() == participants_[0];
}

void Paxos2App::Append(uint64 blockid, uint64 count) {
  // Forward append request to leader.
  Header* header = new Header();
  header->set_from(machine()->machine_id());
  header->set_to(participants_[0]);
  header->set_type(Header::RPC);
  header->set_app(name());
  header->set_rpc("APPEND");
  header->add_misc_int(blockid);
  header->add_misc_int(count);
  machine()->SendMessage(header, new MessageBuffer());
}

void Paxos2App::GetRemoteSequence(MessageBuffer** result) {
  bool get_it = sequences_other_replicas.Front(result);
  CHECK(get_it == true);
}

void Paxos2App::Start() {
  going_ = true;
  replica_count = (machine()->config().size() >= 3) ? 3 : 1;
  partitions_per_replica = machine()->config().size() / replica_count;

  if (IsLeader()) {
    RunLeader();
  } else {
    RunFollower();
  }
  going_ = false;
}

void Paxos2App::Stop() {
  go_ = false;
  while (going_.load()) {
    // Wait for main loop to stop.
    usleep(10);
  }
}

void Paxos2App::HandleOtherMessages(Header* header, MessageBuffer* message) {
  if (header->rpc() == "APPEND") {
    Lock l(&mutex_);
    UInt64Pair* p = sequence_.add_pairs();
    p->set_first(header->misc_int(0));
    p->set_second(header->misc_int(1));
    count_ += p->second();
    has_local_sequence_ = 1;
//LOG(ERROR) << "Machine: "<<machine()->machine_id()<< " ++Paxos2 recevie a Append request. block id is:"<< header->misc_int(0)<<"  count is:"<<header->misc_int(1)<<" from machine:"<<header->from();

  } else if (header->rpc() == "NEW-SEQUENCE") {

PairSequence other_sequence;
other_sequence.ParseFromArray((*message)[0].data(), (*message)[0].size());
CHECK(other_sequence.pairs_size() != 0);

MessageBuffer* m = new MessageBuffer(other_sequence);

    Scalar s;
    s.ParseFromArray((*message)[1].data(), (*message)[1].size());
    m->Append(s);
    s.ParseFromArray((*message)[2].data(), (*message)[2].size());
    m->Append(s);
//LOG(ERROR) << "Machine: "<<machine()->machine_id()<< " ++Paxos2 recevie a NEW-SEQUENCE  from machine:"<<header->from()<<"  version is:"<<other_sequence.misc();
//      for (int i = 0; i < other_sequence.pairs_size();i++) {
//LOG(ERROR) << "Machine: "<<machine()->machine_id()<< " ++Paxos2 recevie a NEW-SEQUENCEuint64 contains block_id:"<<other_sequence.pairs(i).first();
//      }
    sequences_other_replicas.Push(m);

  } else if (header->rpc() == "NEW-SEQUENCE-ACK") {
//LOG(ERROR) << "Machine: "<<machine()->machine_id()<< " ++Paxos2 recevie a NEW-SEQUENCE-ACK. from machine:"<<header->from();
    // Send next sequence to the from-replica
    Scalar s;
    s.ParseFromArray((*message)[0].data(), (*message)[0].size());
    uint32 from_replica = FromScalar<uint32>(s);
 
    Log::Reader* r = readers_for_local_log[from_replica];
    bool find = r->Next();
    while (find == false) {
      usleep(10);
      find = r->Next();
    }

    CHECK(find == true);

    Header* header2 = new Header();
    header2->set_from(machine()->machine_id());
    header2->set_to(from_replica * partitions_per_replica);
    header2->set_type(Header::RPC);
    header2->set_app(name());
    header2->set_rpc("NEW-SEQUENCE");
    MessageBuffer* m = new MessageBuffer();
    m->Append(r->Entry());
    m->Append(ToScalar<uint64>(r->Count()));
    m->Append(ToScalar<uint32>(machine()->machine_id()));
    machine()->SendMessage(header2, m);
//LOG(ERROR) << "Machine: "<<machine()->machine_id()<< "=>Paxos2: Send NEW-SEQUENCE(after receive ack) to: "<<from_replica<<" . version: "<<r->Version();
  } else {
    LOG(FATAL) << "unknown message type: " << header->rpc();
  }

  delete header;
  delete message;
}

void Paxos2App::RunLeader() {
  uint64 next_version = 1;
  int quorum = static_cast<int>(participants_.size()) / 2 + 1;
  set<atomic<int>*> ack_ptrs;
  MessageBuffer* m = NULL;
  bool isFirst = true;
  bool isLocal = false;

  for (uint64 i = 0; i < replica_count; i++) {
    readers_for_local_log[i] = local_log_->GetReader();
  }


  while (go_.load()) {
    // Sleep while there are NO requests.
    while (has_local_sequence_ == 0 && sequences_other_replicas.Size() == 0) {
      usleep(10);
      if (!go_.load()) {
        return;
      }
    }

    string encoded;
    uint64 version;
    uint32 from_machine = machine()->machine_id();
    PairSequence other_sequence;

    if (has_local_sequence_ > 0) {
      // Propose a new sequence.
      {
        Lock l(&mutex_);
        version = next_version;
        next_version += count_.load();
        sequence_.set_misc(version);
        sequence_.SerializeToString(&encoded);
        sequence_.Clear();
        isLocal = true;
        count_ = 0;
        has_local_sequence_ = 0;
//LOG(ERROR) << "Machine: "<<machine()->machine_id()<< "=>Paxos2 proposes a new sequence from local: version:"<< version<< " next_version is: "<<next_version;
      }
    } else if (sequences_other_replicas.Size() != 0) {
      atomic<int>* ack = new atomic<int>(0);

      Header* header = new Header();
      header->set_from(machine()->machine_id());
      header->set_to(machine()->machine_id());
      header->set_type(Header::RPC);
      header->set_app("blocklog");
      header->set_rpc("APPEND_MULTIREPLICA_ACTIONS");
      MessageBuffer* n = new MessageBuffer();
      n->Append(ToScalar<uint64>(reinterpret_cast<uint64>(ack)));
      machine()->SendMessage(header, n);

          // Collect Ack.
      while (ack->load() < 1) {
        usleep(10);
      }

      sequences_other_replicas.Pop(&m);
      version = next_version;

      other_sequence.ParseFromArray((*m)[0].data(), (*m)[0].size());
CHECK(other_sequence.pairs_size() != 0);
      other_sequence.set_misc(version);
      other_sequence.SerializeToString(&encoded);

      Scalar s;
      s.ParseFromArray((*m)[1].data(), (*m)[1].size());

      next_version += FromScalar<uint64>(s);
      s.ParseFromArray((*m)[2].data(), (*m)[2].size());
      from_machine = FromScalar<uint32>(s);
      isLocal = false;
//LOG(ERROR) << "Machine: "<<machine()->machine_id()<< "=>Paxos2 proposes a new sequence from other replicas: version:"<< version << " next_version is: "<<next_version<<". from: "<<from_machine;
    }



    // Propose a new sequence.
    atomic<int>* acks = new atomic<int>(1);
    ack_ptrs.insert(acks);
    for (uint32 i = 1; i < participants_.size(); i++) {
      Header* h = new Header();
      h->set_from(machine()->machine_id());
      h->set_to(participants_[i]);
      h->set_type(Header::DATA);
      h->set_data_channel("paxos2");
      m = new MessageBuffer(new string(encoded));
      m->Append(ToScalar<uint64>(version));
      m->Append(ToScalar<uint64>(next_version - version));
      m->Append(ToScalar<uint64>(reinterpret_cast<uint64>(acks)));
      machine()->SendMessage(h, m);
    }

    // Collect Acks.
    while (acks->load() < quorum) {
      usleep(10);
      if (!go_.load()) {
        return;
      }
    }

    // Commit!
    for (uint32 i = 1; i < participants_.size(); i++) {
      Header* h = new Header();
      h->set_from(machine()->machine_id());
      h->set_to(participants_[i]);
      h->set_type(Header::DATA);
      h->set_data_channel("paxos2");
      machine()->SendMessage(h, new MessageBuffer());
    }

    // Actually append the request into the log
    log_->Append(version, next_version - version, encoded);

    if (isLocal == true) {
      local_log_->Append(version, next_version - version, encoded);
    }
//LOG(ERROR) << "Machine: "<<machine()->machine_id()<< "=>Paxos2: Actually append the request into the log: version:"<< version<<" next_version is:"<<next_version;

   
    if (isLocal == true && isFirst == true) {
      // Send the sequence to the LeaderPaxosApp of all the other replicas;

      for (uint64 i = 0; i < replica_count; i++) {
        if (i != machine()->machine_id()/partitions_per_replica) {
          Log::Reader* r = readers_for_local_log[i];
          bool find = r->Next();
          CHECK(find == true);

          Header* header = new Header();
          header->set_from(machine()->machine_id());
          header->set_to(i*partitions_per_replica);
          header->set_type(Header::RPC);
          header->set_app(name());
          header->set_rpc("NEW-SEQUENCE");
          m = new MessageBuffer(new string(encoded));
	  m->Append(ToScalar<uint64>(next_version - version));
          m->Append(ToScalar<uint32>(machine()->machine_id()));
          machine()->SendMessage(header, m);

//LOG(ERROR) << "Machine: "<<machine()->machine_id()<< "=>Paxos2: Send NEW-SEQUENCE to: "<<i*partitions_per_replica<<" version:"<<version;
	}
      }

      isFirst = false;

    } else if (isLocal == false) {
      Header* header = new Header();
      header->set_from(machine()->machine_id());
      header->set_to(from_machine);
      header->set_type(Header::RPC);
      header->set_app(name());
      header->set_rpc("NEW-SEQUENCE-ACK");
      m = new MessageBuffer();
      m->Append(ToScalar<uint32>(machine()->machine_id()/partitions_per_replica));
      machine()->SendMessage(header, m);
//LOG(ERROR) << "Machine: "<<machine()->machine_id()<< "=>Paxos2: Send NEW-SEQUENCE-ACK to: "<<from_machine<<" version:"<<version;
    }

  }
}

void Paxos2App::RunFollower() {
  auto channel = machine()->DataChannel("paxos2");
  queue<MessageBuffer*> uncommitted;
  while (go_.load()) {
    // Get message from leader.
    MessageBuffer* m = NULL;
    while (!channel->Pop(&m)) {
      usleep(10);
      if (!go_.load()) {
        return;
      }
    }
    if (m->size() == 4) {
//LOG(ERROR) << "Machine: "<<machine()->machine_id()<< "=>Paxos2(Follower): Receive a new proposal:";
      // New proposal.
      uncommitted.push(m);
      // Send ack to leader.
      Header* h = new Header();
      h->set_from(machine()->machine_id());
      h->set_to(participants_[0]);
      h->set_type(Header::ACK);
      Scalar s;
      s.ParseFromArray((*m)[3].data(), (*m)[3].size());
      h->set_ack_counter(FromScalar<uint64>(s));
      machine()->SendMessage(h, new MessageBuffer());
    } else {
//LOG(ERROR) << "Machine: "<<machine()->machine_id()<< "=>Paxos2(Follower): Receive a commit message:";
      // Commit message.
      CHECK(!uncommitted.empty());
      delete m;
      m = uncommitted.front();
      uncommitted.pop();
      Scalar s;
      s.ParseFromArray((*m)[1].data(), (*m)[1].size());
      Scalar count;
      count.ParseFromArray((*m)[2].data(), (*m)[2].size());
      log_->Append(FromScalar<uint64>(s), FromScalar<uint64>(count), (*m)[0]);
      delete m;
    }
  }
}

