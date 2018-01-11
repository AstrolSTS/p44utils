//
//  Copyright (c) 2013-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "httpcomm.hpp"


using namespace p44;


typedef enum {
  httpThreadSignalDataReady = threadSignalUserSignal
} HttpThreadSignals;



HttpComm::HttpComm(MainLoop &aMainLoop) :
  mainLoop(aMainLoop),
  requestInProgress(false),
  mgConn(NULL),
  httpAuthInfo(NULL),
  timeout(Never),
  responseDataFd(-1),
  streamResult(false),
  dataProcessingPending(false)
{
}


HttpComm::~HttpComm()
{
  if (httpAuthInfo) free(httpAuthInfo); // we own this
  terminate();
}


void HttpComm::terminate()
{
  responseCallback.clear(); // prevent calling back now
  cancelRequest(); // make sure subthread is cancelled
}



void HttpComm::requestThread(ChildThreadWrapper &aThread)
{
  string protocol, hostSpec, host, doc;
  uint16_t port;

  requestError.reset();
  response.clear();
  splitURL(requestURL.c_str(), &protocol, &hostSpec, &doc, NULL, NULL);
  bool useSSL = false;
  if (protocol=="http") {
    port = 80;
    useSSL = false;
  }
  else if (protocol=="https") {
    port = 443;
    useSSL = true;
  }
  else {
    requestError = Error::err<HttpCommError>(HttpCommError::invalidParameters, "invalid protocol");
  }
  splitHost(hostSpec.c_str(), &host, &port);
  if (Error::isOK(requestError)) {
    // now issue request
    const size_t ebufSz = 100;
    char ebuf[ebufSz];
    string extraHeaders;
    for (HttpHeaderMap::iterator pos=requestHeaders.begin(); pos!=requestHeaders.end(); ++pos) {
      extraHeaders += string_format("%s: %s\r\n", pos->first.c_str(), pos->second.c_str());
    }
    struct mg_client_options copts;
    copts.host = host.c_str();
    copts.port = port;
    copts.client_cert = NULL;
    copts.server_cert = NULL;
    copts.timeout = timeout==Never ? -2 : (double)timeout/Second;
    if (requestBody.length()>0) {
      // is a request which sends data in the HTTP message body (e.g. POST)
      mgConn = mg_download_secure(
        &copts,
        useSSL,
        method.c_str(),
        doc.c_str(),
        username.empty() ? NULL : username.c_str(),
        password.empty() ? NULL : password.c_str(),
        &httpAuthInfo,
        ebuf, ebufSz,
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "%s"
        "\r\n"
        "%s",
        contentType.c_str(),
        requestBody.length(),
        extraHeaders.c_str(),
        requestBody.c_str()
      );
    }
    else {
      // no request body (e.g. GET, DELETE)
      mgConn = mg_download_secure(
        &copts,
        useSSL,
        method.c_str(),
        doc.c_str(),
        username.empty() ? NULL : username.c_str(),
        password.empty() ? NULL : password.c_str(),
        &httpAuthInfo,
        ebuf, ebufSz,
        "%s"
        "\r\n",
        extraHeaders.c_str()
      );
    }
    if (!mgConn) {
      requestError = Error::err_cstr<HttpCommError>(HttpCommError::mongooseError, ebuf);
    }
    else {
      // successfully initiated connection
      const struct mg_response_info *responseInfo = mg_get_response_info(mgConn);
      // - get status code (which is in uri)
      int status = responseInfo->status_code;
      // check for auth
      if (status==401) {
        LOG(LOG_DEBUG, "401 - http auth?")
      }
      if (status<200 || status>=300) {
        // Important: report status as WebError, not HttpCommError, because it is not technically an error on the HTTP transport level
        requestError = WebError::webErr(status,"HTTP non-ok status");
      }
      // - get headers if requested
      if (responseHeaders) {
        if (responseInfo) {
          for (int i=0; i<responseInfo->num_headers; i++) {
            (*responseHeaders)[responseInfo->http_headers[i].name] = responseInfo->http_headers[i].value;
          }
        }
      }
      if (Error::isOK(requestError) || requestError->isDomain(WebError::domain())) {
        // - read data
        const size_t bufferSz = 2048;
        uint8_t *bufferP = new uint8_t[bufferSz];
        while (true) {
          ssize_t res = mg_read_ex(mgConn, bufferP, bufferSz, streamResult ? 0 : copts.timeout);
          if (res==0) {
            // connection has closed, all bytes read
            if (streamResult) {
              // when streaming, signal end-of-stream condition by an empty data response
              response.clear();
            }
            break;
          }
          else if (res<0) {
            // read error
            requestError = Error::err<HttpCommError>(HttpCommError::read, "HTTP read error: %s", strerror(errno));
            break;
          }
          else {
            // data read
            if (responseDataFd>=0) {
              // write to fd
              write(responseDataFd, bufferP, res);
            }
            else if (streamResult) {
              // pass back the data chunk now
              response.assign((const char *)bufferP, (size_t)res);
              dataProcessingPending = true;
              aThread.signalParentThread(httpThreadSignalDataReady);
              // now wait until data has been processed in main thread
              while (dataProcessingPending) {
                // FIXME: ugly - add better thread signalling here
                usleep(50000); // 50mS
              }
            }
            else {
              // just collect entire response in string
              response.append((const char *)bufferP, (size_t)res);
            }
          }
        }
        delete[] bufferP;
      }
      mg_close_connection(mgConn);
    }
  }
  // ending the thread function will call the requestThreadSignal on the main thread
}



