//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "serialcomm.hpp"

#include <sys/ioctl.h>

using namespace p44;


SerialComm::SerialComm(MainLoop &aMainLoop) :
	inherited(aMainLoop),
  connectionPort(0),
  baudRate(9600),
  charSize(8),
  parityEnable(false),
  evenParity(false),
  twoStopBits(false),
  hardwareHandshake(false),
  connectionOpen(false),
  reconnecting(false)
{
}


SerialComm::~SerialComm()
{
  closeConnection();
}



bool SerialComm::parseConnectionSpecification(
  const char* aConnectionSpec, uint16_t aDefaultPort, const char *aDefaultCommParams,
  string &aConnectionPath,
  int &aBaudRate,
  int &aCharSize,
  bool &aParityEnable,
  bool &aEvenParity,
  bool &aTwoStopBits,
  bool &aHardwareHandshake,
  uint16_t &aConnectionPort
)
{
  // device or IP host?
  aConnectionPort = 0; // means: serial
  aConnectionPath.clear();
  aBaudRate = 9600;
  aCharSize = 8;
  aParityEnable = false;
  aEvenParity = false;
  aTwoStopBits = false;
  aHardwareHandshake = false;
  if (aConnectionSpec && *aConnectionSpec) {
    aConnectionPath = aConnectionSpec;
    if (aConnectionSpec[0]=='/') {
      // serial device
      string opt = nonNullCStr(aDefaultCommParams);
      size_t n = aConnectionPath.find(":");
      if (n!=string::npos) {
        // explicit specification of communication params: baudrate, bits, parity
        opt = aConnectionPath.substr(n+1,string::npos);
        aConnectionPath.erase(n,string::npos);
      }
      if (opt.size()>0) {
        // get communication options: [baud rate][,[bits][,[parity][,[stopbits][,[H]]]]]
        string part;
        const char *p = opt.c_str();
        if (nextPart(p, part, ',')) {
          // baud rate
          sscanf(part.c_str(), "%d", &aBaudRate);
          if (nextPart(p, part, ',')) {
            // bits
            sscanf(part.c_str(), "%d", &aCharSize);
            if (nextPart(p, part, ',')) {
              // parity: O,E,N
              if (part.size()>0) {
                aParityEnable = false;
                if (part[0]=='E') {
                  aParityEnable = true;
                  aEvenParity = true;
                }
                else if (part[0]=='O') {
                  aParityEnable = false;
                  aEvenParity = false;
                }
              }
              if (nextPart(p, part, ',')) {
                // stopbits: 1 or 2
                if (part.size()>0) {
                  aTwoStopBits = part[0]=='2';
                }
                if (nextPart(p, part, ',')) {
                  // hardware handshake?
                  if (part.size()>0) {
                    aHardwareHandshake = part[0]=='H';
                  }
                }
              }
            }
          }
        }
      }
      return true; // real serial
    }
    else {
      // IP host
      aConnectionPort = aDefaultPort; // set default in case aConnectionSpec does not have a path number
      splitHost(aConnectionSpec, &aConnectionPath, &aConnectionPort);
      return false; // no real serial
    }
  }
  return false; // no real serial either
}



void SerialComm::setConnectionSpecification(const char* aConnectionSpec, uint16_t aDefaultPort, const char *aDefaultCommParams)
{
  parseConnectionSpecification(
    aConnectionSpec, aDefaultPort, aDefaultCommParams,
    connectionPath,
    baudRate,
    charSize,
    parityEnable,
    evenParity,
    twoStopBits,
    hardwareHandshake,
    connectionPort
  );
  closeConnection();
}


