//
//  Copyright (c) 2017-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44utils__expressions__
#define __p44utils__expressions__

#include "p44utils_common.hpp"
#include "timeutils.hpp"
#include <string>

#ifndef EXPRESSION_SCRIPT_SUPPORT
  #define EXPRESSION_SCRIPT_SUPPORT 1 // on by default
#endif


using namespace std;

namespace p44 {

  /// Expression Error
  class ExpressionError : public Error
  {
  public:
    // Errors
    typedef enum {
      OK,
      Null,
      Syntax,
      DivisionByZero,
      CyclicReference,
      NotFound, ///< variable, object, function not found (for callback)
    } ErrorCodes;
    static const char *domain() { return "ExpressionError"; }
    virtual const char *getErrorDomain() const { return ExpressionError::domain(); };
    ExpressionError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
    /// factory method to create string error fprint style
    static ErrorPtr err(ErrorCodes aErrCode, const char *aFmt, ...) __printflike(2,3);
  };

  /// expression value, consisting of a value and an error to indicate non-value and reason for it
  class ExpressionValue {
    string* strValP; ///< string values have a std::string here
    double numVal;
  public:
    size_t pos; ///< starting position in expression string (for function arguments and subexpressions)
    ErrorPtr err;
    ExpressionValue() : numVal(0), pos(0), strValP(NULL) { };
    ExpressionValue(double aNumValue) : numVal(aNumValue), pos(0), strValP(NULL) { };
    ExpressionValue(const string &aStrValue) : numVal(0), pos(0), strValP(new string(aStrValue)) { };
    ExpressionValue(const ExpressionValue& aVal); ///< copy constructor
    ExpressionValue& operator=(const ExpressionValue& aVal); ///< assignment operator
    ~ExpressionValue();
    bool operator<(const ExpressionValue& aRightSide) const;
    bool operator==(const ExpressionValue& aRightSide) const;
    ExpressionValue operator+(const ExpressionValue& aRightSide) const;
    ExpressionValue operator-(const ExpressionValue& aRightSide) const;
    ExpressionValue operator*(const ExpressionValue& aRightSide) const;
    ExpressionValue operator/(const ExpressionValue& aRightSide) const;
    ExpressionValue operator&&(const ExpressionValue& aRightSide) const;
    ExpressionValue operator||(const ExpressionValue& aRightSide) const;
    void clrStr();
    void setNumber(double aNumValue) { err.reset(); numVal = aNumValue; clrStr(); }
    void setBool(bool aBoolValue) { err.reset(); numVal = aBoolValue ? 1: 0; clrStr(); }
    void setString(const string& aStrValue) { err.reset(); numVal = 0; clrStr(); strValP = new string(aStrValue); }
    static ExpressionValue errValue(ExpressionError::ErrorCodes aErrCode, const char *aFmt, ...) __printflike(2,3);
    static ExpressionValue nullValue() { return errValue(ExpressionError::Null, "undefined"); }
    ExpressionValue withError(ErrorPtr aError) { err = aError; return *this; }
    ExpressionValue withError(ExpressionError::ErrorCodes aErrCode, const char *aFmt, ...)  __printflike(3,4);
    ExpressionValue withSyntaxError(const char *aFmt, ...)  __printflike(2,3);
    ExpressionValue withNumber(double aNumValue) { setNumber(aNumValue); return *this; }
    ExpressionValue withString(const string& aStrValue) { setString(aStrValue); return *this; }
    ExpressionValue withValue(const ExpressionValue &aExpressionValue) { numVal = aExpressionValue.numVal; err = aExpressionValue.err; if (aExpressionValue.strValP) setString(*aExpressionValue.strValP); return *this; }
    ExpressionValue withPos(size_t aPos) { pos = aPos; return *this; }
    bool isOk() const { return Error::isOK(err); }
    bool valueOk() const { return isOk() || isNull(); } ///< ok as a value, but can be NULL
    bool isNull() const { return Error::isError(err, ExpressionError::domain(), ExpressionError::Null); } ///< ok as a value, but can be NULL
    bool syntaxOk() const { return !Error::isError(err, ExpressionError::domain(), ExpressionError::Syntax); } ///< ok for calculations, not a syntax problem
    bool notOk() const { return !isOk(); }
    bool isString() const { return strValP!=NULL; }
    string stringValue() const; ///< returns a conversion to string if value is numeric
    double numValue() const; ///< returns a conversion to numeric (using literal syntax), if value is string
    bool boolValue() const { return numValue()!=0; } ///< returns true if value is not 0
    int intValue() const { return (int)numValue(); }
    int64_t int64Value() const { return (int64_t)numValue(); }
  };