void HttpComm::requestThreadSignal(ChildThreadWrapper &aChildThread, ThreadSignals aSignalCode)
{
  DBGLOG(LOG_DEBUG, "HttpComm: Received signal from child thread: %d", aSignalCode);
  if (aSignalCode==threadSignalCompleted) {
    DBGLOG(LOG_DEBUG, "- HTTP subthread exited - request completed");
    requestInProgress = false; // thread completed
    // call back with result of request
    // Note: as this callback might initiate another request already and overwrite the callback, copy it here
    HttpCommCB cb = responseCallback;
    string resp = response;
    ErrorPtr reqErr = requestError;
    // release child thread object now
    responseCallback.clear();
    childThread.reset();
    // now execute callback
    if (cb) cb(resp, reqErr);
  }
  else if (aSignalCode==httpThreadSignalDataReady) {
    // data chunk ready in streamResult mode
    DBGLOG(LOG_DEBUG, "- HTTP subthread delivers chunk of data - request going on");
    // callback may NOT issue another request on this httpComm, so no need to copy it
    if (responseCallback) responseCallback(response, ErrorPtr());
    dataProcessingPending = false; // child thread can go on reading
  }
}



bool HttpComm::httpRequest(
  const char *aURL,
  HttpCommCB aResponseCallback,
  const char *aMethod,
  const char* aRequestBody,
  const char* aContentType,
  int aResponseDataFd,
  bool aSaveHeaders,
  bool aStreamResult
)
{
  if (requestInProgress || !aURL)
    return false; // blocked or no URL
  responseDataFd = aResponseDataFd;
  responseHeaders.reset();
  requestError.reset();
  if (aSaveHeaders)
    responseHeaders = HttpHeaderMapPtr(new HttpHeaderMap);
  requestURL = aURL;
  responseCallback = aResponseCallback;
  method = aMethod;
  requestBody = nonNullCStr(aRequestBody);
  if (aContentType)
    contentType = aContentType; // use specified content type
  else
    contentType = defaultContentType(); // use default for the class
  streamResult = aStreamResult;
  // now let subthread handle this
  requestInProgress = true;
  childThread = MainLoop::currentMainLoop().executeInThread(
    boost::bind(&HttpComm::requestThread, this, _1),
    boost::bind(&HttpComm::requestThreadSignal, this, _1, _2)
  );
  return true; // could be initiated (even if immediately ended due to error, but callback was called)
}


void HttpComm::cancelRequest()
{
  if (requestInProgress && childThread) {
    childThread->cancel();
    requestInProgress = false; // prevent cancelling multiple times
  }
}

// MARK: ===== Utilities


//  8.2.1. The form-urlencoded Media Type
//
//     The default encoding for all forms is `application/x-www-form-
//     urlencoded'. A form data set is represented in this media type as
//     follows:
//
//          1. The form field names and values are escaped: space
//          characters are replaced by `+', and then reserved characters
//          are escaped as per [URL]; that is, non-alphanumeric
//          characters are replaced by `%HH', a percent sign and two
//          hexadecimal digits representing the ASCII code of the
//          character. Line breaks, as in multi-line text field values,
//          are represented as CR LF pairs, i.e. `%0D%0A'.
//
//          2. The fields are listed in the order they appear in the
//          document with the name separated from the value by `=' and
//          the pairs separated from each other by `&'. Fields with null
//          values may be omitted. In particular, unselected radio
//          buttons and checkboxes should not appear in the encoded
//          data, but hidden fields with VALUE attributes present
//          should.
//
//              NOTE - The URI from a query form submission can be
//              used in a normal anchor style hyperlink.
//              Unfortunately, the use of the `&' character to
//              separate form fields interacts with its use in SGML
//              attribute values as an entity reference delimiter.
//              For example, the URI `http://host/?x=1&y=2' must be
//              written `<a href="http://host/?x=1&#38;y=2"' or `<a
//              href="http://host/?x=1&amp;y=2">'.
//
//              HTTP server implementors, and in particular, CGI
//              implementors are encouraged to support the use of
//              `;' in place of `&' to save users the trouble of
//              escaping `&' characters this way.

string HttpComm::urlEncode(const string &aString, bool aFormURLEncoded)
{
  string result;
  const char *p = aString.c_str();
  while (char c = *p++) {
    if (aFormURLEncoded && c==' ')
      result += '+'; // replace spaces by pluses
    else if (!isalnum(c))
      string_format_append(result, "%%%02X", (unsigned int)c & 0xFF);
    else
      result += c; // just append
  }
  return result;
}


void HttpComm::appendFormValue(string &aDataString, const string &aFieldname, const string &aValue)
{
  if (aDataString.size()>0) aDataString += '&';
  aDataString += urlEncode(aFieldname, true);
  aDataString += '=';
  aDataString += urlEncode(aValue, true);
}


