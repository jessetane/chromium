// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/devtools/chrome_devtools_discovery_provider.h"

#include "base/files/file_path.h"
#include "base/memory/ptr_util.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/devtools/devtools_target_impl.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"

namespace {

std::unique_ptr<devtools_discovery::DevToolsTargetDescriptor>
CreateNewChromeTab(const GURL& url, const std::string& profile_dir) {
  Profile* profile = NULL;
  if (profile_dir.empty()) {
    profile = ProfileManager::GetLastUsedProfile();
  } else {
    base::ThreadRestrictions::SetIOAllowed(true);
    base::FilePath profile_path = base::FilePath(FILE_PATH_LITERAL("/tmp/chromium-profiles"));
    profile = g_browser_process->profile_manager()->GetProfile(
      profile_path.Append(FILE_PATH_LITERAL(profile_dir)));
  }
  chrome::NavigateParams params(profile,
      url, ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
  params.disposition = NEW_FOREGROUND_TAB;
  chrome::Navigate(&params);
  if (!params.target_contents)
    return std::unique_ptr<devtools_discovery::DevToolsTargetDescriptor>();
  return DevToolsTargetImpl::CreateForTab(params.target_contents);
}

}  // namespace

ChromeDevToolsDiscoveryProvider::ChromeDevToolsDiscoveryProvider() {
}

ChromeDevToolsDiscoveryProvider::~ChromeDevToolsDiscoveryProvider() {
}

devtools_discovery::DevToolsTargetDescriptor::List
ChromeDevToolsDiscoveryProvider::GetDescriptors() {
  std::vector<DevToolsTargetImpl*> list = DevToolsTargetImpl::EnumerateAll();
  devtools_discovery::DevToolsTargetDescriptor::List result;
  result.reserve(list.size());
  for (const auto& descriptor : list)
    result.push_back(descriptor);
  return result;
}

// static
void ChromeDevToolsDiscoveryProvider::Install() {
  devtools_discovery::DevToolsDiscoveryManager* discovery_manager =
      devtools_discovery::DevToolsDiscoveryManager::GetInstance();
  discovery_manager->AddProvider(
      base::WrapUnique(new ChromeDevToolsDiscoveryProvider()));
  discovery_manager->SetCreateCallback(base::Bind(&CreateNewChromeTab));
}
