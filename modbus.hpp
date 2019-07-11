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

#ifndef __p44utils__modbus__
#define __p44utils__modbus__

#ifndef ENABLE_MODBUS
  // We assume that including this file in a build usually means that modbus support is actually needed.
  // Still, ENABLE_MODBUS can be set to 0 to create build variants w/o removing the file from the project/makefile
  #define ENABLE_MODBUS 1
#endif

#if ENABLE_MODBUS

#include "p44utils_common.hpp"
#include "digitalio.hpp"

#include <stdio.h>

// - modbus
#if EMBEDDED_LIBMODBUS
  // use p44utils internal libmodbus (statically linked)
  #include "modbus.h"
#else
  // target with libmodbus installed
  #include <modbus/modbus.h>
#endif

using namespace std;

namespace p44 {


  class ModBusError : public Error
  {
  public:
    // Note: errorCodes are as follows
    //  up to 999 : native system errnos
    // 1000..1999 : modbus protocol exception codes, followed by libmodbus internal errors
    // 2000..     : p44 ModBus errors
    enum {
      SysErr = 0, ///< offset for syserrors
      MBErr = 1000, ///< offset for modbus protocol exceptions (EMBX...) and libmodbus internal errors (EMB...)
      P44Err = 2000, ///< offset for the P44 specific errors below
      InvalidConnParams = P44Err, ///< invalid connection parameters
      NoContext, ///< no valid modbus context
      InvalidSlaveAddr, ///< invalid slave address/address range
      P44HeaderError, ///< invalid P44 header
    };
    static const char *domain() { return "Modbus"; }
    virtual const char *getErrorDomain() const { return ModBusError::domain(); };
    ModBusError(ErrorCode aError) : Error(aError>MODBUS_ENOBASE ? aError-MODBUS_ENOBASE+MBErr : aError, modbus_strerror((int)aError)) {
      prefixMessage("Modbus: ");
    };
  };


  extern "C" {
    void setRts(modbus_t* ctx, int on, void* cbctx);
  }


  typedef uint8_t ModBusPDU[MODBUS_MAX_PDU_LENGTH]; ///< a buffer large enough for a modbus PDU

  class ModbusConnection : public P44Obj
  {
    friend void setRts(modbus_t *ctx, int on, void* cbctx);

  public:

    typedef enum {
      float_abcd,
      float_dcba,
      float_badc,
      float_cdab
    } FloatMode;

  protected:

    modbus_t *modbus; ///< the connection context (modbus RTU or TCP context)
    bool isTcp; ///< set if the backend is TCP
    bool doAcceptConnections; ///< for TCP, if set, connect() will start listening rather than connecting to a server
    int serverSocket; ///< socket where we are listening

    int slaveAddress; ///< current slave address
    bool connected; ///< set if connection is open
    FloatMode floatMode; ///< current mode for setting/getting float values

  public:

    ModbusConnection();
    virtual ~ModbusConnection();

    /// Specify the Modbus connection parameters as single string
    /// @param aConnectionSpec "/dev[:commParams]" or "hostname[:port]"
    /// @param aDefaultPort default port number for TCP connection (irrelevant for direct serial device connection)
    /// @param aDefaultCommParams default communication parameters (in case spec does not contain :commParams)
    /// @param aTransmitEnableSpec optional specification of a DigitalIo used to enable/disable the RS485 transmit driver.
    ///    If set to NULL or "RTS", the RTS line enables the RS485 drivers.
    ///    If set to "RS232", the connection is a plain two-point serial connection
    /// @param aTxDisableDelay if>0, time delay in uS before disabling Tx driver after sending
    /// @return error in case the connection context cannot be created from these parameters
    /// @note commParams syntax is: [baud rate][,[bits][,[parity][,[stopbits][,[H]]]]]
    ///   - parity can be O, E or N
    ///   - H means hardware handshake enabled
    ErrorPtr setConnectionSpecification(
      const char* aConnectionSpec, uint16_t aDefaultPort, const char *aDefaultCommParams,
      const char *aTransmitEnableSpec = NULL, MLMicroSeconds aTxDisableDelay = Never
    );

