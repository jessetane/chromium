// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/mojo/services/mojo_media_application.h"

#include <utility>

#include "media/base/media_log.h"
#include "media/mojo/services/mojo_media_client.h"
#include "media/mojo/services/service_factory_impl.h"
#include "services/shell/public/cpp/connection.h"
#include "services/shell/public/cpp/connector.h"

namespace media {

// TODO(xhwang): Hook up MediaLog when possible.
MojoMediaApplication::MojoMediaApplication(
    std::unique_ptr<MojoMediaClient> mojo_media_client,
    const base::Closure& quit_closure)
    : mojo_media_client_(std::move(mojo_media_client)),
      media_log_(new MediaLog()),
      ref_factory_(quit_closure) {
  DCHECK(mojo_media_client_);
}

MojoMediaApplication::~MojoMediaApplication() {}

void MojoMediaApplication::OnStart(const shell::Identity& identity) {
  mojo_media_client_->Initialize();
}

bool MojoMediaApplication::OnConnect(shell::Connection* connection) {
  connection->AddInterface<mojom::ServiceFactory>(this);
  remote_interface_provider_ = connection->GetRemoteInterfaceProvider();
  return true;
}

bool MojoMediaApplication::OnStop() {
  mojo_media_client_->WillQuit();
  return true;
}

void MojoMediaApplication::Create(
    const shell::Identity& remote_identity,
    mojo::InterfaceRequest<mojom::ServiceFactory> request) {
  // The created object is owned by the pipe.
  new ServiceFactoryImpl(std::move(request), remote_interface_provider_,
                         media_log_, ref_factory_.CreateRef(),
                         mojo_media_client_.get());
}

}  // namespace media