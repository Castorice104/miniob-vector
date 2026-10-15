#include "sql/expr/expression.h"

#include <cctype>

#include "common/lang/string.h"
#include "common/log/log.h"
#include "common/type/data_type.h"
#include "common/value.h"

/// 大小写不敏感的函数名比较
static bool function_name_is(const char *name, const char *expected)
{
  if (name == nullptr || expected == nullptr) {
    return false;
  }
  while (*name != '\0' && *expected != '\0') {
    if (tolower(static_cast<unsigned char>(*name)) != tolower(static_cast<unsigned char>(*expected))) {
      return false;
    }
    name++;
    expected++;
  }
  return *name == '\0' && *expected == '\0';
}

RC FunctionExpr::type_from_string(const char *name, Type &type)
{
  if (function_name_is(name, "string_to_vector") || function_name_is(name, "to_vector")) {
    type = Type::STRING_TO_VECTOR;
    return RC::SUCCESS;
  }
  if (function_name_is(name, "vector_to_string") || function_name_is(name, "from_vector")) {
    type = Type::VECTOR_TO_STRING;
    return RC::SUCCESS;
  }
  return RC::UNIMPLEMENTED;
}

RC FunctionExpr::get_value(const Tuple &tuple, Value &value) const
{
  Value child_value;
  RC    rc = child_->get_value(tuple, child_value);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to get child value. rc=%d", static_cast<int>(rc));
    return rc;
  }

  DataType *vector_type = DataType::type_instance(AttrType::VECTORS);

  switch (function_type_) {
    case Type::STRING_TO_VECTOR: {
      Value string_value;
      if (child_value.attr_type() != AttrType::CHARS) {
        rc = Value::cast_to(child_value, AttrType::CHARS, string_value);
        if (OB_FAIL(rc)) {
          return rc;
        }
      } else {
        string_value = child_value;
      }
      return vector_type->set_value_from_str(value, string_value.get_string());
    }
    case Type::VECTOR_TO_STRING: {
      Value vector_value;
      if (child_value.attr_type() != AttrType::VECTORS) {
        rc = Value::cast_to(child_value, AttrType::VECTORS, vector_value);
        if (OB_FAIL(rc)) {
          return rc;
        }
      } else {
        vector_value = child_value;
      }
      string text;
      rc = vector_type->to_string(vector_value, text);
      if (OB_FAIL(rc)) {
        return rc;
      }
      value.set_string(text.data(), static_cast<int>(text.size()));
      return RC::SUCCESS;
    }
  }
  return RC::UNIMPLEMENTED;
}