    /// set the slave address (when RTU endpoints are involved)
    /// @param aSlaveAddress the slave address
    /// @note in master mode, this specifies the slave to connect to (or all when set to MODBUS_BROADCAST_ADDRESS).
    ///   In slave mode, this is this instance's slave address
    void setSlaveAddress(int aSlaveAddress);

    /// @return the currently set slave address
    int getSlaveAddress() { return slaveAddress; };

    /// @return true if slave address is set to broadcast
    bool isBroadCast() { return slaveAddress == MODBUS_BROADCAST_ADDRESS; };

    /// enable accepting connections (TCP only)
    /// @param aAccept true if TCP servere
    void acceptConnections(bool aAccept) { doAcceptConnections = aAccept; };

    /// open the connection
    /// @return error, if any
    ErrorPtr connect();

    /// close the modbus connection
    virtual void close();

    /// flush the connection (forget bytes already received from transport (but do not flush socket/filedesc itself)
    /// @return number of bytes flushed (or -1 in case of error)
    int flush();

    /// @return true if connection is open
    bool isConnected() { return connected; };

    /// enable debug messages to stderr
    /// @param aDebugEnabled set true to enable debug messages
    void setDebug(bool aDebugEnabled);


    /// set float mode
    /// @param aFloatMode the new float mode
    void setFloatMode(FloatMode aFloatMode) { floatMode = aFloatMode; };

    /// convert two register's contents to double
    /// @param aTwoRegs pointer to first of two consecutive registers containing a modbus float value
    /// @return the floating point value
    /// @note the byte order in the registers must match the mode set with setFloatMode()
    double getAsDouble(const uint16_t *aTwoRegs);

    /// convert double into two registers
    /// @param aTwoRegs pointer to first of two consecutive registers which will receive a modbus float value
    /// @param aDouble the new floating point value
    /// @note the byte order in the registers must match the mode set with setFloatMode()
    void setAsDouble(uint16_t *aTwoRegs, double aDouble);

  protected:

    /// build a exception response message
    /// @param aSft the slaveid/function/transactionid info
    /// @param aExceptionCode the modbus exception code
    /// @param aErrorText the error text (for log)
    /// @param aRsp response will be created in this buffer
    /// @param aRspLen the length of the response will be stored here
    void buildExceptionResponse(sft_t &aSft, int aExceptionCode, const char* aErrorText, ModBusPDU& aRsp, int& aRspLen);

    /// build a exception response message
    /// @param aSft the slaveid/function/transactionid info
    /// @param aError a error object. If it is a ModBusError, its modbus exception code will be used, otherwise, SLAVE_OR_SERVER_FAILURE will be used
    /// @param aRsp response will be created in this buffer
    /// @param aRspLen the length of the response will be stored here
    void buildExceptionResponse(sft_t &aSft, ErrorPtr aError, ModBusPDU& aRsp, int& aRspLen);


    /// build a exception response message
    /// @param aSft the slaveid/function/transactionid info
    /// @param aRsp response will be created in this buffer
    /// @param aRspLen the length of the response will be stored here
    void buildResponseBase(sft_t &aSft, ModBusPDU& aRsp, int& aRspLen);

    /// append message data
    /// @param aDataP data to append, can be NULL to just append 0x00 bytes
    /// @param aNumBytes number of bytes to append
    /// @param aRsp append to response in this buffer
    /// @param aRspLen on input: size of the reponse so far, will be updated
    /// @return false if too much data
    bool appendToMessage(const uint8_t *aDataP, int aNumBytes, ModBusPDU& aMsg, int& aMsgLen);


  protected:

    /// is called when the "modbus" member variable is ready with a newly set-up context
    virtual void mbContextReady();

    /// start receiving messages as server/slave if possible
    virtual void startServing() { /* NOP in base class */ };

    /// check if error is a communication error, that is, if resending a request might make sense
    /// @return true if the error is a comm error (timeout, CRC, connection broken, etc.)
    static bool isCommErr(ErrorPtr aError);

