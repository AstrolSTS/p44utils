//
//  Copyright (c) 2016-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Lukas Zeller <luz@plan44.ch>
//
//  This file is part of p44utils.
//
//  p44utils is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  p44utils is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with p44utils. If not, see <http://www.gnu.org/licenses/>.
//

#include "catch.hpp"

#include "p44utils_common.hpp"
#include "httpcomm.hpp"

using namespace p44;

class HttpFixture {

  string URL;
  string method;
  string requestBody;
  string contentType;
  bool streamResult;
  MLMicroSeconds timeout;

public:
  HttpCommPtr http;

  ErrorPtr httpErr;
  string response;
  MLMicroSeconds tm;

  HttpFixture()
  {
    http = HttpCommPtr(new HttpComm(MainLoop::currentMainLoop()));
  };

  void testRes(const string &aResponse, ErrorPtr aError)
  {
    tm = MainLoop::now()-tm; // calculate duration
    httpErr = aError;
    response = aResponse;
    MainLoop::currentMainLoop().terminate(EXIT_SUCCESS);
  };


  void perform()
  {
    // start timing
    tm = MainLoop::now();
    // start request
    http->setTimeout(timeout);
    if (!http->httpRequest(
      URL.c_str(),
      boost::bind(&HttpFixture::testRes, this, _1, _2),
      method.empty() ? NULL : method.c_str(),
      requestBody.empty() ? NULL : requestBody.c_str(),
      contentType.empty() ? NULL : contentType.c_str(),
      -1,
      true,
      streamResult
    )) {
      MainLoop::currentMainLoop().terminate(EXIT_FAILURE);
    }
  };


  int runHttp(
    string aURL,
    string aMethod = "GET",
    MLMicroSeconds aTimeout = 1*Second,
    string aRequestBody = "",
    string aContentType = "",
    bool aStreamResult = false
  ) {
    // save params
    URL = aURL;
    method = aMethod;
    timeout = aTimeout;
    requestBody = aRequestBody;
    contentType = aContentType;
    streamResult = aStreamResult;
    // schedule execution
    MainLoop::currentMainLoop().executeOnce(boost::bind(&HttpFixture::perform, this));
    // now let mainloop run (and terminate)
    return MainLoop::currentMainLoop().run(true);
  };


};


#define TEST_URL "plan44.ch/testing/httptest.php"
#define ERR404_TEST_URL "plan44.ch/testing/BADhttptest.php"
#define ERR500_TEST_URL "plan44.ch/testing/httptest.php?err=500"
#define SLOWDATA_TEST_URL "plan44.ch/testing/httptest.php?delay=3"
#define NOTRESPOND_TEST_URL "192.168.42.23"
#define AUTH_TEST_URL "plan44.ch/testing/authenticated/httptest.php"
#define AUTH_TEST_USER "testing"
#define AUTH_TEST_PW "testing"

