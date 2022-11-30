#include <algorithm>
#include <iostream>
#include <memory>
#include <vector>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/memory/scoped_refptr.h"
#include "base/message_loop/message_pump.h"
#include "base/message_loop/message_pump_type.h"
#include "base/run_loop.h"
#include "base/sequence_checker.h"
#include "base/task/sequence_manager/sequence_manager.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/thread_pool.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/test/bind.h"
#include "base/threading/thread.h"
#include "chromium_ll_examples/url_loader.mojom.h"
#include "mojo/core/embedder/embedder.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "net/base/completion_once_callback.h"
#include "net/base/io_buffer.h"
#include "net/cookies/cookie_store.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_builder.h"

// Because this is for learning purposes, we'll avoid using auto as much as
// possible.

class URLRequestDelegate : public net::URLRequest::Delegate {
 public:
  explicit URLRequestDelegate(base::OnceClosure finish_callback)
      : buf_(base::MakeRefCounted<net::IOBuffer>(kBufSize)),
        data_helper_(std::vector<char>(kBufSize + 1)),
        finish_callback_(std::move(finish_callback)) {}

  URLRequestDelegate(const URLRequestDelegate&) = delete;
  URLRequestDelegate& operator=(const URLRequestDelegate&) = delete;

  int OnConnected(net::URLRequest* request,
                  const net::TransportInfo& info,
                  net::CompletionOnceCallback callback) override {
    return net::OK;
  }

  void OnReceivedRedirect(net::URLRequest* request,
                          const net::RedirectInfo& redirect_info,
                          bool* defer_redirect) override {
    *defer_redirect = false;
  }

  void OnAuthRequired(net::URLRequest* request,
                      const net::AuthChallengeInfo& auth_info) override {
    DLOG(ERROR) << __FUNCTION__ << "";
  }

  void OnCertificateRequested(
      net::URLRequest* request,
      net::SSLCertRequestInfo* cert_request_info) override {
    request->ContinueWithCertificate(nullptr, nullptr);
  }

  void OnSSLCertificateError(net::URLRequest* request,
                             int net_error,
                             const net::SSLInfo& ssl_info,
                             bool fatal) override {
    DLOG(ERROR) << __FUNCTION__ << "";
  }

  void OnResponseStarted(net::URLRequest* request, int net_error) override {
    if (net_error != net::OK) {
      Finish();
      return;
    }

    ReadBytes(request);
  }

  void OnReadCompleted(net::URLRequest* request, int bytes_read) override {
    DCHECK_NE(bytes_read, net::ERR_IO_PENDING);
    if (bytes_read <= 0) {
      Finish();
      return;
    }

    ReadBytes(request);
  }

 private:
  void ReadBytes(net::URLRequest* request) {
    while (true) {
      int read_result = request->Read(buf_.get(), kBufSize);

      if (read_result == net::ERR_IO_PENDING)
        return;

      if (read_result == 0) {
        Finish();
        return;
      }

      if (read_result < 0) {
        LOG(ERROR) << "Got error code " << read_result;
        Finish();
        return;
      }

      std::copy(buf_->data(), buf_->data() + read_result,
                std::begin(data_helper_));
      data_helper_[read_result] = '\0';
      std::cout << data_helper_.data();
      std::cout.flush();
      total_bytes_downloaded_ += read_result;
    }
  }

  void Finish() {
    DLOG(ERROR) << __FUNCTION__
                << " total bytes downloaded: " << total_bytes_downloaded_;
    if (finish_callback_)
      std::move(finish_callback_).Run();
  }

  static constexpr size_t kBufSize = 4096;

  size_t total_bytes_downloaded_ = 0;
  scoped_refptr<net::IOBuffer> buf_;
  std::vector<char> data_helper_;
  base::OnceClosure finish_callback_;
};

class Request {
 public:
  Request() {}

  Request(const Request&) = delete;
  Request& operator=(const Request&) = delete;
  ~Request() = default;

