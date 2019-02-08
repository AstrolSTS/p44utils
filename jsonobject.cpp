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

#include "jsonobject.hpp"

#if P44_BUILD_DIGI
  // still using older json-c
  #include "json_object_private.h"
#endif

#include <sys/stat.h> // for fstat

using namespace p44;


// MARK: ===== private constructors / destructor


// construct from raw json_object, passing ownership
JsonObject::JsonObject(struct json_object *aObjPassingOwnership) :
  nextEntryP(NULL)
{
  json_obj = aObjPassingOwnership;
}


// construct empty
JsonObject::JsonObject()
{
  json_obj = json_object_new_object();
}


JsonObject::~JsonObject()
{
  if (json_obj) {
    json_object_put(json_obj);
    json_obj = NULL;
  }
}


// MARK: ===== read and write from files


#define MAX_JSON_BUF_SIZE 20000

// factory method, create JSON object from file
JsonObjectPtr JsonObject::objFromFile(const char *aJsonFilePath, ErrorPtr *aErrorP)
{
  JsonObjectPtr obj;
  size_t bufSize = MAX_JSON_BUF_SIZE;
  // read file into string
  int fd = open(aJsonFilePath, O_RDONLY);
  if (fd>=0) {
    // opened, check buffer needs
    struct stat fs;
    fstat(fd, &fs);
    if (fs.st_size<bufSize) bufSize = fs.st_size; // don't need the entire buffer
    // decode
    struct json_tokener* tokener = json_tokener_new();
    char *jsontext = new char[bufSize];
    ssize_t n;
    while((n = read(fd, jsontext, bufSize))>0) {
      struct json_object *o = json_tokener_parse_ex(tokener, jsontext, (int)n);
      if (o==NULL) {
        // error (or incomplete JSON, which is fine)
        JsonError::ErrorCodes jerr = json_tokener_get_error(tokener);
        if (jerr!=json_tokener_continue) {
          // real error
          if (aErrorP) {
            *aErrorP = ErrorPtr(new JsonError(jerr));
            (*aErrorP)->prefixMessage("at offset %d: ", tokener->char_offset);
          }
          json_tokener_reset(tokener);
          break;
        }
      }
      else {
        // got JSON object
        obj = JsonObject::newObj(o);
        break;
      }
    }
    delete[] jsontext;
    json_tokener_free(tokener);
    close(fd);
  }
  else {
    if (aErrorP) {
      *aErrorP = TextError::err("cannot open file '%s'", aJsonFilePath);
    }
  }
  return obj;
}