  /// callback function for obtaining variables
  /// @param aName the name of the value/variable to look up
  /// @return Expression value (with error when value is not available)
  typedef boost::function<ExpressionValue (const string &aName)> ValueLookupCB;

  /// callback function for function evaluation
  /// @param aFunc the name of the function to execute
  /// @param aArgs vector of function arguments, tuple contains expression starting position and value
  typedef std::vector<ExpressionValue> FunctionArgumentVector;
  typedef boost::function<ExpressionValue (const string &aFunc, const FunctionArgumentVector &aArgs)> FunctionLookupCB;

  /// evaluate expression
  /// @param aExpression the expression text
  /// @param aValueLookupCB this will be called to get variables resolved into values
  /// @param aFunctionLookpCB this will be called to execute functions that are not built-in
  /// @return the result of the expression
  ExpressionValue evaluateExpression(const string &aExpression, ValueLookupCB aValueLookupCB, FunctionLookupCB aFunctionLookpCB);

  /// substitute "@{xxx}" type expression placeholders in string
  /// @param aString string to replace placeholders in
  /// @param aValueLookupCB this will be called to get variables resolved into values
  /// @param aFunctionLookpCB this will be called to execute functions that are not built-in
  /// @param aNullText this will be used as substitution for expressions with errors or null value
  /// @return returns first error occurred during substitutions. Note that only unbalanced substitution brackets @{xxx} abort substitution,
  ///    other errors just cause substitution result to be aNullText.
  ErrorPtr substituteExpressionPlaceholders(string &aString, ValueLookupCB aValueLookupCB, FunctionLookupCB aFunctionLookpCB, string aNullText = "null");



  class EvaluationContext;

  /// Expression Evaluation Callback
  /// @param aEvaluationResult the evaluation result (can be error)
  /// @param aContext the evaluation context this result originates from
  /// @return ok, or error in case the result processing wants to pass on a evaluation error or an error of its own.
  typedef boost::function<ErrorPtr (ExpressionValue aEvaluationResult, EvaluationContext &aContext)> EvaluationResultCB;


  typedef enum {
    evalmode_initial, ///< initial evaluator run
    evalmode_externaltrigger, ///< externally triggered evaluation
    evalmode_timed, ///< timed evaluation by timed retrigger
    evalmode_script, ///< evaluate as script code
    evalmode_noexec, ///< evaluate without executing any side effects (for skipping in scripts)
  } EvalMode;

  /// Basic Expression Evaluation Context
  class EvaluationContext : public P44Obj
  {
    friend class ExpressionValue;

  protected:

    const GeoLocation* geolocationP;

    string expression; ///< the expression
    EvaluationResultCB evaluationResultHandler; ///< called when a evaluation started by triggerEvaluation() completes (includes re-evaluations)
    bool evaluating; ///< protection against cyclic references
    MLMicroSeconds nextEvaluation; ///< time when executed functions would like to see the next evaluation (used by TimedEvaluationContext)

    /// unused here, only actually in use by TimedEvaluationContext
    class FrozenResult
    {
    public:
      ExpressionValue frozenResult; ///< the frozen result
      MLMicroSeconds frozenUntil; ///< until when the value remains frozen, Infinite if forever (until explicitly unfrozen)
      /// @return true if still frozen (not expired yet)
      bool frozen();
    };

  public:

    EvaluationContext(const GeoLocation* aGeoLocationP = NULL);
    virtual ~EvaluationContext();

    /// set re-evaluation callback
    /// @param aEvaluationResultHandler is called when a evaluation started by triggerEvaluation() completes
    ///   (which includes delayed re-evaluations the context triggers itself, e.g. when timed functions are called)
    void setEvaluationResultHandler(EvaluationResultCB aEvaluationResultHandler);

    /// set expression to evaluate
    /// @param aExpression set the expression to be evaluated in this context
    /// @note setting an expression that differs from the current one unfreezes any frozen arguments
    /// @return true if expression actually changed (not just set same expession again)
    bool setExpression(const string aExpression);

    /// get current expression
    const char *getExpression() { return expression.c_str(); };

    /// evaluate expression right now, return result
    /// @param aEvalMode if specified, the evaluation mode for this evaluation. Defaults to current evaluation mode.
    /// @param aScheduleReEval if true, re-evaluations as demanded by evaluated expression are scheduled (NOP in base class)
    /// @return expression result
    /// @note does NOT trigger the evaluation result handler
    virtual ExpressionValue evaluateNow(EvalMode aEvalMode, bool aScheduleReEval = false);

    /// trigger a (re-)evaluation
    /// @param aEvalMode the evaluation mode for this evaluation.
    /// @note evaluation result handler will be called when complete
    /// @return ok, or error if expression could not be evaluated
    ErrorPtr triggerEvaluation(EvalMode aEvalMode);