  private:

    void clearModbusContext();
    bool connectionAcceptHandler(int aFd, int aPollFlags);


  public:
    // stuff that needs to be public because friend declaration does not work in gcc (does in clang)
    DigitalIoPtr modbusTxEnable; ///< if set, this I/O is used to enable sending

  };


  class ModbusFileHandler;
  typedef boost::intrusive_ptr<ModbusFileHandler> ModbusFileHandlerPtr;

  /// Callback executed when a file writing completes
  /// @param aFileNo the file number that was written
  /// @param aFinalFilePath the final destination path of the file (not yet valid if aTempFilePath is non-empty)
  /// @param aTempFilePath if the file was written to a temp file, this is the file path of the temp file.
  ///   The handler usually wants to copy or move the temp file to the final destination path
  typedef boost::function<void (uint16_t aFileNo, const string aFinalFilePath, const string aTempFilePath)> ModbusFileWriteCompleteCB;

  class ModbusFileHandler : public P44Obj
  {
    int fileNo; ///< the file number. For large files with multiple segments, subsequent numbers might be in use, too
    int maxSegments; ///< the number of segments, i.e. consecutive file numbers after fileNo + 1 which belong to the same file
    int numFiles; ///< number of files at the same path (with fileno appended)
    string filePath; ///< the local path of the file(s)
    string finalBasePath; ///< final base path (including final separator, if any)
    bool useP44Header; ///< set if this file uses a P44 header
    bool readOnly; ///< set if file is read-only

    /// ongoing transfer parameters
    uint16_t currentBaseFileNo; ///< the currently open file (base number for segmented files)
    int openFd; ///< the open file descriptor

    /// data for/from P44 header
    bool validP44Header; ///< if set, internal P44 header info is valid
    /// the size of a addressed record (in uint16_t quantities).
    /// @note
    /// - Traditional Modbus interpretations use 1 for this, meaning that records are register-sized.
    ///   In this case, using a "Record Length" > 1 in the read/write commands just reads multiple subsequent registers
    /// - Setting this to a larger value means that each record consists of MORE THAN ONE uint_t of the entire file.
    ///   In this case, if the "Record Length" in a read/write command is less than singleRecordLength, only part of the record is
    ///   actually transferred.
    /// - if useP44Header is set, this value is included in the header.
    /// - the implementation uses 1 for this quantity when the size of the file allows it,
    ///   i.e. when the file size is below <maxrecordno>*2 = 0xFFFF*2 = 128kB.
    uint8_t singleRecordLength;
    uint8_t neededSegments; ///< the number of segments needed for the current file
    uint8_t recordsPerChunk; ///< the number of records that are be transmitted in a chunk
    uint16_t firstDataRecord; ///< the number of the first actual data record
    uint32_t remoteMissingRecord; ///< next missing record number (32bit, over all segments of the file) to retransmit (in multicast mode), or noneMissing
    static const uint32_t noneMissing = 0xFFFFFFFF; ///< signals no missing record in remoteMissingRecord and file complete in nextExpectedDataRecord
    uint32_t remoteCRC32; ///< on write, this is the CRC32 expected as extracted from the p44header
    uint32_t remoteFileSize; ///< on write, this is the CRC32 expected as extracted from the p44header
    // local info
    uint32_t localFileSize; ///< the local file size, as obtained by readLocalFileInfo
    static const uint32_t invalidCRC = 0xFFFFFFFF; ///< signals invalid CRC
    uint32_t localCRC32; ///< the local file's CRC32, as obtained by readLocalFileInfo
    typedef std::list<uint32_t> RecordNoList;
    RecordNoList missingDataRecords; ///< list of DATA record numbers of chunks that were missing in the ongoing file receive (chunk might be more than 1 record!)
    uint32_t nextExpectedDataRecord; ///< the next DATA record number we expect to get. If next received is >nextExpectedRecord, the records in between are saved into missingRecords
    bool pendingFinalisation; ///< set if file must be finalized (CRC updated)
    ModbusFileWriteCompleteCB fileWriteCompleteCB; ///< called when a file writing completes

