//
//  Copyright (c) 2013-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#include "application.hpp"

#include "mainloop.hpp"

#include <sys/stat.h> // for umask
#include <sys/signal.h>

#define TEMP_DIR_PATH "/tmp"

using namespace p44;

// MARK: - Application base class

static Application *sharedApplicationP = NULL;


Application *Application::sharedApplication()
{
  return sharedApplicationP;
}


bool Application::isRunning()
{
  if (sharedApplicationP) {
    return sharedApplicationP->mainLoop.isRunning();
  }
  return false; // no app -> not running
}


bool Application::isTerminated()
{
  if (sharedApplicationP) {
    return sharedApplicationP->mainLoop.isTerminated();
  }
  return true; // no app -> consider terminated as well
}



Application::Application(MainLoop &aMainLoop) :
  mainLoop(aMainLoop)
{
  initializeInternal();
}


Application::Application() :
  mainLoop(MainLoop::currentMainLoop())
{
  initializeInternal();
}


Application::~Application()
{
  sharedApplicationP = NULL;
}


void Application::initializeInternal()
{
  resourcepath = "."; // current directory by default
  datapath = TEMP_DIR_PATH; // tmp by default
  // "publish" singleton
  sharedApplicationP = this;
  // register signal handlers
  handleSignal(SIGHUP);
  handleSignal(SIGINT);
  handleSignal(SIGTERM);
  handleSignal(SIGUSR1);
  // make sure we have default SIGCHLD handling
  // - with SIGCHLD ignored, waitpid() cannot catch children's exit status!
  // - SIGCHLD ignored status is inherited via execve(), so if caller of execve
  //   does not restore SIGCHLD to SIG_DFL and execs us, we could be in SIG_IGN
  //   state now - that's why we set it now!
  signal(SIGCHLD, SIG_DFL);
}


void Application::sigaction_handler(int aSignal, siginfo_t *aSiginfo, void *aUap)
{
  if (sharedApplicationP) {
    sharedApplicationP->signalOccurred(aSignal, aSiginfo);
  }
}


void Application::handleSignal(int aSignal)
{
  struct sigaction act;

  memset(&act, 0, sizeof(act));
  act.sa_sigaction = Application::sigaction_handler;
  act.sa_flags = SA_SIGINFO;
  sigaction (aSignal, &act, NULL);
}


int Application::main(int argc, char **argv)
{
	// NOP application
	return EXIT_SUCCESS;
}


void Application::initialize()
{
  // NOP in base class
}


void Application::cleanup(int aExitCode)
{
  // NOP in base class
}


void Application::signalOccurred(int aSignal, siginfo_t *aSiginfo)
{
  if (aSignal==SIGUSR1) {
    // default for SIGUSR1 is showing mainloop statistics
    LOG(LOG_NOTICE, "SIGUSR1 requests %s", mainLoop.description().c_str());
    mainLoop.statistics_reset();
    return;
  }
  // default action for all other signals is terminating the program
  LOG(LOG_ERR, "Terminating because pid %d sent signal %d", aSiginfo->si_pid, aSignal);
  mainLoop.terminate(EXIT_FAILURE);
}


int Application::run()
{
	// schedule the initialize() method as first mainloop method
	mainLoop.executeNow(boost::bind(&Application::initialize, this));
	// run the mainloop
	int exitCode = mainLoop.run();
  // show the statistic
  LOG(LOG_INFO, "Terminated: %s", mainLoop.description().c_str());
  // clean up
  cleanup(exitCode);
  // done
  return exitCode;
}


void Application::terminateApp(int aExitCode)
{
  // have mainloop terminate with given exit code and exit run()
  mainLoop.terminate(aExitCode);
}



void Application::terminateAppWith(ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    mainLoop.terminate(EXIT_SUCCESS);
  }
  else {
    LOG(LOG_ERR, "Terminating because of error: %s", aError->description().c_str());
    mainLoop.terminate(EXIT_FAILURE);
  }
}


string Application::resourcePath(const string aResource)
{
  if (aResource.empty())
    return resourcepath; // just return resource path
  if (aResource[0]=='/')
    return aResource; // argument is absolute path, use it as-is
  // relative to resource directory
  return resourcepath + "/" + aResource;
}


void Application::setResourcePath(const char *aResourcePath)
{
  resourcepath = aResourcePath;
  if (resourcepath.size()>1 && resourcepath[resourcepath.size()-1]=='/') {
    resourcepath.erase(resourcepath.size()-1);
  }
}


