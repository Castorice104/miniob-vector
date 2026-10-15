#include "common/type/vector_type.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common/lang/string.h"
#include "common/lang/vector.h"
#include "common/log/log.h"
#include "common/value.h"

/// 解析 "[1,2,3]" 形式的向量字面量
static RC parse_vector_literal(const char *data, int len, vector<float> &values)
{
  values.clear();
  if (data == nullptr || len <= 0) {
    return RC::INVALID_ARGUMENT;
  }

  string text(data, len);
  size_t begin = text.find('[');
  size_t end   = text.rfind(']');
  if (begin == string::npos || end == string::npos || end <= begin) {
    return RC::INVALID_ARGUMENT;
  }

  string body = text.substr(begin + 1, end - begin - 1);
  size_t pos  = 0;
  while (pos < body.size()) {
    while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t' || body[pos] == ',')) {
      pos++;
    }
    if (pos >= body.size()) {
      break;
    }

    const char *start = body.c_str() + pos;
    char       *stop  = nullptr;
    float       value = strtof(start, &stop);
    if (stop == start) {
      return RC::INVALID_ARGUMENT;
    }
    values.push_back(value);
    pos = static_cast<size_t>(stop - body.c_str());
  }

  if (values.empty()) {
    return RC::INVALID_ARGUMENT;
  }
  return RC::SUCCESS;
}

/// 单个元素格式化：最多两位小数，去掉多余的 0
static void format_vector_element(float value, string &out)
{
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%.2f", value);
  string text(buffer);
  if (text.find('.') != string::npos) {
    while (!text.empty() && text.back() == '0') {
      text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
      text.pop_back();
    }
  }
  if (text == "-0") {
    text = "0";
  }
  out = text;
}

RC VectorType::set_value_from_str(Value &val, const string &data) const
{
  vector<float> values;
  RC            rc = parse_vector_literal(data.c_str(), static_cast<int>(data.size()), values);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to parse vector literal. data=%s", data.c_str());
    return rc;
  }
  val.set_vector(values.data(), static_cast<int>(values.size()));
  return RC::SUCCESS;
}

int VectorType::compare(const Value &left, const Value &right) const
{
  if (left.attr_type() != AttrType::VECTORS || right.attr_type() != AttrType::VECTORS) {
    LOG_WARN("invalid types. left=%d, right=%d", left.attr_type(), right.attr_type());
    return INT32_MAX;
  }

  const int dim = left.vector_dim();
  if (dim != right.vector_dim()) {
    return INT32_MAX;
  }

  const float *left_data  = left.vector_data();
  const float *right_data = right.vector_data();
  for (int i = 0; i < dim; i++) {
    if (left_data[i] < right_data[i]) {
      return -1;
    }
    if (left_data[i] > right_data[i]) {
      return 1;
    }
  }
  return 0;
}

/// 逐元素算术运算，op 取 '+' '-' '*'
static RC vector_element_wise(const Value &left, const Value &right, Value &result, char op)
{
  if (left.attr_type() != AttrType::VECTORS || right.attr_type() != AttrType::VECTORS) {
    return RC::UNSUPPORTED;
  }

  const int dim = left.vector_dim();
  if (dim <= 0 || dim != right.vector_dim()) {
    return RC::UNSUPPORTED;
  }

  vector<float> values(dim);
  const float  *left_data  = left.vector_data();
  const float  *right_data = right.vector_data();
  for (int i = 0; i < dim; i++) {
    switch (op) {
      case '+': values[i] = left_data[i] + right_data[i]; break;
      case '-': values[i] = left_data[i] - right_data[i]; break;
      default: values[i] = left_data[i] * right_data[i]; break;
    }
  }
  result.set_vector(values.data(), dim);
  return RC::SUCCESS;
}

RC VectorType::add(const Value &left, const Value &right, Value &result) const
{
  return vector_element_wise(left, right, result, '+');
}

RC VectorType::subtract(const Value &left, const Value &right, Value &result) const
{
  return vector_element_wise(left, right, result, '-');
}

RC VectorType::multiply(const Value &left, const Value &right, Value &result) const
{
  return vector_element_wise(left, right, result, '*');
}

RC VectorType::cast_to(const Value &val, AttrType type, Value &result) const
{
  switch (type) {
    case AttrType::VECTORS: {
      result.set_vector(val.vector_data(), val.vector_dim());
      return RC::SUCCESS;
    }
    case AttrType::CHARS: {
      string text;
      RC     rc = to_string(val, text);
      if (OB_FAIL(rc)) {
        return rc;
      }
      result.set_string(text.data(), static_cast<int>(text.size()));
      return RC::SUCCESS;
    }
    default: return RC::UNIMPLEMENTED;
  }
}

int VectorType::cast_cost(AttrType type)
{
  if (type == AttrType::VECTORS) {
    return 0;
  }
  if (type == AttrType::CHARS) {
    return 1;
  }
  return INT32_MAX;
}

RC VectorType::to_string(const Value &val, string &result) const
{
  const int dim = val.vector_dim();
  if (dim <= 0) {
    return RC::INVALID_ARGUMENT;
  }

  const float *data = val.vector_data();
  string       text = "[";
  for (int i = 0; i < dim; i++) {
    string element;
    format_vector_element(data[i], element);
    if (i > 0) {
      text += ",";
    }
    text += element;
  }
  text += "]";
  result = text;
  return RC::SUCCESS;
}