  public:

    /// @param aFileNo the modbus file number for the first (maybe only) file handled by this handler
    /// @param aMaxSegments max number of segments (consecutive file numbers reserved for belonging to the same file), If 0, as many segments as needed are read/written
    /// @param aNumFiles number of consecutive files (or groups of filenos of segmented files) the same path, differentiated by appended fileno
    /// @param aP44Header if set, first 8 16bit "registers" or "records" of the file are a file header containing overall file size and CRC
    /// @param aFilePath the local pathname of the file. If aNumFiles>1, the actual file number will have the file number appended
    /// @param aReadOnly if set, the file is read-only
    /// @param aFinalBasePath if set, writing the file is done in temp dir at /tmp/<aFilePath>,
    ///   and only when complete the file is copied to <aFinalBasePath><aFilePath> (note that there is no delimiter inserted automatically in between!)
    ModbusFileHandler(int aFileNo, int aMaxSegments, int aNumFiles, bool aP44Header, const string aFilePath, bool aReadOnly = false, const string aFinalBasePath = "");

    virtual ~ModbusFileHandler();

    /// set callback to be executed when a file write completes successfully
    /// @param aFileWriteCompleteCB the callback
    /// @note the callback is called AFTER the modbus request completing the file has been answered
    void setFileWriteCompleteCB(ModbusFileWriteCompleteCB aFileWriteCompleteCB) { fileWriteCompleteCB = aFileWriteCompleteCB; };

    /// check if a particular file number is handled by this filehandler
    /// @param aFileNo the file number to check
    /// @return true if the handler will handle this fileno
    bool handlesFileNo(uint16_t aFileNo);

    /// Write data to a file
    /// @param aFileNo the file number
    /// @param aRecordNo the record number within the file
    /// @param aDataP the data to be written
    /// @param aDataLen the number of bytes (not records!) to be written
    /// @return Ok or error
    ErrorPtr writeLocalFile(uint16_t aFileNo, uint16_t aRecordNo, const uint8_t* aDataP, size_t aDataLen);

    /// Read data from file
    /// @param aFileNo the file number
    /// @param aRecordNo the record number within the file
    /// @param aDataP the data buffer to be read into
    /// @param aDataLen the number of bytes (not records!) to be read
    /// @return Ok or error
    ErrorPtr readLocalFile(uint16_t aFileNo, uint16_t aRecordNo, uint8_t* aDataP, size_t aDataLen);

    /// open local file for given file number and obtain needed file info
    /// @param aFileNo the file to access (one handler might be responsible for multiple files when numFiles>1)
    /// @param aForLocalWrite if set, file will be open for write after this call
    ErrorPtr openLocalFile(uint16_t aFileNo, bool aForLocalWrite);

    /// close the current local file, header info gets invalid
    void closeLocalFile();

    /// @return true when file needs finalisation
    bool needFinalizing() { return pendingFinalisation; }


    /// Update the local CRC
    /// @return Ok or error
    /// @note file must be open
    ErrorPtr updateLocalCRC();

    /// Finalize the local file (update CRC...)
    /// @return Ok or error
    /// @note file must be open, will be closed afterwards
    ErrorPtr finalize();

    /// read info (size) from local file
    /// @param aInitialize if true, file layout info (segments, single record length...) will be initialized,
    ///    if false, only size is updated
    /// @return Ok or error
    ErrorPtr readLocalFileInfo(bool aInitialize);

    /// @return max number records to be transferred in a single request/response
    /// @note the return value is chosen to fit *and* be a nice number. Absolute max might be a bit higher.
    uint16_t maxRecordsPerRequest();

    /// @return number of record addresses covered by a single chunk
    uint16_t recordAddrsPerChunk();

    /// generate P44 header from local information
    /// @param aDataP where to generate the header to
    /// @param aMaxDataLen size of buffer at aDataP
    /// @return size of header data generated, -1 on error, or 0 if this handler is not enabled for P44 header at all
    int generateP44Header(uint8_t* aDataP, int aMaxDataLen);