ErrorPtr SerialComm::establishConnection()
{
  if (!connectionOpen) {
    // Open connection to bridge
    connectionFd = 0;
    int res;
    struct termios newtio;
    serialConnection = connectionPath[0]=='/';
    // check type of input
    if (serialConnection) {
      // convert the baudrate
      int baudRateCode = 0;
      switch (baudRate) {
        case 50 : baudRateCode = B50; break;
        case 75 : baudRateCode = B75; break;
        case 110 : baudRateCode = B110; break;
        case 134 : baudRateCode = B134; break;
        case 150 : baudRateCode = B150; break;
        case 200 : baudRateCode = B200; break;
        case 300 : baudRateCode = B300; break;
        case 600 : baudRateCode = B600; break;
        case 1200 : baudRateCode = B1200; break;
        case 1800 : baudRateCode = B1800; break;
        case 2400 : baudRateCode = B2400; break;
        case 4800 : baudRateCode = B4800; break;
        case 9600 : baudRateCode = B9600; break;
        case 19200 : baudRateCode = B19200; break;
        case 38400 : baudRateCode = B38400; break;
        case 57600 : baudRateCode = B57600; break;
        case 115200 : baudRateCode = B115200; break;
        case 230400 : baudRateCode = B230400; break;
      }
      if (baudRateCode==0) {
        return ErrorPtr(new SerialCommError(SerialCommError::UnknownBaudrate));
      }
      // assume it's a serial port
      connectionFd = open(connectionPath.c_str(), O_RDWR | O_NOCTTY);
      if (connectionFd<0) {
        return SysError::errNo("Cannot open serial port: ");
      }
      tcgetattr(connectionFd,&oldTermIO); // save current port settings
      // see "man termios" for details
      memset(&newtio, 0, sizeof(newtio));
      // - 8-N-1,
      newtio.c_cflag =
        CLOCAL | CREAD | // no modem control lines (local), reading enabled
        (charSize==5 ? CS5 : (charSize==6 ? CS6 : (charSize==7 ? CS7 : CS8))) | // char size
        (twoStopBits ? CSTOPB : 0) | // stop bits
        (parityEnable ? PARENB | (evenParity ? 0 : PARODD) : 0) | // parity
        (hardwareHandshake ? CRTSCTS : 0); // hardware handshake
      // - ignore parity errors
      newtio.c_iflag =
        parityEnable ? INPCK : IGNPAR; // check or ignore parity
      // - no output control
      newtio.c_oflag = 0;
      // - no input control (non-canonical)
      newtio.c_lflag = 0;
      // - no inter-char time
      newtio.c_cc[VTIME]    = 0;   /* inter-character timer unused */
      // - receive every single char seperately
      newtio.c_cc[VMIN]     = 1;   /* blocking read until 1 chars received */
      // - set speed (as this ors into c_cflag, this must be after setting c_cflag initial value)
      cfsetspeed(&newtio, baudRateCode);
      // - set new params
      tcflush(connectionFd, TCIFLUSH);
      tcsetattr(connectionFd,TCSANOW,&newtio);
    }
    else {
      // assume it's an IP address or hostname
      struct sockaddr_in conn_addr;
      if ((connectionFd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        return SysError::errNo("Cannot create socket: ");
      }
      // prepare IP address
      memset(&conn_addr, '0', sizeof(conn_addr));
      conn_addr.sin_family = AF_INET;
      conn_addr.sin_port = htons(connectionPort);
      struct hostent *server;
      server = gethostbyname(connectionPath.c_str());
      if (server == NULL) {
        close(connectionFd);
        return ErrorPtr(new SerialCommError(SerialCommError::InvalidHost));
      }
      memcpy((void *)&conn_addr.sin_addr.s_addr, (void *)(server->h_addr), sizeof(in_addr_t));
      if ((res = connect(connectionFd, (struct sockaddr *)&conn_addr, sizeof(conn_addr))) < 0) {
        close(connectionFd);
        return SysError::errNo("Cannot open socket: ");
      }
    }
    // successfully opened
    connectionOpen = true;
		// now set FD for FdComm to monitor
		setFd(connectionFd);
  }
  reconnecting = false; // successfully opened, don't try to reconnect any more
  return ErrorPtr(); // ok
}


bool SerialComm::requestConnection()
{
  ErrorPtr err = establishConnection();
  if (Error::notOK(err)) {
    if (!reconnecting) {
      LOG(LOG_ERR, "SerialComm: requestConnection() could not open connection now: %s -> entering background retry mode", err->text());
      reconnecting = true;
      reconnectTicket.executeOnce(boost::bind(&SerialComm::reconnectHandler, this), 5*Second);
    }
    return false;
  }
  return true;
}




void SerialComm::closeConnection()
{
  reconnecting = false; // explicit close, don't try to reconnect any more
  if (connectionOpen) {
		// stop monitoring
		setFd(-1);
    // restore IO settings
    if (serialConnection) {
      tcsetattr(connectionFd,TCSANOW,&oldTermIO);
    }
    // close
    close(connectionFd);
    // closed
    connectionOpen = false;
  }
}


bool SerialComm::connectionIsOpen()
{
  return connectionOpen;
}


// MARK: - break

void SerialComm::sendBreak()
{
  if (!connectionIsOpen() || !serialConnection) return; // ignore
  tcsendbreak(connectionFd, 0); // send standard break, which should be >=0.25sec and <=0.5sec
}


// MARK: - handshake signal control

void SerialComm::setDTR(bool aActive)
{
  if (!connectionIsOpen() || !serialConnection) return; // ignore
  int iFlags = TIOCM_DTR;
  ioctl(connectionFd, aActive ? TIOCMBIS : TIOCMBIC, &iFlags);
}


void SerialComm::setRTS(bool aActive)
{
  if (!connectionIsOpen() || !serialConnection) return; // ignore
  int iFlags = TIOCM_RTS;
  ioctl(connectionFd, aActive ? TIOCMBIS : TIOCMBIC, &iFlags);
}


// MARK: - handling data exception


void SerialComm::dataExceptionHandler(int aFd, int aPollFlags)
{
  DBGLOG(LOG_DEBUG, "SerialComm::dataExceptionHandler(fd==%d, pollflags==0x%X)", aFd, aPollFlags);
  bool reEstablish = false;
  if (aPollFlags & POLLHUP) {
    // other end has closed connection
    LOG(LOG_ERR, "SerialComm: serial connection was hung up unexpectely");
    reEstablish = true;
  }
  else if (aPollFlags & POLLIN) {
    // Note: on linux a socket closed server side does not return POLLHUP, but POLLIN with no data
    // alerted for read, but nothing to read any more: assume connection closed
    LOG(LOG_ERR, "SerialComm: serial connection returns POLLIN with no data: assuming connection broken");
    reEstablish = true;
  }
  else if (aPollFlags & POLLERR) {
    // error
    LOG(LOG_ERR, "SerialComm: error on serial connection: assuming connection broken");
    reEstablish = true;
  }
  // in case of error, close and re-open connection
  if (reEstablish && !reconnecting) {
    LOG(LOG_ERR, "SerialComm: closing and re-opening connection in attempt to re-establish it after error");
    closeConnection();
    // try re-opening right now
    reconnecting = true;
    reconnectHandler();
  }
}


void SerialComm::reconnectHandler()
{
  if (reconnecting) {
    ErrorPtr err = establishConnection();
    if (Error::notOK(err)) {
      LOG(LOG_ERR, "SerialComm: re-connect failed: %s -> retry again later", err->text());
      reconnecting = true;
      reconnectTicket.executeOnce(boost::bind(&SerialComm::reconnectHandler, this), 15*Second);
    }
    else {
      LOG(LOG_NOTICE, "SerialComm: successfully reconnected to %s", connectionPath.c_str());
    }
  }
}


