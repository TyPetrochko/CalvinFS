// Generated by the protocol buffer compiler.  DO NOT EDIT!
// source: cluster_config.proto

#ifndef PROTOBUF_cluster_5fconfig_2eproto__INCLUDED
#define PROTOBUF_cluster_5fconfig_2eproto__INCLUDED

#include <string>

#include <google/protobuf/stubs/common.h>

#if GOOGLE_PROTOBUF_VERSION < 3000000
#error This file was generated by a newer version of protoc which is
#error incompatible with your Protocol Buffer headers.  Please update
#error your headers.
#endif
#if 3000000 < GOOGLE_PROTOBUF_MIN_PROTOC_VERSION
#error This file was generated by an older version of protoc which is
#error incompatible with your Protocol Buffer headers.  Please
#error regenerate this file with a newer version of protoc.
#endif

#include <google/protobuf/arena.h>
#include <google/protobuf/arenastring.h>
#include <google/protobuf/generated_message_util.h>
#include <google/protobuf/metadata.h>
#include <google/protobuf/message.h>
#include <google/protobuf/repeated_field.h>
#include <google/protobuf/extension_set.h>
#include <google/protobuf/unknown_field_set.h>
// @@protoc_insertion_point(includes)

// Internal implementation detail -- do not call these.
void protobuf_AddDesc_cluster_5fconfig_2eproto();
void protobuf_AssignDesc_cluster_5fconfig_2eproto();
void protobuf_ShutdownFile_cluster_5fconfig_2eproto();

class ClusterConfigProto;
class MachineInfo;

// ===================================================================

class MachineInfo : public ::google::protobuf::Message /* @@protoc_insertion_point(class_definition:MachineInfo) */ {
 public:
  MachineInfo();
  virtual ~MachineInfo();

  MachineInfo(const MachineInfo& from);

  inline MachineInfo& operator=(const MachineInfo& from) {
    CopyFrom(from);
    return *this;
  }

  inline const ::google::protobuf::UnknownFieldSet& unknown_fields() const {
    return _internal_metadata_.unknown_fields();
  }

  inline ::google::protobuf::UnknownFieldSet* mutable_unknown_fields() {
    return _internal_metadata_.mutable_unknown_fields();
  }

  static const ::google::protobuf::Descriptor* descriptor();
  static const MachineInfo& default_instance();

  void Swap(MachineInfo* other);

  // implements Message ----------------------------------------------

  inline MachineInfo* New() const { return New(NULL); }

  MachineInfo* New(::google::protobuf::Arena* arena) const;
  void CopyFrom(const ::google::protobuf::Message& from);
  void MergeFrom(const ::google::protobuf::Message& from);
  void CopyFrom(const MachineInfo& from);
  void MergeFrom(const MachineInfo& from);
  void Clear();
  bool IsInitialized() const;

  int ByteSize() const;
  bool MergePartialFromCodedStream(
      ::google::protobuf::io::CodedInputStream* input);
  void SerializeWithCachedSizes(
      ::google::protobuf::io::CodedOutputStream* output) const;
  ::google::protobuf::uint8* InternalSerializeWithCachedSizesToArray(
      bool deterministic, ::google::protobuf::uint8* output) const;
  ::google::protobuf::uint8* SerializeWithCachedSizesToArray(::google::protobuf::uint8* output) const {
    return InternalSerializeWithCachedSizesToArray(false, output);
  }
  int GetCachedSize() const { return _cached_size_; }
  private:
  void SharedCtor();
  void SharedDtor();
  void SetCachedSize(int size) const;
  void InternalSwap(MachineInfo* other);
  private:
  inline ::google::protobuf::Arena* GetArenaNoVirtual() const {
    return _internal_metadata_.arena();
  }
  inline void* MaybeArenaPtr() const {
    return _internal_metadata_.raw_arena_ptr();
  }
  public:

  ::google::protobuf::Metadata GetMetadata() const;

  // nested types ----------------------------------------------------

  // accessors -------------------------------------------------------

  // optional uint64 id = 1;
  bool has_id() const;
  void clear_id();
  static const int kIdFieldNumber = 1;
  ::google::protobuf::uint64 id() const;
  void set_id(::google::protobuf::uint64 value);

  // optional string host = 2;
  bool has_host() const;
  void clear_host();
  static const int kHostFieldNumber = 2;
  const ::std::string& host() const;
  void set_host(const ::std::string& value);
  void set_host(const char* value);
  void set_host(const char* value, size_t size);
  ::std::string* mutable_host();
  ::std::string* release_host();
  void set_allocated_host(::std::string* host);

  // optional int32 port = 3;
  bool has_port() const;
  void clear_port();
  static const int kPortFieldNumber = 3;
  ::google::protobuf::int32 port() const;
  void set_port(::google::protobuf::int32 value);

  // @@protoc_insertion_point(class_scope:MachineInfo)
 private:
  inline void set_has_id();
  inline void clear_has_id();
  inline void set_has_host();
  inline void clear_has_host();
  inline void set_has_port();
  inline void clear_has_port();

