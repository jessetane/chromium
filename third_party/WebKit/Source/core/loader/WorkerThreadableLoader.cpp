/*
 * Copyright (C) 2009, 2010 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/loader/WorkerThreadableLoader.h"

#include "core/dom/CrossThreadTask.h"
#include "core/dom/Document.h"
#include "core/loader/DocumentThreadableLoader.h"
#include "core/timing/WorkerGlobalScopePerformance.h"
#include "core/workers/WorkerGlobalScope.h"
#include "core/workers/WorkerLoaderProxy.h"
#include "core/workers/WorkerThread.h"
#include "platform/CrossThreadFunctional.h"
#include "platform/WaitableEvent.h"
#include "platform/heap/SafePoint.h"
#include "platform/network/ResourceError.h"
#include "platform/network/ResourceRequest.h"
#include "platform/network/ResourceResponse.h"
#include "platform/network/ResourceTimingInfo.h"
#include "platform/weborigin/SecurityPolicy.h"
#include "public/platform/Platform.h"
#include "wtf/PtrUtil.h"
#include "wtf/Vector.h"
#include "wtf/debug/Alias.h"
#include <memory>

namespace blink {

static std::unique_ptr<Vector<char>> createVectorFromMemoryRegion(const char* data, unsigned dataLength)
{
    std::unique_ptr<Vector<char>> buffer = wrapUnique(new Vector<char>(dataLength));
    memcpy(buffer->data(), data, dataLength);
    return buffer;
}

WorkerThreadableLoader::WorkerThreadableLoader(WorkerGlobalScope& workerGlobalScope, ThreadableLoaderClient* client, const ThreadableLoaderOptions& options, const ResourceLoaderOptions& resourceLoaderOptions, BlockingBehavior blockingBehavior)
    : m_workerGlobalScope(&workerGlobalScope)
    , m_workerClientWrapper(new ThreadableLoaderClientWrapper(workerGlobalScope, client))
{
    if (blockingBehavior == LoadAsynchronously) {
        m_bridge = new MainThreadAsyncBridge(workerGlobalScope, m_workerClientWrapper, options, resourceLoaderOptions);
    } else {
        m_bridge = new MainThreadSyncBridge(workerGlobalScope, m_workerClientWrapper, options, resourceLoaderOptions);
    }
}

void WorkerThreadableLoader::loadResourceSynchronously(WorkerGlobalScope& workerGlobalScope, const ResourceRequest& request, ThreadableLoaderClient& client, const ThreadableLoaderOptions& options, const ResourceLoaderOptions& resourceLoaderOptions)
{
    std::unique_ptr<WorkerThreadableLoader> loader = wrapUnique(new WorkerThreadableLoader(workerGlobalScope, &client, options, resourceLoaderOptions, LoadSynchronously));
    loader->start(request);
}

WorkerThreadableLoader::~WorkerThreadableLoader()
{
    DCHECK(m_workerClientWrapper->done());
    m_bridge->destroy();
    m_bridge = nullptr;
}

void WorkerThreadableLoader::start(const ResourceRequest& request)
{
    ResourceRequest requestToPass(request);
    if (!requestToPass.didSetHTTPReferrer())
        requestToPass.setHTTPReferrer(SecurityPolicy::generateReferrer(m_workerGlobalScope->getReferrerPolicy(), request.url(), m_workerGlobalScope->outgoingReferrer()));
    m_bridge->start(requestToPass, *m_workerGlobalScope);
}

void WorkerThreadableLoader::overrideTimeout(unsigned long timeoutMilliseconds)
{
    ASSERT(m_bridge);
    m_bridge->overrideTimeout(timeoutMilliseconds);
}

void WorkerThreadableLoader::cancel()
{
    ASSERT(m_bridge);
    m_bridge->cancel();
}

WorkerThreadableLoader::MainThreadBridgeBase::MainThreadBridgeBase(
    ThreadableLoaderClientWrapper* workerClientWrapper,
    PassRefPtr<WorkerLoaderProxy> loaderProxy)
    : m_workerClientWrapper(workerClientWrapper)
    , m_loaderProxy(loaderProxy)
{
    ASSERT(m_workerClientWrapper.get());
    ASSERT(m_loaderProxy.get());
}

WorkerThreadableLoader::MainThreadBridgeBase::~MainThreadBridgeBase()
{
}

void WorkerThreadableLoader::MainThreadBridgeBase::mainThreadCreateLoader(ThreadableLoaderOptions options, ResourceLoaderOptions resourceLoaderOptions, ExecutionContext* context)
{
    ASSERT(isMainThread());
    Document* document = toDocument(context);

    resourceLoaderOptions.requestInitiatorContext = WorkerContext;
    m_mainThreadLoader = DocumentThreadableLoader::create(*document, this, options, resourceLoaderOptions);
    ASSERT(m_mainThreadLoader);
}

void WorkerThreadableLoader::MainThreadBridgeBase::mainThreadStart(std::unique_ptr<CrossThreadResourceRequestData> requestData)
{
    ASSERT(isMainThread());
    ASSERT(m_mainThreadLoader);
    m_mainThreadLoader->start(ResourceRequest(requestData.get()));
}

void WorkerThreadableLoader::MainThreadBridgeBase::createLoaderInMainThread(const ThreadableLoaderOptions& options, const ResourceLoaderOptions& resourceLoaderOptions)
{
    m_loaderProxy->postTaskToLoader(BLINK_FROM_HERE, createCrossThreadTask(&MainThreadBridgeBase::mainThreadCreateLoader, crossThreadUnretained(this), options, resourceLoaderOptions));
}

void WorkerThreadableLoader::MainThreadBridgeBase::startInMainThread(const ResourceRequest& request, const WorkerGlobalScope& workerGlobalScope)
{
    loaderProxy()->postTaskToLoader(BLINK_FROM_HERE, createCrossThreadTask(&MainThreadBridgeBase::mainThreadStart, crossThreadUnretained(this), request));
}

void WorkerThreadableLoader::MainThreadBridgeBase::mainThreadDestroy(ExecutionContext* context)
{
    ASSERT(isMainThread());
    ASSERT_UNUSED(context, context->isDocument());
    delete this;
}

void WorkerThreadableLoader::MainThreadBridgeBase::destroy()
{
    // Ensure that no more client callbacks are done in the worker context's
    // thread.
    // ThreadableLoaderClientWrapper is an on-heap class and this function can
    // be called in the finalization step but it is safe because
    // m_workerClientWrapper is a CrossThreadPersistent.
    m_workerClientWrapper->clearClient();

    // "delete this" and m_mainThreadLoader::deref() on the worker object's
    // thread.
    m_loaderProxy->postTaskToLoader(BLINK_FROM_HERE, createCrossThreadTask(&MainThreadBridgeBase::mainThreadDestroy, crossThreadUnretained(this)));
}

void WorkerThreadableLoader::MainThreadBridgeBase::mainThreadOverrideTimeout(unsigned long timeoutMilliseconds, ExecutionContext* context)
{
    ASSERT(isMainThread());
    ASSERT_UNUSED(context, context->isDocument());

    if (!m_mainThreadLoader)
        return;
    m_mainThreadLoader->overrideTimeout(timeoutMilliseconds);
}

void WorkerThreadableLoader::MainThreadBridgeBase::overrideTimeout(unsigned long timeoutMilliseconds)
{
    m_loaderProxy->postTaskToLoader(BLINK_FROM_HERE, createCrossThreadTask(&MainThreadBridgeBase::mainThreadOverrideTimeout, crossThreadUnretained(this), timeoutMilliseconds));
}

void WorkerThreadableLoader::MainThreadBridgeBase::mainThreadCancel(ExecutionContext* context)
{
    ASSERT(isMainThread());
    ASSERT_UNUSED(context, context->isDocument());

    if (!m_mainThreadLoader)
        return;
    m_mainThreadLoader->cancel();
    m_mainThreadLoader = nullptr;
}

void WorkerThreadableLoader::MainThreadBridgeBase::cancel()
{
    m_loaderProxy->postTaskToLoader(BLINK_FROM_HERE, createCrossThreadTask(&MainThreadBridgeBase::mainThreadCancel, crossThreadUnretained(this)));
    ThreadableLoaderClientWrapper* clientWrapper = m_workerClientWrapper;
    if (!clientWrapper->done()) {
        // If the client hasn't reached a termination state, then transition it
        // by sending a cancellation error.
        // Note: no more client callbacks will be done after this method -- the
        // m_workerClientWrapper->clearClient() call ensures that.
        ResourceError error(String(), 0, String(), String());
        error.setIsCancellation(true);
        clientWrapper->didFail(error);
    }
    // |this| might be already destructed here because didFail() might
    // clear a reference to ThreadableLoader, which might destruct
    // WorkerThreadableLoader and then MainThreadBridge.
    // Therefore we call clearClient() directly, rather than calling
    // this->m_workerClientWrapper->clearClient().
    clientWrapper->clearClient();
}

void WorkerThreadableLoader::MainThreadBridgeBase::didSendData(unsigned long long bytesSent, unsigned long long totalBytesToBeSent)
{
    forwardTaskToWorker(BLINK_FROM_HERE, createCrossThreadTask(&ThreadableLoaderClientWrapper::didSendData, m_workerClientWrapper, bytesSent, totalBytesToBeSent));
}

void WorkerThreadableLoader::MainThreadBridgeBase::didReceiveResponse(unsigned long identifier, const ResourceResponse& response, std::unique_ptr<WebDataConsumerHandle> handle)
{
    forwardTaskToWorker(BLINK_FROM_HERE, createCrossThreadTask(&ThreadableLoaderClientWrapper::didReceiveResponse, m_workerClientWrapper, identifier, response, passed(std::move(handle))));
}

void WorkerThreadableLoader::MainThreadBridgeBase::didReceiveData(const char* data, unsigned dataLength)
{
    forwardTaskToWorker(BLINK_FROM_HERE, createCrossThreadTask(&ThreadableLoaderClientWrapper::didReceiveData, m_workerClientWrapper, passed(createVectorFromMemoryRegion(data, dataLength))));
}

void WorkerThreadableLoader::MainThreadBridgeBase::didDownloadData(int dataLength)
{
    forwardTaskToWorker(BLINK_FROM_HERE, createCrossThreadTask(&ThreadableLoaderClientWrapper::didDownloadData, m_workerClientWrapper, dataLength));
}

void WorkerThreadableLoader::MainThreadBridgeBase::didReceiveCachedMetadata(const char* data, int dataLength)
{
    forwardTaskToWorker(BLINK_FROM_HERE, createCrossThreadTask(&ThreadableLoaderClientWrapper::didReceiveCachedMetadata, m_workerClientWrapper, passed(createVectorFromMemoryRegion(data, dataLength))));
}

void WorkerThreadableLoader::MainThreadBridgeBase::didFinishLoading(unsigned long identifier, double finishTime)
{
    forwardTaskToWorkerOnLoaderDone(BLINK_FROM_HERE, createCrossThreadTask(&ThreadableLoaderClientWrapper::didFinishLoading, m_workerClientWrapper, identifier, finishTime));
}

void WorkerThreadableLoader::MainThreadBridgeBase::didFail(const ResourceError& error)
{
    forwardTaskToWorkerOnLoaderDone(BLINK_FROM_HERE, createCrossThreadTask(&ThreadableLoaderClientWrapper::didFail, m_workerClientWrapper, error));
}

void WorkerThreadableLoader::MainThreadBridgeBase::didFailAccessControlCheck(const ResourceError& error)
{
    forwardTaskToWorkerOnLoaderDone(BLINK_FROM_HERE, createCrossThreadTask(&ThreadableLoaderClientWrapper::didFailAccessControlCheck, m_workerClientWrapper, error));
}

void WorkerThreadableLoader::MainThreadBridgeBase::didFailRedirectCheck()
{
    forwardTaskToWorkerOnLoaderDone(BLINK_FROM_HERE, createCrossThreadTask(&ThreadableLoaderClientWrapper::didFailRedirectCheck, m_workerClientWrapper));
}

void WorkerThreadableLoader::MainThreadBridgeBase::didReceiveResourceTiming(const ResourceTimingInfo& info)
{
    forwardTaskToWorker(BLINK_FROM_HERE, createCrossThreadTask(&ThreadableLoaderClientWrapper::didReceiveResourceTiming, m_workerClientWrapper, info));
}

WorkerThreadableLoader::MainThreadAsyncBridge::MainThreadAsyncBridge(
    WorkerGlobalScope& workerGlobalScope,
    ThreadableLoaderClientWrapper* workerClientWrapper,
    const ThreadableLoaderOptions& options,
    const ResourceLoaderOptions& resourceLoaderOptions)
    : MainThreadBridgeBase(workerClientWrapper, workerGlobalScope.thread()->workerLoaderProxy())
{
    createLoaderInMainThread(options, resourceLoaderOptions);
}

void WorkerThreadableLoader::MainThreadAsyncBridge::start(const ResourceRequest& request, const WorkerGlobalScope& workerGlobalScope)
{
    startInMainThread(request, workerGlobalScope);
}

WorkerThreadableLoader::MainThreadAsyncBridge::~MainThreadAsyncBridge()
{
}

void WorkerThreadableLoader::MainThreadAsyncBridge::forwardTaskToWorker(const WebTraceLocation& location, std::unique_ptr<ExecutionContextTask> task)
{
    loaderProxy()->postTaskToWorkerGlobalScope(location, std::move(task));
}

void WorkerThreadableLoader::MainThreadAsyncBridge::forwardTaskToWorkerOnLoaderDone(const WebTraceLocation& location, std::unique_ptr<ExecutionContextTask> task)
{
    loaderProxy()->postTaskToWorkerGlobalScope(location, std::move(task));
}

WorkerThreadableLoader::MainThreadSyncBridge::MainThreadSyncBridge(
    WorkerGlobalScope& workerGlobalScope,
    ThreadableLoaderClientWrapper* workerClientWrapper,
    const ThreadableLoaderOptions& options,
    const ResourceLoaderOptions& resourceLoaderOptions)
    : MainThreadBridgeBase(workerClientWrapper, workerGlobalScope.thread()->workerLoaderProxy())
    , m_done(false)
{
    createLoaderInMainThread(options, resourceLoaderOptions);
}

void WorkerThreadableLoader::MainThreadSyncBridge::start(const ResourceRequest& request, const WorkerGlobalScope& workerGlobalScope)
{
    WaitableEvent* terminationEvent = workerGlobalScope.thread()->terminationEvent();
    m_loaderDoneEvent = wrapUnique(new WaitableEvent());

    startInMainThread(request, workerGlobalScope);

    size_t signaledIndex;
    {
        Vector<WaitableEvent*> events;
        // Order is important; indicies are used later.
        events.append(terminationEvent);
        events.append(m_loaderDoneEvent.get());

        SafePointScope scope(BlinkGC::HeapPointersOnStack);
        signaledIndex = WaitableEvent::waitMultiple(events);
    }
    // |signaledIndex| is 0; which is terminationEvent.
    if (signaledIndex == 0) {
        cancel();
        return;
    }

    // The following code must be run only after |m_loaderDoneEvent| is
    // signalled.

    Vector<ClientTask> tasks;
    {
        MutexLocker lock(m_lock);
        ASSERT(m_done);
        m_clientTasks.swap(tasks);
    }
    for (const auto& task : tasks) {
        // Store the program counter where the task is posted from, and alias
        // it to ensure it is stored in the crash dump.
        const void* programCounter = task.m_location.program_counter();
        WTF::debug::alias(&programCounter);

        // m_clientTask contains only CallClosureTasks. So, it's ok to pass
        // the nullptr.
        task.m_task->performTask(nullptr);
    }
}

WorkerThreadableLoader::MainThreadSyncBridge::ClientTask::ClientTask(const WebTraceLocation& location, std::unique_ptr<ExecutionContextTask> task)
    : m_location(location)
    , m_task(std::move(task)) {}

WorkerThreadableLoader::MainThreadSyncBridge::ClientTask::~ClientTask() = default;
WorkerThreadableLoader::MainThreadSyncBridge::ClientTask::ClientTask(ClientTask&&) = default;

WorkerThreadableLoader::MainThreadSyncBridge::~MainThreadSyncBridge()
{
    ASSERT(isMainThread());
}

void WorkerThreadableLoader::MainThreadSyncBridge::forwardTaskToWorker(const WebTraceLocation& location, std::unique_ptr<ExecutionContextTask> task)
{
    ASSERT(isMainThread());

    MutexLocker lock(m_lock);
    RELEASE_ASSERT(!m_done);

    m_clientTasks.append(ClientTask(location, std::move(task)));
}

void WorkerThreadableLoader::MainThreadSyncBridge::forwardTaskToWorkerOnLoaderDone(const WebTraceLocation& location, std::unique_ptr<ExecutionContextTask> task)
{
    ASSERT(isMainThread());

    MutexLocker lock(m_lock);
    RELEASE_ASSERT(!m_done);

    m_clientTasks.append(ClientTask(location, std::move(task)));
    m_done = true;
    m_loaderDoneEvent->signal();
}

} // namespace blink