    /// @return true if currently evaluating an expression.
    bool isEvaluating() { return evaluating; }

    static void skipWhiteSpace(const char *aExpr, size_t& aPos);
    static bool skipIdentifier(const char *aExpr, size_t& aPos);

  protected:

    /// @name to be overridden and enhanced in subclasses
    /// @{

    /// release all evaluation state (none in base class)
    virtual void releaseState() { /* NOP: no state in base class */ };

    /// lookup variables by name
    /// @param aName the name of the value/variable to look up
    /// @param aNextEval Input: time of next re-evaluation that will happen, or Never.
    ///   Output: reduced to more recent time when this value demands more recent re-evaulation, untouched otherwise
    /// @return Expression value (with error when value is not available)
    virtual ExpressionValue valueLookup(const string &aName);

    /// function evaluation
    /// @param aFunc the name of the function to execute
    /// @return Expression value or error.
    /// @param aNextEval will be adjusted to the most recent point in time when a re-evaluation should occur. Inital call should pass Never.
    /// @note must return ExpressionError::NotFound only if function name is unknown
    virtual ExpressionValue evaluateFunction(const string &aFunc, const FunctionArgumentVector &aArgs, EvalMode aEvalMode);

    /// @}

    /// @name API for timed evaluation and freezing values in functions that can be used in timed evaluations
    /// @note base class does not actually implement freezing, but API with dummy functionalizy
    ///   is defined here so function evaluation can use without knowing the context they will execute in
    /// @{

    /// get frozen result if any exists
    /// @param aResult On call: the current result of a (sub)expression - pos member must be set!
    ///   On return: replaced by a frozen result, if one exists
    virtual FrozenResult* getFrozen(ExpressionValue &aResult) { return NULL; /* base class has no frozen results */ }

    /// update existing or create new frozen result
    /// @param aExistingFreeze the pointer obtained from getFrozen(), can be NULL
    /// @param aNewResult the new value to be frozen
    /// @param aFreezeUntil The new freeze date. Specify Infinite to freeze indefinitely, Never to release any previous freeze.
    /// @param aUpdate if set, freeze will be updated/extended unconditionally, even when previous freeze is still running
    virtual FrozenResult* newFreeze(FrozenResult* aExistingFreeze, const ExpressionValue &aNewResult, MLMicroSeconds aFreezeUntil, bool aUpdate = false) { return NULL; /* base class cannot freeze */ }

    /// unfreeze frozen value at aAtPos
    /// @param aAtPos the starting character index of the subexpression to unfreeze
    /// @return true if there was a frozen result at aAtPos
    virtual bool unfreeze(size_t aAtPos) { return false; /* base class has no frozen results */ }

    /// Set time when next evaluation must happen, latest
    /// @param aLatestEval new time when evaluation must happen latest, Never if no next evaluation is needed
    /// @return true if aNextEval was updated
    bool updateNextEval(const MLMicroSeconds aLatestEval);
    /// @param aLatestEvalTm new local broken down time when evaluation must happen latest
    /// @return true if aNextEval was updated
    bool updateNextEval(const struct tm& aLatestEvalTm);

    /// @}


    /// evaluate (sub)expression
    /// @param aExpr the beginning of the entire expression (important for freezing subexpressions via their string position)
    /// @param aPos the current position of evaluation within aExpr
    /// @param aPrecedence encountering a operator with precedence lower or same as aPrecedence will stop parsing the expression
    /// @param aStopChars list of characters that stop the evaluation of an expression (e.g. to make argument processing stop at ')' and ','
    /// @param aNeedStopChar if set, one of the stopchars is REQUIRED and will be skipped. If not stopped by a stopchar, error is returned
    /// @param aEvalMode evaluation mode
    /// @return expression result
    ExpressionValue evaluateExpressionPrivate(const char *aExpr, size_t &aPos, int aPrecedence, const char *aStopChars, bool aNeedStopChar, EvalMode aEvalMode);

  private:

    static void evaluateNumericLiteral(ExpressionValue &res, const string &term);
    ExpressionValue evaluateTerm(const char *aExpr, size_t &aPos, EvalMode aEvalMode);

  };
  typedef boost::intrusive_ptr<EvaluationContext> EvaluationContextPtr;


  #if EXPRESSION_SCRIPT_SUPPORT

  // execution of scripts
  class ScriptExecutionContext : public EvaluationContext
  {
    typedef EvaluationContext inherited;

    typedef std::map<string, ExpressionValue> VariablesMap;
    VariablesMap variables;

  public:

    ScriptExecutionContext(const GeoLocation* aGeoLocationP = NULL);
    virtual ~ScriptExecutionContext();