    /// parse P44 header sent by remote peer to set up parameters of this file handler
    /// @param aDataP data to be parsed withing (at aParsePos)
    /// @param aPos index into aDataP where to start parsing
    /// @param aDataLen size of data at aDataP (parser will not read further)
    /// @param aInitialize if true, file layout info (segments, single record length...) will be initialized,
    ///    if false, the layout info is checked to match our version
    /// @return ok if no header expected or successfully parsed one, error otherwise
    ErrorPtr parseP44Header(const uint8_t* aDataP, int aPos, int aDataLen, bool aInitialize);


    /// @param aChunkIndex an index (in terms of chunks as big as possible for one PDU) starting at 0
    /// @param aRemotely if set, remote file size is the reference
    /// @return true if chunk indicates at or past EOF, false otherwise (also if file size is unknown)
    /// @note only works for remote when a valid p44header has been parsed before
    bool isEOFforChunk(uint32_t aChunkIndex, bool aRemotely);

    /// @param aChunkIndex an index (in terms of chunks as big as possible for one PDU) starting at 0
    /// @param aFileNo receives the file number (base number plus possible segment offset)
    /// @param aRecordNo receives the record number
    /// @param aNumRecords receives the number of records (not bytes!)
    void addressForMaxChunk(uint32_t aChunkIndex, uint16_t& aFileNo, uint16_t& aRecordNo, uint16_t& aNumRecords);

    /// @return number of records (16-bit quantities) in the P44 header
    uint16_t numP44HeaderRecords();

    /// get address data for next block to retransmit from p44header
    /// @param aFileNo receives the file number (base number plus possible segment offset)
    /// @param aRecordNo receives the record number
    /// @param aNumRecords receives the number of records (not bytes!)
    /// @return true if there is a block to re-transmit, false if not (or no p44header)
    bool addrForNextRetransmit(uint16_t& aFileNo, uint16_t& aRecordNo, uint16_t& aNumRecords);

    /// Check if the file integrity is ok (by comparing remote and local sizes and CRCs)
    /// @return true if integrity can be assumed
    bool fileIntegrityOK();

  private:

    string filePathFor(uint16_t aFileNo, bool aTemp);
    uint16_t baseFileNoFor(uint16_t aFileNo);


  };


  class ModbusMaster : public ModbusConnection
  {
    typedef ModbusConnection inherited;

  public:

    typedef std::list<uint8_t> SlaveAddrList;

    ModbusMaster();
    virtual ~ModbusMaster();

    /// read single register
    /// @param aRegAddr the register address
    /// @param aRegData will receive the register's data
    /// @return error, if any
    /// @note if no connection already exists, connection will be opened before and closed after the call
    ErrorPtr readRegister(int aRegAddr, uint16_t &aRegData);

    /// read float register pair
    /// @param aRegAddr address of the first of two registers containing a float value
    /// @param aFloatData will receive the float data
    /// @return error, if any
    /// @note if no connection already exists, connection will be opened before and closed after the call
    /// @note the byte order in the registers must match the mode set with setFloatMode()
    ErrorPtr readFloatRegister(int aRegAddr, double &aFloatData);

    /// read multiple registers
    /// @param aRegAddr the register address of the first register
    /// @param aNumRegs how many consecutive registers to read
    /// @param aRegsP pointer to where to store the register data (must have room for aNumRegs registers)
    /// @return error, if any
    /// @note if no connection already exists, connection will be opened before and closed after the call
    ErrorPtr readRegisters(int aRegAddr, int aNumRegs, uint16_t *aRegsP);

    /// write single register
    /// @param aRegAddr the register address
    /// @param aRegData data to write to the register
    /// @return error, if any
    /// @note if no connection already exists, connection will be opened before and closed after the call
    ErrorPtr writeRegister(int aRegAddr, uint16_t aRegData);

