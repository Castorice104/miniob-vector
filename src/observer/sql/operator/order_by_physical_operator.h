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

#include "sql/expr/tuple.h"
#include "sql/operator/physical_operator.h"

/**
 * @brief 排序物理算子
 * @ingroup PhysicalOperator
 * @details 先取出子算子的全部行并算出排序键，排序后再逐行返回
 */
class OrderByPhysicalOperator : public PhysicalOperator
{
public:
  OrderByPhysicalOperator(vector<unique_ptr<Expression>> &&sort_expressions, vector<bool> &&ascending, int top_k = -1);

  virtual ~OrderByPhysicalOperator() = default;

  PhysicalOperatorType type() const override { return PhysicalOperatorType::ORDER_BY; }
  OpType               get_op_type() const override { return OpType::ORDERBY; }

  RC open(Trx *trx) override;
  RC next() override;
  RC close() override;

  Tuple *current_tuple() override;

  RC tuple_schema(TupleSchema &schema) const override;

private:
  /// 一行数据：排序键 + 行内容快照
  struct SortRow
  {
    vector<Value>  keys;
    ValueListTuple tuple;
  };

  RC materialize();
  /// 按排序键判断 left 是否应排在 right 前面
  bool row_less(const SortRow &left, const SortRow &right) const;

  vector<unique_ptr<Expression>> sort_expressions_;
  vector<bool>                   ascending_;
  int                            top_k_  = -1;
  vector<SortRow>                rows_;   ///< 需要全部排序时的结果；top_k 有效时用作候选堆
  size_t                         cursor_ = 0;
};
