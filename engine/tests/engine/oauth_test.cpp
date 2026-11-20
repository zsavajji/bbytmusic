// YouTube TV's OAuth: the page and script scraping, the token's answer, the account's answer; no network.
#include "check.h"
#include "engine/oauth.h"
#include "util/json.h"

using namespace bbm;

int main() {
    // the script's url
    CHECK_EQ(tv_base_js_url("<script id=\"base-js\" src=\"\\/s\\/tv\\/m=base\" nonce=\"x\"></script>"),
             std::string("https://www.youtube.com/s/tv/m=base"));
    CHECK_EQ(tv_base_js_url("<script id=\"base-js\" src=\"https://www.gstatic.com/a.js\"></script>"), std::string("https://www.gstatic.com/a.js"));
    CHECK_EQ(tv_base_js_url("x.src = '/a/b.js'; y.src = '/s/tv/m=base';"), std::string("https://www.youtube.com/s/tv/m=base"));
    CHECK_EQ(tv_base_js_url("<html>nothing</html>"), std::string(""));

    // the client
    OAuthClient c;
    CHECK(parse_tv_client("a clientId:\"111-aaa.apps.googleusercontent.com\",$ab_c:\"SecretOne\" b clientId:\"222-bbb.apps.googleusercontent.com\",x:\"Two\"", c));
    CHECK_EQ(c.id, std::string("111-aaa.apps.googleusercontent.com"));
    CHECK_EQ(c.secret, std::string("SecretOne"));
    c = {};
    CHECK(parse_tv_client("clientId:\"333-c.apps.googleusercontent.com\",\n  Zz:\"S3\"", c));
    CHECK_EQ(c.secret, std::string("S3"));
    c = {};
    CHECK(parse_tv_client("clientId:\"nope\",a:\"b\" clientId:\"444-d.apps.googleusercontent.com\",q:\"S4\"", c));
    CHECK_EQ(c.id, std::string("444-d.apps.googleusercontent.com"));
    CHECK(!parse_tv_client("clientId:\"nope\",a:\"b\"", c));
    CHECK(!parse_tv_client("nothing here", c));

    // the token's answer
    OAuthToken t;
    std::string err;
    CHECK(parse_token("{\"access_token\":\"AT\",\"expires_in\":3599,\"token_type\":\"Bearer\"}", t, err));
    CHECK_EQ(t.access, std::string("AT"));
    CHECK_EQ(t.expires_in, int64_t(3599));
    CHECK_EQ(t.refresh, std::string(""));
    t = {};
    CHECK(parse_token("{\"access_token\":\"AT\",\"expires_in\":10,\"refresh_token\":\"RT\"}", t, err));
    CHECK_EQ(t.refresh, std::string("RT"));
    CHECK(!parse_token("{\"error\":\"invalid_grant\",\"error_description\":\"x\"}", t, err));
    CHECK_EQ(err, std::string("invalid_grant"));
    CHECK(!parse_token("<html>", t, err));

    // refresh through a fake
    HttpRequest seen;
    HttpResponse answer;
    HttpSendFn send = [&](const HttpRequest &r) {
        seen = r;
        return answer;
    };
    const OAuthClient cl{"id-1", "sec-1"};
    answer.status = 200;
    answer.body = "{\"access_token\":\"AT\",\"expires_in\":3600}";
    CHECK(refresh_token(send, cl, "RT\"1", t, err) == OAuthResult::Ok);
    CHECK_EQ(seen.method, std::string("POST"));
    CHECK_EQ(seen.url, std::string("https://www.youtube.com/o/oauth2/token"));
    {
        JsonDoc d;
        std::string e;
        CHECK(JsonDoc::parse(seen.body, d, e));
        CHECK_EQ(d.root()["refresh_token"].string(), std::string("RT\"1"));
        CHECK_EQ(d.root()["client_id"].string(), std::string("id-1"));
        CHECK_EQ(d.root()["client_secret"].string(), std::string("sec-1"));
        CHECK_EQ(d.root()["grant_type"].string(), std::string("refresh_token"));
    }
    answer.status = 400;
    answer.body = "{\"error\":\"invalid_grant\"}";
    CHECK(refresh_token(send, cl, "RT", t, err) == OAuthResult::Refused);
    CHECK_EQ(err, std::string("invalid_grant"));
    answer = {};
    answer.error = "timeout";
    CHECK(refresh_token(send, cl, "RT", t, err) == OAuthResult::Network);
    answer = {};
    answer.status = 503;
    answer.body = "{\"error\":\"backend\"}";
    CHECK(refresh_token(send, cl, "RT", t, err) == OAuthResult::Network);

    // the account's answer
    const std::string two =
        "{\"contents\":[{\"accountSectionListRenderer\":{\"contents\":[{\"accountItemSectionRenderer\":{\"contents\":["
        "{\"accountItem\":{\"accountName\":{\"simpleText\":\"Brand Channel\"},\"accountPhoto\":{\"thumbnails\":[{\"url\":\"https://yt3.ggpht.com/b\"}]},\"isSelected\":false}},"
        "{\"accountItem\":{\"accountName\":{\"simpleText\":\"Test User\"},\"accountPhoto\":{\"thumbnails\":[{\"url\":\"https://yt3.ggpht.com/u\",\"width\":216},{\"url\":\"x\"}]},\"isSelected\":true}}"
        "]}}]}}]}";
    AccountInfo a;
    CHECK(parse_account(two, a));
    CHECK_EQ(a.name, std::string("Test User"));
    CHECK_EQ(a.photo, std::string("https://yt3.ggpht.com/u"));
    const std::string none =
        "{\"contents\":[{\"accountSectionListRenderer\":{\"contents\":[{\"accountItemSectionRenderer\":{\"contents\":["
        "{\"accountItem\":{\"accountName\":{\"simpleText\":\"Brand Channel\"},\"accountPhoto\":{\"thumbnails\":[{\"url\":\"https://yt3.ggpht.com/b\"}]}}},"
        "{\"accountItem\":{\"accountName\":{\"simpleText\":\"Test User\"},\"isSelected\":false}}]}}]}}]}";
    CHECK(parse_account(none, a));
    CHECK_EQ(a.name, std::string("Brand Channel"));
    CHECK(!parse_account("{\"contents\":[]}", a));
    CHECK(!parse_account("garbage", a));

    // fetch_account's mapping
    answer = {};
    answer.status = 200;
    answer.body = two;
    CHECK(fetch_account(send, "AT9", a, err) == OAuthResult::Ok);
    CHECK_EQ(seen.url, std::string("https://www.youtube.com/youtubei/v1/account/accounts_list?prettyPrint=false"));
    bool bearer = false;
    for (auto &h : seen.headers) bearer |= h.first == "Authorization" && h.second == "Bearer AT9";
    CHECK(bearer);
    answer.status = 401;
    CHECK(fetch_account(send, "AT9", a, err) == OAuthResult::Refused);
    answer.status = 500;
    CHECK(fetch_account(send, "AT9", a, err) == OAuthResult::Network);
    return check_done();
}
