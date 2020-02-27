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

#ifndef __p44utils__application__
#define __p44utils__application__

#include "p44utils_common.hpp"
#if ENABLE_JSON_APPLICATION
  #include "jsonobject.hpp"
#endif

#include <signal.h>

using namespace std;

namespace p44 {

  class MainLoop;

  class Application : public P44Obj
  {
    typedef P44Obj inherited;

    MainLoop &mainLoop;

  protected:

    string resourcepath; ///< path to resources directory for this application
    string datapath; ///< path to (usually persistent) r/w data for this application

  public:
    /// construct application with specific mainloop
    Application(MainLoop &aMainLoop);

    /// construct application using current thread's mainloop
    Application();

    /// destructor
    virtual ~Application();

    /// main routine
    /// @param argc argument count as passed to C-level main() entry point
    /// @param argv argument pointer array as passed to C-level main() entry point
    virtual int main(int argc, char **argv);

    /// get shared instance (singleton)
    static Application *sharedApplication();

    /// @return returns true only when application is running in its mainloop
    /// @note can be used to make sure object tree is not in end-of-app destruction, e.g. when referencing objects from individual
    ///   object destructors
    static bool isRunning();

    /// @return returns true when application has been requested to terminate
    /// @note can be used to make sure no objects are created in case app is terminated early due to option syntax errors or similar
    static bool isTerminated();

    /// terminate app
    /// @param aExitCode the exit code to return to the parent
    void terminateApp(int aExitCode);

    /// terminate app
    /// @param aError if NULL or ErrorOK, app will terminate with EXIT_SUCCESS
    ///   otherwise, app will log aError's description at LOG_ERR level and then terminate with EXIT_FAILURE
    void terminateAppWith(ErrorPtr aError);

    /// get resource path. Resources are usually readonly files
    /// @param aResource if not empty, and it is an absolute path, the the result will be just this path
    ///   if it is a relative path, the application's resource path will be prepended.
    /// @return if aRelativePath is empty, result is the application's resource directory (no separator at end)
    ///   Otherwise, it is the absolute path to the resource specified with aResource
    string resourcePath(const string aResource = "");

    /// get data path. Data are usually persistent read/write files
    /// @param aDataFile if not empty, and it is an absolute path, the the result will be just this path
    ///   if it is a relative path, the application's data path will be prepended.
    /// @return if aDataFile is empty, result is the application's data directory (no separator at end)
    ///   Otherwise, it is the absolute path to the data file specified with aDataFile
    string dataPath(const string aDataFile = "");

    /// get temp path. Temp data are usually non-persistent read/write files located in a ram disk
    /// @param aTempFile if not empty, and it is an absolute path, the the result will be just this path
    ///   if it is a relative path, the application's temp path will be prepended.
    /// @return if aTempFile is empty, result is the application's temp directory (no separator at end)
    ///   Otherwise, it is the absolute path to the temp file specified with aTempFile
    string tempPath(const string aTempFile = "");

    #if ENABLE_JSON_APPLICATION

    /// parse JSON literal or get json file from resource
    /// @param aText the text to parse. If it is a plain string and ends on ".json", treat it as resource file
    ///   (see resourcePath()) containg JSON which is parsed and returned; if the string does not begin with "./", aPrefix is prepended.
    ///   Otherwise, aText is parsed as JSON as-is.
    /// @param aErrorP if set, parsing error is stored here
    /// @param aPrefix prefix possibly used on resource path (see above)
    /// @return json or NULL if none found
    static JsonObjectPtr jsonObjOrResource(const string &aText, ErrorPtr *aErrorP, const string aPrefix="");

    /// parse JSON literal or get json file from resource
    /// @param aConfig input json. If it is a plain string and ends on ".json", treat it as resource file
    ///   (see resourcePath()) containg JSON which is parsed and returned; if the string does not begin with "./", aPrefix is prepended.
    ///   Otherwise, aConfig is returned as-is.
    /// @param aErrorP if set, parsing error is stored here
    /// @param aPrefix prefix possibly used on resource path (see above)
    /// @return json or NULL if none found
    static JsonObjectPtr jsonObjOrResource(JsonObjectPtr aConfig, ErrorPtr *aErrorP, const string aPrefix="");


    #endif // ENABLE_JSON_APPLICATION

    /// @return version of this application
    virtual string version() const;

  protected:

    /// daemonize
    void daemonize();

    /// start running the app's main loop
    int run();

    /// scheduled to run when mainloop has started
    virtual void initialize();