    /// run the stored expression as a script
    ExpressionValue runAsScript();

    /// clear all variables
    void clearVariables();

  protected:

    /// lookup variables by name
    /// @param aName the name of the value/variable to look up
    /// @param aNextEval Input: time of next re-evaluation that will happen, or Never.
    ///   Output: reduced to more recent time when this value demands more recent re-evaulation, untouched otherwise
    /// @return Expression value (with error when value is not available)
    virtual ExpressionValue valueLookup(const string &aName) P44_OVERRIDE;

    /// timed context specific functions
    virtual ExpressionValue evaluateFunction(const string &aFunc, const FunctionArgumentVector &aArgs, EvalMode aEvalMode) P44_OVERRIDE;

  private:

    ExpressionValue runStatementPrivate(const char *aScript, size_t &aPos, EvalMode aMode, bool aInBlock);

  };

  #endif // EXPRESSION_SCRIPT_SUPPORT


  // evaluation with time related functions that must trigger re-evaluations when used
  class TimedEvaluationContext : public EvaluationContext
  {
    typedef EvaluationContext inherited;

    typedef std::map<size_t, FrozenResult> FrozenResultsMap;
    FrozenResultsMap frozenResults; ///< map of expression starting indices and associated frozen results

    MLTicket reEvaluationTicket; ///< ticket for re-evaluation timer

  public:

    TimedEvaluationContext(const GeoLocation* aGeoLocationP);
    virtual ~TimedEvaluationContext();

    /// evaluate expression right now, return result
    /// @param aEvalMode if specified, the evaluation mode for this evaluation. Defaults to current evaluation mode.
    /// @param aScheduleReEval if true, re-evaluations as demanded by evaluated expression are scheduled (NOP in base class)
    /// @return expression result
    /// @note does NOT trigger the evaluation result handler
    virtual ExpressionValue evaluateNow(EvalMode aEvalMode, bool aScheduleReEval = false) P44_OVERRIDE;

    /// schedule latest re-evaluation time. If an earlier evaluation time is already scheduled, nothing will happen
    /// @note this will cancel a possibly already scheduled re-evaluation unconditionally
    void scheduleLatestEvaluation(MLMicroSeconds aAtTime);

    /// schedule a re-evaluation at given time
    /// @note this will cancel a possibly already scheduled re-evaluation unconditionally
    void scheduleReEvaluation(MLMicroSeconds aAtTime);

  protected:

    /// release all evaluation state (such as frozen subexpressions)
    virtual void releaseState() P44_OVERRIDE;

    /// get frozen result if any exists
    /// @param aResult On call: the current result of a (sub)expression - pos member must be set!
    ///   On return: replaced by a frozen result, if one exists
    virtual FrozenResult* getFrozen(ExpressionValue &aResult) P44_OVERRIDE;

    /// update existing or create new frozen result
    /// @param aExistingFreeze the pointer obtained from getFrozen(), can be NULL
    /// @param aNewResult the new value to be frozen
    /// @param aFreezeUntil The new freeze date. Specify Infinite to freeze indefinitely, Never to release any previous freeze.
    /// @param aUpdate if set, freeze will be updated/extended unconditionally, even when previous freeze is still running
    virtual FrozenResult* newFreeze(FrozenResult* aExistingFreeze, const ExpressionValue &aNewResult, MLMicroSeconds aFreezeUntil, bool aUpdate = false) P44_OVERRIDE;

    /// unfreeze frozen value at aAtPos
    /// @param aAtPos the starting character index of the subexpression to unfreeze
    /// @return true if there was a frozen result at aAtPos
    virtual bool unfreeze(size_t aAtPos) P44_OVERRIDE;

    /// timed context specific functions
    virtual ExpressionValue evaluateFunction(const string &aFunc, const FunctionArgumentVector &aArgs, EvalMode aEvalMode) P44_OVERRIDE;

  private:

    void timedEvaluationHandler(MLTimer &aTimer, MLMicroSeconds aNow);

  };
  typedef boost::intrusive_ptr<TimedEvaluationContext> TimedEvaluationContextPtr;


  #if EXPRESSION_LEGACY_PLACEHOLDERS

  /// callback function for obtaining string variables
  /// @param aValue the contents of this is looked up and possibly replaced
  /// @return ok or error
  typedef boost::function<ErrorPtr (const string aName, string &aValue)> StringValueLookupCB;

  /// substitute "@{xxx}" type placeholders in string
  /// @param aString string to replace placeholders in
  /// @param aValueLookupCB this will be called to get variables resolved into values
  ErrorPtr substitutePlaceholders(string &aString, StringValueLookupCB aValueLookupCB);

  #endif


} // namespace p44



#endif // defined(__p44utils__expressions__)
