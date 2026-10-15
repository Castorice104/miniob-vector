#include "sql/expr/expression.h"

#include <cctype>
#include <cmath>

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
  if (function_name_is(name, "l2_distance") || function_name_is(name, "l2")) {
    type = Type::L2_DISTANCE;
    return RC::SUCCESS;
  }
  if (function_name_is(name, "cosine_distance") || function_name_is(name, "cosine")) {
    type = Type::COSINE_DISTANCE;
    return RC::SUCCESS;
  }
  if (function_name_is(name, "inner_product")) {
    type = Type::INNER_PRODUCT;
    return RC::SUCCESS;
  }
  return RC::UNIMPLEMENTED;
}

static RC fetch_vector(const Value &src, Value &holder, const float **data, int *dim)
{
  if (src.attr_type() != AttrType::VECTORS) {
    RC rc = Value::cast_to(src, AttrType::VECTORS, holder);
    if (OB_FAIL(rc)) {
      return rc;
    }
  } else {
    holder = src;
  }
  *data = holder.vector_data();
  *dim  = holder.vector_dim();
  return (*dim > 0) ? RC::SUCCESS : RC::INVALID_ARGUMENT;
}

RC FunctionExpr::get_value(const Tuple &tuple, Value &value) const
{
  Value child_value;
  RC    rc = child_->get_value(tuple, child_value);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to get child value. rc=%d", static_cast<int>(rc));
    return rc;
  }

  if (is_distance()) {
    Value right_value;
    rc = child2_->get_value(tuple, right_value);
    if (OB_FAIL(rc)) {
      return rc;
    }
    Value        left_holder;
    Value        right_holder;
    const float *left_data  = nullptr;
    const float *right_data = nullptr;
    int          left_dim   = 0;
    int          right_dim  = 0;
    rc = fetch_vector(child_value, left_holder, &left_data, &left_dim);
    if (OB_SUCC(rc)) {
      rc = fetch_vector(right_value, right_holder, &right_data, &right_dim);
    }
    if (OB_FAIL(rc) || left_dim != right_dim) {
      return OB_FAIL(rc) ? rc : RC::INVALID_ARGUMENT;
    }

    float result = 0.0f;
    switch (function_type_) {
      case Type::INNER_PRODUCT: {
        for (int i = 0; i < left_dim; i++) {
          result += left_data[i] * right_data[i];
        }
      } break;
      case Type::L2_DISTANCE: {
        for (int i = 0; i < left_dim; i++) {
          const float diff = left_data[i] - right_data[i];
          result += diff * diff;
        }
        result = sqrtf(result);
      } break;
      default: {
        float dot = 0.0f, ln = 0.0f, rn = 0.0f;
        for (int i = 0; i < left_dim; i++) {
          dot += left_data[i] * right_data[i];
          ln += left_data[i] * left_data[i];
          rn += right_data[i] * right_data[i];
        }
        if (ln <= 0.0f || rn <= 0.0f) {
          return RC::INVALID_ARGUMENT;
        }
        result = 1.0f - dot / (sqrtf(ln) * sqrtf(rn));
      } break;
    }
    value.set_float(result);
    return RC::SUCCESS;
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
    default: break;
  }
  return RC::UNIMPLEMENTED;
}