    /// write float register pair
    /// @param aRegAddr address of the first of two registers to store a float value
    /// @param aFloatData the float data to write to the two registers
    /// @return error, if any
    /// @note if no connection already exists, connection will be opened before and closed after the call
    /// @note the byte order in the registers must match the mode set with setFloatMode()
    ErrorPtr writeFloatRegister(int aRegAddr, double aFloatData);

    /// write multiple registers
    /// @param aRegAddr the register address of the first register
    /// @param aNumRegs how many consecutive registers to write
    /// @param aRegsP pointer to data to write to the registers (must be data for aNumRegs registers)
    /// @return error, if any
    /// @note if no connection already exists, connection will be opened before and closed after the call
    ErrorPtr writeRegisters(int aRegAddr, int aNumRegs, const uint16_t *aRegsP);

    /// request slave info (ID and run indicator)
    /// @param aId will be set to the id returned from the slave
    /// @param aRunIndicator will be set to the run indicator status from the slave
    /// @note the id is device specific - usually it is a string but it could be any sequence of bytes
    ErrorPtr readSlaveInfo(string& aId, bool& aRunIndicator);

    /// find slaves matching slaveInfo on the bus and return list
    /// @param aSlaveAddrList will receive the list of slaves
    /// @param aMatchString if not empty, only slaves where this is contained in the slave's iD string are returned
    /// @param aFirstAddr first slave address to try
    /// @param aLastAddr last slave address to try
    /// @return OK or error
    /// @note implementation will block, probably for a long time with large files and/or unreliable connection.
    ///   Not suitable to run from mainloop, use a thread!
    ErrorPtr findSlaves(SlaveAddrList& aSlaveAddrList, string aMatchString, int aFirstAddr=1, int aLastAddr=0xFF);

    /// write file records
    /// @param aFileNo the file number
    /// @param aFirstRecordNo the starting (or only) record number
    /// @param aNumRecords the number of (16bit) records to write to remote party
    /// @param aDataP the data (must have at least aNumRecords*2 bytes)
    /// @return ok or error
    ErrorPtr writeFileRecords(uint16_t aFileNo, uint16_t aFirstRecordNo, uint16_t aNumRecords, const uint8_t* aDataP);

    /// read file records
    /// @param aFileNo the file number
    /// @param aFirstRecordNo the starting (or only) record number
    /// @param aNumRecords the number of (16bit) records to read from remote party
    /// @param aDataP buffer for received data (must have at least aNumRecords*2 bytes)
    /// @return ok or error
    ErrorPtr readFileRecords(uint16_t aFileNo, uint16_t aFirstRecordNo, uint16_t aNumRecords, uint8_t* aDataP);

    /// send entire file via modbus
    /// @param aLocalFilePath the local file to be sent
    /// @param aFileNo the file number in the remote slave device (or devices, when sending as broadcast)
    /// @param aUseP44Header if set, the P44 header containing file size and a CRC is sent first, to allow the
    ///   remote party to check file integrity and exact file length (modbus length is always a multiple of 2).
    ///   For broadcast transfers with P44 header, the remote will track successfully received.
    /// @return OK or error
    /// @note implementation will block, probably for a long time with large files and/or unreliable connection.
    ///   Not suitable to run from mainloop, use a thread!
    ErrorPtr sendFile(const string& aLocalFilePath, int aFileNo, bool aUseP44Header);

    /// read a file via modbus
    /// @param aLocalFilePath the local file to store the file read from remote
    /// @param aFileNo the file number in the remote slave device
    /// @param aUseP44Header if set, the remote file is expected to have a P44 header, which is read first to get
    ///    file size and
    /// @return OK or error
    /// @note implementation will block, probably for a long time with large files and/or unreliable connection.
    ///   Not suitable to run from mainloop, use a thread!
    ErrorPtr receiveFile(const string& aLocalFilePath, int aFileNo, bool aUseP44Header);

