// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_GN_LOADER_H_
#define TOOLS_GN_LOADER_H_

#include <map>
#include <memory>
#include <set>

#include "base/callback.h"
#include "base/memory/ref_counted.h"
#include "tools/gn/label.h"
#include "tools/gn/scope.h"
#include "util/msg_loop.h"

class BuildSettings;
class LocationRange;
class Settings;
class SourceFile;
class Toolchain;

// The loader manages execution of the different build files. It receives
// requests (normally from the Builder) when new references are found, and also
// manages loading the build config files.
//
// This loader class is abstract so it can be mocked out for testing the
// Builder.
class Loader : public base::RefCountedThreadSafe<Loader> {
 public:
  Loader();

  // Loads the given file in the conext of the given toolchain. The initial
  // call to this (the one that actually starts the generation) should have an
  // empty toolchain name, which will trigger the load of the default build
  // config.
  virtual void Load(const SourceFile& file,
                    const LocationRange& origin,
                    const Label& toolchain_name) = 0;

  // Notification that the given toolchain has loaded. This will unblock files
  // waiting on this definition.
  virtual void ToolchainLoaded(const Toolchain* toolchain) = 0;

  // Returns the label of the default toolchain.
  virtual Label GetDefaultToolchain() const = 0;

  // Returns information about the toolchain with the given label. Will return
  // false if we haven't processed this toolchain yet.
  virtual const Settings* GetToolchainSettings(const Label& label) const = 0;

  // Helper function that extracts the file and toolchain name from the given
  // label, and calls Load().
  void Load(const Label& label, const LocationRange& origin);

  // Returns the build file that the given label references.
  static SourceFile BuildFileForLabel(const Label& label);

  // When processing the default build config, we want to capture the argument
  // of set_default_build_config. The implementation of that function uses this
  // constant as a property key to get the Label* out of the scope where the
  // label should be stored.
  static const void* const kDefaultToolchainKey;

 protected:
  friend class base::RefCountedThreadSafe<Loader>;
  virtual ~Loader();
};

class LoaderImpl : public Loader {
 public:
  // Callback to emulate InputFileManager::AsyncLoadFile.
  typedef base::Callback<bool(const LocationRange&,
                              const BuildSettings*,
                              const SourceFile&,
                              const base::Callback<void(const ParseNode*)>&,
                              Err*)>
      AsyncLoadFileCallback;

  explicit LoaderImpl(const BuildSettings* build_settings);

  // Loader implementation.
  void Load(const SourceFile& file,
            const LocationRange& origin,
            const Label& toolchain_name) override;
  void ToolchainLoaded(const Toolchain* toolchain) override;
  Label GetDefaultToolchain() const override;
  const Settings* GetToolchainSettings(const Label& label) const override;

  // Sets the task runner corresponding to the main thread. By default this
  // class will use the thread active during construction, but there is not
  // a task runner active during construction all the time.
  void set_task_runner(MsgLoop* task_runner) { task_runner_ = task_runner; }

  // The complete callback is called whenever there are no more pending loads.
  // Called on the main thread only. This may be called more than once if the
  // queue is drained, but then more stuff gets added.
  void set_complete_callback(const base::Closure& cb) {
    complete_callback_ = cb;
  }

  // This callback is used when the loader finds it wants to load a file.
  void set_async_load_file(const AsyncLoadFileCallback& cb) {
    async_load_file_ = cb;
  }

  const Label& default_toolchain_label() const {
    return default_toolchain_label_;
  }

 private:
  struct LoadID;
  struct ToolchainRecord;

  ~LoaderImpl() override;

  // Schedules the input file manager to load the given file.
  void ScheduleLoadFile(const Settings* settings,
                        const LocationRange& origin,
                        const SourceFile& file);
  void ScheduleLoadBuildConfig(Settings* settings,
                               const Scope::KeyValueMap& toolchain_overrides);

  // Runs the given file on the background thread. These are called by the
  // input file manager.
  void BackgroundLoadFile(const Settings* settings,
                          const SourceFile& file_name,
                          const LocationRange& origin,
                          const ParseNode* root);
  void BackgroundLoadBuildConfig(Settings* settings,
                                 const Scope::KeyValueMap& toolchain_overrides,
                                 const ParseNode* root);

  // Posted to the main thread when any file other than a build config file
  // file has completed running.
  void DidLoadFile();

  // Posted to the main thread when any build config file has completed
  // running. The label should be the name of the toolchain.
  //
  // If there is no defauled toolchain loaded yet, we'll assume that the first
  // call to this indicates to the default toolchain, and this function will
  // set the default toolchain name to the given label.
  void DidLoadBuildConfig(const Label& label);

  // Decrements the pending_loads_ variable and issues the complete callback if
  // necessary.
  void DecrementPendingLoads();

  // Forwards to the appropriate location to load the file.
  bool AsyncLoadFile(const LocationRange& origin,
                     const BuildSettings* build_settings,
                     const SourceFile& file_name,
                     const base::Callback<void(const ParseNode*)>& callback,
                     Err* err);

  MsgLoop* task_runner_;

  int pending_loads_;
  base::Closure complete_callback_;

  // When non-null, use this callback instead of the InputFileManager for
  // mocking purposes.
  AsyncLoadFileCallback async_load_file_;

  typedef std::set<LoadID> LoadIDSet;
  LoadIDSet invocations_;

  const BuildSettings* build_settings_;
  Label default_toolchain_label_;

  // Records for the build config file loads.
  typedef std::map<Label, std::unique_ptr<ToolchainRecord>> ToolchainRecordMap;
  ToolchainRecordMap toolchain_records_;
};

#endif  // TOOLS_GN_LOADER_H_
