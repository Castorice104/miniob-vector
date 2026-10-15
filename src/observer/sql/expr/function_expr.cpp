#include "sql/expr/expression.h"

#include <cctype>
#include <cmath>

#include "common/lang/string.h"
#include "common/log/log.h"
#include "common/type/data_type.h"
#include "common/value.h"

using namespace std;

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

/// 能否作为向量参与运算：向量本身，或需要运行期解析的字符串
static bool is_vector_like(AttrType type) { return type == AttrType::VECTORS || type == AttrType::CHARS; }

int FunctionExpr::arity(Type type)
{
  switch (type) {
    case Type::STRING_TO_VECTOR:
    case Type::VECTOR_TO_STRING: return 1;
    case Type::DISTANCE: return 2;
  }
  return 0;
}

RC FunctionExpr::type_from_string(const char *name, Type &type, Metric &metric)
{
  if (function_name_is(name, "string_to_vector") || function_name_is(name, "to_vector")) {
    type   = Type::STRING_TO_VECTOR;
    metric = Metric::UNSPECIFIED;
    return RC::SUCCESS;
  }
  if (function_name_is(name, "vector_to_string") || function_name_is(name, "from_vector")) {
    type   = Type::VECTOR_TO_STRING;
    metric = Metric::UNSPECIFIED;
    return RC::SUCCESS;
  }
  if (function_name_is(name, "l2_distance") || function_name_is(name, "l2")) {
    type   = Type::DISTANCE;
    metric = Metric::EUCLIDEAN;
    return RC::SUCCESS;
  }
  if (function_name_is(name, "cosine_distance") || function_name_is(name, "cosine")) {
    type   = Type::DISTANCE;
    metric = Metric::COSINE;
    return RC::SUCCESS;
  }
  if (function_name_is(name, "inner_product")) {
    type   = Type::DISTANCE;
    metric = Metric::DOT;
    return RC::SUCCESS;
  }
  if (function_name_is(name, "distance") || function_name_is(name, "vector_distance")) {
    type   = Type::DISTANCE;
    metric = Metric::UNSPECIFIED;
    return RC::SUCCESS;
  }
  return RC::UNIMPLEMENTED;
}

RC FunctionExpr::metric_from_string(const char *name, Metric &metric)
{
  if (function_name_is(name, "euclidean") || function_name_is(name, "l2")) {
    metric = Metric::EUCLIDEAN;
    return RC::SUCCESS;
  }
  if (function_name_is(name, "cosine")) {
    metric = Metric::COSINE;
    return RC::SUCCESS;
  }
  if (function_name_is(name, "dot") || function_name_is(name, "inner_product")) {
    metric = Metric::DOT;
    return RC::SUCCESS;
  }
  return RC::INVALID_ARGUMENT;
}

/// 常量字符串参数在绑定期就解析成向量，非法字面量在这里直接报错
static RC normalize_vector_child(unique_ptr<Expression> &child)
{
  const AttrType type = child->value_type();
  if (type == AttrType::VECTORS) {
    return RC::SUCCESS;
  }
  if (type != AttrType::CHARS) {
    return RC::INVALID_ARGUMENT;
  }
  if (child->type() != ExprType::VALUE) {
    return RC::SUCCESS;
  }

  const string text = static_cast<ValueExpr *>(child.get())->get_value().get_string();
  Value        vector_value;
  RC           rc = DataType::type_instance(AttrType::VECTORS)->set_value_from_str(vector_value, text);
  if (OB_FAIL(rc)) {
    LOG_WARN("invalid vector literal: %s", text.c_str());
    return RC::INVALID_ARGUMENT;
  }
  child = make_unique<ValueExpr>(vector_value);
  return RC::SUCCESS;
}

/// 两个向量参数的维度必须一致；维度未知时留给运行期判断
static RC check_same_dimension(const Expression &left, const Expression &right)
{
  if (left.value_type() != AttrType::VECTORS || right.value_type() != AttrType::VECTORS) {
    return RC::SUCCESS;
  }

  const int left_len  = left.value_length();
  const int right_len = right.value_length();
  if (left_len > 0 && right_len > 0 && left_len != right_len) {
    LOG_WARN("vector dimension mismatch: %d vs %d", left_len / static_cast<int>(sizeof(float)),
        right_len / static_cast<int>(sizeof(float)));
    return RC::INVALID_ARGUMENT;
  }
  return RC::SUCCESS;
}

RC FunctionExpr::normalize(Type type, Metric metric, vector<unique_ptr<Expression>> &children)
{
  if (static_cast<int>(children.size()) != arity(type)) {
    LOG_WARN("invalid argument number of function: %d", static_cast<int>(children.size()));
    return RC::INVALID_ARGUMENT;
  }
  if (type == Type::DISTANCE && (metric == Metric::UNSPECIFIED || !is_vector_like(children[0]->value_type()) ||
                                    !is_vector_like(children[1]->value_type()))) {
    return RC::INVALID_ARGUMENT;
  }

  RC rc = normalize_vector_child(children[0]);
  if (OB_SUCC(rc) && type == Type::DISTANCE) {
    rc = normalize_vector_child(children[1]);
  }
  if (OB_FAIL(rc)) {
    return rc;
  }
  if (type == Type::DISTANCE) {
    return check_same_dimension(*children[0], *children[1]);
  }
  return RC::SUCCESS;
}

