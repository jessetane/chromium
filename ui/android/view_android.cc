// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/android/view_android.h"

#include <algorithm>

#include "base/android/jni_android.h"
#include "cc/layers/layer.h"
#include "ui/android/window_android.h"


namespace ui {

using base::android::JavaRef;

ViewAndroid::ViewAndroid(const JavaRef<jobject>& delegate)
    : parent_(nullptr), delegate_(delegate) {}

ViewAndroid::ViewAndroid() : parent_(nullptr) {}

ViewAndroid::~ViewAndroid() {
  RemoveFromParent();

  for (std::list<ViewAndroid*>::iterator it = children_.begin();
       it != children_.end(); it++) {
    DCHECK_EQ((*it)->parent_, this);
    (*it)->parent_ = nullptr;
  }
}

void ViewAndroid::AddChild(ViewAndroid* child) {
  DCHECK(child);
  DCHECK(std::find(children_.begin(), children_.end(), child) ==
         children_.end());

  children_.push_back(child);
  if (child->parent_)
    child->RemoveFromParent();
  child->parent_ = this;
}

void ViewAndroid::RemoveFromParent() {
  if (parent_)
    parent_->RemoveChild(this);
}

void ViewAndroid::RemoveChild(ViewAndroid* child) {
  DCHECK(child);
  DCHECK_EQ(child->parent_, this);

  std::list<ViewAndroid*>::iterator it =
      std::find(children_.begin(), children_.end(), child);
  DCHECK(it != children_.end());
  children_.erase(it);
  child->parent_ = nullptr;
}

WindowAndroid* ViewAndroid::GetWindowAndroid() const {
  return parent_ ? parent_->GetWindowAndroid() : nullptr;
}

const JavaRef<jobject>& ViewAndroid::GetViewAndroidDelegate()
    const {
  if (!delegate_.is_null())
    return delegate_;

  return parent_ ? parent_->GetViewAndroidDelegate() : delegate_;
}

cc::Layer* ViewAndroid::GetLayer() const {
  return layer_.get();
}

void ViewAndroid::SetLayer(scoped_refptr<cc::Layer> layer) {
  layer_ = layer;
}

void ViewAndroid::StartDragAndDrop(const JavaRef<jstring>& jtext,
                                   const JavaRef<jobject>& jimage) {
  WindowAndroid* window_android = GetWindowAndroid();
  if (!window_android)
    return;

  window_android->StartDragAndDrop(GetViewAndroidDelegate(), jtext, jimage);
}

}  // namespace ui