    /// called when mainloop terminates
    virtual void cleanup(int aExitCode);

    /// called when a signal occurs
    /// @note only SIGHUP,SIGINT,SIGKILL and SIGTERM are handled here
    virtual void signalOccurred(int aSignal, siginfo_t *aSiginfo);

    /// set the resource path
    /// @param aResourcePath path to resource directory, with or without path delimiter at end
    void setResourcePath(const char *aResourcePath);

    /// set the resource path
    /// @param aDataPath path to the r/w data directory for persistent app data
    void setDataPath(const char *aDataPath);

  private:

    void initializeInternal();    
    void handleSignal(int aSignal);
    static void sigaction_handler(int aSignal, siginfo_t *aSiginfo, void *aUap);

  };


  /// standard option texts, can be used as part of setCommandDescriptors() string

  /// - logging options matching processStandardLogOptions()
  /// - for all apps
  #define CMDLINE_APPLICATION_LOGOPTIONS \
    { 'l', "loglevel",        true,  "level;set max level of log message detail to show on stderr" }, \
    { 0  , "deltatstamps",    false, "show timestamp delta between log lines" }
  /// - for daemon apps
  #define DAEMON_APPLICATION_LOGOPTIONS \
    CMDLINE_APPLICATION_LOGOPTIONS, \
    { 0  , "errlevel",      true,  "level;set max level for log messages to go to stderr as well" }, \
    { 0  , "dontlogerrors", false, "don't duplicate error messages (see --errlevel) on stdout" }

  /// - standard options every CmdLineApp understands
  #define CMDLINE_APPLICATION_STDOPTIONS \
    { 'V', "version",         false, "show version" }, \
    { 'h', "help",            false, "show this text" }
  #define CMDLINE_APPLICATION_PATHOPTIONS \
    { 'r', "resourcepath",   true,  "path;path to application resources" }, \
    { 'd', "datapath",       true,  "path;path to the r/w persistent data" }


  /// Command line option descriptor
  /// @note a descriptor with both longOptionName==NULL and shortOptionChar=0 terminates a list of option descriptors
  typedef struct {
    char shortOptionChar; ///< the short option name (single character) or 0/NUL if none
    const char *longOptionName; ///< the long option name (string) or NULL if none
    bool withArgument; ///< true if option has an argument (separated by = or next argument)
    const char *optionDescription; ///< the description of the option, can have multiple lines separated by \n
    int optionIdentifier; ///< an optional identifier
  } CmdLineOptionDescriptor;

  typedef vector<string> ArgumentsVector;
  typedef map<string,string> OptionsMap;

  class CmdLineApp : public Application
  {
    typedef Application inherited;

    const CmdLineOptionDescriptor *optionDescriptors;

    string invocationName;
    string synopsis;
    OptionsMap options;
    ArgumentsVector arguments;

  public:

    /// constructors
    CmdLineApp(MainLoop &aMainLoop);
    CmdLineApp();

    /// destructor
    virtual ~CmdLineApp();

    static CmdLineApp *sharedCmdLineApp();

  protected:

    /// set command description constants (option definitions and synopsis)
    /// @param aSynopsis short usage description, used in showUsage(). %1$s will be replaced by invocationName
    /// @param aOptionDescriptors pointer to array of descriptors for the options
    /// @note you can use CMDLINE_APPLICATION_STDOPTIONS and CMDLINE_APPLICATION_LOGOPTIONS as part of the
    ///   aOptionDescriptors list
    void setCommandDescriptors(const char *aSynopsis, const CmdLineOptionDescriptor *aOptionDescriptors);

    /// show usage, consisting of invocationName + synopsis + option descriptions
    void showUsage();

    /// parse command line.
    /// @param aArgc argument count as passed to C-level main() entry point
    /// @param aArgv argument pointer array as passed to C-level main() entry point
    /// @note setOptionDescriptors() must be called before using this method
    /// @note this method might call terminateApp() in case of command line syntax errors
    void parseCommandLine(int aArgc, char **aArgv);

    /// reset internal argument lists (to save memory when arguments are all processed)
    void resetCommandLine();

    /// process a command line option. Override this to implement processing command line options
    /// @param aOptionDescriptor the descriptor of the option
    /// @param aOptionValue the value of the option, empty string if option has no value
    /// @return true if option has been processed; false if option should be stored for later reference via getOption()
    /// @note will be called from parseCommandLine()
    /// @note base class will process "help" option by showing usage and terminating the app with EXIT_SUCCESS exit code
    virtual bool processOption(const CmdLineOptionDescriptor &aOptionDescriptor, const char *aOptionValue);