    /// broadcast a file via modbus to more than one slave
    /// @param aSlaveAddrList list of slaves to send file to
    /// @param aLocalFilePath the local file to be sent
    /// @param aFileNo the file number in the remote slave devices
    /// @param aUseP44Header if set, the P44 header containing file size and a CRC is sent first, to allow the
    ///   remote parties to check file integrity. This also allows sending the actual file data via broadcast requests
    ///   and check for missing parts with each client afterwards.
    /// @return OK or error
    /// @note implementation will block, probably for a long time with large files and/or unreliable connection.
    ///   Not suitable to run from mainloop, use a thread!
    ErrorPtr broadcastFile(const SlaveAddrList& aSlaveAddrList, const string& aLocalFilePath, int aFileNo, bool aUseP44Header);

  private:

    ErrorPtr sendFile(ModbusFileHandlerPtr aHandler, int aFileNo);


  };
  typedef boost::intrusive_ptr<ModbusMaster> ModbusMasterPtr;



  /// callback for accessed registers in the register model
  /// @param aAddress the modbus register or bit address
  /// @param aBit if true, aAddress is a bit address; if false, aAddress is a register address
  /// @param aInput if true, a read-only bit or input register is accessed; if false, a read-write coil bit or holding register is accessed
  /// @param aWrite if true, it is a write access and aValue contains the newly written data;
  ///   if false, it is a read access and aValue might be updated to return actualized data (otherwise, current register/bit content is returned)
  /// @return return Error object in case the access has failed, ErrorPtr() otherwise
  /// @note use the register model accessor functions to update/read the registers
  typedef boost::function<ErrorPtr (int aAddress, bool aBit, bool aInput, bool aWrite)> ModbusValueAccessCB;

  /// Raw modbus request handler
  typedef boost::function<bool (sft_t &sft, int offset, const ModBusPDU& req, int req_length, ModBusPDU& rsp, int &rsp_length)> ModbusReqCB;

  class ModbusSlave : public ModbusConnection
  {
    typedef ModbusConnection inherited;

    string slaveId;
    modbus_mapping_t* registerModel;
    ModbusValueAccessCB valueAccessHandler;
    ModbusReqCB rawRequestHandler;

    modbus_rcv_t *modbusRcv;

    ModBusPDU modbusReq;
    ModBusPDU modbusRsp;
    MLTicket rcvTimeoutTicket;

    string errStr; ///< holds error string for libmodbus to access a c_str after handler returns

    typedef std::list<ModbusFileHandlerPtr> FileHandlersList;
    FileHandlersList fileHandlers;

  public:

    ModbusSlave();
    virtual ~ModbusSlave();

    /// set the text to be returned by "Report Server/Slave ID"
    void setSlaveId(const string aSlaveId);

    /// define the register model for this slave
    ErrorPtr setRegisterModel(
      int aFirstCoil, int aNumCoils, // I/O bits = coils
      int aFirstBit, int aNumBits, // input bits
      int aFirstReg, int aNumRegs, // R/W registers
      int aFirstInp, int aNumInps // read only registers
    );

    /// free the register model
    void freeRegisterModel();

    /// add a file handler
    /// @param aFileHandler a file handler object to be used for handling READ_FILE_RECORD/WRITE_FILE_RECORD requests
    /// @return the file handler added (same as aFileHandler)
    ModbusFileHandlerPtr addFileHandler(ModbusFileHandlerPtr aFileHandler);

    /// close the modbus connection
    virtual void close() P44_OVERRIDE;

    /// stop listening for incoming messages
    void stopServing();

    /// set a register model access handler
    /// @param aValueAccessCB is called whenever a register or bit is accessed
    /// @note this is called for every single bit or register access once, even if multiple registers are read/written in the same transaction
    void setValueAccessHandler(ModbusValueAccessCB aValueAccessCB);

    /// @name register model accessors
    /// @{

    /// get value from internal bits/register model
    /// @param aAddress the modbus register address
    /// @param aBit if true, aAddress specifies a coil or input bit, if false, a holding or input register
    /// @param aInput if true, aAddress specifies a read-only register or bit; if false, aAddress specifies a read-write register or bit
    /// @return register or bit value
    /// @note if invalid address is specified, result is 0
    uint16_t getValue(int aAddress, bool aBit, bool aInput);

