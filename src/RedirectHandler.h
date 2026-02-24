#ifndef REDIRECT_HANDLER_H
#define REDIRECT_HANDLER_H

#include <memory>
#include <vector>
#include "AsyncHttpClient.h"

class RedirectHandler {
  public:
    explicit RedirectHandler(AsyncHttpClient* client);

    void setRedirectHeaderPolicy(AsyncHttpClient::RedirectHeaderPolicy policy);
    void addRedirectSafeHeader(const char* name);
    void clearRedirectSafeHeaders();

    bool buildRedirectRequest(AsyncHttpClient::RequestContext* context, std::unique_ptr<AsyncHttpRequest>* outRequest,
                              HttpClientError* outError, String* outErrorMessage);
    bool handleRedirect(AsyncHttpClient::RequestContext* context);

  private:
    void lock() const;
    void unlock() const;
    String resolveRedirectUrl(const AsyncHttpRequest* request, const String& location) const;
    bool isSameOrigin(const AsyncHttpRequest* original, const AsyncHttpRequest* redirect) const;
    void resetContextForRedirect(AsyncHttpClient::RequestContext* context,
                                 std::unique_ptr<AsyncHttpRequest> newRequest);
    bool isRedirectStatus(int status) const;

    AsyncHttpClient* _client = nullptr;
    AsyncHttpClient::RedirectHeaderPolicy _redirectHeaderPolicy =
        AsyncHttpClient::RedirectHeaderPolicy::kDropAllCrossOrigin;
    std::vector<String> _redirectSafeHeaders;
};

#endif // REDIRECT_HANDLER_H