  ::google::protobuf::internal::InternalMetadataWithArena _internal_metadata_;
  ::google::protobuf::uint32 _has_bits_[1];
  mutable int _cached_size_;
  ::google::protobuf::uint64 id_;
  ::google::protobuf::internal::ArenaStringPtr host_;
  ::google::protobuf::int32 port_;
  friend void  protobuf_AddDesc_cluster_5fconfig_2eproto();
  friend void protobuf_AssignDesc_cluster_5fconfig_2eproto();
  friend void protobuf_ShutdownFile_cluster_5fconfig_2eproto();

  void InitAsDefaultInstance();
  static MachineInfo* default_instance_;
};
// -------------------------------------------------------------------

class ClusterConfigProto : public ::google::protobuf::Message /* @@protoc_insertion_point(class_definition:ClusterConfigProto) */ {
 public:
  ClusterConfigProto();
  virtual ~ClusterConfigProto();

  ClusterConfigProto(const ClusterConfigProto& from);

  inline ClusterConfigProto& operator=(const ClusterConfigProto& from) {
    CopyFrom(from);
    return *this;
  }

  inline const ::google::protobuf::UnknownFieldSet& unknown_fields() const {
    return _internal_metadata_.unknown_fields();
  }

  inline ::google::protobuf::UnknownFieldSet* mutable_unknown_fields() {
    return _internal_metadata_.mutable_unknown_fields();
  }

  static const ::google::protobuf::Descriptor* descriptor();
  static const ClusterConfigProto& default_instance();

  void Swap(ClusterConfigProto* other);

  // implements Message ----------------------------------------------

  inline ClusterConfigProto* New() const { return New(NULL); }

  ClusterConfigProto* New(::google::protobuf::Arena* arena) const;
  void CopyFrom(const ::google::protobuf::Message& from);
  void MergeFrom(const ::google::protobuf::Message& from);
  void CopyFrom(const ClusterConfigProto& from);
  void MergeFrom(const ClusterConfigProto& from);
  void Clear();
  bool IsInitialized() const;

  int ByteSize() const;
  bool MergePartialFromCodedStream(
      ::google::protobuf::io::CodedInputStream* input);
  void SerializeWithCachedSizes(
      ::google::protobuf::io::CodedOutputStream* output) const;
  ::google::protobuf::uint8* InternalSerializeWithCachedSizesToArray(
      bool deterministic, ::google::protobuf::uint8* output) const;
  ::google::protobuf::uint8* SerializeWithCachedSizesToArray(::google::protobuf::uint8* output) const {
    return InternalSerializeWithCachedSizesToArray(false, output);
  }
  int GetCachedSize() const { return _cached_size_; }
  private:
  void SharedCtor();
  void SharedDtor();
  void SetCachedSize(int size) const;
  void InternalSwap(ClusterConfigProto* other);
  private:
  inline ::google::protobuf::Arena* GetArenaNoVirtual() const {
    return _internal_metadata_.arena();
  }
  inline void* MaybeArenaPtr() const {
    return _internal_metadata_.raw_arena_ptr();
  }
  public:

  ::google::protobuf::Metadata GetMetadata() const;

  // nested types ----------------------------------------------------

  // accessors -------------------------------------------------------

  // repeated .MachineInfo machines = 1;
  int machines_size() const;
  void clear_machines();
  static const int kMachinesFieldNumber = 1;
  const ::MachineInfo& machines(int index) const;
  ::MachineInfo* mutable_machines(int index);
  ::MachineInfo* add_machines();
  ::google::protobuf::RepeatedPtrField< ::MachineInfo >*
      mutable_machines();
  const ::google::protobuf::RepeatedPtrField< ::MachineInfo >&
      machines() const;

  // @@protoc_insertion_point(class_scope:ClusterConfigProto)
 private:

  ::google::protobuf::internal::InternalMetadataWithArena _internal_metadata_;
  ::google::protobuf::uint32 _has_bits_[1];
  mutable int _cached_size_;
  ::google::protobuf::RepeatedPtrField< ::MachineInfo > machines_;
  friend void  protobuf_AddDesc_cluster_5fconfig_2eproto();
  friend void protobuf_AssignDesc_cluster_5fconfig_2eproto();
  friend void protobuf_ShutdownFile_cluster_5fconfig_2eproto();

  void InitAsDefaultInstance();
  static ClusterConfigProto* default_instance_;
};
// ===================================================================


// ===================================================================

#if !PROTOBUF_INLINE_NOT_IN_HEADERS
// MachineInfo

// optional uint64 id = 1;
inline bool MachineInfo::has_id() const {
  return (_has_bits_[0] & 0x00000001u) != 0;
}
inline void MachineInfo::set_has_id() {
  _has_bits_[0] |= 0x00000001u;
}
inline void MachineInfo::clear_has_id() {
  _has_bits_[0] &= ~0x00000001u;
}
inline void MachineInfo::clear_id() {
  id_ = GOOGLE_ULONGLONG(0);
  clear_has_id();
}
inline ::google::protobuf::uint64 MachineInfo::id() const {
  // @@protoc_insertion_point(field_get:MachineInfo.id)
  return id_;
}
inline void MachineInfo::set_id(::google::protobuf::uint64 value) {
  set_has_id();
  id_ = value;
  // @@protoc_insertion_point(field_set:MachineInfo.id)
}