AttrType FunctionExpr::value_type() const
{
  switch (function_type_) {
    case Type::STRING_TO_VECTOR: return AttrType::VECTORS;
    case Type::VECTOR_TO_STRING: return AttrType::CHARS;
    case Type::DISTANCE: return AttrType::FLOATS;
  }
  return AttrType::UNDEFINED;
}

int FunctionExpr::value_length() const
{
  const int child_len = children_.empty() ? 0 : children_[0]->value_length();
  switch (function_type_) {
    case Type::STRING_TO_VECTOR:
      // 常量字符串参数在绑定期已解析为向量，长度就是它的字节数；
      // 字符串字段只能在运行期解析，按每个元素至少 2 个字符估算
      if (children_.empty() || children_[0]->value_type() == AttrType::VECTORS) {
        return child_len > 0 ? child_len : 0;
      }
      return child_len > 0 ? child_len / 2 * static_cast<int>(sizeof(float)) : 0;
    case Type::VECTOR_TO_STRING:
      // 元素按最多 16 个字符估算，再加上方括号
      return child_len > 0 ? child_len / static_cast<int>(sizeof(float)) * 16 + 2 : 0;
    case Type::DISTANCE: return sizeof(float);
  }
  return 0;
}

unique_ptr<Expression> FunctionExpr::copy() const
{
  vector<unique_ptr<Expression>> children;
  for (const unique_ptr<Expression> &child : children_) {
    children.emplace_back(child->copy());
  }

  auto expr = make_unique<FunctionExpr>(function_type_, metric_, std::move(children));
  expr->set_name(name());
  return expr;
}

unique_ptr<Expression> UnboundFunctionExpr::copy() const
{
  vector<unique_ptr<Expression>> args;
  for (const unique_ptr<Expression> &arg : args_) {
    args.emplace_back(arg->copy());
  }
  auto expr = make_unique<UnboundFunctionExpr>(function_name_.c_str(), std::move(args));
  expr->set_name(name());
  return expr;
}

/// 取出向量参数：非向量类型先按字符串字面量解析
static RC fetch_vector(const Value &src, Value &holder, const float **data, int *dim)
{
  if (src.attr_type() == AttrType::VECTORS) {
    // 直接引用原值即可，源值在本次调用期间一直存活，无需再拷贝一份
    *data = src.vector_data();
    *dim  = src.vector_dim();
    return (*dim > 0) ? RC::SUCCESS : RC::INVALID_ARGUMENT;
  }

  RC rc = Value::cast_to(src, AttrType::VECTORS, holder);
  if (OB_FAIL(rc)) {
    return rc;
  }
  *data = holder.vector_data();
  *dim  = holder.vector_dim();
  return (*dim > 0) ? RC::SUCCESS : RC::INVALID_ARGUMENT;
}

RC FunctionExpr::get_value(const Tuple &tuple, Value &value) const
{
  if (function_type_ == Type::DISTANCE) {
    Value left_value;
    RC    rc = children_[0]->get_value(tuple, left_value);
    if (OB_FAIL(rc)) {
      return rc;
    }
    Value right_value;
    rc = children_[1]->get_value(tuple, right_value);
    if (OB_FAIL(rc)) {
      return rc;
    }

    Value        left_holder;
    Value        right_holder;
    const float *left_data  = nullptr;
    const float *right_data = nullptr;
    int          left_dim   = 0;
    int          right_dim  = 0;
    rc = fetch_vector(left_value, left_holder, &left_data, &left_dim);
    if (OB_SUCC(rc)) {
      rc = fetch_vector(right_value, right_holder, &right_data, &right_dim);
    }
    if (OB_FAIL(rc)) {
      return rc;
    }
    if (left_dim != right_dim) {
      LOG_WARN("vector dimension mismatch: %d vs %d", left_dim, right_dim);
      return RC::INVALID_ARGUMENT;
    }

    float result = 0.0f;
    switch (metric_) {
      case Metric::EUCLIDEAN: {
        for (int i = 0; i < left_dim; i++) {
          const float diff = left_data[i] - right_data[i];
          result += diff * diff;
        }
        result = sqrtf(result);
      } break;
      case Metric::DOT: {
        for (int i = 0; i < left_dim; i++) {
          result += left_data[i] * right_data[i];
        }
      } break;
      case Metric::COSINE: {
        float dot = 0.0f, left_norm = 0.0f, right_norm = 0.0f;
        for (int i = 0; i < left_dim; i++) {
          dot += left_data[i] * right_data[i];
          left_norm += left_data[i] * left_data[i];
          right_norm += right_data[i] * right_data[i];
        }
        // 零向量没有方向，分母为零时约定为最大距离，避免个别数据让整个查询报错
        if (left_norm <= 0.0f || right_norm <= 0.0f) {
          LOG_WARN("cosine distance of a zero vector is undefined, use 1.0");
          result = 1.0f;
          break;
        }
        result = 1.0f - dot / (sqrtf(left_norm) * sqrtf(right_norm));
      } break;
      default: {
        LOG_WARN("unsupported distance metric: %d", static_cast<int>(metric_));
        return RC::UNIMPLEMENTED;
      } break;
    }

    value.set_float(result);
    return RC::SUCCESS;
  }

  DataType *vector_type = DataType::type_instance(AttrType::VECTORS);

  Value child_value;
  RC    rc = children_[0]->get_value(tuple, child_value);
  if (OB_FAIL(rc)) {
    return rc;
  }

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