string Application::dataPath(const string aDataFile)
{
  if (aDataFile.empty())
    return datapath; // just return data path
  if (aDataFile[0]=='/')
    return aDataFile; // argument is absolute path, use it as-is
  // relative to data directory
  return datapath + "/" + aDataFile;
}


void Application::setDataPath(const char *aDataPath)
{
  datapath = aDataPath;
  if (datapath.size()>1 && datapath[datapath.size()-1]=='/') {
    datapath.erase(datapath.size()-1);
  }
}


string Application::tempPath(const string aTempFile)
{
  if (aTempFile.empty())
    return TEMP_DIR_PATH; // just return data path
  if (aTempFile[0]=='/')
    return aTempFile; // argument is absolute path, use it as-is
  // relative to temp directory
  return TEMP_DIR_PATH "/" + aTempFile;
}



string Application::version() const
{
  #if defined(P44_APPLICATION_VERSION)
  return P44_APPLICATION_VERSION; // specific version number
  #elif defined(PACKAGE_VERSION)
  return PACKAGE_VERSION; // automake package version number
  #else
  return "unknown_version"; // none known
  #endif
}


void Application::daemonize()
{
  pid_t pid, sid;

  /* already a daemon */
  if ( getppid() == 1 ) return;

  /* Fork off the parent process */
  pid = fork();
  if (pid < 0) {
    exit(EXIT_FAILURE);
  }
  /* If we got a good PID, then we can exit the parent process. */
  if (pid > 0) {
    exit(EXIT_SUCCESS);
  }

  /* At this point we are executing as the child process */

  /* Change the file mode mask */
  umask(0);

  /* Create a new SID for the child process */
  sid = setsid();
  if (sid < 0) {
    exit(EXIT_FAILURE);
  }

  /* Change the current working directory.  This prevents the current
	 directory from being locked; hence not being able to remove it. */
  if ((chdir("/")) < 0) {
    exit(EXIT_FAILURE);
  }

  /* Redirect standard files to /dev/null */
  freopen( "/dev/null", "r", stdin);
  freopen( "/dev/null", "w", stdout);
  freopen( "/dev/null", "w", stderr);
}


// MARK: - CmdLineApp command line application


/// constructor
CmdLineApp::CmdLineApp(MainLoop &aMainLoop) :
  inherited(aMainLoop),
  optionDescriptors(NULL)
{
}

CmdLineApp::CmdLineApp() :
  optionDescriptors(NULL)
{
}


/// destructor
CmdLineApp::~CmdLineApp()
{
}


CmdLineApp *CmdLineApp::sharedCmdLineApp()
{
  return dynamic_cast<CmdLineApp *>(Application::sharedApplication());
}


void CmdLineApp::setCommandDescriptors(const char *aSynopsis, const CmdLineOptionDescriptor *aOptionDescriptors)
{
  optionDescriptors = aOptionDescriptors;
  synopsis = aSynopsis ? aSynopsis : "Usage: %1$s";
}


#define MAX_INDENT 40
#define MAX_LINELEN 100