// optional string host = 2;
inline bool MachineInfo::has_host() const {
  return (_has_bits_[0] & 0x00000002u) != 0;
}
inline void MachineInfo::set_has_host() {
  _has_bits_[0] |= 0x00000002u;
}
inline void MachineInfo::clear_has_host() {
  _has_bits_[0] &= ~0x00000002u;
}
inline void MachineInfo::clear_host() {
  host_.ClearToEmptyNoArena(&::google::protobuf::internal::GetEmptyStringAlreadyInited());
  clear_has_host();
}
inline const ::std::string& MachineInfo::host() const {
  // @@protoc_insertion_point(field_get:MachineInfo.host)
  return host_.GetNoArena(&::google::protobuf::internal::GetEmptyStringAlreadyInited());
}
inline void MachineInfo::set_host(const ::std::string& value) {
  set_has_host();
  host_.SetNoArena(&::google::protobuf::internal::GetEmptyStringAlreadyInited(), value);
  // @@protoc_insertion_point(field_set:MachineInfo.host)
}
inline void MachineInfo::set_host(const char* value) {
  set_has_host();
  host_.SetNoArena(&::google::protobuf::internal::GetEmptyStringAlreadyInited(), ::std::string(value));
  // @@protoc_insertion_point(field_set_char:MachineInfo.host)
}
inline void MachineInfo::set_host(const char* value, size_t size) {
  set_has_host();
  host_.SetNoArena(&::google::protobuf::internal::GetEmptyStringAlreadyInited(),
      ::std::string(reinterpret_cast<const char*>(value), size));
  // @@protoc_insertion_point(field_set_pointer:MachineInfo.host)
}
inline ::std::string* MachineInfo::mutable_host() {
  set_has_host();
  // @@protoc_insertion_point(field_mutable:MachineInfo.host)
  return host_.MutableNoArena(&::google::protobuf::internal::GetEmptyStringAlreadyInited());
}
inline ::std::string* MachineInfo::release_host() {
  // @@protoc_insertion_point(field_release:MachineInfo.host)
  clear_has_host();
  return host_.ReleaseNoArena(&::google::protobuf::internal::GetEmptyStringAlreadyInited());
}
inline void MachineInfo::set_allocated_host(::std::string* host) {
  if (host != NULL) {
    set_has_host();
  } else {
    clear_has_host();
  }
  host_.SetAllocatedNoArena(&::google::protobuf::internal::GetEmptyStringAlreadyInited(), host);
  // @@protoc_insertion_point(field_set_allocated:MachineInfo.host)
}

// optional int32 port = 3;
inline bool MachineInfo::has_port() const {
  return (_has_bits_[0] & 0x00000004u) != 0;
}
inline void MachineInfo::set_has_port() {
  _has_bits_[0] |= 0x00000004u;
}
inline void MachineInfo::clear_has_port() {
  _has_bits_[0] &= ~0x00000004u;
}
inline void MachineInfo::clear_port() {
  port_ = 0;
  clear_has_port();
}
inline ::google::protobuf::int32 MachineInfo::port() const {
  // @@protoc_insertion_point(field_get:MachineInfo.port)
  return port_;
}
inline void MachineInfo::set_port(::google::protobuf::int32 value) {
  set_has_port();
  port_ = value;
  // @@protoc_insertion_point(field_set:MachineInfo.port)
}

// -------------------------------------------------------------------

// ClusterConfigProto

// repeated .MachineInfo machines = 1;
inline int ClusterConfigProto::machines_size() const {
  return machines_.size();
}
inline void ClusterConfigProto::clear_machines() {
  machines_.Clear();
}
inline const ::MachineInfo& ClusterConfigProto::machines(int index) const {
  // @@protoc_insertion_point(field_get:ClusterConfigProto.machines)
  return machines_.Get(index);
}
inline ::MachineInfo* ClusterConfigProto::mutable_machines(int index) {
  // @@protoc_insertion_point(field_mutable:ClusterConfigProto.machines)
  return machines_.Mutable(index);
}
inline ::MachineInfo* ClusterConfigProto::add_machines() {
  // @@protoc_insertion_point(field_add:ClusterConfigProto.machines)
  return machines_.Add();
}
inline ::google::protobuf::RepeatedPtrField< ::MachineInfo >*
ClusterConfigProto::mutable_machines() {
  // @@protoc_insertion_point(field_mutable_list:ClusterConfigProto.machines)
  return &machines_;
}
inline const ::google::protobuf::RepeatedPtrField< ::MachineInfo >&
ClusterConfigProto::machines() const {
  // @@protoc_insertion_point(field_list:ClusterConfigProto.machines)
  return machines_;
}

#endif  // !PROTOBUF_INLINE_NOT_IN_HEADERS
// -------------------------------------------------------------------


// @@protoc_insertion_point(namespace_scope)

// @@protoc_insertion_point(global_scope)

#endif  // PROTOBUF_cluster_5fconfig_2eproto__INCLUDED
