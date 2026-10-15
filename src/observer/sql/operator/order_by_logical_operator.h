/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include "sql/operator/logical_operator.h"

/**
 * @brief 排序逻辑算子
 * @ingroup LogicalOperator
 */
class OrderByLogicalOperator : public LogicalOperator
{
public:
  OrderByLogicalOperator(vector<unique_ptr<Expression>> &&sort_expressions, vector<bool> &&ascending, int top_k)
      : sort_expressions_(std::move(sort_expressions)), ascending_(std::move(ascending)), top_k_(top_k)
  {}

  virtual ~OrderByLogicalOperator() = default;

  LogicalOperatorType type() const override { return LogicalOperatorType::ORDER_BY; }
  OpType              get_op_type() const override { return OpType::ORDERBY; }

  vector<unique_ptr<Expression>> &sort_expressions() { return sort_expressions_; }
  vector<bool>                   &ascending() { return ascending_; }
  /// 排序后只需要前 top_k 行（由 limit 下推得到），-1 表示需要全部
  int top_k() const { return top_k_; }

private:
  vector<unique_ptr<Expression>> sort_expressions_;
  vector<bool>                   ascending_;
  int                            top_k_ = -1;
};