void CmdLineApp::showUsage()
{
  // print synopsis
  fprintf(stderr, synopsis.c_str(), invocationName.c_str());
  // print options
  int numDocumentedOptions = 0;
  // - calculate indent
  ssize_t indent = 0;
  const CmdLineOptionDescriptor *optionDescP = optionDescriptors;
  bool anyShortOpts = false;
  while (optionDescP && (optionDescP->longOptionName!=NULL || optionDescP->shortOptionChar!='\x00')) {
    const char *desc = optionDescP->optionDescription;
    if (desc) {
      // documented option
      numDocumentedOptions++;
      if (optionDescP->shortOptionChar) {
        anyShortOpts = true;
      }
      size_t n = 0;
      if (optionDescP->longOptionName) {
        n += strlen(optionDescP->longOptionName)+2; // "--XXXXX"
      }
      if (optionDescP->withArgument) {
        const char *p = strchr(desc, ';');
        if (p) {
          n += 3 + (p-desc); // add room for argument description
        }
      }
      if (n>indent) indent = n; // new max
    }
    optionDescP++;
  }
  if (anyShortOpts) indent += 4; // "-X, " prefix
  indent += 2 + 2; // two at beginning, two at end
  if (indent>MAX_INDENT) indent = MAX_INDENT;
  // - print options
  if (numDocumentedOptions>0) {
    fprintf(stderr, "Options:\n");
    optionDescP = optionDescriptors;
    while (optionDescP && (optionDescP->longOptionName!=NULL || optionDescP->shortOptionChar!='\x00')) {
      //  fprintf(stderr, "\n");
      const char *desc = optionDescP->optionDescription;
      if (desc) {
        ssize_t remaining = indent;
        fprintf(stderr, "  "); // start indent
        remaining -= 2;
        if (anyShortOpts) {
          // short names exist, print them for those options that have them
          if (optionDescP->shortOptionChar)
            fprintf(stderr, "-%c", optionDescP->shortOptionChar);
          else
            fprintf(stderr, "  ");
          remaining -= 2;
          if (optionDescP->longOptionName) {
            // long option follows, fill up
            if (optionDescP->shortOptionChar)
              fprintf(stderr, ", ");
            else
              fprintf(stderr, "  ");
            remaining -= 2;
          }
        }
        // long name
        if (optionDescP->longOptionName) {
          fprintf(stderr, "--%s", optionDescP->longOptionName);
          remaining -= strlen(optionDescP->longOptionName)+2;
        }
        // argument
        if (optionDescP->withArgument) {
          const char *p = strchr(desc, ';');
          if (p) {
            size_t n = (p-desc);
            string argDesc(desc,n);
            fprintf(stderr, " <%s>", argDesc.c_str());
            remaining -= argDesc.length()+3;
            desc += n+1; // desc starts after semicolon
          }
        }
        // complete first line indent
        if (remaining>0) {
          while (remaining-- > 0) fprintf(stderr, " ");
        }
        else {
          fprintf(stderr, "  "); // just two spaces
          remaining -= 2; // count these
        }
        // print option description, properly indented and word-wrapped
        // Note: remaining is 0 or negative in case arguments reached beyond indent
        if (desc) {
          ssize_t listindent = 0;
          while (*desc) {
            ssize_t ll = MAX_LINELEN-indent+remaining;
            remaining = 0;
            ssize_t l = 0;
            ssize_t lastWs = -1;
            // scan for list indent
            if (*desc=='-') {
              // next non-space is list indent
              while (desc[++listindent]==' ');
            }
            // scan for end of text, last space or line end
            const char *e = desc;
            while (*e) {
              if (*e==' ') lastWs = l;
              else if (*e=='\n') {
                // explicit line break
                listindent = 0;
                break;
              }
              // check line lenght
              l++;
              if (l>=ll) {
                // line gets too long, break at previous space
                if (lastWs>0) {
                  // reposition end
                  e = desc+lastWs;
                }
                break;
              }
              // next
              e++;
            }
            // e now points to either LF, or breaking space, or NUL (end of text)
            // - output chars between desc and e
            while (desc<e) fprintf(stderr, "%c", *desc++);
            // - if not end of text, insert line break and new indent
            if (*desc) {
              // there is a next line
              fprintf(stderr, "\n");
              // indent
              ssize_t r = indent+listindent;
              while (r-- > 0) fprintf(stderr, " ");
              desc++; // skip the LF or space that caused the line end
            }
          }
        }
        // end of option, next line
        fprintf(stderr, "\n");
      }
      // next option
      optionDescP++;
    }
  } // if any options to show
  fprintf(stderr, "\n");
}