  void StartOnIOThread(const GURL& gurl, base::OnceClosure finish_callback) {
    DCHECK(!context_.get());
    net::URLRequestContextBuilder context_builder;
    context_builder.DisableHttpCache();
    context_builder.SetSpdyAndQuicEnabled(false, false);
    context_builder.SetCookieStore(nullptr);

    // This should be io_thread->task_runner().
    auto task_runner = base::SequencedTaskRunner::GetCurrentDefault();
    context_builder.set_proxy_config_service(
        net::ProxyConfigService::CreateSystemProxyConfigService(task_runner));

    context_ = context_builder.Build();

    // We know we shouldn't use lambda, but that doesn't count, if we're lazy,
    // and have fun :)
    url_request_delegate_ =
        std::make_unique<URLRequestDelegate>(base::BindLambdaForTesting(
            [this, finish_callback = std::move(finish_callback)]() mutable {
              this->DeleteOnIOThread();
              std::move(finish_callback).Run();
            }));

    request_ = context_->CreateRequest(
        gurl, net::RequestPriority::HIGHEST, url_request_delegate_.get(),
        net::DefineNetworkTrafficAnnotation("chget_download", R"(
                semantics {
                  sender: "Chget downloader"
                  description:
                    "Simple wget-like utility for downloading files using Chrome
                    stack."
                  trigger: "Program gets called in command line."
                  data: "Url cammand line arguemnt."
                  destination: WEBSITE
                }
                policy {
                  cookies_allowed: NO
                  setting:
                    "Nothing here"
                  chrome_policy {
                  }
              )"));

    request_->Start();
  }

 private:
  void DeleteOnIOThread() {
    request_.reset();
    context_.reset();
  }

  std::unique_ptr<URLRequestDelegate> url_request_delegate_;
  // Must be created and destroyed on IO Thread:
  std::unique_ptr<net::URLRequestContext> context_;
  std::unique_ptr<net::URLRequest> request_;
};

class IOThreadDelegate : public base::Thread::Delegate {
 public:
  IOThreadDelegate()
      : sequence_manager_(CreateUnboundSequenceManager(
            base::sequence_manager::SequenceManager::Settings::Builder()
                .SetMessagePumpType(base::MessagePumpType::IO)
                .Build())),
        default_task_queue_(sequence_manager_->CreateTaskQueue(
            base::sequence_manager::TaskQueue::Spec(
                base::sequence_manager::QueueName::DEFAULT_TQ))),
        default_task_runner_(default_task_queue_->task_runner()),
        simple_task_executor_(default_task_runner_) {}

  IOThreadDelegate(const IOThreadDelegate&) = delete;
  IOThreadDelegate& operator=(const IOThreadDelegate&) = delete;

  ~IOThreadDelegate() override {}

  scoped_refptr<base::SingleThreadTaskRunner> GetDefaultTaskRunner() override {
    return default_task_runner_;
  }

  void BindToCurrentThread(base::TimerSlack timer_slack) override {
    DCHECK(sequence_manager_);
    sequence_manager_->BindToMessagePump(
        base::MessagePump::Create(base::MessagePumpType::IO));
    sequence_manager_->SetTimerSlack(timer_slack);
    sequence_manager_->SetDefaultTaskRunner(default_task_runner_);
    base::SetTaskExecutorForCurrentThread(&simple_task_executor_);
  }

 private:
  const std::unique_ptr<base::sequence_manager::SequenceManager>
      sequence_manager_;
  scoped_refptr<base::sequence_manager::TaskQueue> default_task_queue_;
  scoped_refptr<base::SingleThreadTaskRunner> default_task_runner_;
  base::SimpleTaskExecutor simple_task_executor_;
};

std::unique_ptr<base::Thread> StartIOThread(
    std::unique_ptr<base::Thread::Delegate> delegate) {
  auto io_thread = std::make_unique<base::Thread>("iothread");

  base::Thread::Options options;
  options.message_pump_type = base::MessagePumpType::IO;
  options.delegate = std::move(delegate);
  if (!io_thread->StartWithOptions(std::move(options)))
    LOG(FATAL) << "Failed to start Thread:IO";

  return io_thread;
}

class URLLoader : public url_loader::mojom::URLLoader {
 public:
  explicit URLLoader(
      mojo::PendingReceiver<url_loader::mojom::URLLoader> receiver);
  URLLoader(const URLLoader&) = delete;
  URLLoader& operator=(const URLLoader&) = delete;
  ~URLLoader() override {}

  void DownloadFromURL(const std::string& url,
                       DownloadFromURLCallback callback) override;

 private:
  mojo::Receiver<url_loader::mojom::URLLoader> receiver_;
  std::unique_ptr<base::Thread::Delegate> delegate_;
  std::unique_ptr<base::Thread> io_thread_;
  Request request_;
  scoped_refptr<base::SequencedTaskRunner> main_task_runner_;
};

URLLoader::URLLoader(
    mojo::PendingReceiver<url_loader::mojom::URLLoader> receiver)
    : receiver_(this, std::move(receiver)),
      delegate_(std::make_unique<IOThreadDelegate>()),
      io_thread_(StartIOThread(std::move(delegate_))),
      main_task_runner_(base::SequencedTaskRunner::GetCurrentDefault()) {
  // Request execution needs thread pool.
  base::ThreadPoolInstance::Create("download_thread_pool");
}

void URLLoader::DownloadFromURL(const std::string& url,
                                DownloadFromURLCallback callback) {
  // We must create URL request on task runner bound to io thread.
  io_thread_->task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(
          &Request::StartOnIOThread, base::Unretained(&request_), GURL(url),
          base::BindLambdaForTesting(
              [callback = std::move(callback), url, this]() mutable {
                main_task_runner_->PostTask(
                    FROM_HERE,
                    base::BindLambdaForTesting(
                        [callback = std::move(callback), url]() mutable {
                          std::move(callback).Run("Downloaded " + url);
                        }));
              })));
}

int main(int argc, char* argv[]) {
  logging::SetLogItems(true /* enable_process_id */,
                       true /* enable_thread_id */, true /* enable_timestamp */,
                       true /* enable_tickcount */);
  logging::InitLogging({});
  base::AtExitManager at_exit_manager;
  base::CommandLine::Init(argc, argv);
  base::CommandLine* cmdline = base::CommandLine::ForCurrentProcess();

  if (cmdline->GetArgs().size() < 1) {
    LOG(ERROR) << "Argument missing.";
    return 1;
  }

  const std::string url_str = cmdline->GetArgs().front();
  GURL gurl(url_str);

  if (!gurl.is_valid()) {
    LOG(ERROR) << "Argument must be valid url";
    return 2;
  }

  mojo::core::Init();

  base::SingleThreadTaskExecutor task_executor;
  base::RunLoop main_run_loop;
  base::OnceClosure quit_loop = main_run_loop.QuitClosure();

  mojo::Remote<url_loader::mojom::URLLoader> remote;
  mojo::PendingReceiver<url_loader::mojom::URLLoader> receiver =
      remote.BindNewPipeAndPassReceiver();

  URLLoader url_loader_factory(std::move(receiver));

  remote->DownloadFromURL(
      url_str, base::BindLambdaForTesting([quit_loop = std::move(quit_loop)](
                                              const std::string& msg) mutable {
        DLOG(ERROR) << msg << std::endl;
        std::move(quit_loop).Run();
      }));

  // Loop in place till download completes.
  main_run_loop.Run();
  return 0;
}
