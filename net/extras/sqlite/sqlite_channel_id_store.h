// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_EXTRAS_SQLITE_SQLITE_CHANNEL_ID_STORE_H_
#define NET_EXTRAS_SQLITE_SQLITE_CHANNEL_ID_STORE_H_

#include <list>
#include <string>

#include "base/callback_forward.h"
#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "net/ssl/default_channel_id_store.h"

namespace base {
class FilePath;
class SequencedTaskRunner;
}

namespace net {

// Implements the DefaultChannelIDStore::PersistentStore interface
// in terms of a SQLite database. For documentation about the actual member
// functions consult the documentation of the parent class
// DefaultChannelIDStore::PersistentCertStore.
class SQLiteChannelIDStore : public DefaultChannelIDStore::PersistentStore {
 public:
  // Create or open persistent store in file |path|. All I/O tasks are performed
  // in background using |background_task_runner|.
  SQLiteChannelIDStore(
      const base::FilePath& path,
      const scoped_refptr<base::SequencedTaskRunner>& background_task_runner);

  // DefaultChannelIDStore::PersistentStore:
  void Load(const LoadedCallback& loaded_callback) override;
  void AddChannelID(
      const DefaultChannelIDStore::ChannelID& channel_id) override;
  void DeleteChannelID(
      const DefaultChannelIDStore::ChannelID& channel_id) override;
  void SetForceKeepSessionState() override;
  void Flush() override;

  // Delete channel ids from servers in |server_identifiers|.
  void DeleteAllInList(const std::list<std::string>& server_identifiers);

 private:
  ~SQLiteChannelIDStore() override;

  class Backend;

  scoped_refptr<Backend> backend_;

  DISALLOW_COPY_AND_ASSIGN(SQLiteChannelIDStore);
};

}  // namespace net

#endif  // NET_EXTRAS_SQLITE_SQLITE_CHANNEL_ID_STORE_H_