void CmdLineApp::parseCommandLine(int aArgc, char **aArgv)
{
  if (aArgc>0) {
    invocationName = aArgv[0];
    int rawArgIndex=1;
    while(rawArgIndex<aArgc) {
      const char *argP = aArgv[rawArgIndex];
      if (*argP=='-') {
        // option argument
        argP++;
        bool longOpt = false;
        string optName;
        string optArg;
        bool optArgFound = false;
        if (*argP=='-') {
          // long option
          longOpt = true;
          optName = argP+1;
        }
        else {
          // short option
          optName = argP;
          if (optName.length()>1 && optName[1]!='=') {
            // option argument follows directly after single char option
            optArgFound = true; // is non-empty by definition
            optArg = optName.substr(1,string::npos);
            optName.erase(1,string::npos);
          }
        }
        // search for option argument directly following option separated by equal sign
        string::size_type n = optName.find("=");
        if (n!=string::npos) {
          optArgFound = true; // explicit specification, counts as option argument even if empty string
          optArg = optName.substr(n+1,string::npos);
          optName.erase(n,string::npos);
        }
        // search for option descriptor
        const CmdLineOptionDescriptor *optionDescP = optionDescriptors;
        bool optionFound = false;
        while (optionDescP && (optionDescP->longOptionName!=NULL || optionDescP->shortOptionChar!='\x00')) {
          // not yet end of descriptor list
          if (
            (longOpt && optName==optionDescP->longOptionName) ||
            (!longOpt && optName[0]==optionDescP->shortOptionChar)
          ) {
            // option match found
            if (!optionDescP->withArgument) {
              // option without argument
              if (optArgFound) {
                fprintf(stderr, "Option '%s' does not expect an argument\n", optName.c_str());
                showUsage();
                terminateApp(EXIT_FAILURE);
              }
            }
            else {
              // option with argument
              if (!optArgFound) {
                // check for next arg as option arg
                if (rawArgIndex<aArgc-1) {
                  // there is a next argument, use it as option argument
                  optArgFound = true;
                  optArg = aArgv[++rawArgIndex];
                }
              }
              if (!optArgFound) {
                fprintf(stderr, "Option '%s' requires an argument\n", optName.c_str());
                showUsage();
                terminateApp(EXIT_FAILURE);
              }
            }
            // now have option processed by subclass
            if (!processOption(*optionDescP, optArg.c_str())) {
              // not processed, store instead
              if (optionDescP->longOptionName)
                optName = optionDescP->longOptionName;
              else
                optName[0] = optionDescP->shortOptionChar;
              // save in map
              options[optName] = optArg;
            }
            optionFound = true;
            break;
          }
          // next in list
          optionDescP++;
        }
        if (!optionFound) {
          fprintf(stderr, "Unknown Option '%s'\n", optName.c_str());
          showUsage();
          terminateApp(EXIT_FAILURE);
        }
      }
      else {
        // non-option argument
        // - have argument processed by subclass
        if (!processArgument(argP)) {
          // not processed, store instead
          arguments.push_back(argP);
        }
      }
      // next argument
      rawArgIndex++;
    }
  }
}


bool CmdLineApp::processOption(const CmdLineOptionDescriptor &aOptionDescriptor, const char *aOptionValue)
{
  // directly process "help" option (long name must be "help", short name can be anything but usually is 'h')
  if (!aOptionDescriptor.withArgument && strcmp(aOptionDescriptor.longOptionName,"help")==0) {
    showUsage();
    terminateApp(EXIT_SUCCESS);
  }
  else if (!aOptionDescriptor.withArgument && strcmp(aOptionDescriptor.longOptionName,"version")==0) {
    fprintf(stdout, "%s\n", version().c_str());
    terminateApp(EXIT_SUCCESS);
  }
  else if (aOptionDescriptor.withArgument && strcmp(aOptionDescriptor.longOptionName,"resourcepath")==0) {
    setResourcePath(aOptionValue);
  }
  else if (aOptionDescriptor.withArgument && strcmp(aOptionDescriptor.longOptionName,"datapath")==0) {
    setDataPath(aOptionValue);
  }
  return false; // not processed
}


const char *CmdLineApp::getInvocationName()
{
  return invocationName.c_str();
}


void CmdLineApp::resetCommandLine()
{
  invocationName.clear();
  synopsis.clear();
  options.clear();
  arguments.clear();
}


const char *CmdLineApp::getOption(const char *aOptionName, const char *aDefaultValue)
{
  const char *opt = aDefaultValue;
  OptionsMap::iterator pos = options.find(aOptionName);
  if (pos!=options.end()) {
    opt = pos->second.c_str();
  }
  return opt;
}


bool CmdLineApp::getIntOption(const char *aOptionName, int &aInteger)
{
  const char *opt = getOption(aOptionName);
  if (opt) {
    return sscanf(opt, "%d", &aInteger)==1;
  }
  return false; // no such option
}


bool CmdLineApp::getStringOption(const char *aOptionName, const char *&aCString)
{
  const char *opt = getOption(aOptionName);
  if (opt) {
    aCString = opt;
    return true;
  }
  return false; // no such option
}


bool CmdLineApp::getStringOption(const char *aOptionName, string &aString)
{
  const char *opt = getOption(aOptionName);
  if (opt) {
    aString = opt;
    return true;
  }
  return false; // no such option
}



size_t CmdLineApp::numOptions()
{
  return options.size();
}


const char *CmdLineApp::getArgument(size_t aArgumentIndex)
{
  if (aArgumentIndex>arguments.size()) return NULL;
  return arguments[aArgumentIndex].c_str();
}


size_t CmdLineApp::numArguments()
{
  return arguments.size();
}



