// How the stream module sends a request: through an Http of its own, or a fake in the tests.
#pragma once
#include <functional>
#include <memory>

#include "net/http.h"

namespace bbm {

using HttpSend = std::function<HttpResponse(const HttpRequest &)>;

inline HttpSend http_sender() {
    auto http = std::make_shared<Http>();
    return [http](const HttpRequest &r) { return http->send(r); };
}

}  // namespace bbm