    /// get register value from internal register model
    /// @param aAddress the modbus register address
    /// @param aInput if true, aAddress specifies a read-only input register; if false, aAddress specifies a read-write holding register
    /// @return register value
    /// @note if invalid address is specified, result is 0
    uint16_t getReg(int aAddress, bool aInput);

    /// set register value in internal register model
    /// @param aAddress the modbus register address
    /// @param aInput if true, aAddress specifies a read-only input register; if false, aAddress specifies a read-write holding register
    /// @param aRegValue the new register value to write
    /// @note if invalid address is specified, nothing happens
    void setReg(int aAddress, bool aInput, uint16_t aRegValue);

    /// get floating point value from a pair of registers in the internal register model
    /// @param aAddress first of two modbus register addresses representing a float value
    /// @param aInput if true, aAddress specifies s read-only input register pair; if false, aAddress specifies a read-write holding register pair
    /// @return floating point value represented by register pair
    /// @note the byte order in the registers must match the mode set with setFloatMode()
    /// @note if invalid address is specified, result is 0
    double getFloatReg(int aAddress, bool aInput);

    /// set floating point value into a pair of registers in the internal register model
    /// @param aAddress first of two modbus (holding) register addresses representing a float value
    /// @param aInput if true, aAddress specifies s read-only input register pair; if false, aAddress specifies a read-write holding register pair
    /// @note the byte order in the registers must match the mode set with setFloatMode()
    /// @note if invalid address is specified, nothing happens
    void setFloatReg(int aAddress, bool aInput, double aFloatValue);

    /// get bit value from internal register model
    /// @param aAddress the modbus bit address
    /// @param aInput if true, aAddress specifies a read-only bit; if false, aAddress specifies a read-write coil bit
    /// @return bit state
    /// @note if invalid address is specified, result is false
    bool getBit(int aAddress, bool aInput);

    /// set bit value in internal register model
    /// @param aAddress the modbus bit address
    /// @param aInput if true, aAddress specifies a read-only bit; if false, aAddress specifies a read-write coil bit
    /// @param aBitValue the new bit state to write
    /// @note if invalid address is specified, nothing happens
    void setBit(int aAddress, bool aInput, bool aBitValue);

    /// @}


  protected:

    /// is called when the "modbus" member variable is ready with a newly set-up context
    virtual void mbContextReady() P44_OVERRIDE;

    /// start receiving messages as server/slave if possible
    virtual void startServing() P44_OVERRIDE;

  private:

    uint8_t* getBitAddress(int aAddress, bool aInput, int aBits);
    uint16_t* getRegisterAddress(int aAddress, bool aInput, int aRegs);

    void cancelMsgReception();
    void startMsgReception();
    bool modbusFdPollHandler(int aFD, int aPollFlags);
    void modbusTimeoutHandler();
    void modbusRecoveryHandler();

    bool handleFileAccess(sft_t &aSft, int aOffset, const ModBusPDU& aReq, int aReqLen, ModBusPDU& aRsp, int &aRspLen);

  public:

    // stuff that needs to be public because friend declaration does not work in gcc (does in clang)
    int handleRawRequest(sft_t &aSft, int aOffset, const ModBusPDU& aReq, int aReqLen, ModBusPDU& aRsp);
    const char* accessHandler(modbus_data_access_t aAccess, int aAddr, int aCnt, modbus_data_t aDataP);

  };
  typedef boost::intrusive_ptr<ModbusSlave> ModbusSlavePtr;

  extern "C" {
    int modbus_slave_function_handler(modbus_t* ctx, sft_t *sft, int offset, const uint8_t *req, int req_length, uint8_t *rsp, void *user_ctx);
    const char *modbus_access_handler(modbus_t* ctx, modbus_mapping_t* mappings, modbus_data_access_t access, int addr, int cnt, modbus_data_t dataP, void *user_ctx);
  }


} // namespace p44

#endif // ENABLE_MODBUS
#endif // __p44utils__modbus__
