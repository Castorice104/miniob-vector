/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sql/operator/order_by_physical_operator.h"

#include <algorithm>

#include "common/log/log.h"

using namespace std;

OrderByPhysicalOperator::OrderByPhysicalOperator(
    vector<unique_ptr<Expression>> &&sort_expressions, vector<bool> &&ascending, int top_k)
    : sort_expressions_(std::move(sort_expressions)), ascending_(std::move(ascending)), top_k_(top_k)
{}

RC OrderByPhysicalOperator::open(Trx *trx)
{
  if (children_.size() != 1) {
    LOG_WARN("order by operator must has one child");
    return RC::INTERNAL;
  }

  RC rc = children_[0]->open(trx);
  if (OB_FAIL(rc)) {
    LOG_WARN("failed to open child operator of order by. rc=%s", strrc(rc));
    return rc;
  }

  cursor_ = 0;
  return materialize();
}

RC OrderByPhysicalOperator::materialize()
{
  rows_.clear();

  PhysicalOperator *child = children_[0].get();
  RC                rc    = RC::SUCCESS;
  while (RC::SUCCESS == (rc = child->next())) {
    Tuple *tuple = child->current_tuple();
    if (nullptr == tuple) {
      LOG_WARN("failed to get tuple from child operator");
      return RC::INTERNAL;
    }

    SortRow row;
    row.keys.resize(sort_expressions_.size());
    for (size_t i = 0; i < sort_expressions_.size(); i++) {
      rc = sort_expressions_[i]->get_value(*tuple, row.keys[i]);
      if (OB_FAIL(rc)) {
        LOG_WARN("failed to evaluate order by expression. rc=%s", strrc(rc));
        return rc;
      }
    }

    // 有 limit 时只需要前 top_k 行，先用排序键淘汰，再为留下的行做深拷贝
    if (top_k_ > 0) {
      const size_t top_k = static_cast<size_t>(top_k_);
      if (rows_.size() >= top_k && !row_less(row, rows_.front())) {
        continue;
      }

      rc = ValueListTuple::make(*tuple, row.tuple);
      if (OB_FAIL(rc)) {
        LOG_WARN("failed to materialize tuple. rc=%s", strrc(rc));
        return rc;
      }

      if (rows_.size() < top_k) {
        rows_.emplace_back(std::move(row));
        push_heap(rows_.begin(), rows_.end(), [this](const SortRow &l, const SortRow &r) { return row_less(l, r); });
      } else {
        pop_heap(rows_.begin(), rows_.end(), [this](const SortRow &l, const SortRow &r) { return row_less(l, r); });
        rows_.back() = std::move(row);
        push_heap(rows_.begin(), rows_.end(), [this](const SortRow &l, const SortRow &r) { return row_less(l, r); });
      }
      continue;
    }

    rc = ValueListTuple::make(*tuple, row.tuple);
    if (OB_FAIL(rc)) {
      LOG_WARN("failed to materialize tuple. rc=%s", strrc(rc));
      return rc;
    }
    rows_.emplace_back(std::move(row));
  }

  if (rc != RC::RECORD_EOF) {
    LOG_WARN("failed to fetch tuples from child operator. rc=%s", strrc(rc));
    return rc;
  }

  sort(rows_.begin(), rows_.end(), [this](const SortRow &left, const SortRow &right) { return row_less(left, right); });
  return RC::SUCCESS;
}

bool OrderByPhysicalOperator::row_less(const SortRow &left, const SortRow &right) const
{
  for (size_t i = 0; i < sort_expressions_.size(); i++) {
    const int cmp = left.keys[i].compare(right.keys[i]);
    if (cmp != 0) {
      return ascending_[i] ? cmp < 0 : cmp > 0;
    }
  }
  return false;
}

RC OrderByPhysicalOperator::next()
{
  if (cursor_ >= rows_.size()) {
    return RC::RECORD_EOF;
  }

  cursor_++;
  return RC::SUCCESS;
}

RC OrderByPhysicalOperator::close()
{
  rows_.clear();
  cursor_ = 0;
  return children_[0]->close();
}

Tuple *OrderByPhysicalOperator::current_tuple() { return &rows_[cursor_ - 1].tuple; }

RC OrderByPhysicalOperator::tuple_schema(TupleSchema &schema) const { return children_[0]->tuple_schema(schema); }