    /// process a non-option command line argument
    /// @param aArgument non-option argument
    /// @return true if argument has been processed; false if argument should be stored for later reference via getArgument()
    /// @note will be called from parseCommandLine()
    virtual bool processArgument(const char *aArgument) { return false; /* not processed, store */ };

    /// get app invocation name
    /// @return application invocation name (argv[0])
    /// @note parseCommandLine() must be called before using this method
    const char *getInvocationName();

    /// parse standard logging options and configure logger
    /// @param aForDaemon if set, logger is configured for daemon (rather than command line utility)
    /// @param aDefaultErrLevel sets the default error level for daemons (usually LOG_ERR)
    /// @note - daemon standard is LOG_NOTICE level by default, logging to stdout and logging LOG_ERR and higher also to stderr
    ///   - utility standard is LOG_CRIT level by default, logging only to stderr
    /// @note this is a convenience function to reduce boilerplate. You can also use
    ///   CMDLINE_APPLICATION_LOGOPTIONS as part of the option descriptors passed to setCommandDescriptors().
    void processStandardLogOptions(bool aForDaemon, int aDefaultErrLevel = LOG_ERR);

  public:

    /// get option
    /// @param aOptionName the name of the option (longOptionName if exists, shortOptionChar if no longOptionName exists)
    /// @param aDefaultValue this is returned in case the option is not specified, defaults to NULL
    /// @return aDefaultValue if option was not specified on the command line, empty string for options without argument, option's argument otherwise
    /// @note parseCommandLine() must be called before using this method
    const char *getOption(const char *aOptionName, const char *aDefaultValue = NULL);

    /// @param aOptionName the name of the option (longOptionName if exists, shortOptionChar if no longOptionName exists)
    /// @param aInteger will be set with the integer value of the option, if any
    /// @return true if option was specified and had a valid integer argument, false otherwise (aInteger will be untouched then)
    /// @note parseCommandLine() must be called before using this method
    /// @note integer option can be specified as decimal (NO leading zeroes!!), hex ('0x' prefix) or octal ('0' prefix)
    bool getIntOption(const char *aOptionName, int &aInteger);

    /// @param aOptionName the name of the option (longOptionName if exists, shortOptionChar if no longOptionName exists)
    /// @param aCString will be set to point to the option argument cstring, if any
    /// @return true if option was specified and had an option argument
    /// @note parseCommandLine() must be called before using this method
    bool getStringOption(const char *aOptionName, const char *&aCString);

    /// @param aOptionName the name of the option (longOptionName if exists, shortOptionChar if no longOptionName exists)
    /// @param aString will be set to point to the option argument cstring, if any
    /// @return true if option was specified and had an option argument
    /// @note parseCommandLine() must be called before using this method
    bool getStringOption(const char *aOptionName, string &aString);


    /// get number of stored options
    /// @return number of options present and not already processed by processOption() returning true
    /// @note parseCommandLine() must be called before using this method
    size_t numOptions();

    /// get non-option argument
    /// @param aArgumentIndex the index of the argument (0=first non-option argument, 1=second non-option argument, etc.)
    /// @return NULL if aArgumentIndex>=numArguments(), argument otherwise
    /// @note parseCommandLine() must be called before using this method
    const char *getArgument(size_t aArgumentIndex);

    /// get non-option string argument
    /// @param aArgumentIndex the index of the argument (0=first non-option argument, 1=second non-option argument, etc.)
    /// @param aArg string to store the argument, if any
    /// @return true if argument indicated by aArgumentIndex exists and is assigned to aStr, false otherwise
    bool getStringArgument(size_t aArgumentIndex, string &aArg);

    /// get non-option integer argument
    /// @param aArgumentIndex the index of the argument (0=first non-option argument, 1=second non-option argument, etc.)
    /// @param aInteger integer to store the argument, if any
    /// @return true if argument indicated by aArgumentIndex exists, is valid and is assigned to aInteger , false otherwise
    /// @note integer argument can be specified as decimal (NO leading zeroes!!), hex ('0x' prefix) or octal ('0' prefix)
    bool getIntArgument(size_t aArgumentIndex, int &aInteger);

    /// get number of (non-processed) arguments
    /// @return number of arguments not already processed by processArgument() returning true
    /// @note parseCommandLine() must be called before using this method
    size_t numArguments();

  };


} // namespace p44


#endif /* defined(__p44utils__application__) */