ErrorPtr JsonObject::saveToFile(const char *aJsonFilePath)
{
  int fd = open(aJsonFilePath, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
  if (fd<0) {
    return SysError::errNo("Cannot open file to save JSON: ");
  }
  else {
    const char *jsontext = json_c_str();
    if (write(fd, jsontext, strlen(jsontext))<0) {
      close(fd);
      return SysError::errNo("Error writing JSON: ");
    }
    // success
    close(fd);
  }
  return ErrorPtr();
}


// MARK: ===== type


json_type JsonObject::type() const
{
  return json_object_get_type(json_obj);
}


bool JsonObject::isType(json_type aRefType) const
{
  return json_object_is_type(json_obj, aRefType);
}



// MARK: ===== conversion to string

const char *JsonObject::json_c_str(int aFlags)
{
  return json_object_to_json_string_ext(json_obj, aFlags);
}


string JsonObject::json_str(int aFlags)
{
  return string(json_c_str(aFlags));
}


// MARK: ===== add, get and delete by key

void JsonObject::add(const char* aKey, JsonObjectPtr aObj)
{
  // json_object_object_add assumes caller relinquishing ownership,
  // so we must compensate this by retaining (getting) the object
  // as the object still belongs to us
  // Except if a NULL (no object) is passed
  json_object_object_add(json_obj, aKey, aObj ? json_object_get(aObj->json_obj) : NULL);
}


bool JsonObject::get(const char *aKey, JsonObjectPtr &aJsonObject, bool aNonNull)
{
  json_object *weakObjRef = NULL;
  if (json_object_object_get_ex(json_obj, aKey, &weakObjRef)) {
    // found object, but can be the NULL object (which will return no JsonObjectPtr
    if (weakObjRef==NULL) {
      if (aNonNull) return false; // we don't want a NULL value returned
      aJsonObject = JsonObjectPtr(); // no object
    }
    else {
      // - claim ownership as json_object_object_get_ex does not do that automatically
      json_object_get(weakObjRef);
      // - create wrapper
      aJsonObject = newObj(weakObjRef);
    }
    return true; // key exists, but returned object might still be NULL
  }
  return false; // key does not exist, aJsonObject unchanged
}



JsonObjectPtr JsonObject::get(const char *aKey)
{
  JsonObjectPtr p;
  get(aKey, p);
  return p;
}


const char *JsonObject::getCString(const char *aKey)
{
  JsonObjectPtr p = get(aKey);
  if (p)
    return p->c_strValue();
  return NULL;
}



void JsonObject::del(const char *aKey)
{
  json_object_object_del(json_obj, aKey);
}


// MARK: ===== arrays


int JsonObject::arrayLength() const
{
  if (type()!=json_type_array)
    return 0; // normal objects don't have a length
  else
    return (int)json_object_array_length(json_obj);
}


void JsonObject::arrayAppend(JsonObjectPtr aObj)
{
  if (type()==json_type_array) {
    // - claim ownership as json_object_array_add does not do that automatically
    json_object_get(aObj->json_obj);
    json_object_array_add(json_obj, aObj->json_obj);
  }
}


JsonObjectPtr JsonObject::arrayGet(int aAtIndex)
{
  JsonObjectPtr p;
  json_object *weakObjRef = json_object_array_get_idx(json_obj, aAtIndex);
  if (weakObjRef) {
    // found object
    // - claim ownership as json_object_object_get_ex does not do that automatically
    json_object_get(weakObjRef);
    // - return wrapper
    p = newObj(weakObjRef);
  }
  return p;
}


void JsonObject::arrayPut(int aAtIndex, JsonObjectPtr aObj)
{
  if (type()==json_type_array) {
    // - claim ownership as json_object_array_put_idx does not do that automatically
    json_object_get(aObj->json_obj);
    json_object_array_put_idx(json_obj, aAtIndex, aObj->json_obj);
  }
}


// MARK: ===== object key/value iteration


bool JsonObject::resetKeyIteration()
{
  if (isType(json_type_object)) {
    nextEntryP = json_object_get_object(json_obj)->head;
    return true; // can be iterated (but might still have zero key/values)
  }
  return false; // cannot be iterated
}


bool JsonObject::nextKeyValue(string &aKey, JsonObjectPtr &aValue)
{
  if (nextEntryP) {
    // get key
    aKey = (char*)nextEntryP->k;
    // get value
    json_object *weakObjRef = (struct json_object*)nextEntryP->v;
    if (weakObjRef) {
      // claim ownership
      json_object_get(weakObjRef);
      // - return wrapper
      aValue = newObj(weakObjRef);
    }
    else {
      aValue = JsonObjectPtr(); // NULL
    }
    // advance to next
    nextEntryP = nextEntryP->next;
    return true;
  }
  // no more entries
  return false;
}


JsonObjectPtr JsonObject::nextJsonObj()
{
  if (nextEntryP) {
    json_object *weakObjRef = (struct json_object*)nextEntryP->v;
    if (weakObjRef) {
      json_object_get(weakObjRef);
      JsonObjectPtr obj = newObj();
      obj->add((const char*)nextEntryP->k, newObj(weakObjRef));
      nextEntryP = nextEntryP->next;
      return obj;
    }
  }
  return JsonObjectPtr();
}


// MARK: ===== factories and value getters

// private wrapper factory from newly created json_object (ownership passed in)
JsonObjectPtr JsonObject::newObj(struct json_object *aObjPassingOwnership)
{
  return JsonObjectPtr(new JsonObject(aObjPassingOwnership));
}



JsonObjectPtr JsonObject::newObj()
{
  return JsonObjectPtr(new JsonObject());
}


JsonObjectPtr JsonObject::newNull()
{
  // create wrapper with no embedded object (as a plain C NULL pointer represents NULL in JSON-C)
  return JsonObjectPtr(new JsonObject(NULL));
}


JsonObjectPtr JsonObject::objFromText(const char *aJsonText, ssize_t aMaxChars, ErrorPtr *errP)
{
  JsonObjectPtr obj;
  if (aMaxChars<0) aMaxChars = strlen(aJsonText);
  struct json_tokener* tokener = json_tokener_new();
  struct json_object *o = json_tokener_parse_ex(tokener, aJsonText, (int)aMaxChars);
  if (o) {
    obj = JsonObject::newObj(o);
  }
  else {
    // error
    if (errP) {
      *errP = ErrorPtr(new JsonError(json_tokener_get_error(tokener)));
      (*errP)->prefixMessage("at offset %d: ", tokener->char_offset);
    }
  }
  json_tokener_free(tokener);
  return obj;
}



JsonObjectPtr JsonObject::newArray()
{
  return JsonObjectPtr(new JsonObject(json_object_new_array()));
}




JsonObjectPtr JsonObject::newBool(bool aBool)
{
  return newObj(json_object_new_boolean(aBool));
}

bool JsonObject::boolValue() const
{
  return json_object_get_boolean(json_obj);
}


JsonObjectPtr JsonObject::newInt32(int32_t aInt32)
{
  return newObj(json_object_new_int(aInt32));
}

JsonObjectPtr JsonObject::newInt64(int64_t aInt64)
{
  return newObj(json_object_new_int64(aInt64));
}

int32_t JsonObject::int32Value() const
{
  if (isType(json_type_string)) {
    // check for hex
    const char *xstr = c_strValue();
    if (strncmp(xstr, "0x", 2)==0) {
      return (int32_t)strtol(xstr, NULL, 0);
    }
  }
  return json_object_get_int(json_obj);
}

int64_t JsonObject::int64Value() const
{
  if (isType(json_type_string)) {
    // check for hex
    const char *xstr = c_strValue();
    if (strncmp(xstr, "0x", 2)==0) {
      return (int64_t)strtoll(xstr, NULL, 0);
    }
  }
  return json_object_get_int64(json_obj);
}


JsonObjectPtr JsonObject::newDouble(double aDouble)
{
  return newObj(json_object_new_double(aDouble));
}

double JsonObject::doubleValue() const
{
  return json_object_get_double(json_obj);
}



JsonObjectPtr JsonObject::newString(const char *aCStr)
{
  if (!aCStr) return JsonObjectPtr();
  return newObj(json_object_new_string(aCStr));
}

JsonObjectPtr JsonObject::newString(const char *aCStr, size_t aLen)
{
  if (!aCStr) return JsonObjectPtr();
  return newObj(json_object_new_string_len(aCStr, (int)aLen));
}

JsonObjectPtr JsonObject::newString(const string &aString, bool aEmptyIsNull)
{
  if (aEmptyIsNull & aString.empty()) return JsonObjectPtr();
  return JsonObject::newString(aString.c_str());
}

const char *JsonObject::c_strValue() const
{
  return json_object_get_string(json_obj);
}

size_t JsonObject::stringLength() const
{
  return (size_t)json_object_get_string_len(json_obj);
}

string JsonObject::stringValue() const
{
  return string(c_strValue());
}

string JsonObject::lowercaseStringValue() const
{
  return lowerCase(c_strValue());
}