//TEST_CASE_METHOD(HttpFixture, "http GET test: request to known-good server", "[http]") {
//  REQUIRE(runHttp("http://" TEST_URL)==EXIT_SUCCESS);
//  INFO(Error::text(httpErr));
//  REQUIRE(Error::isOK(httpErr));
//  REQUIRE(response.size()>0);
//}
//
//TEST_CASE_METHOD(HttpFixture, "DNS test: known not existing URL", "[http]") {
//  REQUIRE(runHttp("http://anurlthatxyzdoesnotexxxist.com", "GET", 2*Second)==EXIT_SUCCESS);
//  INFO(Error::text(httpErr));
//  REQUIRE(Error::isError(httpErr, HttpCommError::domain(), HttpCommError::mongooseError));
//  REQUIRE(tm < 2.1*Second);
//}
//
//TEST_CASE_METHOD(HttpFixture, "http timeout test: not responding IPv4", "[http]") {
//  REQUIRE(runHttp("http://" NOTRESPOND_TEST_URL, "GET", 2*Second)==EXIT_SUCCESS);
//  INFO(Error::text(httpErr));
//  REQUIRE(Error::isError(httpErr, HttpCommError::domain(), HttpCommError::mongooseError));
//  REQUIRE(tm < 2.1*Second);
//}
//
//TEST_CASE_METHOD(HttpFixture, "https GET test: request to known-good server", "[http]") {
//  REQUIRE(runHttp("https://" TEST_URL)==EXIT_SUCCESS);
//  INFO(Error::text(httpErr));
//  REQUIRE(Error::isOK(httpErr));
//  REQUIRE(response.size()>0);
//}
//
//TEST_CASE_METHOD(HttpFixture, "https timeout test: not responding IPv4", "[http]") {
//  REQUIRE(runHttp("https://" NOTRESPOND_TEST_URL, "GET", 2*Second)==EXIT_SUCCESS);
//  INFO(Error::text(httpErr));
//  REQUIRE(Error::isError(httpErr, HttpCommError::domain(), HttpCommError::mongooseError));
//  REQUIRE(tm < 2.1*Second);
//}
//
//TEST_CASE_METHOD(HttpFixture, "http auth: no credentials", "[http]") {
//  REQUIRE(runHttp("http://" AUTH_TEST_URL, "GET")==EXIT_SUCCESS);
//  INFO(Error::text(httpErr));
//  REQUIRE(Error::isError(httpErr, WebError::domain(), 401));
//}
//
//TEST_CASE_METHOD(HttpFixture, "http auth: bad credentials", "[http]") {
//  http->setHttpAuthCredentials("BAD" AUTH_TEST_USER, "BAD" AUTH_TEST_PW);
//  REQUIRE(runHttp("http://" AUTH_TEST_URL, "GET")==EXIT_SUCCESS);
//  INFO(Error::text(httpErr));
//  REQUIRE(Error::isError(httpErr, WebError::domain(), 401));
//}
//
//TEST_CASE_METHOD(HttpFixture, "http auth: correct credentials", "[http]") {
//  http->setHttpAuthCredentials(AUTH_TEST_USER, AUTH_TEST_PW);
//  REQUIRE(runHttp("http://" AUTH_TEST_URL, "GET")==EXIT_SUCCESS);
//  INFO(Error::text(httpErr));
//  REQUIRE(Error::isOK(httpErr));
//  REQUIRE(response.size()>0);
//}
//
//TEST_CASE_METHOD(HttpFixture, "https auth: no credentials", "[http]") {
//  REQUIRE(runHttp("https://" AUTH_TEST_URL, "GET")==EXIT_SUCCESS);
//  INFO(Error::text(httpErr));
//  REQUIRE(Error::isError(httpErr, WebError::domain(), 401));
//}
//
//TEST_CASE_METHOD(HttpFixture, "https auth: bad credentials", "[http]") {
//  http->setHttpAuthCredentials("BAD" AUTH_TEST_USER, "BAD" AUTH_TEST_PW);
//  REQUIRE(runHttp("https://" AUTH_TEST_URL, "GET")==EXIT_SUCCESS);
//  INFO(Error::text(httpErr));
//  REQUIRE(Error::isError(httpErr, WebError::domain(), 401));
//}
//
//TEST_CASE_METHOD(HttpFixture, "https auth: correct credentials", "[http]") {
//  http->setHttpAuthCredentials(AUTH_TEST_USER, AUTH_TEST_PW);
//  REQUIRE(runHttp("https://" AUTH_TEST_URL, "GET")==EXIT_SUCCESS);
//  INFO(Error::text(httpErr));
//  REQUIRE(Error::isOK(httpErr));
//  REQUIRE(response.size()>0);
//}
//
//TEST_CASE_METHOD(HttpFixture, "test http Error 404", "[http]") {
//  REQUIRE(runHttp("http://" ERR404_TEST_URL, "GET")==EXIT_SUCCESS);
//  INFO(Error::text(httpErr));
//  REQUIRE(Error::isError(httpErr, WebError::domain(), 404));
//}
//
//TEST_CASE_METHOD(HttpFixture, "test http Error 500", "[http]") {
//  REQUIRE(runHttp("http://" ERR500_TEST_URL, "GET")==EXIT_SUCCESS);
//  INFO(Error::text(httpErr));
//  REQUIRE(Error::isError(httpErr, WebError::domain(), 500));
//}

TEST_CASE_METHOD(HttpFixture, "http data timeout", "[http]") {
  REQUIRE(runHttp("http://" SLOWDATA_TEST_URL, "GET", 2*Second)==EXIT_SUCCESS);
  INFO(Error::text(httpErr));
  REQUIRE(tm == Approx(2*Second).epsilon(0.2));
  REQUIRE(Error::isError(httpErr, WebError::domain(), 500));
}

TEST_CASE_METHOD(HttpFixture, "http slow data", "[http]") {
  REQUIRE(runHttp("http://" SLOWDATA_TEST_URL, "GET", 6*Second)==EXIT_SUCCESS);
  INFO(Error::text(httpErr));
  REQUIRE(Error::isOK(httpErr));
  REQUIRE(response.size()>0);
  REQUIRE(tm == Approx(3*Second).epsilon(0.2));
}




