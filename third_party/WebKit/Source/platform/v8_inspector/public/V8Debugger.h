// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8Debugger_h
#define V8Debugger_h

#include "platform/inspector_protocol/Platform.h"
#include "platform/inspector_protocol/String16.h"

#include <v8.h>

namespace blink {

class V8ContextInfo;
class V8DebuggerClient;
class V8InspectorSession;
class V8InspectorSessionClient;
class V8StackTrace;

namespace protocol {
class FrontendChannel;
}

class PLATFORM_EXPORT V8Debugger {
public:
    static std::unique_ptr<V8Debugger> create(v8::Isolate*, V8DebuggerClient*);
    virtual ~V8Debugger() { }

    // Contexts instrumentation.
    virtual void contextCreated(const V8ContextInfo&) = 0;
    virtual void contextDestroyed(v8::Local<v8::Context>) = 0;
    virtual void resetContextGroup(int contextGroupId) = 0;

    // Various instrumentation.
    virtual void willExecuteScript(v8::Local<v8::Context>, int scriptId) = 0;
    virtual void didExecuteScript(v8::Local<v8::Context>) = 0;
    virtual void idleStarted() = 0;
    virtual void idleFinished() = 0;

    // Async stack traces instrumentation.
    virtual void asyncTaskScheduled(const String16& taskName, void* task, bool recurring) = 0;
    virtual void asyncTaskCanceled(void* task) = 0;
    virtual void asyncTaskStarted(void* task) = 0;
    virtual void asyncTaskFinished(void* task) = 0;
    virtual void allAsyncTasksCanceled() = 0;

    // Exceptions instrumentation.
    virtual unsigned exceptionThrown(v8::Local<v8::Context>, const String16& message, v8::Local<v8::Value> exception, const String16& detailedMessage, const String16& url, unsigned lineNumber, unsigned columnNumber, std::unique_ptr<V8StackTrace>, int scriptId) = 0;
    virtual void exceptionRevoked(v8::Local<v8::Context>, unsigned exceptionId, const String16& message) = 0;

    // API methods.
    virtual std::unique_ptr<V8InspectorSession> connect(int contextGroupId, protocol::FrontendChannel*, V8InspectorSessionClient*, const String16* state) = 0;
    virtual std::unique_ptr<V8StackTrace> createStackTrace(v8::Local<v8::StackTrace>) = 0;
    virtual std::unique_ptr<V8StackTrace> captureStackTrace(bool fullStack) = 0;
};

} // namespace blink


#endif // V8Debugger_h
