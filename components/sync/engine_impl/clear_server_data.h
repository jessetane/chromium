// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SYNC_ENGINE_IMPL_CLEAR_SERVER_DATA_H_
#define COMPONENTS_SYNC_ENGINE_IMPL_CLEAR_SERVER_DATA_H_

#include <string>

#include "components/sync/base/sync_export.h"
#include "components/sync/base/syncer_error.h"
#include "components/sync/protocol/sync.pb.h"

namespace syncer {

namespace sessions {
class SyncSession;
}

// A ClearServerData operation.
//
// An instance of this class corresponds to a single operation and is
// responsible for building a request, sending it, and interpreting the
// response.
class SYNC_EXPORT ClearServerData {
 public:
  explicit ClearServerData(const std::string& account_name);
  ~ClearServerData();

  // Sends the request, blocking until the request has completed.
  SyncerError SendRequest(sessions::SyncSession* session);

 private:
  sync_pb::ClientToServerMessage request_;
};

}  // namespace syncer

#endif  // COMPONENTS_SYNC_ENGINE_IMPL_CLEAR_SERVER_DATA_H_
