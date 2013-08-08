//
//  jsonrpccomm.hpp
//  p44utils
//
//  Created by Lukas Zeller on 08.08.13.
//  Copyright (c) 2013 plan44.ch. All rights reserved.
//

#ifndef __p44utils__jsonrpccomm__
#define __p44utils__jsonrpccomm__

#include "jsoncomm.hpp"

using namespace std;

namespace p44 {

  class JsonRpcError : public Error
  {
  public:
    static const char *domain() { return "JsonRPC"; }
    virtual const char *getErrorDomain() const { return JsonRpcError::domain(); };
    JsonRpcError(ErrorCode aError) : Error(aError) {};
    JsonRpcError(ErrorCode aError, const std::string &aErrorMessage) : Error(aError, aErrorMessage) {};
  };


  class JsonRpcComm;

  /// callback for delivering a received JSON-RPC method or notification request
  /// @param aJsonRpcComm the JsonRpcComm calling this handler
  /// @param aMethod If this is a method call, this is the JSON-RPC (2.0) method or notification requested by the peer.
  /// @param aJsonRpcId the client id. The handler must use this id when calling sendResult(). If this is a notification request, aJsonRpcId is NULL.
  typedef boost::function<void (JsonComm *aJsonRpcComm, const char *aMethod, const char *aJsonRpcId, JsonObjectPtr aParams)> JsonRpcRequestCB;

  /// callback for delivering a received JSON-RPC method result
  /// @param aJsonRpcComm the JsonRpcComm calling this handler
  /// @param aError the referenced ErrorPtr will be set when an error occurred.
  ///   If the error returned is an JsonRpcError, aError.getErrorCode() will return the "code" member from the JSON-RPC error object,
  ///   and aError.description() will return the "message" member from the JSON-RPC error object.
  ///   aResultOrErrorData will contain the "data" member from the JSON-RPC error object, if any.
  /// @param aResultOrErrorData the result object in case of success, or the "data" member from the JSON-RPC error object
  ///   in case of an error returned via JSON-RPC from the remote peer.
  typedef boost::function<void (JsonComm *aJsonRpcComm, ErrorPtr &aError, JsonObjectPtr aResultOrErrorData)> JsonRpcResponseCB;


  typedef boost::shared_ptr<JsonRpcComm> JsonRpcCommPtr;
  /// A class providing low level access to the DALI bus
  class JsonRpcComm : public JsonComm
  {
    typedef JsonComm inherited;

    JsonRpcRequestCB jsonRequestHandler;
    int32_t requestIdCounter;

    typedef map<int32_t, JsonRpcResponseCB> PendingAnswerMap;
    PendingAnswerMap pendingAnswers;

  public:

    JsonRpcComm(SyncIOMainLoop *aMainLoopP);
    virtual ~JsonRpcComm();

    /// install callback for received JSON-RPC requests
    /// @param aJsonRpcRequestHandler will be called when a JSON-RPC request has been received
    void setRequestHandler(JsonRpcRequestCB aJsonRpcRequestHandler);

    /// send a JSON-RPC request
    /// @param aMethod the JSON-RPC (2.0) method or notification request to be sent
    /// @param aParams the parameters for the method or notification request as a JsonObject. Can be NULL.
    /// @param aResponseHandler if the request is a method call, this handler will be called when the method result arrives
    ///   Note that the aResponseHandler might not be called at all in case of lost messages etc. So do not rely on
    ///   this callback for chaining a execution thread.
    /// @result empty or Error object in case of error
    ErrorPtr sendRequest(const char *aMethod, JsonObjectPtr aParams, JsonRpcResponseCB aResponseHandler = JsonRpcResponseCB());

    /// send a JSON-RPC result (answer for successful method call)
    /// @param aJsonRpcId this must be the aJsonRpcId as received in the JsonRpcRequestCB handler.
    /// @param aResult the result as a JsonObject. Can be NULL for procedure calls without return value
    /// @result empty or Error object in case of error sending result response
    ErrorPtr sendResult(const char *aJsonRpcId, JsonObjectPtr aResult);

    /// send a JSON-RPC error (answer for unsuccesful method call)
    /// @param aJsonRpcId this must be the aJsonRpcId as received in the JsonRpcRequestCB handler.
    /// @param aErrorCode the error code
    /// @param aErrorMessage the error message or NULL to generate a standard text
    /// @param aErrorData the optional "data" member for the JSON-RPC error object
    /// @result empty or Error object in case of error sending error response
    ErrorPtr sendError(const char *aJsonRpcId, uint32_t aErrorCode, const char *aErrorMessage = NULL, JsonObjectPtr aErrorData = JsonObjectPtr());

    /// send p44utils::Error object as JSON-RPC error
    /// @param aJsonRpcId this must be the aJsonRpcId as received in the JsonRpcRequestCB handler.
    /// @param aErrorToSend From this error object, getErrorCode() and description() will be used as "code" and "message" members
    ///   of the JSON-RPC 2.0 error object.
    /// @result empty or Error object in case of error sending error response
    ErrorPtr sendError(const char *aJsonRpcId, ErrorPtr aErrorToSend);


  private:
    void gotJson(ErrorPtr aError, JsonObjectPtr aJsonObject);

  };
  
} // namespace p44


#endif /* defined(__p44utils__jsonrpccomm__) */
